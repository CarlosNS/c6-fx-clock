## Capas del firmware

```
transportes   web.c (HTTP + WebSocket)     ble.c (GATT, NimBLE)     console.c (serie)
                         \                      |                     /
control       control.c   órdenes de texto y JSON de estado
              settings.c  tabla única de ajustes (NVS + orden + JSON con la misma clave)
              ota.c       actualización por trozos, independiente del transporte
              update.c    modo actualización: manifiesto, HTTPS, SHA-256, resultado
              bootmode.c  modos normal / UPDATE / USB en NVS
sistema       sys.c       vistas: esfera, ajustes rápidos, lanzador, app; gestos sobre la esfera
              input.c     gestos del táctil (toque, pulsación larga, arrastre, deslizar desde un borde)
              ui.c        dibujo con el estilo Marea Solsticial (paneles, texto, controles, fondos)
              app_*.c     apps    alarm.c    steps.c    audio.c (ES8311)    phone.c (música y llamadas)
servicios     net.c (WiFi)    clock.c (hora)    weather.c (tiempo)    usbdisk.c (modo USB)
pantalla      main.c (bucle a 30 fps, reposo, botón)    fx_*.c (esferas)    gfx.c    lcd_co5300.c
placa         board.c    pmu_axp2101.c    rtc_pcf85063.c    touch_ft3168.c    imu_qmi8658.c    i2c_bus.c
```

La idea central: **todo se controla con mensajes de texto**. Un transporte solo tiene que pasar textos a `app_command()` y devolver `app_status_json()`. Por eso la web, el Bluetooth y la consola serie hacen exactamente lo mismo.

## El bucle principal (`main.c`)

En cada vuelta: lee el táctil y lo convierte en gestos (`input_poll` → `sys_event`), atiende el botón BOOT, alimenta el podómetro con el acelerómetro y, con la pantalla apagada, vigila el gesto de levantar la muñeca. Cada minuto revisa las alarmas y cierra los pasos del día; cada 3 s, la batería y el enlace con el móvil.

Luego dibuja: si la vista es la esfera, la esfera en su lienzo; si no, la vista del sistema en el lienzo nativo, solo cuando ha cambiado (una vista quieta no se vuelve a dibujar ni a enviar). Con la pantalla apagada no se dibuja nada y el panel queda en reposo.

Un **vigilante** reinicia el reloj si el bucle pasa 8 s parado, y al arrancar dice en qué paso estaba (`hang.h`).

## La pantalla y los lienzos

| Lienzo | Tamaño | Al enviarlo | Lo usan |
|---|---|---|---|
| Nativo | 410×502 | tal cual | el sistema, las apps y Plasma (`FX_NATIVE`) |
| Ancho | 274×336 | ×1,5 con suavizado, recortado a 410×502 | las demás esferas (`FX_WIDE`) |
| Antiguo | 172×320 | ×1,5 en un recuadro | ninguna esfera (`FX_COMPAT`, para compatibilidad) |

Escribir la PSRAM es lo que más cuesta, así que el lienzo ancho, con la mitad de píxeles, deja a las esferas animadas llegar a 30 fps. La ampliación la hace la tarea de envío, por franjas, en RAM interna. Las capturas (`GET /screenshot`, `shot` por Bluetooth) devuelven el lienzo del último fotograma.

## El sistema (`sys.c`)

- **Vistas:** esfera, ajustes rápidos, lanzador y app activa. Cada app (`app_t` en `apps.h`) tiene `open`, `draw`, `event` y `close`.
- **Avisos:** una alarma o una llamada abren su pantalla por encima de lo que hubiera (`sys_alert`) y la cierran al acabar.
- **Gestos sobre la esfera:** deslizar desde arriba o abajo abre los ajustes rápidos o el lanzador, a los lados cambia de esfera, mantener abre el selector. En las esferas interactivas el dedo va a la esfera, salvo los gestos que empiezan en un borde.
- **Estilo:** `ui.c` dibuja con la paleta *Marea Solsticial* (`MS_*`), las fuentes Jersey 10 y VT323 en mapas de bits (`ui_fonts.c`, de `tools/make_fonts.py`), iconos de 16×16 (`ui_icons.c`) y los fondos de pixel art de noche y de atardecer.
- **Idiomas:** cada texto lleva sus dos versiones con `TR("español", "english")`, según el ajuste `lang`.

## Órdenes

Una por mensaje (lista completa en `src/control.h`):

| Orden | Qué hace |
|---|---|
| `<ajuste> <número>` | Cambia un ajuste (tabla de abajo), p. ej. `bri 80`, `fx 3` |
| `night <on> <desde> <hasta> <brillo>` | Modo noche de una vez |
| `time <epoch_s> <ms>` | Pone la hora (la manda el móvil) |
| `loc <lat> <lon> <nombre>` | Ciudad del tiempo |
| `wx <t10> <código> <hum> <viento> <máx10> <mín10> <es_de_día>` | Tiempo enviado por el móvil (temperaturas en décimas) |
| `wifi` + `\n<ssid>\n<contraseña>` | Guarda y usa una red WiFi |
| `music <suena 0/1> <volumen>\|<título>\|<artista>`, `music off` | Lo que suena en el móvil |
| `call ring\|<quién>`, `call end` | Llamada entrante y su final |
| `ui home\|quick\|launcher\|app N\|tap X Y\|swipe dir [borde]\|long X Y` | Maneja el sistema a distancia (tests y capturas) |
| `drag <x> <y> <dx> <dy>` / `up` | Panel táctil de las esferas interactivas |
| `dbgtime <h> <m>` | Previsualiza otra hora del día (`-1 0` = hora real) |
| `mode ble\|update\|usb` | Reinicia en ese modo |
| `update apply <url del manifiesto>` | Actualiza por internet |
| `unpair` | Olvida todos los móviles emparejados |

Solo por Bluetooth: `get info`, `get status`, `get faces`, `shot` (captura de pantalla) y `sync done` (fin de una sincronización). Ver [[Protocolo Bluetooth]].

## Ajustes (`settings.c`)

| Clave | Rango | Significado |
|---|---|---|
| `fx` | 0–24 | Esfera |
| `auto` | 0/1 | Cambio automático cada 30 s |
| `fxoff` | 0–2³¹−1 | Esferas que el cambio automático se salta: máscara de bits, el bit *n* es la esfera *n* (máximo 31 esferas) |
| `spd` | 10–300 | Velocidad de las animaciones (%) |
| `bri` | 5–100 | Brillo (%) |
| `h24` | 0/1 | Formato 24 h |
| `secs` | 0/1 | Mostrar segundos |
| `non` / `nst` / `nen` / `nbr` | 0/1, 0–23, 0–23, 1–100 | Modo noche: activo, hora de inicio, hora de fin, brillo |
| `aon` | 0/1 | Pantalla siempre encendida |
| `wks` | 3–600 | Segundos que se queda encendida |
| `raise` | 0/1 | Encender al levantar la muñeca |
| `lang` | 0/1 | Idioma: español o inglés |
| `dnd` | 0/1 | No molestar: alarmas y llamadas sin sonido |
| `vol` | 0–100 | Volumen del altavoz |
| `goal` | 1000–50000 | Objetivo diario de pasos |
| `chue` | 0–359 | Color de la esfera Clásico (tono) |
| `ohue` | 0–359 | Color del acento de la esfera Ónix (tono) |
| `ovl` | 0/1 | Información de depuración en pantalla |

Añadir un ajuste es **una línea** en la tabla de `settings.c`: se guarda en NVS, se controla por orden y aparece en el JSON de estado sin tocar nada más.

## Esferas

Cada esfera es un `src/fx_*.c` con una función que dibuja un fotograma completo, y una entrada en `fx_list` (`src/fx_common.c`) con su nombre, grupo, lienzo, descripción, si es interactiva y sus ajustes propios. La app y la web construyen su menú a partir de ese catálogo (`get faces` o `GET /faces`), así que una esfera nueva aparece sola. Ver [[Crear una esfera]].

## Modos de arranque (`bootmode.c`)

- **Normal** (`ble` o `wifi`, que significan lo mismo): WiFi y Bluetooth a la vez.
- **UPDATE:** una sola vez, para descargar una actualización.
- **USB:** una sola vez, la microSD como disco USB (`usbdisk.c`); el puerto USB pasa a TinyUSB y la consola serie desaparece hasta el siguiente arranque.

En los dos de una sola vez, al entrar, el modo guardado ya es el de vuelta, así que un fallo nunca deja el reloj atrapado.

## La app Android

Kotlin, con la misma página de control (`src/index.html` y `src/transport.js`, copiados al compilar) dentro de un WebView. Kotlin se ocupa de lo que una página no puede:

- emparejar con el Companion Device Manager;
- el enlace Bluetooth (`ClockBle`), que mantiene abierto un servicio en primer plano (`LinkService`) mientras el reloj está cerca;
- detectar el reloj cerca (`ClockPresence`), que arranca ese servicio;
- la música y las llamadas (`PhoneWatch`, un lector de notificaciones, y `Phone`, que habla con el reloj);
- la sincronización periódica de reserva con WorkManager (`SyncWorker`) y actualizarse (`AppUpdate`).

La página habla con Kotlin por `window.ClockBridge`.
