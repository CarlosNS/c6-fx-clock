# Plan: paso a la Waveshare ESP32-S3-Touch-AMOLED-2.06

La placa llegó el 7 de noviembre de 2026. Este plan es el punto de partida de una conversación nueva: resume qué tiene la placa, qué se reutiliza, qué cambia y en qué orden hacerlo. Se van marcando las fases conforme se terminan.

## La placa (comprobado en el arranque del firmware de fábrica, por COM7)

| | |
|---|---|
| Chip | ESP32-S3 rev. 0.2, doble núcleo a 240 MHz, **con FPU** de precisión simple e instrucciones SIMD |
| Memoria | **8 MB de PSRAM octal** a 80 MHz y **32 MB de flash** GigaDevice. La cabecera del firmware de fábrica declara 16 MB |
| Pantalla | AMOLED de 2,06", **410×502**, controlador CO5300 por QSPI (en ESP-IDF, driver compatible `sh8601`). Sin pin de retroiluminación: el brillo es el registro 0x51 (0–255). **Esquinas redondeadas** con un radio de unos 120 px (medido en la fase 2): todo lo que se dibuje cerca de las esquinas se pierde |
| Táctil | FT3168 por I2C |
| Energía | PMIC AXP2101 (carga, batería de 3,7 V por conector MX1.25, medidas). No hay expansor de pines en el bus I2C: el botón PWR va, con toda probabilidad, a la entrada PWRON del AXP2101 (por confirmar en el esquema, fase 7) |
| Reloj de hardware | PCF85063A, alimentado por la batería: **conserva la hora apagado** |
| Movimiento | QMI8658, acelerómetro y giroscopio |
| Audio | ES8311 (altavoz, amplificador en GPIO46) y ES7210 (3 micrófonos, TDM) |
| Otros | Ranura microSD, botón BOOT en GPIO0, USB nativo (aparece como COM7, VID 303A) |
| Firmware de fábrica | Demo «Brookesia» con chat de IA (ESP-IDF 5.5.4) |

### Pines

Sacados del BSP de Waveshare (`examples/factory/components/waveshare__esp32_s3_touch_amoled_2_06`) y de `pin_config.h`, en https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06.

| Función | GPIO |
|---|---|
| LCD QSPI: CS, CLK, D0–D3, RST | 12, 11, 4/5/6/7, 8 |
| I2C compartido: SDA, SCL (táctil, AXP2101, RTC, IMU, códecs) | 15, 14 |
| Táctil: INT, RST | 38, 9 |
| I2S: MCLK, SCLK, LRCK, DOUT (al ES8311), DIN (del ES7210) | 16, 41, 45, 40, 42 |
| Amplificador del altavoz | 46 |
| microSD (SDMMC de 1 bit): CLK, CMD, D0 | 2, 1, 3 (en modo SPI, CS es el 17) |
| BOOT | 0 |

La tabla de particiones de fábrica solo usa los primeros 16 MB (la app `factory` de 8 MB empieza en 0x200000), por eso su cabecera declara 16 MB. esptool detecta 32 MB de flash quad.

El esquema está en https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06/ESP32-S3-Touch-AMOLED-2.06.pdf.

### Bus I2C (escaneado en la fase 1)

| Dirección | Chip |
|---|---|
| 0x18 | ES8311 (audio de salida) |
| 0x34 | AXP2101 (energía) |
| 0x38 | FT3168 (táctil; solo responde tras sacarlo de reset con GPIO9) |
| 0x40 | ES7210 (micrófonos) |
| 0x51 | PCF85063 (RTC) |
| 0x6B | QMI8658 (IMU) |

### Tensiones del AXP2101 al arrancar

Vienen todas encendidas de fábrica, así que la pantalla y los chips funcionan sin configurar el AXP2101 (el ejemplo de Waveshare tampoco lo toca): DC1 3,3 V, ALDO1 3,3 V, ALDO2 3,3 V, ALDO3 3,0 V, ALDO4 1,8 V, BLDO1 1,2 V, BLDO2 2,8 V, y DC2–DC4, CPUSLDO, DLDO1 y DLDO2 encendidas. Antes de apagar alguna para ahorrar (fase 7) hay que ver en el esquema qué alimenta cada una.

## Qué se reutiliza tal cual

- **Capa de control:** `control.c`, `settings.c`, la consola serie, el protocolo Bluetooth (`ble.c`), el modo WiFi (`net.c`) y las actualizaciones (`ota.c` y `update.c`). Todo es ESP-IDF y no depende de la placa.
- **Interfaz:** la página de control y la app Android. El catálogo de esferas se lo pide la app al reloj, así que no hay que tocarla para esferas nuevas.
- **Pruebas y herramientas:** los tests (`tests/clocklib`) y las herramientas, una vez parametrizados el puerto, la placa y el tamaño de pantalla.

## Qué cambia

1. **Pantalla:**
   - Driver nuevo en `lcd.c`, o en un `lcd_co5300.c` aparte: QSPI, 410×502 y brillo por registro.
   - Un fotograma son 411 KB, así que los dos búferes van en la PSRAM.
   - El ancho de banda del QSPI (unos 40 MB/s a 80 MHz) da para unos 10 ms por fotograma: los 30 fps caben.
2. **Esferas: el cambio grande.**
   - Hoy dibujan en 172×320 o 320×172 con coordenadas fijas. La pantalla nueva tiene otra proporción: 0,82 frente a 0,54.
   - Hay 3,7 veces más píxeles, pero el S3 tiene dos núcleos y FPU.
   - Propuesta en tres pasos, decisión de la fase 3:
     1. **Compatibilidad:** las esferas actuales dibujan en un lienzo de 172×320 que se escala, por ejemplo ×1,5 → 258×480, centrado con bandas negras. Las horizontales se dibujan giradas. Así funcionan todas desde el primer día. Hay que comprobar que las esquinas del lienzo escalado no caen en las esquinas redondeadas de la pantalla.
     2. **Esferas a tamaño completo:** `fx_t` gana un campo de resolución, y cada esfera usa `W` y `H` en vez de números fijos. Se adaptan una a una, empezando por las más sencillas (Clásico, Tiempo, Plasma).
     3. **Reparto entre los dos núcleos:** cada núcleo pinta media pantalla, si alguna esfera no llega a 30 fps.
3. **Placa y modos:**
   - `board.h` pasa a ser un archivo por placa, con un entorno de PlatformIO para cada una: `c6lcd` y `s3amoled`.
   - Con PSRAM caben WiFi y Bluetooth a la vez (`BOARD_RADIO_EXCLUSIVE 0`): los modos de arranque podrían desaparecer en esta placa.
4. **Hora:**
   - El PCF85063 conserva la hora sin corriente: se lee al arrancar y se corrige cada vez que llega la hora del móvil o de NTP.
   - Así **sobra el arranque callado de 60 s** de `ble.c` en esta placa.
5. **Entradas:**
   - El táctil sustituye al panel de la app en las esferas interactivas. `app_take_drag` recibe también los gestos de la pantalla.
   - Deslizar el dedo cambia de esfera.
   - El botón BOOT sigue como está. El botón PWR va, con toda probabilidad, al AXP2101.
6. **Energía (es un reloj con batería):**
   - Brillo bajo y pantalla apagada a los pocos segundos.
   - Encender al levantar la muñeca (IMU) o al tocar.
   - Sueño ligero con la pantalla apagada.
   - Nivel de batería en el estado JSON y en las esferas.
   - **AMOLED:** el negro no gasta y hay riesgo de quemado. Fondos oscuros, desplazar el contenido unos píxeles de vez en cuando y quizá un modo «siempre visible» mínimo.
7. **Actualizaciones:**
   - El manifiesto gana una entrada por placa. Los relojes C6 ya instalados siguen leyendo las claves de hoy, que no se tocan.
   - Imágenes, instalador web y `release.py`, uno por placa.
   - Tabla de particiones nueva para 32 MB, con huecos OTA más grandes.
   - ESP-IDF ya rechaza una imagen de otro chip, pero aun así cada placa comprueba que la imagen es la suya.
8. **Extras posibles más adelante:**
   - alarmas y avisos con el altavoz;
   - el acelerómetro en esferas como Arena (la arena cae hacia abajo de verdad);
   - la microSD para fondos o música.

## Fases

| # | Fase | Hecho cuando |
|---|---|---|
| 0 ✅ | **Copia del firmware de fábrica:** `esptool read_flash 0 0x2000000`. Guárdala fuera del repositorio: es de Waveshare | Hecha el 7 de noviembre de 2026: 32 MB en `C:\Users\Carlos\Esp32\backups\s3-amoled-fabrica\`, comprobada con `verify_flash` contra la placa (sin volver a escribirla). El `LEEME.md` de esa carpeta tiene el SHA-256, la tabla de particiones de fábrica y cómo restaurarla |
| 1 ✅ | **Arranque mínimo:** entorno `s3amoled`, PSRAM, escaneo I2C (direcciones de cada chip), AXP2101 encendiendo la pantalla, CO5300 mostrando un degradado | Una imagen fija sin parpadeos; el log lista los chips del bus I2C |
| 2 ✅ | **Bucle de render a 30 fps** con doble búfer en la PSRAM, brillo y rotación | Una esfera de prueba a 30 fps, medida con el contador en pantalla y el log (la orden `ovl 1` llega con `main.c` en la fase 3) |

| 3 ✅ | **Esferas en modo compatibilidad** (lienzo escalado), y decidir el diseño definitivo de resolución | Las 21 esferas funcionan en la pantalla nueva (comprobado por el dueño) |
| 4 ⏳ | **Hora del RTC** y fuera el arranque callado en esta placa | La hora sobrevive a un apagado sin móvil. Hecho salvo esta prueba, pendiente del dueño (ver las notas) |
| 5 | **Táctil:** gestos para cambiar de esfera, encender y esferas interactivas | Deslizar cambia de esfera; las interactivas responden al dedo |
| 6 | **Bluetooth y WiFi a la vez** (si cabe), y la app funcionando con la placa nueva | `pytest -m phase2`, `phase5` y la app pasan contra la S3 |
| 7 | **Energía:** batería, sueño, encender al levantar la muñeca | Horas de batería medidas con un uso normal |
| 8 | **Actualizaciones por placa:** manifiesto, `release.py` e instalador web | Un C6 y una S3 se actualizan cada uno con su imagen |
| 9 | **Esferas a resolución completa**, una a una | Tantas como se quiera; la guía «Crear una esfera» explica las dos placas |

## Notas de las fases

### Fase 1 (hecha el 7 de noviembre de 2026)

- Imagen de prueba comprobada por el dueño: colores y orientación correctos con el USB a la derecha, sin parpadeo, y el botón BOOT cambia el brillo.
- **Esquinas redondeadas:** se comen casi todo un cuadrado de 44 px en cada esquina (medidas en la fase 2).
- `main_s3.c` es un firmware de arranque aparte; `src/CMakeLists.txt` elige los archivos según el chip. `lcd_co5300.c` habla QSPI directamente con `esp_lcd_panel_io`, sin el componente `esp_lcd_sh8601`.
- Enviar un fotograma completo tarda **40 ms** a 40 MHz, copiando trozos de 38 líneas de la PSRAM a un búfer interno y esperando cada uno. No llega a 30 fps. (Resuelto en la fase 2.)
- El driver SPI de ESP-IDF 5.5 no hace DMA directo desde la PSRAM: si se le pasa un búfer de PSRAM, reserva y copia uno interno en cada envío.
- En Windows, abrir COM7 no reinicia la S3. `python tools/bootlog.py` la reinicia por RTS y guarda el log.

### Fase 2 (hecha el 7 de noviembre de 2026)

- **`lcd.h` es común:** toma el tamaño de `board.h`, y `lcd_co5300.c` implementa en la S3 la misma API que `lcd.c` en el C6 (`lcd_init`, `lcd_fb`, `lcd_flush`, brillo, horizontal y último fotograma). `gfx.c`, `fonts.c` y `uifont.c` ya compilan para la S3 sin cambios.
- **Envío:**
  - QSPI a 80 MHz, con dos fotogramas en la PSRAM. Una tarea en el núcleo 1 envía cada fotograma mientras el núcleo 0 dibuja el siguiente.
  - El envío copia trozos de 38 líneas a dos búferes internos alternos: copia uno mientras el otro sale. Se pone la ventana de cada trozo, porque `tx_param` espera a que termine el trozo anterior. No hace falta el comando de continuación 0x3C.
  - Un fotograma tarda **12,4 ms** (12,6 ms mientras se dibuja a la vez).
- **Horizontal:** la esfera dibuja en 502×410 y el envío la gira al copiar. Cuesta +0,5 ms. El borde superior de la imagen horizontal queda en el lado del USB; si hace falta, se cambia en `copy_strip`.
- **Esfera de prueba** (plasma por tablas a pantalla completa, en `main_s3.c`): 30 fps en vertical y en horizontal. Dibujarla tarda **20 ms** de los 33 disponibles: escribir 411 KB por fotograma en la PSRAM cuesta. Las esferas reales a tamaño completo pueden necesitar el reparto entre los dos núcleos.
- **Caché de datos de 64 KB** (`CONFIG_ESP32S3_DATA_CACHE_64KB`, con líneas de 32 o 64 B): la placa se cuelga justo al pasar del bootloader a la app. Descartada, y anotado en `sdkconfig.defaults.esp32s3`.
- **Área visible**, medida con arcos de radio conocido sobre las fotos del dueño:
  - **Esquinas:** radio de 115–120 px. Los arcos de 120 px o más se ven enteros y los de 100 y 110 px no. `BOARD_LCD_CORNER_R 120` en `board_s3amoled.h`.
  - **Bordes rectos:** se ven las líneas a 2, 4 y 6 px; la de 0 px no se distingue con claridad (`BOARD_LCD_EDGE 2`).
  - **Para la fase 3:** el lienzo ×1,5 (258×480, centrado en 76, 11) tiene las esquinas a 117,5 px del centro del redondeo. Queda visible, pero sin margen: lo que haya en las esquinas del lienzo se verá recortado.
- **Pruebas sin tocar la placa:** por la consola serie, `0`, `1` y `2` eligen pantalla y `b` cambia el brillo. El botón BOOT hace lo mismo: pulsación corta para la pantalla, larga para el brillo.

### Fase 3 (hecha el 7 de noviembre de 2026)

- **El reloj entero corre en la S3:** `main.c`, las esferas, el WiFi, el Bluetooth, la consola, los ajustes y las actualizaciones. `main_s3.c` desaparece (queda en el historial). Las dos placas comparten la lista de archivos de `src/CMakeLists.txt`; solo cambian la pantalla (`lcd.c` / `lcd_co5300.c`) y `board_*.c`.
- **`board_init()`** (en `board_c6lcd.c` y `board_s3amoled.c`) se llama lo primero en `app_main`. En la S3 saca el táctil de reset, escanea el bus I2C y lee el AXP2101.
- **Lienzo:** en la S3, `LCD_W`×`LCD_H` sigue siendo 172×320, así que las esferas no cambian. `BOARD_PANEL_W`×`BOARD_PANEL_H` es el panel. `lcd_co5300.c` amplía el lienzo al enviarlo:
  - **vertical ×1,5** → 258×480, en (76, 11);
  - **horizontal ×1,25** → 400×215, en (5, 143), **sin girar**. El reloj se lleva en vertical y todas las esferas acabarán siendo verticales (decisión del dueño), así que mientras tanto las horizontales se ven derechas en una franja central, lejos de las esquinas.
  - En cada grupo de píxeles (3 por cada 2 a ×1,5, 5 por cada 4 a ×1,25), uno es la media de sus vecinos en vez de repetir uno: las líneas quedan de grosor uniforme.
  - Las capturas (`/screenshot`, `ble_shot.py`) siguen dando el lienzo de 172×320.
- **Brillo:** solo lo envía la tarea de la pantalla, después del fotograma en curso. Así los fundidos no frenan el bucle principal.
- **Sin LED RGB:** `led.c` no hace nada si la placa no define `BOARD_PIN_LED`.
- **Medidas** (lienzo en la PSRAM, núcleo 0 dibujando, núcleo 1 enviando): **todas a 30 fps**. Dibujo medio por esfera, en ms:

| Esfera | ms | Esfera | ms | Esfera | ms |
|---|---|---|---|---|---|
| Plasma | 11,2 | RPG | 12,5 | Engranajes | 11,3 |
| Cubo 3D | 5,3 | Estrellada | 15,3 | Lluvia | 8,3 |
| Túnel | 10,1 | WiFi | 9,4 | Agujero negro | 10,6 |
| Lava | 10,6 | Earthrise | 10,5 | Gol | 11,8 |
| Vida | 5,9 | Sakura | 9,9 | Tiempo | 6,6 |
| HUD | 9,0 | Synthwave | 10,6 | Clásico | 19,8 |
| Aventura | 13,3 | Arena | 9,9 | Feria de Sevilla | 7,6 |

- **Pendiente para la fase 9** (esferas a tamaño completo y verticales): con 3,7 veces más píxeles, las esferas más pesadas (Clásico, Estrellada) no cabrían en 33 ms dibujando en la PSRAM con un solo núcleo. Habrá que repartir entre los dos núcleos (el envío ya ocupa parte del núcleo 1), dibujar en RAM interna por franjas, o las dos cosas.
- **Esferas retiradas** (decisión del dueño): El grito (10), Acuario (16) y Cuento (20) se quitan para siempre. Su código se borra, pero sus números siguen ocupados en `fx_list` como entradas `.retired`: el reloj las salta y el catálogo las envía con grupo `-1`. Quedan 21 esferas.
- **Nombres de la C6 que quedan en la S3:** la red WiFi propia sigue llamándose `C6-FX-Demo` y el Bluetooth anuncia el mismo nombre que el C6. Se revisan en la fase 6.

### Fase 4 (7 de noviembre de 2026; falta la prueba de apagado)

- **`rtc_pcf85063.c`** (solo la S3, `BOARD_HAS_RTC 1`): el PCF85063A guarda la hora en UTC; la zona horaria la sigue poniendo `clock.c`.
  - `clock_init()` lee el RTC esperando al cambio de segundo (hasta 1 s de espera al arrancar), y así la hora sale exacta a unos pocos ms. Si el RTC dice que su oscilador se paró (bit OS) o tiene una fecha imposible, no se usa y el reloj arranca sin hora, como el C6.
  - `clock_set_epoch()` (móvil, página, consola) y la sincronización NTP guardan la hora en el RTC. Lo hace un temporizador en el siguiente cambio de segundo, así que quien manda la hora no espera.
  - **Puesta en hora exacta:** se para el reloj (bit STOP), se escribe la hora justo en el cambio de segundo y se suelta STOP 492 ms después. Según la hoja de datos, el primer salto llega 0,508 s después de soltarlo, así que cae en el segundo exacto.
  - **Medido:** tras un reinicio, la hora coincide con la del PC con 24–66 ms de diferencia, que es lo que tarda en medirse por la consola.
- **El RTC llegó parado de fábrica:** con el bit OS activo, sin hora guardada.
- **Arranque callado:** `ble.c` ya se lo saltaba si había hora al arrancar. Como ahora `clock_init()` la tiene antes de `ble_start()`, en la S3 desaparece solo, sin tocar `ble.c`. Si el RTC perdiera la hora, el arranque callado vuelve a hacer falta y sigue funcionando.
- **`i2c_bus.c`:** escritura de varios registros seguidos y un mutex al registrar dispositivos, porque el RTC se escribe desde el temporizador.
- **Pendiente, prueba del dueño:** desenchufar el USB, apagar con una pulsación larga de PWR, esperar y volver a encender, y ver que la hora está bien desde el principio. La S3 no conoce la WiFi ni tiene móvil emparejado, así que solo puede sacarla del RTC. Queda por confirmar que PWR apaga la placa con la configuración de fábrica del AXP2101, y que el RTC sigue alimentado con la placa apagada.
- **Comprobación también pendiente:** que el Bluetooth anuncie al instante con un móvil emparejado y la hora del RTC. Sin emparejar, el arranque callado no llega a activarse.

## Cómo trabajar

- **Rama `s3`:** un solo código para las dos placas, con dos entornos de PlatformIO. La rama se une con `main` cuando el C6 siga compilando y pasando sus tests.
- **No romper el C6:** antes de cada unión con `main`, compila el C6 con `pio run -e c6lcd` y pasa `pytest -m now`.
- **Puertos:** el C6 está en COM5 y la S3 en COM7. Abrir COM7 no reinicia la S3; para ver su arranque, usa `python tools/bootlog.py`.
- **3.0.0 del C6:** aplazada (7 de noviembre de 2026); ver `CLAUDE.md`, sección Estado.
