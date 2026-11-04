![Algunas esferas del reloj](https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/esferas.png)

**C6 FX Clock** es un reloj de escritorio con **23 esferas animadas** a 30 fps para la placa **Waveshare ESP32-C6-LCD-1.47**. Se controla desde el móvil con una app Android por Bluetooth, o desde cualquier navegador por WiFi, recibe la hora y el tiempo del móvil y se actualiza solo por internet.

## Empieza aquí

| Quiero… | Página |
|---|---|
| Instalarlo en una placa nueva | [[Instalación]] |
| Saber qué hace el botón y los modos | [[Uso del reloj]] |
| Usar la app del móvil | [[App Android]] |
| Controlarlo desde el navegador | [[Página de control]] |
| Ver todas las esferas | [[Esferas]] |
| Actualizarlo | [[Actualizaciones]] |
| Arreglar algo que no va | [[Solución de problemas]] |

## Para desarrolladores

| | |
|---|---|
| [[Hardware]] | La placa actual y la definitiva (ESP32-S3 AMOLED) |
| [[Arquitectura]] | Módulos del firmware, comandos, ajustes y modos de arranque |
| [[Protocolo Bluetooth]] | Servicio GATT, troceado de mensajes y seguridad |
| [[Crear una esfera]] | Haz tu propia esfera: plantilla, herramientas de dibujo, rendimiento y cómo enviarla |
| [[Compilar y contribuir]] | Firmware, app, tests y publicar versiones |
| [[Licencia]] | Software libre con la GPL-3.0: qué puedes hacer y qué debes cumplir |

## En pocas palabras

- **Dos modos:** Bluetooth para el día a día con la app, y WiFi con la página web de siempre. En esta placa no caben los dos a la vez en la RAM, así que se cambia con un reinicio.
- **La hora y el tiempo** los pone el móvil cada 30 minutos, aunque la app esté cerrada, y también en cuanto el reloj se enciende o vuelve a estar cerca.
- **Actualizaciones por internet**, con comprobación de SHA-256 y vuelta atrás automática si la versión nueva falla.
- **Todo dibujado por código:** sin imágenes ni fuentes con derechos de terceros.

Código: <https://github.com/CarlosNS/c6-fx-clock> · Descargas: [releases](https://github.com/CarlosNS/c6-fx-clock/releases)
