# Tests del reloj

Automáticos con `pytest`, organizados por fases del plan (`docs/PLAN-bluetooth.md`). El Bluetooth se prueba desde el PC con un adaptador USB (`bleak`).

```
pip install -r tests/requirements.txt
cd tests
pytest -m now          # rápido: unitarios y web (sin Bluetooth)
pytest -m phase2       # una fase concreta
pytest                 # todo
```

Opciones: `--host 192.168.1.140` (modo WiFi), `--port COM5`, `--ble-name "C6 Clock"`. Para saltar lo lento: `-m "now and not slow"`.

| Carpeta | Qué prueba | Fase | Necesita |
|---|---|---|---|
| `unit/` | Troceado de mensajes Bluetooth y vectores compartidos con la app (`app/vectors.json`) | ya | nada |
| `wifi/` | Web actual: estado, catálogo, ajustes, capturas, OTA, 30 fps | ya | reloj en la red |
| `firmware/` | Modos de arranque por la consola serie: WIFI → BLE → UPDATE (vuelve solo) → WIFI, y un reinicio a mitad de UPDATE (**reinicia la placa**) | 1 | USB |
| `ble/test_transport.py` | Anuncio, conexión, comandos, notificaciones, fugas de memoria | 2 | adaptador Bluetooth |
| `ble/test_sync.py` | Sincronización: hora y tiempo enviados por "el móvil", registro | 2 (y 5c con la app real) | adaptador Bluetooth + internet |
| `ble/test_sync_session.py` | Sesiones completas como la app: duración, 50 sincronizaciones y 30 reconexiones sin fugas | 2 | adaptador Bluetooth + internet |
| `ble/test_security.py` | Emparejamiento con la clave real (leída del log serie), clave errónea, comandos sin emparejar ignorados, emparejamiento que sobrevive a un reinicio, olvidar móviles | 3 | adaptador Bluetooth + USB |
| `ble/test_screenshot.py` | Capturas de pantalla por Bluetooth | 4 | adaptador Bluetooth |
| `app/framing.test.html` | Troceado de `src/transport.js` (abrir en el navegador) | 5a | navegador |
| `ble/test_web_page.py` | La página de control por Web Bluetooth en un Edge/Chrome real, manejado por DevTools (que también elige el reloj en el selector): carga, hora, ajustes en los dos sentidos, esferas, reconexión | 5a | adaptador Bluetooth + USB + Edge |
| `android/` | Tests unitarios Kotlin: troceado byte a byte contra `app/vectors.json`, comando `wx`, versiones, pasos de la sincronización (`cd android && gradlew test`) | 5b–5c | JDK 21 + SDK de Android |
| `app/test_android_app.py` | La app en el móvil por `adb`: arranca, sincroniza con el reloj, tarea periódica programada | 5b–5c | móvil por USB con depuración |
| `update/` | Actualización por internet contra un servidor del PC (`update_server.py`): http:// a internet rechazado, al día, correcta, SHA-256 erróneo, cortada, otro proyecto, sin manifiesto, lenta (límite de 5 min), corte de corriente a mitad (reset por USB) | 6 | Bluetooth + USB + WiFi + puerto 8770 abierto para Python |
| `MANUAL.md` | Lo que necesita el móvil (clave de emparejamiento, app instalada) | 3, 5, 6 | tú |

Los tests de Bluetooth ponen el reloj en modo BLE por la consola serie y lo devuelven al modo en que estaba al terminar. Los tests guardan los ajustes del reloj antes de empezar y los restauran al terminar (`conftest.py`), porque el reloj está en uso.

`clocklib/` contiene lo compartido: `protocol.py` (UUIDs y troceado), `ble.py` (enlace Bluetooth síncrono), `phone.py` (lo que hace la sincronización de la app, para simular el móvil) y `board.py` (HTTP y consola serie; `status()` lee el estado por la consola, sea cual sea el enlace) y `browser.py` (Edge/Chrome por DevTools, para probar la página por Web Bluetooth). `update/update_server.py` también sirve como herramienta suelta para probar actualizaciones a mano.
