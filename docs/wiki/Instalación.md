## Lo que necesitas

- Una placa **Waveshare ESP32-S3-Touch-AMOLED-2.06** (ver [[Hardware]]) y un cable USB-C de datos.
- Un ordenador con **Chrome o Edge** (el instalador usa Web Serial; Firefox y Safari no lo tienen).
- Para la app: un móvil **Android 10 o superior** con Bluetooth.
- Si quieres guardar el firmware de fábrica antes, mira [[Hardware]].

## 1. Instalar el firmware (USB, desde el navegador)

1. Conecta la placa al ordenador.
2. Abre el instalador web: <https://carlosns.github.io/c6-fx-clock/flasher/>. Siempre instala la última versión publicada, y elige sola la imagen de tu placa.
3. Pulsa **Instalar el reloj** y elige el puerto de la placa (en Windows suele ser `COMx`; en Linux `/dev/ttyACM0`). Si no aparece o no entra en modo de grabación: mantén **BOOT**, pulsa y suelta **RESET** (o desconecta y conecta el cable) y suelta BOOT.
4. Si te pregunta si quieres **borrar el dispositivo**:
   - **Sí** en una placa nueva o para empezar de cero (se pierden los ajustes, la red WiFi, las alarmas y los móviles emparejados).
   - **No** para conservarlos.
5. Al terminar, el reloj se reinicia y enseña su esfera.

> Sin internet en el ordenador: descarga `marea.bin` de las [releases](https://github.com/CarlosNS/c6-fx-clock/releases) y grábalo en la dirección `0x0` con [esptool](https://docs.espressif.com/projects/esptool/) o con el `webflasher/` del repositorio (`Abrir flasher.bat`).

## 2. Conectar el móvil (app Android)

1. Descarga en el móvil el APK de la app (`c6-clock-app.apk`) de la última [release](https://github.com/CarlosNS/c6-fx-clock/releases) y ábrelo. Android pedirá permiso para instalar apps de esa fuente (navegador o gestor de archivos).
2. Abre **Marea** y acepta los permisos de **Dispositivos cercanos** y **Notificaciones**.
3. Pulsa **Buscar el reloj**, elige **Marea XXXX** en la lista del sistema y escribe la **clave de 6 cifras** que aparece en el reloj. Tienes 30 segundos (la barra de encima de la clave los cuenta).
4. En *Ajustes → Móvil y conexión*, permite que funcione en segundo plano y, si quieres ver la música y las llamadas en el reloj, *Música y llamadas → Permitir*. Todo el proceso, con los ajustes de Xiaomi/HyperOS y otros fabricantes, está en [[App Android]].

Empareja **siempre desde la app**, no desde los ajustes de Bluetooth de Android.

## 3. Guardar la red WiFi

El reloj usa el WiFi para las [[Actualizaciones]], para traer el tiempo por su cuenta y para la [[Página de control]] desde el navegador. Puedes darle tu red de dos formas:

- **Desde la app:** *Ajustes → Móvil y conexión → Red → Cambiar*, con el nombre de la red y la contraseña.
- **Sin app:** conéctate con el móvil o el portátil a la red WiFi **`Marea`** (clave `mareamarea`), que el reloj crea mientras no tiene otra, abre <http://192.168.4.1> y escribe tu red en **Ajustes → Móvil y conexión → Red**.

El reloj se conecta y su IP aparece en *Ajustes → Conexiones*. A partir de ahí, la página de control está en `http://<esa IP>` desde cualquier dispositivo de tu red. El reloj solo usa **2,4 GHz**: una red solo de 5 GHz no sirve.
