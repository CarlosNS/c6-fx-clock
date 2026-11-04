Guía para crear una esfera nueva y enviarla al proyecto. Una esfera es un fichero C que dibuja la pantalla entera 30 veces por segundo, hora incluida. No hace falta saber nada del resto del firmware: Bluetooth, WiFi, ajustes y app ya funcionan solos con cualquier esfera nueva.

**Índice:** [En 5 minutos](#en-5-minutos) · [Cómo funciona](#cómo-funciona-una-esfera) · [La pantalla](#la-pantalla) · [El tiempo](#el-tiempo-animación-y-hora) · [Dibujar la hora](#dibujar-la-hora) · [Herramientas de dibujo](#herramientas-de-dibujo) · [Rendimiento](#rendimiento-30-fps-sin-fpu) · [Memoria](#memoria) · [Interactiva](#esfera-interactiva) · [Ajustes propios](#ajustes-propios) · [El catálogo](#el-catálogo) · [Probarla](#probarla) · [Normas](#normas) · [Enviarla](#enviarla)

## En 5 minutos

Necesitas el entorno para compilar el firmware ([[Compilar y contribuir]]) y, para verla, el reloj por USB.

```bash
python tools/new_face.py burbujas "Burbujas"
pio run -e c6lcd -t upload
```

El primer comando crea `src/fx_burbujas.c` con una esfera que ya funciona y la da de alta en el firmware. El segundo la compila y la graba en el reloj. Elígela en la app (sale la última de su grupo) y empieza a cambiar el dibujo.

| Vertical (por defecto) | Horizontal (`--landscape`) |
|---|---|
| <img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/plantilla-v.png" alt="Plantilla vertical" width="172"> | <img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/plantilla-h.png" alt="Plantilla horizontal" width="320"> |

La plantilla pinta un degradado que cambia de color, una bola de luz que se mueve (y que puedes empujar con el dedo desde la app) y la fecha, la hora y la barra de segundos.

Opciones de `tools/new_face.py`:

| Opción | Qué hace |
|---|---|
| `id` | Nombre interno: minúsculas, números y `_` (`burbujas`). Da nombre al fichero y a las funciones |
| `label` | Nombre en la app (`"Burbujas"`), con tildes si quieres |
| `--landscape` | Horizontal, 320×172, en vez de vertical, 172×320 |
| `--group` | Sección de la app: `effects` (Efectos, por defecto), `games` (Videojuegos), `art` (Arte), `nature` (Naturaleza y espacio), `clocks` (Relojes y útiles) |
| `--desc` | Una frase para la app: qué se ve |
| `--hint` | Qué hace el dedo en el panel táctil de la app; si la pones, la app muestra el panel ([interactiva](#esfera-interactiva)) |
| `--dry-run` | Enseña lo que cambiaría sin tocar nada |

Si prefieres hacerlo a mano, el script solo toca cuatro sitios:

1. Crea `src/fx_<id>.c` con dos funciones, `<id>_enter` y `<id>_render`.
2. Declara las dos funciones en `src/fx.h` y suma 1 a `FX_COUNT`.
3. Añade la esfera **al final** de `fx_list` en `src/fx_common.c` ([por qué al final](#el-catálogo)).
4. Añade el fichero a `SRCS` en `src/CMakeLists.txt`.

## Cómo funciona una esfera

```c
void burbujas_enter(void);                                        // al elegirla
void burbujas_render(uint16_t *fb, uint32_t t, uint32_t dt);      // cada fotograma
```

- **`enter()`** se llama cada vez que alguien elige la esfera, aunque ya la hubiera usado antes. Aquí se reinicia el estado y se preparan tablas. Puede estar vacía.
- **`render(fb, t, dt)`** dibuja un fotograma completo en `fb`. El bucle principal (`src/main.c`) la llama hasta 30 veces por segundo y luego manda la imagen a la pantalla.
- Solo funciona una esfera a la vez. Su estado vive en variables `static` del propio fichero.
- La esfera no se ocupa de nada más: el brillo, el modo noche, el apagado de pantalla, los avisos y el código de emparejamiento los pinta o gestiona el firmware por encima.

## La pantalla

| | Vertical | Horizontal |
|---|---|---|
| Tamaño | `LCD_W` × `LCD_H` = 172 × 320 | `LAND_W` × `LAND_H` = 320 × 172 |
| En `fx_list` | `.landscape = false` | `.landscape = true` |

El reloj gira la pantalla solo al cambiar de esfera. La plantilla define `W` y `H` al principio, así el mismo código sirve para las dos orientaciones.

- **Píxeles:** `fb[y * W + x]`, de arriba a la izquierda hacia la derecha y hacia abajo.
- **Color:** RGB565 con los bytes ya invertidos para el bus SPI. Crea siempre los colores con `rgb565(r, g, b)` (0–255) o `hsv565(h, s, v)` (los tres 0–255; `h` da la vuelta al llegar a 256). No escribas valores a mano como `0xF800`: saldrían otros colores.
- **Pinta todos los píxeles en cada fotograma.** Hay dos búferes que se alternan: el `fb` que recibes contiene la imagen de **hace dos fotogramas**, no la anterior. Lo que no pintes saldrá con parpadeo. Lo normal es empezar con un fondo completo (un degradado, `gfx_fill`, una imagen calculada).

## El tiempo: animación y hora

Hay dos relojes distintos, y conviene no mezclarlos:

**`t` y `dt`, para animar.** `t` son milisegundos de animación desde el arranque y `dt` los milisegundos desde el fotograma anterior. Los dos ya vienen multiplicados por el ajuste *Velocidad de las animaciones*: si el usuario la pone al 50 %, `t` avanza a la mitad. Anima siempre con `t` (posiciones, fases, colores) y con `dt` (física: `pos += vel * dt`). No uses `esp_timer_get_time()`, porque entonces tu esfera ignoraría la velocidad.

**`g_now`, para la hora real.** Se actualiza en cada fotograma (`src/clock.h`):

| Campo | Qué es |
|---|---|
| `g_now.valid` | `false` hasta que el móvil o internet le dan la hora. Los ayudantes de abajo ya pintan `--:--` |
| `g_now.h`, `m`, `s`, `ms` | Hora (0–23), minuto, segundo y milisegundo |
| `g_now.day`, `mon`, `year`, `wday` | Fecha; `mon` es 1–12 y `wday` 0 = domingo |
| `g_now.minute_ms` | Milisegundos dentro del minuto actual, para animaciones que duran un minuto exacto |

- Para la hora en pantalla usa `clock_display_hour()`, que respeta el ajuste de 12/24 h, en vez de `g_now.h`.
- Si dibujas segundos, respeta `g_app.show_secs` (ajuste *Mostrar segundos*). `clock_draw_seconds_bar` ya lo hace.
- Para efectos que dependen de la hora del día (cielo, sol, luna), la orden `dbgtime 21 30` finge esa hora y `dbgtime -1 -1` vuelve a la real ([cómo mandar órdenes](#probarla)).

## Dibujar la hora

Cada esfera dibuja su propia hora, como mejor le quede. Ayudantes de `src/fx.h`:

| Función | Qué dibuja |
|---|---|
| `clock_draw_row(fb, &font, y, espacio, relleno, borde, modo)` | `HH:MM` en una fila centrada, con los dos puntos parpadeando |
| `clock_draw_stacked(fb, &font, y, hueco, relleno, borde, modo)` | Horas encima de minutos; devuelve la `y` de debajo |
| `clock_row_grad(fb, &font, cx, y, espacio, arriba, abajo, borde, sombra)` | `HH:MM` con relleno degradado y sombra |
| `clock_draw_seconds_bar(fb, x, y, w, h, color, fondo)` | Barra del minuto en curso (solo si los segundos están activados) |
| `clock_date_str(buf)` | Texto `"MIE 4 NOV"` |
| `clock_hhmm(buf, dos_puntos)` | Texto `"09:41"`, para dibujarlo a tu manera |

`modo` es `FONT_FILL`, `FONT_OUTLINE` o los dos (`FONT_FILL | FONT_OUTLINE`). El borde oscuro hace la hora legible sobre cualquier fondo.

Fuentes de dígitos grandes con antialias (`src/fonts.h`, generadas por `tools/gen_fonts.py`):

| Estilo | Grande (104 px) | Mediana (60 px) | Pequeña (34 px) |
|---|---|---|---|
| Redonda | `font_round_xl` | `font_round_m` | `font_round_s` |
| 7 segmentos | `font_seg_xl` | `font_seg_m` | `font_seg_s` |
| Caligráfica | | `font_nib_m` | `font_nib_s` |

Para otros textos:

| Función | Uso |
|---|---|
| `ui_text(fb, &ui_s, x, y, texto, relleno, borde, modo)` | Texto proporcional con antialias, admite tildes (`ui_s`, `ui_m`); `ui_width` mide |
| `gfx_text` / `gfx_text_ol(fb, x, y, texto, color, [borde,] escala, [grosor])` | Fuente pixelada 5×7 de cualquier tamaño (escala 1, 2, 3…); `gfx_text_width` mide |
| `pix_digits_draw(fb, x, y, texto, escala, degradado[4], borde)` | Dígitos de pixel art con biselado, para esferas de videojuego |

## Herramientas de dibujo

Todas recortan a la pantalla, así que puedes dibujar medio fuera sin miedo (`src/gfx.h`):

| Función | Dibuja |
|---|---|
| `gfx_fill`, `gfx_fill_rect` | Pantalla entera o rectángulo de un color |
| `gfx_line`, `gfx_tri`, `gfx_poly` | Línea, triángulo, polígono relleno (hasta 160 vértices) |
| `gfx_circle`, `gfx_ring` | Círculo relleno, anillo |
| `gfx_aa_capsule` | Segmento con extremos redondos y antialias (agujas, trazos); coordenadas en 1/16 de píxel |
| `gfx_glow` | Halo de luz que se difumina desde el centro |
| `gfx_darken_rect` | Oscurece un rectángulo redondeado, para paneles detrás de la hora |
| `gfx_blend(fondo, color, alfa)` | Mezcla dos colores (alfa 0–16) |
| `sprite_draw`, `sprite_draw_scaled` | Sprites de pixel art (`src/sprites.h`) |

Para cálculos:

| Ayudante | Para qué |
|---|---|
| `sin_q14[1024]` | Seno en 1024 pasos por vuelta, valores −16384…16384: `(sin_q14[fase & 1023] * radio) >> 14`. El coseno es `fase + 256` |
| `sin_u8[256]` | Seno de 0 a 255, para plasmas y paletas |
| `fx_hash(x, y)` | Número pseudoaleatorio fijo para cada `(x, y)`: ruido, estrellas, texturas que no parpadean |
| `fx_rand()` | Número aleatorio (cambia en cada llamada) |
| `gfx_isqrt32` | Raíz cuadrada entera |

Las esferas existentes son el mejor manual. Por tamaño: `fx_plasma.c` (50 líneas, un efecto por píxel), `fx_tunnel.c` (tablas precalculadas), `fx_synth.c` (escena con perspectiva), `fx_weather.c` (esfera analógica y datos del móvil) y `fx_classic.c` (reloj realista con ajuste de color).

## Rendimiento: 30 fps sin FPU

El ESP32-C6 es un RISC-V a 160 MHz **sin unidad de coma flotante**: cada operación con `float` se emula y es decenas de veces más lenta que con enteros. Hay 33 ms por fotograma, pero la esfera no debería pasar de unos 20 ms de media, porque Bluetooth y WiFi también necesitan tiempo. La plantilla tarda unos 5 ms.

- **Nada de `float` dentro de bucles por píxel.** Usa enteros y coma fija (`valor << 8`, multiplicar y desplazar). Unos pocos `float` por fotograma no importan.
- **Saca del bucle lo que no cambia:** lo que depende solo de `y`, calcúlalo una vez por fila; lo que depende solo de `x`, en una tabla por fotograma (como `fx_plasma.c`).
- **Tablas en vez de funciones caras:** `sin_q14` en vez de `sinf`, tablas de distancia o ángulo creadas en `enter()`, paletas de 256 colores en vez de `hsv565` por píxel.
- **Evita divisiones dentro de los bucles:** usa desplazamientos (`>> 8`) o multiplica por un inverso calculado antes.
- **Dibuja a menos resolución lo que es suave** (cielos, nieblas) y amplíalo: `fx_goal.c` dibuja los jugadores a media resolución.

Para medir:
- **`ovl 1`** pinta en la pantalla el nombre de la esfera y los fps; `ovl 0` lo quita.
- **`status`** (por Bluetooth, `get status`) devuelve `fps`, `rus` (tiempo medio de dibujo en µs) y `rmax` (el peor del último segundo).
- **Test de todas las esferas:** con el reloj en WiFi, `pytest tests/wifi -k 30fps` pasa por todas y falla si alguna baja de 30 fps.

## Memoria

El reloj tiene poca RAM libre: unos 55 KB en modo Bluetooth y 35 KB en WiFi, compartidos con todo lo demás.

- Las tablas `static` de tu fichero ocupan RAM siempre, aunque la esfera no esté puesta. Un búfer de 172×320 a 16 bits son 110 KB: no cabe.
- Para tablas grandes usa **`fx_scratch`**, 21 600 bytes compartidos entre esferas, porque solo una funciona a la vez. Resérvalo en `enter()` con un número propio (por convenio, el número de tu esfera), y rehaz las tablas solo si otra lo ha usado entretanto:

  ```c
  #define SCRATCH_ID 23  // el número de tu esfera: así no coincide con otra
  void burbujas_enter(void)
  {
      if (!fx_scratch_claim(SCRATCH_ID)) build_tables(fx_scratch);  // false = había otra: rehacer
  }
  ```

- Los datos que no cambian (paletas fijas, formas, sprites) decláralos `static const`: van a la flash, que sobra.
- No uses `malloc` en `render()`.

## Esfera interactiva

Si la esfera tiene `.hint` en `fx_list` (o usas `--hint` al crearla), la app muestra un panel táctil con ese texto. Al arrastrar el dedo, la esfera recibe el movimiento:

```c
int dx, dy;
app_take_drag(&dx, &dy);   // movimiento acumulado desde el fotograma anterior (y lo pone a 0)
```

También tienes `g_app.touch` (si el dedo está en el panel ahora mismo) y `g_app.tx`, `g_app.ty` (su posición, de 0 a 1000). Ejemplos: `fx_lava.c` (la bola sigue al dedo) y `fx_life.c` (dibujar células).

## Ajustes propios

Una esfera puede tener sus propios ajustes en la app, como el color de *Clásico* o la ciudad de *Tiempo*. Para un ajuste numérico:

1. **Valor:** un campo en `app_state_t` (`src/app.h`), por ejemplo `volatile int bub_size;`, y su valor por defecto en `g_app` (`src/main.c`).
2. **Ajuste:** una línea en `SETTINGS` de `src/settings.c`:

   ```c
   {"bsize", &g_app.bub_size, 1, 10, true, NULL},   // clave (≤ 15 letras), mín., máx., se guarda
   ```

   Con eso ya existe la orden `bsize 5` (por Bluetooth, WiFi y consola), se guarda en el reloj y sale en `status`.
3. **Catálogo:** `.opts = "bb"` (dos letras cualesquiera) en la entrada de `fx_list`.
4. **App:** en `src/index.html`, junto a los otros bloques de esfera (busca `data-opts="cl"`), un bloque que la app solo enseña con tu esfera:

   ```html
   <div class="own hide" data-opts="bb">
     <div class="lbl">Ajustes de esta esfera</div>
     <div class="slider" style="padding-top:0"><div class="top">Tamaño de las burbujas</div>
       <input type="range" id="bsize" min="1" max="10"></div>
   </div>
   ```

   Después, en el script, `bind('bsize','bsize')` para que mover el control mande la orden. Añade `'bsize'` a la lista de `wks`, `nst`, `nen`, `chue` para que el control muestre el valor que tiene el reloj.
5. **Esfera:** lee `g_app.bub_size` en `render()`.

La app Android usa esta misma página, así que el ajuste sale también en la app al compilarla.

## El catálogo

Cada entrada de `fx_list` (`src/fx_common.c`) describe la esfera para el firmware y para la app:

| Campo | Ejemplo | Para qué |
|---|---|---|
| `.name` | `"BURBUJAS"` | Nombre corto en ASCII para el registro y la información de depuración |
| `.landscape` | `false` | Orientación |
| `.enter`, `.render` | `burbujas_enter`, … | Las dos funciones |
| `.label` | `"Burbujas"` | Nombre en la app |
| `.group` | `FX_G_EFFECTS` | Sección: `FX_G_EFFECTS`, `FX_G_GAMES`, `FX_G_ART`, `FX_G_NATURE`, `FX_G_CLOCKS` |
| `.hint` | `"Arrastra para…"` | Solo si es interactiva |
| `.desc` | `"Burbujas que…"` | Una o dos frases para la tarjeta *Esfera actual* |
| `.opts` | `"bb"` | Solo si tiene [ajustes propios](#ajustes-propios) |

**Las esferas nuevas van siempre al final.** El número de cada esfera es su posición en la lista, y los relojes lo guardan (la esfera elegida, el cambio automático). Si insertas una en medio o reordenas, a quien actualice le cambiará la esfera. El cambio automático se salta las esferas que el usuario quita en la app (`fxoff`, máscara de 31 bits: hay un `_Static_assert` en `fx.h` que avisa si pasas de 31 esferas). La app las agrupa por `.group`, así que el orden de la lista no se ve. La app lee el catálogo del propio reloj, así que no hay que tocarla.

## Probarla

1. **Grábala:** `pio run -e c6lcd -t upload`. Si el reloj está en modo Bluetooth y emparejado, tarda un minuto en volver a anunciarse (espera la hora del móvil).
2. **Elígela:** en la app, o con la orden `fx N` (N = su número; el script lo dice).
3. **Órdenes:** cualquier orden (`fx 23`, `ovl 1`, `dbgtime 6 30`, `status`) se puede mandar:
   - por el **puerto serie**, como una línea de texto (115200 baudios; abrir el puerto reinicia el reloj);
   - por **Bluetooth** desde el PC: `python tools/ble_console.py --cmd "fx 23"` (o sin `--cmd`, para escribirlas una a una);
   - en modo **WiFi**, desde la página de control.
4. **Captura:** `python tools/ble_shot.py --fx N --out fxNN.png` por Bluetooth. En modo WiFi también vale `http://<ip-del-reloj>/screenshot?fx=N&h=21&m=30`, que admite una hora fingida.
5. **Comprueba:**
   - que va a 30 fps (`ovl 1`) con un `rus` medio por debajo de 20 000;
   - que se lee bien a distintas horas (`dbgtime`), en 12 y 24 h, con y sin segundos, y sin hora (`--:--` justo al encender);
   - que sigue bien al quitarla y volver a ponerla (`enter()` reinicia lo que debe);
   - si es interactiva, el panel táctil de la app.
6. **Tests:** `pytest -m now` (rápidos); con el reloj conectado, los de su modo (ver [[Compilar y contribuir]]). Cuentan las esferas a partir de `FX_COUNT`, así que una esfera nueva no los rompe.

## Normas

- **Todo dibujado por código.** Sin imágenes, fuentes ni sprites copiados de juegos, películas, series o webs. Inspirarse en un estilo (pixel art de 16 bits, Art Déco, un cuadro) está bien; copiar personajes o logotipos, no.
- **Obras de arte:** solo de dominio público, como *La noche estrellada* (Van Gogh, 1889) o *El grito* (Munch, 1893), reinterpretadas por código. Indica la obra y el año en `.desc`.
- **Personas reales y marcas:** mejor no. Si la esfera se inspira en alguien, que no lo represente ni use su imagen ([THIRD-PARTY-NOTICES.md](https://github.com/CarlosNS/c6-fx-clock/blob/main/THIRD-PARTY-NOTICES.md)).
- **Estilo:** el del resto del código. Comentarios en inglés, textos de la app en español, un comentario al principio del fichero que explique la esfera.
- **Licencia:** al enviarla la aportas con la GPL-3.0-or-later, en las condiciones de [CONTRIBUTING.md](https://github.com/CarlosNS/c6-fx-clock/blob/main/CONTRIBUTING.md).

## Enviarla

Una pull request con:

- [ ] `src/fx_<id>.c` y los cambios de `fx.h`, `fx_common.c` y `CMakeLists.txt` (y de `app.h`, `settings.c`, `main.c` e `index.html` si tiene ajustes);
- [ ] una fila en la tabla de esferas del `README.md`, en su grupo;
- [ ] su captura, `fxNN.png`, hecha con `tools/ble_shot.py`, adjunta a la pull request (el autor la añade a la página [[Esferas]] de la wiki);
- [ ] los fps y el `rus` medio que has medido, en la descripción;
- [ ] una línea en `CHANGELOG.md`, en la versión en preparación.

¿Dudas o una idea a medias? Abre una [issue](https://github.com/CarlosNS/c6-fx-clock/issues/new/choose) con la plantilla *Idea o esfera nueva* y lo hablamos antes de que le dediques horas.
