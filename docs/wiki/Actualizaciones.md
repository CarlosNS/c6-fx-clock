Hay cuatro formas de actualizar el reloj. La normal es la primera.

| Forma | Cuándo | Necesita |
|---|---|---|
| **Desde la app, por Bluetooth** | Siempre que haya versión nueva, en cualquier sitio | La app Marea con internet en el móvil; el reloj desde la 3.0.0-beta.6 |
| **Por el WiFi del reloj** | Relojes anteriores a la beta.6, o desde el navegador | La red WiFi guardada en el reloj |
| **Subiendo un archivo** | Versiones de prueba, sin internet | El reloj en tu WiFi y el archivo `marea-ota.bin` |
| **Por USB** | Primera instalación, recuperación o cambio de particiones | Cable y Chrome/Edge (ver [[Instalación]]) |

## Desde la app, por Bluetooth

1. En la app: **Actualizaciones → Buscar actualización**. La app lo hace sola al abrirse y avisa con una notificación cuando sale una versión.
2. Si hay versión nueva, pulsa **Actualizar a la X** y confirma.
3. El **móvil** descarga la imagen de GitHub y comprueba su tamaño y su **SHA-256**.
4. Después se la pasa al reloj por Bluetooth. El reloj la va guardando en su otro hueco de memoria mientras sigue funcionando, con una pantalla que enseña el progreso.
5. Al terminar, el reloj vuelve a comprobar la huella, se reinicia con la versión nueva y la tarjeta dice cómo fue.

Tarda unos minutos. Durante ese tiempo, deja la app abierta (la pantalla del móvil no se apaga sola) y el reloj cerca. **El reloj no necesita WiFi**: funciona en la calle, con los datos del móvil.

Si se corta el Bluetooth, cierras la app o la imagen llega mal, el reloj lo descarta y **sigue con la versión que tenía**.

## Por el WiFi del reloj

Es la forma de los relojes anteriores a la 3.0.0-beta.6, y la que usa la página de control abierta en un navegador. La app la elige sola cuando el reloj no sabe recibir la versión por Bluetooth.

1. Pulsa **Actualizar a la X** y confirma.
2. El reloj se reinicia en **modo actualización**, se conecta a tu WiFi y descarga la imagen, con un máximo de 5 minutos.
3. Comprueba que es de este proyecto, su tamaño y su **SHA-256**, la instala y vuelve **solo** al modo en que estaba. Tarda de 1 a 3 minutos.

Hace falta tener tu red guardada en *Conexión*.

Las versiones se publican en GitHub Pages: el manifiesto <https://carlosns.github.io/c6-fx-clock/manifest.json> dice cuál es la última, dónde está y su huella. La app también se actualiza desde ese mismo manifiesto (ver [[App Android]]).

### Resultados

| Mensaje | Qué pasó |
|---|---|
| Actualizado | Todo bien: ya tiene la versión nueva |
| Ya tenía la última versión | No había nada que instalar |
| Se cortó el Bluetooth a mitad | El reloj se alejó o se apagó el Bluetooth del móvil |
| Cancelada | Se cerró la app durante el envío |
| No pudo conectarse a su red WiFi | (por WiFi) Red mal guardada, contraseña cambiada o fuera de alcance |
| Su red no tenía internet | (por WiFi) Se conectó, pero no llegó a GitHub |
| No pudo leer la información de la versión | Manifiesto inaccesible o roto |
| La descarga no era válida | Tamaño o SHA-256 incorrectos: no se instala nada |
| La descarga tardó demasiado | El móvil dejó de enviar 20 s, o por WiFi pasaron los 5 minutos |
| Se cortó a mitad | Reinicio o corte de corriente durante la actualización |

En todos los casos de error **el reloj sigue con la versión que tenía**.

## Subiendo un archivo (por WiFi)

*Avanzado → Actualizar por WiFi (OTA)*: elige `marea-ota.bin` (de las releases o de `webflasher/` si lo compilas tú) y pulsa *Actualizar firmware*. Tarda unos 7 segundos. Solo acepta firmware de este proyecto.

Desde el PC también se puede mandar por Bluetooth con `python tools/ble_ota.py` (ver [[Compilar]]).

## Por qué es seguro

- La memoria tiene **dos huecos de 3,5 MB** para el programa. La versión nueva se escribe en el que no está en uso, así que un corte a medias no estropea la que funciona.
- **Vuelta atrás automática:** una versión nueva tiene que funcionar 30 segundos para quedar confirmada. Si se cuelga antes, en el siguiente arranque el reloj vuelve solo a la anterior.
- Por Bluetooth solo acepta la imagen el móvil **emparejado** (el enlace va cifrado), y la huella SHA-256 se comprueba dos veces: en el móvil y en el reloj.
- Si el modo actualización por WiFi falla o se reinicia, el reloj ya ha guardado el modo al que debe volver: nunca se queda atrapado en él.

## Cuándo hace falta el USB

- Placa nueva o borrada.
- Si una versión cambia la tabla de particiones (pasó en la 2.0.0). Las notas de la versión lo dirían.
- Si algo saliese tan mal que el reloj no arranca: el instalador web lo recupera siempre.
