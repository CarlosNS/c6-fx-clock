# Avisos de terceros

El código de este repositorio es de CarlosNS y se distribuye con la licencia [GPL-3.0-or-later](LICENSE). Todas las licencias de abajo son compatibles con ella. Los firmwares y la app que se publican incluyen o usan además estos componentes de terceros, cada uno con su propia licencia, que **no cambia** por estar aquí:

## Firmware del reloj

| Componente | Licencia | Uso |
|---|---|---|
| [ESP-IDF](https://github.com/espressif/esp-idf) (Espressif), incluidos FreeRTOS, lwIP, NimBLE, mbedTLS y el controlador WiFi/Bluetooth | Apache 2.0 (y MIT/BSD en algunos componentes) | Sistema base, red, Bluetooth, HTTPS |
| [esp_tinyusb](https://components.espressif.com/components/espressif/esp_tinyusb) (Espressif) y [TinyUSB](https://github.com/hathach/tinyusb) (Ha Thach) | Apache 2.0 y MIT | Modo USB: la microSD como disco (`src/usbdisk.c`) |
| Controladores [es8311 y es7210](https://github.com/espressif/esp-bsp) (Espressif) | Apache 2.0 | Las secuencias de registros del altavoz y los micrófonos (`src/audio.c`), escritas a partir de ellos |
| [FatFs](http://elm-chan.org/fsw/ff/) (ChaN), componente `fatfs` de ESP-IDF (Espressif) | BSD de 1 cláusula y Apache 2.0 | microSD en FAT y exFAT; copia con exFAT activado en `components/fatfs/` |
| Fuente [Aileron](https://dotcolon.net/font/aileron/) (Sora Sagano), la que trae Pillow | CC0 (dominio público) | Texto pequeño de las esferas (`src/uifont.c`, generado por `tools/gen_uifont.py`) |
| Fuente [Jersey 10](https://fonts.google.com/specimen/Jersey+10) (The Soft Type Project Authors) | [SIL Open Font License 1.1](tools/fonts/OFL-jersey10.txt) | Títulos, números y botones del sistema (`src/ui_fonts.c`, generado por `tools/make_fonts.py`) |
| Fuente [VT323](https://fonts.google.com/specimen/VT323) (Peter Hull) | [SIL Open Font License 1.1](tools/fonts/OFL-vt323.txt) | Etiquetas y textos del sistema (`src/ui_fonts.c`) |

Las dos fuentes OFL van convertidas a mapas de bits dentro del firmware; sus archivos originales y licencias están en `tools/fonts/`. La página de control y la app usan además una copia de cada una recortada a los caracteres latinos (`src/web/jersey10.ttf` y `src/web/vt323.ttf`, hechas con `fontTools.subset`), con la misma licencia y su mismo nombre, que ninguna de las dos reserva. Los dígitos grandes, los iconos, los fondos de pixel art, los sprites y todas las esferas son diseño propio, dibujados por código.

## App Android

| Componente | Licencia |
|---|---|
| AndroidX (core, activity, webkit, work) | Apache 2.0 |
| Kotlin y kotlinx.coroutines (JetBrains) | Apache 2.0 |

## Instalador web

| Componente | Licencia |
|---|---|
| [ESP Web Tools](https://github.com/esphome/esp-web-tools) (cargado desde unpkg) | Apache 2.0 |

## Datos y servicios

- **Tiempo y búsqueda de ciudades:** [Open-Meteo.com](https://open-meteo.com/), datos con licencia [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Gratis para uso no comercial; un uso comercial necesita además un plan de Open-Meteo.

## Obras en las que se inspiran algunas esferas

Son reinterpretaciones animadas dibujadas por código, no copias de las imágenes:

- *La noche estrellada*, Vincent van Gogh (1889), dominio público.
- *Earthrise*, fotografía de William Anders, Apolo 8 (NASA, 1968), dominio público.

## Nombres de personas y marcas

La licencia cubre el código y el diseño de este proyecto, no los nombres ni la imagen de personas reales ni las marcas que se mencionen (por ejemplo, el nombre del jugador en que se inspira la esfera *Gol*, que no lo representa, o Waveshare, Espressif y Android). Quien los use, sobre todo con fines comerciales, debe obtener por su cuenta los permisos que hagan falta.
