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
// 🔧 CORRECCIÓN: Probamos diferentes pines para el botón BOOT
const int PIN_START = 0;    // GPIO 0 - Botón BOOT (probemos este primero)
// const int PIN_START = 2;  // Alternativa si GPIO 0 no funciona
// const int PIN_START = 4;  // Otra alternativa
const int PIN_BATTERY = 36;
const int STOP_ID = 99;

// ============================================================================
// VARIABLES GLOBALES
// ============================================================================
volatile bool running = false;
volatile bool finished = false;
volatile bool wifiActive = false;
volatile bool screenActive = true;

uint32_t lastPacketTime = 0;
uint32_t t0_us = 0;
uint32_t lastUpdate = 0;
uint32_t lastInteraction = 0;
float batteryPercent = 100.0;

// Variables del botón
volatile bool lastButtonState = HIGH;
uint32_t pressStartTime = 0;
const uint32_t LONG_PRESS_MS = 5000;
const uint32_t STANDBY_TIMEOUT_MS = 60000;

// Variables para la barra de progreso
volatile bool showingProgress = false;
uint32_t progressStartTime = 0;
float lastStopTime = -1.0;

// ============================================================================
// DEBUG DEL BOTÓN - PARA DIAGNÓSTICO
// ============================================================================
void debugButtonState() {
    static uint32_t lastDebug = 0;
    if (millis() - lastDebug > 500) {
        lastDebug = millis();
        bool currentState = digitalRead(PIN_START);
        Serial.printf("🔘 BOTÓN: %s | running: %d | finished: %d\n",
                     currentState ? "HIGH" : "LOW ", running, finished);
    }
}

// ============================================================================
// GESTIÓN DEL BOTÓN CORREGIDA
// ============================================================================

void handleButton() {
    bool currentState = digitalRead(PIN_START);
    
    // 🆕 DEBUG EN TIEMPO REAL
    static uint32_t lastStateChange = 0;
    if (currentState != lastButtonState) {
        lastStateChange = millis();
        Serial.printf("🔄 Cambio estado: %s -> %s\n", 
                     lastButtonState ? "HIGH" : "LOW", 
                     currentState ? "HIGH" : "LOW");
    }
    
    // Detección de reactivación cuando pantalla está apagada
    if (!screenActive && currentState == LOW) {
        Serial.println("👆 Reactivando pantalla desde standby");
        returnToReady();
        delay(300); // Debounce más largo para botón físico
        return;
    }
    
    // 🆕 DETECCIÓN MEJORADA DE FLANCOS
    if (lastButtonState == HIGH && currentState == LOW) {
        // Flanco descendente - botón presionado
        pressStartTime = millis();
        lastInteraction = millis();
        screenActive = true;
        Serial.println("🔽 BOTÓN PRESIONADO - Iniciando conteo");
    }
    
    if (lastButtonState == LOW && currentState == HIGH) {
        // Flanco ascendente - botón liberado
        uint32_t duration = millis() - pressStartTime;
        lastInteraction = millis();
        screenActive = true;
        
        Serial.printf("🔼 BOTÓN LIBERADO - Duración: %lu ms\n", duration);
        
        // Si estaba mostrando progreso, ya se manejó en showResetProgress()
        if (showingProgress) {
            Serial.println("⏹️ Liberado durante progreso - ignorar");
            return;
        }
        
        // Procesar pulsaciones normales (evitar rebotes)
        if (duration > 100 && duration < 1000) { // 🔧 Aumentado debounce
            if (!running && !finished) {
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
    
    // 🆕 DETECCIÓN MEJORADA DE PULSACIÓN LARGA
    if (currentState == LOW && !showingProgress) {
        uint32_t pressDuration = millis() - pressStartTime;
        
        // Informar progreso de pulsación larga
        if (pressDuration > 1000 && pressDuration < 1500) {
            Serial.println("📊 Iniciando barra de progreso para reinicio");
            showResetProgress();
        }
        
        // Feedback cada segundo durante pulsación larga
        if (pressDuration > 1000) {
            static uint32_t lastFeedback = 0;
            if (millis() - lastFeedback > 1000) {
                lastFeedback = millis();
                Serial.printf("⏳ Mantenido: %lu/%lu segundos\n", 
                             pressDuration/1000, LONG_PRESS_MS/1000);
            }
        }
    }
    
    lastButtonState = currentState;
}

// ============================================================================
// FUNCIONES DE PANTALLA (optimizadas)
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

void showStatusScreen(float s = -1, bool showTime = false) {
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
  display.print(F("End:"));
  display.print(wifiActive ? F("OK") : F("--"));
  
  display.drawFastHLine(0, 9, SCREEN_WIDTH, SSD1306_WHITE);
  
  // Contenido principal
  if (showTime && s >= 0) {
    display.setTextSize(2);
    display.setCursor(25, 25);
    display.print(s, 2);
    display.print(F(" s"));
    display.setTextSize(1);
    display.setCursor(20, 50);
    display.println(F("Hold to restart"));
  } else if (running) {
    float t = (micros() - t0_us) / 1000000.0f;
    display.setTextSize(3);
    display.setCursor(15, 20);
    display.print(t, 1);
    display.setTextSize(1);
    display.setCursor(95, 25);
    display.print(F("s"));
  } else {
    display.setTextSize(2);
    display.setCursor(36, 25);
    display.println(F("READY"));
    display.setTextSize(1);
    display.setCursor(22, 50);
    display.println(F("Press to start"));
  }
  
  display.display();
  screenActive = true;
  lastInteraction = millis();
}

// ============================================================================
// FUNCIONES PRINCIPALES (mantenemos igual)
// ============================================================================

float readBatteryPercent() {
  // ... (tu código existente)
  return batteryPercent;
}

void toneStart(int freq = 2000, int ms = 120) {
  tone(PIN_BUZZER, freq, ms);
}

void sendSync() {
  t0_us = micros();
  char buf[32];
  snprintf(buf, sizeof(buf), "SYNC:%lu", t0_us);
  udp.beginPacket(IPAddress(255,255,255,255), PORT);
  udp.write((uint8_t*)buf, strlen(buf));
  udp.endPacket();
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
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.softAP(ssid, password);
  }
  sendSync();
  toneStart(1000, 80); 
  delay(150);
  toneStart(2000, 140);
  t0_us = micros();
  running = true;
  lastUpdate = millis();
  lastInteraction = millis();
  screenActive = true;
}

void stopStage(uint32_t t_hit) {
  running = false;
  finished = true;
  float s = (t_hit - t0_us) / 1000000.0f;
  lastStopTime = s;
  toneStart(1500, 60); 
  delay(60);
  toneStart(800, 120);
  showStatusScreen(s, true);
}

void resetStage() {
  running = false;
  finished = false;
  if (WiFi.softAPgetStationNum() > 0) {
    WiFi.disconnect(true);
  }
  wifiActive = false;
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

void handleStandby() {
    uint32_t currentTime = millis();
    uint32_t inactiveTime = currentTime - lastInteraction;
    
    if (inactiveTime > STANDBY_TIMEOUT_MS && screenActive && !showingProgress) {
        Serial.println("💤 Entrando en modo standby...");
        uint32_t animationStart = currentTime;
        bool visible = true;
        
        while (currentTime - animationStart < 4000) {
            display.clearDisplay();
            if (visible) {
                display.setTextSize(2);
                display.setCursor(45, 20);
                display.println(F("Zz"));
                display.setTextSize(1);
                display.setCursor(35, 45);
                display.println(F("Display off"));
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
    }
}

void loop() {
    uint32_t currentTime = millis();
    
    // 🆕 DEBUG ACTIVADO - Comenta esta línea cuando funcione
    debugButtonState();
    
    handleButton();
    
    if (screenActive && running && !showingProgress) {
        if (currentTime - lastUpdate >= 100) {
            lastUpdate = currentTime;
            showStatusScreen();
        }
    }
    
    if (currentTime - lastPacketTime > 5000) {
        wifiActive = false;
    }
    
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
        lastPacketTime = currentTime;
        wifiActive = true;
        lastInteraction = currentTime;
        screenActive = true;

        char msg[64];
        int len = udp.read(msg, 64);
        if (len > 0) {
            msg[len] = '\0';
            if (strncmp(msg, "HIT:", 4) == 0 && running && !finished) {
                char* p = msg + 4;
                int id = atoi(strtok(p, ":"));
                uint32_t t_hit = strtoul(strtok(NULL, ":"), nullptr, 10);
                uint16_t seq = atoi(strtok(NULL, ":"));
                float s = (t_hit - t0_us) / 1000000.0f;
                showStatusScreen(s, true);
                if (id == STOP_ID) {
                    stopStage(t_hit);
                }
                char ack[16];
                snprintf(ack, sizeof(ack), "ACK:%u", seq);
                udp.beginPacket(udp.remoteIP(), PORT);
                udp.write((uint8_t*)ack, strlen(ack));
                udp.endPacket();
            }
        }
    }
    
    handleStandby();
    delay(10);
}

void setup() {
  pinMode(PIN_BUZZER, OUTPUT);
  
  // 🔧 CONFIGURACIÓN MEJORADA DEL BOTÓN
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
  
  Serial.println("🚀 Sistema listo - Monitorea el Serial para debug del botón");
}