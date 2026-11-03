Hay tres formas de actualizar el reloj. La normal es la primera.

| Forma | Cuándo | Necesita |
|---|---|---|
| **Por internet** | Siempre que haya versión nueva | La red WiFi guardada en el reloj |
| **Subiendo un archivo** | Versiones de prueba, sin internet | Modo WiFi y el archivo `c6-fx-clock-ota.bin` |
| **Por USB** | Primera instalación, recuperación o cambio de particiones | Cable y Chrome/Edge (ver [[Instalación]]) |

## Por internet

1. En la app o la página: **Actualizaciones → Buscar actualización** (la app lo hace sola al abrirse y te avisa con una notificación).
2. Si hay versión nueva, pulsa **Actualizar a la X** y confirma.
3. El reloj se reinicia en **modo actualización**, se conecta a tu WiFi, descarga la imagen (máximo 5 minutos), comprueba que es de este proyecto, su tamaño y su **SHA-256**, la instala y vuelve **solo** al modo en que estaba. Tarda de 1 a 3 minutos y la pantalla enseña el progreso.
4. Al volver, la tarjeta dice cómo fue.

Funciona igual en modo Bluetooth: el móvil solo da la orden y el reloj descarga por su cuenta. Por eso conviene guardar tu red en *Conexión* aunque uses la app.

Las versiones se publican en GitHub Pages: el manifiesto <https://carlosns.github.io/c6-fx-clock/manifest.json> dice cuál es la última, dónde está y su huella. La app también se actualiza desde ese mismo manifiesto (ver [[App Android]]).

### Resultados

| Mensaje | Qué pasó |
|---|---|
| Actualizado | Todo bien: ya tiene la versión nueva |
| Ya tenía la última versión | No había nada que instalar |
| No pudo conectarse a su red WiFi | Red mal guardada, contraseña cambiada o fuera de alcance: revísala en *Conexión* |
| Su red no tenía internet | Se conectó pero no llegó a GitHub |
| No pudo leer la información de la versión | Manifiesto inaccesible o roto |
| La descarga no era válida | Tamaño o SHA-256 incorrectos: no se instala nada |
| La descarga tardó demasiado | Pasaron los 5 minutos |
| Se cortó a mitad | Reinicio o corte de corriente durante la descarga |

En todos los casos de error **el reloj sigue con la versión que tenía**.

## Subiendo un archivo (modo WiFi)

*Avanzado → Actualizar por WiFi (OTA)*: elige `c6-fx-clock-ota.bin` (de las releases o de `webflasher/` si lo compilas tú) y pulsa *Actualizar firmware*. Tarda unos 7 segundos. Solo acepta firmware de este proyecto.

## Por qué es seguro

- La memoria tiene **dos huecos de 3,5 MB** para el programa: la versión nueva se escribe en el que no está en uso, así que un corte a medias no estropea la que funciona.
- **Vuelta atrás automática:** una versión nueva tiene que funcionar 30 segundos para quedar confirmada. Si se cuelga antes, en el siguiente arranque el reloj vuelve solo a la anterior.
- Si el modo actualización falla o se reinicia, el reloj ya ha guardado el modo al que debe volver: nunca se queda atrapado en él.

## Cuándo hace falta el USB

- Placa nueva o borrada.
- Si una versión cambia la tabla de particiones (pasó en la 2.0.0). Las notas de la versión lo dirían.
- Si algo saliese tan mal que el reloj no arranca: el instalador web lo recupera siempre.
