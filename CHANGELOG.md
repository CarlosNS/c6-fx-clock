# Cambios

## Sin publicar (rama `s3`)

> **Esferas retiradas: El grito, Acuario y Cuento.** Quedan 21 esferas. Sus números (10, 16 y 20) no se reutilizan; si un reloj tenía elegida una de ellas, pasa a la siguiente.

- Placa ESP32-S3-Touch-AMOLED-2.06 (en desarrollo, ver `docs/PLAN-s3.md`): el firmware completo funciona en modo compatibilidad, y la hora se guarda en su reloj de hardware (PCF85063), que la conserva con el reloj apagado. En esta placa desaparece la espera de 60 s del Bluetooth al arrancar. Su pantalla táctil enciende la pantalla, cambia de esfera al deslizar el dedo y mueve las esferas interactivas. Usa el WiFi y el Bluetooth a la vez, sin modos que cambiar.
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
