/*
 * Home WiFi Signal Tester
 * Board  : ESP32-C5-KITC-A (ESPC5-32)
 * Display: SSD1306 128x64 OLED, I2C (SDA=GPIO6, SCL=GPIO7), address 0x3C
 * LED    : WS2812 NeoPixel on GPIO27 (onboard)
 *
 * Behavior:
 *   - Scans for SSID "USSKOEHLERONI" every 30 seconds
 *   - Alternates OLED between 2.4 GHz and 5 GHz readings every 10 seconds
 *   - NeoPixel: Green = Good (>-50 dBm), Yellow = Fair (-50 to -70), Red = Poor (<-70)
 *
 * Required libraries (install via Arduino Library Manager):
 *   - Adafruit NeoPixel
 *   - Adafruit SSD1306
 *   - Adafruit GFX Library
 *
 * Arduino core: esp32 by Espressif Systems v3.x
 * Board target: ESP32C5 Dev Module
 */

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

// ── Hardware config ──────────────────────────────────────────────────────────
#define NEOPIXEL_PIN    27
#define NEOPIXEL_COUNT   1
#define NEOPIXEL_BRIGHTNESS 60   // 0-255

#define OLED_SDA        6
#define OLED_SCL        7
#define OLED_ADDR       0x3C     // try 0x3D if display stays blank
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_RESET      -1       // no reset pin

// ── Network config ───────────────────────────────────────────────────────────
const char* TARGET_SSID = "USSKOEHLERONI";

// ── Signal quality thresholds (dBm) ─────────────────────────────────────────
#define RSSI_GOOD  -50   // > -50  → Good
#define RSSI_FAIR  -70   // -50 to -70 → Fair  |  < -70 → Poor

// ── Timing (ms) ─────────────────────────────────────────────────────────────
#define SCAN_INTERVAL    30000UL   // rescan WiFi every 30 s
#define DISPLAY_INTERVAL 10000UL  // flip between 2.4G / 5G every 10 s

// ── Globals ──────────────────────────────────────────────────────────────────
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
Adafruit_NeoPixel pixel(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

int rssi24 = -999;   // best RSSI found on 2.4 GHz band (-999 = not found)
int rssi5  = -999;   // best RSSI found on 5 GHz band

unsigned long lastScanMs    = 0;
unsigned long lastDisplayMs = 0;
bool showing5G = false;   // which band is currently on-screen

// ── Helpers ──────────────────────────────────────────────────────────────────

// Returns true if WiFi channel is in the 5 GHz band
bool is5GHzChannel(int channel) {
    return channel >= 36;
}

// Maps RSSI to a quality label
const char* getQuality(int rssi) {
    if (rssi == -999)    return "N/A";
    if (rssi > RSSI_GOOD) return "Good";
    if (rssi > RSSI_FAIR) return "Fair";
    return "Poor";
}

// Sets the onboard NeoPixel color based on RSSI
void setNeoPixel(int rssi) {
    uint32_t color;
    if (rssi == -999) {
        color = pixel.Color(30, 0, 0);   // dim red = not found
    } else if (rssi > RSSI_GOOD) {
        color = pixel.Color(0, NEOPIXEL_BRIGHTNESS, 0);   // green
    } else if (rssi > RSSI_FAIR) {
        color = pixel.Color(NEOPIXEL_BRIGHTNESS, NEOPIXEL_BRIGHTNESS / 2, 0); // yellow
    } else {
        color = pixel.Color(NEOPIXEL_BRIGHTNESS, 0, 0);   // red
    }
    pixel.setPixelColor(0, color);
    pixel.show();
}

// Scans for TARGET_SSID and records best RSSI per band
void scanWifi() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 24);
    display.println("  Scanning WiFi...");
    display.display();

    // Dim white while scanning
    pixel.setPixelColor(0, pixel.Color(30, 30, 30));
    pixel.show();

    int found = WiFi.scanNetworks(false, false);  // blocking scan

    rssi24 = -999;
    rssi5  = -999;

    for (int i = 0; i < found; i++) {
        if (WiFi.SSID(i) == TARGET_SSID) {
            int ch   = WiFi.channel(i);
            int rssi = WiFi.RSSI(i);

            if (is5GHzChannel(ch)) {
                if (rssi > rssi5) rssi5 = rssi;
            } else {
                if (rssi > rssi24) rssi24 = rssi;
            }
        }
    }

    WiFi.scanDelete();  // free scan memory
}

// Draws the readings for a given band on the OLED
void displayBand(bool show5G) {
    int rssi = show5G ? rssi5 : rssi24;
    const char* bandLabel = show5G ? "5 GHz" : "2.4 GHz";
    const char* quality   = getQuality(rssi);

    display.clearDisplay();

    // ── Row 0: SSID (small) ──────────────────────────────────────────────────
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print(TARGET_SSID);

    // ── Divider line ─────────────────────────────────────────────────────────
    display.drawFastHLine(0, 10, OLED_WIDTH, SSD1306_WHITE);

    // ── Row 1: Band label (large) ────────────────────────────────────────────
    display.setTextSize(2);
    display.setCursor(0, 14);
    display.print(bandLabel);

    // ── Row 2: dBm value (small) ─────────────────────────────────────────────
    display.setTextSize(1);
    display.setCursor(0, 34);
    if (rssi == -999) {
        display.print("Not found");
    } else {
        display.print(rssi);
        display.print(" dBm");
    }

    // ── Row 3: Quality label (large) ─────────────────────────────────────────
    display.setTextSize(2);
    display.setCursor(0, 46);
    display.print(quality);

    display.display();
    setNeoPixel(rssi);
}

// ── Arduino lifecycle ─────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);

    // NeoPixel
    pixel.begin();
    pixel.setBrightness(NEOPIXEL_BRIGHTNESS);
    pixel.clear();
    pixel.show();

    // OLED
    Wire.begin(OLED_SDA, OLED_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("SSD1306 init failed. Check wiring and OLED_ADDR.");
        // Blink red forever so user knows something is wrong
        while (true) {
            pixel.setPixelColor(0, pixel.Color(60, 0, 0));
            pixel.show();
            delay(300);
            pixel.clear();
            pixel.show();
            delay(300);
        }
    }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.println("  WiFi Tester v1.0");
    display.setCursor(0, 32);
    display.println("  Initializing...");
    display.display();
    delay(1000);

    // WiFi in station mode, no connection needed — scan only
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    // First scan
    scanWifi();
    lastScanMs    = millis();
    lastDisplayMs = millis();

    // Show 2.4 GHz first
    showing5G = false;
    displayBand(showing5G);
}

void loop() {
    unsigned long now = millis();

    // Rescan every SCAN_INTERVAL milliseconds
    if (now - lastScanMs >= SCAN_INTERVAL) {
        scanWifi();
        lastScanMs = millis();
        // Refresh display with updated data immediately after scan
        displayBand(showing5G);
        lastDisplayMs = millis();
    }

    // Flip band display every DISPLAY_INTERVAL milliseconds
    if (now - lastDisplayMs >= DISPLAY_INTERVAL) {
        showing5G = !showing5G;
        displayBand(showing5G);
        lastDisplayMs = millis();
    }
}
