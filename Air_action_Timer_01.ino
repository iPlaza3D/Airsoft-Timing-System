#include <WiFi.h>
#include <WiFiUdp.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ============================================================================
// CONFIGURACIÓN CONSTANTE
// ============================================================================
const char* ssid = "AIRSOFT Stage 01";
const char* password = "12345678";
WiFiUDP udp;
const uint16_t PORT = 4210;

const int PIN_BUZZER = 25;
const int PIN_START = 0;
const int PIN_BATTERY = 36;
const int STOP_ID = 99;

// ============================================================================
// VARIABLES GLOBALES
// ============================================================================
volatile bool running = false;
volatile bool finished = false;
volatile bool wifiActive = false;
volatile bool screenActive = true;

// SISTEMA PING/PONG PARA DETECCIÓN DE CONEXIÓN
volatile bool plateConnected = false;
uint32_t lastPingTime = 0;
const uint32_t PING_TIMEOUT_MS = 3000; // 3 segundos sin PING = desconectado

// VARIABLE PARA RETARDO DE INICIO
volatile bool countdownActive = false;
uint32_t countdownStartTime = 0;
const uint32_t COUNTDOWN_DELAY_MS = 2000; // 2 segundos

uint32_t lastPacketTime = 0;
uint32_t t0_us = 0;
uint32_t lastUpdate = 0;
uint32_t lastInteraction = 0;
float batteryPercent = 100.0;

// Variables del botón
volatile bool lastButtonState = HIGH;
uint32_t pressStartTime = 0;
const uint32_t LONG_PRESS_MS = 3000; // 🆕 CAMBIO: 5 segundos → 3 segundos
const uint32_t STANDBY_TIMEOUT_MS = 60000;

// Variables para la barra de progreso
volatile bool showingProgress = false;
uint32_t progressStartTime = 0;
float lastStopTime = -1.0;

// ============================================================================
// DECLARACIONES DE FUNCIONES
// ============================================================================
void toneStart(int freq = 2000, int ms = 120);
void showStatusScreen(float s = -1, bool showTime = false);
void showCountdownScreen();
void startStage();
void stopStage(uint32_t t_hit);
void resetStage();
void returnToReady();
void showResetProgress();
void handleButton();
void debugButtonState();
void handleStandby();
void updateDisplayHeader();
void processUDPPacket(int packetSize);
void handlePingMessage(char* msg);
void handleHitMessage(char* msg);
void checkPingTimeout();

// ============================================================================
// DEBUG DEL BOTÓN - PARA DIAGNÓSTIC
// ============================================================================
void debugButtonState() {
    static uint32_t lastDebug = 0;
    if (millis() - lastDebug > 500) {
        lastDebug = millis();
        bool currentState = digitalRead(PIN_START);
        Serial.printf("🔘 BOTÓN: %s | running: %d | finished: %d | lastStopTime: %.2f\n",
                     currentState ? "HIGH" : "LOW ", running, finished, lastStopTime);
    }
}

// ============================================================================
// GESTIÓN DEL BOTÓN CORREGIDA
// ============================================================================
void handleButton() {
    bool currentState = digitalRead(PIN_START);
    
    // Detección de reactivación cuando pantalla está apagada
    if (!screenActive && currentState == LOW) {
        Serial.println("👆 Reactivando pantalla desde standby");
        returnToReady();
        delay(300);
        return;
    }
    
    // 🆕 CORRECCIÓN: BLOQUEAR COMPLETAMENTE SI HAY ÚLTIMO TIEMPO MOSTRÁNDOSE
    if (finished && lastStopTime >= 0 && screenActive) {
        Serial.printf("🔒 MODO BLOQUEADO - Tiempo: %.2f s | Solo pulsación larga permitida\n", lastStopTime);
        
        // 🆕 SOLO DETECTAR PULSACIÓN LARGA
        if (lastButtonState == HIGH && currentState == LOW) {
            // Flanco descendente - botón presionado
            pressStartTime = millis();
            lastInteraction = millis();
            screenActive = true;
            Serial.println("🔽 BOTÓN PRESIONADO - Solo reinicio permitido");
        }
        
        if (lastButtonState == LOW && currentState == HIGH) {
            // Flanco ascendente - botón liberado
            uint32_t duration = millis() - pressStartTime;
            lastInteraction = millis();
            screenActive = true;
            
            Serial.printf("🔼 BOTÓN LIBERADO - Duración: %lu ms\n", duration);
            
            if (showingProgress) {
                Serial.println("⏹️ Liberado durante progreso - ignorar");
                lastButtonState = currentState;
                return;
            }
            
            // 🆕 BLOQUEAR TODAS LAS PULSACIONES CORTAS Y MEDIAS
            if (duration < LONG_PRESS_MS) {
                Serial.println("❌ Pulsación BLOQUEADA - Solo reinicio largo permitido");
                toneStart(500, 100); // Feedback de error
                
                // 🆕 MANTENER LA PANTALLA MOSTRANDO EL TIEMPO
                showStatusScreen(lastStopTime, true);
                
                lastButtonState = currentState;
                return;
            }
        }
        
        // 🆕 DETECCIÓN DE PULSACIÓN LARGA (BARRA DE PROGRESO)
        if (currentState == LOW && !showingProgress) {
            uint32_t pressDuration = millis() - pressStartTime;
            
            if (pressDuration > 1000 && pressDuration < 1500) {
                Serial.println("📊 Iniciando barra de progreso para reinicio");
                showResetProgress();
            }
        }
        
        lastButtonState = currentState;
        return; // 🆕 SALIR TEMPRANO - NO PROCESAR MÁS LÓGICA
    }
    
    // 🆕 COMPORTAMIENTO NORMAL (solo si NO hay último tiempo mostrándose)
    if (lastButtonState == HIGH && currentState == LOW) {
        pressStartTime = millis();
        lastInteraction = millis();
        screenActive = true;
        Serial.println("🔽 BOTÓN PRESIONADO - Iniciando conteo");
    }
    
    if (lastButtonState == LOW && currentState == HIGH) {
        uint32_t duration = millis() - pressStartTime;
        lastInteraction = millis();
        screenActive = true;
        
        Serial.printf("🔼 BOTÓN LIBERADO - Duración: %lu ms\n", duration);
        
        if (showingProgress) {
            Serial.println("⏹️ Liberado durante progreso - ignorar");
            return;
        }
        
        // 🆕 SOLO PERMITIR PULSACIONES CORTAS SI NO HAY TIEMPO GUARDADO
        if (duration > 100 && duration < 1000) {
            if (!running && !finished && lastStopTime < 0) {
                Serial.println("🚀 SHORT PRESS - Iniciando stage");
                startStage();
            } else if (running && !finished) {
                Serial.println("⏹️ SHORT PRESS - Deteniendo stage");
                uint32_t t_hit = micros();
                stopStage(t_hit);
            }
        } else if (duration < 100) {
            Serial.println("❌ Rebote ignorado");
        }
    }
    
    // DETECCIÓN DE PULSACIÓN LARGA PARA COMPORTAMIENTO NORMAL
    if (currentState == LOW && !showingProgress) {
        uint32_t pressDuration = millis() - pressStartTime;
        
        if (pressDuration > 1000 && pressDuration < 1500) {
            Serial.println("📊 Iniciando barra de progreso para reinicio");
            showResetProgress();
        }
    }
    
    lastButtonState = currentState;
}

// ============================================================================
// FUNCIONES DE PANTALLA
// ============================================================================
void initDisplay() {
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("❌ Error inicializando SSD1306"));
    for(;;);
  }
  display.setTextColor(SSD1306_WHITE);
  display.cp437(true);
  display.clearDisplay();
}

void showWelcomeScreen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(12, 10);
  display.println(F("AIR TIMER"));
  display.setTextSize(1);
  display.setCursor(50, 35);
  display.println(F("v1.0"));
  display.setCursor(15, 50);
  display.println(F("Inicializando..."));
  display.display();
  delay(2000);
}

// FUNCIÓN PARA MOSTRAR CUENTA REGRESIVA
void showCountdownScreen() {
  display.clearDisplay();
  
  // Header
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("Bat:"));
  display.print((int)batteryPercent);
  display.print(F("%"));
  
  display.setCursor(SCREEN_WIDTH - 25, 0);
  display.print(wifiActive ? F("OK") : F("NO"));
  
  display.setCursor(50, 0);
  display.print(F("Standby"));  // 🆕 CAMBIO: "PREPARE!" → "Standby"
  
  display.drawFastHLine(0, 9, SCREEN_WIDTH, SSD1306_WHITE);
  
  // Contador regresivo grande
  display.setTextSize(3);
  display.setCursor(50, 25);
  
  uint32_t elapsed = millis() - countdownStartTime;
  uint32_t remaining = COUNTDOWN_DELAY_MS - elapsed;
  int seconds = (remaining / 1000) + 1;
  
  display.print(seconds);
  
  display.display();
  screenActive = true;
}

// 🆕 FUNCIÓN OPTIMIZADA PARA ACTUALIZAR SOLO EL HEADER
void updateDisplayHeader() {
    // Solo actualizar si la pantalla está activa
    if (!screenActive) return;
    
    // 🆕 Limitar frecuencia de actualizaciones
    static uint32_t lastHeaderUpdate = 0;
    if (millis() - lastHeaderUpdate < 1000) return; // Máximo 1 vez por segundo
    lastHeaderUpdate = millis();
    
    // Actualizar solo la línea del header
    display.fillRect(0, 0, SCREEN_WIDTH, 9, SSD1306_BLACK); // Limpiar header
    
    display.setCursor(0, 0);
    display.print(F("Bat:"));
    display.print((int)batteryPercent);
    display.print(F("%"));
    
    display.setCursor(SCREEN_WIDTH - 25, 0);
    display.print(wifiActive ? F("OK") : F("NO"));
    
    // USAR plateConnected
    display.setCursor(50, 0);
    display.print(F("Plate:"));
    display.print(plateConnected ? F("OK") : F("--"));
    
    // Dibujar línea separadora
    display.drawFastHLine(0, 9, SCREEN_WIDTH, SSD1306_WHITE);
    
    display.display();
}

// 🆕 FUNCIÓN MEJORADA PARA MOSTRAR ESTADO Y CRONÓMETRO
void showStatusScreen(float s, bool showTime) {
  display.clearDisplay();
  
  // Header
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("Bat:"));
  display.print((int)batteryPercent);
  display.print(F("%"));
  
  display.setCursor(SCREEN_WIDTH - 25, 0);
  display.print(wifiActive ? F("OK") : F("NO"));
  
  // USAR plateConnected
  display.setCursor(50, 0);
  display.print(F("Plate:"));
  display.print(plateConnected ? F("OK") : F("--"));
  
  display.drawFastHLine(0, 9, SCREEN_WIDTH, SSD1306_WHITE);
  
  // Contenido principal - MEJORADA
  if (showTime && s >= 0) {
    // 🆕 DISEÑO MEJORADO PARA TIEMPO FINAL
    display.setTextSize(1);
    display.setCursor(35, 18);
    display.println(F("FINAL TIME"));
    
    display.setTextSize(3);
    display.setCursor(10, 28);
    
    // 🆕 FORMATEO MEJORADO - 2 decimales fijos
    char timeStr[16];
    if (s < 10) {
      snprintf(timeStr, sizeof(timeStr), " %.2f", s);
    } else if (s < 100) {
      snprintf(timeStr, sizeof(timeStr), "%.2f", s);
    } else {
      snprintf(timeStr, sizeof(timeStr), "%.1f", s); // Para tiempos largos, 1 decimal
    }
    display.print(timeStr);
    
    display.setTextSize(1);
    display.setCursor(SCREEN_WIDTH - 20, 35);
    display.print(F("s"));
    
    // 🆕 INSTRUCCIÓN MEJORADA
    display.setTextSize(1);
    display.setCursor(1, 55);
    display.println(F(">>> HOLD TO RESET <<<"));
    
  } else if (running) {
    // 🆕 DISEÑO MEJORADO PARA CRONÓMETRO EN EJECUCIÓN
    float t = (micros() - t0_us) / 1000000.0f;
    
    display.setTextSize(1);
    display.setCursor(45, 16);
    display.println(F("RUNNING"));
    
    display.setTextSize(3);
    display.setCursor(10, 28);
    
    // 🆕 CORRECCIÓN: MOSTRAR SIEMPRE 2 DECIMALES EN RUNNING
    char timeStr[16];
    if (t < 10) {
      snprintf(timeStr, sizeof(timeStr), " %.2f", t);
    } else if (t < 100) {
      snprintf(timeStr, sizeof(timeStr), "%.2f", t);
    } else {
      snprintf(timeStr, sizeof(timeStr), "%.1f", t); // Para tiempos >100s, 1 decimal
    }
    display.print(timeStr);
    
    display.setTextSize(1);
    display.setCursor(SCREEN_WIDTH - 20, 35);
    display.print(F("s"));
    
    // 🆕 INDICADOR VISUAL DE ACTIVIDAD
    display.fillCircle(SCREEN_WIDTH - 10, SCREEN_HEIGHT - 10, 3, SSD1306_WHITE);
    
  } else {
    // 🆕 DISEÑO MEJORADO PARA MODO READY - ELIMINADO PLATE STATUS
    display.setTextSize(2);
    display.setCursor(35, 25);
    display.println(F("READY"));
    
    display.setTextSize(1);
    display.setCursor(3, 50);
    display.println(F(">> PRESS TO START <<"));
  }
  
  display.display();
  screenActive = true;
  lastInteraction = millis();
}

// ============================================================================
// FUNCIONES PRINCIPALES
// ============================================================================
float readBatteryPercent() {
  int rawValue = analogRead(PIN_BATTERY);
  // Calibración para batería LiPo 3.7V
  float voltage = (rawValue / 4095.0) * 3.3 * 2;
  batteryPercent = map(constrain(voltage, 3.2, 4.2), 3.2, 4.2, 0, 100);
  return batteryPercent;
}

// FUNCIÓN toneStart CORREGIDA
void toneStart(int freq, int ms) {
  // ALTERNATIVA SIMPLE PARA BUZZER PASIVO
  if (freq == 0) {
    digitalWrite(PIN_BUZZER, LOW);
    return;
  }
  
  int period = 1000000L / freq; // Periodo en microsegundos
  int pulse = period / 2;       // Medio periodo para el duty cycle 50%
  
  uint32_t startTime = micros();
  
  while (micros() - startTime < ms * 1000L) {
    digitalWrite(PIN_BUZZER, HIGH);
    delayMicroseconds(pulse);
    digitalWrite(PIN_BUZZER, LOW);
    delayMicroseconds(pulse);
  }
}

void sendSync() {
  t0_us = micros();
  char buf[32];
  snprintf(buf, sizeof(buf), "SYNC:%lu", t0_us);
  udp.beginPacket(IPAddress(255,255,255,255), PORT);
  udp.write((uint8_t*)buf, strlen(buf));
  udp.endPacket();
  
  Serial.printf("⏱️ SYNC ENVIADO - t0_us: %lu\n", t0_us);
}

void drawProgressBar(uint32_t duration) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(23, 5);
  display.println(F("PRESS AND HOLD"));
  display.setCursor(34, 20);
  display.println(F("TO RESTART"));
  
  int barWidth = 100;
  int barHeight = 10;
  int barX = (SCREEN_WIDTH - barWidth) / 2;
  int barY = 30;
  
  display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);
  float progress = constrain((float)duration / LONG_PRESS_MS, 0.0f, 1.0f);
  int fillWidth = (int)(barWidth * progress);
  
  if (fillWidth > 0) {
    display.fillRect(barX, barY, fillWidth, barHeight, SSD1306_WHITE);
  }
  
  display.setCursor(53, 45);
  display.print((int)(progress * 100));
  display.print(F("%"));
  
  if (progress >= 1.0f) {
    display.setCursor(45, 55);
    display.println(F("RELEASE"));
  } else {
    display.setCursor(15, 55);
    display.println(F("RELEASE TO CANCEL"));
  }
  
  display.display();
}

void showResetProgress() {
  showingProgress = true;
  progressStartTime = millis();
  Serial.println("📊 Mostrando barra de progreso para reinicio");
  
  uint32_t lastBeepTime = 0;
  bool buttonStillPressed = true;
  
  while (buttonStillPressed) {
    uint32_t elapsed = millis() - progressStartTime;
    uint32_t currentTime = millis();
    
    drawProgressBar(elapsed);
    
    if (currentTime - lastBeepTime >= 1000) {
      lastBeepTime = currentTime;
      toneStart(800, 50);
      Serial.printf("⏳ Progreso: %lu/%lu ms (%d%%)\n", elapsed, LONG_PRESS_MS, (int)((float)elapsed / LONG_PRESS_MS * 100));
    }
    
    buttonStillPressed = (digitalRead(PIN_START) == LOW);
    delay(50);
  }
  
  uint32_t finalElapsed = millis() - progressStartTime;
  
  if (finalElapsed >= LONG_PRESS_MS) {
    toneStart(1500, 200);
    resetStage();
    Serial.println("✅ Reinicio confirmado y ejecutado");
  } else {
    toneStart(500, 100);
    Serial.println("❌ Reinicio cancelado");
    
    if (lastStopTime >= 0) {
      showStatusScreen(lastStopTime, true);
    } else {
      showStatusScreen();
    }
  }
  
  showingProgress = false;
}

void startStage() {
  finished = false;
  // 🆕 RESETEAR lastStopTime AL INICIAR NUEVO STAGE
  lastStopTime = -1.0;
  
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.softAP(ssid, password);
  }
  sendSync();
  
  // INICIAR CONTADOR REGRESIVO SIN PITIDOS
  countdownActive = true;
  countdownStartTime = millis();
  lastInteraction = millis();
  screenActive = true;
  
  Serial.println("⏱️ Iniciando cuenta regresiva de 2 segundos...");
  showCountdownScreen();
}

void stopStage(uint32_t t_hit) {
  running = false;
  finished = true;
  
  // CALCULAR TIEMPO FINAL
  float s = (t_hit - t0_us) / 1000000.0f;
  lastStopTime = s;
  
  Serial.printf("⏹️ STOP STAGE - Tiempo final: %.2f segundos\n", s);
  showStatusScreen(s, true);
}

void resetStage() {
  running = false;
  finished = false;
  countdownActive = false;
  // 🆕 RESETEA EL ESTADO DEL PLATE Y EL TIEMPO
  plateConnected = false;
  lastStopTime = -1.0;
  
  if (WiFi.softAPgetStationNum() > 0) {
    WiFi.disconnect(true);
  }
  wifiActive = false;
  
  // SONIDOS ORIGINALES DEL RESET
  toneStart(1000, 80); 
  delay(80);
  toneStart(1500, 80);
  
  lastInteraction = millis();
  screenActive = true;
  showStatusScreen();
}

void returnToReady() {
    running = false;
    finished = false;
    wifiActive = false;
    lastInteraction = millis();
    screenActive = true;
    if (lastStopTime >= 0) {
        showStatusScreen(lastStopTime, true);
    } else {
        showStatusScreen();
    }
}

// 🆕 FUNCIÓN STANDBY MEJORADA CON "DISPLAY OFF"
void handleStandby() {
    uint32_t currentTime = millis();
    uint32_t inactiveTime = currentTime - lastInteraction;
    
    if (inactiveTime > STANDBY_TIMEOUT_MS && screenActive && !showingProgress && !running && !countdownActive) {
        Serial.println("💤 Entrando en modo standby...");
        uint32_t animationStart = currentTime;
        bool visible = true;
        
        while (currentTime - animationStart < 4000) {
            display.clearDisplay();
            if (visible) {
                display.setTextSize(2);
                display.setCursor(50, 20);
                display.println(F("Zz"));
                display.setTextSize(1);
                display.setCursor(35, 45);
                display.println(F("Display off"));  // 🆕 CAMBIO AQUÍ
            }
            display.display();
            visible = !visible;
            
            if (digitalRead(PIN_START) == LOW) {
                returnToReady();
                return;
            }
            
            delay(500);
            currentTime = millis();
        }
        
        display.clearDisplay();
        display.display();
        screenActive = false;
        Serial.println("💤 Pantalla apagada - Display off");
    }
}

// 🆕 FUNCIÓN PARA VERIFICAR TIMEOUT DE PING
void checkPingTimeout() {
    static uint32_t lastPingCheck = 0;
    uint32_t currentTime = millis();
    
    if (currentTime - lastPingCheck >= 1000) {  // 🆕 Reducir a 1 segundo
        lastPingCheck = currentTime;
        
        bool previousState = plateConnected;
        
        // Si pasó el timeout sin PINGs, marcar como desconectado
        if (plateConnected && (currentTime - lastPingTime > PING_TIMEOUT_MS)) {
            plateConnected = false;
            Serial.println("🔌 PLATE DESCONECTADO - Timeout de ping");
        }
        
        // 🆕 Solo actualizar pantalla si el estado cambió Y la pantalla está activa
        if (previousState != plateConnected && screenActive && !showingProgress) {
            updateDisplayHeader();
        }
    }
}

// 🆕 FUNCIÓN PARA MANEJAR MENSAJES PING
void handlePingMessage(char* msg) {
    uint16_t seq = atoi(msg + 5);
    lastPingTime = millis();
    
    if (!plateConnected) {
        plateConnected = true;
        Serial.println("🔌 PLATE CONECTADO - Ping recibido");
        // 🆕 Solo actualizar si la pantalla ya está activa
        if (screenActive && !showingProgress) {
            updateDisplayHeader();
        }
    }
    
    // Responder con PONG
    char pong[16];
    snprintf(pong, sizeof(pong), "PONG:%u", seq);
    udp.beginPacket(udp.remoteIP(), PORT);
    udp.write((uint8_t*)pong, strlen(pong));
    udp.endPacket();
    Serial.printf("📤 PONG enviado: %s\n", pong);
}

// 🆕 FUNCIÓN PARA MANEJAR MENSAJES HIT
void handleHitMessage(char* msg) {
    // LOS HITS SÍ REACTIVAN LA PANTALLA
    screenActive = true;
    lastInteraction = millis();
    
    lastPingTime = millis();
    if (!plateConnected) {
        plateConnected = true;
        Serial.println("🔌 PLATE CONECTADO - Hit recibido");
    }
    
    char* p = msg + 4;
    int id = atoi(strtok(p, ":"));
    // IGNORAR EL TIEMPO DEL CLIENTE, SOLO NECESITAMOS EL SEQ
    strtok(NULL, ":"); // Saltar el tiempo del cliente
    uint16_t seq = atoi(strtok(NULL, ":"));
    
    // USAR EL TIEMPO ACTUAL DEL ESP32 (IGUAL QUE EL BOTÓN)
    uint32_t current_time = micros();
    float s = (current_time - t0_us) / 1000000.0f;
    
    Serial.printf("🎯 HIT RECIBIDO - ID: %d, Tiempo: %.2f s\n", id, s);
    showStatusScreen(s, true);
    
    // PITIDOS DIFERENCIADOS SEGÚN EL ID
    if (id == 99) {
        toneStart(1800, 300);
        Serial.println("🛑 STOP PLATE - Stage finalizado");
    } else {
        toneStart(1500, 150);
        Serial.printf("🎯 HIT RECIBIDO - ID %d\n", id);
    }
    
    // PARAR CON EL TIEMPO ACTUAL (IGUAL QUE EL BOTÓN)
    stopStage(current_time);
    
    // Enviar ACK
    char ack[16];
    snprintf(ack, sizeof(ack), "ACK:%u", seq);
    udp.beginPacket(udp.remoteIP(), PORT);
    udp.write((uint8_t*)ack, strlen(ack));
    udp.endPacket();
    Serial.printf("📤 ACK enviado: %s\n", ack);
}

// 🆕 FUNCIÓN PARA PROCESAR PAQUETES UDP
void processUDPPacket(int packetSize) {
    lastPacketTime = millis();
    wifiActive = true;
    
    // 🆕 NO reactivar lastInteraction automáticamente
    // Solo mantener los estados de conexión
    
    char msg[64];
    int len = udp.read(msg, 64);
    if (len > 0) {
        msg[len] = '\0';
        
        Serial.printf("📦 PAQUETE RECIBIDO: %s\n", msg);
        
        if (strncmp(msg, "PING:", 5) == 0) {
            handlePingMessage(msg);
        } else if (strncmp(msg, "HIT:", 4) == 0 && running && !finished) {
            handleHitMessage(msg); // 🆕 Esta SÍ reactiva la pantalla
        }
    }
}

void loop() {
    uint32_t currentTime = millis();
    
    // 1. VERIFICAR TIMEOUT DE PING (OPTIMIZADO)
    checkPingTimeout();
    
    // 2. VERIFICAR SI ESTAMOS EN CUENTA REGRESIVA
    if (countdownActive) {
        uint32_t elapsed = currentTime - countdownStartTime;
        
        // Actualizar pantalla de cuenta regresiva
        if (currentTime - lastUpdate >= 100) {
            lastUpdate = currentTime;
            showCountdownScreen();
        }
        
        // CUANDO TERMINA LA CUENTA REGRESIVA, INICIAR EL STAGE CON PITIDO
        if (elapsed >= COUNTDOWN_DELAY_MS) {
            countdownActive = false;
            running = true;
            t0_us = micros();
            lastUpdate = currentTime;
            toneStart(2000, 200);
            Serial.println("🚀 CRONÓMETRO INICIADO!");
        }
        
        delay(10);
        return;
    }
    
    // 3. PROCESAR PAQUETES UDP (OPTIMIZADO)
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
        processUDPPacket(packetSize);
    }
    
    // 4. ACTUALIZAR PANTALLA CADA 100ms PARA TIEMPO EN VIVO
    if (screenActive && running && !showingProgress) {
        if (currentTime - lastUpdate >= 100) {
            lastUpdate = currentTime;
            showStatusScreen();
        }
    }
    
    // 5. DEBUG Y BOTÓN
    debugButtonState();
    handleButton();
    
    // 6. ACTUALIZAR BATERÍA PERIÓDICAMENTE
    static uint32_t lastBatteryCheck = 0;
    if (currentTime - lastBatteryCheck >= 5000) { // Cada 5 segundos
        lastBatteryCheck = currentTime;
        readBatteryPercent();
        if (screenActive && !showingProgress) {
            updateDisplayHeader();
        }
    }
    
    // 7. STANDBY (AHORA DEBERÍA FUNCIONAR CORRECTAMENTE)
    handleStandby();
    
    delay(10);
}

void setup() {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW); // Asegurar que empiece apagado
  
  // CONFIGURACIÓN DEL BOTÓN
  Serial.begin(115200);
  Serial.println("🎯 Iniciando Air Timer - DEBUG ACTIVADO");
  Serial.println("🔘 Configurando botón BOOT/EN...");
  
  pinMode(PIN_START, INPUT_PULLUP);
  
  // Test inicial del botón
  bool initialState = digitalRead(PIN_START);
  Serial.printf("✅ Botón inicializado. Estado inicial: %s\n", 
               initialState ? "HIGH (no presionado)" : "LOW (PRESIONADO)");
  Serial.printf("📍 Usando GPIO: %d\n", PIN_START);
  
  analogReadResolution(12);
  WiFi.softAP(ssid, password);
  udp.begin(PORT);
  Wire.begin(21, 22);
  initDisplay();
  
  showWelcomeScreen();
  showStatusScreen();
  lastInteraction = millis();
  
  Serial.println("🚀 Sistema listo - Display mejorado y modo standby optimizado");
  Serial.println("💤 La pantalla mostrará 'Display off' antes de apagarse");
  Serial.printf("⏱️  Pulsación larga reducida a %lu segundos\n", LONG_PRESS_MS / 1000);
}