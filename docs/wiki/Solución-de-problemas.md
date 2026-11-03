## Emparejar y conectar

**La app no encuentra el reloj.**
- Comprueba que está en **modo Bluetooth**: la pantalla de bienvenida lo dice al encenderlo (BOOT 2 s ahí para cambiar).
- Mientras otro dispositivo está conectado (otro móvil, un PC, Chrome por Web Bluetooth), el reloj **deja de anunciarse**. Cierra esa conexión.
- Acércate: el alcance práctico es de unos pocos metros a través de paredes.

**El emparejamiento falla o se cancela.**
- Hay **30 segundos** desde que aparece la clave (límite del Bluetooth; lo cuenta la barra de encima). Vuelve a pulsar *Buscar el reloj*.
- Empareja **desde la app**, no desde los ajustes de Bluetooth del móvil.
- Si el móvil ya tenía el reloj emparejado de antes: en la app *Este móvil → Olvidar este reloj* (o bórralo de los ajustes de Bluetooth) y en el reloj **BOOT 10 s**; luego empareja de nuevo.
- El reloj recuerda **3 móviles**: si ya tiene 3, olvídalos con BOOT 10 s.

**Pide la clave otra vez cada vez.** El móvil o el reloj han perdido el emparejamiento (por ejemplo, tras BOOT 10 s o reinstalar la app). Olvídalo en los dos lados y vuelve a emparejar.

## La hora y el tiempo

**El reloj no tiene la hora al enchufarlo.** La placa no tiene pila de reloj. En modo Bluetooth el móvil se la pone en segundos al detectarlo (Android 12+) o, como mucho, en la siguiente sincronización de 30 minutos; abrir la app lo hace al momento. En modo WiFi la toma de internet al conectarse.

**Deja de sincronizar con la app cerrada.** El sistema está matando la app en segundo plano. Sigue los pasos de *Dejar que funcione en segundo plano* en [[App Android]]; en Xiaomi/HyperOS son imprescindibles el **Inicio automático** y la batería **Sin restricciones**. La tarjeta *Este móvil* dice cuándo fue la última sincronización.

**La esfera Tiempo no tiene datos.** Necesita internet en el móvil (modo Bluetooth) o en el reloj (modo WiFi). La ciudad se elige en los ajustes de la propia esfera.

## WiFi

**No se conecta a mi red.**
- El ESP32-C6 solo usa **2,4 GHz** (y WiFi 6 en esa banda): una red solo de 5 GHz no sirve.
- Revisa nombre y contraseña en *Conexión*; distingue mayúsculas.
- Si todo falla, su propia red `C6-FX-Demo` (clave `esp32c6fx`, página en `192.168.4.1`) siempre está disponible en modo WiFi.

**En una red mallada (varios puntos de acceso) conecta pero no tiene IP.** Algunos nodos aceptan la conexión y no dan dirección. El reloj lo detecta a los 20 s y se pasa al siguiente nodo más fuerte de la misma red; dale un minuto.

**No resuelve nombres (no hay hora ni tiempo aunque está conectado).** Usa DNS públicos (1.1.1.1 y 8.8.8.8) precisamente porque algunos routers rechazan sus consultas; si tu red bloquea DNS externos, ese es el problema.

## Actualizaciones

**«No pudo conectarse a su red WiFi».** Guarda la red en *Conexión* (también en modo Bluetooth). Ver la tabla completa de resultados en [[Actualizaciones]].

**La app no se actualiza.** La primera vez Android pide permitir que C6 Clock instale apps («Permitir de esta fuente»): actívalo, vuelve y pulsa otra vez. Si dice que la firma no coincide, la app instalada se compiló con otra clave: desinstálala e instala la de las releases (habrá que emparejar de nuevo).

**Instalar el APK desde el PC con `adb` falla con `INSTALL_FAILED_USER_RESTRICTED`.** En Xiaomi hay que activar *Opciones de desarrollador → Instalar vía USB* y aceptar el aviso en el móvil antes de 10 segundos.

## USB

**El ordenador no ve la placa.** Usa un cable de datos (muchos solo cargan). En Windows aparece como «Dispositivo serie USB» (COMx) sin instalar drivers.

**El instalador no conecta.** Usa Chrome o Edge (no Firefox ni Safari). Si sigue sin conectar, mantén **BOOT**, pulsa y suelta **RESET**, suelta BOOT, y vuelve a intentarlo: la placa entra en modo de descarga.

**Abrir el monitor serie reinicia el reloj.** Es normal en esta placa: el puerto USB controla el reinicio.

## Nada de lo anterior

Abre una [issue](https://github.com/CarlosNS/c6-fx-clock/issues) con la versión del firmware y de la app, el modelo de móvil y lo que ves.
