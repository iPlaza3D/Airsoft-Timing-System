#include <WiFi.h>
#include <WiFiUdp.h>

// ============================================================================
// CONFIGURACIÓN CLIENTE - END PLATE
// ============================================================================
const char* ssid = "AIRSOFT Stage 01";
const char* password = "12345678";
WiFiUDP udp;
const uint16_t PORT = 4210;
const char* SERVER_IP = "255.255.255.255"; // Broadcast

const int BUTTON_PIN = 0;    // Botón BOOT para simular hit
const int LED_PIN = 2;       // LED interno para feedback
const int SENSOR_PIN = 4;    // Pin para sensor físico (opcional)

// Variables de comunicación
uint16_t sequenceNumber = 0;
uint16_t pingSequence = 0;
bool lastButtonState = HIGH;
bool lastSensorState = HIGH;

// 🆕 SISTEMA PING/PONG
bool connectedToTimer = false;
uint32_t lastPongTime = 0;
uint32_t lastPingTime = 0;
const uint32_t PING_INTERVAL_MS = 2000;    // Enviar PING cada 2 segundos
const uint32_t PONG_TIMEOUT_MS = 5000;     // 5 segundos sin PONG = desconectado

// Variables de debounce
uint32_t lastButtonPress = 0;
uint32_t lastSensorPress = 0;
const uint32_t DEBOUNCE_DELAY = 200;

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  
  // Configurar pines
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(SENSOR_PIN, INPUT_PULLUP); // Sensor normalmente HIGH (pull-up)
  
  // Conectar a WiFi
  WiFi.begin(ssid, password);
  Serial.print("🔌 Conectando a WiFi");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // LED parpadeante
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Conectado a: " + String(ssid));
    Serial.println("📡 IP: " + WiFi.localIP().toString());
    digitalWrite(LED_PIN, HIGH); // LED encendido = conectado
  } else {
    Serial.println("\n❌ Error conectando a WiFi");
    digitalWrite(LED_PIN, LOW);
  }
  
  udp.begin(PORT);
  
  Serial.println("\n🎯 END PLATE - AIRSOFT TIMER");
  Serial.println("==========================================");
  Serial.println("📋 COMANDOS DISPONIBLES:");
  Serial.println("   'hit' o 'h'     - Simular impacto normal (ID 1)");
  Serial.println("   'hit X'         - Simular impacto ID específico");
  Serial.println("   'stop' o 's'    - Finalizar stage (ID 99)");
  Serial.println("   'ping' o 'p'    - Forzar envío de PING");
  Serial.println("   'status'        - Mostrar información");
  Serial.println("   'help'          - Mostrar ayuda");
  Serial.println("🔘 BOTÓN BOOT      - Enviar hit (ID 1)");
  Serial.println("🔧 SENSOR GPIO 4   - Enviar hit (ID 1)");
  Serial.println("==========================================\n");
}

// ============================================================================
// ENVIAR MENSAJE HIT
// ============================================================================
void sendHit(int sensorId) {
  uint32_t currentTime = micros();
  char message[64];
  
  snprintf(message, sizeof(message), "HIT:%d:%lu:%d", sensorId, currentTime, sequenceNumber++);
  
  udp.beginPacket(SERVER_IP, PORT);
  udp.write((uint8_t*)message, strlen(message));
  udp.endPacket();
  
  Serial.printf("🎯 HIT ENVIADO → ID: %d, Time: %lu, Seq: %d\n", sensorId, currentTime, sequenceNumber-1);
  
  // Feedback visual
  digitalWrite(LED_PIN, LOW);
  delay(50);
  digitalWrite(LED_PIN, HIGH);
}

// ============================================================================
// ENVIAR MENSAJE STOP
// ============================================================================
void sendStop() {
  uint32_t currentTime = micros();
  char message[64];
  
  snprintf(message, sizeof(message), "HIT:99:%lu:%d", currentTime, sequenceNumber++);
  
  udp.beginPacket(SERVER_IP, PORT);
  udp.write((uint8_t*)message, strlen(message));
  udp.endPacket();
  
  Serial.println("🛑 STOP ENVIADO → Finalizando stage");
  
  // Feedback visual especial
  for(int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, LOW);
    delay(80);
    digitalWrite(LED_PIN, HIGH);
    delay(80);
  }
}

// ============================================================================
// 🆕 ENVIAR MENSAJE PING
// ============================================================================
void sendPing() {
  char message[32];
  snprintf(message, sizeof(message), "PING:%d", pingSequence++);
  
  udp.beginPacket(SERVER_IP, PORT);
  udp.write((uint8_t*)message, strlen(message));
  udp.endPacket();
  
  lastPingTime = millis();
  Serial.printf("📡 PING ENVIADO → Seq: %d\n", pingSequence-1);
}

// ============================================================================
// 🆕 VERIFICAR CONEXIÓN CON TIMER
// ============================================================================
void checkConnection() {
  uint32_t currentTime = millis();
  
  // Enviar PING periódicamente
  if (currentTime - lastPingTime > PING_INTERVAL_MS) {
    sendPing();
  }
  
  // Verificar timeout de PONG
  if (connectedToTimer && (currentTime - lastPongTime > PONG_TIMEOUT_MS)) {
    connectedToTimer = false;
    Serial.println("🔌 DESCONECTADO del Timer - Timeout de PONG");
    digitalWrite(LED_PIN, LOW); // LED apagado = desconectado
  }
}

// ============================================================================
// DETECTAR BOTÓN FÍSICO
// ============================================================================
void checkButton() {
  bool currentState = digitalRead(BUTTON_PIN);
  uint32_t currentTime = millis();
  
  if (lastButtonState == HIGH && currentState == LOW) {
    // Botón presionado - detección flanco descendente
    if (currentTime - lastButtonPress > DEBOUNCE_DELAY) {
      lastButtonPress = currentTime;
      Serial.println("🔘 Botón presionado - Enviando HIT...");
      sendHit(1); // Enviar hit con ID 1
    }
  }
  
  lastButtonState = currentState;
}

// ============================================================================
// 🆕 DETECTAR SENSOR FÍSICO
// ============================================================================
void checkSensor() {
  bool currentState = digitalRead(SENSOR_PIN);
  uint32_t currentTime = millis();
  
  if (lastSensorState == HIGH && currentState == LOW) {
    // Sensor activado - detección flanco descendente
    if (currentTime - lastSensorPress > DEBOUNCE_DELAY) {
      lastSensorPress = currentTime;
      Serial.println("🔧 Sensor activado - Enviando HIT...");
      sendHit(1); // Enviar hit con ID 1
    }
  }
  
  lastSensorState = currentState;
}

// ============================================================================
// ESCUCHAR RESPUESTAS DEL TIMER
// ============================================================================
void listenForResponses() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char packet[64];
    int len = udp.read(packet, 64);
    if (len > 0) {
      packet[len] = '\0';
      
      Serial.print("📥 RESPUESTA: ");
      Serial.println(packet);
      
      if (strncmp(packet, "ACK:", 4) == 0) {
        uint16_t ackSeq = atoi(packet + 4);
        Serial.printf("✅ ACK confirmado - Seq: %d\n", ackSeq);
        
        // Feedback visual ACK
        for(int i = 0; i < 2; i++) {
          digitalWrite(LED_PIN, LOW);
          delay(30);
          digitalWrite(LED_PIN, HIGH);
          delay(30);
        }
      }
      else if (strncmp(packet, "PONG:", 5) == 0) {
        uint16_t pongSeq = atoi(packet + 5);
        lastPongTime = millis();
        
        if (!connectedToTimer) {
          connectedToTimer = true;
          Serial.println("🔌 CONECTADO al Timer - PONG recibido");
          digitalWrite(LED_PIN, HIGH); // LED encendido = conectado
        }
        
        Serial.printf("🔄 PONG recibido - Seq: %d\n", pongSeq);
        
        // Feedback visual PONG
        digitalWrite(LED_PIN, LOW);
        delay(20);
        digitalWrite(LED_PIN, HIGH);
      }
      else {
        Serial.println("📨 Mensaje desconocido recibido");
      }
    }
  }
}

// ============================================================================
// VERIFICAR CONEXIÓN WIFI
// ============================================================================
void checkWiFiConnection() {
  static uint32_t lastCheck = 0;
  uint32_t currentTime = millis();
  
  if (currentTime - lastCheck > 10000) { // Cada 10 segundos
    lastCheck = currentTime;
    
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("⚠️  WiFi desconectado - Reconectando...");
      digitalWrite(LED_PIN, LOW);
      WiFi.reconnect();
      
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("✅ WiFi reconectado");
        if (connectedToTimer) {
          digitalWrite(LED_PIN, HIGH);
        }
      }
    }
  }
}

// ============================================================================
// PROCESAR COMANDOS SERIAL
// ============================================================================
void processSerialCommand() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toLowerCase();
    
    Serial.printf("📝 Comando recibido: %s\n", command.c_str());
    
    if (command == "hit" || command == "h") {
      sendHit(1); // Sensor ID 1
    }
    else if (command == "stop" || command == "s") {
      sendStop();
    }
    else if (command == "ping" || command == "p") {
      sendPing();
    }
    else if (command.startsWith("hit ")) {
      // Formato: "hit 5" para ID específico
      int id = command.substring(4).toInt();
      if (id > 0 && id != 99) {
        sendHit(id);
      } else if (id == 99) {
        Serial.println("❌ Usa 'stop' para ID 99");
      }
    }
    else if (command == "status" || command == "info") {
      showStatus();
    }
    else if (command == "help" || command == "?") {
      showHelp();
    }
    else {
      Serial.println("❌ Comando desconocido. Escribe 'help' para ayuda.");
    }
  }
}

// ============================================================================
// MOSTRAR AYUDA
// ============================================================================
void showHelp() {
  Serial.println("\n📋 AYUDA - COMANDOS DISPONIBLES:");
  Serial.println("   hit, h          - Enviar hit (ID 1)");
  Serial.println("   hit X           - Enviar hit ID X");
  Serial.println("   stop, s         - Finalizar stage (ID 99)");
  Serial.println("   ping, p         - Enviar PING manualmente");
  Serial.println("   status, info    - Mostrar información");
  Serial.println("   help, ?         - Mostrar esta ayuda");
  Serial.println("🔘 BOTÓN BOOT      - Enviar hit (ID 1)");
  Serial.println("🔧 SENSOR GPIO 4   - Enviar hit (ID 1)");
}

// ============================================================================
// MOSTRAR ESTADO
// ============================================================================
void showStatus() {
  Serial.println("\n📊 ESTADO DEL END PLATE:");
  Serial.printf("   WiFi: %s\n", WiFi.status() == WL_CONNECTED ? "✅ Conectado" : "❌ Desconectado");
  Serial.printf("   IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("   Timer: %s\n", connectedToTimer ? "✅ Conectado" : "❌ Desconectado");
  Serial.printf("   Secuencia HIT: %d\n", sequenceNumber);
  Serial.printf("   Secuencia PING: %d\n", pingSequence);
  Serial.printf("   Servidor: %s:%d\n", SERVER_IP, PORT);
  Serial.printf("   Último PONG: %lu ms\n", millis() - lastPongTime);
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================
void loop() {
  // 🆕 VERIFICAR CONEXIÓN Y ENVIAR PINGS
  checkConnection();
  
  // DETECTAR ENTRADAS FÍSICAS
  checkButton();
  checkSensor();
  
  // PROCESAR COMANDOS SERIAL
  processSerialCommand();
  
  // ESCUCHAR RESPUESTAS
  listenForResponses();
  
  // VERIFICAR CONEXIÓN WIFI
  checkWiFiConnection();
  
  delay(10); // Pequeño delay para evitar sobrecarga
}

// ============================================================================
// INFORMACIÓN INICIAL AL CONECTAR
// ============================================================================
void showWelcome() {
  Serial.println("\n" 
    "╔══════════════════════════════════════╗\n"
    "║           END PLATE TIMER            ║\n"
    "║            CLIENTE UDP               ║\n"
    "╚══════════════════════════════════════╝");
}