# Action Air IPSC System
Dispositivos de Crono para Action Air IPSC

# Timing System (ESP32 Edition)

Proyecto DIY basado en el **M4P1+ Airsoft Timing System de Special Pie**, adaptado con un **ESP32 CH340C WiFi + Bluetooth** y componentes adicionales para crear un sistema de cronometraje y control de competición.  
El sistema consta de **dos dispositivos**:
1. **Crono (Chrono Unit)** → da la salida al tirador y hace de **servidor** para el "End plate".  
2. **End Plate (Plate Unit)** → cliente conectado al crono que registra el tiempo final del ejercicio.

---

## 🚀 Características principales
- Cronógrafo con servidor integrado (WiFi).
- Pantalla OLED de 0,96" (SSD1306, I2C, 128x64).
- Zumbador pasivo para alertas acústicas.
- Alimentación mediante batería LiPo con módulo de carga TP4056.
- Conectividad WiFi y Bluetooth para expansión futura.
- Diseño modular y portátil.

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
           ┌─────────────────────────────────┐
           │         ESP32 (CRONÓMETRO)      │
           │                                 │
BATERÍA →  │ Vin (5V)  ◄─── TP4056 VOut+     │
           │   GND     ◄─── TP4056 VOut-     │
           │                                 │
           │ GPIO0  ──► BUTTON (BOOT/EN) ──┐ │
           │          (a GND cuando presiona)│
           │                                 │
           │ GPIO25 ──► BUZZER [+]           │
           │   GND  ◄── BUZZER [-]           │
           │                                 │
           │ GPIO21 ──► OLED SDA             │
           │ GPIO22 ──► OLED SCL             │
           │  3.3V  ──► OLED VCC             │
           │   GND  ──► OLED GND             │
           │                                 │
           │ GPIO36 ◄── DIVISOR BATERÍA      │
           │          (100kΩ/100kΩ de Bat+)  │
           └─────────────────────────────────┘
                    │
                    ▼
           ┌─────────────────────────────────┐
           │        TP4056 + BATERÍA         │
           │                                 │
           │ Bat+  ◄── BATERÍA LiPo [+]      │
           │ Bat-  ◄── BATERÍA LiPo [-]      │
           │                                 │
           │ Micro USB ──► CARGA (5V)        │
           │                                 │
           │ VOut+ ──► ESP32 Vin (5V)        │
           │ VOut- ──► ESP32 GND             │
           │                                 │
           │ Bat+  ──► DIVISOR 100kΩ/100kΩ   │
           │          └──► GPIO36 (lectura)  │
           └─────────────────────────────────┘
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
           ┌─────────────────────────────────┐
           │         ESP32 (END PLATE)       │
           │                                 |
BATERÍA →  │ Vin (5V)  ◄─── TP4056 VOut+     |
           │   GND     ◄─── TP4056 VOut-     │
           │                                 │
           │ GPIO0  ──► BUTTON (BOOT/EN) ──┐ │
           │          (a GND cuando presiona)│
           │                                 │
           │ GPIO4  ◄── PIEZO [+]            │
           │   GND  ◄── PIEZO [-]            │
           │                                 │
           │ GPIO2  ──► LED INTERNO          │
           │          (con resistor 220Ω)    │
           └─────────────────────────────────┘
                    │
                    ▼
           ┌─────────────────────────────────┐
           │        TP4056 + BATERÍA         │
           │                                 │
           │ Bat+  ◄── BATERÍA 18650 [+]     │
           │ Bat-  ◄── BATERÍA 18650 [-]     │
           │                                 │
           │ Micro USB ──► CARGA (5V)        │
           │                                 │
           │ VOut+ ──► ESP32 Vin (5V)        │
           │ VOut- ──► ESP32 GND             │
           └─────────────────────────────────┘
```
---

*(Los pines pueden ajustarse según tu firmware.)*

---

## ⚙️ Instalación

Abre el proyecto en Arduino IDE o PlatformIO.

Instala las librerías necesarias:
  Adafruit SSD1306
  Adafruit GFX
  WiFi.h (incluida en ESP32)
  BluetoothSerial.h (incluida en ESP32)

Compila y sube el firmware a cada ESP32
