La app **Marea** es la forma normal de usar el reloj: se queda conectada por Bluetooth mientras el reloj está cerca, le pone la hora y el tiempo, le pasa la música y las llamadas del móvil y permite cambiar esferas y ajustes.

- Requiere **Android 10 o superior**. La conexión que arranca sola al acercar el reloj necesita Android 12+.
- No está en Google Play: se instala con el APK de las [releases](https://github.com/CarlosNS/c6-fx-clock/releases) y después **se actualiza sola**.

## Instalar

1. En el móvil, descarga el APK de la app de la última release y ábrelo.
2. Android avisará de que es una app de fuera de la tienda: permite instalar desde el navegador o el gestor de archivos que estés usando.
   Si la borraste y no sabes de dónde sacarla, el reloj te la da: *Ajustes → App del móvil* enseña un QR con el enlace a la última versión.
3. Abre **Marea**: la primera vez te guía una **introducción** de cinco pasos.
   1. **Hola:** qué hace la app y el idioma (el del reloj se cambia a la vez).
   2. **Empareja el reloj:** *Buscar el reloj* pide **Dispositivos cercanos** (para hablar con el reloj) y abre la lista de Android. Si falla, explica por qué puede ser: Bluetooth apagado (con un botón para encenderlo), el reloj emparejado con otro móvil o la app reinstalada (en el reloj, *Ajustes → Conexiones → Olvidar móviles*), o la clave caducada.
   3. **Que no se corte:** permitir el segundo plano, en Xiaomi, Redmi y POCO también el inicio automático y el ahorro de batería sin restricciones, y las **Notificaciones** (aviso de conexión y versiones nuevas).
   4. **Extras** opcionales: música y llamadas, órdenes de voz.
   5. **Tu reloj:** formato 24 h, objetivo de pasos y un atajo para elegir esfera.

   Cada permiso se pide en su paso, con su explicación, y no todos al abrir. Se puede saltar desde el primer paso y repetir en *Ajustes → Avanzado → Ver la introducción*. La app no pide ubicación: el emparejamiento lo hace el propio Android.

## Emparejar

Hazlo **desde la app**, no desde los ajustes de Bluetooth del móvil.

1. Ten el reloj encendido y cerca.
2. En la app, pulsa **Buscar el reloj**. Android enseña su lista de dispositivos: elige **Marea XXXX** (las 4 letras son el final de su dirección).
3. El reloj enciende la pantalla y muestra una **clave de 6 cifras**. Escríbela en el móvil cuando te la pida.
4. **Tienes 30 segundos** desde que aparece la clave: es un límite del propio Bluetooth. La barra de encima de la clave se acorta. Si se pasa el tiempo, vuelve a pulsar *Buscar el reloj*.
5. Al terminar aparecen las esferas.

El reloj recuerda hasta **3 móviles**. Para olvidarlos todos: BOOT 10 s, *Ajustes → Olvidar móviles* en el reloj, o *Ajustes → Móvil y conexión → Olvidar este reloj* en la app (olvida ese móvil en los dos lados).

## La conexión permanente

Mientras el reloj está cerca, la app mantiene el enlace Bluetooth abierto, aunque la cierres. Android lo muestra con un aviso fijo, discreto, **«Marea conectado»** (o «Buscando el reloj…» mientras reconecta), como hacen las apps de los relojes comerciales.

| Cuándo | Qué hace |
|---|---|
| El reloj aparece cerca o se enciende (Android 12+), o abres la app | Conecta y sincroniza: hora, tiempo de la ciudad del reloj, versión |
| Mientras sigue conectada | Repite la sincronización cada 30 minutos y pasa al reloj la música y las llamadas al momento |
| Se corta (el reloj se aleja o se reinicia) | Un intento directo (busca el reloj durante 20 s) y, si no lo encuentra, deja a Android **esperándolo**: en cuanto el reloj se anuncia otra vez, conecta solo, sin consultas periódicas y con poco gasto |
| El reloj se va lejos un rato | Sigue esperándolo de la misma forma y conecta en cuanto vuelve |
| Abres la app sin conexión | Vuelve a intentarlo en el acto |
| Hay firmware o app nuevos | Notificación (una por versión) |

Con la app en pantalla el enlace va más rápido (para la página de control); en segundo plano usa el intervalo de bajo consumo del Bluetooth.

Sin conexión, Inicio enseña un aviso en rojo a los pocos segundos. Si el **Bluetooth del móvil está apagado**, lo dice y abre el diálogo de Android para activarlo (una vez cada vez que se abre la app, y con el botón **Activar Bluetooth**); al activarlo, la app vuelve a conectar sola. Si no, dice qué está haciendo (*Buscando el reloj…*, *Esperando a que aparezca* o *No aparece: ¿está cerca y encendido?*) con un botón **Reconectar**, que cierra cualquier conexión a medias y busca el reloj en ese momento. En el reloj, la casilla **Reconectar** de los ajustes rápidos hace que se anuncie rápido durante 30 s, para que el móvil lo encuentre al instante.

**Mientras no hay conexión:**

- Una barra arriba de cada sección dice *Sin conexión con el reloj · reconectando…* y lleva a Inicio.
- Lo que vive en el reloj (esferas, alarmas, archivos, ajustes de pantalla y hora, objetivo de pasos, voz, WiFi del reloj, actualizarlo, sincronizar, restablecer, idioma) se ve **atenuado**: tocarlo avisa de que el reloj no está conectado, en vez de parecer que se cambia y perderse.
- Inicio enseña lo último que se supo (*La última vez en el reloj*), sin batería ni hora.
- Sigue funcionando lo que no necesita el reloj: Mis esferas (se guardan en el móvil), *Buscar el reloj* (sonará en cuanto conecte), los permisos y las actualizaciones de la app.
- Al volver la conexión, la app vuelve a leer todo del reloj (ajustes, alarmas, archivos, Mis esferas).

**Sin reloj emparejado** (la primera vez, o después de olvidarlo), Inicio es solo la bienvenida: los pasos para emparejarlo y el botón **Buscar el reloj**.

En *Ajustes → Avanzado*, **Sincronización del móvil** enseña cuándo fue la última y si salió bien, con el botón **Ahora** para forzar una.

La app es la misma [[Página de control]] que sirve el reloj: Inicio con los avisos, Esferas con vistas previas, Alarmas, Archivos de la microSD y Ajustes. El botón atrás del móvil vuelve por las secciones antes de salir.

## Música y llamadas

En *Ajustes → Móvil y conexión*, *Música y llamadas → Permitir* (Inicio avisa si falta) abre la pantalla de Android para dejar que la app **lea las notificaciones**. Con eso:

- **Música:** el reloj (app Música) muestra el título, el artista y si suena, de cualquier app de música o podcasts del móvil, y la controla: reproducir, pausa, pista anterior y siguiente, y volumen.
- **Llamadas:** cuando suena el móvil, el reloj muestra quién llama y suena por su altavoz (la placa no tiene motor de vibración), salvo con *No molestar*. La pantalla se queda mientras dure el timbre y se quita al contestar, al colgar quien llama, al rechazar o silenciar, o si se corta el Bluetooth. Desde la muñeca se puede **rechazar** (pulsa el botón de rechazar de la propia llamada) o **silenciar** el timbre durante un minuto.

**Si Android dice «Se ha denegado el acceso»** (o «ajuste restringido»): desde Android 13, las apps instaladas fuera de Google Play no pueden pedir este permiso hasta que lo autorizas en su ficha. Ve a *Ajustes → Aplicaciones → Marea*, toca **⋮** arriba a la derecha → **Permitir ajustes restringidos**, confírmalo y vuelve a pulsar *Permitir* en la app. Solo hace falta una vez; el botón de la app lo explica y abre esa ficha.

La app no pide permisos de teléfono ni de contactos: el nombre de quien llama lo saca de la notificación de la llamada. No guarda ni envía nada fuera del reloj.

## Dejar que funcione en segundo plano

La conexión permanente es un servicio en primer plano, que Android respeta, pero algunas marcas lo cortan igualmente.

- En la app: *Ajustes → Móvil y conexión → Funcionar en segundo plano → Permitir*.
- **Xiaomi / HyperOS / MIUI:** *Ajustes → Apps → Marea*: activa **Inicio automático** y pon **Ahorro de batería → Sin restricciones**.
- **Samsung:** *Ajustes → Batería → Límites de uso en segundo plano*: que Marea no esté en «Apps en suspensión».
- **Otros:** busca en <https://dontkillmyapp.com> las instrucciones de tu marca.

## Mis esferas

En *Esferas*, arriba, **Mis esferas** deja crear, cambiar y borrar esferas propias. Es solo de la app (no está en la página por WiFi) y necesita una **microSD** en el reloj; sin ella, la app lo avisa y no deja guardar.

1. **+ Nueva esfera** (o *Editar* en una que ya tengas) y ponle un **nombre**: es obligatorio y no puede repetir el de ninguna esfera, ni de serie ni tuya.
2. **Elegir imagen o GIF** abre la galería del móvil. En *Encuadrar fondo*, arrastra para mover y pellizca para acercar; las esquinas oscurecidas son lo que tapa la pantalla redondeada.
3. **Tramos del día** (opcional, con su interruptor; apagado, la sección no aparece y la esfera usa una sola imagen): con **+ Tramo** la esfera cambia de imagen a lo largo del día, como *Trueno y peluches*. Cada tramo tiene su hora de inicio (*Desde*) y su propia imagen o GIF con su encuadre: elige el tramo y luego su imagen. El primero empieza a las 00:00 y cada uno dura hasta que empieza el siguiente, así que todo el día queda cubierto; un tramo sin imagen sale marcado con «!» y no deja guardar. Hasta 8 tramos.
4. En *La hora*, el **reloj**:
   - **Digital:** **fuente** (Redonda, Segmentos, Caligráfica, Píxel o Jersey), **tamaño** y sitio (*Mover la hora*).
   - Con la **fecha** marcada, *Mover la fecha* la lleva a donde quieras (si no la mueves, va en su sitio: bajo la hora, bajo el centro del clásico o en el centro con el de borde).
   - **Clásico:** agujas desde el centro, con marcas y segundero; el tamaño cambia el radio y *Mover la hora* lo coloca donde quieras.
   - **Borde:** las agujas salen del borde de la pantalla hacia el centro sin llegar, para que se vea el fondo; el segundero es un punto que recorre el borde. El tamaño cambia su grosor.
   - En los tres: **color**, **contorno** o **sombra** (o nada) para que se lea sobre cualquier fondo, **segundos** y **fecha**.
5. La vista previa es exactamente lo que verá el reloj. **Probar en el reloj** la enseña 8 segundos sin guardar nada: si has cambiado algo, se manda a una esfera temporal del reloj (no aparece en ninguna lista y se borra al salir del editor), así la que tenías guardada no se toca; si no has cambiado nada, enseña la guardada. Si sales sin guardar, los cambios se pierden.
6. **Guardar en el reloj** la manda por Bluetooth (unos segundos una foto; un GIF, hasta un minuto; al cambiar una esfera solo se mandan las imágenes que cambiaron) y guarda una copia en el móvil. Si cambias una que ya estaba en el reloj, pregunta antes de sobrescribirla. **Ponerla** la deja como esfera del reloj.

- **Los GIF** se reducen a 274×336 píxeles (en el reloj se ven a pantalla completa, algo menos nítidos que una foto), hasta 40 fotogramas y unos 10 por segundo; si el GIF tiene más, la app avisa de que se ha reducido. Como mucho ocupan 3 MB. Se repiten mientras la pantalla está encendida.
- **La lista** dice dónde está cada una: *en el reloj*, *solo en el móvil* o *solo en el reloj*, y cuál está *puesta*.
- **Eliminar** la borra del reloj y del móvil.
- **Restaurar en el reloj** vuelve a mandar todas las que guarda el móvil, por ejemplo tras cambiar o formatear la tarjeta.
- **Enviar al reloj** se hace en una ventana con el progreso que tapa la app: mientras dura no se puede cambiar nada (ni volver atrás), para no estropear el envío.
- **Al reconectar**, las esferas que estaban solo en el móvil (guardadas sin el reloj cerca, o tras cambiar la tarjeta) se mandan solas, una vez por conexión.

## Buscar el reloj

En Inicio, la tarjeta **Buscar el reloj** dice si está conectado o cuándo se vio por última vez, y **Hacer sonar** lo hace sonar y parpadear (ver [[Manual del reloj]]). Mientras suena, la señal Bluetooth dice lo cerca que está; **Parar** lo calla. Si el reloj no está a tiro, la petición espera y sonará en cuanto conecte.

Al revés, la casilla **Buscar móvil** del reloj hace sonar el móvil con su sonido de alarma al máximo (también en silencio) y vibrar, con una notificación para **Parar**; la app vuelve a dejar el volumen de alarma como estaba.

## Órdenes de voz

Con *Órdenes de voz* activas (en *Ajustes* de la app o del reloj), al mantener BOOT el reloj graba con sus micrófonos y manda el audio a la app. La app lo pasa al reconocimiento de voz de Android, en el idioma del reloj, y le devuelve al reloj lo que ha entendido para que pregunte antes de hacerlo (ver [[Manual del reloj]]).

- **Android 13 o posterior.** El reconocedor tiene que aceptar audio que no viene del micrófono del móvil; en el POCO F7 funciona el de Google, sin internet una vez descargado el idioma.
- **Permiso de micrófono.** Android lo exige para reconocer voz aunque el audio sea del reloj: la app **no graba con el micrófono del móvil**. Por eso el servicio de conexión permanente se declara también como servicio de micrófono, y Android solo deja añadirlo con la app en pantalla: tras instalar o actualizar la app, ábrela una vez.
- **Idioma.** Si falta el paquete del idioma, la app pide a Android que lo descargue y el reloj avisa de que se pruebe en unos minutos.
- El audio solo va del reloj a la app y al reconocedor del móvil; no se guarda.

## Actualizaciones

En *Ajustes → Actualizaciones* la app muestra las dos versiones, e Inicio avisa cuando hay una nueva:

- **Reloj:** *Actualizar a la X* descarga la versión en el móvil y se la pasa al reloj por Bluetooth, así que el reloj no necesita WiFi y funciona en cualquier sitio (relojes desde la 3.0.0-beta.6; los anteriores la descargan con su WiFi). Deja la app abierta unos minutos. Ver [[Actualizaciones]].
- **App:** *Actualizar la app a la X* la descarga, comprueba su tamaño y su SHA-256 y abre el instalador de Android, que pide confirmación. La primera vez Android pide además permitir que Marea instale apps («Permitir de esta fuente»). Solo acepta versiones firmadas con la misma clave, y se conserva el emparejamiento.

## Privacidad

La app solo habla con el reloj, con [Open-Meteo](https://open-meteo.com/) (tiempo de la ciudad configurada en el reloj; no usa tu ubicación) y con GitHub Pages (para buscar versiones nuevas y las novedades). El audio de las órdenes de voz solo pasa por el reconocedor de voz del propio móvil. Lo que lee de las notificaciones (música y llamadas) solo va al reloj. No tiene cuentas, anuncios ni analítica.

## Si algo falla

Ver [[Solución de problemas]].
