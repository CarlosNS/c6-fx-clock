## Firmware

Con [PlatformIO](https://platformio.org/) (ESP-IDF 5.5; un solo entorno, `s3amoled`):

```
pio run                          # compilar
pio run -t upload                # compilar y grabar por USB
python tools/bootlog.py          # reinicia el reloj y muestra su arranque (abrir el puerto no lo reinicia)
python tools/build_webflasher.py # genera webflasher/marea.bin (USB) y marea-ota.bin (OTA)
```

La versión está en `CMakeLists.txt` (`PROJECT_VER`). Los archivos que se incrustan en el firmware (`index.html`, `transport.js`) se declaran en `platformio.ini` **y** en `src/CMakeLists.txt`.

**Consola serie:** cualquier orden (ver [[Arquitectura]]) escrita como una línea por el puerto serie funciona, p. ej. `fx 7`, `ui launcher` o `status`. Muy útil cuando no hay WiFi ni Bluetooth.

**Configuración:** si cambias `sdkconfig.defaults`, borra `sdkconfig.s3amoled` para que se regenere (compilación en limpio, unos 5 minutos). `esp_tinyusb` se descarga del registro de componentes de Espressif (`src/idf_component.yml`; `dependencies.lock` fija la versión).

## App Android

Necesita **JDK 21** (`JAVA_HOME`) y el SDK de Android (`android/local.properties` con `sdk.dir=…`).

```
cd android
gradlew assembleDebug     # app/build/outputs/apk/debug/app-debug.apk
gradlew test              # tests unitarios (troceado, sincronización, versiones)
gradlew lintRelease       # lint
gradlew assembleRelease   # firmada: necesita android/keystore.properties (fuera de git)
```

La página de la app es la misma `src/index.html`: se copia al compilar, así que un cambio de la página llega a la web y a la app a la vez. Cada versión publicada de la app necesita un `versionCode` mayor que la anterior, porque así decide la app si actualizarse.

## Tests

Automáticos con pytest, organizados por fases del plan. Detalles en `tests/README.md`.

```
pip install -r tests/requirements.txt
cd tests
pytest -m now          # rápido: unitarios y web
pytest -m phase2       # una fase concreta
pytest                 # todo (unos 30 min; necesita el reloj por USB, WiFi y un adaptador Bluetooth)
```

- Los tests de Bluetooth ponen el reloj en modo BLE por la consola serie y lo devuelven a su modo al terminar; también guardan y restauran los ajustes.
- `tests/app/test_android_app.py` prueba la app en un móvil real por `adb`.
- `tests/MANUAL.md` recoge lo que necesita a una persona (emparejar, una noche en segundo plano…).

## Publicar una versión

```
python tools/release.py --dry-run --notes "Qué cambia"   # prepara release/ sin publicar
python tools/release.py --notes "Qué cambia"             # compila, publica en GitHub Pages y crea la release
```

Publica el firmware, el instalador web y la app, y escribe el manifiesto que leen los relojes y la app. Necesita la CLI de GitHub con sesión iniciada. Antes, sube `PROJECT_VER` (y el `versionCode` de la app si ha cambiado).

## Contribuir

- Fallos e ideas: [issues](https://github.com/CarlosNS/c6-fx-clock/issues).
- Normas de estilo y la licencia de las contribuciones: [CONTRIBUTING.md](https://github.com/CarlosNS/c6-fx-clock/blob/main/CONTRIBUTING.md). Resumen: el código se aporta con la misma licencia GPL-3.0-or-later y su autor puede ofrecerlo también con otras licencias.
- Las esferas se dibujan por código: sin imágenes ni fuentes de terceros. Para hacer una: [[Crear una esfera]].

## Esta wiki

Sus páginas viven en el repositorio, en `docs/wiki/`, y se revisan como el código. Para publicarlas:

```
python tools/publish_wiki.py --dry-run   # qué cambiaría
python tools/publish_wiki.py             # publica docs/wiki/ en la wiki de GitHub
```
