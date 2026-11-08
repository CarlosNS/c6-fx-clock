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

- **Pantalla en tres modos** (`lcd_co5300.c`):
  - **nativo de 410×502**, para el sistema, las apps y Plasma;
  - **lienzo ancho de 274×336**, toda la pantalla a 2/3 de resolución (×1,5 al enviar), para las esferas animadas: dibujarlo cuesta la mitad y llegan a 30 fps;
  - el lienzo antiguo de 172×320, que ya no usa ninguna esfera (queda para compatibilidad).

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
| 2 | Base gráfica: modo nativo, fuentes, paneles y componentes | ✅ |
| 3 | Vistas, gestos, barra de estado, ajustes rápidos y lanzador | ✅ (falta probarlo con el dedo) |
| 4 | Apps: cronómetro, linterna, ajustes, selector de esferas, información del sistema | ✅ |
| 5 | Audio (ES8311), alarmas y podómetro | ✅ (audio y pasos sin probar en la muñeca) |
| 6 | Esferas a pantalla completa, una a una | ✅ |
| 7 | Sincronización: enlace permanente, llamadas y música (firmware y app) | ✅ (app sin probar en el móvil) |
| 8 | Archivos de la microSD y modo USB | ✅ (sin tarjeta para probar el disco) |
| 9 | README, wiki y CHANGELOG | ✅ (wiki sin publicar) |

## Notas de las fases

### Estado al parar (8 de noviembre de 2026, madrugada)

- **Hecho y compilando:**
  - el modo nativo de 410×502 en `lcd_co5300.c` (`lcd_native_fb()`, `lcd_flush_native()`);
  - las capturas, que ya saben el tamaño del último fotograma (`lcd_last_frame(&w, &h)`);
  - las fuentes Jersey 10 y VT323 en mapas de bits: `tools/make_fonts.py` genera `src/ui_fonts.c`, con las TTF y sus licencias OFL en `tools/fonts/`.
- **Escrito pero fuera de `src/CMakeLists.txt`, todavía sin compilar:**
  - `ui.c`/`ui.h`: paleta, paneles, texto con sombra, controles y el fondo ilustrado de noche y atardecer;
  - `input.c`/`input.h`: gestos;
  - `sys.c`/`sys.h`: vistas, lanzador, ajustes rápidos, gestos sobre la esfera y la orden remota `ui …`;
  - `apps.h`: interfaz de las apps.
- **Para seguir:**
  1. declarar e implementar `ui_background_band()` en `ui.c`;
  2. añadir a `app.h` (y a `settings.c`) los ajustes `lang` y `dnd`;
  3. escribir las apps que `sys.c` ya enlaza (`app_faces`, `app_alarms`, `app_steps`, `app_stopwatch`, `app_music`, `app_torch`, `app_files`, `app_settings`), al principio aunque sea como pantallas sencillas;
  4. sustituir `poll_touch()` de `main.c` por `input_poll()` + `sys_event()`, dibujar con `sys_draw()` cuando la vista no sea la esfera, y refrescar `g_app.bat_pct`, `bat_chg` y `phone_linked` cada pocos segundos;
  5. la orden `ui <petición>` en `control.c`, que llama a `sys_request()`;
  6. compilar, flashear y revisar cada pantalla con `GET /screenshot` (el reloj está en 192.168.1.157).
- Avisar a THIRD-PARTY-NOTICES de las fuentes OFL cuando entren en el firmware.

### Sistema y apps (8 de noviembre de 2026)

- **Funcionando y revisado con capturas** (`GET /screenshot` tras `ui <vista>`):
  - el lanzador (rejilla de 2 columnas que se desplaza);
  - los ajustes rápidos (brillo, Bluetooth, silencio, siempre encendida, linterna y batería);
  - las apps Esferas, Ajustes, Alarmas, Pasos, Cronómetro, Música, Linterna y Archivos.
- **Sin probar todavía con el dedo ni de oído:** los gestos reales sobre el panel, el audio del ES8311 (la secuencia viene del driver de Espressif), el detector de pasos en la muñeca y la microSD (no consta que haya una tarjeta metida).
- **El bloqueo del reloj, resuelto:**
  - con el rastro de `hang.h`, el bucle estaba esperando un fotograma y la tarea de envío, dentro de la orden de ventana de `esp_lcd`, esperando una transferencia que nunca acababa;
  - pasaba en cuanto una esfera enviaba fotogramas nativos seguidos;
  - ahora `lcd_co5300.c` usa `spi_master` directamente: una escritura de píxeles por fotograma con el CS activo, y los trozos encolados sin órdenes en medio;
  - probado: 60 s de Plasma nativa y 30 s de Cubo 3D sin bloqueos.
- **Rendimiento:** Plasma nativa tarda 36 ms en dibujarse (26 fps): 12 ms de cálculo, 13 ms escribiendo la PSRAM y 10 ms los dígitos de 176 px. Para animaciones a pantalla completa más fluidas habría que dibujar por franjas directamente en RAM interna (pendiente).
- **Fuentes:** `font_round_xxl` y `font_seg_xxl` (176 px), generadas con `tools/gen_fonts.py`.
- **Esferas nativas:** `fx_t.res` (`FX_COMPAT`, `FX_WIDE`, `FX_NATIVE`).

### Esferas a pantalla completa (8 de noviembre de 2026)

- **Todas las esferas ocupan ya toda la pantalla y van a 30 fps:** Plasma en nativo; las demás en el lienzo ancho (`WW`×`WH`, esquinas de radio `WIDE_CORNER_R` 80).
- **Las apaisadas pasan a verticales:** Vida (hora en dos filas), HUD, Aventura, Estrellada, WiFi (hora en dos filas), Earthrise, Sakura, Synthwave, Agujero negro y Gol.
- **Cambios de diseño:**
  - Clásico: la caja es la pantalla y su bisel sigue las esquinas; firma MAREA;
  - Feria: guirnaldas del alumbrado hacia los lados;
  - RPG: arena a toda pantalla con la ventana del grupo translúcida;
  - Agujero negro: halo con tramado;
  - Gol: la escena de 320×172 se dibuja en su búfer y se compone con grada, marcador y césped;
  - Túnel: sin panel, las cifras en el fondo oscuro.
- **Arreglo:** `gfx_fill` limpiaba solo el tamaño del lienzo antiguo.

### Sincronización, modo USB y documentación (8 de noviembre de 2026)

- **App 0.4.0** (`versionCode` 14), compilada pero **sin instalar ni probar** en el móvil:
  - `LinkService`: servicio en primer plano de tipo *connectedDevice* que mantiene el enlace; lo arrancan `ClockPresence` (reloj a la vista) y la app al abrirse; reconecta con esperas de 2 s a 1 min; sincroniza al conectar y cada 30 min; prioridad de conexión baja salvo con la app en pantalla;
  - `Phone` + `PhoneWatch` (lector de notificaciones): música por MediaSession, llamadas por la notificación de categoría `call` (rechazar pulsa su acción; silenciar baja el timbre 1 min);
  - la página muestra «Música y llamadas en el reloj»; `SyncWorker` ya no conecta si el servicio está vivo.
- **Firmware:** el tiempo del móvil siempre gana (`weather_set_from_web`); el test `ble/test_sync.py` pasa.
- **Modo USB** (`usbdisk.c`, `mode usb`, botón USB de Archivos): TinyUSB MSC con la microSD; sale con BOOT, al expulsar o al desconectar el cable. Probado solo sin tarjeta (vuelve solo a los 4 s).
- **Rendimiento:** todas las esferas a 30 fps (`pytest tests/wifi -k 30fps` pasa). Plasma prepara sus cifras una vez por minuto; Clásico calcula la esfera de dos en dos píxeles. `gen_fonts.py` estampa el trazo redondo (las cifras grandes tenían espinas).
- **Documentación:** README, CHANGELOG, THIRD-PARTY-NOTICES, `new_face.py` (lienzo ancho o `--native`) y toda la wiki en `docs/wiki/` (sin publicar: `tools/publish_wiki.py` necesita permiso).
- **Pendiente de probar con el dueño:** instalar la app 0.4.0 (adb o release), música y llamadas reales, gestos con el dedo, audio, pasos en la muñeca, alarmas, y el modo USB con una tarjeta metida.

### Siguiente: batería (pedido del dueño, 8 de noviembre de 2026)

El dueño vio bajar la batería del 100 % al 39 % en una mañana (con muchas pruebas con la pantalla encendida, pero aun así es demasiado). Encontrado:

- `net.c` pone `esp_wifi_set_ps(WIFI_PS_NONE)` (herencia del C6, para la latencia del panel táctil de la página): la radio WiFi nunca duerme.
- Sin `CONFIG_PM_ENABLE`: ni bajada de frecuencia ni sueño ligero; con la pantalla apagada la CPU sigue a 240 MHz.

Plan: 1) medir (registro de porcentaje y voltaje por estado: pantalla apagada con WiFi+BLE, apagada solo BLE, encendida); 2) WiFi bajo demanda (apagado con el móvil conectado; interruptor en ajustes rápidos y al abrir la página; las actualizaciones siguen con su arranque propio, que enciende el WiFi solo para descargar); 3) gestión de energía (DFS y sueño ligero) y bucle lento con la pantalla apagada; 4) decidir con el dueño qué hacer con la esfera WiFi.
