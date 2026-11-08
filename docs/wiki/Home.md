![Algunas esferas del reloj](https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/esferas.png)

**Marea** es un reloj de pulsera con **21 esferas animadas a pantalla completa** y apps propias, para la placa **Waveshare ESP32-S3-Touch-AMOLED-2.06** (AMOLED 410×502 táctil, batería, reloj de hardware, acelerómetro, altavoz y microSD). Se queda conectado al móvil por Bluetooth mientras está cerca, recibe la hora, el tiempo, la música y las llamadas, se controla también desde el navegador por WiFi y se actualiza solo por internet.

## Empieza aquí

| Quiero… | Página |
|---|---|
| Instalarlo en una placa nueva | [[Instalación]] |
| Saber cómo se maneja: gestos, botón, apps | [[Uso del reloj]] |
| Usar la app del móvil | [[App Android]] |
| Controlarlo desde el navegador | [[Página de control]] |
| Ver todas las esferas | [[Esferas]] |
| Actualizarlo | [[Actualizaciones]] |
| Arreglar algo que no va | [[Solución de problemas]] |

## Para desarrolladores

| | |
|---|---|
| [[Hardware]] | La placa, sus pines, la pantalla, la memoria y las particiones |
| [[Arquitectura]] | Módulos del firmware, sistema y apps, lienzos, órdenes, ajustes y modos de arranque |
| [[Protocolo Bluetooth]] | Servicio GATT, troceado de mensajes, música, llamadas y seguridad |
| [[Crear una esfera]] | Haz tu propia esfera: plantilla, herramientas de dibujo, rendimiento y cómo enviarla |
| [[Compilar y contribuir]] | Firmware, app, tests y publicar versiones |
| [[Licencia]] | Software libre con la GPL-3.0: qué puedes hacer y qué debes cumplir |

## En pocas palabras

- **Un reloj de verdad:** gestos con el dedo, lanzador de apps, ajustes rápidos, alarmas, podómetro, cronómetro, música y llamadas del móvil, linterna y archivos de la microSD, con el estilo *Marea Solsticial*.
- **WiFi y Bluetooth a la vez.** La app se queda conectada mientras el reloj está cerca y vuelve a conectar sola.
- **La hora** la guarda su reloj de hardware aunque se apague; el móvil o internet la corrigen.
- **Actualizaciones por internet**, con comprobación de SHA-256 y vuelta atrás automática si la versión nueva falla.
- **Todo dibujado por código:** sin imágenes con derechos de terceros; las fuentes del sistema son libres (OFL).

El proyecto empezó como un reloj de sobremesa con un ESP32-C6, que ya no se mantiene (última versión: 2.1.0).

Código: <https://github.com/CarlosNS/c6-fx-clock> · Descargas: [releases](https://github.com/CarlosNS/c6-fx-clock/releases)
