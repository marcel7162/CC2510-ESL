/*
 * PriceTag Gateway - OpenEPaperLink-AP-Shim
 *
 * Emuliert nach aussen die OEPL-AP-Schnittstelle, sodass die OEPL-Home-
 * Assistant-Integration (Service drawcustom) und der ESPHome-Designer
 * (Drag&Drop) unveraendert funktionieren:
 *
 *   HA rendert JPEG (296x152 oder 400x300) --POST /imgupload--> SHIM
 *     SHIM: JPEG -> BWR-Ebenen -> RLE -> Funk-Stream --> CC2510 -> E-Ink
 *
 * Unterstuetzte Displays (parallel, per Tag):
 *   - 2.66" 296x152 BWR (IL0373, hwType 0x56)
 *   - 4.2"  400x300 BWR (IL0398, hwType 0x02)
 *   - 9.7"  960x672 BWR (Pervasive E2969JS08, Dual-COG, hwType 0x2E)
 *
 * Der Funk-/Enroll-/Stream-Code ist 1:1 aus gateway_weather_pio uebernommen.
 * Neu ist nur das Frontend (statt Open-Meteo + Adafruit GFX): ein HTTP/WS-
 * Server, der sich wie ein OEPL-AP verhaelt, plus JPEG-Decode + Quantisierung.
 *
 * STATUS: erstes Geruest. Die JSON-Felder fuer /sysinfo, /get_db und /ws sind
 * best-effort aus dem Integrations-Quellcode abgeleitet und werden gegen eine
 * echte HA-Instanz noch feinjustiert.
 */

#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <ESPmDNS.h>
#include <time.h>
#include <ESPAsyncWebServer.h>
#include <TJpg_Decoder.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include "credentials.h"

// ===================== CONFIG =====================
#define TZ_STRING "CET-1CEST,M3.5.0,M10.5.0/3"

// Feste IP: HA muss den AP immer unter derselben Adresse erreichen. Ohne das
// bekam der ESP bei jedem WLAN-Aus/An (waehrend eines Streams) per DHCP eine
// neue IP -> HA verlor den AP. An DEIN Netz anpassen (Gateway/Subnetz)!
#define USE_STATIC_IP 0
IPAddress STATIC_IP (192,168,137,174);
IPAddress STATIC_GW (192,168,137,1);
IPAddress STATIC_SN (255,255,255,0);
IPAddress STATIC_DNS(192,168,137,1);
// Zusaetzlich per mDNS erreichbar: http://pricetag-ap.local (falls dein Netz
// mDNS routet - dann ist HA sogar IP-unabhaengig).
#define MDNS_NAME "pricetag-ap"
// ==================================================

// Display-Typen: der Tag meldet seinen Typ beim Enroll (panel_type Byte).
#define DISP_266  0    // 2.66" 296x152 BWR (IL0373)
#define DISP_42   1    // 4.2"  400x300 BWR (IL0398)
#define DISP_97   2    // 9.7"  960x672 BWR (Pervasive E2969JS08, 2 COGs)
#define DISP_COUNT 3

// OEPL-hwType pro Display-Typ (steuert welche Aufloesung HA rendert)
#define HW_TYPE_266 0x56   // "HS BWR 2.66"" 296x152
#define HW_TYPE_42  0x02   // "M2 4.2"" 400x300 (OEPL-Fallback-Dict)
#define HW_TYPE_97  0x2E   // "M3 9.7"" 960x672 BWR (OEPL tagtypes/2E.json)

// Dimensionen pro Display-Typ
#define SCR_W_266  296
#define SCR_H_266  152
#define HBYTES_266 19
#define PLANE_266  5624    // 296 * 19

#define SCR_W_42   400
#define SCR_H_42   300
#define HBYTES_42  50
#define PLANE_42   15000   // 300 * 50

// 9.7": zwei Controller (Master = linke, Slave = rechte Haelfte). Jede Ebene
// liegt im Speicher als [linke Haelfte 672x60 B][rechte Haelfte 672x60 B],
// gestreamt wird in COG-Reihenfolge: M-schwarz, M-rot, S-schwarz, S-rot.
#define SCR_W_97   960
#define SCR_H_97   672
#define HBYTES_97  60       // pro Haelfte (480 px)
#define HALF_97    40320    // 672 * 60
#define PLANE_97   80640    // 2 Haelften

struct DispGeo { uint16_t w, h; uint8_t hbytes; int plane; uint8_t hwType; const char *name; };
static const DispGeo GEO[DISP_COUNT] = {
    { SCR_W_266, SCR_H_266, HBYTES_266, PLANE_266, HW_TYPE_266, "2.66\"" },
    { SCR_W_42,  SCR_H_42,  HBYTES_42,  PLANE_42,  HW_TYPE_42,  "4.2\""  },
    { SCR_W_97,  SCR_H_97,  HBYTES_97,  PLANE_97,  HW_TYPE_97,  "9.7\""  },
};
static inline const DispGeo &geo(uint8_t d){ return GEO[d < DISP_COUNT ? d : DISP_266]; }

// WS2812 Status-LED (ESP32-S3-Zero onboard)
#define LED_PIN 21
static void led(uint8_t r, uint8_t g, uint8_t b){ neopixelWrite(LED_PIN, r, g, b); }

#define CE_PIN 1
#define CSN_PIN 2
#define NRF_CHANNEL 76
#define NRF_PAYLOAD_SIZE 32

#define ENROLL_ADDR        0x00
#define CMD_IMG_BEGIN      0x20
#define CMD_IMG_DATA       0x21
#define CMD_IMG_SHOW       0x22
#define CMD_ACK            0xF1
#define CMD_ENROLL_REQ     0xE0
#define CMD_ENROLL_ASSIGN  0xE1
#define CMD_ENROLL_PENDING 0xE2
#define CMD_PING           0xF0
#define CMD_POLL           0x23
#define CMD_NOUPDATE       0x24

#define IMG_CHUNK 12

const uint8_t txAddr[5] = {0xA7, 0x5C, 0xA7, 0xAA, 0xAA};
const uint8_t rxAddr[3] = {0xA7, 0x5C, 0xA7};

RF24 radio(CE_PIN, CSN_PIN);
Preferences prefs;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncWebSocket wsLog("/log");

// Print-Tee: schreibt auf Serial UND pusht zeilenweise an /log-WebSocket-Clients.
class LogPrint : public Print {
    char buf[256];
    uint8_t pos = 0;
    void flush() {
        if(!pos) return;
        buf[pos] = 0;
        if(wsLog.count()) wsLog.textAll(buf, pos);
        pos = 0;
    }
public:
    size_t write(uint8_t c) override {
        Serial.write(c);
        if(c == '\n' || pos >= sizeof(buf)-1) { buf[pos++]=c; flush(); }
        else buf[pos++] = c;
        return 1;
    }
    size_t write(const uint8_t *d, size_t n) override {
        for(size_t i=0;i<n;i++) write(d[i]);
        return n;
    }
};
LogPrint Log;

// Decode-Buffer: Ebenen werden pro Encode auf dem Heap angelegt (9.7" = 2 x 80 KB;
// PSRAM wird bevorzugt, falls vorhanden) und danach wieder freigegeben.
// Vorkodierte RLE-Daten liegen auf LittleFS (/img/<mac>.bin).
static uint8_t *planeBW = nullptr;
static uint8_t *planeRD = nullptr;

static void freePlanes(){
    if(planeBW){ free(planeBW); planeBW=nullptr; }
    if(planeRD){ free(planeRD); planeRD=nullptr; }
}
static bool allocPlanes(int size){
    freePlanes();
    planeBW = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(!planeBW) planeBW = (uint8_t*)malloc(size);
    planeRD = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(!planeRD) planeRD = (uint8_t*)malloc(size);
    if(!planeBW || !planeRD){
        Log.printf("[mem] %d B Ebenen nicht allokierbar (frei: %u)\n", 2*size, (unsigned)ESP.getFreeHeap());
        freePlanes();
        return false;
    }
    return true;
}

// Upload: JPEG von HA wird direkt in eine Temp-Datei geschrieben (mac ist
// erst im onRequest-Callback sicher bekannt) und dann umbenannt.
#define UPLOAD_TMP "/img/_upload.tmp"
static File uploadFile;
static volatile size_t jpgLen = 0;

// JPEG-Decode-Kontext (Geometrie + Dithering des aktuell dekodierten Displays)
static uint8_t  g_decDisp   = DISP_266;
static uint8_t  g_decDither = 0;
static uint16_t g_decScrW   = SCR_W_266;
static uint16_t g_decScrH   = SCR_H_266;
static uint8_t  g_decHbytes = HBYTES_266;

#define MAX_ENTRIES 16
struct Entry { uint8_t uid[7]; uint8_t addr; uint8_t label[4]; uint8_t disp; };
Entry reg[MAX_ENTRIES]; uint8_t regCount = 0;
bool online[MAX_ENTRIES]; uint32_t lastFrame[MAX_ENTRIES]; uint32_t lastSeen[MAX_ENTRIES];
bool tagImgPending[MAX_ENTRIES];
uint8_t tagDither[MAX_ENTRIES];
uint32_t updateCount[MAX_ENTRIES];

uint16_t streamReTx = 0;
uint8_t txSeq = 0;
uint8_t lastRssi = 0, lastLqi = 0, lastFreq = 0;

// --- Host-steuerbare Konfig; der Tag erhaelt sie in jeder Poll-Antwort ---
#define HOME_CHANNEL 76                    // 2476 MHz: Rendezvous + Fallback
uint16_t g_pollInterval     = 300;         // Sekunden bis zum naechsten Poll
uint8_t  g_operatingChannel = HOME_CHANNEL;// Kanal, auf dem das Gateway IST
uint8_t  g_advertChannel    = HOME_CHANNEL;// Kanal, den wir dem Tag ansagen
uint32_t g_lastTagContact   = 0;           // fuer den Home-Fallback

#define MAX_PENDING 8
struct Pend { uint8_t uid[7]; uint8_t label[4]; };
Pend pend[MAX_PENDING]; uint8_t pendCount = 0;

// ---------- Vorwaertsdeklarationen ----------
String hexBytes(const uint8_t *p, uint8_t n, char sep);
bool uidEq(const uint8_t *a, const uint8_t *b);
int findReg(const uint8_t *u);
int findPend(const uint8_t *u);
int findAddr(uint8_t a);
uint8_t nextFreeAddr();
void saveReg();
void loadReg();
uint8_t crc8(const uint8_t *p, uint8_t n);
uint8_t rdReg(uint8_t r);
void wrReg(uint8_t r, uint8_t v);
void crcOff();
void modeTx();
void modeRx();
void writeCmd(uint8_t dest, const uint8_t *data, uint8_t n);
bool waitAck(uint32_t to, uint8_t seq);
bool sendReliable(uint8_t addr, const uint8_t *d, uint8_t n, uint32_t to, uint8_t tries, uint8_t seq);
void sendCmd(uint8_t dest, const uint8_t *d, uint8_t n);
void replyAssign(const uint8_t *u, uint8_t a);
void replyPending(const uint8_t *u);
int rleEncode(const uint8_t *data, int len, uint8_t *out, int cap);
bool streamImage(uint8_t addr, const uint8_t *tokens, int total, uint8_t dt);
bool encodeAndStore(uint8_t idx);
void sendFrame(uint8_t idx);
void handleReq(const uint8_t *uid, const uint8_t *label, uint8_t dispType);
uint8_t approveUid(const uint8_t *uid, const uint8_t *label, uint8_t dispType);
String tagMacStr(uint8_t idx);
String imgPath(uint8_t idx);
int findByMac(const String &mac);
bool jpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bmp);
void startServer();
void maybeSwitchChannel();
int  scanChannel(uint8_t ch, int samples);
uint8_t scanBestChannel(String *report);
String buildTagJson(uint8_t idx);
void wsPushTag(uint8_t idx);
void wsPushSys();

// ---------- helpers (aus gateway_weather uebernommen) ----------
String hexBytes(const uint8_t *p, uint8_t n, char sep) {
    String s;
    for (uint8_t i = 0; i < n; i++) { if (i && sep) s += sep; if (p[i] < 16) s += '0'; s += String(p[i], HEX); }
    s.toUpperCase(); return s;
}
bool uidEq(const uint8_t *a, const uint8_t *b){ for (uint8_t i=0;i<7;i++) if(a[i]!=b[i]) return false; return true; }
int findReg(const uint8_t *u){ for(uint8_t i=0;i<regCount;i++) if(uidEq(reg[i].uid,u)) return i; return -1; }
int findPend(const uint8_t *u){ for(uint8_t i=0;i<pendCount;i++) if(uidEq(pend[i].uid,u)) return i; return -1; }
int findAddr(uint8_t a){ for(uint8_t i=0;i<regCount;i++) if(reg[i].addr==a) return i; return -1; }
uint8_t nextFreeAddr(){ for(uint16_t a=1;a<=254;a++){ bool u=false; for(uint8_t i=0;i<regCount;i++) if(reg[i].addr==a){u=true;break;} if(!u) return a; } return 0; }
void saveReg(){ prefs.putBytes("reg", reg, regCount*sizeof(Entry)); }
void loadReg(){
    size_t n=prefs.getBytes("reg",reg,sizeof(reg));
    if(n>0 && (n % sizeof(Entry)) != 0 && (n % 12) == 0){
        /* NVS-Migration: alte Entries ohne disp-Feld (12 Byte) -> neue (13 Byte).
         * Rueckwaerts lesen damit wir nichts ueberschreiben. */
        uint8_t old_count = n / 12;
        uint8_t raw[MAX_ENTRIES * 13]; memcpy(raw, reg, n);
        for(int i=old_count-1; i>=0; i--){
            memcpy(reg[i].uid, &raw[i*12], 7);
            reg[i].addr = raw[i*12+7];
            memcpy(reg[i].label, &raw[i*12+8], 4);
            reg[i].disp = DISP_266;
        }
        regCount = old_count;
        saveReg();
        Log.printf("[nvs] %u Eintraege von 12->13 Byte migriert\n", old_count);
    } else {
        regCount = n / sizeof(Entry);
    }
    for(uint8_t i=0;i<MAX_ENTRIES;i++){
        online[i]=false;lastFrame[i]=0;lastSeen[i]=0;updateCount[i]=0;tagDither[i]=0;
        tagImgPending[i] = (i < regCount) && LittleFS.exists(imgPath(i));
    }
}

uint8_t crc8(const uint8_t *p, uint8_t n){ uint8_t c=0; for(uint8_t i=0;i<n;i++){ c^=p[i]; for(uint8_t b=0;b<8;b++) c=(c&0x80)?(uint8_t)((c<<1)^0x07):(uint8_t)(c<<1);} return c; }

// ---------- nRF24 (aus gateway_weather uebernommen) ----------
uint8_t rdReg(uint8_t r){ uint8_t v; digitalWrite(CSN_PIN,LOW); SPI.transfer(r&0x1F); v=SPI.transfer(0xFF); digitalWrite(CSN_PIN,HIGH); return v; }
void wrReg(uint8_t r,uint8_t v){ digitalWrite(CSN_PIN,LOW); SPI.transfer(0x20|(r&0x1F)); SPI.transfer(v); digitalWrite(CSN_PIN,HIGH); }
void crcOff(){ wrReg(0x00, rdReg(0x00)&~0x0C); }
void modeTx(){ radio.stopListening(); radio.setAddressWidth(5); radio.openWritingPipe(txAddr); }
void modeRx(){ radio.setAddressWidth(3); radio.openReadingPipe(0,rxAddr); radio.startListening(); crcOff(); }
void writeCmd(uint8_t dest,const uint8_t*data,uint8_t n){
    uint8_t p[NRF_PAYLOAD_SIZE]; memset(p,0,sizeof(p)); p[0]=dest; p[1]=n; memcpy(&p[2],data,n);
    p[n+2]=crc8(p,n+2); radio.write(p,sizeof(p));
}
bool waitAck(uint32_t to,uint8_t seq){
    uint32_t t0=millis();
    while(millis()-t0<to){
        if(radio.available()){
            uint8_t raw[NRF_PAYLOAD_SIZE]; radio.read(raw,sizeof(raw)); uint8_t n=raw[1];
            if(n>=1 && (uint8_t)(n+3)<=NRF_PAYLOAD_SIZE && crc8(raw,n+2)==raw[n+2]
               && raw[2]==CMD_ACK && (n<3 || raw[4]==seq)){
                if(n>=6){ lastRssi=raw[5]; lastLqi=raw[6]; lastFreq=raw[7]; }
                return true;
            }
        }
    }
    return false;
}
bool sendReliable(uint8_t addr,const uint8_t*d,uint8_t n,uint32_t to,uint8_t tries,uint8_t seq){
    for(uint8_t t=0;t<tries;t++){
        if(t){ streamReTx++; if(t%20==0) delay(50); }
        modeTx(); writeCmd(addr,d,n); modeRx();
        if(waitAck(to,seq)){ delay(3); return true; }
    }
    return false;
}
void sendCmd(uint8_t dest,const uint8_t*d,uint8_t n){ modeTx(); writeCmd(dest,d,n); modeRx(); }
void replyAssign(const uint8_t*u,uint8_t a){ uint8_t d[9]={CMD_ENROLL_ASSIGN}; memcpy(&d[1],u,7); d[8]=a; for(uint8_t k=0;k<3;k++){ sendCmd(ENROLL_ADDR,d,9); delay(5);} }
void replyPending(const uint8_t*u){ uint8_t d[8]={CMD_ENROLL_PENDING}; memcpy(&d[1],u,7); for(uint8_t k=0;k<3;k++){ sendCmd(ENROLL_ADDR,d,8); delay(5);} }

int rleEncode(const uint8_t* data, int len, uint8_t* out, int cap) {
    int o=0,i=0;
    while(i<len){
        uint8_t v=data[i]; int run=1;
        while(i+run<len && data[i+run]==v && run<255) run++;
        if(o+2>cap) return -1;
        out[o++]=run; out[o++]=v; i+=run;
    }
    return o;
}

// Byte-Strom in Funk-Reihenfolge als Segmentliste (RLE laeuft ueber die
// Segmentgrenzen hinweg; der Tag zaehlt nur Bytes).
struct StreamSrc {
    const uint8_t *p[4]; int n[4]; int nseg;
    int seg, pos;
    void reset(){ seg=0; pos=0; }
    bool get(uint8_t &v){
        while(seg<nseg && pos>=n[seg]){ seg++; pos=0; }
        if(seg>=nseg) return false;
        v=p[seg][pos++]; return true;
    }
    bool peek(uint8_t &v){
        int sg=seg, ps=pos;
        while(sg<nseg && ps>=n[sg]){ sg++; ps=0; }
        if(sg>=nseg) return false;
        v=p[sg][ps]; return true;
    }
};
static void buildStreamSrc(StreamSrc &src, uint8_t dt){
    if(dt==DISP_97){
        src.p[0]=planeBW;          src.n[0]=HALF_97;   // Master schwarz
        src.p[1]=planeRD;          src.n[1]=HALF_97;   // Master rot
        src.p[2]=planeBW+HALF_97;  src.n[2]=HALF_97;   // Slave schwarz
        src.p[3]=planeRD+HALF_97;  src.n[3]=HALF_97;   // Slave rot
        src.nseg=4;
    } else {
        int ps=geo(dt).plane;
        src.p[0]=planeBW; src.n[0]=ps;
        src.p[1]=planeRD; src.n[1]=ps;
        src.nseg=2;
    }
    src.reset();
}
// Naechstes RLE-Token-Paar [run][value]; false am Ende.
static bool rleNext(StreamSrc &src, uint8_t &run, uint8_t &val){
    if(!src.get(val)) return false;
    run=1; uint8_t nx;
    while(run<255 && src.peek(nx) && nx==val){ src.get(nx); run++; }
    return true;
}

// WLAN waehrend des Funk-Streams aus (stoert sonst den nRF24 massiv, s. Mess-
// werte im Wetter-Gateway). Danach Reconnect im Hintergrund.
// RLE-Pfad fuer einen Tag: /img/<mac>.bin
String imgPath(uint8_t idx){ return "/img/" + tagMacStr(idx) + ".bin"; }

// JPEG aus FS dekodieren, RLE-kodieren (in COG-Reihenfolge), als .bin speichern.
// Wird einmalig beim Upload aufgerufen, nicht beim Senden.
bool encodeAndStore(uint8_t idx){
    String jpgPath = "/img/" + tagMacStr(idx) + ".jpg";
    uint8_t dt = reg[idx].disp;
    const DispGeo &g = geo(dt);
    g_decDisp   = dt;
    g_decDither = tagDither[idx];
    g_decScrW   = g.w;
    g_decScrH   = g.h;
    g_decHbytes = g.hbytes;

    if(!allocPlanes(g.plane)) return false;
    memset(planeBW,0xFF,g.plane); memset(planeRD,0xFF,g.plane);
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(jpgOutput);
    JRESULT jr = TJpgDec.drawFsJpg(0, 0, jpgPath.c_str(), LittleFS);
    if(jr!=JDR_OK){ Log.printf("[img] Decode fehlgeschlagen (%d)\n", jr); freePlanes(); return false; }

    StreamSrc src; buildStreamSrc(src, dt);
    int total=0; { uint8_t r,v; while(rleNext(src,r,v)) total+=2; }

    String binPath = imgPath(idx);
    File f = LittleFS.open(binPath, "w");
    if(!f){ Log.println("[img] FS write failed"); freePlanes(); return false; }
    uint8_t hdr[4] = {(uint8_t)total, (uint8_t)(total>>8), (uint8_t)(total>>16), (uint8_t)(total>>24)};
    f.write(hdr, 4);

    src.reset();
    uint8_t r,v;
    while(rleNext(src,r,v)){ f.write(r); f.write(v); }
    f.close();
    freePlanes();

    Log.printf("[img] %s (%s): %d RLE-Bytes gespeichert\n", binPath.c_str(), g.name, total);
    return true;
}

// Vorkodierte RLE-Tokens per Funk senden.
bool streamImage(uint8_t addr, const uint8_t *tokens, int total, uint8_t dt) {
    streamReTx = 0;
    Log.printf("  %d RLE-Bytes, ~%d DATA-Pakete\n", total, (total + IMG_CHUNK - 1) / IMG_CHUNK);

    uint8_t d[2+IMG_CHUNK]; uint8_t s;
    s = ++txSeq; d[0] = CMD_IMG_BEGIN; d[1] = s;
    if (!sendReliable(addr, d, 2, 200, 80, s)) { Log.println("  BEGIN kein ACK"); return false; }
    Log.printf("  link@CC: RSSI=%ddBm LQI=%d (kleiner=besser) FREQEST=%d\n",
                  ((int8_t)lastRssi)/2 - 71, lastLqi & 0x7F, (int)(int8_t)lastFreq);

    int sent=0;
    for (int i=0;i<total; ) {
        int chunk = min(IMG_CHUNK, total-i);
        s = ++txSeq; d[0]=CMD_IMG_DATA; d[1]=s;
        memcpy(&d[2], &tokens[i], chunk);
        if (!sendReliable(addr, d, 2+chunk, 80, 120, s)) {
            Log.printf("  DATA kein ACK @%d (seq=%d/%d, %d ReTx) -> Abbruch\n", i, s, total, streamReTx);
            return false;
        }
        i += chunk; sent++;
    }

    s = ++txSeq; d[0]=CMD_IMG_SHOW; d[1]=s;
    d[2]=(uint8_t)(g_pollInterval>>8); d[3]=(uint8_t)g_pollInterval; d[4]=g_advertChannel;
    // Tag ACKt SHOW erst nach dem Refresh (9.7" Spectra: Soft-Start + ~40 s)
    uint32_t showTo = (dt==DISP_97) ? 150000 : 60000;
    bool ok = sendReliable(addr, d, 5, showTo, 3, s);
    Log.printf("  stream %d RLE-Bytes in %d Paketen, %d ReTx, refresh %s\n",
                  total, sent, streamReTx, ok?"OK":"FAIL");
    return ok;
}

void sendFrame(uint8_t idx){
    String path = imgPath(idx);
    File f = LittleFS.open(path, "r");
    if(!f){ Log.printf("[img] %s nicht gefunden\n", path.c_str()); return; }
    uint8_t hdr[4];
    if(f.read(hdr, 4) != 4){ f.close(); return; }
    int total = (int)(hdr[0] | (hdr[1]<<8) | (hdr[2]<<16) | (hdr[3]<<24));
    if(total <= 0 || total > 400000){ f.close(); Log.printf("[img] ungueltige Laenge %d\n", total); return; }
    uint8_t *tokens = (uint8_t*)heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(!tokens) tokens = (uint8_t*)malloc(total);
    if(!tokens){ f.close(); Log.printf("[img] %d B Tokens nicht allokierbar\n", total); return; }
    f.read(tokens, total);
    f.close();

    led(20,0,20);  // magenta: streaming
    bool wasOn = (WiFi.status()==WL_CONNECTED || WiFi.getMode()!=WIFI_OFF);
    if(wasOn){
        WiFi.disconnect(true);
        esp_wifi_stop();
        delay(100);
    }
    Log.printf("[%lu] stream -> 0x%02X (%d RLE-Bytes, WLAN aus)\n", millis(), reg[idx].addr, total);
    bool ok = streamImage(reg[idx].addr, tokens, total, reg[idx].disp);
    free(tokens);
    lastFrame[idx]=millis();
    if(ok){
        updateCount[idx]++;
        tagImgPending[idx] = false;
        LittleFS.remove(path);
    } else {
        Log.printf("[stream] FEHLGESCHLAGEN fuer 0x%02X, Retry beim naechsten Poll\n", reg[idx].addr);
    }
    if(wasOn){
        esp_wifi_start();
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
    led(0,20,0);  // gruen: bereit
}

// ---------- enroll (Auto-Freigabe: Appliance-Betrieb ohne Serial-'y') ----------
uint8_t approveUid(const uint8_t *uid, const uint8_t *label, uint8_t dispType){
    int idx=findReg(uid);
    if(idx>=0){
        if(reg[idx].disp != dispType){ reg[idx].disp=dispType; saveReg(); }
        return reg[idx].addr;
    }
    if(regCount>=MAX_ENTRIES) return 0;
    uint8_t a=nextFreeAddr(); if(!a) return 0;
    memcpy(reg[regCount].uid,uid,7); reg[regCount].addr=a; memcpy(reg[regCount].label,label,4);
    reg[regCount].disp=dispType;
    online[regCount]=false; lastFrame[regCount]=0; lastSeen[regCount]=0; updateCount[regCount]=0;
    regCount++; saveReg();
    const char *dname = geo(dispType).name;
    Log.printf("[enroll] %s -> 0x%02X (%s, auto)\n", hexBytes(label,4,0).c_str(), a, dname);
    return a;
}
void handleReq(const uint8_t*uid,const uint8_t*label, uint8_t dispType){
    uint8_t a=approveUid(uid,label,dispType);
    if(a){ int idx=findAddr(a); replyAssign(reg[idx].uid,a); lastSeen[idx]=millis();
        if(!online[idx]){ online[idx]=true; wsPushTag(idx);
            Log.printf("[online] %s Addr 0x%02X\n", hexBytes(reg[idx].label,4,0).c_str(), a); } }
    else replyPending(uid);
}

// ---------- OEPL-MAC <-> Tag ----------
// OEPL erwartet 6/8-Byte-MAC-Hexstring; unsere NFC-UID hat 7 Byte -> auf 8
// padden. HA nutzt den String als Entity-Id und schickt ihn bei /imgupload
// zurueck, also nur konsistent sein.
String tagMacStr(uint8_t idx){ return hexBytes(reg[idx].uid,7,0) + "00"; }
int findByMac(const String &mac){
    String m=mac; m.toUpperCase();
    for(uint8_t i=0;i<regCount;i++) if(tagMacStr(i)==m) return i;
    return -1;
}

// ---------- JPEG -> BWR-Ebenen (mit Ordered Dithering) ----------
// 4x4 Bayer-Matrix fuer 17 Graustufen-Simulation auf BW-Display
static const uint8_t bayer4[4][4] = {
    {  0, 136,  34, 170 },
    { 204,  68, 238, 102 },
    {  51, 187,  17, 153 },
    { 255, 119, 221,  85 }
};

// Das Koordinaten-Mapping haengt vom Display-Typ ab:
// 2.66": 90°-Rotation (scan row = screen X, reversed column = screen Y)
// 4.2":  keine Rotation (scan row = screen Y, column = screen X)
bool jpgOutput(int16_t bx, int16_t by, uint16_t w, uint16_t h, uint16_t *bmp){
    for(uint16_t row=0; row<h; row++){
        int Y = by + row; if(Y<0 || Y>=g_decScrH) continue;
        for(uint16_t col=0; col<w; col++){
            int X = bx + col; if(X<0 || X>=g_decScrW) continue;
            uint16_t px = bmp[row*w + col];
            uint8_t r = ((px>>11)&0x1F)<<3;
            uint8_t g = ((px>>5)&0x3F)<<2;
            uint8_t b = (px&0x1F)<<3;

            int idx; uint8_t mask;
            if (g_decDisp == DISP_97) {
                // linke Haelfte -> Master-COG, rechte -> Slave-COG
                int half = (X >= SCR_W_97/2) ? 1 : 0;
                int xx = X - half * (SCR_W_97/2);
                idx = half * HALF_97 + Y * HBYTES_97 + (xx >> 3);
                mask = 0x80 >> (xx & 7);
            } else if (g_decDisp == DISP_42) {
                idx = Y * g_decHbytes + (X >> 3);
                mask = 0x80 >> (X & 7);
            } else {
                int p = (g_decScrH - 1) - Y;
                idx = X * g_decHbytes + (p >> 3);
                mask = 0x80 >> (p & 7);
            }

            if(r>120 && g<90 && b<90){
                planeRD[idx] &= ~mask;
            } else if(g_decDither) {
                uint8_t gray = (uint8_t)((r*77 + g*150 + b*29) >> 8);
                if(gray < bayer4[Y & 3][X & 3]) planeBW[idx] &= ~mask;
            } else {
                if((int)r+g+b < 240) planeBW[idx] &= ~mask;
            }
        }
    }
    return true;
}

// ---------- HTTP/WS: OEPL-AP-Emulation ----------
String buildTagJson(uint8_t idx){
    time_t now = time(nullptr);
    String s = "{";
    s += "\"mac\":\"" + tagMacStr(idx) + "\",";
    s += "\"alias\":\"" + hexBytes(reg[idx].label,4,0) + "\",";
    uint8_t hwt = geo(reg[idx].disp).hwType;
    s += "\"hwType\":" + String(hwt) + ",";
    s += "\"RSSI\":" + String(((int8_t)lastRssi)/2 - 71) + ",";
    s += "\"LQI\":" + String(lastLqi & 0x7F) + ",";
    s += "\"batteryMv\":2800,";
    s += "\"temperature\":20,";
    s += "\"lastseen\":" + String((uint32_t)(online[idx]?now:0)) + ",";
    s += "\"nextcheckin\":" + String((uint32_t)(now+g_pollInterval)) + ",";
    s += "\"contentMode\":0,";
    s += "\"pending\":" + String(tagImgPending[idx] ? "true":"false") + ",";
    s += "\"hash\":\"0\",";
    s += "\"rotate\":0,\"lut\":0,\"ch\":" + String(g_operatingChannel) + ",";
    s += "\"ver\":1,\"capabilities\":0,\"isexternal\":false,";
    s += "\"wakeupReason\":1,\"updatecount\":" + String(updateCount[idx]);
    s += "}";
    return s;
}
void wsPushTag(uint8_t idx){ if(ws.count()) ws.textAll("{\"tags\":[" + buildTagJson(idx) + "]}"); }
void wsPushSys(){
    if(!ws.count()) return;
    String s = "{\"sys\":{";
    s += "\"alias\":\"PriceTag-Shim\",";
    s += "\"env\":\"PriceTag_CC2510_Shim\",";
    s += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
    s += "\"recordcount\":" + String(regCount) + ",";
    s += "\"dbsize\":0,\"littlefsfree\":0,\"psfree\":0,";
    s += "\"rssi\":" + String(WiFi.RSSI()) + ",";
    s += "\"apstate\":1,\"runstate\":2,\"temp\":25,";
    s += "\"wifistatus\":1,\"wifissid\":\"" + String(WIFI_SSID) + "\",";
    s += "\"uptime\":" + String(millis()/1000);
    s += "}}";
    ws.textAll(s);
}

void onWsEvent(AsyncWebSocket *s, AsyncWebSocketClient *c, AwsEventType type,
               void *arg, uint8_t *data, size_t len){
    if(type==WS_EVT_CONNECT){
        Log.printf("[ws] HA verbunden (%s)\n", c->remoteIP().toString().c_str());
        wsPushSys();
        for(uint8_t i=0;i<regCount;i++) wsPushTag(i);
    } else if(type==WS_EVT_DISCONNECT){
        Log.println("[ws] HA getrennt");
    }
}

// --- Kanal-Agilitaet: Wechsel + Scan ---
// Nach dem Beantworten eines Polls selbst auf den angesagten Kanal wechseln.
// Reihenfolge wichtig: erst dem Tag ansagen, DANN selbst umschalten - der Tag
// kommt beim naechsten Poll dort an. Verpasst er die Ansage, faellt er (und
// via Timeout auch wir) auf Home zurueck.
void maybeSwitchChannel(){
    if(g_advertChannel==g_operatingChannel) return;
    Log.printf("[ch] wechsle ch%u -> ch%u (Tag angesagt)\n", g_operatingChannel, g_advertChannel);
    g_operatingChannel=g_advertChannel;
    radio.setChannel(g_operatingChannel); modeRx();
}
// Laerm auf einem Kanal messen: RX, RPD (Received Power Detector, >-64dBm)
// mehrfach abtasten; mehr Treffer = mehr Stoerung.
int scanChannel(uint8_t ch, int samples){
    radio.setChannel(ch);
    int hits=0;
    for(int i=0;i<samples;i++){
        radio.startListening();
        delayMicroseconds(200);
        if(radio.testRPD()) hits++;
        radio.stopListening();
    }
    return hits;
}
// Ruhigsten Kanal im Raster 2..82 finden (oberhalb 82 = ausserhalb ISM).
uint8_t scanBestChannel(String *report){
    uint8_t best=HOME_CHANNEL; int bestHits=1000000;
    for(uint8_t ch=2; ch<=82; ch+=2){
        int h=scanChannel(ch,128);
        if(report) *report += String(ch)+":"+String(h)+" ";
        if(h<bestHits){ bestHits=h; best=ch; }
    }
    radio.setChannel(g_operatingChannel); modeRx();   // Normalbetrieb wiederherstellen
    return best;
}

void startServer(){
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(200,"text/plain","OpenEPaperLink"); });

    // Gespeicherte RLE-Daten zum Download: /img?mac=XXXX
    server.on("/img", HTTP_GET, [](AsyncWebServerRequest *r){
        if(!r->hasParam("mac")){ r->send(400,"text/plain","need ?mac=..."); return; }
        int idx = findByMac(r->getParam("mac")->value());
        if(idx<0){ r->send(404,"text/plain","unknown mac"); return; }
        String path = imgPath(idx);
        if(!LittleFS.exists(path)){ r->send(404,"text/plain","kein Bild gespeichert (erst nach Upload, weg nach Senden)"); return; }
        File f = LittleFS.open(path, "r");
        uint8_t hdr[4]; f.read(hdr,4); f.close();
        int total = hdr[0]|(hdr[1]<<8)|(hdr[2]<<16)|(hdr[3]<<24);
        String info = "RLE pending: " + String(total) + " Bytes (BW+RD)\nFuer JPEG: HA drawcustom mit dry-run: true";
        r->send(200,"text/plain",info);
    });

    // Host-Konfig: ?poll=<sek>&ch=<1..125> (beide optional). Der Tag uebernimmt
    // die Werte bei seiner naechsten Poll-Antwort.
    server.on("/config", HTTP_GET, [](AsyncWebServerRequest *r){
        if(r->hasParam("poll")){ long v=r->getParam("poll")->value().toInt(); if(v>=10&&v<=65000) g_pollInterval=(uint16_t)v; }
        if(r->hasParam("ch")){ long c=r->getParam("ch")->value().toInt(); if(c>=1&&c<=125) g_advertChannel=(uint8_t)c; }
        String s="{\"poll\":"+String(g_pollInterval)+",\"operatingChannel\":"+String(g_operatingChannel)
                 +",\"advertChannel\":"+String(g_advertChannel)+"}";
        r->send(200,"application/json",s);
    });

    // Funk-Scan: misst alle Kanaele, meldet den ruhigsten. ?apply=1 uebernimmt
    // ihn direkt (wird dem Tag dann angesagt). Blockiert ~1-2 s.
    server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *r){
        String rep; uint8_t best=scanBestChannel(&rep);
        bool apply = r->hasParam("apply") && r->getParam("apply")->value()=="1";
        if(apply) g_advertChannel=best;
        String s="{\"best\":"+String(best)+",\"applied\":"+String(apply?"true":"false")
                 +",\"hits\":\""+rep+"\"}";
        r->send(200,"application/json",s);
    });

    // Display-Typ eines Tags manuell setzen: /settype?mac=XX&type=42 oder type=266
    server.on("/settype", HTTP_GET, [](AsyncWebServerRequest *r){
        if(!r->hasParam("mac") || !r->hasParam("type")){
            r->send(400,"text/plain","need ?mac=...&type=266|42|97"); return; }
        int idx = findByMac(r->getParam("mac")->value());
        if(idx<0){ r->send(404,"text/plain","unknown mac"); return; }
        String t = r->getParam("type")->value();
        uint8_t dt = (t=="97") ? DISP_97 : (t=="42") ? DISP_42 : DISP_266;
        reg[idx].disp = dt;  saveReg();
        const char *dname = geo(dt).name;
        Log.printf("[settype] %s -> %s\n", tagMacStr(idx).c_str(), dname);
        r->send(200,"application/json","{\"mac\":\""+tagMacStr(idx)+"\",\"disp\":\""+dname+"\"}");
        wsPushTag(idx);
    });

    server.on("/sysinfo", HTTP_GET, [](AsyncWebServerRequest *r){
        String s = "{\"env\":\"PriceTag_CC2510_Shim\",\"alias\":\"PriceTag-Shim\",";
        s += "\"heap\":" + String(ESP.getFreeHeap()) + ",\"recordcount\":" + String(regCount) + "}";
        r->send(200,"application/json",s);
    });

    server.on("/get_db", HTTP_GET, [](AsyncWebServerRequest *r){
        String s = "{\"tags\":[";
        for(uint8_t i=0;i<regCount;i++){ if(i) s += ","; s += buildTagJson(i); }
        s += "],\"continu\":0}";
        r->send(200,"application/json",s);
    });

    // /imgupload: multipart (mac, contentmode, dither, ttl, lut, image=JPEG).
    // JPEG wird sofort dekodiert + RLE-kodiert + als .bin gespeichert.
    // Beim naechsten Poll liest sendFrame() nur noch die fertigen RLE-Tokens.
    server.on("/imgupload", HTTP_POST,
        [](AsyncWebServerRequest *r){
            String mac = r->hasParam("mac", true) ? r->getParam("mac", true)->value() : "";
            int idx = findByMac(mac);
            Log.printf("[imgupload] mac=%s len=%u idx=%d\n", mac.c_str(), (unsigned)jpgLen, idx);
            if(uploadFile) uploadFile.close();
            if(idx<0 || jpgLen==0){ LittleFS.remove(UPLOAD_TMP); r->send(404,"text/plain","unknown mac / no image"); jpgLen=0; return; }

            uint8_t dither = r->hasParam("dither", true) ? r->getParam("dither", true)->value().toInt() : 0;
            tagDither[idx] = dither;

            led(0,15,20);  // cyan: decode+encode
            String jpgPath = "/img/" + tagMacStr(idx) + ".jpg";
            size_t savedLen = jpgLen;
            LittleFS.remove(jpgPath);
            if(!LittleFS.rename(UPLOAD_TMP, jpgPath)){
                Log.println("[imgupload] FS rename failed"); r->send(500,"text/plain","fs error"); jpgLen=0; return; }
            jpgLen=0;

            if(!encodeAndStore(idx)){
                LittleFS.remove(jpgPath);
                r->send(500,"text/plain","decode/encode failed");
                led(0,20,0);
                return;
            }
            LittleFS.remove(jpgPath);

            Log.printf("[imgupload] %uB JPEG -> RLE, wartet auf Poll von 0x%02X\n",
                          (unsigned)savedLen, reg[idx].addr);
            tagImgPending[idx] = true;
            led(0,20,0);
            r->send(200,"text/plain","OK");
        },
        [](AsyncWebServerRequest *r, String filename, size_t index, uint8_t *data, size_t len, bool final){
            if(index==0){
                jpgLen=0;
                if(uploadFile) uploadFile.close();
                uploadFile = LittleFS.open(UPLOAD_TMP, "w");
            }
            if(uploadFile && len) jpgLen += uploadFile.write(data, len);
            if(final && uploadFile) uploadFile.close();
        });

    // Live-Konsole: /console zeigt Log-Output im Browser.
    server.on("/console", HTTP_GET, [](AsyncWebServerRequest *r){
        r->send(200, "text/html",
            "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width'>"
            "<title>PriceTag Log</title><style>"
            "body{margin:0;background:#1a1a2e;color:#0f0;font:13px/1.4 monospace}"
            "#log{padding:8px;white-space:pre-wrap;word-break:break-all}"
            "#bar{position:sticky;top:0;background:#16213e;padding:6px 10px;border-bottom:1px solid #333;"
            "display:flex;justify-content:space-between;align-items:center;color:#aaa;font-size:12px}"
            "#bar b{color:#0f0} button{background:#0f0;color:#000;border:0;padding:4px 12px;cursor:pointer;font:inherit}"
            ".err{color:#f44} .warn{color:#fa0} .info{color:#0af}"
            "</style></head><body>"
            "<div id='bar'><span><b>PriceTag</b> Serial Console</span>"
            "<span id='st'>verbinde...</span>"
            "<button onclick='document.getElementById(\"log\").textContent=\"\"'>Clear</button></div>"
            "<div id='log'></div><script>"
            "var log=document.getElementById('log'),st=document.getElementById('st');"
            "function go(){"
            "var ws=new WebSocket('ws://'+location.host+'/log');"
            "ws.onopen=function(){st.textContent='verbunden';st.style.color='#0f0'};"
            "ws.onclose=function(){st.textContent='getrennt - reconnect...';st.style.color='#f44';"
            "setTimeout(go,2000)};"
            "ws.onmessage=function(e){"
            "var s=document.createElement('span');"
            "var t=e.data;"
            "if(t.indexOf('FEHL')>=0||t.indexOf('ERROR')>=0||t.indexOf('overflow')>=0)s.className='err';"
            "else if(t.indexOf('WARN')>=0)s.className='warn';"
            "else if(t.indexOf('[http]')>=0||t.indexOf('[ws]')>=0||t.indexOf('WiFi')>=0)s.className='info';"
            "s.textContent=t;"
            "log.appendChild(s);"
            "if(log.childNodes.length>500)log.removeChild(log.firstChild);"
            "window.scrollTo(0,document.body.scrollHeight)}"
            "} go();"
            "</script></body></html>"
        );
    });

    wsLog.onEvent([](AsyncWebSocket*,AsyncWebSocketClient*c,AwsEventType t,void*,uint8_t*,size_t){
        if(t==WS_EVT_CONNECT) Log.printf("[console] Browser verbunden (%s)\n", c->remoteIP().toString().c_str());
    });
    server.addHandler(&wsLog);

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.begin();
    Log.println("[http] OEPL-AP-Shim laeuft auf Port 80 (/, /sysinfo, /get_db, /ws, /imgupload, /settype, /img, /console)");
}

// ---------- setup / loop ----------
// --- WLAN-Auto-Reconnect: haelt die Verbindung, versucht bei Fehlversuch/Abriss
//     immer wieder neu und erledigt die Netz-Init (mDNS/Zeit/Webserver) genau
//     einmal, sobald das WLAN steht. Absichtliches WLAN-Aus (waehrend eines
//     Funk-Streams) wird respektiert. ---
static bool wifiWasConnected = false;
static uint32_t wifiNextTry = 0;
void ensureWifi(){
    static bool netInit = false;
    if(WiFi.getMode()==WIFI_OFF) return;                 // absichtlich aus -> in Ruhe lassen
    if(WiFi.status()==WL_CONNECTED){
        if(!wifiWasConnected){
            wifiWasConnected = true;
            Log.printf("WiFi verbunden: %s  (http://%s.local)\n", WiFi.localIP().toString().c_str(), MDNS_NAME);
            configTzTime(TZ_STRING,"pool.ntp.org","time.google.com");
            if(!netInit){                                // Server/mDNS nur einmal starten
                netInit = true;
                if(MDNS.begin(MDNS_NAME)) MDNS.addService("http","tcp",80);
                startServer();
            }
        }
        return;
    }
    if(wifiWasConnected){ wifiWasConnected=false; Log.println("WiFi getrennt - versuche erneut..."); }
    if(millis() >= wifiNextTry){
        wifiNextTry = millis() + 5000;
        wifi_mode_t m; esp_wifi_get_mode(&m);
        if(m == WIFI_MODE_NULL){
            esp_wifi_start();
            WiFi.mode(WIFI_STA);
        }
        WiFi.disconnect(false);
        delay(10);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        Log.println("WiFi: erneuter Verbindungsversuch...");
    }
}

void setup(){
    Serial.begin(115200);
    { uint32_t _t=millis(); while(!Serial && millis()-_t<1500) delay(10); }  // nicht ewig auf USB-Serial warten
    led(0,0,20);  // blau: Boot
    Log.println("\n=== PriceTag Gateway (OpenEPaperLink-AP-Shim) ===");
    if(!LittleFS.begin(true)) Log.println("WARN: LittleFS mount failed");
    LittleFS.mkdir("/img");
    prefs.begin("pricetag",false); loadReg();
    Log.printf("Registry: %u Tag(s)\n", regCount);
    for(uint8_t i=0;i<regCount;i++){
        const char *dname = geo(reg[i].disp).name;
        Log.printf("  0x%02X  mac=%s  label=%s  disp=%s\n", reg[i].addr, tagMacStr(i).c_str(), hexBytes(reg[i].label,4,0).c_str(), dname);
    }

    WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
#if USE_STATIC_IP
    if(!WiFi.config(STATIC_IP, STATIC_GW, STATIC_SN, STATIC_DNS))
        Log.println("WARN: WiFi.config (feste IP) fehlgeschlagen");
#endif
    led(20,15,0);  // gelb: WiFi verbinden
    WiFi.begin(WIFI_SSID,WIFI_PASS); Log.print("WiFi");
    for(int i=0;i<40 && WiFi.status()!=WL_CONNECTED;i++){ delay(250); Log.print("."); }
    // Verbindung, Reconnect und Netz-Init (mDNS/Zeit/Server) uebernimmt
    // ensureWifi() in loop() - auch wenn der erste Versuch hier scheitert.
    Log.println(WiFi.status()==WL_CONNECTED ? " ok" : " FEHLT (versuche im Hintergrund weiter)");

    SPI.begin(3,9,7,CSN_PIN);
    if(!radio.begin(&SPI)){ led(20,0,0); Log.println("ERROR nRF24"); while(1) delay(1000); }
    radio.setChannel(g_operatingChannel); radio.setDataRate(RF24_250KBPS); radio.setPALevel(RF24_PA_MAX);
    radio.setPayloadSize(NRF_PAYLOAD_SIZE); radio.setAutoAck(false); radio.setRetries(0,0);
    radio.disableDynamicPayloads(); radio.disableCRC(); modeRx();

    led(0,20,0);  // gruen: bereit
    Log.println("Bereit. Tag per NFC/Boot meldet sich an (Auto-Enroll).\n");
}

uint32_t lastSysPush = 0;

void loop(){
    ensureWifi();
    ws.cleanupClients();
    wsLog.cleanupClients();

    // Periodischer sys-Push, damit HA den AP als online fuehrt.
    if(millis()-lastSysPush > 30000){ lastSysPush=millis(); wsPushSys(); }

    // Eingehend vom Tag: raw = [srcAddr][len][cmd...][crc].
    if(radio.available()){ uint8_t raw[NRF_PAYLOAD_SIZE]; radio.read(raw,sizeof(raw)); uint8_t len=raw[1];
        if(len>=1 && (uint8_t)(len+3)<=NRF_PAYLOAD_SIZE && crc8(raw,len+2)==raw[len+2]){
            uint8_t cmd=raw[2];
            g_lastTagContact=millis();
            if(cmd==CMD_ENROLL_REQ && len>=12){
                uint8_t ptype = (len >= 13 && raw[14] < DISP_COUNT) ? raw[14] : DISP_266;
                handleReq(&raw[3],&raw[10], ptype);
            }
            else if(cmd==CMD_POLL){
                uint8_t src=raw[0]; int idx=findAddr(src);
                if(idx>=0){ lastSeen[idx]=millis();
                    if(!online[idx]){ online[idx]=true; Log.printf("[online] 0x%02X\n", src); }
                    if(tagImgPending[idx]){
                        Log.printf("[%lu] poll 0x%02X -> stream (Bild von HA)\n", millis(), src);
                        sendFrame(idx);
                        wsPushTag(idx);
                    } else {
                        // NOUPDATE traegt [sleep_hi][sleep_lo][channel]
                        uint8_t d[4]={CMD_NOUPDATE,(uint8_t)(g_pollInterval>>8),(uint8_t)g_pollInterval,g_advertChannel};
                        sendCmd(src,d,4);
                        Log.printf("[%lu] poll 0x%02X -> nix (poll=%us ch=%u)\n", millis(), src, g_pollInterval, g_advertChannel);
                    }
                    maybeSwitchChannel();   // nach der Antwort ggf. selbst auf den neuen Kanal
                }
            }
        }
    }

    // Home-Fallback: hoeren wir lange nichts (3x Poll-Intervall) und sind nicht
    // auf Home, ist der Tag wohl auf Home zurueckgefallen -> wir folgen.
    if(g_operatingChannel!=HOME_CHANNEL && g_lastTagContact &&
       millis()-g_lastTagContact > (uint32_t)g_pollInterval*3000UL){
        Log.printf("[ch] lange kein Tag auf ch%u -> zurueck auf Home ch%u\n", g_operatingChannel, HOME_CHANNEL);
        g_advertChannel=HOME_CHANNEL; g_operatingChannel=HOME_CHANNEL;
        radio.setChannel(HOME_CHANNEL); modeRx();
        g_lastTagContact=millis();
    }
}
