/*
 * PriceTag Gateway - OpenEPaperLink-AP-Shim
 *
 * Emuliert nach aussen die OEPL-AP-Schnittstelle, sodass die OEPL-Home-
 * Assistant-Integration (Service drawcustom) und der ESPHome-Designer
 * (Drag&Drop) unveraendert funktionieren:
 *
 *   HA rendert JPEG (296x152) --POST /imgupload--> SHIM
 *     SHIM: JPEG -> BWR-Ebenen -> RLE -> Funk-Stream --> CC2510 -> E-Ink
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
#include <ESPmDNS.h>
#include <time.h>
#include <ESPAsyncWebServer.h>
#include <TJpg_Decoder.h>
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
// OEPL-Hardwaretyp 0x56 = "HS BWR 2.66"" 296x152 schwarz/weiss/rot -> exakter
// Match fuer das IL0373-E2266-Panel. HA rendert damit nativ in 296x152.
#define HW_TYPE 0x56
// ==================================================

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

#define SCR_W 296
#define SCR_H 152
#define HBYTES 19
#define PLANE 5624            // SCR_W * HBYTES
#define IMG_CHUNK 12

const uint8_t txAddr[5] = {0xA7, 0x5C, 0xA7, 0xAA, 0xAA};
const uint8_t rxAddr[3] = {0xA7, 0x5C, 0xA7};

RF24 radio(CE_PIN, CSN_PIN);
Preferences prefs;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

static uint8_t planeBW[PLANE];
static uint8_t planeRD[PLANE];
static uint8_t tokens[24000];

// Eingehendes JPEG von HA (296x152 BWR ist klein; 48k reicht grosszuegig).
static uint8_t jpgBuf[48000];
static volatile size_t jpgLen = 0;

// Fertig dekodiertes Bild wartet auf den naechsten Poll dieses Tags.
volatile bool g_imgPending = false;
volatile uint8_t g_imgAddr = 0;

#define MAX_ENTRIES 16
struct Entry { uint8_t uid[7]; uint8_t addr; uint8_t label[4]; };
Entry reg[MAX_ENTRIES]; uint8_t regCount = 0;
bool online[MAX_ENTRIES]; uint32_t lastFrame[MAX_ENTRIES]; uint32_t lastSeen[MAX_ENTRIES];
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
bool streamImage(uint8_t addr);
void sendFrame(uint8_t idx);
void handleReq(const uint8_t *uid, const uint8_t *label);
uint8_t approveUid(const uint8_t *uid, const uint8_t *label);
String tagMacStr(uint8_t idx);
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
void loadReg(){ size_t n=prefs.getBytes("reg",reg,sizeof(reg)); regCount=n/sizeof(Entry); for(uint8_t i=0;i<MAX_ENTRIES;i++){online[i]=false;lastFrame[i]=0;lastSeen[i]=0;updateCount[i]=0;} }

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
    for(uint8_t t=0;t<tries;t++){ if(t) streamReTx++; modeTx(); writeCmd(addr,d,n); modeRx(); if(waitAck(to,seq)){ delay(3); return true; } }
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

// Stream der bereits in planeBW/planeRD liegenden Ebenen (kein Rendern mehr).
bool streamImage(uint8_t addr) {
    int o = rleEncode(planeBW, PLANE, tokens, sizeof(tokens));
    int o2 = (o>=0) ? rleEncode(planeRD, PLANE, tokens+o, sizeof(tokens)-o) : -1;
    if (o<0 || o2<0) { Serial.println("RLE overflow!"); return false; }
    int total = o + o2;
    streamReTx = 0;
    Serial.printf("  BW=%d RD=%d total=%d RLE-Bytes, ~%d DATA-Pakete\n",
                  o, o2, total, (total + IMG_CHUNK - 1) / IMG_CHUNK);

    uint8_t d[2+IMG_CHUNK]; uint8_t s;
    s = ++txSeq; d[0] = CMD_IMG_BEGIN; d[1] = s;
    if (!sendReliable(addr, d, 2, 150, 40, s)) { Serial.println("  BEGIN kein ACK"); return false; }
    Serial.printf("  link@CC: RSSI=%ddBm LQI=%d (kleiner=besser) FREQEST=%d\n",
                  ((int8_t)lastRssi)/2 - 71, lastLqi & 0x7F, (int)(int8_t)lastFreq);

    int sent=0;
    for (int i=0;i<total; ) {
        int chunk = min(IMG_CHUNK, total-i);
        s = ++txSeq; d[0]=CMD_IMG_DATA; d[1]=s;
        memcpy(&d[2], &tokens[i], chunk);
        if (!sendReliable(addr, d, 2+chunk, 80, 60, s)) {
            Serial.printf("  DATA kein ACK @%d (seq=%d/%d, %d ReTx) -> Abbruch\n", i, s, total, streamReTx);
            return false;
        }
        i += chunk; sent++;
    }

    s = ++txSeq; d[0]=CMD_IMG_SHOW; d[1]=s;
    d[2]=(uint8_t)(g_pollInterval>>8); d[3]=(uint8_t)g_pollInterval; d[4]=g_advertChannel;
    bool ok = sendReliable(addr, d, 5, 60000, 3, s);   // + sleep(2) + channel(1)
    Serial.printf("  stream %d RLE-Bytes in %d Paketen, %d ReTx, refresh %s\n",
                  total, sent, streamReTx, ok?"OK":"FAIL");
    return ok;
}

// WLAN waehrend des Funk-Streams aus (stoert sonst den nRF24 massiv, s. Mess-
// werte im Wetter-Gateway). Danach Reconnect im Hintergrund.
void sendFrame(uint8_t idx){
    bool wasOn = (WiFi.getMode()!=WIFI_OFF);
    // Nur deauth, Zugangsdaten/feste IP NICHT loeschen -> schneller, gleicher IP.
    if(wasOn){ WiFi.disconnect(true,false); WiFi.mode(WIFI_OFF); delay(30); }
    Serial.printf("[%lu] stream -> 0x%02X (WLAN aus fuer Stream)\n", millis(), reg[idx].addr);
    bool ok = streamImage(reg[idx].addr);
    lastFrame[idx]=millis();
    if(ok) updateCount[idx]++;
    if(wasOn){ WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID,WIFI_PASS); }
}

// ---------- enroll (Auto-Freigabe: Appliance-Betrieb ohne Serial-'y') ----------
uint8_t approveUid(const uint8_t *uid, const uint8_t *label){
    int idx=findReg(uid);
    if(idx>=0) return reg[idx].addr;
    if(regCount>=MAX_ENTRIES) return 0;
    uint8_t a=nextFreeAddr(); if(!a) return 0;
    memcpy(reg[regCount].uid,uid,7); reg[regCount].addr=a; memcpy(reg[regCount].label,label,4);
    online[regCount]=false; lastFrame[regCount]=0; lastSeen[regCount]=0; updateCount[regCount]=0;
    regCount++; saveReg();
    Serial.printf("[enroll] %s -> 0x%02X (auto)\n", hexBytes(label,4,0).c_str(), a);
    return a;
}
void handleReq(const uint8_t*uid,const uint8_t*label){
    uint8_t a=approveUid(uid,label);
    if(a){ int idx=findAddr(a); replyAssign(reg[idx].uid,a); lastSeen[idx]=millis();
        if(!online[idx]){ online[idx]=true; wsPushTag(idx);
            Serial.printf("[online] %s Addr 0x%02X\n", hexBytes(reg[idx].label,4,0).c_str(), a); } }
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

// ---------- JPEG -> BWR-Ebenen ----------
bool jpgOutput(int16_t bx, int16_t by, uint16_t w, uint16_t h, uint16_t *bmp){
    for(uint16_t row=0; row<h; row++){
        int Y = by + row; if(Y<0 || Y>=SCR_H) continue;
        int p = (SCR_H-1) - Y;
        int rowbase = (p>>3);
        uint8_t mask = 0x80 >> (p & 7);
        for(uint16_t col=0; col<w; col++){
            int X = bx + col; if(X<0 || X>=SCR_W) continue;
            uint16_t px = bmp[row*w + col];
            uint8_t r = ((px>>11)&0x1F)<<3;
            uint8_t g = ((px>>5)&0x3F)<<2;
            uint8_t b = (px&0x1F)<<3;
            int idx = X*HBYTES + rowbase;
            if(r>120 && g<90 && b<90)            planeRD[idx] &= ~mask;   // rot
            else if((int)r+g+b < 240)            planeBW[idx] &= ~mask;   // schwarz
            // sonst weiss (Bit bleibt gesetzt)
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
    s += "\"hwType\":" + String(HW_TYPE) + ",";
    s += "\"RSSI\":" + String(((int8_t)lastRssi)/2 - 71) + ",";
    s += "\"LQI\":" + String(lastLqi & 0x7F) + ",";
    s += "\"batteryMv\":2800,";
    s += "\"temperature\":20,";
    s += "\"lastseen\":" + String((uint32_t)(online[idx]?now:0)) + ",";
    s += "\"nextcheckin\":" + String((uint32_t)(now+g_pollInterval)) + ",";
    s += "\"contentMode\":0,";
    s += "\"pending\":" + String(g_imgPending && g_imgAddr==reg[idx].addr ? "true":"false") + ",";
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
        Serial.printf("[ws] HA verbunden (%s)\n", c->remoteIP().toString().c_str());
        wsPushSys();
        for(uint8_t i=0;i<regCount;i++) wsPushTag(i);
    } else if(type==WS_EVT_DISCONNECT){
        Serial.println("[ws] HA getrennt");
    }
}

// --- Kanal-Agilitaet: Wechsel + Scan ---
// Nach dem Beantworten eines Polls selbst auf den angesagten Kanal wechseln.
// Reihenfolge wichtig: erst dem Tag ansagen, DANN selbst umschalten - der Tag
// kommt beim naechsten Poll dort an. Verpasst er die Ansage, faellt er (und
// via Timeout auch wir) auf Home zurueck.
void maybeSwitchChannel(){
    if(g_advertChannel==g_operatingChannel) return;
    Serial.printf("[ch] wechsle ch%u -> ch%u (Tag angesagt)\n", g_operatingChannel, g_advertChannel);
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
    // Felder kommen als POST-Params, die Bilddaten per Upload-Callback.
    server.on("/imgupload", HTTP_POST,
        [](AsyncWebServerRequest *r){
            // onRequest: kompletter Body da -> mac holen, JPEG dekodieren.
            String mac = r->hasParam("mac", true) ? r->getParam("mac", true)->value() : "";
            int idx = findByMac(mac);
            Serial.printf("[imgupload] mac=%s len=%u idx=%d\n", mac.c_str(), (unsigned)jpgLen, idx);
            if(idx<0 || jpgLen==0){ r->send(404,"text/plain","unknown mac / no image"); jpgLen=0; return; }

            memset(planeBW,0xFF,PLANE); memset(planeRD,0xFF,PLANE);
            TJpgDec.setJpgScale(1);
            TJpgDec.setCallback(jpgOutput);
            uint16_t jw=0, jh=0; TJpgDec.getJpgSize(&jw,&jh,jpgBuf,jpgLen);
            JRESULT jr = TJpgDec.drawJpg(0,0, jpgBuf, jpgLen);
            jpgLen=0;
            if(jr!=JDR_OK){ Serial.printf("[imgupload] JPEG-Decode fehlgeschlagen (%d, %ux%u)\n", jr, jw, jh); r->send(500,"text/plain","jpeg decode failed"); return; }
            Serial.printf("[imgupload] JPEG %ux%u dekodiert -> wartet auf Poll von 0x%02X\n", jw, jh, reg[idx].addr);
            g_imgAddr = reg[idx].addr; g_imgPending = true;
            r->send(200,"text/plain","OK");
        },
        [](AsyncWebServerRequest *r, String filename, size_t index, uint8_t *data, size_t len, bool final){
            // onUpload: Bilddaten (Feld 'image') einsammeln.
            if(index==0) jpgLen=0;
            if(jpgLen + len <= sizeof(jpgBuf)){ memcpy(jpgBuf+jpgLen, data, len); jpgLen += len; }
        });

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.begin();
    Serial.println("[http] OEPL-AP-Shim laeuft auf Port 80 (/, /sysinfo, /get_db, /ws, /imgupload)");
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
            Serial.printf("WiFi verbunden: %s  (http://%s.local)\n", WiFi.localIP().toString().c_str(), MDNS_NAME);
            configTzTime(TZ_STRING,"pool.ntp.org","time.google.com");
            if(!netInit){                                // Server/mDNS nur einmal starten
                netInit = true;
                if(MDNS.begin(MDNS_NAME)) MDNS.addService("http","tcp",80);
                startServer();
            }
        }
        return;
    }
    if(wifiWasConnected){ wifiWasConnected=false; Serial.println("WiFi getrennt - versuche erneut..."); }
    if(millis() >= wifiNextTry){
        wifiNextTry = millis() + 5000;                   // alle 5 s neuer Versuch
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        Serial.println("WiFi: erneuter Verbindungsversuch...");
    }
}

void setup(){
    Serial.begin(115200);
    { uint32_t _t=millis(); while(!Serial && millis()-_t<1500) delay(10); }  // nicht ewig auf USB-Serial warten
    Serial.println("\n=== PriceTag Gateway (OpenEPaperLink-AP-Shim) ===");
    prefs.begin("pricetag",false); loadReg();
    Serial.printf("Registry: %u Tag(s)\n", regCount);
    for(uint8_t i=0;i<regCount;i++)
        Serial.printf("  0x%02X  mac=%s  label=%s\n", reg[i].addr, tagMacStr(i).c_str(), hexBytes(reg[i].label,4,0).c_str());

    WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
#if USE_STATIC_IP
    if(!WiFi.config(STATIC_IP, STATIC_GW, STATIC_SN, STATIC_DNS))
        Serial.println("WARN: WiFi.config (feste IP) fehlgeschlagen");
#endif
    WiFi.begin(WIFI_SSID,WIFI_PASS); Serial.print("WiFi");
    for(int i=0;i<40 && WiFi.status()!=WL_CONNECTED;i++){ delay(250); Serial.print("."); }
    // Verbindung, Reconnect und Netz-Init (mDNS/Zeit/Server) uebernimmt
    // ensureWifi() in loop() - auch wenn der erste Versuch hier scheitert.
    Serial.println(WiFi.status()==WL_CONNECTED ? " ok" : " FEHLT (versuche im Hintergrund weiter)");

    SPI.begin(3,9,7,CSN_PIN);
    if(!radio.begin(&SPI)){ Serial.println("ERROR nRF24"); while(1) delay(1000); }
    radio.setChannel(g_operatingChannel); radio.setDataRate(RF24_250KBPS); radio.setPALevel(RF24_PA_MAX);
    radio.setPayloadSize(NRF_PAYLOAD_SIZE); radio.setAutoAck(false); radio.setRetries(0,0);
    radio.disableDynamicPayloads(); radio.disableCRC(); modeRx();

    Serial.println("Bereit. Tag per NFC/Boot meldet sich an (Auto-Enroll).\n");
}

uint32_t lastSysPush = 0;

void loop(){
    ensureWifi();
    ws.cleanupClients();

    // Periodischer sys-Push, damit HA den AP als online fuehrt.
    if(millis()-lastSysPush > 30000){ lastSysPush=millis(); wsPushSys(); }

    // Eingehend vom Tag: raw = [srcAddr][len][cmd...][crc].
    if(radio.available()){ uint8_t raw[NRF_PAYLOAD_SIZE]; radio.read(raw,sizeof(raw)); uint8_t len=raw[1];
        if(len>=1 && (uint8_t)(len+3)<=NRF_PAYLOAD_SIZE && crc8(raw,len+2)==raw[len+2]){
            uint8_t cmd=raw[2];
            g_lastTagContact=millis();
            if(cmd==CMD_ENROLL_REQ && len>=12){ handleReq(&raw[3],&raw[10]); }
            else if(cmd==CMD_POLL){
                uint8_t src=raw[0]; int idx=findAddr(src);
                if(idx>=0){ lastSeen[idx]=millis();
                    if(!online[idx]){ online[idx]=true; Serial.printf("[online] 0x%02X\n", src); }
                    if(g_imgPending && g_imgAddr==src){
                        Serial.printf("[%lu] poll 0x%02X -> stream (Bild von HA)\n", millis(), src);
                        g_imgPending=false;
                        sendFrame(idx);                 // IMG_SHOW traegt sleep+channel
                        wsPushTag(idx);
                    } else {
                        // NOUPDATE traegt [sleep_hi][sleep_lo][channel]
                        uint8_t d[4]={CMD_NOUPDATE,(uint8_t)(g_pollInterval>>8),(uint8_t)g_pollInterval,g_advertChannel};
                        sendCmd(src,d,4);
                        Serial.printf("[%lu] poll 0x%02X -> nix (poll=%us ch=%u)\n", millis(), src, g_pollInterval, g_advertChannel);
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
        Serial.printf("[ch] lange kein Tag auf ch%u -> zurueck auf Home ch%u\n", g_operatingChannel, HOME_CHANNEL);
        g_advertChannel=HOME_CHANNEL; g_operatingChannel=HOME_CHANNEL;
        radio.setChannel(HOME_CHANNEL); modeRx();
        g_lastTagContact=millis();
    }
}
