## El botón BOOT

Es el único botón que hace falta (el otro, RESET, reinicia la placa).

| Cuándo | Gesto | Qué hace |
|---|---|---|
| Pantalla apagada | Pulsación | Enciende la pantalla |
| Pantalla encendida | Pulsación corta | Siguiente esfera |
| Pantalla encendida | Pulsación larga (más de 0,8 s) | Activa o desactiva el **cambio automático** de esfera (cada 30 s); al soltar se ve «AUTO: SÍ / NO» |
| Pantalla de bienvenida (al encender) | Mantener 2 s | Cambia de modo: WiFi ↔ Bluetooth (se llena una barra y se reinicia) |
| En cualquier momento | Mantener 3 s | Aparece «10 S: OLVIDAR MÓVILES» |
| En cualquier momento | Mantener 10 s | **Olvida todos los móviles emparejados** («MÓVILES OLVIDADOS») |

## La pantalla

- Por defecto está **apagada** y no dibuja nada, para gastar poco y no calentar el chip.
- BOOT, la app o la página la encienden durante el tiempo elegido en *Pantalla → Apagar la pantalla tras* (de 3 s a 10 min; 10 s por defecto).
- *Pantalla siempre encendida* la deja encendida de forma permanente.
- **Modo noche:** de 23:00 a 07:00 baja el brillo al 10 % y apaga el LED. Horas y brillo se cambian en *Pantalla*.
- El LED RGB de la placa imita el color medio de la pantalla, como un pequeño ambilight.

## Los modos

En el ESP32-C6 el WiFi y el Bluetooth no caben a la vez en la memoria, así que el reloj arranca en uno de tres modos:

| Modo | Para qué | Cómo se controla |
|---|---|---|
| **Bluetooth** | Uso diario | App Android (o Chrome/Edge por Web Bluetooth) |
| **WiFi** | Sin app, o para configurar | Página web en la IP del reloj, o en `192.168.4.1` desde su propia red `C6-FX-Demo` |
| **Actualización** | Solo durante una actualización | Automático: se conecta a tu WiFi, descarga, y vuelve solo al modo anterior |

La pantalla de bienvenida dice el modo actual («MODO WIFI» o «MODO BLUETOOTH»). Para cambiar:

- **BOOT 2 s** en la pantalla de bienvenida, o
- el botón de *Avanzado → Modo de conexión* en la app o la página.

El modo se guarda: el reloj sigue en él tras apagarlo.

## La hora

- **Modo Bluetooth:** la pone el móvil al abrir la app, cada 30 minutos aunque esté cerrada y en cuanto el reloj se enciende o vuelve a estar cerca (Android 12+).
- **Modo WiFi:** por internet (NTP), con la zona horaria de Madrid y su horario de verano. Si la red no tiene internet, la página la envía al abrirla (*Sincronizar hora con este dispositivo*).
- La placa **no tiene pila de reloj**: al quitar la corriente pierde la hora hasta la siguiente sincronización. La placa definitiva sí la tendrá (ver [[Hardware]]).

## El tiempo

Lo usa la esfera **Tiempo** (y algunos fondos). Los datos son de [Open-Meteo](https://open-meteo.com/):

- En modo Bluetooth los manda el móvil en cada sincronización.
- En modo WiFi el reloj los pide solo cada 10 minutos.
- La ciudad se cambia en los ajustes de la esfera Tiempo (busca por nombre). Por defecto, Sevilla.

## Ajustes que se guardan

Esfera, brillo, velocidad de las animaciones, formato 24 h, segundos, cambio automático, modo noche, tiempo de encendido, color de la esfera Clásico y ciudad. Se guardan en la memoria flash 3 segundos después del último cambio.
