# C6 FX Clock: notas para Claude Code

Reloj de escritorio con esferas animadas a 30 fps, una app Android por Bluetooth, una página web de control y actualizaciones por internet.

- **Placa actual:** Waveshare ESP32-C6-LCD-1.47.
- **Placa nueva:** Waveshare ESP32-S3-Touch-AMOLED-2.06 (ver [docs/PLAN-s3.md](docs/PLAN-s3.md)).
- **Repositorio público:** https://github.com/CarlosNS/c6-fx-clock (GPL-3.0-or-later).
- **Documentación de usuario y de desarrollo:** la wiki, cuyo código fuente está en `docs/wiki/`.

## Normas del dueño (obligatorias)

- **Todo en español:** respuestas, lista de tareas, resúmenes, documentación y mensajes de commit. Los comentarios del código están en inglés; mantén ese estilo.
- **Commits:** autor `CarlosNS <19167897+CarlosNS@users.noreply.github.com>`, ya configurado en el repositorio. **Nunca** escribas el correo personal del dueño en ningún archivo del repositorio. Termina cada commit con la línea `Co-Authored-By` que indique el sistema.
- **Publicar** necesita permiso explícito **cada vez**: `tools/release.py` (release, GitHub Pages y manifiesto que leen los relojes) y `tools/publish_wiki.py`. Hacer push a `main` también se pregunta, salvo que el dueño ya lo haya pedido en esa tarea.
- **Esferas:** las nuevas van **siempre al final** de `fx_list`, porque los relojes guardan el número de la esfera elegida. Nunca reordenes. Usa `python tools/new_face.py` y sigue la guía de la wiki `Crear-una-esfera.md`, que el dueño quiere completa y al día.
- **Documentación:** cada cambio visible para el usuario se documenta en el README, en `docs/wiki/` y en `CHANGELOG.md`.
- **Derechos:** nada de arte, marcas ni personajes copiados. Solo obras de dominio público o inspiración libre (ver THIRD-PARTY-NOTICES.md).

## Estructura

| Ruta | Qué es |
|---|---|
| `src/` | Firmware (ESP-IDF 5.5 con PlatformIO). Detalle en la wiki, página `Arquitectura.md` |
| `src/board.h`, `src/lcd.c` | Todo lo que depende de la placa: pines, pantalla, botón, LED |
| `src/fx_*.c`, `src/fx_common.c` | Esferas y su catálogo `fx_list` (`FX_COUNT` en `fx.h`) |
| `src/settings.c` | Tabla de ajustes: una línea da orden, guardado en NVS y campo en el estado JSON |
| `src/control.c` | Órdenes de texto, iguales por WiFi, Bluetooth y consola serie (`control.h` las lista) |
| `src/index.html`, `src/transport.js` | Página de control, embebida en el firmware y también dentro de la app |
| `android/` | App Kotlin: una WebView con la misma página, Companion Device Manager y WorkManager |
| `tests/` | pytest por fases (`tests/README.md`), con `clocklib/` como código compartido |
| `tools/` | `new_face.py`, `ble_shot.py` (captura por Bluetooth), `ble_console.py`, `release.py`, `publish_wiki.py`, `build_webflasher.py` |
| `docs/PLAN-bluetooth.md` | Plan histórico de las fases 0–7, ya cerrado |
| `docs/PLAN-s3.md` | Plan del paso a la placa S3 AMOLED |

## Compilar, flashear y probar (Windows)

```bash
PIO="$USERPROFILE/.platformio/penv/Scripts/pio.exe"
"$PIO" run -e c6lcd                 # compilar (la primera vez tarda unos 2 min)
"$PIO" run -e c6lcd -t upload       # flashear por COM5
python tools/build_webflasher.py    # después de cada cambio: imágenes para el instalador web y los tests de actualización
cd tests && python -m pytest -m now # unitarios (16) y web
```

- `pio run` tarda más de 2 minutos en limpio: lánzalo en segundo plano y espera el aviso. No hagas sondeos con `sleep`.
- **Abrir COM5 reinicia el C6**, aunque sea solo para leer. Comprueba lo que haga falta antes de abrirlo.
- **Consola serie:** acepta cualquier orden de `control.h` (`status`, `fx 3`, `mode ble|wifi|update`, `time <epoch> <ms>`, `dbgtime h m`). Úsala desde `tests/clocklib/board.py` con `SerialSession`.
  - Sin hilo lector propio, llama a `status(reading=False)`.
  - Tras abrir el puerto, espera a la línea `Face:` del arranque antes de mandar órdenes.
- **Las órdenes guardan los ajustes del reloj:** restaura lo que cambies (esfera, `fxoff`, `auto`…). `tests/conftest.py` lo hace en las pruebas por WiFi.
- **Arranque callado:** un reloj emparejado que arranca sin hora no anuncia su Bluetooth durante 60 s (`QUIET_BOOT_MS` en `ble.c`), para que el móvil lo note y le dé la hora. Los tests se lo saltan mandando `time` por la consola (`SerialSession.wait_advertising`).
- **Capturas:** `python tools/ble_shot.py --fx N --out x.png --scale 2` en modo Bluetooth, o `GET /screenshot` en modo WiFi.
- **Herramientas:** JDK 21 en `%LOCALAPPDATA%\DevTools\jdk-21.0.13+9` (pon `JAVA_HOME` antes de `android\gradlew.bat`), Android SDK en `%LOCALAPPDATA%\Android\Sdk` y `gh` en `%LOCALAPPDATA%\Programs\gh\bin\gh.exe`.
- **App:** cada versión publicada necesita un `versionCode` mayor.
  - La clave de firma está en `%USERPROFILE%\.c6clock\`. Si se pierde, los usuarios tendrán que desinstalar la app para actualizar.
  - Para instalar con `adb install`, el móvil (POCO F7, HyperOS) tiene que tener activado «Instalar vía USB» y alguien debe tocar «Instalar» en él.

## Rarezas conocidas

- **Memoria del C6:** no caben WiFi y Bluetooth a la vez, por eso existen los modos de arranque BLE, WIFI y UPDATE.
- **HyperOS:**
  - bloquea los escaneos Bluetooth en segundo plano;
  - avisa de que el reloj desaparece y reaparece cada unos 20 s, así que la presencia solo sincroniza tras 30 s de ausencia;
  - `am force-stop` corta los avisos de presencia hasta que se abre la app.
- **Red WiFi «Merlos»:** es una red mallada. Un nodo puede asociar el reloj sin darle IP; `net.c` lo vigila.
- **El C6 no tiene coma flotante (FPU):** las esferas usan enteros y tablas (`sin_q14`).
- **Doble búfer:** `fb` trae el fotograma de hace dos, así que cada esfera debe repintar todos los píxeles.
- **No renombres el repositorio:** los relojes instalados y la app leen `https://carlosns.github.io/c6-fx-clock/manifest.json`, y un cambio de nombre rompería esa dirección de GitHub Pages.

## Estado (noviembre de 2026)

- **Versión publicada:** 2.1.0.
- **3.0.0:** preparada en `CHANGELOG.md` y sin publicar. **Aplazada** por decisión del dueño (7 de noviembre de 2026) mientras se trabaja en la S3; faltan las pruebas con el móvil de `tests/MANUAL.md`. Antes de publicarla:
  - `PROJECT_VER` 3.0.0;
  - en la app, `versionName` 1.0.0 y un `versionCode` mayor;
  - `BOOT_DEFAULT` → `BOOT_BLE`;
  - y permiso del dueño.
- **App:** la 0.3.10 (código 13) está compilada pero no instalada en el móvil del dueño.
- **Placa nueva:** ha llegado la S3 AMOLED. El trabajo sigue según [docs/PLAN-s3.md](docs/PLAN-s3.md).
