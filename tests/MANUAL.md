# Pruebas manuales (con el móvil)

Lo que no se puede automatizar desde el PC. Marca cada punto al hacerlo.

## Fase 1 — Botón de cambio de modo

- [ ] Encender el reloj y, mientras se ve la pantalla de bienvenida, mantener BOOT: se llena una barra y a los 2 s se reinicia en el otro modo (WiFi ↔ Bluetooth).
- [ ] Soltar BOOT antes de 2 s: no cambia nada.
- [ ] La pantalla de bienvenida indica el modo actual ("MODO WIFI" o "MODO BLUETOOTH").

## Fase 3 — Emparejamiento real

La lógica ya la prueban los tests automáticos con la clave real (leída del log serie). Aquí queda lo que se ve y se toca:

- [ ] Al emparejar, la clave aparece en grande sobre la esfera, con "CLAVE BLUETOOTH" y "Escribela en el movil".
- [ ] La pantalla sigue encendida mientras la clave está en pantalla, aunque tardes más de 10 s en teclearla.
- [ ] La barra morada sobre la clave se acorta durante 30 s y se pone roja en los 10 últimos.
- [ ] Mantener BOOT: a los 3 s aparece "10 S: OLVIDAR MOVILES" y a los 10 s "MOVILES OLVIDADOS".
- [ ] Pulsación larga (1–3 s) y soltar: cambia "AUTO: SI / NO" al soltar.

- [ ] Abrir la app y pulsar *Conectar*: el reloj enciende la pantalla y muestra una clave de 6 dígitos.
- [ ] Teclear la clave en el móvil: queda emparejado y aparecen las esferas.
- [ ] Teclear una clave **incorrecta**: no empareja y el reloj no cambia nada.
- [ ] Cerrar la app, apagar y encender el reloj, abrir la app: conecta sin volver a pedir la clave.
- [ ] *Olvidar móviles* (o BOOT 10 s): la siguiente conexión vuelve a pedir la clave.

## Fase 5 — App

Preparación (una vez): en el móvil, *Ajustes → Sobre el teléfono → Versión de HyperOS*, tocar 7 veces; luego *Ajustes adicionales → Opciones de desarrollador → Depuración USB* (y en HyperOS también *Instalar vía USB*). Conectar por USB y aceptar la huella del PC. Poner el reloj en modo Bluetooth (BOOT 2 s en la bienvenida).

- [ ] La APK se instala (`adb install android/app/build/outputs/apk/debug/app-debug.apk`) y pide los permisos de Bluetooth y notificaciones.
- [ ] *Buscar el reloj*: aparece la lista del sistema con "C6 Clock XXXX"; al elegirlo, el reloj enseña la clave y el móvil la pide. Tras teclearla se ven las esferas.
- [ ] En *Este móvil → Funcionar en segundo plano → Permitir*: aceptar. En HyperOS, además: *Ajustes → Apps → C6 Clock → Inicio automático* activado y *Ahorro de batería → Sin restricciones*.
- [ ] Con la app emparejada, `pytest tests/app -m phase5` pasa (arranque, sincronización por `adb`, tarea periódica).
- [ ] Cambiar de esfera, brillo, color del Clásico y ciudad del tiempo: se ve en el reloj al instante.
- [ ] Al abrir la app, el reloj pone la hora del móvil (comparar segundos).
- [ ] La esfera Tiempo se actualiza con los datos que manda el móvil.
- [ ] Con el reloj fuera de alcance, la app avisa y reconecta sola al volver.
- [ ] Panel táctil en Plasma, Cubo 3D, Túnel, Lava y Vida: responde sin retraso apreciable.

## Fase 5c — Sincronización en segundo plano

- [ ] Con la app cerrada, a los 30 min la esfera Tiempo tiene datos nuevos (mirar la antigüedad).
- [ ] Una noche entera con la app cerrada y el móvil en reposo: por la mañana la hora y el tiempo están al día.
- [ ] Con el reloj fuera de alcance, la sincronización falla sin avisos molestos y se reanuda al volver.
- [ ] Con la app cerrada, desenchufar el reloj unos minutos y volver a enchufarlo: en pocos segundos tiene la hora del móvil (comprobado por USB el 2026-11-03: 1,6 s).
- [ ] Publicar una versión nueva: llega una notificación de Android sin abrir la app.
- [ ] Consumo de batería de la app tras un día (Ajustes → Batería): debe ser despreciable.

## Fase 6 — Actualización desde el móvil

- [ ] Con una versión nueva publicada, la app la muestra con sus notas.
- [ ] *Actualizar*: el reloj enseña el progreso, se reinicia y la app indica "Actualizado a X".
- [ ] Con la versión al día: la app indica que no hay nada nuevo.
- [ ] Con el WiFi del reloj mal configurado: la app indica "El reloj no pudo conectarse al WiFi".
