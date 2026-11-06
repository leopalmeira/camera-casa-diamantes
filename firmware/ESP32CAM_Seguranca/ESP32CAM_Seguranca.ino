/* ESP32-CAM AI Thinker - Uploader JPEG + Wi-Fi provisioning + failover
   Board: AI Thinker ESP32-CAM; ESP32 Arduino core 3.x
   GPIO13 grounded at power-on enters configuration AP (without microSD).
*/
#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>

#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22
#define CONFIG_PIN 13

Preferences prefs;
WebServer portal(80);
String wifiSsid[2], wifiPass[2], serverUrl, deviceToken;
int currentNetwork = -1;
uint32_t lastAttempt = 0;
uint32_t lastFrame = 0;
uint32_t lastWifiCheck = 0;
const uint32_t FRAME_INTERVAL_MS = 900; // ~1 fps, not continuous HD video
const uint32_t WIFI_RETRY_MS = 15000;
String htmlEscape(String s) {
  s.replace("&", "&amp;"); s.replace("<", "&lt;"); s.replace(">", "&gt;");
  s.replace("\"", "&quot;"); return s;
}
void loadConfig() {
  prefs.begin("security", true);
  wifiSsid[0] = prefs.getString("ssid1", "");
  wifiPass[0] = prefs.getString("pass1", "");
  wifiSsid[1] = prefs.getString("ssid2", "");
  wifiPass[1] = prefs.getString("pass2", "");
  serverUrl = prefs.getString("url", "https://camera-casa-diamantes.onrender.com");
  deviceToken = prefs.getString("token", "");
  prefs.end();
  serverUrl.trim();
  while (serverUrl.endsWith("/")) serverUrl.remove(serverUrl.length()-1);
}
void portalPage() {
  String page = R"HTML(<!doctype html><html lang="pt-BR"><meta name="viewport" content="width=device-width,initial-scale=1"><meta charset="utf-8"><title>Configurar ESP32-CAM</title><style>body{background:#101827;color:#eff6ff;font:16px Arial;margin:0;padding:24px}main{max-width:430px;margin:auto}input{box-sizing:border-box;width:100%;padding:12px;margin:7px 0 16px;border:1px solid #64748b;border-radius:8px}button{background:#10b981;border:0;border-radius:8px;padding:14px;width:100%;font-weight:700}small{color:#94a3b8}</style><main><h2>ESP32-CAM • Configuração</h2><small>Cadastre até duas redes Wi-Fi 2,4 GHz. Dados ficam gravados no dispositivo.</small><form method="post" action="/save"><label>Wi-Fi 1 (casa)</label><input name="s1" value=")HTML";
  page += htmlEscape(wifiSsid[0]);
  page += R"HTML(" required maxlength="32"><label>Senha Wi-Fi 1</label><input type="password" name="p1" placeholder="Deixe vazio para manter a anterior"><label>Wi-Fi 2 (alternativa)</label><input name="s2" maxlength="32" value=")HTML";
  page += htmlEscape(wifiSsid[1]);
  page += R"HTML("><label>Senha Wi-Fi 2</label><input type="password" name="p2" placeholder="Deixe vazio para manter a anterior"><label>URL HTTPS do servidor</label><input name="url" placeholder="https://meu-servidor.onrender.com" required value=")HTML";
  page += htmlEscape(serverUrl);
  page += R"HTML("><label>Token secreto do dispositivo</label><input type="password" name="token" placeholder="Deixe vazio para manter o anterior"><button>Salvar e reiniciar</button></form><p><small>Para reabrir: ligue com GPIO13 conectado ao GND. Não use microSD neste projeto.</small></p></main></html>)HTML";
  portal.send(200, "text/html; charset=utf-8", page);
}
void startPortal() {
  WiFi.disconnect(true); WiFi.mode(WIFI_AP);
  String ap = "CAM-CONFIG-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  String pass = "CAM-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (pass.length() < 8) pass += "12345678";
  WiFi.softAP(ap.c_str(), pass.c_str());
  portal.on("/", HTTP_GET, portalPage);
  portal.on("/save", HTTP_POST, []() {
    String s1 = portal.arg("s1"), s2 = portal.arg("s2"), u = portal.arg("url");
    String t = portal.arg("token"), p1 = portal.arg("p1"), p2 = portal.arg("p2");
    u.trim();
    if (!u.startsWith("https://") || s1.length() == 0 || (t.isEmpty() && deviceToken.isEmpty())) {
      portal.send(400, "text/plain", "Informe Wi-Fi 1, URL HTTPS e token do dispositivo."); return;
    }
    prefs.begin("security", false);
    prefs.putString("ssid1", s1); prefs.putString("ssid2", s2);
    if (p1.length()) prefs.putString("pass1", p1);
    if (p2.length()) prefs.putString("pass2", p2);
    prefs.putString("url", u);
    if (t.length()) prefs.putString("token", t);
    prefs.end();
    portal.send(200, "text/html; charset=utf-8", "<h2>Configuração salva. Reiniciando...</h2>");
    delay(1200); ESP.restart();
  });
  portal.onNotFound([]() {portal.sendHeader("Location", "http://192.168.4.1/"); portal.send(302);});
  portal.begin();
  Serial.printf("\nPortal Wi-Fi: %s | senha: %s | http://192.168.4.1\n", ap.c_str(), pass.c_str());
  while (true) { portal.handleClient(); delay(5); }
}
bool startCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0; c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM; c.pin_d1 = Y3_GPIO_NUM; c.pin_d2 = Y4_GPIO_NUM;
  c.pin_d3 = Y5_GPIO_NUM; c.pin_d4 = Y6_GPIO_NUM; c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM; c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM; c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM; c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM; c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM; c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000; c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_QVGA; c.jpeg_quality = 12;
  c.fb_count = psramFound() ? 2 : 1;
  c.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;
  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) Serial.printf("Falha camera: 0x%x\n", err);
  return err == ESP_OK;
}
void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastAttempt < WIFI_RETRY_MS && lastAttempt != 0) return;
  lastAttempt = millis();
  int next = currentNetwork;
  for (int i = 0; i < 2; i++) {
    next = (next + 1) % 2;
    if (wifiSsid[next].isEmpty()) continue;
    WiFi.disconnect();
    Serial.printf("Tentando rede %d: %s\n", next+1, wifiSsid[next].c_str());
    WiFi.begin(wifiSsid[next].c_str(), wifiPass[next].c_str());
    for (int n = 0; n < 30 && WiFi.status() != WL_CONNECTED; n++) delay(250);
    if (WiFi.status() == WL_CONNECTED) {
      currentNetwork = next;
      Serial.printf("Conectado em %s | IP %s\n", wifiSsid[next].c_str(), WiFi.localIP().toString().c_str());
      return;
    }
  }
  Serial.println("Nenhuma rede disponivel; tentando novamente.");
}
void uploadFrame() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { Serial.println("Falha ao capturar frame"); return; }
  WiFiClientSecure client;
  // SOMENTE PROTOTIPO: validação TLS desabilitada; em produção usar CA confiável.
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(7000);
  if (http.begin(client, serverUrl + "/api/frame")) {
    http.addHeader("Content-Type", "image/jpeg");
    http.addHeader("Authorization", "Bearer " + deviceToken);
    int result = http.POST(fb->buf, fb->len);
    if (result < 0 || result >= 400) Serial.printf("Upload erro HTTP: %d\n", result);
    http.end();
  }
  esp_camera_fb_return(fb);
}
void setup() {
  Serial.begin(115200);
  pinMode(CONFIG_PIN, INPUT_PULLUP);
  loadConfig();
  if (digitalRead(CONFIG_PIN) == LOW || wifiSsid[0].isEmpty() || serverUrl.isEmpty() || deviceToken.isEmpty()) startPortal();
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.persistent(false);
  if (!startCamera()) {delay(3000); ESP.restart();}
  connectWifi();
}
void loop() {
  if (WiFi.status() != WL_CONNECTED) { connectWifi(); delay(100); return; }
  if (millis() - lastFrame >= FRAME_INTERVAL_MS) {
    lastFrame = millis();
    uploadFrame();
  }
  delay(10);
}
