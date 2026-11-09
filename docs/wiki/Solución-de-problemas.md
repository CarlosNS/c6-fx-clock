## Emparejar y conectar

**La app no encuentra el reloj.**
- Mientras otro dispositivo está conectado (otro móvil, un PC, Chrome por Web Bluetooth), el reloj **deja de anunciarse**. Cierra esa conexión.
- Acércate: el alcance práctico es de unos pocos metros a través de paredes.

**El emparejamiento falla o se cancela.**
- Hay **30 segundos** desde que aparece la clave (límite del Bluetooth; lo cuenta la barra de encima). Vuelve a pulsar *Buscar el reloj*.
- Empareja **desde la app**, no desde los ajustes de Bluetooth del móvil.
- Si el móvil ya tenía el reloj emparejado de antes: en la app *Ajustes → Móvil y conexión → Olvidar este reloj* (o bórralo de los ajustes de Bluetooth) y en el reloj **BOOT 10 s**; luego empareja de nuevo.
- El reloj recuerda **3 móviles**: si ya tiene 3, olvídalos con BOOT 10 s.

**Pide la clave otra vez cada vez.** El móvil o el reloj han perdido el emparejamiento (por ejemplo, tras BOOT 10 s o reinstalar la app). Olvídalo en los dos lados y vuelve a emparejar.

## La hora y el tiempo

**El reloj no tiene la hora.** Su reloj de hardware la guarda mientras quede algo de batería. Si se agotó del todo, la recupera en cuanto se conecta el móvil (al abrir la app o al acercarlo, con Android 12+) o internet por WiFi.

**La app se desconecta con la app cerrada, o desaparece el aviso «Marea conectado».** El sistema está matando la app en segundo plano. Sigue los pasos de *Dejar que funcione en segundo plano* en [[App Android]]; en Xiaomi/HyperOS son imprescindibles el **Inicio automático** y la batería **Sin restricciones**. *Ajustes → Avanzado* dice cuándo fue la última sincronización.

**El reloj no muestra la música o no avisa de las llamadas.** Hace falta el permiso de leer notificaciones: *Ajustes → Móvil y conexión → Música y llamadas → Permitir*. Si Android responde «Se ha denegado el acceso», primero *Ajustes → Aplicaciones → Marea → ⋮ → Permitir ajustes restringidos* (ver [[App Android]]). Si ya está dado y no va, quítalo y vuelve a darlo (algunos móviles lo desactivan al actualizar la app). Las llamadas no suenan con *No molestar*.

**La esfera Tiempo no tiene datos.** Necesita internet en el móvil o en el reloj (por WiFi). La ciudad se elige en los ajustes de la propia esfera.

## WiFi

**No se conecta a mi red.**
- El reloj solo usa **2,4 GHz**: una red solo de 5 GHz no sirve.
- Revisa nombre y contraseña en *Conexión*; distingue mayúsculas.
- Mientras no tiene ninguna red, el reloj crea la suya, `Marea` (clave `mareamarea`, página en `192.168.4.1`).

**En una red mallada (varios puntos de acceso) conecta pero no tiene IP.** Algunos nodos aceptan la conexión y no dan dirección. El reloj lo detecta a los 20 s y se pasa al siguiente nodo más fuerte de la misma red; dale un minuto.

**No resuelve nombres (no hay hora ni tiempo aunque está conectado).** Usa DNS públicos (1.1.1.1 y 8.8.8.8) precisamente porque algunos routers rechazan sus consultas; si tu red bloquea DNS externos, ese es el problema.

## Actualizaciones

**«No pudo conectarse a su red WiFi».** Guarda la red en *Conexión*. Ver la tabla completa de resultados en [[Actualizaciones]].

**La app no se actualiza.** La primera vez Android pide permitir que Marea instale apps («Permitir de esta fuente»): actívalo, vuelve y pulsa otra vez. Si dice que la firma no coincide, la app instalada se compiló con otra clave: desinstálala e instala la de las releases (habrá que emparejar de nuevo).

**Instalar el APK desde el PC con `adb` falla con `INSTALL_FAILED_USER_RESTRICTED`.** En Xiaomi hay que activar *Opciones de desarrollador → Instalar vía USB* y aceptar el aviso en el móvil antes de 10 segundos.

## USB

**El ordenador no ve la placa.** Usa un cable de datos (muchos solo cargan). En Windows aparece como «Dispositivo serie USB» (COMx) sin instalar drivers. Si el reloj está en **modo USB** (pantalla *MODO USB*), el ordenador ve un disco en vez del puerto serie: pulsa BOOT para volver.

**El instalador no conecta.** Usa Chrome o Edge (no Firefox ni Safari). Si sigue sin conectar, mantén **BOOT**, pulsa y suelta **RESET**, suelta BOOT, y vuelve a intentarlo: la placa entra en modo de descarga.

**El modo USB no muestra el disco.** Mete una microSD antes de entrar (sin tarjeta el reloj vuelve solo) y usa un cable de datos. Las tarjetas se ven con el formato que tengan; si el ordenador no la reconoce, formatéala en FAT32 o exFAT desde la app Archivos o desde el ordenador.

## Nada de lo anterior

Abre una [issue](https://github.com/CarlosNS/c6-fx-clock/issues) con la versión del firmware y de la app, el modelo de móvil y lo que ves.
