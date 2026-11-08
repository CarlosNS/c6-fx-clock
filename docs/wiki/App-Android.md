La app **Marea** es la forma normal de usar el reloj: se queda conectada por Bluetooth mientras el reloj está cerca, le pone la hora y el tiempo, le pasa la música y las llamadas del móvil y permite cambiar esferas y ajustes.

- Requiere **Android 10 o superior**. La conexión que arranca sola al acercar el reloj necesita Android 12+.
- No está en Google Play: se instala con el APK de las [releases](https://github.com/CarlosNS/c6-fx-clock/releases) y después **se actualiza sola**.

## Instalar

1. En el móvil, descarga el APK de la app de la última release y ábrelo.
2. Android avisará de que es una app de fuera de la tienda: permite instalar desde el navegador o el gestor de archivos que estés usando.
3. Abre **Marea** y acepta **Dispositivos cercanos** (para hablar con el reloj) y **Notificaciones** (para el aviso de conexión y las versiones nuevas). La app no pide ubicación: el emparejamiento lo hace el propio Android.

## Emparejar

Hazlo **desde la app**, no desde los ajustes de Bluetooth del móvil.

1. Ten el reloj encendido y cerca.
2. En la app, pulsa **Buscar el reloj**. Android enseña su lista de dispositivos: elige **Marea XXXX** (las 4 letras son el final de su dirección).
3. El reloj enciende la pantalla y muestra una **clave de 6 cifras**. Escríbela en el móvil cuando te la pida.
4. **Tienes 30 segundos** desde que aparece la clave: es un límite del propio Bluetooth. La barra de encima de la clave se acorta. Si se pasa el tiempo, vuelve a pulsar *Buscar el reloj*.
5. Al terminar aparecen las esferas.

El reloj recuerda hasta **3 móviles**. Para olvidarlos todos: BOOT 10 s, *Ajustes → Olvidar móviles* en el reloj, o *Este móvil → Olvidar este reloj* en la app (olvida ese móvil en los dos lados).

## La conexión permanente

Mientras el reloj está cerca, la app mantiene el enlace Bluetooth abierto, aunque la cierres. Android lo muestra con un aviso fijo, discreto, **«Marea conectado»** (o «Buscando el reloj…» mientras reconecta), como hacen las apps de los relojes comerciales.

| Cuándo | Qué hace |
|---|---|
| El reloj aparece cerca o se enciende (Android 12+), o abres la app | Conecta y sincroniza: hora, tiempo de la ciudad del reloj, versión |
| Mientras sigue conectada | Repite la sincronización cada 30 minutos y pasa al reloj la música y las llamadas al momento |
| Se corta (el reloj se aleja o se reinicia) | Vuelve a conectar sola, con esperas que crecen hasta 1 minuto |
| El reloj desaparece del todo | Deja de intentarlo hasta que Android lo vuelva a ver |
| Hay firmware o app nuevos | Notificación (una por versión) |

Con la app en pantalla el enlace va más rápido (para la página de control); en segundo plano usa el intervalo de bajo consumo del Bluetooth.

La tarjeta **Este móvil** enseña cuándo fue la última sincronización y si salió bien, y tiene el botón **Ahora** para forzar una.

## Música y llamadas

En la tarjeta **Este móvil**, *Música y llamadas en el reloj → Permitir* abre la pantalla de Android para dejar que la app **lea las notificaciones**. Con eso:

- **Música:** el reloj (app Música) muestra el título, el artista y si suena, de cualquier app de música o podcasts del móvil, y la controla: reproducir, pausa, pista anterior y siguiente, y volumen.
- **Llamadas:** cuando suena el móvil, el reloj muestra quién llama y suena por su altavoz (no tiene motor de vibración), salvo con *No molestar*. Desde la muñeca se puede **rechazar** (pulsa el botón de rechazar de la propia llamada) o **silenciar** el timbre durante un minuto.

La app no pide permisos de teléfono ni de contactos: el nombre de quien llama lo saca de la notificación de la llamada. No guarda ni envía nada fuera del reloj.

## Dejar que funcione en segundo plano

La conexión permanente es un servicio en primer plano, que Android respeta, pero algunas marcas lo cortan igualmente.

- En la app: *Este móvil → Funcionar en segundo plano → Permitir*.
- **Xiaomi / HyperOS / MIUI:** *Ajustes → Apps → Marea*: activa **Inicio automático** y pon **Ahorro de batería → Sin restricciones**.
- **Samsung:** *Ajustes → Batería → Límites de uso en segundo plano*: que Marea no esté en «Apps en suspensión».
- **Otros:** busca en <https://dontkillmyapp.com> las instrucciones de tu marca.

## Actualizaciones

En la tarjeta **Actualizaciones** la app muestra las dos versiones:

- **Reloj:** *Actualizar a la X* manda al reloj descargarla por su WiFi (ver [[Actualizaciones]]).
- **App:** *Actualizar la app a la X* la descarga, comprueba su tamaño y su SHA-256 y abre el instalador de Android, que pide confirmación. La primera vez Android pide además permitir que Marea instale apps («Permitir de esta fuente»). Solo acepta versiones firmadas con la misma clave, y se conserva el emparejamiento.

## Privacidad

La app solo habla con el reloj, con [Open-Meteo](https://open-meteo.com/) (tiempo de la ciudad configurada en el reloj; no usa tu ubicación) y con GitHub Pages (para buscar versiones nuevas). Lo que lee de las notificaciones (música y llamadas) solo va al reloj. No tiene cuentas, anuncios ni analítica.

## Si algo falla

Ver [[Solución de problemas]].
