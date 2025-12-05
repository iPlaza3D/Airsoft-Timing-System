# Action Air IPSC System
Dispositivos de Crono para Action Air IPSC

# Timing System (ESP32 Edition)

Proyecto DIY basado en el **M4P1+ Airsoft Timing System de Special Pie**, adaptado con un **ESP32 CH340C WiFi + Bluetooth** y componentes adicionales para crear un sistema de cronometraje y control de competición.  
El sistema consta de **dos dispositivos**:
1. **Crono (Chrono Unit)** → da la salida al tirador y hace de **servidor** para el "End plate".  
2. **End Plate (Plate Unit)** → cliente conectado al crono que registra el tiempo final del ejercicio.

---

## 🚀 Características principales
- ⏱️ Cronómetro de alta precisión (microsegundos)
- 📡 Comunicación WiFi UDP (modo AP)
- 🔋 Alimentación por batería LiPo con carga USB
- 🎨 Interfaz OLED optimizada
- 🔊 Feedback sonoro diferenciado
- 🔌 Sistema de ping/pong para detección de conexión del End Plate
- 💤 Modo reposo para ahorro energético

---

## 🛠️ Hardware utilizado Crono
- **ESP32 CH340C WiFi + Bluetooth**
- **Pantalla OLED 0,96" I2C SSD1306 (128x64)**
- **Zumbador pasivo 42R 12085 (12mm x 8,5mm)**
- **Módulo de carga TP4056**
- **Batería LiPo**
- **Interruptor básico**

## 📐 Esquema básico de conexión

```
ESP32      → Componente
───────────────────────
Vin (5V)   → TP4056 VOut+
GND        → TP4056 VOut- + OLED GND + Buzzer GND
GPIO21     → OLED SDA
GPIO22     → OLED SCL
GPIO25     → Buzzer +
GPIO36     → Divisor batería (100kΩ/100kΩ)
3.3V       → OLED VCC
```
---

## 🛠️ Hardware utilizado End Plate
- **ESP32 CH340C WiFi + Bluetooth**
- **Piezoeléctrico placa de oblea de cerámica**
- **Módulo de carga TP4056**
- **Batería LiPo**
- **Interruptor básico**

## 📐 Esquema básico de conexión
```
ESP32      → Componente
───────────────────────
Vin (5V)   → TP4056 VOut+
GND        → TP4056 VOut-
GPIO4      → Piezoeléctrico [+]
GND        → Piezoeléctrico [-]
GPIO0      → Botón BOOT/EN (ya integrado)
GPIO2      → LED interno (feedback)
```
---

*(Los pines pueden ajustarse según tu firmware.)*

---

## ⚙️ Instalación

Abre el proyecto en Arduino IDE o PlatformIO.

Instala las librerías necesarias:

- Adafruit SSD1306
- Adafruit GFX
- BluetoothSerial.h (incluida en ESP32)
- WiFi.h (incluida en ESP32)
- WiFiUdp.h (incluida en ESP32)

Compila y sube el firmware a cada ESP32

---

## 🛠️ Operación:
- Iniciar stage: Pulsación corta botón BOOT/EN
- Impacto normal: End Plate envía tiempo vía WiFi
- Impacto final (Plate ID 99): Detiene cronómetro (Tiempo fin de ejercicio)
- Detener manual: Pulsación corta durante ejecución
- Reiniciar: Pulsación larga (3 segundos)

---

## ⚙️ Configuración Avanzada

```
1. Modificar SSID/Password:
  En ambos firmwares:
     const char* ssid = "AIRSOFT Stage 01";
     const char* password = "12345678";

2. Ajustar sensibilidad piezoeléctrico:
  En End Plate:
     const int VOLTAGE_THRESHOLD = 500;  #Valor más bajo = más sensible

3. Modificar tiempos:
  En Cronómetro:
     const uint32_t COUNTDOWN_DELAY_MS = 2000;  #Cuenta regresiva
     const uint32_t STANDBY_TIMEOUT_MS = 60000; #Tiempo reposo

```
