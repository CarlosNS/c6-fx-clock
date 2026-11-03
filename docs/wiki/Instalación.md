## Lo que necesitas

- Una placa **Waveshare ESP32-C6-LCD-1.47** (ver [[Hardware]]) y un cable USB-C de datos.
- Un ordenador con **Chrome o Edge** (el instalador usa Web Serial; Firefox y Safari no lo tienen).
- Para la app: un móvil **Android 10 o superior** con Bluetooth.

## 1. Instalar el firmware (USB, desde el navegador)

1. Conecta la placa al ordenador.
2. Abre el instalador web: <https://carlosns.github.io/c6-fx-clock/flasher/>. Siempre instala la última versión publicada.
3. Pulsa **Instalar el reloj** y elige el puerto de la placa (en Windows suele ser `COMx`; en Linux `/dev/ttyACM0`).
4. Si te pregunta si quieres **borrar el dispositivo**:
   - **Sí** en una placa nueva o para empezar de cero (se pierden los ajustes, la red WiFi y los móviles emparejados).
   - **No** para conservarlos.
5. Al terminar, el reloj se reinicia y enseña la pantalla de bienvenida con el modo en que arranca.

> Sin internet en el ordenador: descarga `c6-fx-clock.bin` de las [releases](https://github.com/CarlosNS/c6-fx-clock/releases) y grábalo en la dirección `0x0` con [esptool](https://docs.espressif.com/projects/esptool/) o con el `webflasher/` del repositorio (`Abrir flasher.bat`).

## 2. Elegir el modo

El reloj tiene dos modos (detalles en [[Uso del reloj]]):

- **Bluetooth:** se maneja con la app Android. Recomendado para el día a día.
- **WiFi:** se maneja desde el navegador. Útil sin móvil Android o para configurar la red.

Para cambiar de modo, **mantén pulsado BOOT 2 segundos mientras se ve la pantalla de bienvenida** (al encenderlo): se llena una barra y el reloj se reinicia en el otro modo.

## 3a. Con la app Android (modo Bluetooth)

1. Descarga el APK `c6-clock-app-….apk` de la última [release](https://github.com/CarlosNS/c6-fx-clock/releases) en el móvil y ábrelo. Android pedirá permiso para instalar apps de esa fuente (navegador o gestor de archivos).
2. Abre **C6 Clock** y acepta los permisos de **Dispositivos cercanos** y **Notificaciones**.
3. Pulsa **Buscar el reloj**, elige **C6 Clock XXXX** en la lista del sistema y escribe la **clave de 6 cifras** que aparece en el reloj. Tienes 30 segundos (la barra de encima de la clave los cuenta).
4. Permite que funcione en segundo plano. Todo el proceso, con los ajustes de Xiaomi/HyperOS y otros fabricantes, está en [[App Android]].

Empareja **siempre desde la app**, no desde los ajustes de Bluetooth de Android.

## 3b. Con el navegador (modo WiFi)

1. Con el móvil o el portátil, conéctate a la red WiFi **`C6-FX-Demo`** (clave `esp32c6fx`).
2. Abre <http://192.168.4.1>.
3. En **Ajustes generales → Conexión**, escribe el nombre de tu red WiFi y su contraseña. El reloj se conecta y enseña su IP en pantalla; a partir de ahí, ábrelo en `http://<esa IP>` desde cualquier dispositivo de tu red.

Más en [[Página de control]].

## 4. Guardar la red WiFi (recomendado también en modo Bluetooth)

Aunque uses la app, guarda tu red en **Conexión**: el reloj la usa solo para descargar actualizaciones (ver [[Actualizaciones]]).
