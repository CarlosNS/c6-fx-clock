# Plan: reloj sincronizado por Bluetooth con el móvil

Objetivo: que el reloj se controle desde el móvil por **Bluetooth (BLE)** sin depender del WiFi, que el móvil lo **sincronice periódicamente** (hora y tiempo) aunque la app esté cerrada, y que pueda ordenarle **conectarse a internet para buscar y aplicar una actualización**.

Estado: **plan, sin implementar**. Cada fase termina en algo que funciona y se prueba solo; no se pasa a la siguiente sin que sus tests estén en verde.

Hardware de desarrollo: Waveshare ESP32-C6-LCD-1.47. Hardware final: Waveshare ESP32-S3-Touch-AMOLED-2.06 (apartado 6).

---

## 1. Decisiones

| # | Decisión | Respuesta | Consecuencia |
|---|---|---|---|
| D1 | Móvil | **Android** | App Android |
| D2 | Publicación de actualizaciones | **GitHub** | Repositorio + GitHub Pages para el manifiesto y el firmware; las APK de la app, en GitHub Releases |
| D3 | Modo WiFi (la web actual) | **Se mantiene** como respaldo | Modo WIFI accesible manteniendo BOOT en la pantalla de bienvenida o desde la app |
| D4 | Tiempo con la app cerrada | **Sincronización periódica** con la app | Una web no puede ejecutarse en segundo plano: hace falta una **app Android nativa** (apartado 3.5) |

Datos para las fases siguientes:
- **Móvil:** POCO F7 (12 GB), con HyperOS de Xiaomi sobre Android 15. HyperOS es de los más agresivos cerrando apps en segundo plano. La app tendrá que guiar para activar su **inicio automático** y poner la batería en **"Sin restricciones"**, además de usar Companion Device Manager. La prueba de una noche de la fase 5c es obligatoria.
- **GitHub:** usuario [CarlosNS](https://github.com/CarlosNS).
- **Herramientas:** tengo permiso para instalar GitHub CLI, JDK 21 y el SDK de Android.

---

## 2. La clave en el C6: memoria

| Situación | RAM libre (medido) |
|---|---|
| Tras la pantalla (2 framebuffers de 110 KB) | 108 KB |
| + WiFi + servidor web (hoy) | ~33 KB |
| WiFi + servidor web ocupan | ~74 KB |
| NimBLE solo periférico (estimado, se mide en la fase 0) | ~40–55 KB |

**En el C6, WiFi y Bluetooth a la vez no caben** con los dos framebuffers. Por eso aquí se usan **modos de arranque**:

| Modo | Radio | Pantalla | Para qué |
|---|---|---|---|
| **BLE** (normal) | Solo Bluetooth | 2 framebuffers, 30 fps | Uso diario y sincronización con el móvil |
| **UPDATE** | Solo WiFi (cliente) | 1 framebuffer + pantalla de progreso | Buscar y aplicar una actualización; vuelve solo a BLE |
| **WIFI** (respaldo) | WiFi + web actual | 2 framebuffers | Como hoy |

El paso de un modo a otro es un **reinicio controlado** (marca en NVS + `esp_restart`): cada modo arranca con la memoria limpia. En modo UPDATE se reserva un solo framebuffer, lo que deja ~110 KB extra para HTTPS (TLS necesita ~40 KB).

En el reloj final (8 MB de PSRAM) caben los dos a la vez. Por eso los modos dependen de la placa (`board.h`) y no se reparten por el código.

**Recuperación:** mantener pulsado BOOT 2 s mientras se ve la pantalla de bienvenida → cambia entre modo WIFI y BLE (por si se pierde el móvil o falla el Bluetooth). No vale "BOOT al encender": en el C6 ese botón es también el pin que, pulsado al arrancar, pone el chip en modo de grabación.

---

## 3. Diseño

```
     app Android (Kotlin + WebView)          PC: tests y herramientas      navegador (modo WIFI)
              |  BLE GATT                          |  BLE (bleak)                 |  HTTP + WebSocket
              \_________________  _________________/                              |
                                ble.c (nuevo)                                  web.c (actual)
                                       \______________________  _____________________/
                                                              \/
                         control.c  (comandos de texto + estado JSON, ya existe)
                         settings.c (tabla de ajustes, ya existe)
                         update.c   (nuevo: modo UPDATE, manifiesto, descarga HTTPS)
                         ota.c      (ya existe: escritura por trozos + vuelta atrás)
```

`control.c` y `ota.c` ya son independientes del transporte (se preparó en la 2.0.0), así que el Bluetooth es "otro transporte" más.

### 3.1 Servicio Bluetooth (GATT)

Nombre anunciado: `C6 Clock XXXX` (XXXX = final de la MAC). El reloj anuncia siempre, con un intervalo lento (~1 s) que gasta poco, para que el móvil lo encuentre en cada sincronización.

| Característica | UUID | Propiedades | Contenido |
|---|---|---|---|
| Servicio reloj | `7f1c0001-6c0c-4e0b-a1c6-0000c6c10c00` | — | — |
| **CMD** | `7f1c0002-…` | escritura (cifrada) | Un comando de texto, la misma sintaxis que hoy (`bri 80`, `fx 3`, `wifi\nred\nclave`…) |
| **EVT** | `7f1c0003-…` | notificación (cifrada) | Respuestas y eventos en JSON, troceados (ver 3.2) |
| Device Information (estándar `0x180A`) | `0x2A26` | lectura (pública) | Versión del firmware, para que la app la vea antes de emparejar |

Comandos nuevos (solo los que no existen ya):

| Comando | Respuesta en EVT |
|---|---|
| `get info` | `{"t":"info","ver":"2.1.0","mode":"ble","part":"ota_0","sync":{…},"upd":{…}}` |
| `get status` | `{"t":"status", …igual que /status…}` |
| `get faces` | `{"t":"faces","groups":[…],"faces":[…]}` (igual que /faces) |
| `sync done` | ack; el reloj apunta la hora de la última sincronización (ver 3.6) |
| `update apply <url>` | `{"t":"ack","cmd":"update"}` y reinicia en modo UPDATE |
| `mode wifi` / `mode ble` | ack y reinicio en ese modo |
| `unpair` | borra los móviles emparejados |
| `shot` (depuración) | la pantalla en trozos, para pruebas automáticas sin WiFi |

Además el reloj **envía `status` solo** cuando cambia un ajuste, y cada 30 s mientras haya conexión.

### 3.2 Troceado de mensajes

Un mensaje JSON (p. ej. `faces`, ~6 KB) no cabe en una notificación (máx. MTU − 3 = 244 bytes). Cada notificación lleva **1 byte de cabecera**:

```
bit 7  = último trozo
bit 6  = primer trozo
bits 0-5 = número de mensaje (0..63, rota)
```

El receptor concatena trozos del mismo número desde "primero" hasta "último". Se usa igual en la app y en los tests.

### 3.3 Seguridad

- Emparejamiento **LE Secure Connections con clave de 6 dígitos que aparece en la pantalla del reloj** (el móvil la teclea). Evita que un vecino controle el reloj y protege la contraseña del WiFi, que viaja por CMD.
- CMD y EVT exigen enlace cifrado; la versión (0x2A26) es pública.
- Hasta 3 móviles recordados (NVS de NimBLE). `unpair` o BOOT 10 s los borra.
- **Build de pruebas** (`-DCLOCK_BLE_TEST`): emparejamiento *Just Works* (cifrado pero sin clave), porque Windows/bleak no automatiza bien la entrada de clave. La clave real se prueba a mano con el móvil.

### 3.4 Actualización por internet

1. La app (que tiene internet) lee el **manifiesto** `https://<usuario>.github.io/c6-fx-clock/manifest.json`:
   `{"version":"2.1.0","url":".../c6-fx-clock-ota.bin","size":1601856,"sha256":"…","notes":"…"}`
2. Si hay versión nueva, avisa (también desde la sincronización periódica, con una notificación de Android) y muestra el botón **Actualizar** → envía `update apply <url-del-manifiesto>`.
3. El reloj guarda la orden en NVS y reinicia en **modo UPDATE**: un framebuffer, pantalla de progreso, WiFi con la red guardada.
4. Descarga el manifiesto, comprueba proyecto/versión, descarga con `esp_https_ota` (certificados raíz incluidos), comprueba tamaño y **SHA-256** y activa el hueco nuevo.
5. Guarda el resultado en NVS y reinicia en modo BLE. `get info` lo devuelve como `"upd":{"state":…,"at":<contador>,"from":"2.0.0","to":"2.1.0"}`, donde `state` es `updated`, `up_to_date`, `no_wifi`, `no_internet`, `bad_manifest`, `bad_image` o `timeout`, y `at` crece con cada intento para que la app sepa que el resultado es nuevo.
6. La app se reconecta sola, pide `get info` y muestra el resultado.

Solo HTTPS en el firmware normal; el **build de pruebas** admite también `http://` para el servidor local de la fase 6. Límites de seguridad: WiFi 20 s, descarga 5 min; si se supera, se aborta sin tocar el firmware actual. La **vuelta atrás automática** de la 2.0.0 sigue protegiendo si la versión nueva se cuelga.

### 3.5 La app Android

**App nativa en Kotlin** con la interfaz en un **WebView**:

- **Capa Bluetooth en Kotlin:** conexión, emparejamiento, troceado y la sincronización en segundo plano. Es lo único que una web no puede hacer.
- **Interfaz en HTML/JS dentro de la app:** es la web actual, con la misma lógica y el mismo diseño. Habla con Kotlin por un puente JavaScript, así que el código de interfaz es uno solo.
- **Tres transportes para esa interfaz** (`transport.js`):
  - **Android:** el puente a Kotlin, que es el que usa la app.
  - **Web Bluetooth:** para que yo la pruebe en Chrome del PC con el pincho, sin móvil.
  - **WebSocket:** el modo WIFI de respaldo.
- **Emparejamiento con el sistema (Companion Device Manager):** Android trata el reloj como un accesorio, como hace con los relojes comerciales. Así la app puede trabajar en segundo plano sin que el ahorro de batería la mate.
- **Pantallas:**
  - **Esfera** y **Elegir esfera**: como hoy.
  - **Ajustes**.
  - **Reloj**: versión, emparejamiento, WiFi para actualizaciones, actualizar, modo WiFi, última sincronización.
- **Distribución:** APK firmada en GitHub Releases, instalada a mano. No pasa por Google Play.
- **Requisitos:** Android 10 o superior. Lo ideal es Android 12+, por los permisos de Bluetooth.

### 3.6 Sincronización periódica

- **Cuándo:** la app hace una sincronización corta cada **30 min** (WorkManager, tarea periódica que Android respeta aunque la app esté cerrada), y además cada vez que se abre la app.
- **Qué hace cada sincronización** (unos 3 s de conexión, poca batería):
  1. Conecta al reloj emparejado.
  2. Envía `time <epoch> <ms>`.
  3. Pide el tiempo a Open-Meteo para la ciudad configurada y lo envía con `wx …`.
  4. Lee `get info`: versión y resultado de la última actualización.
  5. Comprueba si hay firmware nuevo en el manifiesto y, si lo hay, muestra una notificación de Android.
  6. Envía `sync done` y desconecta.
- **En el reloj:** `get info` incluye `"sync":{"last":<epoch>,"count":n}`. Si no hay sincronización en más de 2 horas, la esfera Tiempo indica que los datos son antiguos (ya lo hace por edad). El reloj sigue funcionando solo, con su propia hora.
- **Simulador de móvil:** en el PC, `tools/fake_phone.py` hace exactamente lo mismo que la tarea de la app. Así puedo probar el lado del reloj (y horas de sincronizaciones seguidas) sin el móvil.

---

## 4. Fases

Cada fase: implementación → tests automáticos → revisión en la placa → commit. Los tests están en `tests/` (ver `tests/README.md`).

### Fase 0 — Preparación y medida (½ día)

- Crear repositorio git con el estado actual (commit base `v2.0.0`) y subirlo a GitHub.
- Herramientas de pruebas: `pip install -r tests/requirements.txt` (ya instaladas: pytest y bleak).
- **Prototipo desechable**: NimBLE periférico mínimo con WiFi apagado. Medir heap libre, tamaño en flash y fps de las 23 esferas.
- **Criterio para seguir:** ≥ 30 KB libres en modo BLE con la esfera más pesada y 30 fps. Si no se cumple, plan B: framebuffer único en todos los modos (≈ 20–25 fps en las esferas pesadas).

Tests: `pytest -m now` en verde (23 tests, ya pasan) · medida de heap anotada en este documento.

**Resultado (2026-11-02): superado.** Prototipo en la rama `spike/ble-medida` (no se fusiona) y log en `docs/spike-ble-medida.log`.

| Medida | Valor |
|---|---|
| Heap tras la pantalla, con Bluetooth compilado | 92,7 KB (el controlador reserva ~15 KB estáticos) |
| Coste de NimBLE en heap (controlador + host + servicio) | 28 KB |
| **Heap libre en modo BLE, en reposo** | **64 KB** (mínimo histórico 63,3 KB) |
| **Heap libre con el PC conectado** | **61 KB** (mínimo 60,1 KB) |
| Flash del firmware sin WiFi | 1,33 MB (frente a 1,60 MB con WiFi) |
| Esferas | Las 23 a 30 fps; peor fotograma 25,8 ms (Cuento) |
| Coste de la conexión en CPU | Ninguno apreciable (Cuento y Clásico, iguales conectados y sin conectar) |
| MTU negociado con Windows | 256 bytes |
| 100 comandos seguidos | 1,1 s, sin errores |

Hallazgos para las fases siguientes:
- **RPG tiene picos propios de 35–41 ms** durante 2–3 s cada ~40 s (movimiento de cámara); el resto del tiempo va a 26–29 ms. No es el Bluetooth, y probablemente pasaba ya con WiFi (antes solo se medía 2–3 s). Se corrige en la fase 2 y el test de 30 fps de RPG pasa a medir un minuto entero.
- **Descubrimiento desde Windows irregular:** 2,8 s en una prueba y más de 30 s en otra. El móvil emparejado conectará por dirección (no necesita buscar), pero en la fase 2 se acelera el anuncio durante 30 s tras cada desconexión y se incluye el UUID del servicio.
- **Los tests deben guardar y restaurar los ajustes:** las pruebas del prototipo cambiaron la esfera y "siempre encendida", y se guardaron en NVS. Desde la fase 2 un fixture guarda el estado antes y lo restaura después.

### Fase 1 — Modos de arranque y capa de placa (1 día)

- `boot_mode` en NVS (`ble` / `wifi` / `update`) leído al arrancar. `main.c` arranca solo lo que necesita ese modo.
- `board.h`: lo que depende de la placa (radios exclusivas o no, número de framebuffers, pines). Es lo que cambiará con el reloj final.
- `lcd_init(n_buffers)` para poder usar un solo framebuffer.
- Servicios condicionados: servidor web, SNTP y descarga del tiempo solo en modo WIFI.
- BOOT mantenido 2 s en la pantalla de bienvenida → cambia entre WIFI y BLE.

Tests: `tests/firmware/test_boot_modes.py` · `tests/wifi/` sigue en verde en modo WIFI.

**Resultado (2026-11-02): terminada.** 29 tests en verde (`pytest -m "phase1 or now"`).

- `bootmode.c`: modos `ble` / `wifi` / `update` en NVS y comando `mode <nombre>`. El modo UPDATE es de un solo uso: antes de hacer nada deja guardado el modo de vuelta, así que un cuelgue a mitad de una actualización nunca deja el reloj atrapado en él (hay un test que lo provoca).
- `board.h`: pines y capacidades de la placa (radios exclusivas, número de framebuffers). `lcd_init(n)` admite 1 o 2 búferes.
- `console.c`: **consola por el puerto serie** con los mismos comandos que la web. Los tests y yo controlamos el reloj en cualquier modo, incluso sin radios. Primero se hizo con el driver USB, pero costaba 8 KB y dejaba la web con 23 KB y fallando; ahora el bucle principal lee el puerto sin driver y no cuesta memoria.
- Memoria libre al arrancar: **35 KB en modo WIFI** (igual que la 2.0.0) y **~100 KB en modo BLE** (todavía sin Bluetooth).
- Modo BLE por defecto: sigue siendo WIFI hasta que exista el transporte Bluetooth (fase 2), para no dejar el reloj sin control.
- **RPG arreglado:** los tests honestos mostraron que bajaba a 24 fps (40 ms) durante el hechizo de cada minuto y rondaba los 31 ms el resto. Medido por partes con la consola: la hora caligráfica costaba 5,6 ms y el panel del grupo 2,1 ms. La fuente con degradado ahora se dibuja en 2 pasadas en vez de 3 (también acelera Cuento) y el panel, que está fuera de la escena 3D, se pinta opaco (mismo aspecto). Ahora: 22 ms de base, 31 ms en el hechizo, 30 fps todo el minuto.
- El test de 30 fps ahora exige 30 fps sostenidos (media < 33 ms); los fotogramas sueltos lentos solo se avisan si pasan de 45 ms.

### Fase 2 — Transporte Bluetooth (2 días)

- `ble.c`: NimBLE periférico, anuncio continuo, servicio GATT, troceado, `get info/status/faces`, `sync done`, envío de status al cambiar.
- Build de pruebas con Just Works.
- Herramientas `tools/ble_console.py` (comandos a mano desde el PC) y `tools/fake_phone.py` (una sincronización como la de la app).

Tests: `tests/ble/test_transport.py` (anuncio, conexión, MTU, info, faces = 23, ajustes y notificaciones, comandos inválidos, ráfagas, fugas de memoria) · `tests/ble/test_sync.py` (hora y tiempo enviados por "el móvil", `sync done`, 50 sincronizaciones seguidas sin fugas).

**Resultado (2026-11-02): terminada.** 17 tests de Bluetooth en verde (`pytest -m phase2`), con las pruebas largas incluidas.

- `ble.c`: NimBLE periférico, servicio del reloj (CMD, EVT troceado) + Device Information con la versión, `get info/status/faces`, `sync done`, envío del estado al cambiar un ajuste y cada 30 s. Un hilo propio procesa los comandos, así las respuestas grandes (esferas, ~6 KB) no bloquean la radio.
- Anuncio rápido (100 ms) durante 30 s tras arrancar o desconectar y lento (1 s) después; el UUID del servicio va en el anuncio y el nombre en la respuesta al escaneo.
- **Memoria:** en el C6 el controlador Bluetooth deja ~29 KB de *código* en RAM en todos los modos (el código y los datos comparten la SRAM), y el modo WiFi se quedaba con 5,6 KB. Solución: `BT_CTRL_RUN_IN_FLASH_ONLY` (el controlador corre desde flash; suficiente para comandos cortos), `esp_bt_mem_release` en los modos sin Bluetooth y 6 búferes fijos de recepción WiFi en vez de 10. Resultado: **35,8 KB libres en modo WiFi** (igual que antes) y **64 KB en modo BLE**.
- Medidas: 50 sincronizaciones seguidas y 30 ciclos de reconexión sin perder memoria; una sincronización con la dirección ya conocida (como hará la app) tarda unos pocos segundos.
- Herramientas: `tools/ble_console.py` (comandos a mano) y `tools/fake_phone.py` (sincronizaciones como la app, una o en bucle).
- Web en modo WiFi: botón *Avanzado → Modo Bluetooth*.
- El modo por defecto sigue siendo WIFI hasta que exista la app (fase 5): sin ella, el modo BLE solo se puede controlar desde el PC.
- Lecciones de los tests: en Windows todas las conexiones a una misma dirección comparten una sesión del sistema, así que la librería de tests usa un único bucle de eventos y nunca deja clientes a medias; y los tests que abren sus propias conexiones van en un módulo aparte, porque el reloj acepta una sola conexión.
- **Bajo carga** (ráfaga de 100 comandos), el log serie mostró dos fallos reales del firmware: la cola de comandos se llenaba y la pila Bluetooth esperaba 20 ms por cada comando que no cabía (más de un segundo bloqueada, suficiente para que Windows diera el enlace por perdido), y se enviaba el estado completo tras cada cambio hasta agotar los búferes. Ahora la pila nunca espera (cola de 16; lo que no cabe se descarta y se registra) y los cambios se anuncian como mucho cada 300 ms con los últimos valores, que es lo que necesita un deslizador de la app.
- Los tests de Bluetooth guardan el log serie del reloj en `tests/.ble_serial.log` para leer cada fallo junto a lo que hacía el reloj.

### Fase 3 — Seguridad (1 día)

- Clave de 6 dígitos en pantalla, enlace cifrado obligatorio, bonding, `unpair`, BOOT 10 s.

Tests: `tests/ble/test_security.py` · prueba manual con el móvil (`tests/MANUAL.md`).

**Resultado (2026-11-03): terminada.** 8 tests de seguridad en verde (`pytest -m phase3`), con la clave real.

- LE Secure Connections con clave de 6 dígitos aleatoria (protección contra intermediarios), bonding en NVS (hasta 3 móviles; el más antiguo se olvida si no cabe). CMD solo acepta escrituras de enlaces emparejados con clave y el reloj no envía nada por enlaces sin emparejar; la versión (0x2A26) sigue siendo pública.
- La clave aparece en grande sobre cualquier esfera y **la pantalla no se apaga mientras haya un emparejamiento en curso**, aunque se tarde en teclearla.
- Olvidar móviles: comando `unpair` (Bluetooth o consola serie) o BOOT 10 s (a los 3 s avisa). La pulsación larga normal ahora cambia el modo automático al soltar.
- **Sin build de pruebas:** el plan preveía uno con *Just Works* porque Windows no teclea la clave, pero la API de Windows que hay debajo de bleak sí permite entregarla desde código (`ClockLink.pair_with_pin`). El reloj escribe la clave en su log serie y los tests la leen de ahí, así que se prueba el mismo firmware que usarás, con clave de verdad.
- Lo que queda para la prueba manual: ver la clave en la pantalla y teclearla en el móvil (`tests/MANUAL.md`).
- Detalles de Windows que reflejan los tests: al olvidar un dispositivo lo borra también de su caché (hay que volver a encontrarlo escaneando) y puede mantener el enlace abierto si no se desconecta explícitamente; tras reiniciarse el reloj, el primer intento de conexión a un dispositivo emparejado puede fallar y hay que reintentar (la app hará lo mismo).

### Fase 4 — Captura de pantalla por Bluetooth (½ día)

- Comando `shot` y `tools/ble_shot.py` → PNG, para comprobar las esferas sin WiFi.

Tests: `tests/ble/test_screenshot.py`.

**Resultado (2026-11-03): terminada.** Una captura tarda **5–6 s** (unos 110 KB en ~600 notificaciones), en vertical y en horizontal.

- `shot`: congela el render (como `/screenshot` en WiFi), envía cabecera, una fila por mensaje (RGB565 en base64) y fin. Si el enlace cae a mitad, se corta y el reloj sigue animando.
- `tools/ble_shot.py`: captura una o varias esferas a PNG (`--fx 22 7 20 --scale 2`); si Windows no está emparejado, empareja con la clave del log serie (`--serial COM5`) o pidiéndola por teclado.

### Fase 5 — App Android (5–7 días, en tres partes)

Requisitos previos (con tu permiso): SDK de Android en línea de comandos, JDK 21 (el JDK 25 instalado es demasiado nuevo para las herramientas de Android) y GitHub CLI. Para probar en el móvil: activar la depuración USB y conectarlo al PC.

- **5a. Interfaz compartida (1–2 días):** `transport.js` con los tres transportes y la web actual adaptada. Se prueba en Chrome del PC con Web Bluetooth y el pincho, sin móvil todavía.
- **5b. App Kotlin (2–3 días):** WebView, puente JS, capa Bluetooth, emparejamiento con Companion Device Manager y permisos. APK firmada.
- **5c. Sincronización periódica (1–2 días):** WorkManager cada 30 min, Open-Meteo, aviso de versión nueva con notificación, ciudad configurable.

Tests:
- `tests/app/framing.test.html` (troceado de la interfaz, en el navegador).
- Tests unitarios de Kotlin con `gradlew test` (troceado, parser, planificación de la sincronización); se ejecutan en el PC.
- `tests/ble/test_sync.py` contra el reloj usando la app real (`adb` lanza una sincronización).
- Prueba manual en el móvil (`tests/MANUAL.md`), incluida una noche con la app cerrada.

**Resultado (2026-11-03): hecha la parte que no necesita el móvil.** Falta instalarla en el móvil, emparejarla con la clave real y la noche en segundo plano (`tests/MANUAL.md`).

- **5a.** `src/transport.js` (el reloj lo sirve en `/transport.js`) da a la página los tres enlaces con las mismas llamadas: `start`, `send`, `request('status'|'faces'|'info')`. Por Bluetooth el estado llega empujado por el reloj y la página pide uno cada 5 s; la hora avanza en local entre mensajes. Las órdenes de arrastre van cada 60 ms en vez de 30. Por Bluetooth se oculta la subida de firmware por archivo, y el botón de modo sirve en los dos sentidos. Probado en un Edge real con el pincho: conecta, carga las 23 esferas, los ajustes llegan en los dos sentidos y se reconecta sola **4,5 s** después de que vuelva el reloj. El selector de dispositivos se contesta por DevTools (`DeviceAccess`), así que no hace falta nadie delante. El navegador integrado de la app de Claude no tiene selector.
- **5b.** `android/` (Kotlin, minSdk 29, targetSdk 35): WebView con la página (copiada de `src/` al compilar, una sola copia), puente `ClockBridge`, `ClockBle` (MTU 247, notificaciones cifradas, una operación GATT a la vez), emparejamiento con Companion Device Manager + `createBond` (Android pide la clave), "Olvidar este reloj" y acceso a los ajustes de batería. APK firmada con clave propia (fuera del repositorio, en `%USERPROFILE%\.c6clock`).
- **5c.** `SyncWorker` (WorkManager, cada 30 min): hora, tiempo de Open-Meteo para la ciudad del reloj, `get info`, `sync done`. Si la app está abierta reutiliza su enlace. Avisa con una notificación, una vez por versión, si el manifiesto de la fase 6 trae una versión mayor (hasta entonces el manifiesto no existe y no pasa nada).
- **Tests:** 11 tests unitarios de Kotlin, que comprueban el troceado byte a byte contra `tests/app/vectors.json` (generado desde `protocol.py`); `framing.test.html` 15/15 sobre el `transport.js` real; `tests/ble/test_web_page.py` 6/6; `tests/app/test_android_app.py` listo para cuando el móvil esté conectado por USB.
- **De paso:**
  - `net.c`: si un nodo de la malla asocia pero no da IP en 20 s, el reloj busca y fija el siguiente nodo más fuerte de la misma red (antes reintentaba siempre el mismo y se quedaba sin red).
  - La consola serie tiene `status`, que imprime el estado en JSON en cualquier modo; los tests la usan para comprobar el reloj mientras otro programa tiene el enlace Bluetooth.
- **Memoria:** en modo WiFi quedan unos 30–33 KB libres frente a los 35,8 KB de la fase 2. No viene de esta fase (la RAM fija es la misma que en la fase 4, +8 bytes); queda por investigar.

### Fase 6 — Actualización por internet (2 días)

- `update.c`: modo UPDATE, manifiesto, `esp_https_ota`, SHA-256, resultado en NVS, límites de tiempo.
- `tools/release.py`: compila, sube versión, genera `manifest.json` con SHA-256 y publica en GitHub Pages (GitHub CLI).
- App: comprobar versión, botón Actualizar, reconexión y resultado.

Tests: `tests/update/test_update.py` con un **servidor local de pruebas** en el PC que sirve manifiestos y firmwares. Casos:
- ya actualizado;
- actualiza;
- SHA-256 incorrecto;
- firmware cortado;
- firmware de otro proyecto;
- manifiesto inexistente;
- descarga lenta → `timeout`;
- corte de corriente a mitad.

**Resultado (2026-11-03): terminada. Publicada la 2.1.0.**

- **El modo UPDATE** tiene 203 KB libres (un solo framebuffer y sin Bluetooth), sobra para HTTPS. Pasos:
  1. Conecta con la red guardada; la vuelta a otro nodo de la malla también vale aquí.
  2. Lee el manifiesto y compara la versión.
  3. Descarga en trozos de 4 KB calculando el SHA-256 sobre la marcha.
  4. `ota.c` rechaza otro proyecto antes de escribir nada; se comprueban también tamaño, hash y límite de 5 min.
  5. Guarda el resultado y vuelve al modo anterior.
  
  Los buffers se reservan solo en ese modo: como `static` costaban 5,5 KB de RAM en todos los modos.
- **http://** solo se acepta hacia direcciones privadas (10/8, 172.16/12, 192.168/16), así que no hace falta una versión especial para los tests. Hacia internet, solo https. La orden también exige estar emparejado.
- **Resultado:** el campo `upd` sale en `/status`, en el estado por Bluetooth y en `get info`. Además de los estados previstos, existen `running`, que pasa a `interrupted` si un reset corta la actualización, y `none`.
- **Red WiFi en modo Bluetooth:** se puede guardar (copia propia en NVS), porque el driver WiFi solo deja leer su copia con el WiFi en marcha.
- **Encontrado por los tests:** un cambio de modo en los 30 s siguientes a una actualización provocaba la vuelta atrás. Ahora `boot_mode_switch` confirma la versión en uso.
- **Página y app:** la tarjeta *Actualizaciones* lee el manifiesto desde el móvil o navegador, ofrece la versión nueva y muestra el resultado al volver el reloj. La app no necesita nada más: usa la misma página, y su sincronización avisa con una notificación.
- **Tests:** `tests/update`, 9 de 9: http hacia internet rechazado, ya al día, actualiza, SHA-256 erróneo, imagen cortada, otro proyecto, sin manifiesto, lenta → `timeout` a los 5 min, corte de corriente → `interrupted`. El corte se simula reabriendo el puerto USB: el truco de RTS de esptool no reinicia esta placa en Windows.
- **Publicación:** `tools/release.py` publicó la 2.1.0 en GitHub Pages (manifiesto, imagen y web de instalación en `/flasher/`) y como release con la app. Pages envía `Access-Control-Allow-Origin: *`, así que la página puede leer el manifiesto desde cualquier origen.
- **Prueba real por HTTPS:** el reloj, con una 2.0.99 grabada por USB, se actualizó a la 2.1.0 desde GitHub Pages en **35 s** de principio a fin. La página mostró "Actualizado (2.0.99 → 2.1.0)" y "Tienes la última versión", y tras un reset arranca desde `ota_1` con la 2.1.0 (confirmada). Heap en modo WiFi: 29,8 KB.

### Fase 7 — Cierre (½ día)

- README, webflasher, notas de versión 3.0.0, limpieza del prototipo, memoria del proyecto.

Tests: suite completa en verde (`pytest tests`).

**Resultado (2026-11-03): cerrada a falta del móvil.**

- **Documentación:**
  - README actualizado: modos, app, actualizaciones por internet, enlaces a las releases y al instalador web.
  - Instalador web: cómo actualizar sin cable y cómo usar la app.
  - Notas de versión en `CHANGELOG.md`.
- **La 3.0.0 está preparada pero no publicada.** Se publicará cuando la app pase la prueba manual en el móvil. En ese momento `BOOT_DEFAULT` pasará a `BOOT_BLE` (los pasos están en `CHANGELOG.md`). Hasta entonces, una instalación nueva arranca en WiFi, para no dejar a nadie con un reloj que solo se maneja con una app sin probar.
- **Prototipo:** la rama `spike/ble-medida` se conserva; sus medidas están en la sección de la fase 0.
- **Encontrado al pasar la suite completa:** los tests de actualización importaban el fixture de paquete de `tests/ble`. pytest lo cerraba al final de la sesión, y los tests WiFi se ejecutaban con el reloj aún en Bluetooth. Ahora cada paquete crea el suyo con `ble_mode()` (y `tests/update` tiene su `__init__.py`). Además, el test de actualización se detiene si la imagen de prueba no es de la versión que tiene el reloj, para no instalarle otra.
- **Suite completa:** 72 tests correctos y 3 omitidos (los de la app en el móvil), en 32 min. Falló una vez `test_pairing_code_changes_every_time`, con la cancelación intermitente del emparejamiento en Windows; repetido dos veces, pasa.
- **Pendiente con el móvil:** `tests/MANUAL.md` (fases 3, 5, 5c y 6) y `tests/app/test_android_app.py`.

**Total estimado: 13–16 días de trabajo**, en sesiones cortas, fase a fase.

---

## 5. Riesgos

| Riesgo | Probabilidad | Mitigación |
|---|---|---|
| NimBLE no cabe con 2 framebuffers en el C6 | Media | Se mide en la fase 0 antes de construir nada; plan B: 1 framebuffer. No aplica al reloj final |
| Android mata la sincronización en segundo plano | Media (según marca) | Companion Device Manager + WorkManager; guía de ajustes de batería para tu marca |
| Windows no automatiza el emparejamiento con clave | Alta | Build de pruebas con Just Works + prueba manual de la clave |
| Tamaño de las herramientas de Android (2–3 GB) | Seguro | Se instalan una vez, con tu permiso, al empezar la fase 5 |
| El cortafuegos de Windows bloquea el servidor local de la fase 6 | Media | Pedirte permiso para una regla temporal, o servir desde GitHub Pages en una ruta de pruebas |
| Red mallada inestable en modo UPDATE | Media | Ya hay perro guardián de DHCP; límites de tiempo y resultado `no_wifi` |
| Abrir COM5 reinicia la placa | Seguro | Los tests de Bluetooth no usan el puerto serie; los de arranque sí, a propósito |

---

## 6. Pensando en el reloj final (ESP32-S3-Touch-AMOLED-2.06)

Hoy solo se tiene en cuenta para no cerrar puertas; seguimos con el C6.

| Del reloj final | Qué implica | Qué hacemos ya en este plan |
|---|---|---|
| ESP32-S3R8, **8 MB de PSRAM**, 32 MB de flash | WiFi y Bluetooth a la vez; framebuffers en PSRAM | Los modos son configuración de la placa (`board.h`), no lógica repartida |
| **Batería** (gestor AXP2101) | La autonomía manda: el WiFi solo para actualizar | El diseño "Bluetooth primero, WiFi solo para actualizar" ya es el correcto; el estado podrá incluir la batería |
| **RTC con batería** (PCF85063) | Mantiene la hora apagado | La sincronización del móvil corrige la hora, pero no es imprescindible |
| **Pantalla táctil** AMOLED 410×502 (CO5300 por QSPI) | Las esferas actuales están dibujadas para 172×320 / 320×172; el AMOLED gasta menos con fondos negros | Lo nuevo (control, Bluetooth, app, actualización) no depende de la pantalla; las esferas tendrán que rediseñarse o escalarse en su momento |
| Táctil (FT3168), IMU (QMI8658), audio, botones BOOT y PWR | Cambiar de esfera deslizando, levantar la muñeca para encender, avisos con sonido | Los comandos de control ya son los mismos para botón, táctil o app |

Los únicos módulos que cambiarán con la placa son `lcd.c`, `board.h`, la entrada (botón/táctil) y los modos de arranque. El protocolo Bluetooth, la app, las actualizaciones y los tests valdrán para los dos relojes.

---

## 7. Lo que puedo hacer solo y lo que necesita tu mano

| Solo (con el pincho Bluetooth y el USB) | Contigo |
|---|---|
| Compilar, flashear, logs serie | Usuario de GitHub y `gh auth login` (la sesión es tuya) |
| Todos los tests de `tests/ble`, `tests/update`, `tests/wifi`, `tests/firmware` | Permiso para instalar el SDK de Android, el JDK 21 y GitHub CLI |
| Simular el móvil (`fake_phone.py`) y horas de sincronizaciones | Emparejar el móvil con la clave real |
| Capturas de pantalla por Bluetooth | Conectar el móvil por USB con depuración activada |
| Probar la interfaz en Chrome del PC con el pincho | Instalar la APK y dejarla una noche en segundo plano |
| Compilar la app y sus tests unitarios | Ajustes de batería de tu marca de móvil |
| Servidor local de actualizaciones | Permiso para la regla del cortafuegos, si hace falta |
