# Marea: notas para Claude Code

**Marea** es un reloj de pulsera con esferas animadas y apps propias, sobre la **Waveshare ESP32-S3-Touch-AMOLED-2.06** (AMOLED 410×502, táctil, batería, RTC, acelerómetro, altavoz, microSD). Tiene una app Android que se queda conectada por Bluetooth mientras el reloj está cerca, una página web de control y actualizaciones por internet.

- **Repositorio público:** https://github.com/CarlosNS/c6-fx-clock (GPL-3.0-or-later). El nombre viene del primer reloj, con un ESP32-C6, que ya no se usa ni se mantiene.
- **Plan en curso:** [docs/PLAN-marea.md](docs/PLAN-marea.md). Históricos: `docs/PLAN-s3.md` (paso del C6 a la S3) y `docs/PLAN-bluetooth.md`.
- **Estilo visual del sistema:** «Marea Solsticial» (pixel art nocturno junto al mar), resumido en `docs/PLAN-marea.md`. Cada esfera tiene su propio estilo.
- **Documentación de usuario y de desarrollo:** la wiki, cuyo código fuente está en `docs/wiki/`.

## Normas del dueño (obligatorias)

- **Todo en español:** respuestas, lista de tareas, resúmenes, documentación y mensajes de commit. Los comentarios del código están en inglés; mantén ese estilo.
- **Commits:** autor `CarlosNS <19167897+CarlosNS@users.noreply.github.com>`, ya configurado en el repositorio. **Nunca** escribas el correo personal del dueño en ningún archivo del repositorio. Termina cada commit con la línea `Co-Authored-By` que indique el sistema.
- **Publicar** necesita permiso explícito **cada vez**: `tools/release.py` (GitHub Pages, manifiesto que leen los relojes y releases) y `tools/publish_wiki.py`. Hacer push a `main` también se pregunta, salvo que el dueño ya lo haya pedido en esa tarea.
- **Esferas:** las nuevas van **siempre al final** de `fx_list`, porque los relojes guardan el número de la esfera elegida. Nunca reordenes. Para quitar una, se **retira** (entrada `.retired`, ver la guía) y su número no se reutiliza. Retiradas para siempre: El grito (10), Acuario (16) y Cuento (20); el dueño no quiere dedicarles más tiempo. Usa `python tools/new_face.py` y sigue la guía de la wiki `Crear-una-esfera.md`, que el dueño quiere completa y al día.
- **Documentación:** cada cambio visible para el usuario se documenta en el README, en `docs/wiki/` y en `CHANGELOG.md`.
- **Derechos:** nada de arte, marcas ni personajes copiados. Solo obras de dominio público o inspiración libre (ver THIRD-PARTY-NOTICES.md).

## Estructura

| Ruta | Qué es |
|---|---|
| `src/` | Firmware (ESP-IDF 5.5 con PlatformIO). Detalle en la wiki, página `Arquitectura.md` |
| `src/board.h`, `src/board.c` | Pines y arranque de la placa: bus I2C, AXP2101, táctil, acelerómetro |
| `src/lcd_co5300.c` | Pantalla: QSPI a 80 MHz, búferes en PSRAM, envío en el núcleo 1. Lienzos: nativo 410×502 (sistema, Plasma), ancho 274×336 ×1,5 (las demás esferas) y el antiguo 172×320 (ya sin uso) |
| `src/pmu_axp2101.c`, `src/rtc_pcf85063.c`, `src/touch_ft3168.c`, `src/imu_qmi8658.c`, `src/i2c_bus.c` | Batería, reloj de hardware, táctil, acelerómetro y bus I2C |
| `src/fx_*.c`, `src/fx_common.c` | Esferas y su catálogo `fx_list` (`FX_COUNT` en `fx.h`) |
| `src/settings.c` | Tabla de ajustes: una línea da orden, guardado en NVS y campo en el estado JSON |
| `src/control.c` | Órdenes de texto, iguales por WiFi, Bluetooth y consola serie (`control.h` las lista) |
| `src/index.html`, `src/transport.js` | Página de control, embebida en el firmware y también dentro de la app. Secciones con `#` (Inicio, Esferas, Alarmas, Archivos, Ajustes), estilo Marea Solsticial, español/inglés con `data-en` y `tr()` |
| `src/web/` | Vistas previas (`fxN.webp`, `alarmN.webp`, con `tools/web_previews.py`) y fuentes de la página; `web.pack` se genera al compilar y el reloj las sirve en `/a/` |
| `android/` | App Kotlin «Marea» (paquete `io.github.carlosns.c6clock`, que no cambia para que se actualice sobre la instalada) |
| `tests/` | pytest por fases (`tests/README.md`), con `clocklib/` como código compartido |
| `tools/` | `new_face.py`, `ble_shot.py`, `ble_console.py`, `bootlog.py`, `release.py`, `publish_wiki.py`, `build_webflasher.py`, `gen_fonts.py`, `make_fonts.py`, `web_pack.py` (lo lanza PlatformIO), `web_previews.py` |
| `src/sys.c`, `src/ui.c`, `src/app_*.c` | Sistema (vistas, gestos, lanzador), estilo Marea Solsticial y apps |
| `src/usbdisk.c` | Modo USB: la microSD como disco (TinyUSB, `src/idf_component.yml`) |

## Compilar, flashear y probar (Windows)

```bash
PIO="$USERPROFILE/.platformio/penv/Scripts/pio.exe"
"$PIO" run                          # compilar (en limpio, unos 5 min)
"$PIO" run -t upload                # flashear por COM7
python tools/bootlog.py             # reinicia el reloj por RTS y muestra su arranque
python tools/build_webflasher.py    # imágenes marea.bin / marea-ota.bin (instalador web, tests de actualización)
cd tests && python -m pytest -m now --ignore=wifi   # unitarios
```

- `pio run` tarda varios minutos en limpio: lánzalo en segundo plano y espera el aviso. No hagas sondeos con `sleep`.
- **Abrir COM7 no reinicia el reloj.** `tools/bootlog.py` y `SerialSession` lo reinician por RTS (en Windows hay que reescribir DTR para que `usbser.sys` mande el cambio).
- **Si cambias `sdkconfig.defaults`, borra `sdkconfig.s3amoled`** para que se regenere.
- **Consola serie:** acepta cualquier orden de `control.h` (`status`, `fx 3`, `mode update`, `time <epoch> <ms>`, `dbgtime h m`). Úsala desde `tests/clocklib/board.py` con `SerialSession`.
- **Las órdenes guardan los ajustes del reloj:** restaura lo que cambies (esfera, `fxoff`, `auto`, `aon`…).
- **Capturas:** `GET /screenshot` por WiFi o `python tools/ble_shot.py` por Bluetooth (dan el lienzo de la esfera).
- **Herramientas:** JDK 21 en `%LOCALAPPDATA%\DevTools\jdk-21.0.13+9` (pon `JAVA_HOME` antes de `android\gradlew.bat`), Android SDK en `%LOCALAPPDATA%\Android\Sdk` y `gh` en `%LOCALAPPDATA%\Programs\gh\bin\gh.exe`. `build_webflasher.py` se ejecuta con el `python` del sistema, que trae esptool.
- **App:** cada versión publicada necesita un `versionCode` mayor. La clave de firma está en `%USERPROFILE%\.c6clock\`; si se pierde, habrá que desinstalar la app para actualizar. Para `adb install`, el móvil (POCO F7, HyperOS) necesita «Instalar vía USB» y que alguien toque «Instalar».

## Rarezas conocidas

- **Esquinas redondeadas:** radio de unos 120 px (`BOARD_LCD_CORNER_R`), y los 2 px exteriores de cada borde pueden no verse. Ten en cuenta siempre esa zona perdida al colocar cualquier cosa.
- **RAM interna:** es lo que se agota (unos 70 KB libres con WiFi y Bluetooth a la vez). Lo grande va a la PSRAM, incluidos los búferes de WiFi, NimBLE y TLS.
- **Doble búfer:** `fb` trae el fotograma de hace dos, así que cada esfera debe repintar todos los píxeles.
- **La caché de datos de 64 KB del S3 cuelga la placa al arrancar:** no la actives.
- **Vigilante:** si el bucle principal pasa 8 s sin avanzar, el reloj se reinicia y al arrancar dice en qué paso estaba (`hang.h`).
- **Pantalla sin esp_lcd:** `lcd_co5300.c` habla con el panel directamente por `spi_master`. Cada fotograma es una sola escritura de píxeles con el bus tomado y el CS activo de principio a fin. El `esp_lcd_panel_io` de antes, que mezclaba órdenes por sondeo con transferencias encoladas, a veces se quedaba esperando para siempre: ese era el bloqueo del reloj.
- **La PSRAM es lo lento:** escribir un fotograma completo de 410×502 cuesta unos 13 ms mientras la tarea de envío lo lee. Una esfera animada a pantalla completa ronda los 25–30 fps; las de fondo quieto van sobradas.
- **El táctil a veces no responde a una lectura:** el driver conserva el último estado; los errores de `i2c.master` están silenciados. Lo normal son unos 6 fallos por minuto (`touch: N failed reads`); muchos más indican un problema.
- **No leas el AXP2101 justo antes del táctil:** una lectura del AXP2101 (el botón PWR, `poll_pwrkey`) colocada en el bucle justo antes de `input_poll` hacía fallar casi todas las lecturas del FT3168. Va después del acelerómetro.
- **Panel o táctil colgados:** tras un fallo grave, el panel puede quedarse negro aunque el reloj dibuje (la captura sale bien). Reiniciar el ESP32 no basta: hace falta cortar la corriente (PWR 10 s). Pasó el 10/11/2026 tras el desbordamiento de pila de Archivos.
- **Manifiesto de actualizaciones:** la entrada de Marea es `boards.s3amoled`. Las claves de arriba son de los relojes C6 (2.1.0) y **nunca** se tocan. Los C6 solo leen 1023 bytes: `release.py` lo comprueba.
- **`project(c6-fx-demo)` no se renombra:** `ota.c` exige que la imagen nueva tenga el mismo nombre de proyecto que la instalada.
- **HyperOS:** bloquea escaneos Bluetooth en segundo plano y mata apps; la app tiene que guiar para quitar las restricciones de batería.
- **Red WiFi «Merlos»:** es una red mallada. Un nodo puede asociar el reloj sin darle IP; `net.c` lo vigila.
- **No renombres el repositorio:** los relojes y la app leen `https://carlosns.github.io/c6-fx-clock/manifest.json`.

## Estado (noviembre de 2026)

- **En el reloj del dueño:** `3.0.0-beta.1`, instalada por la actualización real desde GitHub (solo `boards.s3amoled` en Pages).
- **En `main` (sin publicar):** el sistema Marea con apps, todas las esferas a pantalla completa, modo USB y la app 0.4.0 con conexión permanente, música y llamadas. Falta probar en el móvil y con el dedo; ver «Pendiente» al final de [docs/PLAN-marea.md](docs/PLAN-marea.md).
- **Copia del firmware de fábrica:** `C:\Users\Carlos\Esp32\backups\s3-amoled-fabrica\`.
