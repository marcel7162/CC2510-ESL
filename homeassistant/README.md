# Home-Assistant-Wetterdisplay (OpenEPaperLink + OpenWeatherMap)

Zeigt das Wetter von **OpenWeatherMap** auf dem PriceTag-Preisschild an — gerendert
in Home Assistant über die **OpenEPaperLink**-Integration, gefunkt über den
PriceTag-**OEPL-Shim** auf dem ESP32.

```
OpenWeatherMap ─▶ Home Assistant ─(drawcustom/JPEG)─▶ ESP32 OEPL-Shim ─(nRF24)─▶ CC2510 ─▶ E-Ink (296×152 BWR)
```

Datei: [`weather_oepl_owm.yaml`](./weather_oepl_owm.yaml) — eine HA-Automation.

## Voraussetzungen

1. **PriceTag-Gateway (OEPL-Shim)** läuft: `gateway/gateway_oepl_shim/` auf den
   ESP32-S3 geflasht, erreichbar z. B. als `pricetag-ap.local`. Der Shim meldet
   den Tag als **hwType 0x56 = 2,66" 296×152 BWR** — daher ist das Layout auf
   diese Auflösung ausgelegt.
2. **OpenEPaperLink-Integration** in HA installiert (via HACS) und auf den Shim
   als Accesspoint gezeigt. Der Tag muss dort auftauchen (Enroll per NFC-UID).
3. **OpenWeatherMap-Integration** in HA eingerichtet
   (Einstellungen → Geräte & Dienste → Integration hinzufügen → OpenWeatherMap,
   API-Key von openweathermap.org). Sie liefert eine `weather.*`-Entity.

## Einrichten

1. **Drei Werte** oben in `weather_oepl_owm.yaml` anpassen:
   - `tag_entity` — die Entity deines Tags (Einstellungen → Geräte → dein Tag →
     die `open_epaper_link.XXXXXXXXXXXX`-Entity).
   - `weather_entity` — deine OpenWeatherMap-Wetter-Entity (z. B.
     `weather.openweathermap` oder `weather.forecast_home`).
   - `city` — Anzeigename im Banner.
2. Automation anlegen: **Einstellungen → Automationen & Szenen → Neue Automation
   → 3-Punkte-Menü → „In YAML bearbeiten"**, den Inhalt der Datei einfügen,
   speichern. (Alternativ in `automations.yaml` einhängen.)
3. Testen: Automation öffnen → **„Ausführen"**. Nach dem nächsten Tag-Poll
   erscheint das Bild (Pull-Modell — der Tag holt das Update beim Aufwachen).

Danach aktualisiert die Automation alle **15 Minuten** und bei HA-Neustart.

## Layout (296×152)
- **Rote Banner-Leiste**: Ort links, Wochentag + Uhrzeit rechts.
- **Große Temperatur** (schwarz) + **Zustandstext** (rot) links.
- **„gefühlt"** klein darunter (nur wenn die Entity `apparent_temperature` hat).
- **Wetter-Icon** oben rechts (Sonne/Nacht in Rot, sonst Schwarz).
- **Hoch/Tief** (aus der Tages-Vorhersage) + **Feuchte/Wind** rechts.

## Anpassen / Fehlersuche
- **Umlaute** (ö/ü) kommen über die TTF-Schrift der Integration; falls dein
  Build sie nicht zeigt, in den `value:`-Strings durch `oe`/`ue` ersetzen.
- **Icon-Farbe** wird hier über `color:` gesetzt. Zeigt deine OEPL-Version das
  Icon nicht farbig, `color:` beim `icon`-Element in `fill:` umbenennen.
- **Hoch/Tief leer (`--`)**: Deine OWM-Entity liefert evtl. keine Tages-
  Vorhersage über `weather.get_forecasts`. Dann die beiden H/T-Textelemente
  entfernen oder auf eine Forecast-fähige Entity zeigen.
- **Weitere Elementtypen** (Fortschrittsbalken, Diagramme, Bilder) siehe die
  OEPL-Doku: `drawcustom/supported_types.md` im Integrations-Repo.
- Design per Drag-&-Drop ausprobieren: es gibt Online-Designer, die
  `drawcustom`-Payloads erzeugen (z. B. OpenEpaperGUI / odl-drawcustom-designer).
