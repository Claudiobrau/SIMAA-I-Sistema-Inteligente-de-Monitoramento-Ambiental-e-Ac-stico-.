/*
 * ============================================================================
 * SIMAA-I - Sistema Inteligente de Monitoramento Ambiental e Acústico
 * ============================================================================
 * 
 * DESCRIÇÃO:
 *   Versão adaptada para a placa NodeMCU ESP32-C2, mantendo todas as funcionalidades
 *   originais e adicionando suporte nativo para Bluetooth.
 * 
 * HARDWARE (Atualizado para ESP32-C2):
 *   - NodeMCU ESP32-C2 (ESP8684)
 *   - AHT21 (Temperatura e Umidade) - I2C
 *   - ENS160 (Qualidade do ar: eCO2, TVOC, AQI) - I2C
 *   - MAX9814 (Nível Sonoro) - Pino ADC
 * 
 * CONEXÕES ELÉTRICAS:
 *   AHT21 e ENS160 (Barramento I2C):
 *     - VCC -> 3.3V
 *     - GND -> GND
 *     - SDA -> GPIO4   (pino D2, igual ao ESP8266)
 *     - SCL -> GPIO5   (pino D1, igual ao ESP8266)
 *   
 *   MAX9814 (Microfone):
 *     - VCC -> 3.3V
 *     - GND -> GND
 *     - OUT -> GPIO0   (pino A0 no ESP8266, agora GPIO0 no ESP32-C2)
 *     - GAIN -> VDD (3.3V) -> ganho fixo de 40dB
 * 
 * DATA: 04/05/2026 - Versão ESP32-C2
 * ============================================================================
 */

#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <DFRobot_ENS160.h>
#include <WiFi.h>                 // Biblioteca específica para ESP32
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ============================================================================
// >>>>>>>>>>>>>> CONFIGURAÇÕES ATUALIZADAS PARA O ESP32-C2 <<<<<<<<<<<<<<<<
// ============================================================================

// --- Configurações de Rede Wi-Fi (Mantidas) ---
const char* ssid = "Net_177casa2";
const char* password = "anasophia0304";
//const char* ssid = "F_Braulio";
//const char* password = "f26121943";
//const char* ssid = "Retorno_Palco_IAP";
//const char* password = "SoRetorno#1";

// --- Configurações MQTT - THINGSBOARD (Mantidas) ---
const char* mqtt_server = "thingsboard.cloud";
const int mqtt_port = 1883;
const char* mqtt_topic = "v1/devices/me/telemetry";
const char* mqtt_token = "YqtUePEgTOYxm8x4Tliz";

// --- Configurações NTP (Mantidas) ---
const char* ntpServer = "pool.ntp.br";
const long gmtOffset = -10800;   // GMT-3 para horário de Brasília
const int ntpTimeout = 10000;

// ============================================================================
// PINAGEM ESPECÍFICA PARA O ESP32-C2
// ============================================================================

// Define os pinos que serão usados para a comunicação I2C.
// Para o ESP32-C2, podemos escolher qualquer GPIO disponível.
// Usei os mesmos GPIOs que o ESP8266 por conveniência.
const int I2C_SDA = 4;   // Pino D2
const int I2C_SCL = 5;   // Pino D1

// Define o pino do microfone. O ESP32-C2 tem pinos ADC nos GPIOs 0-4.
// O GPIO0 é uma ótima escolha, similar ao pino A0 do ESP8266.
const int MIC_PIN = 0;

// --- Configuração de Bluetooth (NOVO!) ---
// Habilite esta opção se quiser enviar dados também pelo Bluetooth.
// Você vai precisar instalar a biblioteca "ESP32 BLE Arduino".
#define ENABLE_BLUETOOTH false   // Mude para 'true' para ativar

#ifdef ENABLE_BLUETOOTH
  #include <BLEDevice.h>
  #include <BLEUtils.h>
  #include <BLEServer.h>
  #include <BLE2902.h>

  #define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
  #define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
  BLECharacteristic *pCharacteristic;
#endif

// ============================================================================
// VARIÁVEIS GLOBAIS
// ============================================================================
Adafruit_AHTX0 aht21;
DFRobot_ENS160_I2C ens160;
WiFiClient espClient;
PubSubClient client(espClient);

const unsigned long JANELA_AMOSTRAGEM = 1000;
bool aht21Ok = false;
bool ens160Ok = false;
bool wifiOk = false;
bool ntpSincronizado = false;
unsigned long ultimoEnvio = 0;
const unsigned long intervaloEnvio = 15000;
unsigned long ultimaTentativaWiFi = 0;
const unsigned long intervaloReconexaoWiFi = 30000;

// (A função 'calcularDB' e 'lerPicoAPico' permanecem iguais, não são repetidas aqui por brevidade)
// *** IMPORTANTE: Cole aqui as funções 'calcularDB' e 'lerPicoAPico' do código original. ***
float calcularDB(float vpp) {
  if (vpp <= 0.0001) vpp = 0.0001;
  float db = 34.48 * log10(vpp) + 82.25;
  if (db < 35.0) db = 35.0;
  if (db > 105.0) db = 105.0;
  return db;
}

float lerPicoAPico(unsigned long tempoMs, int* maxADC, int* minADC, float* volts) {
  unsigned long inicio = millis();
  int sinalMax = 0;
  int sinalMin = 1024;
  
  while (millis() - inicio < tempoMs) {
    int amostra = analogRead(MIC_PIN);
    if (amostra < 1024) {
      if (amostra > sinalMax) sinalMax = amostra;
      if (amostra < sinalMin) sinalMin = amostra;
    }
  }
  
  *maxADC = sinalMax;
  *minADC = sinalMin;
  int picoAPicoADC = sinalMax - sinalMin;
  *volts = (picoAPicoADC * 3.3) / 1024.0;
  return *volts;
}

// (As funções 'obterTimestamp', 'conectarWiFi', 'conectarMQTT' e 'enviarDadosMQTT' também permanecem iguais)
// *** IMPORTANTE: Cole aqui as funções do código original. ***
String obterTimestamp() {
  if (!ntpSincronizado) return "Sem NTP";
  time_t now = time(nullptr);
  if (now < 100000) return "NTP inválido";
  struct tm* timeinfo = localtime(&now);
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
  return String(buffer);
}

void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiOk = true;
    return;
  }
  
  unsigned long agora = millis();
  if (agora - ultimaTentativaWiFi < intervaloReconexaoWiFi && ultimaTentativaWiFi != 0) {
    return;
  }
  ultimaTentativaWiFi = agora;
  
  Serial.print("Conectando ao Wi-Fi");
  WiFi.begin(ssid, password);
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Wi-Fi conectado!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    wifiOk = true;
    
    if (!ntpSincronizado) {
      configTime(gmtOffset, 0, ntpServer);
      Serial.print("Aguardando sincronização NTP...");
      unsigned long inicioNTP = millis();
      while (time(nullptr) < 100000 && (millis() - inicioNTP) < ntpTimeout) {
        delay(100);
        Serial.print(".");
      }
      if (time(nullptr) >= 100000) {
        Serial.println(" ✅ NTP sincronizado!");
        ntpSincronizado = true;
      } else {
        Serial.println(" ⚠️ Falha na sincronização NTP (usando timestamp genérico)");
      }
    }
  } else {
    Serial.println("\n⚠️ Wi-Fi não conectado!");
    wifiOk = false;
  }
}

void conectarMQTT() {
  if (client.connected()) return;
  Serial.print("Conectando ao ThingsBoard MQTT...");
  if (client.connect("SIMAA_Grupo10", mqtt_token, NULL)) {
    Serial.println("✅ Conectado!");
  } else {
    Serial.print("❌ Falhou, rc=");
    Serial.println(client.state());
  }
}

void enviarDadosMQTT(float temp, float umid, int eco2, int tvoc, int aqi,
                     int maxADC, int minADC, float vpp, float db) {
  StaticJsonDocument<384> doc;
  doc["timestamp"] = obterTimestamp();
  doc["temperatura"] = temp;
  doc["umidade"] = umid;
  doc["eco2"] = eco2;
  doc["tvoc"] = tvoc;
  doc["aqi"] = aqi;
  doc["som_max_adc"] = maxADC;
  doc["som_min_adc"] = minADC;
  doc["som_vpp"] = vpp;
  doc["som_db"] = db;
  
  char buffer[384];
  size_t n = serializeJson(doc, buffer);
  if (n == 0) {
    Serial.println("❌ Erro ao serializar JSON");
    return;
  }
  
  if (client.publish(mqtt_topic, buffer)) {
    Serial.println("📤 Dados enviados com sucesso!");
    Serial.println(buffer);
  } else {
    Serial.println("❌ Falha ao enviar dados (MQTT desconectado?)");
  }
}

// ============================================================================
// FUNÇÃO NOVA: enviarDadosBluetooth
// ============================================================================
#ifdef ENABLE_BLUETOOTH
void enviarDadosBluetooth(float temp, float umid, int eco2, int tvoc, int aqi,
                          float db) {
  if (pCharacteristic == nullptr) return;
  
  StaticJsonDocument<256> doc;
  doc["t"] = temp;
  doc["h"] = umid;
  doc["co2"] = eco2;
  doc["voc"] = tvoc;
  doc["aqi"] = aqi;
  doc["db"] = db;
  
  char buffer[256];
  size_t n = serializeJson(doc, buffer);
  if (n > 0) {
    pCharacteristic->setValue((uint8_t*)buffer, n);
    pCharacteristic->notify();
  }
}
#endif

// ============================================================================
// FUNÇÃO NOVA: initBluetooth
// ============================================================================
#ifdef ENABLE_BLUETOOTH
void initBluetooth() {
  BLEDevice::init("SIMAA-I_Sensor");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                     CHARACTERISTIC_UUID,
                     BLECharacteristic::PROPERTY_READ   |
                     BLECharacteristic::PROPERTY_NOTIFY
                   );
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->start();
  Serial.println("✅ Bluetooth configurado e anunciando como 'SIMAA-I_Sensor'");
}
#endif

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n╔══════════════════════════════════════════════╗");
  Serial.println("║    SIMAA-I - Sistema de Monitoramento v.ESP32-C2   ║");
  Serial.println("║         Wi-Fi + ThingsBoard + Bluetooth LE        ║");
  Serial.println("╚══════════════════════════════════════════════╝\n");
  
  // --- Inicializa o barramento I2C com os pinos definidos (CRÍTICO) ---
  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.printf("🔌 I2C inicializado nos pinos SDA=%d, SCL=%d\n", I2C_SDA, I2C_SCL);
  
  // --- Inicializa os sensores I2C ---
  Serial.print("Inicializando AHT21... ");
  aht21Ok = aht21.begin();
  Serial.println(aht21Ok ? "✅ OK" : "❌ FALHA");
  
  Serial.print("Inicializando ENS160... ");
  ens160Ok = (ens160.begin() == NO_ERR);
  if (ens160Ok) {
    Serial.println("✅ OK");
    ens160.setPWRMode(ENS160_STANDARD_MODE);
  } else {
    Serial.println("❌ FALHA");
  }
  
  // --- Inicializa Wi-Fi, NTP e MQTT ---
  conectarWiFi();
  client.setServer(mqtt_server, mqtt_port);
  
  // --- Inicializa Bluetooth (se habilitado) ---
  #ifdef ENABLE_BLUETOOTH
    if (ENABLE_BLUETOOTH) {
      initBluetooth();
    }
  #endif
  
  Serial.printf("\n🎤 Microfone no pino ADC: GPIO%d\n", MIC_PIN);
  Serial.println("\n--- Sistema pronto! Enviando dados a cada 15 segundos ---\n");
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================
void loop() {
  // (O código do loop permanece o mesmo, apenas com a inclusão do envio Bluetooth)
  // *** IMPORTANTE: Cole aqui o código do loop do original, IDÊNTICO, apenas adicionando a chamada de 'enviarDadosBluetooth' ***
  
  if (WiFi.status() != WL_CONNECTED) conectarWiFi();
  if (wifiOk && !client.connected()) conectarMQTT();
  client.loop();
  
  if (millis() - ultimoEnvio >= intervaloEnvio) {
    ultimoEnvio = millis();
    
    // --- Leitura do microfone ---
    int maxADC = 0, minADC = 0;
    float vpp = 0.0;
    lerPicoAPico(JANELA_AMOSTRAGEM, &maxADC, &minADC, &vpp);
    float db = calcularDB(vpp);
    
    // --- Leitura AHT21 ---
    float temperatura = 0, umidade = 0;
    if (aht21Ok) {
      sensors_event_t humidity, temp;
      aht21.getEvent(&humidity, &temp);
      temperatura = temp.temperature;
      umidade = humidity.relative_humidity;
      if (ens160Ok) ens160.setTempAndHum(temperatura, umidade);
    }
    
    // --- Leitura ENS160 ---
    uint16_t eCO2 = 0, TVOC = 0;
    uint8_t AQI = 0;
    if (ens160Ok) {
      eCO2 = ens160.getECO2();
      TVOC = ens160.getTVOC();
      AQI = ens160.getAQI();
    }
    
    // --- Serial Monitor (igual) ---
    Serial.println("────────────────────────────────────────");
    Serial.println(obterTimestamp());
    Serial.printf("🌡️ Temperatura: %.2f °C\n", temperatura);
    Serial.printf("💧 Umidade: %.2f %%\n", umidade);
    Serial.printf("🌫️ eCO2: %d ppm\n", eCO2);
    Serial.printf("🧪 TVOC: %d ppb\n", TVOC);
    Serial.printf("📊 AQI: %d ", AQI);
    switch(AQI) {
      case 1: Serial.println("(Excelente)"); break;
      case 2: Serial.println("(Bom)"); break;
      case 3: Serial.println("(Moderado)"); break;
      case 4: Serial.println("(Ruim)"); break;
      case 5: Serial.println("(Péssimo)"); break;
      default: Serial.println("(Indefinido)");
    }
    Serial.printf("🎤 Som (ADC): max=%d, min=%d, Vpp=%.3fV, dB=%.1f\n",
                  maxADC, minADC, vpp, db);
    
    if (vpp > 0.05) {
      int barras = map(vpp * 1000, 50, 2500, 1, 10);
      barras = constrain(barras, 1, 10);
      Serial.print("🔊 Volume (Vpp): ");
      for (int i = 0; i < barras; i++) Serial.print("█");
      for (int i = barras; i < 10; i++) Serial.print("░");
      Serial.println();
    }
    
    // --- Envio MQTT ---
    if (client.connected()) {
      enviarDadosMQTT(temperatura, umidade, eCO2, TVOC, AQI,
                      maxADC, minADC, vpp, db);
    } else {
      Serial.println("⚠️ MQTT desconectado, dados não enviados.");
    }

    // --- Envio Bluetooth (NOVO!) ---
    #ifdef ENABLE_BLUETOOTH
      if (ENABLE_BLUETOOTH) {
        enviarDadosBluetooth(temperatura, umidade, eCO2, TVOC, AQI, db);
      }
    #endif
  }
  
  delay(100);
}