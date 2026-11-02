# Pruebas manuales (con el móvil)

Lo que no se puede automatizar desde el PC. Marca cada punto al hacerlo.

## Fase 1 — Botón de cambio de modo

- [ ] Encender el reloj y, mientras se ve la pantalla de bienvenida, mantener BOOT: se llena una barra y a los 2 s se reinicia en el otro modo (WiFi ↔ Bluetooth).
- [ ] Soltar BOOT antes de 2 s: no cambia nada.
- [ ] La pantalla de bienvenida indica el modo actual ("MODO WIFI" o "MODO BLUETOOTH").

## Fase 3 — Emparejamiento real

- [ ] Abrir la app y pulsar *Conectar*: el reloj enciende la pantalla y muestra una clave de 6 dígitos.
- [ ] Teclear la clave en el móvil: queda emparejado y aparecen las esferas.
- [ ] Teclear una clave **incorrecta**: no empareja y el reloj no cambia nada.
- [ ] Cerrar la app, apagar y encender el reloj, abrir la app: conecta sin volver a pedir la clave.
- [ ] *Olvidar móviles* (o BOOT 10 s): la siguiente conexión vuelve a pedir la clave.

## Fase 5 — App

- [ ] La APK se instala y pide los permisos de Bluetooth y notificaciones.
- [ ] El emparejamiento lo hace el sistema (Companion Device Manager) y el reloj aparece como accesorio.
- [ ] Cambiar de esfera, brillo, color del Clásico y ciudad del tiempo: se ve en el reloj al instante.
- [ ] Al abrir la app, el reloj pone la hora del móvil (comparar segundos).
- [ ] La esfera Tiempo se actualiza con los datos que manda el móvil.
- [ ] Con el reloj fuera de alcance, la app avisa y reconecta sola al volver.
- [ ] Panel táctil en Plasma, Cubo 3D, Túnel, Lava y Vida: responde sin retraso apreciable.

## Fase 5c — Sincronización en segundo plano

- [ ] Con la app cerrada, a los 30 min la esfera Tiempo tiene datos nuevos (mirar la antigüedad).
- [ ] Una noche entera con la app cerrada y el móvil en reposo: por la mañana la hora y el tiempo están al día.
- [ ] Con el reloj fuera de alcance, la sincronización falla sin avisos molestos y se reanuda al volver.
- [ ] Publicar una versión nueva: llega una notificación de Android sin abrir la app.
- [ ] Consumo de batería de la app tras un día (Ajustes → Batería): debe ser despreciable.

## Fase 6 — Actualización desde el móvil

- [ ] Con una versión nueva publicada, la app la muestra con sus notas.
- [ ] *Actualizar*: el reloj enseña el progreso, se reinicia y la app indica "Actualizado a X".
- [ ] Con la versión al día: la app indica que no hay nada nuevo.
- [ ] Con el WiFi del reloj mal configurado: la app indica "El reloj no pudo conectarse al WiFi".
