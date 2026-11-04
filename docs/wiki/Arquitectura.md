## Capas del firmware

```
transportes   web.c (HTTP + WebSocket)     ble.c (GATT, NimBLE)     console.c (serie)
                         \                      |                     /
control       control.c   comandos de texto y JSON de estado
              settings.c  tabla única de ajustes (NVS + comando + JSON con la misma clave)
              ota.c       actualización por trozos, independiente del transporte
              update.c    modo actualización: manifiesto, HTTPS, SHA-256, resultado
              bootmode.c  modos BLE / WIFI / UPDATE en NVS
servicios     net.c (WiFi)    clock.c (hora)    weather.c (tiempo)
pantalla      main.c (bucle a 30 fps, reposo, botón)    fx_*.c (esferas)    gfx.c / lcd.c
```

La idea central: **todo se controla con mensajes de texto**. Un transporte solo tiene que pasar textos a `app_command()` y devolver `app_status_json()`. Por eso la web, el Bluetooth y la consola serie hacen exactamente lo mismo.

## Comandos

Uno por mensaje (lista completa en `src/control.h`):

| Comando | Qué hace |
|---|---|
| `<ajuste> <número>` | Cambia un ajuste (tabla de abajo), p. ej. `bri 80`, `fx 3` |
| `night <on> <desde> <hasta> <brillo>` | Modo noche de una vez |
| `time <epoch_s> <ms>` | Pone la hora (la manda el móvil) |
| `loc <lat> <lon> <nombre>` | Ciudad del tiempo |
| `wx <t10> <código> <hum> <viento> <máx10> <mín10> <es_de_día>` | Tiempo enviado por el móvil (temperaturas en décimas) |
| `wifi` + `\n<ssid>\n<contraseña>` | Guarda y usa una red WiFi |
| `drag <x> <y> <dx> <dy>` / `up` | Panel táctil de las esferas interactivas |
| `dbgtime <h> <m>` | Previsualiza otra hora del día (`-1` = hora real) |
| `mode ble\|wifi\|update` | Reinicia en ese modo |
| `update apply <url del manifiesto>` | Actualiza por internet |
| `unpair` | Olvida todos los móviles emparejados |

Solo por Bluetooth: `get info`, `get status`, `get faces`, `shot` (captura de pantalla) y `sync done` (fin de una sincronización). Ver [[Protocolo Bluetooth]].

## Ajustes (`settings.c`)

| Clave | Rango | Significado |
|---|---|---|
| `fx` | 0–22 | Esfera |
| `auto` | 0/1 | Cambio automático cada 30 s |
| `fxoff` | 0–2³¹−1 | Esferas que el cambio automático se salta: máscara de bits, el bit *n* es la esfera *n* (máximo 31 esferas) |
| `spd` | 10–300 | Velocidad de las animaciones (%) |
| `bri` | 5–100 | Brillo (%) |
| `h24` | 0/1 | Formato 24 h |
| `secs` | 0/1 | Mostrar segundos |
| `non` / `nst` / `nen` / `nbr` | 0/1, 0–23, 0–23, 1–100 | Modo noche: activo, hora de inicio, hora de fin, brillo |
| `aon` | 0/1 | Pantalla siempre encendida |
| `wks` | 3–600 | Segundos que se queda encendida |
| `chue` | 0–359 | Color de la esfera Clásico (tono) |

Añadir un ajuste es **una línea** en la tabla de `settings.c`: se guarda en NVS, se controla por comando y aparece en el JSON de estado sin tocar nada más.

## Esferas

Cada esfera es un `src/fx_*.c` con una función que dibuja un fotograma en el framebuffer RGB565, y una entrada en `fx_list` (`src/fx_common.c`) con su nombre, grupo, orientación, descripción, si es interactiva y sus ajustes propios. La app y la web construyen su menú a partir de ese catálogo (`get faces` o `GET /faces`), así que una esfera nueva aparece sola.

Trucos de rendimiento (el C6 no tiene FPU y divide lento): mezcla de color RGB565 con una sola multiplicación, tablas precalculadas, aritmética de punto fijo y fuentes con cobertura y contorno en 4 bits por píxel (`tools/gen_fonts.py`, `tools/gen_uifont.py`).

## Modos de arranque (`bootmode.c`)

- **BLE:** uso diario.
- **WIFI:** página web.
- **UPDATE:** una sola vez; al entrar, el modo guardado ya es el de vuelta, así que un fallo nunca deja el reloj atrapado.

Las instalaciones nuevas arrancan en `BOOT_DEFAULT` (`bootmode.h`).

## La app Android

Kotlin, con la misma página de control (`src/index.html` y `src/transport.js`, copiados al compilar) dentro de un WebView. Kotlin se ocupa de lo que una página no puede: emparejar con el Companion Device Manager, el enlace Bluetooth (`ClockBle`), la sincronización periódica con WorkManager (`SyncWorker`), detectar el reloj cerca (`ClockPresence`) y actualizarse (`AppUpdate`). La página habla con Kotlin por `window.ClockBridge`.
