#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ============ SUAS CONFIGURAÇÕES ============
// WiFi
const char* WIFI_SSID = "GX1001_CFTV";
const char* WIFI_PASSWORD = "Instrument#2025";

// MQTT
const char* MQTT_BROKER = "broker.hivemq.com";  // Sem https://, apenas o domínio
const int MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "ESP8266_SE11F-VV07";    // ID único para seu dispositivo

// Tópicos separados (conforme solicitado)
const char* TOPIC_TEMP = "enterprise/SE11F/VV07/temp";
const char* TOPIC_UMID = "enterprise/SE11F/VV07/umid";

// Sensor DHT22
#define DHTPIN 2        // GPIO2 (D4 no NodeMCU)
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// Intervalo de leitura
const unsigned long INTERVALO_LEITURA = 30000; // 30 Segundos
//const unsigned long INTERVALO_LEITURA = 120000; // 2 minutos
//const unsigned long INTERVALO_LEITURA = 60000; // 1 minutos


unsigned long lastRead = 0;

// ============ CONTROLE DE FALHAS ============
int falhasConsecutivas = 0;
const int MAX_FALHAS = 5; // Número máximo de falhas antes de reiniciar

// ============ OBJETOS ============
WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== Sistema Enterprise VALE ===");
  Serial.print("Max falhas antes de reinício: ");
  Serial.println(MAX_FALHAS);
  
  // Inicializa sensor
  dht.begin();
  delay(2000);
  
  // Conecta WiFi
  conectarWiFi();
  delay(750);
  
  // Configura MQTT
  client.setServer(MQTT_BROKER, MQTT_PORT);
  
  Serial.println("Setup concluído!");
  Serial.print("Tópico Temp: ");
  Serial.println(TOPIC_TEMP);
  Serial.print("Tópico Umid: ");
  Serial.println(TOPIC_UMID);
  delay(750);
}

void loop() {
  // Mantém conexão MQTT ativa
  if (!client.connected()) {
    reconectarMQTT();
  }
  client.loop();
  
  // Verifica se é hora de nova leitura
  if (millis() - lastRead >= INTERVALO_LEITURA) {
    fazerLeitura();
    lastRead = millis();
  }
  
  // Comandos via Serial
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 't') {  // Digite 't' para teste
      publicarTeste();
    } else if (c == 'r') { // Digite 'r' para reiniciar manualmente
      Serial.println("Reiniciando manualmente...");
      delay(1000);
      ESP.restart();
    } else if (c == 's') { // Digite 's' para ver status
      Serial.print("Falhas consecutivas: ");
      Serial.println(falhasConsecutivas);
    }
  }
}

void conectarWiFi() {
  Serial.print("Conectando ao WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 40) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("WiFi conectado!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("Falha ao conectar WiFi!");
  }
}

void reconectarMQTT() {
  // Tenta reconectar até conseguir
  while (!client.connected()) {
    Serial.print("Conectando ao broker MQTT...");
    
    if (client.connect(MQTT_CLIENT_ID)) {
      Serial.println("conectado!");
      Serial.println("Tópicos configurados:");
      Serial.print("  - ");
      Serial.println(TOPIC_TEMP);
      Serial.print("  - ");
      Serial.println(TOPIC_UMID);
    } else {
      Serial.print("falha, rc=");
      Serial.print(client.state());
      Serial.println(" tentando novamente em 5 segundos");
      
      for (int i = 0; i < 5; i++) {
        delay(1000);
        if (WiFi.status() != WL_CONNECTED) {
          conectarWiFi();
        }
      }
    }
  }
}

void fazerLeitura() {
  Serial.println("\n--- Nova leitura ---");
  
  // Verifica conexões
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado, reconectando...");
    conectarWiFi();
  }
  
  if (!client.connected()) {
    reconectarMQTT();
  }
  
  // Lê sensor
  float temperatura = dht.readTemperature();
  float umidade = dht.readHumidity();
  
  // ============ NOVA LÓGICA DE TRATAMENTO DE ERRO ============
  // Verifica se leitura foi válida
  if (isnan(temperatura) || isnan(umidade)) {
    falhasConsecutivas++;
    Serial.print("❌ Erro na leitura do sensor! Falha #");
    Serial.print(falhasConsecutivas);
    Serial.print(" de ");
    Serial.println(MAX_FALHAS);
    
    if (falhasConsecutivas >= MAX_FALHAS) {
      Serial.println("⚠️ Muitas falhas consecutivas! Reiniciando ESP...");
      delay(5000); // Aguarda 5 segundos para mensagem aparecer
      reiniciarESP();
    }
    return;
  }
  
  // Se chegou aqui, leitura foi bem sucedida - RESETA CONTADOR
  if (falhasConsecutivas > 0) {
    Serial.print("✅ Leitura OK após ");
    Serial.print(falhasConsecutivas);
    Serial.println(" falhas. Contador zerado.");
    falhasConsecutivas = 0;
  }
  
  // Mostra leitura no Serial
  Serial.print("Temperatura: ");
  Serial.print(temperatura);
  Serial.print(" °C, Umidade: ");
  Serial.print(umidade);
  Serial.println(" %");
  
  // Publica temperatura (como string simples, sem JSON)
  char tempStr[10];
  dtostrf(temperatura, 4, 1, tempStr); // Formata com 1 casa decimal
  
  if (client.publish(TOPIC_TEMP, tempStr)) {
    Serial.print("✅ Temperatura publicada: ");
    Serial.print(tempStr);
    Serial.print(" °C em ");
    Serial.println(TOPIC_TEMP);
  } else {
    Serial.println("❌ Falha ao publicar temperatura");
  }
  
  // Pequeno delay entre publicações
  delay(200);
  
  // Publica umidade (como string simples, sem JSON)
  char umidStr[10];
  dtostrf(umidade, 4, 1, umidStr); // Formata com 1 casa decimal
  
  if (client.publish(TOPIC_UMID, umidStr)) {
    Serial.print("✅ Umidade publicada: ");
    Serial.print(umidStr);
    Serial.print(" % em ");
    Serial.println(TOPIC_UMID);
  } else {
    Serial.println("❌ Falha ao publicar umidade");
  }
  
  Serial.println("--------------------\n");
}

// ============ NOVA FUNÇÃO DE REINÍCIO ============
void reiniciarESP() {
  Serial.println("🔄 Reiniciando ESP8266...");
  
  // Tenta publicar mensagem de erro antes de reiniciar (opcional)
  if (client.connected()) {
    client.publish("enterprise/SE11F/VV07/status", "reiniciando_por_erro(DHT22)", true);
    delay(750);
  }
  
  Serial.flush(); // Garante que todas as mensagens serial foram enviadas
  delay(1000);
  
  ESP.restart(); // Reinicia o ESP8266
  
  // O código nunca chega aqui, mas por segurança:
  while(1) {
    Serial.println("LOOP_INFINITE");
    delay(1000);
  }
}

void publicarTeste() {
  // Publica valores de teste
  if (client.publish(TOPIC_TEMP, "80.0")) {
    Serial.println("✅ Teste temperatura publicado: 80.0°C");
  }
  delay(200);
  if (client.publish(TOPIC_UMID, "80.0")) {
    Serial.println("✅ Teste umidade publicado: 80.0%");
  }
}
