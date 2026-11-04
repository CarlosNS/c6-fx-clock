# Avisos de terceros

El código de este repositorio es de CarlosNS y se distribuye con la licencia [GPL-3.0-or-later](LICENSE). Todas las licencias de abajo son compatibles con ella. Los firmwares y la app que se publican incluyen o usan además estos componentes de terceros, cada uno con su propia licencia, que **no cambia** por estar aquí:

## Firmware del reloj

| Componente | Licencia | Uso |
|---|---|---|
| [ESP-IDF](https://github.com/espressif/esp-idf) (Espressif), incluidos FreeRTOS, lwIP, NimBLE, mbedTLS y el controlador WiFi/Bluetooth | Apache 2.0 (y MIT/BSD en algunos componentes) | Sistema base, red, Bluetooth, HTTPS |
| Fuente [Aileron](https://dotcolon.net/font/aileron/) (Sora Sagano), la que trae Pillow | CC0 (dominio público) | Texto pequeño de la interfaz (`src/uifont.c`, generado por `tools/gen_uifont.py`) |

Los dígitos grandes, los sprites y todas las esferas son diseño propio, dibujados por código.

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
- *El grito*, Edvard Munch (1893), dominio público.
- *Earthrise*, fotografía de William Anders, Apolo 8 (NASA, 1968), dominio público.

## Nombres de personas y marcas

La licencia cubre el código y el diseño de este proyecto, no los nombres ni la imagen de personas reales ni las marcas que se mencionen (por ejemplo, el jugador que aparece en la esfera *Gol*, o Waveshare, Espressif y Android). Quien los use, sobre todo con fines comerciales, debe obtener por su cuenta los permisos que hagan falta.
