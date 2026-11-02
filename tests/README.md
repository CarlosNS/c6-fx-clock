# Tests del reloj

Automáticos con `pytest`, organizados por fases del plan (`docs/PLAN-bluetooth.md`). El Bluetooth se prueba desde el PC con un adaptador USB (`bleak`).

```
pip install -r tests/requirements.txt
cd tests
pytest -m now          # lo que ya funciona hoy (firmware 2.0.0)
pytest -m phase2       # una fase concreta
pytest                 # todo
```

Opciones: `--host 192.168.1.140` (modo WiFi), `--port COM5`, `--ble-name "C6 Clock"`. Para saltar lo lento: `-m "now and not slow"`.

| Carpeta | Qué prueba | Fase | Necesita |
|---|---|---|---|
| `unit/` | Troceado de mensajes Bluetooth | ya | nada |
| `wifi/` | Web actual: estado, catálogo, ajustes, capturas, OTA, 30 fps | ya | reloj en la red |
| `firmware/` | Modos de arranque por la consola serie: WIFI → BLE → UPDATE (vuelve solo) → WIFI, y un reinicio a mitad de UPDATE (**reinicia la placa**) | 1 | USB |
| `ble/test_transport.py` | Anuncio, conexión, comandos, notificaciones, fugas de memoria | 2 | adaptador Bluetooth |
| `ble/test_sync.py` | Sincronización: hora y tiempo enviados por "el móvil", registro | 2 (y 5c con la app real) | adaptador Bluetooth + internet |
| `ble/test_sync_session.py` | Sesiones completas como la app: duración, 50 sincronizaciones y 30 reconexiones sin fugas | 2 | adaptador Bluetooth + internet |
| `ble/test_security.py` | Cifrado obligatorio, versión pública, olvidar móviles | 3 | adaptador Bluetooth |
| `ble/test_screenshot.py` | Capturas de pantalla por Bluetooth | 4 | adaptador Bluetooth |
| `app/framing.test.html` | Troceado en la interfaz de la app (abrir en el navegador) | 5a | navegador |
| *(app Android)* | Tests unitarios Kotlin con `gradlew test` dentro del proyecto de la app | 5b–5c | SDK de Android |
| `update/` | Actualización: al día, correcta, corrupta, otro proyecto, sin manifiesto, lenta, corte de corriente | 6 | Bluetooth + WiFi + puerto 8770 |
| `MANUAL.md` | Lo que necesita el móvil (clave de emparejamiento, app instalada) | 3, 5, 6 | tú |

Los tests de Bluetooth ponen el reloj en modo BLE por la consola serie y lo devuelven al modo en que estaba al terminar. Los tests guardan los ajustes del reloj antes de empezar y los restauran al terminar (`conftest.py`), porque el reloj está en uso.

`clocklib/` contiene lo compartido: `protocol.py` (UUIDs y troceado), `ble.py` (enlace Bluetooth síncrono), `phone.py` (lo que hace la sincronización de la app, para simular el móvil) y `board.py` (HTTP y consola serie). `update/update_server.py` también sirve como herramienta suelta para probar actualizaciones a mano.
