La app **C6 Clock** es la forma normal de usar el reloj: cambia esferas y ajustes por Bluetooth y, sin que tengas que abrirla, le pone la hora y el tiempo.

- Requiere **Android 10 o superior**. Algunas funciones (sincronizar al encender el reloj) necesitan Android 12+.
- No está en Google Play: se instala con el APK de las [releases](https://github.com/CarlosNS/c6-fx-clock/releases) y después **se actualiza sola**.
- El reloj tiene que estar en **modo Bluetooth** (ver [[Uso del reloj]]).

## Instalar

1. En el móvil, descarga `c6-clock-app-….apk` de la última release y ábrelo.
2. Android avisará de que es una app de fuera de la tienda: permite instalar desde el navegador o el gestor de archivos que estés usando.
3. Abre **C6 Clock** y acepta **Dispositivos cercanos** (para hablar con el reloj) y **Notificaciones** (para avisarte de versiones nuevas). La app no pide ubicación: el emparejamiento lo hace el propio Android.

## Emparejar

Hazlo **desde la app**, no desde los ajustes de Bluetooth del móvil.

1. Ten el reloj encendido y cerca, en modo Bluetooth.
2. En la app, pulsa **Buscar el reloj**. Android enseña su lista de dispositivos: elige **C6 Clock XXXX** (las 4 letras son el final de su dirección).
3. El reloj enciende la pantalla y muestra una **clave de 6 cifras**. Escríbela en el móvil cuando te la pida.
4. **Tienes 30 segundos** desde que aparece la clave: es un límite del propio Bluetooth. La barra morada de encima de la clave se acorta y se pone roja en los 10 últimos. Si se pasa el tiempo, vuelve a pulsar *Buscar el reloj*.
5. Al terminar aparecen las esferas.

El reloj recuerda hasta **3 móviles**. Para olvidarlos todos: BOOT 10 s, o *Este móvil → Olvidar este reloj* en la app (olvida ese móvil en los dos lados).

## Dejar que funcione en segundo plano

Sin esto, Android puede cortar la sincronización cuando cierras la app.

- En la app: *Este móvil → Funcionar en segundo plano → Permitir*.
- **Xiaomi / HyperOS / MIUI:** *Ajustes → Apps → C6 Clock*: activa **Inicio automático** y pon **Ahorro de batería → Sin restricciones**.
- **Samsung:** *Ajustes → Batería → Límites de uso en segundo plano*: que C6 Clock no esté en «Apps en suspensión».
- **Otros:** busca en <https://dontkillmyapp.com> las instrucciones de tu marca.

## Qué hace sola

| Cuándo | Qué |
|---|---|
| Al abrir la app | Conecta, pone la hora y busca actualizaciones del reloj y de la propia app |
| Cada 30 minutos, aunque esté cerrada | Conecta unos segundos: hora, tiempo de la ciudad del reloj, y comprueba versiones nuevas |
| Al encender el reloj o volver a tenerlo cerca (Android 12+) | Android avisa a la app y esta le pone la hora en segundos |
| Hay firmware o app nuevos | Notificación (una por versión) |

La tarjeta **Este móvil** enseña cuándo fue la última sincronización y si salió bien, y tiene el botón **Ahora** para forzar una.

## Actualizaciones

En la tarjeta **Actualizaciones** la app muestra las dos versiones:

- **Reloj:** *Actualizar a la X* manda al reloj descargarla por su WiFi (ver [[Actualizaciones]]).
- **App:** *Actualizar la app a la X* la descarga, comprueba su tamaño y su SHA-256 y abre el instalador de Android, que pide confirmación. La primera vez Android pide además permitir que C6 Clock instale apps («Permitir de esta fuente»). Solo acepta versiones firmadas con la misma clave, y se conserva el emparejamiento.

## Privacidad

La app solo habla con el reloj, con [Open-Meteo](https://open-meteo.com/) (tiempo de la ciudad configurada en el reloj; no usa tu ubicación) y con GitHub Pages (para buscar versiones nuevas). No tiene cuentas, anuncios ni analítica.

## Si algo falla

Ver [[Solución de problemas]].
