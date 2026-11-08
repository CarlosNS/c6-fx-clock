# Cambios

## Sin publicar: Marea (3.0.0)

> **Marea: el reloj pasa a la Waveshare ESP32-S3-Touch-AMOLED-2.06.** Pantalla AMOLED táctil de 410×502, batería, reloj de hardware, acelerómetro, altavoz y microSD. El ESP32-C6 deja de mantenerse (su última versión es la 2.1.0).

- **Sistema nuevo** con el estilo *Marea Solsticial*: lanzador de apps (deslizar desde abajo), ajustes rápidos (desde arriba), selector de esferas (mantener el dedo) y volver deslizando desde la izquierda o con BOOT. Textos en español o inglés.
- **Apps:** Esferas, Alarmas (por el altavoz; se posponen al agitar la muñeca), Pasos (podómetro con objetivo diario), Cronómetro, Música, Linterna, Archivos y Ajustes. Llamadas entrantes en la muñeca: quién llama, rechazar o silenciar.
- **Todas las esferas a pantalla completa**, siguiendo las esquinas redondeadas y a 30 fps. Las que eran horizontales (Vida, HUD, Aventura, Estrellada, WiFi, Earthrise, Sakura, Synthwave, Agujero negro y Gol) pasan a verticales. Clásico usa la pantalla como caja; Feria, RPG, Túnel y Agujero negro cambian su composición.
- **App 0.4.0: conexión permanente.** Mientras el reloj está cerca, la app se queda conectada y vuelve a conectar sola; sincroniza la hora y el tiempo al conectar y cada 30 minutos. Con permiso para leer notificaciones, el reloj muestra y controla la música del móvil y avisa de las llamadas.
- **Modo USB:** el botón USB de Archivos reinicia el reloj como disco USB con la microSD; se vuelve con BOOT, expulsando el disco o desconectando el cable.
- **Esfera nueva: Ónix** (número 24). Analógica minimalista para AMOLED: negro puro, índices finos, agujas cálidas y una estela de luz como segundero. El color del acento se elige en la app (ajuste `ohue`) y el dibujo se desplaza un poco cada minuto para cuidar la pantalla.
- La barra de estado muestra el porcentaje de batería, y un rayo sobre el icono mientras está enchufado; los ajustes rápidos dicen «cargando» o «cargada».
- El tiempo que manda el móvil siempre gana sobre la descarga propia del reloj (es el más reciente).
- La hora se guarda en el reloj de hardware (PCF85063) y se conserva con el reloj apagado. WiFi y Bluetooth funcionan a la vez, sin modos que cambiar. La pantalla se enciende al levantar la muñeca.
- Red del punto de acceso: **Marea** (clave `mareamarea`); Bluetooth: **Marea XXXX**.
- Para crear esferas: `tools/new_face.py` hace esferas a pantalla completa (lienzo ancho o `--native`).

### Detalles de la etapa de paso a la S3

> **Esferas retiradas: El grito, Acuario y Cuento.** Quedan 21 esferas. Sus números (10, 16 y 20) no se reutilizan; si un reloj tenía elegida una de ellas, pasa a la siguiente.

- Placa ESP32-S3-Touch-AMOLED-2.06 (en desarrollo, ver `docs/PLAN-s3.md`): el firmware completo funciona en modo compatibilidad, y la hora se guarda en su reloj de hardware (PCF85063), que la conserva con el reloj apagado. En esta placa desaparece la espera de 60 s del Bluetooth al arrancar. Su pantalla táctil enciende la pantalla, cambia de esfera al deslizar el dedo y mueve las esferas interactivas. Usa el WiFi y el Bluetooth a la vez, sin modos que cambiar. La página de control muestra su batería; la pantalla se enciende al levantar la muñeca (se puede desactivar) y el panel se pone en reposo cuando está apagada.
- Actualizaciones por placa: el manifiesto conserva las claves del C6 y añade `boards.s3amoled`; el instalador web elige la imagen según el chip; la página y la app ofrecen la versión de la placa del reloj. El estado y el mensaje `info` dicen la placa (`board`).
- El catálogo de esferas (`/faces`, `get faces`) sigue teniendo una entrada por número; las retiradas llevan `"x":1` y grupo `-1`, y ninguna versión de la app ni de la página las muestra.

## 3.0.0 — preparada, sin publicar

Se publicará con `tools/release.py` cuando la app esté probada en un móvil real (`tests/MANUAL.md`). Antes de publicarla:

- `PROJECT_VER` a 3.0.0 en `CMakeLists.txt` y `versionName` a 1.0.0 en `android/app/build.gradle.kts`.
- `versionCode` de la app mayor que el publicado (la app solo se actualiza a un `versionCode` superior; `release.py` lo escribe en el bloque `app` del manifiesto).
- `BOOT_DEFAULT` a `BOOT_BLE` en `src/bootmode.h`. Solo afecta a las instalaciones nuevas, porque los relojes ya instalados conservan su modo.

Notas para la release:

> **Esferas que no salen solas.** En la app, *Esferas del cambio automático → Elegir* permite quitar esferas del cambio automático; siguen disponibles a mano. Ajuste `fxoff`.
>
> **Esfera nueva: Feria de Sevilla.** Cuenta atrás hasta el alumbrado de la Feria de Abril, con fuegos artificiales durante la Feria. La fecha se calcula sola cada año.

> **El reloj se controla desde el móvil.** Nueva app Android (*C6 Clock*): cambia de esfera y ajustes por Bluetooth, sin WiFi, y cada 30 minutos le pone la hora y el tiempo aunque la app esté cerrada. Se empareja una vez con la clave que aparece en la pantalla del reloj.
>
> - Al encender el reloj o al volver a estar cerca, el móvil le pone la hora en menos de un minuto, sin esperar a la siguiente sincronización (Android 12 o superior).
> - Al emparejar, una barra sobre la clave cuenta los 30 segundos que hay para escribirla.
> - La app avisa también de sus propias versiones nuevas y se actualiza sola (*Actualizaciones → Actualizar la app*), sin desinstalar ni perder el emparejamiento.
> - Las actualizaciones llegan por internet: *Actualizaciones → Buscar actualización*, en la app o en la página. Si algo falla, el reloj se queda con la versión que tenía.
> - El modo WiFi sigue disponible: BOOT 2 s en la pantalla de bienvenida.
> - ¿Quieres hacer tu propia esfera? `tools/new_face.py` crea una que ya funciona, y la wiki tiene la guía completa: [Crear una esfera](https://github.com/CarlosNS/c6-fx-clock/wiki/Crear-una-esfera).
> - Instalación por USB: <https://carlosns.github.io/c6-fx-clock/flasher/>

## 2.1.0 — 2026-11-03

- **Actualización por internet:** el reloj descarga el firmware de GitHub Pages y comprueba proyecto, tamaño y SHA-256. Funciona en modo WiFi y en modo Bluetooth (la orden llega desde la página o la app).
- **App Android** (primera versión, 0.1.0): la página de control dentro de la app, emparejamiento con el sistema y sincronización periódica.
- **Web Bluetooth:** la página de control funciona también desde Chrome o Edge por Bluetooth.
- **WiFi en redes malladas:** si un nodo no da IP, el reloj prueba con el siguiente.
- La red WiFi se puede guardar también en modo Bluetooth.

## 2.0.0

- Nueva tabla de particiones de 8 MB con dos huecos para OTA y vuelta atrás automática.
- Actualización por WiFi subiendo el archivo desde la web.
- Capa de control independiente del transporte (`control.c`, `settings.c`), preparada para el Bluetooth.
