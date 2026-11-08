# Plan: Marea, el reloj completo

El 8 de noviembre de 2026 el dueño decidió dejar el ESP32-C6 y convertir la placa S3 en **Marea**, un reloj de pulsera con menú, apps propias y esferas hechas para su pantalla. Este plan recoge su encargo (una especificación hecha con otra IA y una guía de estilo) y las decisiones técnicas tomadas al aplicarlo. Donde el encargo y la técnica chocan, manda la técnica y aquí se explica por qué.

Orden de trabajo pedido: **1) sistema y apps, 2) esferas, 3) sincronización con el móvil.**

## Decisiones del dueño

- Fuera todo el soporte del C6. Los C6 instalados se quedan en la 2.1.0: las claves de arriba del manifiesto no se tocan nunca.
- El reloj se llama **Marea**: Bluetooth «Marea XXXX», WiFi propia «Marea» (clave `mareamarea`) y app «Marea».
- Conexión **permanente** por Bluetooth con el móvil mientras esté cerca (necesaria para llamadas y música), con su notificación fija en Android.
- Trabajo directo en `main`, que se sube. Publicar versiones sigue necesitando permiso expreso.

## Estilo «Marea Solsticial» (interfaz del sistema)

Resumen de la guía del dueño, aplicado a la pantalla de 410×502. Las esferas no lo siguen: cada una tiene su estilo.

- **Ambiente:** pixel art nocturno junto al mar (luna, faro, hoguera), tranquilo y cálido. Noche = uso normal; atardecer = ajustes, emparejado, primera puesta en marcha.
- **Paleta:**
  - interfaz: `noche #14102C` (fondo), `panel #221A42`, `panel2 #2A2052`, `linea #463A7A`, `barra-off #3E3468`, `crema #F6EEE2` (texto, nunca blanco), `gris #A89CCE` (secundario);
  - color con significado: `oro #F9B25A` / `oro2 #FFD166` (destacado), `verde #78D6A0` (bien, cargando), `coral #EC6066` (problema).
- **Fuentes:** Jersey 10 (títulos, cifras, botones) y VT323 (texto), las dos con licencia SIL OFL. Se convierten a mapas de bits con `tools/make_fonts.py`.
- **Formas:** paneles con borde pixelado de 4–6 px y esquinas recortadas, sombras duras sin desenfoque, nada de redondeos ni degradados en la interfaz, animaciones a saltos.
- **Textos:** español claro y corto, coma decimal, espacio antes de «%», separador « · ».

## Arquitectura

- **Pantalla en dos modos** (`lcd_co5300.c`):
  - lienzo de 172×320 ampliado, para las esferas que aún no se han rehecho;
  - **nativo de 410×502**, para el sistema, las apps y las esferas nuevas.

  Cada uno tiene dos búferes en PSRAM. El envío sigue en el núcleo 1.
- **Vistas:** esfera, ajustes rápidos, lanzador y app activa. Cada vista dibuja y recibe gestos. Una vista que no cambia no se vuelve a dibujar ni a enviar, y así ahorra batería.
- **Gestos** (táctil):
  - toque, pulsación larga y arrastre;
  - deslizamiento, distinguiendo si empieza en un borde.
- **Navegación:**
  - **en la esfera:**
    - deslizar desde el borde superior abre los ajustes rápidos;
    - desde el borde inferior, el lanzador;
    - a izquierda o derecha cambia de esfera;
    - una pulsación larga abre el selector de esferas;
  - **en una app o en el lanzador:** BOOT o deslizar hacia la derecha desde el borde izquierdo vuelve atrás;
  - **pantalla apagada:** BOOT, un toque o levantar la muñeca la encienden.
- **Textos en dos idiomas** (español e inglés) con una tabla de cadenas; el español es el idioma de referencia.

## Ajustes a la especificación (criterio técnico)

| Pide | Se hace | Por qué |
|---|---|---|
| 60 fps en animaciones | 30 fps | Enviar un fotograma completo al panel cuesta unos 12 ms y dibujarlo en PSRAM otro tanto. Las vistas quietas no se redibujan |
| Vibración | Sonidos por el altavoz | La placa no tiene motor de vibración |
| Esferas guardadas en la tarjeta | Esferas en el firmware; la tarjeta, para sonidos y datos | Las esferas son código, no imágenes |
| *Deep sleep* | Panel en reposo y bucle lento con la pantalla apagada | El reloj tiene que seguir conectado al móvil y contando pasos |
| USB de almacenamiento | Modo aparte con reinicio | El USB es el mismo que la consola: o una cosa o la otra |
| Llamadas y música | Mensajes por el enlace Bluetooth permanente | La app los recoge del móvil y los reenvía |

## Fases de esta noche

| # | Fase | Estado |
|---|---|---|
| 1 | Quitar el C6, renombrar a Marea | ✅ |
| 2 | Base gráfica: modo nativo, fuentes, paneles y componentes | |
| 3 | Vistas, gestos, barra de estado, ajustes rápidos y lanzador | |
| 4 | Apps: cronómetro, linterna, ajustes, selector de esferas, información del sistema | |
| 5 | Audio (ES8311), alarmas y podómetro | |
| 6 | Esferas a pantalla completa, una a una | |
| 7 | Sincronización: enlace permanente, llamadas y música (firmware y app) | |
| 8 | Archivos de la microSD y modo USB | |
| 9 | README, wiki y CHANGELOG | |

## Notas de las fases
