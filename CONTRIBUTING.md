# Contribuir

¡Gracias por querer mejorar el reloj! Ideas, fallos y esferas nuevas son bienvenidos.

## Antes de empezar

- **Fallos e ideas:** abre una [issue](https://github.com/CarlosNS/c6-fx-clock/issues) con lo que pasa, la versión del firmware y de la app, y cómo reproducirlo.
- **Cambios de código:** para algo grande (una esfera, un transporte nuevo), abre antes una issue y lo hablamos.
- Cómo compilar, probar y publicar: [wiki → Compilar y contribuir](https://github.com/CarlosNS/c6-fx-clock/wiki/Compilar-y-contribuir).

## Estilo

- C (ESP-IDF) y Kotlin con el estilo del código que lo rodea; comentarios en inglés, textos de interfaz en español.
- Las esferas se dibujan por código: sin imágenes ni fuentes con derechos de terceros.
- Si tocas el protocolo Bluetooth, actualiza a la vez `tests/clocklib/protocol.py`, `src/transport.js` y la app, y sus tests (`tests/app/vectors.json`).

## Licencia de las contribuciones

El proyecto se distribuye con la [PolyForm Noncommercial 1.0.0](LICENSE.md) y su autor puede, además, conceder licencias comerciales. Para que eso siga siendo posible, al enviar una contribución (pull request, parche o código en una issue):

1. Declaras que es obra tuya, o que tienes derecho a aportarla.
2. La aportas con la misma licencia PolyForm Noncommercial 1.0.0.
3. Concedes además a CarlosNS una licencia perpetua, mundial, gratuita e irrevocable para usarla, modificarla, sublicenciarla y distribuirla con cualquier licencia, incluidas licencias comerciales.

Si no estás de acuerdo con el punto 3, dilo en la pull request antes de que se acepte.
