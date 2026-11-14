# Cambios

## Sin publicar: Marea (3.0.0)

### 3.0.0-beta.9 y app 0.8.0 (en preparación)

- **Introducción la primera vez**, en el reloj y en la app. El reloj: idioma, los gestos practicados, el QR de la app esperando a que el móvil se conecte (y entonces sigue solo) y «¡Listo!»; los relojes ya en uso no la ven. La app: idioma, emparejar con ayuda si falla, el segundo plano (con los pasos de Xiaomi) y las notificaciones, extras opcionales y unas preferencias del reloj. Los permisos se piden en su paso y no todos al abrir. Las dos se saltan y se repiten desde los ajustes.
- **Ajustes del reloj por secciones:** un menú (Pantalla, Sonido, Hora y esferas, General, Conexiones, Sistema y App del móvil) con lo que guarda cada una; BOOT vuelve al menú. Ahora también están en el reloj el formato de 24 h y la voz.
- **La app, desde el reloj:** *Ajustes → App del móvil* enseña un QR con el enlace a la última versión de la app para Android, por si se borra (no está en la Play Store).
- **Esferas nuevas:** *Disfruta* (un prado mecido por el viento que sigue la hora del día, con luciérnagas y fuegos artificiales de noche; sin hora, solo «Disfruta» o «Relax») y *El escultor* (la estatua de un corredor que se talla según los pasos de hoy, en once escenas hasta cumplir el objetivo).
- **Mis esferas:** en la app (*Esferas → Mis esferas*) se crean, cambian y borran esferas propias: un nombre, una foto o un GIF de la galería encuadrado a la pantalla, y la hora digital con fuente, tamaño, sitio, color, contorno o sombra, segundos y fecha, con vista previa exacta y *Probar en el reloj*. Se guardan en la microSD del reloj (hace falta una; la app avisa si no la hay) y el móvil guarda una copia para restaurarlas. Los GIF se repiten mientras la pantalla está encendida (hasta 40 fotogramas, 274×336). El reloj recibe archivos por Bluetooth a su tarjeta.
- **Editor de alarmas más seguro:** *Cancelar* y *Guardar*; *Borrar alarma* va aparte y pregunta antes. En Archivos, *Formatear* pasa a *Más…*.
- Arreglado: textos que se pisaban en Alarmas, Pasos, Cronómetro, Archivos y el lanzador.
- **La app sin el reloj:** una barra avisa en todas las secciones; lo que necesita el reloj se ve atenuado y, al tocarlo, explica que no está conectado (antes parecía cambiarse y se perdía); Inicio enseña lo último que se supo, y al reconectar se vuelve a leer todo. Sin reloj emparejado, Inicio es la bienvenida con los pasos.
- **Mis esferas:** los tramos del día van tras un interruptor; la fecha también se puede mover; **Probar en el reloj** funciona sin guardar (manda lo que ves a una esfera temporal y no toca la guardada), y guardar encima de una que ya estaba pide confirmación.
- **El reloj ya no se enciende al abrir la app:** las consultas que la app hace sola (estado, alarmas, lista de esferas, sincronización, archivos) no despiertan la pantalla; cambiar algo desde la app, sí.
- **Mis esferas, más completas:** tramos del día (cada uno con su imagen o GIF; cubren todo el día), reloj analógico clásico o con agujas que salen del borde además del digital, y nombre obligatorio que no puede repetir el de otra esfera.
- **Mis esferas:** el envío al reloj se ve en una ventana que bloquea la app hasta terminar; al reconectar, las que estaban solo en el móvil se mandan solas.
- Arreglado: al conectar, el reloj podía rechazar una orden por falta de búferes (mientras mandaba su estado); ahora deja búferes libres para las órdenes y la app reintenta.
- Arreglado: tras reiniciarse el reloj, la app podía no volver a conectar nunca (esperaba una respuesta del Bluetooth que Android no siempre da).
- Arreglado: en la app, el campo de la hora de las alarmas se salía de la tarjeta.
- **Alarmas de una vez, a la vista:** en la app, *Repetir* con *Una vez*, *Todos los días*, *Laborables* y *Fin de semana* además de los días sueltos; en el reloj, el editor dice en qué quedan los días (*una vez: se borra al pararla*, *laborables*…). Una alarma nueva es de una vez, en el reloj y en la app.
- **Tu esfera en la app:** con una de Mis esferas puesta, Inicio y Esferas enseñan su nombre y su miniatura. Mis esferas ya no entra en el cambio automático.
- Arreglado: en la app, los nombres largos de las esferas se cortaban; en el editor de alarmas del reloj, el título pisaba las flechas.
- **Lanzador en tres columnas:** todas las apps caben sin desplazar.
- **Cambiar de esfera con intención:** deslizar a los lados pide un gesto decidido y la esfera nueva enseña su nombre un momento; las primeras veces, unas asas recuerdan que se puede deslizar desde arriba y desde abajo.
- En la lista de alarmas, los días y «sin repetir» se leen más grandes.
- **Transiciones:** las pantallas del reloj se deslizan en la dirección del gesto (ajustes rápidos desde arriba, lanzador desde abajo, apps desde la derecha).
- **Ajustes rápidos rediseñados:** cada casilla con icono, nombre y estado; las activas, rellenas de dorado; tirador en la barra de brillo. *Siempre* pasa a *Pantalla*, *Buscar móvil* a *Buscar* y *Reconectar* a *Conectar*, todas a un mismo tamaño.
- **Volver paso a paso:** BOOT y el gesto desde el borde izquierdo cierran primero el diálogo o la pantalla interna (editor, carpeta, mes de Pasos) y luego salen a donde se abrió la app: el lanzador, los ajustes rápidos o la esfera. Los paneles se cierran con el gesto contrario al que los abrió.
- **App Esferas del reloj, en rueda:** una lista circular que da la vuelta, sigue con el impulso del dedo y se para en una esfera; la del centro sale grande con su grupo. Incluye al final las esferas propias de la microSD (Mis esferas).
- **Trueno y peluches**, esfera nueva: un día de un guitarrista de thrash metal en pixel art de 16 bits, con siete escenas animadas según la hora (dormido con su peluche rosa, el café al amanecer, la afinación, el garaje del ensayo, la furgoneta de gira, el concierto con la hora grande y la pizza en el sofá).
- **Coloso** sustituye a *Arena*: un reloj de arena del tamaño de una torre visto desde abajo; la arena tarda una hora en caer.
- **Engranajes**, rehecha: el interior de una torre del reloj gótica, con engranajes enormes, péndulo, cadenas y luz de luna.
- **Lluvia** responde al acelerómetro: las gotas resbalan hacia donde inclinas el reloj, y las que se van dejan sitio a gotas nuevas. (Se corrigió de paso la orientación de los ejes del acelerómetro.)
- Arreglado: con una captura de pantalla por WiFi lenta, el reloj podía reiniciarse.
- **Reconexión:** tras un corte la app hace un intento directo y luego deja a Android esperando al reloj, que conecta en cuanto vuelve a anunciarse (más rápido y con menos gasto que reintentar cada minuto); al abrir la app lo intenta en el acto. Inicio enseña qué está haciendo, con un botón **Reconectar**, y el reloj tiene la casilla **Reconectar** en los ajustes rápidos (se anuncia rápido 30 s).
- **Alarmas sin repetición:** una alarma sin días suena una vez y se borra al pararla (antes solo se apagaba).
- **Sin conexión, la app avisa:** un aviso en Inicio explica qué pasa; si el Bluetooth del móvil está apagado, pide activarlo con el diálogo de Android y reconecta en cuanto se enciende.
- **Restablecer ajustes** en el reloj y en la app, con confirmación; los valores de fábrica están en el manual.
- **Objetivo de pasos** editable en la app, y **calibración del podómetro** (graba el acelerómetro en la microSD; el archivo se guarda cada 15 s).
- **Podómetro más preciso:** contaba de más, sobre todo andando despacio (cada paso daba dos picos) y con la pantalla apagada, cuando leía solo la mitad de las muestras. Ahora el acelerómetro da 11 muestras por segundo, encienda o no la pantalla, el umbral es más alto, la media de fondo va por tiempo y cuenta tras 8 pasos seguidos. Con grabaciones reales: de 239 a unos 195 en 200 pasos contados, y 0 en 5 minutos sentado.
- Arreglado: en *Aventura* la fecha y el segundero pisaban la hora; en *Gol* el segundero también, y ya no sale el nombre de un jugador real; en *Pasos* el círculo pisaba el título; en el editor de alarmas las letras de los días se salían de sus casillas; la batería salía dos veces en los ajustes rápidos.


> **Marea: el reloj pasa a la Waveshare ESP32-S3-Touch-AMOLED-2.06.** Pantalla AMOLED táctil de 410×502, batería, reloj de hardware, acelerómetro, altavoz y microSD. El ESP32-C6 deja de mantenerse (su última versión es la 2.1.0).

- **Sistema nuevo** con el estilo *Marea Solsticial*: lanzador de apps (deslizar desde abajo), ajustes rápidos (desde arriba), selector de esferas (mantener el dedo) y volver deslizando desde la izquierda o con BOOT. Textos en español o inglés.
- **Apps:** Esferas, Alarmas (por el altavoz; se posponen al agitar la muñeca), Pasos (podómetro con objetivo diario), Cronómetro, Música, Linterna, Archivos y Ajustes. Llamadas entrantes en la muñeca: quién llama, rechazar o silenciar.
- **Todas las esferas a pantalla completa**, siguiendo las esquinas redondeadas y a 30 fps. Las que eran horizontales (Vida, HUD, Aventura, Estrellada, WiFi, Earthrise, Sakura, Synthwave, Agujero negro y Gol) pasan a verticales. Clásico usa la pantalla como caja; Feria, RPG, Túnel y Agujero negro cambian su composición.
- **App 0.4.0: conexión permanente.** Mientras el reloj está cerca, la app se queda conectada y vuelve a conectar sola; sincroniza la hora y el tiempo al conectar y cada 30 minutos. Con permiso para leer notificaciones, el reloj muestra y controla la música del móvil y avisa de las llamadas.
- **Modo USB:** el botón USB de Archivos reinicia el reloj como disco USB con la microSD; se vuelve con BOOT, expulsando el disco o desconectando el cable.
- **Alarma con amanecer:** mientras suena, el sol sale del mar en pixel art y el cielo pasa de la noche al oro; al posponer se hunde y al parar termina de salir.
- Arreglado: la pantalla no se encendía tras el primer apagado (3.0.0-beta.3).
- **Batería (3.0.0-beta.6):** el reloj gastaba alrededor de un 1 % por minuto incluso sin usarlo. Ahora:
  - El **WiFi viene apagado** (casilla *WiFi* en los ajustes rápidos y en la app). Antes, fuera de casa, buscaba la red sin parar.
  - Con la pantalla apagada y sin cable, el procesador **duerme**, con el Bluetooth en bajo consumo.
  - La pantalla ya no se enciende cada vez que el móvil se conecta o cambia la canción.
  - **Apagado seguro:** aviso al 10 %; por debajo de 3,30 V guarda los ajustes y se apaga. Se enciende con PWR.
  - Al enchufarlo, el ordenador vuelve a reconocer su USB aunque estuviera dormido.
- **Alarmas con etiqueta y animación:** desde la app (tarjeta Alarmas) se pone a cada alarma una etiqueta, que se ve en el reloj, y la animación con la que suena. Nueva animación **Bomba**: cuenta atrás con cables y unos alicates que no se atreven a cortar; +5 min reinicia la cuenta y Parar la desactiva en verde. El botón de posponer dice «+5 min».
- **Velocidad por esfera:** cada esfera guarda su propia velocidad de animación (75 % por defecto); el control de la app ajusta la esfera que se ve.
- **Iconos nuevos en la barra de estado:** la microSD (coral con «!» si no se puede leer) y el USB cuando el cable va a un ordenador.
- **microSD en exFAT:** el reloj lee tarjetas FAT32 y exFAT y formatea en exFAT. Una tarjeta ilegible se puede formatear desde Archivos.
- Arreglado: tras usar el modo USB, el reloj podía quedarse con el USB a medias. Su registro se quedaba esperando a un puerto que nadie leía, el Bluetooth dejaba de responder y la batería se vaciaba en una noche. Ahora devuelve el USB a la consola al arrancar y, sin ordenador o mientras duerme, no escribe el registro.
- Batería en reposo: con la pantalla apagada, el reloj comprueba sensores 10 veces por segundo (antes 20) y un toque lo despierta al instante. Pide al móvil un intervalo de conexión lento y apaga el códec de audio cuando no suena nada.
- Arreglado: al expulsar el disco en el ordenador, el modo USB no volvía solo al reloj.
- Arreglado: con una app abierta, la esfera se reiniciaba 30 veces por segundo por detrás (gasto de batería inútil).
- Arreglado: la página ofrecía versiones más viejas que la instalada; la app comparaba mal las betas.
- **Actualizaciones sin WiFi en el reloj (3.0.0-beta.6, app 0.5.0):** el móvil descarga la versión nueva y se la pasa al reloj por Bluetooth, que la instala mientras sigue funcionando, con una pantalla de progreso. Funciona en cualquier sitio con los datos del móvil. Los relojes anteriores siguen actualizándose por su WiFi. Desde el PC: `tools/ble_ota.py`.
- **App y página nuevas, con el estilo del reloj (3.0.0-beta.7, app 0.6.0)** (*Marea Solsticial*: pixel art nocturno, fuentes Jersey 10 y VT323). En vez de una lista larguísima, **Inicio** con los avisos que piden algo (versión nueva del reloj o de la app, permiso que falta), la esfera que se ve y cuatro secciones:
  - **Esferas:** vista previa de cada una; el **ojo** la incluye o la saca del cambio automático. La esfera actual reúne su velocidad, su panel táctil y sus opciones.
  - **Alarmas:** la etiqueta se ve sin abrirlas, cada una tiene interruptor, y **Animación de alarma** abre una ventana con la vista previa de cada animación.
  - **Archivos:** para mirar la microSD desde el móvil: carpetas, tamaños y fechas (no abre ni borra).
  - **Ajustes:** Idioma (español o inglés, en el reloj y en la app a la vez), Pantalla, Hora, Móvil y conexión, Actualizaciones y Avanzado (donde pasa *Sincronizar hora*, que ya se hace sola).
  - El botón atrás del móvil recorre las secciones antes de salir.
- **Animación de encendido:** al encender el reloj desde apagado, el mar de noche se ilumina a saltos, la luna sale por detrás de la isla del faro con su reflejo y caen las letras de «Marea», hasta la pantalla de bienvenida. En los reinicios no sale.
- **Botón PWR:** una pulsación corta apaga o enciende la pantalla (y para una alarma); mantenerlo enseña «¿Apagar?» con una barra y a los 3 s el reloj guarda todo y se apaga. El corte forzado del chip de alimentación pasa de 6 a 10 s.
- **Orden de las actualizaciones:** si hay versión nueva del reloj y de la app a la vez, la app solo deja pulsar la que va primero (el reloj, salvo que la versión se publique con `--first app`).
- **Órdenes de voz (3.0.0-beta.8, app 0.7.0):** con la opción activa, mantener BOOT graba con los micrófonos del reloj y el móvil lo entiende en el idioma del reloj (español o inglés, sin internet si el idioma está descargado). El reloj enseña lo que ha entendido y pregunta antes de hacerlo. Sabe crear alarmas, cambiar de esfera, activar o desactivar no molestar, WiFi, modo noche, pantalla siempre encendida y cambio automático, poner brillo y volumen y abrir apps. Necesita Android 13 y el permiso de micrófono (la app no graba con el del móvil).
- **Buscar el reloj y el móvil:** desde la app, el reloj suena al máximo (aunque esté en *No molestar*) y su pantalla parpadea hasta que se para en el reloj, en la app o a los 5 minutos; la app dice lo cerca que está por la señal Bluetooth y cuándo lo vio por última vez. Desde los ajustes rápidos del reloj, *Buscar móvil* hace sonar el móvil aunque esté en silencio.
- **Al enchufar el USB**, la pantalla se enciende y dice si está cargando, cargando y con el ordenador, o solo con el ordenador.
- **Novedades en Inicio:** un tablón con lo que trae cada versión del reloj y de la app, de la más reciente a la más antigua.
- **Alarmas desde la app (3.0.0-beta.8, app 0.7.0):** además de la etiqueta y la animación, se crean, se cambian de hora y de días y se borran desde la app o la página.
- **Tocar para encender:** ajuste nuevo en el reloj y en la app (Pantalla). Con la pantalla apagada, *un toque* la enciende (como hasta ahora), *doble toque* pide dos seguidos para evitar encendidos sin querer, y *no* deja solo los botones y la muñeca.
- **Barra de estado:** el Bluetooth parpadea mientras el reloj busca la app, y un icono de **WiFi** sale cuando está conectado a tu red.
- Al elegir una esfera en la app, el reloj vuelve a la esfera aunque estuviera en un menú.
- Arreglado (app 0.7.0): la app podía decir «conectado» sin estarlo. Android cerraba la conexión por dentro sin avisar y nada llegaba al reloj (ni el cambio de esfera); ahora la app lo detecta en unos segundos y vuelve a conectar.
- Arreglado: el aviso de apagado con PWR se salía de su caja; la pantalla de arranque decía «conectando a …» con el WiFi apagado (no se conectaba, solo lo decía).
- Arreglado (3.0.0-beta.7): abrir Archivos en la app podía reiniciar el reloj. La tarea de Bluetooth que lee la microSD se quedaba sin pila; ahora tiene el doble.
- Arreglado (app 0.4.1): la llamada entrante desaparecía del reloj al segundo en móviles como los Xiaomi, que cambian la notificación mientras suena. Ahora se queda hasta que se contesta, cuelga quien llama, se rechaza o silencia, o se corta el Bluetooth.
- **Esfera nueva: Ónix** (número 24). Analógica minimalista para AMOLED: negro puro, índices finos, agujas cálidas y una estela de luz como segundero. El color del acento se elige en la app (ajuste `ohue`) y el dibujo se desplaza un poco cada minuto para cuidar la pantalla.
- La barra de estado muestra el porcentaje de batería, y un rayo sobre el icono mientras está enchufado; los ajustes rápidos dicen «cargando» o «cargada».
- El tiempo que manda el móvil siempre gana sobre la descarga propia del reloj (es el más reciente).
- La hora se guarda en el reloj de hardware (PCF85063) y se conserva con el reloj apagado. WiFi y Bluetooth funcionan a la vez, sin modos que cambiar. La pantalla se enciende al levantar la muñeca.
- Red del punto de acceso: **Marea** (clave `mareamarea`); Bluetooth: **Marea XXXX**.
- Para crear esferas: `tools/new_face.py` hace esferas a pantalla completa (lienzo ancho o `--native`).

### Detalles de la etapa de paso a la S3

> **Esferas retiradas: El grito, Acuario y Cuento.** Quedan 21 esferas. Sus números (10, 16 y 20) no se reutilizan; si un reloj tenía elegida una de ellas, pasa a la siguiente.

- Placa ESP32-S3-Touch-AMOLED-2.06 (en desarrollo, ver `docs/PLAN-s3.md`): el firmware completo funciona en modo compatibilidad, y la hora se guarda en su reloj de hardware (PCF85063), que la conserva con el reloj apagado. En esta placa desaparece la espera de 60 s del Bluetooth al arrancar. Su pantalla táctil enciende la pantalla, cambia de esfera al deslizar el dedo y mueve las esferas interactivas. Usa el WiFi y el Bluetooth a la vez, sin modos que cambiar. La página de control muestra su batería; la pantalla se enciende al levantar la muñeca (se puede desactivar) y el panel se pone en reposo cuando está apagada.
- Actualizaciones por placa: el manifiesto conserva las claves del C6 y añade `boards.s3amoled`; el instalador web elige la imagen según el chip; la página y la app ofrecen la versión de la placa del reloj. El estado y el mensaje `info` dicen la placa (`board`).
- El catálogo de esferas (`/faces`, `get faces`) sigue teniendo una entrada por número; las retiradas llevan `"x":1` y grupo `-1`, y ninguna versión de la app ni de la página las muestra.

## 3.0.0 — preparada, sin publicar

Se publicará con `tools/release.py` cuando la app esté probada en un móvil real (`tests/MANUAL.md`). Antes de publicarla:

- `PROJECT_VER` a 3.0.0 en `CMakeLists.txt` y `versionName` a 1.0.0 en `android/app/build.gradle.kts`.
- `versionCode` de la app mayor que el publicado (la app solo se actualiza a un `versionCode` superior; `release.py` lo escribe en el bloque `app` del manifiesto).
- `BOOT_DEFAULT` a `BOOT_BLE` en `src/bootmode.h`. Solo afecta a las instalaciones nuevas, porque los relojes ya instalados conservan su modo.

Notas para la release:

> **Esferas que no salen solas.** En la app, *Esferas del cambio automático → Elegir* permite quitar esferas del cambio automático; siguen disponibles a mano. Ajuste `fxoff`.
>
> **Esfera nueva: Feria de Sevilla.** Cuenta atrás hasta el alumbrado de la Feria de Abril, con fuegos artificiales durante la Feria. La fecha se calcula sola cada año.

> **El reloj se controla desde el móvil.** Nueva app Android (*C6 Clock*): cambia de esfera y ajustes por Bluetooth, sin WiFi, y cada 30 minutos le pone la hora y el tiempo aunque la app esté cerrada. Se empareja una vez con la clave que aparece en la pantalla del reloj.
>
> - Al encender el reloj o al volver a estar cerca, el móvil le pone la hora en menos de un minuto, sin esperar a la siguiente sincronización (Android 12 o superior).
> - Al emparejar, una barra sobre la clave cuenta los 30 segundos que hay para escribirla.
> - La app avisa también de sus propias versiones nuevas y se actualiza sola (*Actualizaciones → Actualizar la app*), sin desinstalar ni perder el emparejamiento.
> - Las actualizaciones llegan por internet: *Actualizaciones → Buscar actualización*, en la app o en la página. Si algo falla, el reloj se queda con la versión que tenía.
> - El modo WiFi sigue disponible: BOOT 2 s en la pantalla de bienvenida.
> - ¿Quieres hacer tu propia esfera? `tools/new_face.py` crea una que ya funciona, y la wiki tiene la guía completa: [Crear una esfera](https://github.com/CarlosNS/c6-fx-clock/wiki/Crear-una-esfera).
> - Instalación por USB: <https://carlosns.github.io/c6-fx-clock/flasher/>

## 2.1.0 — 2026-11-03

- **Actualización por internet:** el reloj descarga el firmware de GitHub Pages y comprueba proyecto, tamaño y SHA-256. Funciona en modo WiFi y en modo Bluetooth (la orden llega desde la página o la app).
- **App Android** (primera versión, 0.1.0): la página de control dentro de la app, emparejamiento con el sistema y sincronización periódica.
- **Web Bluetooth:** la página de control funciona también desde Chrome o Edge por Bluetooth.
- **WiFi en redes malladas:** si un nodo no da IP, el reloj prueba con el siguiente.
- La red WiFi se puede guardar también en modo Bluetooth.

## 2.0.0

- Nueva tabla de particiones de 8 MB con dos huecos para OTA y vuelta atrás automática.
- Actualización por WiFi subiendo el archivo desde la web.
- Capa de control independiente del transporte (`control.c`, `settings.c`), preparada para el Bluetooth.
