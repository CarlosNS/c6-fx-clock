![El sistema: esfera, lanzador, ajustes rápidos y ajustes](https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/sistema.png)

Marea se maneja con el dedo sobre la pantalla y con el botón **BOOT** (el otro botón, RESET, reinicia la placa). El sistema sigue el estilo *Marea Solsticial*: pixel art nocturno junto al mar, paneles índigo, texto crema y oro para lo importante.

## Gestos

| Gesto | En la esfera | En una app o un menú |
|---|---|---|
| Deslizar desde el **borde de arriba** hacia abajo | Abre los **ajustes rápidos** | |
| Deslizar desde el **borde de abajo** hacia arriba | Abre el **lanzador** de apps | |
| Deslizar a **izquierda o derecha** | Esfera siguiente o anterior | Pasar páginas, si las hay |
| **Mantener** el dedo | Abre el **selector de esferas** | Acciones extra (por ejemplo, borrar en Archivos) |
| Deslizar desde el **borde izquierdo** hacia la derecha | | **Volver** |
| Tocar | En las esferas interactivas (Lava, Vida…), el dedo mueve la esfera | Pulsar botones e interruptores |

Las esquinas de la pantalla son redondeadas: los gestos de borde empiezan en el borde recto, no en la esquina.

## El botón BOOT

| Cuándo | Gesto | Qué hace |
|---|---|---|
| Pantalla apagada | Pulsación | Enciende la pantalla (sin hacer nada más) |
| En la esfera | Pulsación corta | Siguiente esfera |
| En una app o un menú | Pulsación corta | Volver |
| En la esfera | Pulsación larga (más de 0,8 s) | Activa o desactiva el **cambio automático** de esfera (cada 30 s). Salta las esferas que hayas quitado en la app |
| Suena una alarma | Pulsación | La para |
| En cualquier momento | Mantener 3 s | Aparece «10 S: OLVIDAR MÓVILES» |
| En cualquier momento | Mantener 10 s | **Olvida todos los móviles emparejados** |

## Ajustes rápidos

Desde el borde de arriba: **brillo** (arrastra la barra), **Bluetooth** (dice si hay un móvil conectado), **Silencio** (no molestar: ni alarmas ni llamadas suenan), **Siempre** (pantalla siempre encendida), **Linterna** y la **batería**.

## Apps

| App | Qué hace |
|---|---|
| **Esferas** | Todas las esferas, para elegir una |
| **Alarmas** | Hasta 6 alarmas, de una vez o con días de la semana. Suenan por el altavoz; agitar la muñeca las pospone 5 minutos y BOOT las para |
| **Pasos** | Podómetro con el acelerómetro: hoy (anillo hacia el objetivo, calorías y distancia) y, deslizando, los últimos 30 días |
| **Cronómetro** | Con vueltas; sigue contando aunque salgas de la app |
| **Música** | Lo que suena en el móvil, con reproducir, pausa, pistas y volumen (necesita la [[App Android]]) |
| **Linterna** | Pantalla blanca al máximo brillo; un toque la pasa a una luz roja tenue que no deslumbra de noche |
| **Archivos** | La tarjeta microSD: carpetas, archivos, borrar, formatear y [modo USB](#modo-usb) |
| **Ajustes** | Pantalla (brillo, modo noche, apagado, siempre encendida, levantar la muñeca), sonido (volumen, no molestar), esferas (segundos, cambio automático), idioma (español o inglés), objetivo de pasos, conexiones (Bluetooth, WiFi, olvidar móviles) y sistema (versión, memoria, batería) |

**Llamadas:** cuando suena el móvil, el reloj muestra quién llama y deja rechazarla o silenciarla. Hace falta la [[App Android]] con el permiso de notificaciones.

## La pantalla

- Se enciende con BOOT, con un toque o al **levantar la muñeca** (se puede desactivar en Ajustes), y se apaga sola tras el tiempo elegido (de 3 s a 10 min; 10 s por defecto). *Siempre encendida* la deja encendida.
- Apagada no dibuja nada y el panel queda en reposo, pero el reloj sigue conectado al móvil, contando pasos y vigilando las alarmas.
- **Modo noche:** de 23:00 a 07:00 baja el brillo al 10 %. Horas y brillo se cambian en Ajustes, en la app o en la página.

## Modo USB

En **Archivos**, el botón **USB** (pulsado dos veces) reinicia el reloj como un disco USB: al conectarlo al ordenador por el cable, la microSD aparece como una unidad más para copiar archivos. Se vuelve al reloj normal:

- pulsando **BOOT**,
- expulsando el disco en el ordenador, o
- desconectando el cable.

Sin tarjeta, el reloj lo dice y vuelve solo a los 4 segundos. Mientras está en modo USB no hay Bluetooth, WiFi ni consola serie.

## Conexiones

El WiFi y el Bluetooth funcionan a la vez:

- **Bluetooth:** la [[App Android]] se queda conectada mientras el reloj está cerca. Se anuncia como **Marea XXXX**.
- **WiFi:** la [[Página de control]] en la IP del reloj (sale en Ajustes → Conexiones), o en `http://192.168.4.1` desde su propia red **Marea** (clave `mareamarea`) si no tiene tu WiFi guardada.
- **Actualización:** un arranque especial, solo mientras se descarga una versión nueva; vuelve solo al acabar ([[Actualizaciones]]).

## La hora

- La pone el móvil al conectar y cada 30 minutos, o internet (NTP) por WiFi, con la zona horaria de Madrid y su horario de verano.
- La guarda el **reloj de hardware** de la placa, que sigue contando con el reloj apagado mientras quede batería.

## El tiempo

Lo usan la esfera **Tiempo** y otras. Los datos son de [Open-Meteo](https://open-meteo.com/): los trae el móvil en cada sincronización o el propio reloj por WiFi cada 10 minutos, y gana el más reciente. La ciudad se cambia en los ajustes de la esfera Tiempo (busca por nombre). Por defecto, Sevilla.

## Ajustes que se guardan

Esfera, brillo, velocidad de las animaciones, formato 24 h, segundos, cambio automático (y qué esferas entran), modo noche, tiempo de encendido, levantar para encender, idioma, silencio, volumen, objetivo de pasos, alarmas, color de la esfera Clásico y ciudad. Se guardan en la memoria flash 3 segundos después del último cambio.
