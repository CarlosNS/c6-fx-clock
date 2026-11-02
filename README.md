# ESP32-C6 FX Clock

Reloj con esferas animadas para la **Waveshare ESP32-C6-LCD-1.47** (ST7789, 172×320), controlado desde el móvil por WiFi.

## Esferas

| # | Esfera | Orientación | Cómo muestra la hora |
|---|--------|-------------|----------------------|
| 0 | Plasma | vertical | HH / MM grandes sobre panel oscuro translúcido |
| 1 | Cubo 3D | vertical | hora bajo el cubo; el cubo da una vuelta rápida en cada minuto |
| 2 | Túnel | vertical | 7 segmentos del color complementario al túnel |
| 3 | Lava | vertical | dígitos oscuros con borde luminoso |
| 4 | Vida | horizontal | dígitos de células inmortales; al cambiar el minuto, los viejos se disuelven en la simulación |
| 5 | HUD | horizontal | interfaz sci-fi: anillo de segundos, temperatura del chip, señal WiFi, glitch cada minuto |
| 6 | Aventura | horizontal | viaje en pixel art a resolución nativa: parallax con bruma atmosférica, lago con reflejos, luz según la hora real (amanecer, día, atardecer, noche con faroles y luciérnagas) |
| 7 | RPG | vertical | batalla 3D estilo FF VIII–X: cámara cinemática, arena con círculo rúnico, cristal guardián, grupo en HD-2D; las barras ATB marcan los segundos y cada minuto se lanza un hechizo con la hora como daño |

**Basadas en obras famosas** (todas de dominio público; reinterpretaciones animadas, no copias):

| # | Esfera | Orientación | Obra y animación |
|---|--------|-------------|------------------|
| 8 | Estrellada | horizontal | *La noche estrellada* (Van Gogh, 1889): cielo de pinceladas en remolino, halos que laten, ciprés que se mece |
| 10 | El grito | vertical | *El grito* (Munch, 1893): cielo ondulante, fiordo en espiral, puente en perspectiva, figura que se retuerce |
| 11 | Earthrise | horizontal | Foto del Apolo 8 (NASA, 1968): Tierra 3D con continentes y nubes que giran saliendo sobre la Luna |

**Diseño libre:**

| # | Esfera | Orientación | Descripción |
|---|--------|-------------|-------------|
| 9 | WiFi | horizontal | la hora en grande sobre un espectro tenue de las redes WiFi de alrededor (escaneo en segundo plano) |
| 12 | Sakura | horizontal | cerezo en flor junto a un estanque: pétalos que caen revoloteando y ondas donde tocan el agua |
| 13 | Synthwave | horizontal | sol a franjas, rejilla de neón, palmeras, números cromados |
| 14 | Arena | vertical | reloj de arena con física de granos: se vacía exactamente en un minuto y gira |
| 15 | Engranajes | vertical | reloj analógico mecánico: engranajes que engranan, volante oscilante, agujas con segundero |
| 16 | Acuario | horizontal | peces a dos profundidades, burbujas, algas, rayos de luz y cáusticas |
| 17 | Lluvia | vertical | gotas-lente en el cristal sobre una ciudad desenfocada, hora en neón que parpadea |
| 18 | Agujero negro | horizontal | disco de acreción con efecto Doppler, lente gravitatoria del disco trasero y anillo de fotones; las estrellas se curvan alrededor (ecuación de lente puntual) |
| 19 | Gol | horizontal | jugada de fútbol en 16 bits con Mbappé: regate, salto sobre una entrada, gol por la escuadra con el portero vencido, confeti y celebración con los brazos cruzados; la hora en el marcador del estadio. Los jugadores son esqueletos 2D rasterizados a media resolución con sombreado y contorno, así cada pose se interpola suave |
| 20 | Cuento | horizontal | fantasía oscura pintada como un libro infantil en acuarela: un árbol dorado gigante que respira luz en el horizonte, hojas doradas que caen y un osito caballero (personaje original) que camina hacia él; hora en tinta caligráfica |
| 21 | Tiempo | vertical | minimalista: esfera analógica con agujas suavizadas y segundero de barrido; debajo, icono animado, temperatura, máx/mín y estado del cielo de la ciudad configurada. El fondo cambia con el tiempo (sol, gris, lluvia, noche) |
| 22 | Clásico | vertical | reloj de vestir rectangular Art Déco: caja de metal pulido con corona, esfera sunburst cuyos reflejos se mueven (verde por defecto; el color se elige con un slider en la web), minutería de raíles, índices y agujas dauphine doradas, fecha a las 3 y pequeño segundero a las 6 con 8 pulsos por segundo |

Todas van limitadas a **30 fps**.

## Instalar y actualizar

`python tools/build_webflasher.py` (tras `pio run`) genera en `webflasher/` dos archivos:

| Archivo | Para qué |
|---|---|
| `c6-fx-clock.bin` | Instalación completa por USB (bootloader + tabla de particiones + firmware). Primera instalación o recuperación. |
| `c6-fx-clock-ota.bin` | Solo el firmware, para actualizar **por WiFi** desde la web del reloj. |

### Por USB (webflasher)

1. Doble clic en **`webflasher/Abrir flasher.bat`** (necesita Python; abre `http://localhost:8765`).
2. En Chrome o Edge, pulsa **Instalar el reloj** y elige el puerto de la placa.
3. Si eliges borrar el dispositivo, luego conecta el móvil a `C6-FX-Demo` y configura tu WiFi en la sección **Conexión** de `http://192.168.4.1`.

También se puede publicar la carpeta en cualquier hosting con HTTPS (p. ej. GitHub Pages).

### Por WiFi (OTA)

En la web del reloj: **Avanzado → Actualizar por WiFi**, elige `c6-fx-clock-ota.bin` y pulsa *Actualizar firmware* (unos 7 s). La pantalla muestra el progreso y el reloj se reinicia solo con la versión nueva.

- La flash tiene dos huecos de 3,5 MB (`ota_0` / `ota_1`): la actualización se escribe en el que no está en uso, así que un corte a medias no estropea nada.
- Solo acepta firmware de este proyecto: otro archivo se rechaza antes de escribir nada.
- **Vuelta atrás automática:** una versión nueva debe funcionar 30 s para quedar confirmada; si se cuelga antes, el reloj vuelve solo a la anterior en el siguiente arranque.
- Cambiar la tabla de particiones (como el paso a 2.0.0) exige una instalación por USB; OTA solo sustituye el firmware.

## Uso

1. Compila y flashea: `pio run -t upload` (puerto COM5).
2. Abre el panel web:
   - en tu WiFi: la IP aparece en la pantalla al arrancar;
   - o conecta el móvil a **C6-FX-Demo** (clave `esp32c6fx`) y abre **http://192.168.4.1**.
3. **Pantalla**: apagada por defecto (no se renderiza nada). El **botón BOOT** la enciende el tiempo elegido en la web (*Apagar la pantalla tras*: de 5 s a 10 min; 10 s por defecto); con ella encendida, pulsación corta = siguiente esfera, larga = cambio automático. Usar la web también la mantiene encendida. Opción *Pantalla siempre encendida* en la web.

### Hora

- Se sincroniza por **NTP** si la red WiFi tiene salida a internet (zona horaria Europe/Madrid con horario de verano).
- Si no, la **web la envía sola** al abrirla (o con el botón *Sincronizar hora*).
- La placa no tiene batería de reloj: tras quitar la corriente hay que volver a sincronizar.

### Tiempo

- Fuente: [Open-Meteo](https://open-meteo.com) (gratis, sin clave). La placa lo consulta sola por HTTP cada 10 min.
- Si la red no tiene salida a internet, la web lo consulta desde el móvil al abrirla y se lo pasa a la placa.
- Ciudad: tarjeta **Tiempo** de la web (busca por nombre y elige el resultado). Por defecto, Sevilla.

### Ajustes (se guardan en flash)

Esfera, brillo, velocidad, 24/12 h, segundos, cambio automático y **modo noche** (por defecto 23:00–07:00 al 10 % de brillo, con el LED apagado).

### Depuración

- `GET /screenshot` devuelve la pantalla como BMP. Parámetros opcionales: `fx` (esfera), `h` y `m` (simular una hora), `wait` (ms antes de capturar).
- `GET /status` incluye `rus` / `rmax`: tiempo medio y máximo de render por fotograma (µs). El presupuesto a 30 fps es 33 ms.
- `GET /faces` devuelve el catálogo de esferas (nombre, grupo, descripción, si es interactiva, ajustes propios).

## Detalles técnicos

- Cifras temáticas: Aventura usa dígitos pixel art 7×10 (degradado crema-dorado, bisel, contorno y sombra, como los HUD de RPG de 16 bits); RPG usa numerales caligráficos de pluma ancha con serifas y degradado (`font_nib_*`), en la línea de los menús de los JRPG de PS1/PS2. Todo diseño propio.
- Texto de interfaz suavizado generado por `tools/gen_uifont.py` con Aileron (dominio público, incluida en Pillow).
- Mezcla de color RGB565 con una sola multiplicación (canales empaquetados en 32 bits); el C6 no tiene FPU y divide lento.
- Fuentes grandes generadas por `tools/gen_fonts.py` (dígitos de diseño propio, sin fuentes externas): cobertura + contorno en 4 bits por píxel para que se lean sobre cualquier fondo.
- Rotación por hardware (MADCTL) según la esfera; mismo framebuffer en ambas orientaciones.
- Doble framebuffer (2 × 110 KB) con DMA; el código de WiFi/lwIP/FreeRTOS va en flash para dejar heap a la red (~33 KB libres en marcha).
- Flash de 8 MB: `nvs` (ajustes y WiFi), `otadata`, dos huecos de app de 3,5 MB para OTA y 896 KB libres (`storage`) para el futuro.
- DNS públicos (1.1.1.1 / 8.8.8.8): algunos routers rechazan las consultas DNS de la LAN.
- WiFi en redes malladas: busca en todos los canales y se une al nodo con mejor señal; si un nodo acepta la conexión pero no da IP en 20 s, se desconecta y reintenta.

## Arquitectura

```
transportes        web.c (HTTP + WebSocket)        [futuro: ble.c]
                            |                             |
control            control.c  -- comandos de texto y JSON de estado --
                   settings.c -- tabla única de ajustes (NVS, comando y JSON con la misma clave)
                   ota.c      -- actualización por trozos, sin depender del transporte
servicios          net.c (WiFi)   clock.c (hora)   weather.c (tiempo)
pantalla           main.c (bucle a 30 fps, reposo, botón)   fx_*.c (esferas)   gfx.c / lcd.c
```

- **Comandos:** todo se controla con mensajes de texto (`bri 80`, `fx 3`, `loc 37.38 -5.98 Sevilla`, `wifi`...). La lista completa está en `control.h`. Cualquier transporte solo tiene que pasar esos textos a `app_command()` y leer `app_status_json()`.
- **Ajustes:** añadir uno es una línea en `settings.c`; se guarda, se controla por comando y aparece en el estado sin tocar nada más.
- **Esferas:** cada una se describe una sola vez en `fx_list` (`fx_common.c`): nombre, grupo, descripción, si es interactiva y sus ajustes propios. La web construye su menú a partir de `/faces`.

### Bluetooth

En marcha según `docs/PLAN-bluetooth.md` (fases 0–2 hechas). El modo Bluetooth se activa desde la web (*Avanzado → Modo Bluetooth*) o manteniendo BOOT 2 s en la pantalla de bienvenida (también para volver al WiFi). El reloj se anuncia como `C6 Clock XXXX` y acepta los mismos comandos que la web; desde el PC:

```
python tools/ble_console.py          # comandos a mano
python tools/fake_phone.py           # una sincronización como la hará la app
```

En el C6 el WiFi y el Bluetooth no caben a la vez en la RAM, así que cada uno tiene su modo de arranque. La app Android llegará en la fase 5.
