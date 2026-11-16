#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>
#include <ESPmDNS.h>
#include <ctype.h>
#include <string.h>

// ======================================================
// LED SETUP
// ======================================================
// V6: Das gleiche Signal wird parallel auf 4 Pins ausgegeben.
// Pins bei Bedarf hier ändern (gut geeignet: 18, 19, 21, 22, 23, 25, 26, 27, 32, 33).
// Nicht verwenden: 6-11 (Flash), 34-39 (nur Eingang), 0/2/12/15 (Boot-Pins).
#define LED_PIN_1 18
#define LED_PIN_2 19
#define LED_PIN_3 21
#define LED_PIN_4 22

#define NUM_LEDS 180          // LEDs pro Ausgang
#define MAX_BRIGHTNESS 255
#define NUM_MODES 10          // Effekte 0..9

// Zeichenfläche: Alle Effekte werden hier mit VOLLER Helligkeit gezeichnet.
// Dieser Puffer ist an keinen Pin angeschlossen (Pin -1) und wird nie direkt gesendet.
// Vorteil: Die Helligkeit wird erst beim Ausgeben angewendet, dadurch gehen beim
// Abdunkeln (Comet, Twinkle, Knight Rider) keine Farbinformationen mehr verloren.
Adafruit_NeoPixel strip(NUM_LEDS, -1, NEO_GRB + NEO_KHZ800);

// Die 4 echten Ausgänge
Adafruit_NeoPixel out1(NUM_LEDS, LED_PIN_1, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel out2(NUM_LEDS, LED_PIN_2, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel out3(NUM_LEDS, LED_PIN_3, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel out4(NUM_LEDS, LED_PIN_4, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel* outputs[] = { &out1, &out2, &out3, &out4 };
const uint8_t NUM_OUTPUTS = sizeof(outputs) / sizeof(outputs[0]);

WebServer server(80);

// ======================================================
// WLAN
// ======================================================
const char* ssid = "ESP32-LED";
const char* password = "12345678";   // Tipp: eigenes Passwort (mind. 8 Zeichen) verwenden

// ======================================================
// EINSTELLUNGEN
// ======================================================
int currentMode = 0;
int brightnessPercent = 40;
int speedPercent = 50;
int sizePercent = 50;
uint8_t selectedR = 255;
uint8_t selectedG = 0;
uint8_t selectedB = 0;

unsigned long lastFrame = 0;
unsigned long lastStaticRefresh = 0;
bool needsRedraw = true;      // für AUS und FARBE: nur neu senden, wenn sich etwas ändert

uint16_t rainbowHue = 0;
float breathePhase = 0;
float wavePhase = 0;
int chaseOffset = 0;
int cometPos = 0;
int knightPos = 0;
int knightDirection = 1;
uint8_t heat[NUM_LEDS];

// ======================================================
// WEBSEITE
// ======================================================
const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>LED Controller</title>
  <style>
    * { box-sizing: border-box; }
    body {
      margin: 0;
      padding: 20px;
      background: linear-gradient(160deg, #080808, #181818);
      color: white;
      font-family: Arial, Helvetica, sans-serif;
      text-align: center;
    }
    .container { max-width: 520px; margin: auto; }
    h1 { margin-bottom: 5px; }
    .subtitle { color: #999; margin-bottom: 25px; }
    .card {
      background: #1d1d1d;
      border-radius: 18px;
      padding: 18px;
      margin-bottom: 16px;
      box-shadow: 0 5px 20px rgba(0, 0, 0, .35);
    }
    .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
    button {
      border: 0;
      border-radius: 14px;
      padding: 16px 5px;
      font-size: 15px;
      font-weight: bold;
      cursor: pointer;
      background: #333;
      color: white;
    }
    button:active { transform: scale(.96); }
    button.active { box-shadow: 0 0 0 3px #1d1d1d, 0 0 0 6px white; }
    button:focus-visible, input:focus-visible, canvas:focus-visible {
      outline: 3px solid white;
      outline-offset: 5px;
    }
    .off { background: #555; }
    .rainbow { color: black; background: linear-gradient(90deg, red, orange, yellow, lime, cyan, blue, violet); }
    .fire { background: linear-gradient(90deg, #ff2000, #ff8c00); }
    .comet { background: #1261ff; }
    .knight { background: #c00000; }
    .breathe { background: #6224b6; }
    .twinkle { background: #555577; }
    .wave { background: #087f8c; }
    .rainbowchase { background: #b943cc; }
    .solid { background: white; color: black; }
    input[type=range] { width: 100%; margin-top: 14px; }
    .value { font-size: 22px; font-weight: bold; margin-top: 5px; }
    .wheel {
      position: relative;
      width: 100%;
      max-width: 300px;
      margin: 22px auto;
      user-select: none;
      -webkit-user-select: none;
    }
    #colorWheel {
      display: block;
      width: 100%;
      height: auto;
      border-radius: 50%;
      cursor: crosshair;
      touch-action: none;
    }
    #colorMarker {
      position: absolute;
      width: 22px;
      height: 22px;
      border: 3px solid white;
      border-radius: 50%;
      box-shadow: 0 0 0 2px rgba(0, 0, 0, .65), 0 2px 8px rgba(0, 0, 0, .5);
      transform: translate(-50%, -50%);
      pointer-events: none;
    }
    .colorPreview { display: flex; justify-content: center; align-items: center; gap: 12px; }
    #colorSwatch { width: 34px; height: 34px; border: 2px solid #777; border-radius: 50%; }
    #colorValue { font: bold 18px monospace; }
    .hint { color: #bbb; font-size: 14px; line-height: 1.5; }
    #connectionStatus { color: #ffb4ab; font-size: 14px; margin: 8px 0 0; }
    #connectionStatus:empty { display: none; }
    .srOnly { position: absolute; width: 1px; height: 1px; overflow: hidden; clip-path: inset(50%); }
    @media (max-width: 360px) {
      body { padding: 12px; }
      .card { padding: 16px; }
      button { font-size: 13px; }
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>💡 LED Controller</h1>
    <div class="subtitle">180 Pixel • 4 Ausgänge • ESP32</div>

    <div class="card">
      <h2>Effekte</h2>
      <div class="grid" id="modeButtons">
        <button class="off" data-mode="0">AUS</button>
        <button class="solid" data-mode="1">🎨 FARBE</button>
        <button class="rainbow" data-mode="2">🌈 RAINBOW</button>
        <button class="comet" data-mode="3">☄️ COMET</button>
        <button class="knight" data-mode="4">🔴 KNIGHT RIDER</button>
        <button class="breathe" data-mode="5">💜 BREATHING</button>
        <button class="twinkle" data-mode="6">✨ TWINKLE</button>
        <button class="fire" data-mode="7">🔥 FIRE</button>
        <button class="wave" data-mode="8">🌊 COLOR WAVE</button>
        <button class="rainbowchase" data-mode="9">🌈 RAINBOW CHASE</button>
      </div>
      <p id="connectionStatus" role="status"></p>
    </div>

    <div class="card">
      <h2>Farbkreis</h2>
      <div class="wheel">
        <canvas id="colorWheel" width="480" height="480" tabindex="0"
                role="group" aria-label="Farbkreis" aria-describedby="colorHelp keyboardHelp">
          Dein Browser muss Canvas unterstützen, um den Farbkreis anzuzeigen.
        </canvas>
        <div id="colorMarker" aria-hidden="true"></div>
      </div>
      <div class="colorPreview">
        <span id="colorSwatch" aria-hidden="true"></span>
        <output id="colorValue" aria-label="Ausgewählte Farbe">#FF0000</output>
      </div>
      <p class="hint" id="colorHelp">Antippen oder ziehen · Mitte = Weiß<br>Dunklere Töne über die Helligkeit einstellen.</p>
      <span class="srOnly" id="keyboardHelp">Pfeiltasten links/rechts ändern den Farbton, oben/unten die Sättigung. Pos1 wählt Weiß, Ende die volle Sättigung.</span>
    </div>

    <div class="card">
      <h2><label for="brightness">Helligkeit</label></h2>
      <div class="value"><span id="brightnessValue">40</span> %</div>
      <input id="brightness" type="range" min="0" max="100" value="40">
    </div>
    <div class="card">
      <h2><label for="speed">Geschwindigkeit</label></h2>
      <div class="value"><span id="speedValue">50</span> %</div>
      <input id="speed" type="range" min="1" max="100" value="50">
    </div>
    <div class="card">
      <h2><label for="size">Effektgröße / Schweif</label></h2>
      <div class="value"><span id="sizeValue">50</span> %</div>
      <input id="size" type="range" min="1" max="100" value="50">
    </div>
  </div>

  <script>
    const wheel = document.getElementById('colorWheel');
    const marker = document.getElementById('colorMarker');
    const colorSwatch = document.getElementById('colorSwatch');
    const colorValue = document.getElementById('colorValue');
    const connectionStatus = document.getElementById('connectionStatus');
    const modeButtons = document.querySelectorAll('#modeButtons button');
    const center = wheel.width / 2;
    const radius = center - 2;
    let selectedHue = 0;
    let selectedSaturation = 1;
    let activePointer = null;

    async function send(url) {
      // Ein Zeitlimit verhindert eine dauerhaft blockierte Übergabe.
      const controller = new AbortController();
      const timeout = setTimeout(() => controller.abort(), 3000);
      try {
        const response = await fetch(url, { cache: 'no-store', signal: controller.signal });
        if (!response.ok) throw new Error('HTTP ' + response.status);
        const text = await response.text();
        connectionStatus.textContent = '';
        return text;
      } catch (error) {
        connectionStatus.textContent = 'Keine Verbindung zum ESP32. WLAN prüfen und erneut auswählen.';
        console.error(error);
        return null;
      } finally {
        clearTimeout(timeout);
      }
    }

    // Warteschlange pro Einstellung: immer nur eine Anfrage gleichzeitig,
    // beim Ziehen eines Reglers zählt nur der neueste Wert.
    // (V4 hat bei jedem Reglerschritt eine Anfrage geschickt und den ESP32 überlastet.)
    const queues = {};
    function queueSend(key, url) {
      const q = queues[key] || (queues[key] = { pending: null, busy: false, timer: null });
      q.pending = url;
      if (q.timer === null && !q.busy) q.timer = setTimeout(() => flushQueue(key), 80);
    }
    async function flushQueue(key) {
      const q = queues[key];
      q.timer = null;
      if (q.busy || q.pending === null) return;
      const url = q.pending;
      q.pending = null;
      q.busy = true;
      await send(url);
      q.busy = false;
      if (q.pending !== null) q.timer = setTimeout(() => flushQueue(key), 80);
    }

    function markMode(m) {
      modeButtons.forEach(b => {
        const on = Number(b.dataset.mode) === m;
        b.classList.toggle('active', on);
        b.setAttribute('aria-pressed', on ? 'true' : 'false');
      });
    }
    function setMode(m) {
      markMode(m);
      send('/mode?value=' + m);
    }
    modeButtons.forEach(b => b.addEventListener('click', () => setMode(Number(b.dataset.mode))));

    // HSV bei voller Farbhelligkeit; die LED-Helligkeit regelt der Schieberegler.
    function hsvToRgb(hue, saturation) {
      const h = ((hue % 360) + 360) % 360 / 60;
      const x = saturation * (1 - Math.abs(h % 2 - 1));
      const m = 1 - saturation;
      let rgb;
      if (h < 1) rgb = [saturation, x, 0];
      else if (h < 2) rgb = [x, saturation, 0];
      else if (h < 3) rgb = [0, saturation, x];
      else if (h < 4) rgb = [0, x, saturation];
      else if (h < 5) rgb = [x, 0, saturation];
      else rgb = [saturation, 0, x];
      return rgb.map(v => Math.round((v + m) * 255));
    }

    // Umkehrung für den gespeicherten Zustand vom ESP32.
    function rgbToHueSat(r, g, b) {
      const max = Math.max(r, g, b), min = Math.min(r, g, b), d = max - min;
      const sat = max === 0 ? 0 : d / max;
      let hue = 0;
      if (d > 0) {
        if (max === r) hue = 60 * (((g - b) / d) % 6);
        else if (max === g) hue = 60 * ((b - r) / d + 2);
        else hue = 60 * ((r - g) / d + 4);
      }
      return [(hue + 360) % 360, sat];
    }

    function drawWheel() {
      const context = wheel.getContext('2d');
      const pixels = context.createImageData(wheel.width, wheel.height);
      for (let y = 0; y < wheel.height; y++) {
        for (let x = 0; x < wheel.width; x++) {
          const dx = x + 0.5 - center;
          const dy = center - y - 0.5;
          const distance = Math.hypot(dx, dy);
          if (distance > radius + 0.5) continue;
          const hue = (Math.atan2(dy, dx) * 180 / Math.PI + 360) % 360;
          const rgb = hsvToRgb(hue, Math.min(1, distance / radius));
          const index = (y * wheel.width + x) * 4;
          pixels.data[index] = rgb[0];
          pixels.data[index + 1] = rgb[1];
          pixels.data[index + 2] = rgb[2];
          pixels.data[index + 3] = Math.round(Math.min(1, radius + 0.5 - distance) * 255);
        }
      }
      context.putImageData(pixels, 0, 0);
    }

    function updateColor(transmit = true) {
      const rgb = hsvToRgb(selectedHue, selectedSaturation);
      const hex = '#' + rgb.map(v => v.toString(16).padStart(2, '0')).join('');
      const angle = selectedHue * Math.PI / 180;
      marker.style.left = (center + Math.cos(angle) * selectedSaturation * radius) / wheel.width * 100 + '%';
      marker.style.top = (center - Math.sin(angle) * selectedSaturation * radius) / wheel.height * 100 + '%';
      marker.style.backgroundColor = hex;
      colorSwatch.style.backgroundColor = hex;
      colorValue.textContent = hex.toUpperCase();
      wheel.setAttribute('aria-label', 'Farbkreis, ausgewählte Farbe ' + hex.toUpperCase());
      if (transmit) queueSend('color', '/color?value=' + encodeURIComponent(hex));
    }

    function chooseAt(event) {
      const rect = wheel.getBoundingClientRect();
      const dx = ((event.clientX - rect.left) / rect.width * wheel.width - center) / radius;
      const dy = (center - (event.clientY - rect.top) / rect.height * wheel.height) / radius;
      selectedSaturation = Math.min(1, Math.hypot(dx, dy));
      // Kleiner Fangbereich in der Mitte für reines Weiß per Finger.
      if (selectedSaturation < 0.025) selectedSaturation = 0;
      if (selectedSaturation > 0) selectedHue = (Math.atan2(dy, dx) * 180 / Math.PI + 360) % 360;
      updateColor();
    }

    wheel.addEventListener('pointerdown', event => {
      if (!event.isPrimary || event.button !== 0 || activePointer !== null) return;
      event.preventDefault();
      activePointer = event.pointerId;
      wheel.setPointerCapture(event.pointerId);
      wheel.focus({ preventScroll: true });
      chooseAt(event);
    });
    wheel.addEventListener('pointermove', event => {
      if (event.pointerId === activePointer) chooseAt(event);
    });
    wheel.addEventListener('pointerup', event => {
      if (event.pointerId !== activePointer) return;
      chooseAt(event);
      activePointer = null;
      if (wheel.hasPointerCapture(event.pointerId)) wheel.releasePointerCapture(event.pointerId);
    });
    function endDrag(event) {
      if (event.pointerId === activePointer) activePointer = null;
    }
    wheel.addEventListener('pointercancel', endDrag);
    wheel.addEventListener('lostpointercapture', endDrag);

    wheel.addEventListener('keydown', event => {
      switch (event.key) {
        case 'ArrowLeft': selectedHue = (selectedHue + 357) % 360; break;
        case 'ArrowRight': selectedHue = (selectedHue + 3) % 360; break;
        case 'ArrowUp': selectedSaturation = Math.min(1, selectedSaturation + 0.03); break;
        case 'ArrowDown': selectedSaturation = Math.max(0, selectedSaturation - 0.03); break;
        case 'Home': selectedSaturation = 0; break;
        case 'End': selectedSaturation = 1; break;
        default: return;
      }
      event.preventDefault();
      updateColor();
    });

    // Schieberegler
    ['brightness', 'speed', 'size'].forEach(name => {
      const input = document.getElementById(name);
      const label = document.getElementById(name + 'Value');
      input.addEventListener('input', () => {
        label.textContent = input.value;
        queueSend(name, '/' + name + '?value=' + input.value);
      });
    });

    // Aktuellen Zustand vom ESP32 laden, damit die Seite nach einem
    // Neuladen nicht falsche Standardwerte anzeigt.
    async function loadState() {
      const text = await send('/state');
      if (!text) return;
      try {
        const s = JSON.parse(text);
        for (const name of ['brightness', 'speed', 'size']) {
          document.getElementById(name).value = s[name];
          document.getElementById(name + 'Value').textContent = s[name];
        }
        markMode(s.mode);
        const hex = s.color.replace('#', '');
        const r = parseInt(hex.slice(0, 2), 16), g = parseInt(hex.slice(2, 4), 16), b = parseInt(hex.slice(4, 6), 16);
        [selectedHue, selectedSaturation] = rgbToHueSat(r, g, b);
        updateColor(false);
      } catch (error) {
        console.error(error);
      }
    }

    drawWheel();
    updateColor(false);
    markMode(0);
    loadState();
  </script>
</body>
</html>

)rawliteral";

// ======================================================
// HILFSFUNKTIONEN
// ======================================================
uint32_t selectedColor() {
  return strip.Color(selectedR, selectedG, selectedB);
}

uint8_t outputBrightness() {
  return map(brightnessPercent, 0, 100, 0, MAX_BRIGHTNESS);
}

// Bild aus der Zeichenfläche mit eingestellter Helligkeit auf alle 4 Ausgänge senden.
void showFrame() {
  Adafruit_NeoPixel* first = outputs[0];
  first->setBrightness(outputBrightness());
  for (int i = 0; i < NUM_LEDS; i++) {
    first->setPixelColor(i, strip.getPixelColor(i));   // Helligkeit wird hier angewendet
  }
  // Exakt dieselben Daten in die anderen Ausgänge kopieren (3 Bytes pro LED bei NEO_GRB)
  for (uint8_t o = 1; o < NUM_OUTPUTS; o++) {
    memcpy(outputs[o]->getPixels(), first->getPixels(), NUM_LEDS * 3);
  }
  for (uint8_t o = 0; o < NUM_OUTPUTS; o++) {
    outputs[o]->show();
  }
}

void fadeAll(uint8_t amount) {
  for (int i = 0; i < NUM_LEDS; i++) {
    uint32_t c = strip.getPixelColor(i);
    uint8_t r = (uint8_t)(c >> 16);
    uint8_t g = (uint8_t)(c >> 8);
    uint8_t b = (uint8_t)c;
    r = (r * amount) / 255;
    g = (g * amount) / 255;
    b = (b * amount) / 255;
    strip.setPixelColor(i, r, g, b);
  }
}

void resetEffectState() {
  strip.clear();
  cometPos = 0;
  knightPos = 0;
  knightDirection = 1;
  rainbowHue = 0;
  breathePhase = 0;
  wavePhase = 0;
  chaseOffset = 0;
  memset(heat, 0, sizeof(heat));
}

// Prüft "#RRGGBB" bzw. "RRGGBB" – V4 hat auch ungültige Zeichen teilweise übernommen.
bool parseHexColor(String hex, uint8_t& r, uint8_t& g, uint8_t& b) {
  hex.trim();
  if (hex.startsWith("#")) hex.remove(0, 1);
  if (hex.length() != 6) return false;
  for (int i = 0; i < 6; i++) {
    if (!isxdigit((unsigned char)hex[i])) return false;
  }
  uint32_t color = strtoul(hex.c_str(), NULL, 16);
  r = (color >> 16) & 0xFF;
  g = (color >> 8) & 0xFF;
  b = color & 0xFF;
  return true;
}

// Liest einen Zahlenwert sicher aus der Anfrage (nur Ziffern erlaubt).
bool readIntArg(int& value, int minValue, int maxValue) {
  if (!server.hasArg("value")) return false;
  String s = server.arg("value");
  s.trim();
  if (s.length() == 0 || s.length() > 4) return false;
  for (unsigned int i = 0; i < s.length(); i++) {
    if (!isdigit((unsigned char)s[i])) return false;
  }
  value = constrain(s.toInt(), minValue, maxValue);
  return true;
}

void sendOk() { server.send(200, "text/plain", "OK"); }
void sendBad() { server.send(400, "text/plain", "Ungueltiger Wert"); }

// ======================================================
// EFFEKTE (zeichnen nur in die Zeichenfläche, Ausgabe erfolgt in showFrame)
// ======================================================
void effectRainbow() {
  for (int i = 0; i < NUM_LEDS; i++) {
    uint16_t hue = rainbowHue + ((uint32_t)i * 65536 / NUM_LEDS);
    strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(hue)));
  }
  rainbowHue += 180;
}

void effectComet() {
  fadeAll(205);
  int tailLength = map(sizePercent, 1, 100, 3, 18);
  for (int t = 0; t < tailLength; t++) {
    int pos = cometPos - t;
    while (pos < 0) pos += NUM_LEDS;
    float factor = 1.0 - ((float)t / tailLength);
    strip.setPixelColor(pos, selectedR * factor, selectedG * factor, selectedB * factor);
  }
  cometPos++;
  if (cometPos >= NUM_LEDS) cometPos = 0;
}

void effectKnightRider() {
  fadeAll(185);
  int eyeSize = map(sizePercent, 1, 100, 1, 8);
  for (int i = -eyeSize; i <= eyeSize; i++) {
    int pos = knightPos + i;
    if (pos >= 0 && pos < NUM_LEDS) {
      float factor = 1.0 - (abs(i) / (float)(eyeSize + 1));
      strip.setPixelColor(pos, selectedR * factor, selectedG * factor, selectedB * factor);
    }
  }
  knightPos += knightDirection;
  if (knightPos >= NUM_LEDS - 1) {
    knightPos = NUM_LEDS - 1;
    knightDirection = -1;
  }
  if (knightPos <= 0) {
    knightPos = 0;
    knightDirection = 1;
  }
}

void effectBreathing() {
  breathePhase += 0.04;
  if (breathePhase > TWO_PI) breathePhase -= TWO_PI;
  float level = (sin(breathePhase) + 1.0) / 2.0;   // weicher Übergang
  const float minBrightness = 0.20;
  level = minBrightness + (1.0 - minBrightness) * level;
  strip.fill(strip.Color(selectedR * level, selectedG * level, selectedB * level));
}

void effectTwinkle() {
  fadeAll(220);
  int amount = map(sizePercent, 1, 100, 1, 6);
  for (int x = 0; x < amount; x++) {
    if (random(100) < 35) {
      strip.setPixelColor(random(NUM_LEDS), selectedR, selectedG, selectedB);
    }
  }
}

uint32_t heatColor(uint8_t temperature) {
  uint8_t t192 = round((temperature / 255.0) * 191);
  uint8_t heatramp = (t192 & 0x3F) << 2;
  if (t192 > 0x80) return strip.Color(255, 255, heatramp);
  if (t192 > 0x40) return strip.Color(255, heatramp, 0);
  return strip.Color(heatramp, 0, 0);
}

void effectFire() {
  // LEDs etwas abkühlen
  for (int i = 0; i < NUM_LEDS; i++) {
    int cooldown = random(0, ((55 * 10) / NUM_LEDS) + 2);
    heat[i] = (cooldown > heat[i]) ? 0 : heat[i] - cooldown;
  }
  // Hitze nach oben wandern lassen
  for (int k = NUM_LEDS - 1; k >= 2; k--) {
    heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
  }
  // Neue Funken am Anfang des Streifens
  if (random(255) < 120) {
    int y = random(7);
    int newHeat = (int)heat[y] + (int)random(160, 255);
    heat[y] = (uint8_t)min(newHeat, 255);
  }
  for (int j = 0; j < NUM_LEDS; j++) {
    strip.setPixelColor(j, heatColor(heat[j]));
  }
}

void effectColorWave() {
  wavePhase += 0.12;
  if (wavePhase > TWO_PI) wavePhase -= TWO_PI;   // verhindert Genauigkeitsverlust nach langer Laufzeit
  for (int i = 0; i < NUM_LEDS; i++) {
    float value = (sin(wavePhase + i * 0.35) + 1) / 2;
    value = 0.08 + value * 0.92;
    strip.setPixelColor(i, selectedR * value, selectedG * value, selectedB * value);
  }
}

void effectRainbowChase() {
  strip.clear();
  for (int i = 0; i < NUM_LEDS; i++) {
    if (((i + chaseOffset) % 4) < 2) {
      uint16_t hue = ((uint32_t)i * 65536 / NUM_LEDS) + rainbowHue;
      strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(hue)));
    }
  }
  chaseOffset = (chaseOffset + 1) % 4;
  rainbowHue += 300;
}

// ======================================================
// SETUP
// ======================================================
void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());   // echter Hardware-Zufall statt analogRead

  // Alle Ausgänge starten und sofort ausschalten
  for (uint8_t o = 0; o < NUM_OUTPUTS; o++) {
    outputs[o]->begin();
    outputs[o]->clear();
  }
  strip.clear();
  showFrame();

  // WLAN starten
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(ssid, password)) {
    Serial.println("FEHLER: Access Point konnte nicht gestartet werden!");
  }
  WiFi.setSleep(false);   // schnellere Reaktion der Webseite

  if (MDNS.begin("led")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS gestartet: http://led.local");
  }

  Serial.println();
  Serial.println("ESP32 LED Controller V6");
  Serial.print("WLAN: ");
  Serial.println(ssid);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.printf("Ausgaenge: GPIO %d, %d, %d, %d\n", LED_PIN_1, LED_PIN_2, LED_PIN_3, LED_PIN_4);

  // Webseite
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", webpage);
  });

  // Aktueller Zustand (für die Webseite nach dem Neuladen)
  server.on("/state", HTTP_GET, []() {
    char json[128];
    snprintf(json, sizeof(json),
             "{\"mode\":%d,\"brightness\":%d,\"speed\":%d,\"size\":%d,\"color\":\"#%02x%02x%02x\"}",
             currentMode, brightnessPercent, speedPercent, sizePercent,
             selectedR, selectedG, selectedB);
    server.send(200, "application/json", json);
  });

  // Effekt auswählen
  server.on("/mode", HTTP_GET, []() {
    int value;
    if (!readIntArg(value, 0, NUM_MODES - 1)) { sendBad(); return; }
    currentMode = value;
    resetEffectState();
    needsRedraw = true;   // AUS/FARBE sofort neu ausgeben (Fix: AUS ging in V4 nur beim 1. Mal)
    lastFrame = 0;        // Effekt sofort starten
    sendOk();
  });

  // Farbe
  server.on("/color", HTTP_GET, []() {
    uint8_t r, g, b;
    if (!server.hasArg("value") || !parseHexColor(server.arg("value"), r, g, b)) { sendBad(); return; }
    selectedR = r;
    selectedG = g;
    selectedB = b;
    needsRedraw = true;
    sendOk();
  });

  // Helligkeit
  server.on("/brightness", HTTP_GET, []() {
    int value;
    if (!readIntArg(value, 0, 100)) { sendBad(); return; }
    brightnessPercent = value;
    needsRedraw = true;
    sendOk();
  });

  // Geschwindigkeit
  server.on("/speed", HTTP_GET, []() {
    int value;
    if (!readIntArg(value, 1, 100)) { sendBad(); return; }
    speedPercent = value;
    sendOk();
  });

  // Effektgröße
  server.on("/size", HTTP_GET, []() {
    int value;
    if (!readIntArg(value, 1, 100)) { sendBad(); return; }
    sizePercent = value;
    sendOk();
  });

  server.onNotFound([]() {
    server.send(404, "text/plain", "Nicht gefunden");
  });

  server.begin();
}

// ======================================================
// LOOP
// ======================================================
void loop() {
  server.handleClient();
  unsigned long now = millis();

  // AUS und FARBE: statisches Bild.
  // Fix gegenüber V4: Dort wurde AUS durch eine static-Variable nur beim allerersten
  // Mal ausgeführt – danach blieben die LEDs beim erneuten Drücken an.
  // Jetzt wird bei jeder Änderung sofort gesendet und zusätzlich alle 2 s
  // aufgefrischt, falls eine Störung auf der Datenleitung LEDs falsch gesetzt hat.
  if (currentMode == 0 || currentMode == 1) {
    if (needsRedraw || now - lastStaticRefresh >= 2000) {
      if (currentMode == 0) strip.clear();
      else strip.fill(selectedColor());
      showFrame();
      needsRedraw = false;
      lastStaticRefresh = now;
    }
    delay(2);
    return;
  }

  // Animierte Effekte
  unsigned long interval = map(speedPercent, 1, 100, 120, 12);
  if (now - lastFrame < interval) return;
  lastFrame = now;

  switch (currentMode) {
    case 2: effectRainbow(); break;
    case 3: effectComet(); break;
    case 4: effectKnightRider(); break;
    case 5: effectBreathing(); break;
    case 6: effectTwinkle(); break;
    case 7: effectFire(); break;
    case 8: effectColorWave(); break;
    case 9: effectRainbowChase(); break;
    default: return;
  }
  showFrame();
}
