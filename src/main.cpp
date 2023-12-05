#include <Wire.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <EEPROM.h>
#include <ESPAsyncWebServer.h>
#include <RtcPCF8563.h>


bool firstBootCheck();
void setupAPMode();
void loadConfig();
void saveConfig();
void connectToWiFi();
void handleConfigPage(AsyncWebServerRequest* request);
void handleSaveConfig(AsyncWebServerRequest* request);


const char* ssid = "TwojaSiec";
const char* password = "TwojeHaslo";
const char* apSSID = "ESP32_AP";
const char* apPassword = "12345678";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);
RtcPCF8563<TwoWire> rtc(Wire);

AsyncWebServer server(80);

struct {
    char ssid[32];
    char password[64];
    int syncInterval;
    int lampCount;
    int RGB[3];
    int brightness;
    bool isSleepMode;
    char timezone[32];
} config;

void setup()
{
    Serial.begin(115200);
    Wire.begin(21, 22);

    EEPROM.begin(512);

    if (firstBootCheck()) {
        setupAPMode();
    } else {
        loadConfig();
        connectToWiFi();
        timeClient.begin();
        rtc.Begin();
    }
}

bool firstBootCheck() { return EEPROM.read(0) == 255; }

void setupAPMode()
{
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSSID, apPassword);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    server.on("/", HTTP_GET, handleConfigPage);
    server.on("/save", HTTP_POST, handleSaveConfig);
    server.begin();
}

void handleConfigPage(AsyncWebServerRequest* request)
{
    String html = R"(
    <h1>Config</h1>
    <form action="/save" method="post">
      SSID: <input type="text" name="ssid"><br>
      Password: <input type="password" name="password"><br>
      Sync Interval (ms): <input type="number" name="syncInterval"><br>
      Lamp Count: <input type="number" name="lampCount" min="2" max="16"><br>
      R: <input type="number" name="R" max="255"><br>
      G: <input type="number" name="G" max="255"><br>
      B: <input type="number" name="B" max="255"><br>
      Brightness: <input type="number" name="brightness" max="255"><br>
      Sleep Mode: <input type="checkbox" name="isSleepMode"><br>
      Timezone: <input type="text" name="timezone"><br>
      <input type="submit" value="Save">
    </form>
  )";
    request->send(200, "text/html", html);
}

void handleSaveConfig(AsyncWebServerRequest* request)
{
    if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
        strncpy(config.ssid, request->getParam("ssid", true)->value().c_str(), sizeof(config.ssid));
        strncpy(config.password, request->getParam("password", true)->value().c_str(), sizeof(config.password));
        config.syncInterval = request->getParam("syncInterval", true)->value().toInt();
        config.lampCount = request->getParam("lampCount", true)->value().toInt();
        config.RGB[0] = request->getParam("R", true)->value().toInt();
        config.RGB[1] = request->getParam("G", true)->value().toInt();
        config.RGB[2] = request->getParam("B", true)->value().toInt();
        config.brightness = request->getParam("brightness", true)->value().toInt();
        config.isSleepMode = request->hasParam("isSleepMode", true);
        strncpy(config.timezone, request->getParam("timezone", true)->value().c_str(), sizeof(config.timezone));

        saveConfig();
        EEPROM.write(0, 0); // Mark as configured
        EEPROM.commit();

        request->send(200, "text/plain", "Konfiguracja zapisana! Urządzenie zostanie zrestartowane.");
        delay(3000); // Allow the response to be sent
        ESP.restart();
    } else {
        request->send(400, "text/plain", "Brak wymaganych parametrów!");
    }
}

void loadConfig() { EEPROM.get(1, config); }

void saveConfig()
{
    EEPROM.put(1, config);
    EEPROM.commit();
}

void connectToWiFi()
{
    int attempt = 0;
    WiFi.begin(config.ssid, config.password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Łączenie z WiFi...");
        if (++attempt > 10) {
            Serial.println("Nie udało się połączyć z siecią WiFi!");
            return;
        }
    }
    Serial.println("Połączono z WiFi!");
}

void updateNTP()
{
    WiFi.mode(WIFI_STA); // Włącz WiFi
    WiFi.begin(config.ssid, config.password); // Połącz ponownie

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 5) {
        delay(1000);
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (!timeClient.update()) {
            Serial.println("Błąd aktualizacji czasu z serwera NTP!");
            return;
        }

        // struct tm now;
        // now.tm_hour = timeClient.getHours();
        // now.tm_min = timeClient.getMinutes();
        // now.tm_sec = timeClient.getSeconds();
        // now.tm_mday = timeClient.getDay();
        // now.tm_mon = timeClient.getMonth() - 1;
        // now.tm_year = timeClient.getYear() - 1900;
        // timeClient.getEpochTime();

        RtcDateTime now(timeClient.getEpochTime() - 1577836800);
        rtc.SetDateTime(now);
    }

    WiFi.disconnect(); // Rozłącz się z WiFi
    WiFi.mode(WIFI_OFF); // Wyłącz WiFi
}

void displayTime()
{
    RtcDateTime now = rtc.GetDateTime();
  
    Wire.beginTransmission(0x20);
    Wire.write(now.Hour());
    Wire.write(now.Minute());
    Wire.write(now.Second());
    Wire.endTransmission();
}

void loop()
{
    if (WiFi.status() != WL_CONNECTED) {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(100);
        digitalWrite(LED_BUILTIN, LOW);
        delay(100);
    } else {
        updateNTP();
        displayTime();

        if (config.isSleepMode) {
            esp_sleep_enable_timer_wakeup(config.syncInterval * 1000); // Przejście w tryb uśpienia
            esp_deep_sleep_start();
        } else {
            delay(config.syncInterval);
        }
    }
}