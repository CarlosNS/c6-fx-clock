## Placa: Waveshare ESP32-S3-Touch-AMOLED-2.06

| | |
|---|---|
| Chip | ESP32-S3R8: doble núcleo a 240 MHz con coma flotante, 512 KB de SRAM y **8 MB de PSRAM** octal |
| Flash | 32 MB |
| Pantalla | AMOLED de 2,06", 410×502, controlador CO5300 por QSPI a 80 MHz, con **esquinas redondeadas** (radio de unos 120 px) |
| Táctil | FT3168 por I2C |
| Energía | Batería con gestor AXP2101 (carga, nivel, botón de encendido) |
| Reloj | RTC PCF85063: guarda la hora con el reloj apagado |
| Movimiento | Acelerómetro y giroscopio QMI8658 (levantar para encender, podómetro, agitar para posponer) |
| Sonido | Códec ES8311 con altavoz y amplificador; micrófonos con ES7210 (sin uso todavía) |
| Otros | Ranura microSD (SDMMC de 1 bit), botones BOOT y PWR, USB-C |
| Radio | WiFi a 2,4 GHz y Bluetooth 5 LE, los dos a la vez |

### Pines (`src/board.h`)

| Señal | GPIO |
|---|---|
| Pantalla QSPI: CS / SCLK / D0–D3 / RST | 12 / 11 / 4–7 / 8 |
| Bus I2C (PMU, RTC, táctil, IMU, códec): SDA / SCL | 15 / 14 |
| Táctil: INT / RST | 38 / 9 |
| I2S: MCLK / SCLK / LRCK / salida / entrada | 16 / 41 / 45 / 40 / 42 |
| Amplificador del altavoz | 46 |
| microSD: CLK / CMD / D0 | 2 / 1 / 3 |
| Botón BOOT (activo a nivel bajo; también pin de arranque) | 0 |

El panel de 410 columnas empieza en la columna 22 de la memoria del controlador (`BOARD_LCD_X_GAP`).

### La pantalla

- **Sin `esp_lcd`:** `src/lcd_co5300.c` habla con el panel por `spi_master`. Cada fotograma es una sola escritura de píxeles, con el bus tomado y el CS activo de principio a fin, en trozos de 20 líneas encolados. Una tarea en el núcleo 1 envía mientras el bucle principal dibuja el siguiente.
- **Tres lienzos**, cada uno con dos búferes en PSRAM: nativo (410×502), ancho (274×336, ampliado ×1,5 al enviar) y el antiguo de 172×320. Ver [[Arquitectura]].
- **Esquinas:** radio de unos 120 px (`BOARD_LCD_CORNER_R`), y los 2 px de cada borde pueden no verse (`BOARD_LCD_EDGE`).
- **Lo lento es la PSRAM:** escribir un fotograma nativo cuesta unos 13 ms mientras la tarea de envío lo lee.

### Memoria

Lo que se agota es la **RAM interna**: quedan unos 70 KB con WiFi y Bluetooth en marcha. Los fotogramas, los búferes de WiFi, NimBLE y TLS, y los búferes grandes de las esferas van a la PSRAM. La caché de datos de 64 KB del S3 cuelga la placa al arrancar: no se activa.

### Particiones (32 MB)

| Nombre | Tipo | Tamaño | Uso |
|---|---|---|---|
| `nvs` | datos | 64 KB | Ajustes, red WiFi, modo, emparejamientos, alarmas |
| `otadata` | datos | 8 KB | Qué hueco de programa arranca |
| `phy_init` | datos | 4 KB | Calibración de radio |
| `ota_0` / `ota_1` | programa | 7 MB cada uno | Dos huecos para actualizar con vuelta atrás; terminan por debajo de los 16 MB que la CPU puede mapear |
| `storage` | datos | 17,9 MB | Libre para más adelante (fondos, sonidos, registros) |

### Copia de fábrica

El firmware con el que viene la placa se puede guardar antes de instalar Marea con `esptool read_flash` (32 MB). Waveshare publica también sus demos en la [wiki de la placa](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.06/Resources-And-Documents).

## El reloj anterior: ESP32-C6-LCD-1.47

El proyecto empezó en una **Waveshare ESP32-C6-LCD-1.47** (pantalla IPS de 172×320, sin PSRAM, sin batería). Esa placa ya no se mantiene: su última versión es la 2.1.0, que los relojes C6 siguen pudiendo instalar desde el manifiesto de actualizaciones.
