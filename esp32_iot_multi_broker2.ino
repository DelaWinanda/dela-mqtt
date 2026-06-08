/*
 * ============================================================
 *  Sistem IoT ESP32 - Multi-Broker MQTT
 *  Komponen: DHT11 + 4 Relay + 3 MQTT Broker
 * ============================================================
 * Library yang dibutuhkan (install via Arduino Library Manager):
 *   - PubSubClient by Nick O'Leary
 *   - DHT sensor library by Adafruit
 *   - Adafruit Unified Sensor by Adafruit
 * ============================================================
 * Wiring:
 *   DHT11  DATA  -> GPIO 4
 *   Relay1        -> GPIO 23
 *   Relay2        -> GPIO 19
 *   Relay3        -> GPIO 18
 *   Relay4        -> GPIO 5
 * ============================================================
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ============================================================
//  KONFIGURASI — EDIT BAGIAN INI
// ============================================================
const char* WIFI_SSID     = "realme 11";
const char* WIFI_PASSWORD = "vivaldi11";

// --- Broker 1: Eclipse Mosquitto (public test, tanpa akun) ---
const char* BROKER1_HOST = "test.mosquitto.org";
const int   BROKER1_PORT = 1883;

// --- Broker 2: MQTT Cool (public test, tanpa akun) ---
const char* BROKER2_HOST = "broker.mqtt.cool";
const int   BROKER2_PORT = 1883;

// --- Broker 3: Flespi (gratis, butuh token dari flespi.io) ---
// Cara dapat token: daftar di https://flespi.io -> Tokens -> buat token baru -> copy ke sini
const char* BROKER3_HOST = "mqtt.flespi.io";
const int   BROKER3_PORT = 1883;
const char* BROKER3_USER = "6TEFk2Z0l2Lqh8tr3pDdB9lDlZuUIOkpLcebKzfcumnE0AjKF4gyd0NYvOaTAUpj"; // ganti dengan token dari flespi.io
const char* BROKER3_PASS = "";                   // password dikosongkan

// ============================================================
//  PIN DEFINITIONS
// ============================================================
#define DHT_PIN     4
#define DHT_TYPE    DHT11

#define RELAY1_PIN  23
#define RELAY2_PIN  19
#define RELAY3_PIN  18
#define RELAY4_PIN  5

// Relay aktif LOW
#define RELAY_ON    LOW
#define RELAY_OFF   HIGH

// ============================================================
//  TOPIC MQTT
// ============================================================
// Subscribe (perintah dari web/server ke ESP32)
const char* TOPIC_RELAY1_CMD  = "home/relay/1/set";
const char* TOPIC_RELAY2_CMD  = "home/relay/2/set";
const char* TOPIC_RELAY3_CMD  = "home/relay/3/set";
const char* TOPIC_RELAY4_CMD  = "home/relay/4/set";
const char* TOPIC_COMBO_CMD   = "home/relay/combo/set";

// Publish (status dari ESP32 ke web/server)
const char* TOPIC_RELAY1_STAT = "home/relay/1/status";
const char* TOPIC_RELAY2_STAT = "home/relay/2/status";
const char* TOPIC_RELAY3_STAT = "home/relay/3/status";
const char* TOPIC_RELAY4_STAT = "home/relay/4/status";
const char* TOPIC_SENSOR      = "home/sensor/dht";

// ============================================================
//  VARIABEL GLOBAL
// ============================================================
DHT dht(DHT_PIN, DHT_TYPE);

// Masing-masing broker punya WiFiClient & PubSubClient sendiri
WiFiClient   wifiClient1, wifiClient2, wifiClient3;
PubSubClient mqtt1(wifiClient1);
PubSubClient mqtt2(wifiClient2);
PubSubClient mqtt3(wifiClient3);

bool relayState[4] = {false, false, false, false};

unsigned long lastSensorPublish = 0;
const long    SENSOR_INTERVAL   = 5000; // publish sensor tiap 5 detik

// Untuk animasi kombinasi
unsigned long lastComboStep = 0;
int           comboMode     = 0; // 0=off, 1=kiri-ke-kanan, 2=kanan-ke-kiri(4->1), 3=strobe
int           comboStep     = 0;

// ============================================================
//  CALLBACK — dipanggil saat ada pesan MQTT masuk
// ============================================================
void handleMessage(const char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  msg.trim();
  msg.toUpperCase();

  Serial.printf("[MQTT] Topic: %s  | Msg: %s\n", topic, msg.c_str());

  // --- Kendali relay individual ---
  if      (strcmp(topic, TOPIC_RELAY1_CMD) == 0) setRelay(0, msg == "ON");
  else if (strcmp(topic, TOPIC_RELAY2_CMD) == 0) setRelay(1, msg == "ON");
  else if (strcmp(topic, TOPIC_RELAY3_CMD) == 0) setRelay(2, msg == "ON");
  else if (strcmp(topic, TOPIC_RELAY4_CMD) == 0) setRelay(3, msg == "ON");

  // --- Kendali kombinasi ---
  else if (strcmp(topic, TOPIC_COMBO_CMD) == 0) {
    if      (msg == "KIRI_KE_KANAN") { comboMode = 1; comboStep = 0; allRelays(false); }
    else if (msg == "KANAN_KE_KIRI") { comboMode = 2; comboStep = 0; allRelays(false); }
    else if (msg == "STROBE")        { comboMode = 3; comboStep = 0; }
    else if (msg == "OFF")           { comboMode = 0; allRelays(false); }
  }
}

// Wrapper callback untuk setiap broker (PubSubClient butuh fungsi terpisah)
void callback1(char* t, byte* p, unsigned int l) { handleMessage(t, p, l); }
void callback2(char* t, byte* p, unsigned int l) { handleMessage(t, p, l); }
void callback3(char* t, byte* p, unsigned int l) { handleMessage(t, p, l); }

// ============================================================
//  FUNGSI RELAY
// ============================================================
void setRelay(int idx, bool state) {
  if (idx < 0 || idx > 3) return;
  relayState[idx] = state;
  int pins[4] = {RELAY1_PIN, RELAY2_PIN, RELAY3_PIN, RELAY4_PIN};
  digitalWrite(pins[idx], state ? RELAY_ON : RELAY_OFF);

  // Publish status ke semua broker
  const char* topics[4] = {TOPIC_RELAY1_STAT, TOPIC_RELAY2_STAT,
                            TOPIC_RELAY3_STAT, TOPIC_RELAY4_STAT};
  const char* msg = state ? "ON" : "OFF";
  mqtt1.publish(topics[idx], msg, true);
  mqtt2.publish(topics[idx], msg, true);
  mqtt3.publish(topics[idx], msg, true);

  Serial.printf("[RELAY] Relay %d -> %s\n", idx + 1, msg);
}

void allRelays(bool state) {
  for (int i = 0; i < 4; i++) setRelay(i, state);
}

// ============================================================
//  ANIMASI KOMBINASI
// ============================================================
void runCombo() {
  if (comboMode == 0) return;

  unsigned long now = millis();
  long interval = (comboMode == 3) ? 150 : 300; // strobe lebih cepat

  if (now - lastComboStep < (unsigned long)interval) return;
  lastComboStep = now;

  if (comboMode == 1) {
    // Kiri ke kanan: relay nyala bergantian 1->2->3->4->off->repeat
    allRelays(false);
    if (comboStep < 4) setRelay(comboStep, true);
    comboStep = (comboStep + 1) % 5;
  }
  else if (comboMode == 2) {
    // Kanan ke kiri: relay nyala bergantian 4->3->2->1->off->repeat
    allRelays(false);
    int idx = 3 - (comboStep % 5); // step 0=relay4(idx3), 1=relay3(idx2), dst
    if (comboStep < 4) setRelay(idx, true);
    comboStep = (comboStep + 1) % 5;
  }
  else if (comboMode == 3) {
    // Strobe: semua relay on/off bergantian cepat
    bool s = (comboStep % 2 == 0);
    allRelays(s);
    comboStep++;
  }
}

// ============================================================
//  KONEKSI MQTT — Broker 1 & 2 (tanpa autentikasi)
// ============================================================
void connectBroker(PubSubClient &mqtt, const char* host, int port,
                   MQTT_CALLBACK_SIGNATURE) {
  mqtt.setServer(host, port);
  mqtt.setCallback(callback);

  Serial.printf("[MQTT] Menghubungkan ke %s:%d ...\n", host, port);
  if (mqtt.connect("")) {
    Serial.printf("[MQTT] Terhubung ke %s\n", host);
    mqtt.subscribe(TOPIC_RELAY1_CMD);
    mqtt.subscribe(TOPIC_RELAY2_CMD);
    mqtt.subscribe(TOPIC_RELAY3_CMD);
    mqtt.subscribe(TOPIC_RELAY4_CMD);
    mqtt.subscribe(TOPIC_COMBO_CMD);
  } else {
    Serial.printf("[MQTT] Gagal terhubung ke %s, rc=%d\n", host, mqtt.state());
  }
}

// ============================================================
//  KONEKSI MQTT — Broker 3 Flespi (dengan token autentikasi)
// ============================================================
void connectFlespi() {
  mqtt3.setServer(BROKER3_HOST, BROKER3_PORT);
  mqtt3.setCallback(callback3);

  Serial.printf("[MQTT] Menghubungkan ke flespi (%s:%d) ...\n", BROKER3_HOST, BROKER3_PORT);
  if (mqtt3.connect("", BROKER3_USER, BROKER3_PASS)) {
    Serial.println("[MQTT] Terhubung ke flespi");
    mqtt3.subscribe(TOPIC_RELAY1_CMD);
    mqtt3.subscribe(TOPIC_RELAY2_CMD);
    mqtt3.subscribe(TOPIC_RELAY3_CMD);
    mqtt3.subscribe(TOPIC_RELAY4_CMD);
    mqtt3.subscribe(TOPIC_COMBO_CMD);
  } else {
    Serial.printf("[MQTT] Gagal flespi, rc=%d\n", mqtt3.state());
  }
}

// ============================================================
//  RECONNECT SEMUA BROKER JIKA PUTUS
// ============================================================
void reconnectIfNeeded() {
  if (!mqtt1.connected()) {
    connectBroker(mqtt1, BROKER1_HOST, BROKER1_PORT, callback1);
  }
  if (!mqtt2.connected()) {
    connectBroker(mqtt2, BROKER2_HOST, BROKER2_PORT, callback2);
  }
  if (!mqtt3.connected()) {
    connectFlespi();
  }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  // Inisialisasi pin relay
  pinMode(RELAY1_PIN, OUTPUT); digitalWrite(RELAY1_PIN, RELAY_OFF);
  pinMode(RELAY2_PIN, OUTPUT); digitalWrite(RELAY2_PIN, RELAY_OFF);
  pinMode(RELAY3_PIN, OUTPUT); digitalWrite(RELAY3_PIN, RELAY_OFF);
  pinMode(RELAY4_PIN, OUTPUT); digitalWrite(RELAY4_PIN, RELAY_OFF);

  // Inisialisasi DHT
  dht.begin();

  // Koneksi WiFi
  Serial.printf("\n[WiFi] Menghubungkan ke %s...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] Terhubung! IP: %s\n", WiFi.localIP().toString().c_str());

  // Koneksi ke 3 broker
  connectBroker(mqtt1, BROKER1_HOST, BROKER1_PORT, callback1);
  connectBroker(mqtt2, BROKER2_HOST, BROKER2_PORT, callback2);
  connectFlespi();
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  // Reconnect jika putus
  reconnectIfNeeded();

  // Proses pesan masuk dari ketiga broker
  mqtt1.loop();
  mqtt2.loop();
  mqtt3.loop();

  // Jalankan animasi kombinasi
  runCombo();

  // Publish data sensor setiap SENSOR_INTERVAL ms
  unsigned long now = millis();
  if (now - lastSensorPublish >= SENSOR_INTERVAL) {
    lastSensorPublish = now;
    publishSensor();
  }
}

// ============================================================
//  PUBLISH DATA SENSOR DHT11
// ============================================================
void publishSensor() {
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();

  if (isnan(temp) || isnan(hum)) {
    Serial.println("[DHT] Gagal membaca sensor!");
    return;
  }

  // Format JSON
  char payload[80];
  snprintf(payload, sizeof(payload),
           "{\"temperature\":%.1f,\"humidity\":%.1f}", temp, hum);

  mqtt1.publish(TOPIC_SENSOR, payload, true);
  mqtt2.publish(TOPIC_SENSOR, payload, true);
  mqtt3.publish(TOPIC_SENSOR, payload, true);

  Serial.printf("[DHT] Suhu: %.1f C | Kelembapan: %.1f%%\n", temp, hum);
}
