## Capas del firmware

```
transportes   web.c (HTTP + WebSocket)     ble.c (GATT, NimBLE)     console.c (serie)
                         \                      |                     /
control       control.c   Ã³rdenes de texto y JSON de estado
              settings.c  tabla Ãºnica de ajustes (NVS + orden + JSON con la misma clave)
              ota.c       actualizaciÃ³n por trozos, independiente del transporte
              update.c    modo actualizaciÃ³n: manifiesto, HTTPS, SHA-256, resultado
              bootmode.c  modos normal / UPDATE / USB en NVS
sistema       sys.c       vistas: esfera, ajustes rÃ¡pidos, lanzador, app; gestos sobre la esfera
              input.c     gestos del tÃ¡ctil (toque, pulsaciÃ³n larga, arrastre, deslizar desde un borde)
              ui.c        dibujo con el estilo Marea Solsticial (paneles, texto, controles, fondos)
              app_*.c     apps    alarm.c    steps.c    audio.c (ES8311)    phone.c (mÃºsica y llamadas)
servicios     net.c (WiFi)    clock.c (hora)    weather.c (tiempo)    usbdisk.c (modo USB)
pantalla      main.c (bucle a 30 fps, reposo, botÃ³n)    fx_*.c (esferas)    gfx.c    lcd_co5300.c
placa         board.c    pmu_axp2101.c    rtc_pcf85063.c    touch_ft3168.c    imu_qmi8658.c    i2c_bus.c
```

La idea central: **todo se controla con mensajes de texto**. Un transporte solo tiene que pasar textos a `app_command()` y devolver `app_status_json()`. Por eso la web, el Bluetooth y la consola serie hacen exactamente lo mismo.

## El bucle principal (`main.c`)

En cada vuelta: lee el tÃ¡ctil y lo convierte en gestos (`input_poll` â†’ `sys_event`), atiende el botÃ³n BOOT, alimenta el podÃ³metro con el acelerÃ³metro y, con la pantalla apagada, vigila el gesto de levantar la muÃ±eca. Cada minuto revisa las alarmas y cierra los pasos del dÃ­a; cada 3 s, la baterÃ­a y el enlace con el mÃ³vil.

Luego dibuja: si la vista es la esfera, la esfera en su lienzo; si no, la vista del sistema en el lienzo nativo, solo cuando ha cambiado (una vista quieta no se vuelve a dibujar ni a enviar). Con la pantalla apagada no se dibuja nada y el panel queda en reposo.

Un **vigilante** reinicia el reloj si el bucle pasa 8 s parado, y al arrancar dice en quÃ© paso estaba (`hang.h`).

## La pantalla y los lienzos

| Lienzo | TamaÃ±o | Al enviarlo | Lo usan |
|---|---|---|---|
| Nativo | 410Ã—502 | tal cual | el sistema, las apps y Plasma (`FX_NATIVE`) |
| Ancho | 274Ã—336 | Ã—1,5 con suavizado, recortado a 410Ã—502 | las demÃ¡s esferas (`FX_WIDE`) |
| Antiguo | 172Ã—320 | Ã—1,5 en un recuadro | ninguna esfera (`FX_COMPAT`, para compatibilidad) |

Escribir la PSRAM es lo que mÃ¡s cuesta, asÃ­ que el lienzo ancho, con la mitad de pÃ­xeles, deja a las esferas animadas llegar a 30 fps. La ampliaciÃ³n la hace la tarea de envÃ­o, por franjas, en RAM interna. Las capturas (`GET /screenshot`, `shot` por Bluetooth) devuelven el lienzo del Ãºltimo fotograma.

## El sistema (`sys.c`)

- **Vistas:** esfera, ajustes rÃ¡pidos, lanzador y app activa. Cada app (`app_t` en `apps.h`) tiene `open`, `draw`, `event` y `close`.
- **Avisos:** una alarma o una llamada abren su pantalla por encima de lo que hubiera (`sys_alert`) y la cierran al acabar.
- **Gestos sobre la esfera:** deslizar desde arriba o abajo abre los ajustes rÃ¡pidos o el lanzador, a los lados cambia de esfera, mantener abre el selector. En las esferas interactivas el dedo va a la esfera, salvo los gestos que empiezan en un borde.
- **Estilo:** `ui.c` dibuja con la paleta *Marea Solsticial* (`MS_*`), las fuentes Jersey 10 y VT323 en mapas de bits (`ui_fonts.c`, de `tools/make_fonts.py`), iconos de 16Ã—16 (`ui_icons.c`) y los fondos de pixel art de noche y de atardecer.
- **Idiomas:** cada texto lleva sus dos versiones con `TR("espaÃ±ol", "english")`, segÃºn el ajuste `lang`.

## Ã“rdenes

Una por mensaje (lista completa en `src/control.h`):

| Orden | QuÃ© hace |
|---|---|
| `<ajuste> <nÃºmero>` | Cambia un ajuste (tabla de abajo), p. ej. `bri 80`, `fx 3` |
| `night <on> <desde> <hasta> <brillo>` | Modo noche de una vez |
| `time <epoch_s> <ms>` | Pone la hora (la manda el mÃ³vil) |
| `loc <lat> <lon> <nombre>` | Ciudad del tiempo |
| `wx <t10> <cÃ³digo> <hum> <viento> <mÃ¡x10> <mÃ­n10> <es_de_dÃ­a>` | Tiempo enviado por el mÃ³vil (temperaturas en dÃ©cimas) |
| `wifi` + `\n<ssid>\n<contraseÃ±a>` | Guarda y usa una red WiFi |
| `music <suena 0/1> <volumen>\|<tÃ­tulo>\|<artista>`, `music off` | Lo que suena en el mÃ³vil |
| `call ring\|<quiÃ©n>`, `call end` | Llamada entrante y su final |
| `ui home\|quick\|launcher\|app N|ring|tap X Y\|tap X Y\|swipe dir [borde]\|long X Y` | Maneja el sistema a distancia (tests y capturas) |
| `drag <x> <y> <dx> <dy>` / `up` | Panel tÃ¡ctil de las esferas interactivas |
| `dbgtime <h> <m>` | Previsualiza otra hora del dÃ­a (`-1 0` = hora real) |
| `mode ble\|update\|usb` | Reinicia en ese modo |
| `update apply <url del manifiesto>` | Actualiza por internet |
| `ota begin <tamaño> <sha256> <versión>` / `ota abort` | Actualización por Bluetooth desde el móvil (solo Bluetooth; ver [[Protocolo Bluetooth]]) |
| `unpair` | Olvida todos los mÃ³viles emparejados |

Solo por Bluetooth: `get info`, `get status`, `get faces`, `shot` (captura de pantalla) y `sync done` (fin de una sincronizaciÃ³n). Ver [[Protocolo Bluetooth]].

## Ajustes (`settings.c`)

| Clave | Rango | Significado |
|---|---|---|
| `fx` | 0â€“24 | Esfera |
| `auto` | 0/1 | Cambio automÃ¡tico cada 30 s |
| `fxoff` | 0â€“2Â³Â¹âˆ’1 | Esferas que el cambio automÃ¡tico se salta: mÃ¡scara de bits, el bit *n* es la esfera *n* (mÃ¡ximo 31 esferas) |
| `spd` | 10â€“300 | Velocidad de las animaciones (%) |
| `bri` | 5â€“100 | Brillo (%) |
| `h24` | 0/1 | Formato 24 h |
| `secs` | 0/1 | Mostrar segundos |
| `non` / `nst` / `nen` / `nbr` | 0/1, 0â€“23, 0â€“23, 1â€“100 | Modo noche: activo, hora de inicio, hora de fin, brillo |
| `aon` | 0/1 | Pantalla siempre encendida |
| `wks` | 3â€“600 | Segundos que se queda encendida |
| `raise` | 0/1 | Encender al levantar la muÃ±eca |
| `lang` | 0/1 | Idioma: espaÃ±ol o inglÃ©s |
| `dnd` | 0/1 | No molestar: alarmas y llamadas sin sonido |
| `vol` | 0â€“100 | Volumen del altavoz |
| `goal` | 1000â€“50000 | Objetivo diario de pasos |
| `chue` | 0â€“359 | Color de la esfera ClÃ¡sico (tono) |
| `ohue` | 0â€“359 | Color del acento de la esfera Ã“nix (tono) |
| `ovl` | 0/1 | InformaciÃ³n de depuraciÃ³n en pantalla |

AÃ±adir un ajuste es **una lÃ­nea** en la tabla de `settings.c`: se guarda en NVS, se controla por orden y aparece en el JSON de estado sin tocar nada mÃ¡s.

## Esferas

Cada esfera es un `src/fx_*.c` con una funciÃ³n que dibuja un fotograma completo, y una entrada en `fx_list` (`src/fx_common.c`) con su nombre, grupo, lienzo, descripciÃ³n, si es interactiva y sus ajustes propios. La app y la web construyen su menÃº a partir de ese catÃ¡logo (`get faces` o `GET /faces`), asÃ­ que una esfera nueva aparece sola. Ver [[Crear una esfera]].

## Modos de arranque (`bootmode.c`)

- **Normal** (`ble` o `wifi`, que significan lo mismo): WiFi y Bluetooth a la vez.
- **UPDATE:** una sola vez, para descargar una actualizaciÃ³n.
- **USB:** una sola vez, la microSD como disco USB (`usbdisk.c`); el puerto USB pasa a TinyUSB y la consola serie desaparece hasta el siguiente arranque.

En los dos de una sola vez, al entrar, el modo guardado ya es el de vuelta, asÃ­ que un fallo nunca deja el reloj atrapado.

## La app Android

Kotlin, con la misma pÃ¡gina de control (`src/index.html` y `src/transport.js`, copiados al compilar) dentro de un WebView. Kotlin se ocupa de lo que una pÃ¡gina no puede:

- emparejar con el Companion Device Manager;
- el enlace Bluetooth (`ClockBle`), que mantiene abierto un servicio en primer plano (`LinkService`) mientras el reloj estÃ¡ cerca;
- detectar el reloj cerca (`ClockPresence`), que arranca ese servicio;
- la mÃºsica y las llamadas (`PhoneWatch`, un lector de notificaciones, y `Phone`, que habla con el reloj);
- la sincronizaciÃ³n periÃ³dica de reserva con WorkManager (`SyncWorker`) y actualizarse (`AppUpdate`).

La pÃ¡gina habla con Kotlin por `window.ClockBridge`.
