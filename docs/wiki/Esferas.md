El reloj trae **29 esferas**, todas dibujadas por código a 30 fps y a pantalla completa, siguiendo las esquinas redondeadas de la pantalla. Además, con la app del móvil puedes hacer **las tuyas** con una foto o un GIF: ver [Mis esferas](#mis-esferas).

- Se cambian **deslizando** a izquierda o derecha, con una **pulsación corta de BOOT** (las dos siguen el orden del selector: por grupos y al final todas las de *Mis esferas*), en la app **Esferas** del reloj (o manteniendo el dedo en la esfera), desde la app del móvil o desde la página; con *cambio automático* pasan solas cada 30 s. En la app, **Esferas del cambio automático → Elegir** deja quitar las que no quieras que salgan solas: siguen disponibles a mano.
- Las que tienen ajustes propios o son interactivas lo indican con los botones *Tocar* y *Opciones* de la sección Esferas de la app (ver [[Página de control]]).
- Las capturas son de la pantalla real (410×502, con sus esquinas), sacadas con `GET /screenshot`.
- ¿Echas alguna en falta? [[Crear una esfera]] explica cómo hacer la tuya.
- Los números 10, 16 y 20 están **retirados**: eran esferas eliminadas (El grito, Acuario y Cuento) y no se vuelven a usar, para que ningún reloj cambie de esfera al actualizar. Si un reloj tenía elegida una de ellas, muestra la siguiente.

Índice: [Efectos](#efectos) · [Videojuegos](#videojuegos) · [Arte](#arte) · [Naturaleza y espacio](#naturaleza-y-espacio) · [Relojes y útiles](#relojes-y-útiles) · [Mis esferas](#mis-esferas)

## Efectos

### Plasma

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx00.png" alt="Esfera Plasma" width="205">

*Número 0* — HH / MM enormes sobre un plasma dibujado a resolución nativa, oscurecido detrás de las cifras.

### Cubo 3D

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx01.png" alt="Esfera Cubo 3D" width="205">

*Número 1* — Hora bajo el cubo; el cubo da una vuelta rápida en cada minuto.

### Túnel

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx02.png" alt="Esfera Túnel" width="205">

*Número 2* — 7 segmentos del color complementario al túnel, en su fondo oscuro.

### Lava

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx03.png" alt="Esfera Lava" width="205">

*Número 3* — Dígitos oscuros con borde luminoso.

### Vida

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx04.png" alt="Esfera Vida" width="205">

*Número 4* — Horas encima de minutos, de células inmortales del juego de la vida; al cambiar el minuto, los viejos se disuelven en la simulación. Es interactiva: el dedo dibuja células.

### Synthwave

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx13.png" alt="Esfera Synthwave" width="205">

*Número 13* — Sol a franjas, rejilla de neón, palmeras, números cromados.

### Dunas

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx29.png" alt="Esfera Dunas" width="205">

*Número 29* — Un desierto hecho de puntos de luz sobre el negro de la AMOLED, visto con una cámara baja: las dunas avanzan despacio hacia ti, los puntos cercanos son grandes y los lejanos diminutos y se apagan con la distancia, las caras que miran a la luz brillan y las crestas tapan lo que hay detrás. El relieve sale del ruido Perlin, el mismo tipo de ruido con el que se generan los terrenos de muchos videojuegos. La hora flota en el cielo, hecha también de puntos, que se recolocan uno a uno al cambiar el minuto.

Es interactiva: al **pasar el dedo**, la arena (y el cielo) se ilumina por donde pasa y se apaga poco a poco; un **toque** lanza una onda sobre la arena. Mantener el dedo quieto sigue abriendo el escaparate.

En la app (*Esferas → Dunas → Opciones*) se elige:

- **Color:** Hielo (azul claro y azul, el de serie), Abismo, Aurora, Lavanda, Ámbar o Plata.
- **Dunas:** *Crestas* (marcadas), *Suaves* o *Curvas* (curvas de nivel que se mueven sobre la arena).
- **Hora:** *Grande*, *Pequeña* o *Al tocar* (solo aparece 5 s después de tocar la pantalla).
- **Puntos:** finos, normales o gruesos.
- **Brillo.** La velocidad es la de cualquier esfera (*Velocidad de esta esfera*).

### Mariposa

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx30.png" alt="Esfera Mariposa" width="205">

*Número 30* — Una partícula de luz sigue las ecuaciones de Lorenz, el ejemplo más famoso de caos: nunca pasa dos veces por el mismo sitio, pero siempre dibuja las mismas dos alas. Deja una estela que se apaga con el tiempo, en 3D sobre el negro de la AMOLED: lo lejano se ve más tenue y donde la estela se cruza brilla más. La figura gira sola, despacio. Cada hora la partícula vuelve a empezar desde otro punto y la figura se dibuja de nuevo.

Es interactiva: al **arrastrar** giras la figura (al soltar sigue girando un poco); un **toque** lanza otra partícula, de otro color, a una millonésima de la primera. Durante unos segundos van juntas y luego se separan: es el *efecto mariposa*. Dura 45 segundos. Mantener el dedo quieto sigue abriendo el escaparate.

En la app (*Esferas → Mariposa → Opciones*) se elige:

- **Atractor:** *Lorenz* (la mariposa, el de serie), *Aizawa* (una esfera atravesada por un tubo), *Thomas* (un laberinto que se enreda) o *Halvorsen* (tres lazos).
- **Color:** Hielo, Aurora, Fuego, Lavanda o Plata.
- **Hora:** *Arriba*, *Abajo* o *Al tocar* (aparece 5 s después de tocar la pantalla).
- **Estela:** de 600 a 4.000 puntos.
- **Brillo.**

### Corriente

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx31.png" alt="Esfera Corriente" width="205">

*Número 31* — Miles de partículas siguen las corrientes de un campo de ruido Perlin que cambia despacio y dejan trazos finos de luz sobre el negro de la AMOLED, como seda o humo. Al cruzar los números van más despacio y dejan más luz, y los números tienen un brillo suave propio: la hora aparece **tejida** en el propio flujo, y al cambiar el minuto el flujo borra la vieja y teje la nueva.

Es interactiva: al **arrastrar** se forma un remolino alrededor del dedo; un **toque** suelta un puñado de partículas brillantes. Mantener el dedo quieto sigue abriendo el escaparate.

En la app (*Esferas → Corriente → Opciones*) se elige:

- **Estilo:** *Seda* (hilos largos, el de serie), *Humo* (trazos cortos y suaves) o *Río* (corrientes rápidas).
- **Color:** Hielo, Aurora, Fuego, Lavanda o Plata.
- **Hora:** *Tejida*, *Encima* (en el centro, sobre el flujo) o *Al tocar* (aparece 5 s después de tocar la pantalla).
- **Partículas** (de 400 a 3.000), **turbulencia** y **brillo.**

## Videojuegos

### HUD

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx05.png" alt="Esfera HUD" width="205">

*Número 5* — Interfaz sci-fi: la hora arriba, temperatura del chip, señal WiFi y tiempo encendido en medio, y el anillo de segundos abajo; glitch cada minuto.

### Aventura

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx06.png" alt="Esfera Aventura" width="205">

*Número 6* — Viaje en pixel art con cielo alto: parallax con bruma atmosférica, lago con reflejos, luz según la hora real (amanecer, día, atardecer, noche con faroles y luciérnagas).

### RPG

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx07.png" alt="Esfera RPG" width="205">

*Número 7* — Batalla 3D estilo JRPG que ocupa toda la pantalla: cámara cinemática, arena con círculo rúnico, cristal guardián y grupo; la ventana del grupo flota translúcida y sus barras de turno marcan los segundos y cada minuto se lanza un hechizo con la hora como daño.

### Gol

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx19.png" alt="Esfera Gol" width="205">

*Número 19* — Jugada de fútbol en 16 bits con un delantero con el 10: regate, salto sobre una entrada, gol por la escuadra con el portero vencido, confeti y celebración con los brazos cruzados. Arriba, la grada con los focos y el marcador del estadio con la hora, con la barra de los segundos debajo. Los jugadores son esqueletos 2D rasterizados a media resolución con sombreado y contorno, así cada pose se interpola suave.

## Arte

### Estrellada

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx08.png" alt="Esfera Estrellada" width="205">

*Número 8* — *La noche estrellada* (Van Gogh, 1889): cielo de pinceladas en remolino, halos que laten y el ciprés, alto, que se mece como una llama.

### Earthrise

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx11.png" alt="Esfera Earthrise" width="205">

*Número 11* — Foto del Apolo 8 (NASA, 1968): Tierra 3D con continentes y nubes que giran saliendo sobre la Luna.

### Trueno y peluches

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx27.png" alt="Esfera Trueno y peluches" width="205">

*Número 27* — Un día en la vida de un guitarrista de thrash metal (un rockero grande y fuerte de pelo largo; banda inventada: ni nombre, ni logo, ni músicos reales), en pixel art de 16 bits con volumen, tramado y contornos de color. La escena cambia con la hora y cada una tiene su animación:

- **0–8:** duerme abrazado a **Rosita**, su osito de peluche rosa, bajo un haz de luna con polvo flotando; respira y le salen las Z.
- **8–11:** medio dormido en la cocina, con ojeras y una taza negra entre las manos; el amanecer entra por la persiana y el vapor sube en espiral.
- **11–13:** sentado en el amplificador, afina con cara de pocos amigos su guitarra en V; el afinador de pinza tiene la luz en rojo hasta que la cuerda queda en verde, y le resbala una gota de sudor.
- **13–16:** el garaje del ensayo: cartones de huevos, el amplificador con sus cuatro conos, dos guitarras puntiagudas, micro, cables y pedal; del amplificador salen ondas de sonido.
- **16–19:** la furgoneta negra de gira, con llamas pintadas y el equipo en la baca, hacia la puesta de sol; colinas, postes y rayas de la carretera pasan a distintas velocidades.
- **19–23:** el concierto: focos rojos, ámbar y blancos que se cruzan, columnas de fuego, el público con los cuernos en alto y la hora grande en medio.
- **23–24:** derrotado en un sofá de cuero viejo, con la cabeza caída, un brazo colgando y la lata en el suelo, y una caja de pizza abierta delante; solo le alumbra una tele fuera de plano, con su parpadeo azulado.

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx27-escenas.png" alt="Las siete escenas de Trueno y peluches" width="100%">

## Naturaleza y espacio

### Sakura

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx12.png" alt="Esfera Sakura" width="205">

*Número 12* — Cerezo en flor junto a un estanque: pétalos que caen revoloteando y ondas donde tocan el agua.

### Disfruta

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx25.png" alt="Esfera Disfruta" width="205">

*Número 25* — Para el tiempo libre: un prado con la hierba y un árbol mecidos por el viento a rachas, nubes lentas, flores y semillas flotando. Sigue la hora real, como *Aventura*: cielo azul a mediodía, dorado por la mañana temprano y al caer la tarde, el sol cruzando el cielo y poniéndose tras las colinas, y de noche luna, estrellas, luciérnagas sobre la hierba y, de vez en cuando, fuegos artificiales. No tiene hora a propósito: solo pone «Disfruta» («Relax» en inglés).

### Lluvia

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx17.png" alt="Esfera Lluvia" width="205">

*Número 17* — Gotas-lente en el cristal sobre una ciudad desenfocada, hora en neón que parpadea. Las gotas resbalan hacia donde inclines el reloj (acelerómetro) y la lluvia de fuera se inclina con él; con el reloj plano, se quedan quietas.

### Agujero negro

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx18.png" alt="Esfera Agujero negro" width="205">

*Número 18* — Disco de acreción con efecto Doppler, lente gravitatoria del disco trasero y anillo de fotones, con un halo cálido; las estrellas se curvan alrededor (ecuación de lente puntual).

### Globos

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx32.png" alt="Esfera Globos" width="205">

*Número 32* — Un cielo de verdad (fotos de [Poly Haven](https://polyhaven.com), de dominio público) que se mueve muy despacio y cambia con la hora del día: noche hasta las 5:30, amanecer de 6:00 a 7:30, día de 8:00 a 18:00, atardecer de 18:30 a 20:00 y noche desde las 20:30, fundiéndose de uno a otro en media hora; de noche titilan algunas estrellas. La hora, grande y en blanco con una sombra suave, en el centro. Globos de colores suben a intervalos irregulares, a veces en grupitos: detrás de la hora, pequeños, lentos y con algo de bruma; delante, más grandes y rápidos; y de vez en cuando uno pasa muy cerca, tapando las cifras. A la hora en punto hay una suelta de globos.

Es interactiva: **tocar un globo** lo explota con un «pop» por el altavoz (sus trozos caen y el hilo cae dando vueltas); **arrastrar** sopla viento y los globos se desvían. Mantener el dedo quieto sigue abriendo el escaparate.

En la app (*Esferas → Globos → Opciones*) se elige:

- **Globos:** variados (de serie), pasteles, rojos o metalizados.
- **Cielo:** *Según la hora* o *Siempre de día*.
- **Cantidad** de globos y **sonido al explotar**.

## Relojes y útiles

### WiFi

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx09.png" alt="Esfera WiFi" width="205">

*Número 9* — La hora muy grande, horas encima de minutos, sobre un espectro tenue de las redes WiFi de alrededor (escaneo en segundo plano).

### Coloso

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx14.png" alt="Esfera Coloso" width="205">

*Número 14* — Un reloj de arena del tamaño de una torre, visto desde abajo entre dos columnas que se pierden en el cielo del desierto al atardecer; las nubes lo cruzan a media altura y una figura diminuta lo mira desde el suelo. La arena tarda una hora en caer: el montón de abajo son los minutos que han pasado. La hora, grabada en oro en la base. (Hasta la 3.0.0-beta.8 se llamaba *Arena* y era un reloj de arena de un minuto.)

### Engranajes

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx15.png" alt="Esfera Engranajes" width="205">

*Número 15* — Dentro de una torre del reloj gótica: engranajes enormes de bronce y hierro que avanzan a tirones con cada segundo, cadenas con contrapesos que bajan a lo largo de la hora, un péndulo, la esfera con números romanos y segundero, y la luz de la luna entrando por un ventanal ojival con polvo flotando. La hora también se lee abajo.

### Tiempo

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx21.png" alt="Esfera Tiempo" width="205">

*Número 21* — Minimalista: esfera analógica con agujas suavizadas y segundero de barrido; debajo, icono animado, temperatura, máx/mín y estado del cielo de la ciudad configurada. El fondo cambia con el tiempo (sol, gris, lluvia, noche).

### Clásico

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx22.png" alt="Esfera Clásico" width="205">

*Número 22* — Reloj de vestir rectangular Art Déco en el que la pantalla es la caja: metal pulido cuyo bisel sigue las esquinas redondeadas, esfera sunburst cuyos reflejos se mueven (verde por defecto; el color se elige en la app), minutería de raíles, índices y agujas dauphine doradas, fecha a las 3 y pequeño segundero a las 6 con 8 pulsos por segundo.

### Feria de Sevilla

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx23.png" alt="Esfera Feria de Sevilla: cuenta atrás" width="205"> <img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx23-feria.png" alt="Esfera Feria de Sevilla durante la Feria" width="205">

*Número 23* — Cuenta atrás hasta el alumbrado de la Feria de Abril de Sevilla: portada de bombillas que parpadean con guirnaldas del alumbrado a los lados, farolillos que se mecen, toldo de caseta y, abajo, la hora sobre un traje de lunares. Muestra los días, horas, minutos y segundos que faltan (sin segundos si se desactivan). La fecha se calcula sola cada año: el alumbrado es a la medianoche del lunes al martes, 16 días después del Domingo de Resurrección (13 de abril de 2027). Desde la víspera y durante la Feria hay fuegos artificiales, y de martes a domingo pone «¡Es Feria!» y el día en que va; pasada la Feria, cuenta hacia la del año siguiente. Necesita la hora puesta (la da la app).

### Ónix

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx24.png" alt="Esfera Ónix" width="205">

*Número 24* — Analógica minimalista hecha para la pantalla AMOLED: fondo negro puro, con esos píxeles apagados, minutería de puntos tenues, índices finos, agujas en blanco cálido y, como segundero, una estela de luz que recorre la minutería. Las agujas se mueven sin saltos. Todo el dibujo se desplaza 2 píxeles cada minuto para no marcar la pantalla. El **color del acento** (estela y centro) se elige en la app; sin segundos, la estela desaparece.

### El escultor

<img src="https://raw.githubusercontent.com/wiki/CarlosNS/c6-fx-clock/images/fx26.png" alt="Esfera El escultor" width="205">

*Número 26* — Tu objetivo de pasos como una escultura: un escultor talla la estatua de un corredor en un bloque de piedra. Hay once escenas, una por cada 10 % del objetivo; la talla baja por el bloque y el escultor trabaja subido al andamio. Al 100 % la estatua brilla en mármol y oro, con rayos y destellos, y el escultor lo celebra. Arriba la hora; abajo los pasos y el porcentaje. El objetivo se cambia en el reloj o en la app.

## Mis esferas

*Número 28* — La esfera que haces tú en la app del móvil (ver [[App Android]]): de fondo, una **foto** o un **GIF** de la galería encuadrado a la pantalla, o varios que cambian a lo largo del día por **tramos**; encima, la hora **digital** (con la fuente, el tamaño, el sitio, el color, el contorno o la sombra, los segundos y la fecha que elijas) o **analógica**, con agujas clásicas o con agujas que salen del borde para dejar ver el fondo. El GIF se repite mientras la pantalla está encendida y vuelve a empezar al encenderla; con la pantalla apagada no gasta nada.

- Necesita una **microSD** en el reloj: cada esfera se guarda en `/esferas/<número>/`. Sin tarjeta, la app no deja guardarla y la esfera lo avisa.
- Puedes tener varias; se ponen desde la app o desde el escaparate de la app **Esferas** del reloj, donde salen al final, con la miniatura de su imagen. Mientras no pongas ninguna, el número 28 no sale al deslizar. No entra en el cambio automático: se pone a propósito.
- El móvil guarda una **copia** de cada una, así que si la tarjeta se pierde o se formatea, *Restaurar en el reloj* las vuelve a mandar.

## ¿Una esfera nueva?

Cada esfera es un archivo `src/fx_*.c` con una función que dibuja un fotograma, y una entrada en `fx_list` (`src/fx_common.c`). Cómo hacer la tuya: [[Crear una esfera]].
