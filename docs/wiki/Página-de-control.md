La misma página de control funciona por tres caminos. Lo único que cambia es cómo llega al reloj (`src/transport.js`):

| Camino | Enlace | Dónde |
|---|---|---|
| **App Android** | Bluetooth | Dentro de la app (ver [[App Android]]) |
| **Navegador por WiFi** | WiFi | `http://<IP del reloj>` (sale en *Ajustes → Conexiones* del reloj), o `http://192.168.4.1` conectado a la red `Marea` (clave `mareamarea`) |
| **Web Bluetooth** | Bluetooth | Chrome o Edge de escritorio o Android abriendo la página desde un servidor local (`src/index.html` en `http://localhost`) y pulsando *Buscar el reloj* |

## Secciones

La página tiene el estilo del reloj (*Marea Solsticial*: pixel art nocturno, con las fuentes Jersey 10 y VT323). Se abre en **Inicio** y cada sección vuelve con **‹** arriba a la izquierda (o con el botón atrás del móvil).

- **Inicio:** si el reloj está conectado, su batería y su hora. Los **avisos** que piden algo: una versión nueva del reloj o de la app (con su botón *Actualizar*) y un permiso que falta en el móvil. Debajo, la **esfera que se ve** en el reloj y las secciones: Esferas, Alarmas, Archivos y Ajustes.
- **Esferas:** arriba, la esfera actual con su descripción, su **velocidad de animación** y, si los tiene, los botones **Tocar** (panel táctil de las esferas interactivas: Plasma, Cubo 3D, Túnel, Lava y Vida) y **Opciones** (color de Clásico y Ónix, ciudad de Tiempo). Luego el **cambio automático** cada 30 s y todas las esferas por grupos, cada una con su **vista previa**. Tocar una la pone en el reloj; el **ojo** de su esquina la incluye o la saca del cambio automático.
- **Alarmas:** cada alarma con su **etiqueta**, su hora, sus días y su animación, y un interruptor para encenderla o apagarla. Al tocarla se despliegan la etiqueta (hasta 23 bytes), **Animación de alarma**, que abre una ventana con la vista previa de cada animación (Amanecer y Bomba), y *Probar en el reloj*. La hora y los días se ponen en el reloj.
- **Archivos:** la **microSD** del reloj, solo para mirar: espacio libre, carpetas y archivos con tamaño y fecha. Las carpetas se abren y la ruta de arriba lleva de vuelta; los archivos no se abren ni se borran (eso, en la app Archivos del reloj).
- **Ajustes:**
  - **Idioma:** español o inglés, en el reloj y en la página a la vez.
  - **Pantalla:** brillo, siempre encendida, levantar la muñeca, apagar tras y modo noche (horas y brillo).
  - **Hora:** formato 24 h y segundos. La hora llega sola desde el móvil.
  - **Móvil y conexión:** si el reloj está conectado, olvidarlo, los permisos de la app (segundo plano, música y llamadas) y el WiFi del reloj (interruptor y red).
  - **Actualizaciones:** versión del reloj y de la app, y botones para actualizar (ver [[Actualizaciones]]).
  - **Avanzado:** sincronizar la hora a mano, la sincronización de la app, batería, firmware, rendimiento, información de depuración en la pantalla y actualizar subiendo un archivo (solo por WiFi).

Los nombres y las descripciones de las esferas vienen del firmware y por ahora solo están en español.

## Por WiFi, además

Útil para depurar o para integrarlo con otras cosas:

| Petición | Respuesta |
|---|---|
| `GET /status` | Estado completo en JSON (ajustes, hora, red, memoria, fps, tiempo de render, actualización) |
| `GET /faces` | Catálogo de esferas: nombre, grupo, descripción y ajustes propios |
| `GET /alarms` | Alarmas con su etiqueta y su animación, y los nombres de las animaciones |
| `GET /files?p=/Música` | Una carpeta de la microSD: estado de la tarjeta, espacio y lista con nombre, carpeta o no, tamaño y fecha |
| `GET /a/<archivo>` | Imágenes y fuentes de la página: vistas previas (`fxN.webp`, `alarmN.webp`) y `jersey10.ttf`, `vt323.ttf` |
| `GET /screenshot` | La pantalla como BMP (el lienzo de la esfera o de la vista del sistema). Opcionales: `fx` (esfera), `h` y `m` (simular una hora), `wait` (ms antes de capturar) |
| WebSocket `/ws` | Los mismos comandos de texto que por Bluetooth (ver [[Arquitectura]]) |

Por ejemplo, `http://192.168.1.140/screenshot?fx=22&h=10&m=10` captura la esfera Clásico a las 10:10.
