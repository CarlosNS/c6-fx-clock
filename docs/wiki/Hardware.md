## Placa actual: Waveshare ESP32-C6-LCD-1.47

| | |
|---|---|
| Chip | ESP32-C6 (RISC-V a 160 MHz, sin FPU), 512 KB de SRAM |
| Flash | **8 MB** (hay variantes de 4 MB: el firmware necesita 8) |
| Pantalla | 1,47" IPS, 172×320, controlador ST7789 por SPI |
| Radio | WiFi 6 a 2,4 GHz y Bluetooth 5 LE |
| Otros | LED RGB WS2812, botones BOOT y RESET, ranura microSD (sin uso), USB-C |

### Pines

| Señal | GPIO |
|---|---|
| LCD MOSI / SCLK | 6 / 7 |
| LCD CS / DC / RST | 14 / 15 / 21 |
| Retroiluminación | 22 |
| CS de la microSD (comparte el bus; se mantiene sin seleccionar) | 4 |
| Botón BOOT (activo a nivel bajo; también pin de arranque) | 9 |
| LED WS2812 | 8 |

El panel de 172 px está centrado en la RAM de 240 px del controlador (desplazamiento de 34 px). La rotación entre vertical y horizontal se hace por hardware (MADCTL) con el mismo framebuffer.

### Memoria: por qué hay modos

Dos framebuffers de 110 KB (doble búfer con DMA para 30 fps) dejan sitio para **una sola pila de radio**. Por eso WiFi y Bluetooth van en modos de arranque separados (`BOARD_RADIO_EXCLUSIVE`). Para ganar RAM, el código de WiFi, lwIP, FreeRTOS y el controlador Bluetooth se ejecuta desde la flash.

### Particiones (8 MB)

| Nombre | Tipo | Tamaño | Uso |
|---|---|---|---|
| `nvs` | datos | 24 KB | Ajustes, red WiFi, modo, emparejamientos |
| `otadata` | datos | 8 KB | Qué hueco de programa arranca |
| `phy_init` | datos | 4 KB | Calibración de radio |
| `ota_0` / `ota_1` | programa | 3,5 MB cada uno | Dos huecos para actualizar con vuelta atrás |
| `storage` | datos | 896 KB | Libre para el futuro |

## Placa definitiva: Waveshare ESP32-S3-Touch-AMOLED-2.06

El C6 es provisional. El destino es un reloj de pulsera de verdad:

| | |
|---|---|
| Chip | ESP32-S3R8 (doble núcleo a 240 MHz) con **8 MB de PSRAM** |
| Flash | 32 MB |
| Pantalla | AMOLED de 2,06", 410×502, controlador CO5300 por QSPI, **táctil** FT3168 |
| Energía | Batería con gestor AXP2101 |
| Otros | RTC PCF85063 (no perderá la hora), IMU QMI8658 |

Con PSRAM caben WiFi y Bluetooth a la vez, así que los modos de arranque desaparecerán. Todo lo que depende de la placa está aislado en `src/board.h`, `src/lcd.c` y la lectura del botón, para que el cambio sea acotado.
