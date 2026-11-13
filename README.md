# BWifi-ESP8266 — Suite Multi-Herramienta RF, WiFi, BLE-over-nRF24 & Arcade para ESP8266

[![Platform](https://img.shields.io/badge/platform-ESP8266%20(NodeMCU%20%2F%20D1%20Mini)-blue)](https://www.espressif.com/en/products/socs/esp8266)
[![Framework](https://img.shields.io/badge/framework-Arduino%20%2F%20PlatformIO-00979D)](https://platformio.org/)
[![Display](https://img.shields.io/badge/OLED-SSD1306%20128x64%20I2C-purple)](https://github.com/olikraus/u8g2)
[![Radio](https://img.shields.io/badge/RF-Single%2FDual%20nRF24L01%2B%20PA%2BLNA-red)](https://nrf24.github.io/RF24/)
[![Control](https://img.shields.io/badge/Input-Rotary%20Encoder%20HW--040-orange)](#sistema-de-control-con-rotary-encoder-hw-040)

---

**BWifi-ESP8266 (BWifi8266 v3.5)** es la edición para **ESP8266 (`ESP8266EX @ 160 MHz`, NodeMCU v2/v3 o Wemos D1 Mini)** de la suite **BWifi v3.5**. Conserva los **22 módulos**, el carrusel gráfico animado de 3 iconos simultáneos, el control determinista mediante **Rotary Encoder HW-040 (KY-040)**, los **7 submodos de defensa y auditoría WiFi** (`Probe+MAC RND`, `Heatmap`, `Wardrive+Kismet`, `Rogue AP/Evil Twin`, `Hidden SSID Unmasker`, `Karma/Pineapple`, `PCAP .pcap`), el analizador de espectro con **4 vistas** (`Barras`, `Waterfall 2D`, `RF Fingerprint IA` y `MouseJack / ESB Lab`), el intérprete **Ducky Script (`/payload.txt`)** con selector `ES/EN`, el sistema de archivos **`LittleFS`**, captura de pantalla OLED a `/screen.bmp` y los **10 juegos Arcade** (`Snake`, `Pong`, `Flappy`, `Invaders`, `Dino`, `Breakout`, `Simon`, `Tetris`, `Asteroids`, `2048`), incorporando además **síntesis y escaneo de tramas BLE reales (`ADV_NONCONN_IND`) con estimador de distancia en metros a través del transceptor `nRF24L01+`**.

---

## Tabla de Contenidos

- [Aviso de Uso Ético y Legal (Modo LAB)](#aviso-de-uso-ético-y-legal-modo-lab)
- [Adaptaciones clave de la Arquitectura ESP8266](#adaptaciones-clave-de-la-arquitectura-esp8266)
- [Requisitos de Hardware y Pinout Paso a Paso (NodeMCU / Wemos D1 Mini)](#requisitos-de-hardware-y-pinout-paso-a-paso-nodemcu--wemos-d1-mini)
- [Instalación y Compilación Paso a Paso](#instalación-y-compilación-paso-a-paso)
- [Guía de los 22 Módulos en ESP8266](#guía-de-los-22-módulos-en-esp8266)

---

## Aviso de Uso Ético y Legal (Modo LAB)

> **IMPORTANTE — SOLO PARA AUDITORÍA AUTORIZADA Y ENTORNOS DE LABORATORIO (`LAB MODE`)**
>
> Este firmware ha sido desarrollado exclusivamente con fines **educativos, de investigación en ciberseguridad, diagnóstico de espectro RF y pruebas de robustez en redes propias o explícitamente autorizadas**.

---

## Adaptaciones clave de la Arquitectura ESP8266

| Subsistema | En `BWifi-ESP32` | En `BWifi-ESP8266` |
|---|---|---|
| **MCU y Frecuencia** | ESP32 Dual-Core @ 240 MHz | ESP8266EX @ **160 MHz** (`board_build.f_cpu = 160000000L`) |
| **Inyección y Sniffing WiFi** | `esp_wifi_80211_tx` / `esp_wifi_set_promiscuous` | `wifi_send_pkt_freedom()` / `wifi_set_promiscuous_rx_cb()` (`<user_interface.h>`) |
| **BLE Spam, iBeacon y Radar AirTag** | Controlador BLE interno del ESP32 | **Síntesis y recepción BLE GFSK a 1 Mbps sobre `nRF24L01+`** (`BleRf24` en [`include/esp8266_compat.h`](include/esp8266_compat.h)) con *whitening* LFSR y `CRC24` sobre los canales publicitarios BLE **37 (`RF 2`), 38 (`RF 26`) y 39 (`RF 80`)** |
| **Radios `nRF24L01+`** | Dual Radio (`jam1` + `jam2`) | **Auto-Detección Single / Dual Radio**: Funciona con **1 solo `nRF24L01+` (`jam1`)** cubriendo los 125 canales, o con **2 `nRF24L01+` (`jam1` + `jam2`)** si se conecta el segundo CSN en `GPIO 10 (SD3)` |
| **Almacenamiento y NVS** | `SPIFFS` + `Preferences` | `LittleFS` (`/logins.txt`, `/wardrive.csv`, `/trackers.txt`) + `EEPROM` (`AppSettings`) |
| **Optimización de RAM (80 KB DRAM)** | Plantillas en memoria | Plantillas HTML servidas directamente desde Flash con `PROGMEM` y `send_P()` |

---

## Requisitos de Hardware y Pinout Paso a Paso (NodeMCU / Wemos D1 Mini)

El pinout ha sido diseñado respetando los pines de arranque (*bootstrapping*) del ESP8266 (`D3/GPIO 0` y `D4/GPIO 2` en `HIGH`, `D8/GPIO 15` en `LOW` al encender) y reservando pines con soporte de interrupción (`IRAM_ATTR`) para el encoder:

### Paso 1: Pantalla OLED SSD1306 128×64 (I2C)

| Pin del OLED | Pin NodeMCU / D1 Mini | GPIO ESP8266 | Función |
|---|---|---|---|
| **`SCL`** | **`D1`** | **`GPIO 5`** | Reloj I2C Hardware |
| **`SDA`** | **`D2`** | **`GPIO 4`** | Datos I2C Hardware |
| **`VCC`** | **`3V3`** | — | Alimentación 3.3V |
| **`GND`** | **`GND`** | — | Tierra común |

### Paso 2: Rotary Encoder HW-040 (KY-040)

| Pin del HW-040 | Pin NodeMCU / D1 Mini | GPIO ESP8266 | Función |
|---|---|---|---|
| **`CLK`** | **`D3`** | **`GPIO 0`** | Fase A (`INPUT_PULLUP` + ISR en RAM; nivel alto seguro en arranque) |
| **`DT`** | **`D4`** | **`GPIO 2`** | Fase B (`INPUT_PULLUP` + ISR en RAM; nivel alto seguro en arranque) |
| **`SW`** | **`RX`** | **`GPIO 3`** | Pulsador integrado (`OK` corto / `BACK` largo; `Serial` usa `TX-only` en `GPIO 1`) |
| **`+` (VCC)** | **`3V3`** | — | Alimentación 3.3V |
| **`GND`** | **`GND`** | — | Tierra común |

### Paso 3: Módulos RF nRF24L01+PA+LNA (SPI Hardware HSPI)

| Señal del nRF24L01+ | Radio #1 (`jam1` — Principal) | Radio #2 (`jam2` — Opcional) | Descripción |
|---|---|---|---|
| **`SCK`** | **`D5` (`GPIO 14`)** | **`D5` (`GPIO 14`)** | Reloj SPI Hardware compartido |
| **`MISO`** | **`D6` (`GPIO 12`)** | **`D6` (`GPIO 12`)** | MISO SPI Hardware compartido |
| **`MOSI`** | **`D7` (`GPIO 13`)** | **`D7` (`GPIO 13`)** | MOSI SPI Hardware compartido |
| **`CE`** | **`D0` (`GPIO 16`)** | **`D0` (`GPIO 16`)** | Chip Enable compartido |
| **`CSN`** | **`D8` (`GPIO 15`)** | **`SD3` (`GPIO 10`)** | Chip Select independiente (`board_build.flash_mode = dio`) |
| **`VCC`** | **3.3V Regulado** | **3.3V Regulado** | Con condensador de 10 µF – 100 µF entre `VCC` y `GND` |
| **`GND`** | **`GND`** | **`GND`** | Tierra común |

> **Nota sobre 1 o 2 radios**: Si solo conectas el **Radio #1 (`jam1` en `D0`/`D8`)**, el firmware detecta automáticamente la configuración **1-Radio** al arrancar y ejecuta el analizador de 125 canales (con Waterfall), Jammer, BLE Spam, Radar AirTag e iBeacon íntegramente sobre `jam1`.

---

## Instalación y Compilación Paso a Paso

1. **Abrir el directorio del proyecto**:
   ```powershell
   cd C:\Users\Jorge\Desktop\BWifi-ESP8266
   ```

2. **Compilar el firmware**:
   ```powershell
   & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
   ```

3. **Subir el firmware por USB**:
   ```powershell
   & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t upload
   ```

---

## Guía de los 22 Módulos en ESP8266

1. **`01. WIFI SCANNER`** ([src/wifiscan.cpp](src/wifiscan.cpp)): Escáner WiFi con marquesina, detalles de red, conexión a redes abiertas y clonación de objetivo.
2. **`02. WIFI AUDIT PRO`** ([src/wifi_tools.cpp](src/wifi_tools.cpp)):
   - `1. PROBE SNIFFER`: Captura pasiva de tramas *Probe Request* (`0x40`).
   - `2. CHANNEL HEATMAP`: Mapa de ocupación de los canales `1..13` (`LIBRE` / `SAT`).
   - `3. WARDRIVING LOG (CSV)`: Registro continuo en `/wardrive.csv` (`LittleFS`).
3. **`03. ANALIZADOR`** ([src/spectrograph.cpp](src/spectrograph.cpp)): Analizador de 125 canales 2.4 GHz con modo **Barras + Picos** y modo **`WATERFALL` (Cascada 2D)** conmutable con `OK`.
4. **`04. BT SCANNER`** ([src/btscan.cpp](src/btscan.cpp)): Radar trigonométrico que escanea paquetes BLE en los canales 37/38/39 y portadoras Bluetooth en `2..80` mediante `nRF24L01+`.
5. **`05. BLE TRACKERS`** ([src/ble_trackers.cpp](src/ble_trackers.cpp)): Detector de **AirTag / FindMy / SmartTag / Tile** por demodulación de tramas BLE sobre `nRF24L01+` + **Emisor iBeacon**.
6. **`06. PACKET MONITOR`** ([src/packet_monitor.cpp](src/packet_monitor.cpp)): Histograma de tráfico WiFi por canal (`AUTO SCAN` y `MANUAL GIRO`).
7. **`07. MODO CENTINELA`** ([src/centinela.cpp](src/centinela.cpp)): Detector pasivo de tramas Deauth/Disassoc (`0xC0`/`0xA0`).
8. **`08. JAMMER CANAL`** ([src/jammer.cpp](src/jammer.cpp)): Generador de portadora en el canal WiFi seleccionado (`1..14`).
9. **`09. BARRIDO TOTAL`** ([src/total_jammer.cpp](src/total_jammer.cpp)): Barrido continuo sobre toda la banda WiFi + BT.
10. **`10. BT JAMMER`** ([src/bt_jammer.cpp](src/bt_jammer.cpp)): Salto rápido sobre los 40 canales Bluetooth (`2..80`).
11. **`11. BEACON SPAM`** ([src/beacon_spam.cpp](src/beacon_spam.cpp)): Inyección de tramas 802.11 Beacon mediante `wifi_send_pkt_freedom()`.
12. **`12. BLE SPAM (POP)`** ([src/ble_spam.cpp](src/ble_spam.cpp)): Emisión de anuncios BLE `ADV_NONCONN_IND` sintetizados sobre `nRF24L01+` (`Apple iOS`, `Windows SwiftPair`, `Android FastPair`, `Samsung Buds`, `Multi-OS`).
13. **`13. MODO HIBRIDO`** ([src/hybrid_attack.cpp](src/hybrid_attack.cpp)): Barrido combinado `nRF24L01+` + salto de canal WiFi.
14. **`14. EVIL PORTAL`** ([src/evil_portal.cpp](src/evil_portal.cpp)): Portal cautivo DNS con 5 plantillas en `PROGMEM` y guardado en `/logins.txt`.
15. **`15. IP SCANNER`** ([src/ip_scanner.cpp](src/ip_scanner.cpp)): Escáner ICMP Ping (`1..254`), `UDP FLOOD` y `PORT SCAN`.
16. **`16. CONTROL ESCLAVO`** ([src/slave.control.cpp](src/slave.control.cpp)): Control remoto ESP-NOW (`<espnow.h>`) con estado guardado en EEPROM.
17. **`17. WEB DASHBOARD`** ([include/web_server.h](include/web_server.h), [src/web_dashboard.cpp](src/web_dashboard.cpp)): Punto de acceso `BW8266-<chipID>` (clave `12345678`) con panel en `http://192.168.4.1`, `/api/status` y descargas de `/logins.txt`, `/wardrive.csv` y `/trackers.txt`.
18. **`18. REMOTE HID/UDP`** ([src/bt_remote.cpp](src/bt_remote.cpp)): Mando remoto inalámbrico por paquetes **UDP (`:4210`) + ESP-NOW** con modos Multimedia, Ratón por giro, Reels/PPT y Macro.
19. **`19. LEER LOGS`** ([src/log_viewer.cpp](src/log_viewer.cpp)): Lector en pantalla OLED de `/logins.txt` con scroll y borrado.
20. **`20. ARCADE`** ([src/games_menu.cpp](src/games_menu.cpp)): 7 juegos (`Snake`, `Pong`, `Flappy`, `Invaders`, `Dino Run`, `Breakout`, `Simon Memoria`) con récords en EEPROM.
21. **`21. AJUSTES / TEST`** ([src/settings_menu.cpp](src/settings_menu.cpp)): Brillo OLED, invertir encoder, salvapantallas, perfil, PIN `1234`, aviso legal y `SELF-TEST HW`.
22. **`22. ABOUT`** ([src/about_info.cpp](src/about_info.cpp)): Información del sistema y memoria libre con animación *Matrix*.

#   B W i f i - E S P 8 2 6 6  
 