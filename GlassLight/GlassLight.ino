/*
 * ════════════════════════════════════════════════════════
 *  GlassLight v1.6
 *  Glasses-clip wearable smart light + notepad
 *  Seeed Studio XIAO ESP32-C6 + WS2812B
 * ════════════════════════════════════════════════════════
 *
 *  HARDWARE
 *  --------
 *  Board  : Seeed Studio XIAO ESP32-C6
 *  LED    : 1x WS2812B on D10 (GPIO18)
 *  Btn A  : D9  (GPIO20) -- Next color
 *  Btn B  : D8  (GPIO19) -- Previous color
 *  Btn C  : D7  (GPIO17) -- Tap: on/off | Hold 3s: WiFi config mode
 *  Power  : LiPo on BAT pins (XIAO onboard charger)
 *
 *  BATTERY SENSE WIRING
 *  ---------------------
 *    BAT+ --[ 1k ohm ]--+-- D0 (GPIO0, ADC)
 *                       |
 *                    [100 ohm]
 *                       |
 *                      GND
 *
 *  BATTERY CALIBRATION
 *  --------------------
 *  Send 'r' via Serial (115200) to print raw ADC.
 *  1. Unplug USB, fully charged  -> note raw -> set BAT_ADC_FULL
 *  2. Nearly dead                -> note raw -> set BAT_ADC_EMPTY
 *
 *  BUTTON REFERENCE
 *  -----------------
 *  Btn A (D9) : Tap       -> Next color
 *  Btn B (D8) : Tap       -> Previous color
 *  Btn C (D7) : Tap       -> Toggle LED on/off
 *             : Hold 3s   -> Enter WiFi config mode (3x blue blink, done)
 *
 *  WIFI CONFIG MODE
 *  -----------------
 *  SSID    : GlassLight-Setup  (open)
 *  Pages   :  /          Settings
 *             /control   Live LED control
 *             /notes     Persistent notepad
 *  Physical buttons work in config mode.
 *  No breathing animation -- LED stays off unless controlled.
 *
 *  NOTEPAD
 *  --------
 *  Stored in SPIFFS (/notes.txt). Survives reboots and reflashes.
 *  Max size: NOTE_MAX_BYTES. Paste prepends to top (newest first).
 *  Features: paste, copy, clear, character count, timestamps on entries.
 *
 *  POWER
 *  ------
 *  WiFi off: light sleep between ticks (~3mA idle)
 *  WiFi on : full active (~30mA + LED)
 *
 *  BATTERY WARNINGS
 *  -----------------
 *  <= 20% : 3 red flashes every 60s
 *  <= 10% : Continuous slow red pulse
 *
 *  LIBRARY REQUIRED
 *  -----------------
 *  Adafruit NeoPixel  (Arduino Library Manager)
 *
 *  BOARD SETTINGS
 *  ---------------
 *  Board     : XIAO_ESP32C6
 *  Partition : Default 4MB with spiffs  <-- IMPORTANT for notepad storage
 * ════════════════════════════════════════════════════════
 */

#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include "esp_sleep.h"

// ─────────────────────────────────────────
//  PINS
// ─────────────────────────────────────────
#define LED_PIN       18
#define BTN_NEXT      20
#define BTN_PREV      19
#define BTN_TOGGLE    17
#define BAT_ADC_PIN    0

// ─────────────────────────────────────────
//  DEFAULTS
// ─────────────────────────────────────────
#define DEFAULT_COLOR_INDEX   0
#define DEFAULT_BRIGHTNESS    191
#define DEFAULT_AUTO_OFF      0

// ─────────────────────────────────────────
//  TIMING
// ─────────────────────────────────────────
#define CONFIG_HOLD_MS        3000
#define DEBOUNCE_MS           50
#define LIGHT_SLEEP_US        20000

// ─────────────────────────────────────────
//  WIFI
// ─────────────────────────────────────────
#define AP_SSID               "GlassLight-Setup"
#define AP_PASS               ""
#define DNS_PORT              53
#define HTTP_PORT             80

// ─────────────────────────────────────────
//  BATTERY
// ─────────────────────────────────────────
#define BAT_LOW_PCT           20
#define BAT_CRITICAL_PCT      10
#define BAT_CHECK_INTERVAL    30000
#define BAT_WARN_INTERVAL     60000
#define BAT_ADC_FULL          390    // calibrate: send 'r' via serial
#define BAT_ADC_EMPTY         280

// ─────────────────────────────────────────
//  NOTEPAD
// ─────────────────────────────────────────
#define NOTES_FILE            "/notes.txt"
#define NOTE_MAX_BYTES        2097152  // 2MB max notepad size
#define NOTE_ENTRY_SEPARATOR  "\n---\n"

// ─────────────────────────────────────────
//  BRAND
// ─────────────────────────────────────────
#define BRAND_RED  "#B71723"

// ─────────────────────────────────────────
//  COLOR PALETTE
// ─────────────────────────────────────────
struct Color { const char* name; uint8_t r, g, b; };
Color palette[] = {
  { "White",      255, 255, 255 },
  { "Warm White", 255, 180,  80 },
  { "Red",        255,   0,   0 },
  { "Blue",         0,   0, 255 },
  { "Green",        0, 255,   0 },
  { "Purple",     180,   0, 255 },
  { "Cyan",         0, 255, 255 },
};
#define NUM_COLORS (sizeof(palette) / sizeof(palette[0]))
uint8_t activeColors = 0x7F;

// ─────────────────────────────────────────
//  GLOBALS
// ─────────────────────────────────────────
Adafruit_NeoPixel led(1, LED_PIN, NEO_GRB + NEO_KHZ800);
Preferences       prefs;
WebServer         server(HTTP_PORT);
DNSServer         dns;

int           colorIndex    = DEFAULT_COLOR_INDEX;
uint8_t       brightness    = DEFAULT_BRIGHTNESS;
int           autoOffMin    = DEFAULT_AUTO_OFF;
bool          ledOn         = true;
bool          configMode    = false;
unsigned long ledOnTime     = 0;

unsigned long togglePressAt   = 0;
bool          toggleHeld      = false;
unsigned long lastNextPress   = 0;
unsigned long lastPrevPress   = 0;
unsigned long lastTogglePress = 0;
bool          prevNextState   = HIGH;
bool          prevPrevState   = HIGH;
bool          prevToggleState = HIGH;

int           batPercent    = 100;
bool          batLow        = false;
bool          batCritical   = false;
unsigned long lastBatCheck  = 0;
unsigned long lastBatWarn   = 0;

// ═════════════════════════════════════════
//  BATTERY
// ═════════════════════════════════════════

int readBatteryPercent() {
  long sum = 0;
  for (int i = 0; i < 16; i++) { sum += analogRead(BAT_ADC_PIN); delay(2); }
  int raw = (int)(sum / 16);
  int pct = (int)map(raw, BAT_ADC_EMPTY, BAT_ADC_FULL, 0, 100);
  pct = constrain(pct, 0, 100);
  Serial.printf("[BAT] ADC raw: %d  ->  %d%%\n", raw, pct);
  return pct;
}

void printRawADC() {
  long sum = 0;
  for (int i = 0; i < 16; i++) { sum += analogRead(BAT_ADC_PIN); delay(2); }
  Serial.printf("[CAL] Raw ADC: %d  (FULL=%d EMPTY=%d)\n",
    (int)(sum/16), BAT_ADC_FULL, BAT_ADC_EMPTY);
}

void checkBattery() {
  if (millis() - lastBatCheck < BAT_CHECK_INTERVAL) return;
  lastBatCheck = millis();
  batPercent  = readBatteryPercent();
  batCritical = (batPercent <= BAT_CRITICAL_PCT);
  batLow      = (batPercent <= BAT_LOW_PCT);
}

void flashBatWarning(int times) {
  uint32_t saved = led.getPixelColor(0);
  for (int i = 0; i < times; i++) {
    led.setPixelColor(0, led.Color(255,0,0)); led.show(); delay(150);
    led.setPixelColor(0, 0);                  led.show(); delay(150);
  }
  led.setPixelColor(0, saved); led.show();
}

void handleBatteryWarning() {
  unsigned long now = millis();
  if (batCritical) {
    static uint8_t pulse = 0; static int8_t pdir = 1;
    static unsigned long lastPulse = 0;
    if (now - lastPulse > 20) {
      lastPulse = now;
      pulse = (uint8_t)constrain((int)pulse + pdir*4, 4, 180);
      if (pulse >= 180) pdir = -1;
      if (pulse <=   4) pdir =  1;
      led.setPixelColor(0, led.Color(pulse,0,0)); led.show();
    }
    return;
  }
  if (batLow && (now - lastBatWarn > BAT_WARN_INTERVAL)) {
    lastBatWarn = now;
    flashBatWarning(3);
  }
}

// ═════════════════════════════════════════
//  LED HELPERS
// ═════════════════════════════════════════

void applyLED() {
  if (!ledOn) { led.setPixelColor(0,0); led.show(); return; }
  Color& c = palette[colorIndex];
  led.setPixelColor(0, led.Color(
    (c.r*brightness)/255, (c.g*brightness)/255, (c.b*brightness)/255));
  led.show();
}

int nextActiveColor(int current, int dir) {
  for (int i = 1; i <= (int)NUM_COLORS; i++) {
    int idx = ((current + dir*i) % (int)NUM_COLORS + NUM_COLORS) % NUM_COLORS;
    if (activeColors & (1<<idx)) return idx;
  }
  return current;
}

void blinkLED(uint8_t r, uint8_t g, uint8_t b, int times, int ms) {
  for (int i = 0; i < times; i++) {
    led.setPixelColor(0, led.Color(r,g,b)); led.show(); delay(ms);
    led.setPixelColor(0, 0);               led.show(); delay(ms);
  }
}

// ═════════════════════════════════════════
//  PREFERENCES
// ═════════════════════════════════════════

void loadPrefs() {
  prefs.begin("glasslight", true);
  colorIndex   = prefs.getInt("color",    DEFAULT_COLOR_INDEX);
  brightness   = prefs.getUChar("bright", DEFAULT_BRIGHTNESS);
  autoOffMin   = prefs.getInt("autooff",  DEFAULT_AUTO_OFF);
  activeColors = prefs.getUChar("active", 0x7F);
  prefs.end();
  if (colorIndex < 0 || colorIndex >= (int)NUM_COLORS) colorIndex = 0;
  if (brightness < 12) brightness = DEFAULT_BRIGHTNESS;
  if (activeColors == 0) activeColors = 0x7F;
  if (!(activeColors & (1<<colorIndex))) colorIndex = nextActiveColor(colorIndex, 1);
}

void savePrefs() {
  prefs.begin("glasslight", false);
  prefs.putInt("color",    colorIndex);
  prefs.putUChar("bright", brightness);
  prefs.putInt("autooff",  autoOffMin);
  prefs.putUChar("active", activeColors);
  prefs.end();
}

// ═════════════════════════════════════════
//  NOTEPAD  (SPIFFS)
// ═════════════════════════════════════════

// Get file size without loading into RAM
size_t notesSize() {
  if (!SPIFFS.exists(NOTES_FILE)) return 0;
  File f = SPIFFS.open(NOTES_FILE, "r");
  if (!f) return 0;
  size_t sz = f.size();
  f.close();
  return sz;
}

// Prepend new entry using a temp file — never loads full content into RAM
void prependNote(const String& entry) {
  if (entry.length() == 0) return;
  String header = entry + NOTE_ENTRY_SEPARATOR;

  // Check if adding this would exceed limit; if so, we'll truncate from the end
  size_t existingSize = notesSize();
  size_t newTotal     = header.length() + existingSize;
  size_t writeLimit   = (newTotal > NOTE_MAX_BYTES) ? NOTE_MAX_BYTES : newTotal;

  // Write to temp file: header first, then existing content up to limit
  File tmp = SPIFFS.open("/notes_tmp.txt", "w");
  if (!tmp) { Serial.println("[NOTES] Temp write failed"); return; }

  size_t written = 0;

  // Write header
  for (int i = 0; i < (int)header.length() && written < writeLimit; i++) {
    tmp.write((uint8_t)header[i]);
    written++;
  }

  // Stream existing file
  if (written < writeLimit && SPIFFS.exists(NOTES_FILE)) {
    File old = SPIFFS.open(NOTES_FILE, "r");
    if (old) {
      uint8_t buf[256];
      while (old.available() && written < writeLimit) {
        size_t toRead = min((size_t)256, writeLimit - written);
        size_t got    = old.read(buf, toRead);
        tmp.write(buf, got);
        written += got;
      }
      old.close();
    }
  }
  tmp.close();

  // Replace notes file with temp
  SPIFFS.remove(NOTES_FILE);
  SPIFFS.rename("/notes_tmp.txt", NOTES_FILE);
  Serial.printf("[NOTES] Saved %d bytes\n", written);
}

void clearNotes() {
  SPIFFS.remove(NOTES_FILE);
  Serial.println("[NOTES] Cleared");
}

// HTML-escape for safe display in textarea
String htmlEscape(String s) {
  s.replace("&",  "&amp;");
  s.replace("<",  "&lt;");
  s.replace(">",  "&gt;");
  s.replace("\"", "&quot;");
  return s;
}

// ═════════════════════════════════════════
//  HTML SHELL
// ═════════════════════════════════════════

String htmlHead(const char* title) {
  return String(F("<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>")) + title + F("</title>"
    "<style>"
    "body{font-family:sans-serif;background:#111;color:#eee;"
         "max-width:440px;margin:0 auto;padding:20px}"
    "h1{color:" BRAND_RED ";font-size:1.5em;margin-bottom:2px}"
    "nav{margin-bottom:18px;border-bottom:1px solid #2a2a2a;padding-bottom:10px}"
    "nav a{color:#666;text-decoration:none;margin-right:16px;font-size:.88em;"
           "padding:4px 0;border-bottom:2px solid transparent;transition:color .2s}"
    "nav a.active{color:" BRAND_RED ";border-bottom:2px solid " BRAND_RED "}"
    "nav a:hover{color:#ccc}"
    ".sub{color:#444;font-size:.76em;margin:0 0 8px}"
    ".bat{font-size:.8em;padding:4px 9px;border-radius:5px;"
         "display:inline-block;margin-bottom:14px}"
    ".bat-ok{background:#1a3a1a;color:#00ff99}"
    ".bat-low{background:#3a2a00;color:#ffaa00}"
    ".bat-crit{background:#3a0000;color:#ff5555}"
    "label{display:block;margin-top:16px;font-size:.86em;color:#aaa}"
    "select,input[type=range],input[type=number]{"
      "width:100%;margin-top:5px;padding:8px;background:#1a1a1a;"
      "border:1px solid #2e2e2e;color:#eee;border-radius:6px;"
      "font-size:1em;box-sizing:border-box}"
    ".val{color:" BRAND_RED ";font-weight:bold}"
    ".chips{display:flex;flex-wrap:wrap;gap:7px;margin-top:8px}"
    ".chip{display:flex;align-items:center;gap:5px;background:#1a1a1a;"
      "border:1px solid #2e2e2e;border-radius:6px;padding:6px 9px;"
      "font-size:.83em;cursor:pointer}"
    ".chip input{width:14px;height:14px}"
    ".sw{width:12px;height:12px;border-radius:50%;border:1px solid #444;flex-shrink:0}"
    // Buttons
    ".btn-red{display:block;width:100%;padding:13px;background:" BRAND_RED ";"
      "color:#fff;border:none;border-radius:8px;font-size:.95em;font-weight:bold;"
      "cursor:pointer;text-align:center;text-decoration:none;box-sizing:border-box;margin-top:20px}"
    ".btn-red:active{background:#8a1118}"
    ".btn-dark{display:block;width:100%;padding:11px;background:#1e1e1e;"
      "color:#eee;border:1px solid #333;border-radius:8px;font-size:.9em;"
      "cursor:pointer;text-align:center;box-sizing:border-box;margin-top:10px}"
    ".btn-dark:active{background:#2a2a2a}"
    ".ctrl-grid{display:grid;grid-template-columns:1fr 1fr;gap:9px;margin-top:14px}"
    ".ctrl-btn{padding:14px 6px;background:#1a1a1a;border:1px solid #2e2e2e;"
      "border-radius:8px;color:#eee;font-size:.88em;cursor:pointer;width:100%;margin:0}"
    ".ctrl-btn:active{background:#262626}"
    ".active-color{border-color:" BRAND_RED " !important;color:" BRAND_RED " !important}"
    ".color-bar{height:5px;border-radius:3px;margin-top:7px}"
    ".big-toggle{font-size:1.3em;padding:16px}"
    ".row{display:flex;gap:9px;margin-top:12px}"
    ".row form{flex:1;margin:0}"
    // Notepad
    "textarea{width:100%;background:#0d0d0d;color:#ddd;border:1px solid #2e2e2e;"
      "border-radius:8px;padding:12px;font-size:.88em;font-family:monospace;"
      "resize:vertical;box-sizing:border-box;min-height:260px;line-height:1.5}"
    ".note-toolbar{display:flex;gap:8px;margin-top:12px}"
    ".note-toolbar form{flex:1;margin:0}"
    ".note-meta{font-size:.75em;color:#444;margin-top:6px;text-align:right}"
    ".toast{display:none;background:#1a3a1a;color:#00ff99;padding:8px 12px;"
      "border-radius:6px;font-size:.82em;margin-top:8px;text-align:center}"
    "</style>");
}

String batBadge() {
  String cls, lbl;
  if      (batCritical) { cls="bat-crit"; lbl="Battery: CRITICAL " +String(batPercent)+"%"; }
  else if (batLow)      { cls="bat-low";  lbl="Battery: Low "      +String(batPercent)+"%"; }
  else                  { cls="bat-ok";   lbl="Battery: "          +String(batPercent)+"%"; }
  return "<div class='bat "+cls+"'>"+lbl+"</div>";
}

String navBar(const char* active) {
  String s = "<nav>";
  s += String("<a href='/'")         + (strcmp(active,"settings")==0 ? " class='active'" : "") + ">Settings</a>";
  s += String("<a href='/control'")  + (strcmp(active,"control") ==0 ? " class='active'" : "") + ">Control</a>";
  s += String("<a href='/notes'")    + (strcmp(active,"notes")   ==0 ? " class='active'" : "") + ">Notepad</a>";
  s += "</nav>";
  return s;
}

// ═════════════════════════════════════════
//  SETTINGS PAGE
// ═════════════════════════════════════════

String buildSettingsPage() {
  String html = htmlHead("GlassLight");
  html += F("</head><body>");
  html += F("<h1>GlassLight</h1><p class='sub'>v1.6 &mdash; wearable config</p>");
  html += navBar("settings");
  html += batBadge();
  html += F("<form method='POST' action='/save'>");

  html += F("<label>Boot Color</label><select name='color'>");
  for (int i = 0; i < (int)NUM_COLORS; i++) {
    html += "<option value='"+String(i)+"'";
    if (i==colorIndex) html += " selected";
    html += ">"+String(palette[i].name)+"</option>";
  }
  html += F("</select>");

  int bpct = (brightness*100)/255;
  html += F("<label>Boot Brightness: <span class='val' id='bv'>");
  html += String(bpct);
  html += F("%</span></label><input type='range' name='brightness' min='5' max='100' value='");
  html += String(bpct);
  html += F("' oninput=\"document.getElementById('bv').textContent=this.value+'%'\">");

  html += F("<label>Auto-Off (minutes &mdash; 0 = stay on)</label>"
            "<input type='number' name='autooff' min='0' max='999' value='");
  html += String(autoOffMin);
  html += F("'>");

  html += F("<label>Colors Active in Cycle</label><div class='chips'>");
  const char* sw[] = {"#fff","#ffb450","#f00","#00f","#0f0","#b400ff","#0ff"};
  for (int i = 0; i < (int)NUM_COLORS; i++) {
    html += "<label class='chip'><input type='checkbox' name='c"+String(i)+"'";
    if (activeColors & (1<<i)) html += " checked";
    html += "><div class='sw' style='background:"+String(sw[i])+"'></div>";
    html += String(palette[i].name)+"</label>";
  }
  html += F("</div>");
  html += F("<button class='btn-red' type='submit'>Save &amp; Restart</button>");
  html += F("</form></body></html>");
  return html;
}

// ═════════════════════════════════════════
//  CONTROL PAGE
// ═════════════════════════════════════════

String buildControlPage() {
  String html = htmlHead("GlassLight Control");
  html += F("</head><body>");
  html += F("<h1>GlassLight</h1><p class='sub'>v1.6 &mdash; live control</p>");
  html += navBar("control");
  html += batBadge();

  html += F("<form method='POST' action='/ctrl'>"
    "<input type='hidden' name='action' value='toggle'>"
    "<button class='btn-red big-toggle' type='submit'>");
  html += ledOn ? "Turn OFF" : "Turn ON";
  html += F("</button></form>");

  html += F("<label style='margin-top:18px'>Color</label><div class='ctrl-grid'>");
  const char* sw[] = {"#fff","#ffb450","#f00","#00f","#0f0","#b400ff","#0ff"};
  for (int i = 0; i < (int)NUM_COLORS; i++) {
    if (!(activeColors & (1<<i))) continue;
    bool cur = (i==colorIndex && ledOn);
    html += "<form method='POST' action='/ctrl' style='margin:0'>"
            "<input type='hidden' name='action' value='color'>"
            "<input type='hidden' name='idx' value='"+String(i)+"'>"
            "<button type='submit' class='ctrl-btn"+(cur?" active-color":"")+"'>"
            "<div class='color-bar' style='background:"+String(sw[i])+"'></div>"
            +String(palette[i].name)+"</button></form>";
  }
  html += F("</div>");

  int bpct = (brightness*100)/255;
  html += F("<form method='POST' action='/ctrl'>"
    "<input type='hidden' name='action' value='bright'>"
    "<label>Brightness: <span class='val' id='bv'>");
  html += String(bpct);
  html += F("%</span></label><input type='range' name='brightness' min='5' max='100' value='");
  html += String(bpct);
  html += F("' oninput=\"document.getElementById('bv').textContent=this.value+'%'\">"
    "<button class='btn-red' type='submit'>Set Brightness</button></form>");

  html += F("<div class='row'>"
    "<form method='POST' action='/ctrl'><input type='hidden' name='action' value='prev'>"
    "<button type='submit' class='ctrl-btn'>&#9664; Prev</button></form>"
    "<form method='POST' action='/ctrl'><input type='hidden' name='action' value='next'>"
    "<button type='submit' class='ctrl-btn'>Next &#9654;</button></form></div>");

  html += F("</body></html>");
  return html;
}

// ═════════════════════════════════════════
//  NOTEPAD PAGE
// ═════════════════════════════════════════

String buildNotesPage(const char* flash = "") {
  int    nBytes = (int)notesSize();
  int    nPct   = (int)(((long)nBytes * 100) / NOTE_MAX_BYTES);

  String html = htmlHead("GlassLight Notes");
  // Clipboard JS helpers
  html += F("<script>"
    "function copyAll(){"
      "var t=document.getElementById('notepad');"
      "t.select();t.setSelectionRange(0,99999);"
      "try{document.execCommand('copy');}catch(e){navigator.clipboard.writeText(t.value);}"
      "showToast('Copied to clipboard');"
    "}"
    "function showToast(msg){"
      "var el=document.getElementById('toast');"
      "el.textContent=msg;el.style.display='block';"
      "setTimeout(function(){el.style.display='none';},2000);"
    "}"
    "</script>");
  html += F("</head><body>");
  html += F("<h1>GlassLight</h1><p class='sub'>v1.6 &mdash; notepad</p>");
  html += navBar("notes");
  html += batBadge();

  // Flash message (e.g. "Saved", "Cleared")
  if (strlen(flash) > 0) {
    html += "<div class='toast' style='display:block'>" + String(flash) + "</div>";
  }

  // Paste form — prepends to top
  html += F("<form method='POST' action='/notes/paste'>"
    "<label>Paste entry (appends to top of log)</label>"
    "<textarea name='entry' rows='4' "
      "placeholder='Paste text here then tap Add...'></textarea>"
    "<button class='btn-red' type='submit'>Add to Log</button>"
    "</form>");

  // Divider
  html += F("<div style='border-top:1px solid #222;margin:20px 0'></div>");

  // Current log — editable, wrapped in save form
  html += F("<form method='POST' action='/notes/edit'>"
    "<label>Your Log <span style=\'color:#444;font-size:.75em\'>(editable)</span></label>");
  html += "<textarea id='notepad' name='content' rows='14'>";
  if (SPIFFS.exists(NOTES_FILE)) {
    File nf = SPIFFS.open(NOTES_FILE, "r");
    if (nf) {
      char buf[129];
      while (nf.available()) {
        int got = nf.readBytes(buf, 128);
        buf[got] = '\0';
        String chunk = String(buf);
        html += htmlEscape(chunk);
      }
      nf.close();
    }
  }
  html += "</textarea>";

  // Meta / storage bar
  String barColor = (nPct > 80) ? BRAND_RED : "#00cfff";
  // Show KB for readability
  String usedStr = (nBytes > 1024) ? String(nBytes/1024)+"KB" : String(nBytes)+"B";
  html += "<div class='note-meta'>" + usedStr + " / 2MB used</div>"
          "<div style='background:#1a1a1a;border-radius:4px;height:4px;margin-top:4px'>"
          "<div style='background:" + barColor + ";width:" + String(nPct)
        + "%;height:4px;border-radius:4px;transition:width .3s'></div></div>";

  // Save edits button (inside the edit form)
  html += F("<button class='btn-red' type='submit' style='margin-top:10px'>Save Edits</button>"
    "</form>");

  // Toolbar row: Copy All | Clear All
  html += F("<div class='note-toolbar'>"
    "<div style='flex:1'>"
    "<button class='btn-dark' onclick='copyAll()'>Copy All</button>"
    "</div>"
    "<form style='flex:1;margin:0' method='POST' action='/notes/clear'"
      " onsubmit=\"return confirm('Clear all notes?')\">"
    "<button class='btn-dark' type='submit'"
      " style='color:#ff5555;border-color:#5a1a1a'>Clear All</button>"
    "</form></div>");

  html += F("<div id='toast' class='toast'></div>");
  html += F("</body></html>");
  return html;
}

// ═════════════════════════════════════════
//  HTTP HANDLERS
// ═════════════════════════════════════════

void handleRoot()    { server.send(200,"text/html; charset=UTF-8", buildSettingsPage()); }
void handleControl() { server.send(200,"text/html; charset=UTF-8", buildControlPage()); }
void handleNotes()   { server.send(200,"text/html; charset=UTF-8", buildNotesPage()); }

void handleNotesPaste() {
  if (server.hasArg("entry")) {
    String entry = server.arg("entry");
    entry.trim();
    if (entry.length() > 0) {
      prependNote(entry);
      server.send(200,"text/html; charset=UTF-8", buildNotesPage("Added!"));
      return;
    }
  }
  server.sendHeader("Location","/notes",true);
  server.send(302,"text/plain","");
}

void handleNotesClear() {
  clearNotes();
  server.send(200,"text/html; charset=UTF-8", buildNotesPage("Notes cleared."));
}

void handleNotesEdit() {
  if (server.hasArg("content")) {
    String edited = server.arg("content");
    // Stream edited content directly to SPIFFS in chunks (avoid RAM limit)
    File f = SPIFFS.open(NOTES_FILE, "w");
    if (f) {
      // Write in 256-byte chunks
      size_t total = edited.length();
      size_t written = 0;
      size_t limit = min((size_t)NOTE_MAX_BYTES, total);
      while (written < limit) {
        size_t chunk = min((size_t)256, limit - written);
        f.write((const uint8_t*)(edited.c_str() + written), chunk);
        written += chunk;
      }
      f.close();
      Serial.printf("[NOTES] Edited, saved %d bytes\n", (int)written);
      server.send(200,"text/html; charset=UTF-8", buildNotesPage("Edits saved!"));
      return;
    }
  }
  server.sendHeader("Location","/notes",true);
  server.send(302,"text/plain","");
}

void handleSave() {
  if (server.hasArg("color"))
    colorIndex = constrain(server.arg("color").toInt(), 0, (int)NUM_COLORS-1);
  if (server.hasArg("brightness"))
    brightness = (uint8_t)constrain((server.arg("brightness").toInt()*255)/100, 12, 255);
  if (server.hasArg("autooff"))
    autoOffMin = constrain(server.arg("autooff").toInt(), 0, 999);
  uint8_t na = 0;
  for (int i = 0; i < (int)NUM_COLORS; i++)
    if (server.hasArg("c"+String(i))) na |= (1<<i);
  activeColors = (na==0) ? 0x7F : na;
  savePrefs();
  server.send(200,"text/html; charset=UTF-8",
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta http-equiv='refresh' content='2;url=/'></head>"
    "<body style='background:#111;color:#00ff99;font-family:sans-serif;"
    "text-align:center;padding:50px'><h2>Saved! Restarting...</h2></body></html>");
  delay(1500);
  ESP.restart();
}

void handleCtrl() {
  if (!server.hasArg("action")) {
    server.sendHeader("Location","/control",true);
    server.send(302,"text/plain",""); return;
  }
  String action = server.arg("action");
  if (action=="toggle") {
    ledOn = !ledOn; if (ledOn) ledOnTime = millis();
  } else if (action=="color" && server.hasArg("idx")) {
    int idx = constrain(server.arg("idx").toInt(), 0, (int)NUM_COLORS-1);
    if (activeColors & (1<<idx)) { colorIndex=idx; ledOn=true; ledOnTime=millis(); }
  } else if (action=="next") {
    colorIndex = nextActiveColor(colorIndex, 1);
    if (!ledOn) { ledOn=true; ledOnTime=millis(); }
  } else if (action=="prev") {
    colorIndex = nextActiveColor(colorIndex, -1);
    if (!ledOn) { ledOn=true; ledOnTime=millis(); }
  } else if (action=="bright" && server.hasArg("brightness")) {
    brightness = (uint8_t)constrain((server.arg("brightness").toInt()*255)/100, 12, 255);
  }
  applyLED();
  server.sendHeader("Location","/control",true);
  server.send(302,"text/plain","");
}

void handleNotFound() {
  server.sendHeader("Location","http://192.168.4.1/",true);
  server.send(302,"text/plain","");
}

// ═════════════════════════════════════════
//  CONFIG MODE
// ═════════════════════════════════════════

void enterConfigMode() {
  configMode = true;
  Serial.println("[CONFIG] Entering config mode...");

  // 3x blue blink — that's it, no breathing
  blinkLED(0, 0, 160, 3, 220);

  // Restore LED to current state after blink
  applyLED();

  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("[CONFIG] AP IP: "); Serial.println(WiFi.softAPIP());

  dns.start(DNS_PORT, "*", WiFi.softAPIP());
  server.on("/",             HTTP_GET,  handleRoot);
  server.on("/",             HTTP_POST, handleSave);
  server.on("/save",         HTTP_POST, handleSave);
  server.on("/control",      HTTP_GET,  handleControl);
  server.on("/ctrl",         HTTP_POST, handleCtrl);
  server.on("/notes",        HTTP_GET,  handleNotes);
  server.on("/notes/paste",  HTTP_POST, handleNotesPaste);
  server.on("/notes/edit",   HTTP_POST, handleNotesEdit);
  server.on("/notes/clear",  HTTP_POST, handleNotesClear);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("[CONFIG] Portal up -- connect to: " + String(AP_SSID));
}

// No animation in config mode -- LED stays at whatever state it's in
void runConfigMode() {
  dns.processNextRequest();
  server.handleClient();
}

// ═════════════════════════════════════════
//  BUTTON HANDLING
// ═════════════════════════════════════════

void handleButtons() {
  unsigned long now = millis();
  bool sNext   = digitalRead(BTN_NEXT);
  bool sPrev   = digitalRead(BTN_PREV);
  bool sToggle = digitalRead(BTN_TOGGLE);

  if (prevNextState==HIGH && sNext==LOW && (now-lastNextPress>DEBOUNCE_MS)) {
    lastNextPress = now;
    colorIndex = nextActiveColor(colorIndex, 1);
    ledOn=true; ledOnTime=millis(); applyLED();
    Serial.printf("[BTN] -> %s\n", palette[colorIndex].name);
  }
  prevNextState = sNext;

  if (prevPrevState==HIGH && sPrev==LOW && (now-lastPrevPress>DEBOUNCE_MS)) {
    lastPrevPress = now;
    colorIndex = nextActiveColor(colorIndex, -1);
    ledOn=true; ledOnTime=millis(); applyLED();
    Serial.printf("[BTN] <- %s\n", palette[colorIndex].name);
  }
  prevPrevState = sPrev;

  if (prevToggleState==HIGH && sToggle==LOW) { togglePressAt=now; toggleHeld=false; }
  if (sToggle==LOW && !toggleHeld) {
    if (now-togglePressAt >= CONFIG_HOLD_MS) { toggleHeld=true; enterConfigMode(); }
  }
  if (prevToggleState==LOW && sToggle==HIGH) {
    if (!toggleHeld && (now-togglePressAt>=DEBOUNCE_MS) && (now-lastTogglePress>DEBOUNCE_MS)) {
      lastTogglePress=now;
      ledOn=!ledOn; if (ledOn) ledOnTime=millis();
      applyLED();
      Serial.println(ledOn ? "[BTN] LED ON" : "[BTN] LED OFF");
    }
    toggleHeld=false;
  }
  prevToggleState = sToggle;
}

// ═════════════════════════════════════════
//  AUTO-OFF
// ═════════════════════════════════════════

void checkAutoOff() {
  if (!ledOn || autoOffMin==0) return;
  if ((millis()-ledOnTime)/60000UL >= (unsigned long)autoOffMin) {
    ledOn=false; applyLED();
    Serial.println("[AUTO] Auto-off.");
  }
}

// ═════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n========================");
  Serial.println("  GlassLight v1.6");
  Serial.println("========================");
  Serial.println("  Send 'r' for battery calibration");

  pinMode(BTN_NEXT,    INPUT_PULLUP);
  pinMode(BTN_PREV,    INPUT_PULLUP);
  pinMode(BTN_TOGGLE,  INPUT_PULLUP);
  pinMode(BAT_ADC_PIN, INPUT);
  analogReadResolution(12);

  led.begin(); led.setBrightness(255); led.clear(); led.show();

  // Init SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("[SPIFFS] Mount failed -- notepad unavailable");
  } else {
    Serial.println("[SPIFFS] Mounted OK");
    // Show note count on boot
    Serial.printf("[NOTES] %d bytes stored\n", (int)notesSize());
  }

  loadPrefs();

  batPercent  = readBatteryPercent();
  batCritical = (batPercent <= BAT_CRITICAL_PCT);
  batLow      = (batPercent <= BAT_LOW_PCT);
  lastBatCheck = millis();

  ledOn=true; ledOnTime=millis();
  applyLED();

  Serial.printf("[BOOT] Color: %s | Brightness: %d%% | Auto-off: %dmin | Battery: %d%%\n",
    palette[colorIndex].name, (brightness*100)/255, autoOffMin, batPercent);
}

// ═════════════════════════════════════════
//  LOOP
// ═════════════════════════════════════════

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c=='r'||c=='R') printRawADC();
  }

  if (configMode) {
    runConfigMode();
    handleButtons();
    checkAutoOff();
    return;
  }

  handleButtons();
  checkAutoOff();
  checkBattery();
  if (batCritical || batLow) handleBatteryWarning();

  esp_sleep_enable_timer_wakeup(LIGHT_SLEEP_US);
  esp_light_sleep_start();
}
