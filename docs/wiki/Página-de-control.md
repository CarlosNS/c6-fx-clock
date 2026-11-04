La misma página de control funciona por tres caminos. Lo único que cambia es cómo llega al reloj (`src/transport.js`):

| Camino | Modo del reloj | Dónde |
|---|---|---|
| **App Android** | Bluetooth | Dentro de la app (ver [[App Android]]) |
| **Navegador por WiFi** | WiFi | `http://<IP del reloj>`, o `http://192.168.4.1` conectado a la red `C6-FX-Demo` (clave `esp32c6fx`) |
| **Web Bluetooth** | Bluetooth | Chrome o Edge de escritorio o Android abriendo la página desde un servidor local (`src/index.html` en `http://localhost`) y pulsando *Buscar el reloj* |

## Tarjetas

- **Esfera actual:** nombre, orientación, grupo y descripción. Algunas esferas tienen ajustes propios (color de Clásico, ciudad de Tiempo) o son **interactivas**: en Plasma, Cubo 3D, Túnel, Lava y Vida aparece un panel táctil para moverlas con el dedo.
- **Elegir esfera:** todas las esferas por grupos; el icono indica si es vertical u horizontal.
- **Pantalla:** brillo, tiempo de encendido, siempre encendida y modo noche (horas y brillo).
- **Reloj:** formato 24 h, segundos, cambio automático cada 30 s (con **Esferas del cambio automático → Elegir** se marcan las que no deben salir solas) y velocidad de las animaciones.
- **Conexión:** la red WiFi del reloj (también en modo Bluetooth, para las actualizaciones) y *Sincronizar hora con este dispositivo*.
- **Actualizaciones:** versión del reloj y de la app, y botones para actualizar (ver [[Actualizaciones]]).
- **Este móvil** (solo en la app): última sincronización, permiso de segundo plano y olvidar el reloj.
- **Avanzado:** información de depuración en la pantalla (esfera, fps e IP), rendimiento (fps y memoria libre), versión del firmware, modo de conexión (WiFi ↔ Bluetooth) y actualizar subiendo un archivo (solo en WiFi).

## Por WiFi, además

Útil para depurar o para integrarlo con otras cosas:

| Petición | Respuesta |
|---|---|
| `GET /status` | Estado completo en JSON (ajustes, hora, red, memoria, fps, tiempo de render, actualización) |
| `GET /faces` | Catálogo de esferas: nombre, grupo, orientación, descripción y ajustes propios |
| `GET /screenshot` | La pantalla como BMP. Opcionales: `fx` (esfera), `h` y `m` (simular una hora), `wait` (ms antes de capturar) |
| WebSocket `/ws` | Los mismos comandos de texto que por Bluetooth (ver [[Arquitectura]]) |

Por ejemplo, `http://192.168.1.140/screenshot?fx=22&h=10&m=10` captura la esfera Clásico a las 10:10.
