# Cambios

## 3.0.0 — preparada, sin publicar

Se publicará con `tools/release.py` cuando la app esté probada en un móvil real (`tests/MANUAL.md`). Antes de publicarla:

- `PROJECT_VER` a 3.0.0 en `CMakeLists.txt` y `versionName` a 1.0.0 en `android/app/build.gradle.kts`.
- `BOOT_DEFAULT` a `BOOT_BLE` en `src/bootmode.h`. Solo afecta a las instalaciones nuevas, porque los relojes ya instalados conservan su modo.

Notas para la release:

> **El reloj se controla desde el móvil.** Nueva app Android (*C6 Clock*): cambia de esfera y ajustes por Bluetooth, sin WiFi, y cada 30 minutos le pone la hora y el tiempo aunque la app esté cerrada. Se empareja una vez con la clave que aparece en la pantalla del reloj.
>
> - Las actualizaciones llegan por internet: *Actualizaciones → Buscar actualización*, en la app o en la página. Si algo falla, el reloj se queda con la versión que tenía.
> - El modo WiFi sigue disponible: BOOT 2 s en la pantalla de bienvenida.
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
