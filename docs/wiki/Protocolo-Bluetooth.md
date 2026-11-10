Cómo hablan el reloj y el móvil (o cualquier programa) por Bluetooth LE. La referencia completa está en `docs/PLAN-bluetooth.md` y la implementación de referencia en `tests/clocklib/protocol.py`, `src/transport.js` y `Protocol.kt` (comparten vectores de prueba: `tests/app/vectors.json`).

## Anuncio

El reloj se anuncia como **`Marea XXXX`** (las 4 últimas cifras hexadecimales de su dirección) con el UUID de servicio de abajo. Acepta **una conexión a la vez**: mientras alguien está conectado, deja de anunciarse.

## Servicio GATT

| Característica | UUID | Propiedades | Uso |
|---|---|---|---|
| Servicio del reloj | `7f1c0001-6c0c-4e0b-a1c6-0000c6c10c00` | | |
| Comando | `7f1c0002-6c0c-4e0b-a1c6-0000c6c10c00` | escritura, **cifrada** | Un comando de texto UTF-8 por escritura (máx. 248 bytes) |
| Eventos | `7f1c0003-6c0c-4e0b-a1c6-0000c6c10c00` | notificación, **cifrada** | Mensajes JSON troceados (abajo) |
| Firmware | `7f1c0004-6c0c-4e0b-a1c6-0000c6c10c00` | escritura sin respuesta, **cifrada** | Trozos de una actualización (abajo). Desde la 3.0.0-beta.6 |
| Versión de firmware | `00002a26-…` (Device Information) | lectura, pública | Para saber qué firmware hay sin emparejar |

El reloj pide un MTU de 247.

## Seguridad

- **LE Secure Connections** con *Passkey Entry*: el reloj muestra una clave aleatoria de 6 cifras en pantalla y el móvil la escribe (hay 30 s). Protege contra quien escuche o se ponga en medio.
- Los emparejamientos se guardan en la flash (máx. **3** móviles). BOOT 10 s o el comando `unpair` los borran.
- Sin emparejar, las escrituras de comandos se rechazan («Insufficient Authentication»).

## Troceado de los eventos

Un mensaje JSON puede ser más largo que una notificación, así que se trocea. Cada notificación lleva un byte de cabecera:

| Bit | Significado |
|---|---|
| 7 (`0x80`) | Último trozo del mensaje |
| 6 (`0x40`) | Primer trozo del mensaje |
| 0–5 | Identificador del mensaje (0–63), igual en todos sus trozos |

Un mensaje de un solo trozo lleva `0xC0 | id`. El receptor junta los trozos con el mismo identificador; si llega uno fuera de secuencia, descarta el mensaje a medias.

## Mensajes del reloj

| `t` | Cuándo | Contenido |
|---|---|---|
| `status` | Tras `get status`, cuando cambia algo (máx. uno cada 300 ms) y cada 30 s | Estado completo: ajustes, hora, ciudad (`wlat`, `wlon`), versión, actualización… |
| `info` | Tras `get info` | `ver`, `mode`, `part`, `name`, `sync` (`last`, `count`), `heap`, `upd` |
| `faces` | Tras `get faces` | Catálogo de esferas |
| `alarms` | Tras `get alarms` | `list` (cada alarma: `h`, `m`, `d` días en bits de lunes a domingo, `on`, `a` animación, `l` etiqueta) y `anims` (nombres de las animaciones) |
| `files` | Tras `get files <ruta>` (p. ej. `get files /Música`; sin ruta, la raíz) | `st` (`ok`, `none` sin tarjeta, `bad` ilegible, `nodir`), `path`, `free` y `total` en bytes, y `list` con `n` nombre, `d` carpeta, `s` tamaño y `m` fecha (epoch) |
| `ack` | Tras `sync done`, `unpair` y `update apply` | `cmd`; en `update`, también `ok` y `msg` |
| `shot` / `shotdata` / `shotend` | Tras `shot` | Captura RGB565 en filas codificadas en base64 |
| `media` | Al tocar los controles de la app Música | `cmd`: `play`, `pause`, `next`, `prev`, `vol+` o `vol-` |
| `call` | Al tocar los botones de una llamada | `cmd`: `reject` o `silence` |
| `ota` | Durante una actualización por Bluetooth | `st`: `ready` (con `win`), `ack` (con `off`), `done` o `err` (con `msg`) |
| `voice` | Al empezar y acabar una orden de voz | `st`: `start` (con `lang`, `es-ES` o `en-US`, y `rate`, 16000), `end` o `cancel` |
| `va` | Mientras se graba, cada 100 ms | Audio: `s` número de mensaje, `p` e `i` estado del codificador (predicción e índice de paso) y `d`, IMA ADPCM de 4 bits (nibble bajo primero) en base64 |

Los comandos son los mismos que en la web: ver [[Arquitectura]].

## Música y llamadas

El móvil manda al reloj lo que pasa, con dos órdenes de texto:

| Orden | Cuándo |
|---|---|
| `music <suena 0/1> <volumen 0-100>\|<título>\|<artista>` | Cambia la canción, se pausa o reanuda, o cambia el volumen |
| `music off` | No hay ninguna sesión de música |
| `call ring\|<quién>` | Entra una llamada: el reloj suena (salvo *No molestar*) y abre la pantalla de la llamada |
| `call end` | La llamada se contesta, se rechaza o se pierde |

El título va hasta 60 bytes y el artista hasta 44, en UTF-8, sin `|` y solo con los caracteres que tienen las fuentes del reloj (ASCII, tildes, ñ, ¡¿ y algunos signos); la app quita los acentos que no tenga. Al volver a conectar, la app repite el estado actual. Las respuestas del reloj son los mensajes `media` y `call` de arriba.

## Órdenes de voz

Mientras se mantiene BOOT, el reloj manda `voice` `start`, unos 10 mensajes `va` por segundo (16 kHz mono de los dos micrófonos, unos 11 KB/s en base64; cada uno lleva el estado del codificador con el que empieza, así que perder uno solo pierde sus 100 ms) y `voice` `end`. La app contesta con una orden:

| Orden | Significa |
|---|---|
| `voice ask <orden>\|<lo oído>\|<lo que hará>` | Ha entendido algo: el reloj pregunta y, si se acepta, ejecuta `<orden>` (p. ej. `alarm add 7 30 31`) |
| `voice fail <lo oído>` | Ha oído palabras pero ninguna orden |
| `voice err <motivo>` | `nada` (no oyó voz), `permiso`, `idioma`, `nomovil` (Android anterior al 13) u otro |

## Actualizar el firmware por Bluetooth

El móvil descarga la imagen (`marea-ota.bin`, del manifiesto) y se la pasa al reloj, que no necesita WiFi. El estado (`status`) lleva `"bleota":1` en los relojes que lo admiten.

1. Orden `ota begin <tamaño> <sha256 en hex> <versión>`. El reloj prepara el hueco libre, enseña la pantalla de actualización y responde `{"t":"ota","st":"ready","win":49152}`, o `err` con el motivo (ya tiene esa versión, tamaño no válido…).
2. El móvil escribe en la característica **Firmware** trozos de `<desplazamiento, 4 bytes little-endian><datos>`, en orden. Con MTU 247 caben 240 bytes de datos.
3. El reloj guarda en la flash y responde `{"t":"ota","st":"ack","off":N}` cada 16 KB. El móvil nunca adelanta más de `win` bytes al último `ack`.
4. Con todo escrito, el reloj comprueba el SHA-256, marca la versión nueva para el siguiente arranque, manda `{"t":"ota","st":"done"}` y se reinicia a los 2 s.

`ota abort` cancela. También cancelan que se corte el enlace, un trozo fuera de orden o 20 s sin datos. En todos esos casos el reloj sigue con su versión. La app lo hace en `ClockUpdate.kt`, y `tools/ble_ota.py` lo hace desde el PC.

## Una sincronización

La app la hace al conectar y cada 30 minutos mientras sigue conectada:

1. Conectar y activar las notificaciones de eventos.
2. `get info` → versión del reloj.
3. `time <epoch_s> <ms>`.
4. `get status` → coordenadas de la ciudad del reloj.
5. Pedir el tiempo a Open-Meteo y enviar `wx …` (si hay internet).
6. `sync done` → esperar `ack`.

Dura unos pocos segundos. La app no desconecta después: el enlace se queda abierto (ver [[App Android]]). Con la app en segundo plano pide el intervalo de conexión de bajo consumo. `tools/fake_phone.py` hace lo mismo desde un PC.

## Herramientas en el PC

Con un adaptador Bluetooth y `pip install bleak pillow`:

```
python tools/ble_console.py          # comandos a mano
python tools/fake_phone.py           # una sincronización como la app
python tools/ble_shot.py --fx 22     # captura de pantalla (PNG)
python tools/ble_ota.py              # actualizar con webflasher/marea-ota.bin
```
