package io.github.carlosns.c6clock

import android.Manifest
import android.app.AlertDialog
import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.companion.AssociationInfo
import android.companion.AssociationRequest
import android.companion.BluetoothLeDeviceFilter
import android.companion.CompanionDeviceManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.IntentSender
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.ParcelUuid
import android.os.PowerManager
import android.provider.Settings
import android.util.Log
import android.webkit.JavascriptInterface
import android.webkit.JsResult
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.view.WindowManager
import android.webkit.WebViewClient
import android.widget.FrameLayout
import androidx.activity.ComponentActivity
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.lifecycle.lifecycleScope
import androidx.webkit.WebViewAssetLoader
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeout
import org.json.JSONObject
import java.util.concurrent.Executor

/**
 * The app is the clock's own control page (src/index.html) in a WebView. Kotlin owns what a page
 * cannot do: pairing through the Companion Device Manager, the Bluetooth link and the background
 * sync. The page reaches it through window.ClockBridge (see AndroidLink in src/transport.js).
 */
@SuppressLint("MissingPermission")  // every Bluetooth call is behind Perms.canConnect
class MainActivity : ComponentActivity() {
    private lateinit var web: WebView
    private lateinit var store: Store
    private var pageReady = false
    private val link get() = Live.link?.takeIf { it.ready }
    private val onClockMessage: (String) -> Unit = { s -> js("clockMessage", JSONObject.quote(s)) }
    private val onClockState: (Boolean) -> Unit = { up -> js("clockState", up.toString()) }

    private val permissions = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { connectIfPaired() }
    private val chooser = registerForActivityResult(ActivityResultContracts.StartIntentSenderForResult()) { r ->
        val address = if (r.resultCode == RESULT_OK) associatedAddress(r.data) else null
        if (address == null) js("clockPickFailed", JSONObject.quote("No elegiste ningún reloj")) else paired(address)
    }

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        store = Store(this)
        val assets = WebViewAssetLoader.Builder().addPathHandler("/assets/", WebViewAssetLoader.AssetsPathHandler(this)).build()
        web = WebView(this).apply {
            setBackgroundColor(0xFF0B0D14.toInt())
            settings.javaScriptEnabled = true
            settings.domStorageEnabled = true
            webChromeClient = object : WebChromeClient() {
                // confirm() as a native dialog, without the "the page at https://appassets… says" header;
                // the page's ask() sends {title, body, ok}
                override fun onJsConfirm(view: WebView, url: String, message: String, result: JsResult): Boolean {
                    val q = runCatching { JSONObject(message) }.getOrNull()
                    AlertDialog.Builder(this@MainActivity, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                        .setTitle(q?.optString("title")?.ifEmpty { null })
                        .setMessage(q?.optString("body") ?: message)
                        .setPositiveButton(q?.optString("ok")?.ifEmpty { null } ?: "Aceptar") { _, _ -> result.confirm() }
                        .setNegativeButton("Cancelar") { _, _ -> result.cancel() }
                        .setOnCancelListener { result.cancel() }
                        .show()
                    return true
                }
            }
            webViewClient = object : WebViewClient() {
                override fun shouldInterceptRequest(view: WebView, req: WebResourceRequest): WebResourceResponse? =
                    assets.shouldInterceptRequest(req.url)
            }
            addJavascriptInterface(Bridge(), "ClockBridge")
        }
        // Android 15 draws apps under the status and navigation bars: keep the page clear of them.
        // The padding goes on a frame around the WebView (a WebView's own padding doesn't move its content)
        val frame = FrameLayout(this).apply { setBackgroundColor(0xFF0B0D14.toInt()); addView(web) }
        ViewCompat.setOnApplyWindowInsetsListener(frame) { v, insets ->
            val b = insets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
            v.setPadding(b.left, b.top, b.right, b.bottom)
            WindowInsetsCompat.CONSUMED
        }
        setContentView(frame)
        web.loadUrl("https://appassets.androidplatform.net/assets/web/index.html")
        val wanted = buildList {
            if (Build.VERSION.SDK_INT >= 31) { add(Manifest.permission.BLUETOOTH_CONNECT); add(Manifest.permission.BLUETOOTH_SCAN) }
            if (Build.VERSION.SDK_INT >= 33) add(Manifest.permission.POST_NOTIFICATIONS)
        }.filter { ContextCompat.checkSelfPermission(this, it) != android.content.pm.PackageManager.PERMISSION_GRANTED }
        if (wanted.isNotEmpty()) permissions.launch(wanted.toTypedArray())
        store.address?.let {
            SyncWorker.schedule(this)
            Presence.observe(this, it)  // also for apps paired before presence existed (0.1.0)
        }
        syncIfAsked(intent)
    }

    // For the tests: adb shell am start -n io.github.carlosns.c6clock/.MainActivity --ez sync true
    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        syncIfAsked(intent)
    }

    private fun syncIfAsked(i: Intent?) {
        // For the tests: read updates from the PC (adb reverse); never from any other host
        i?.getStringExtra("manifest")?.takeIf { it.startsWith("http://127.0.0.1:") }?.let { Firmware.manifestUrl = it }
        if (i?.getBooleanExtra("sync", false) == true) SyncWorker.runNow(this)
    }

    // The link itself lives in LinkService and stays up with the app closed; on screen it only runs
    // at a quicker interval and the page listens to it
    override fun onStart() {
        super.onStart()
        Live.visible = true
        Live.listen(onClockMessage, onClockState)
        Live.link?.priority(true)
        connectIfPaired()
    }

    override fun onStop() {
        super.onStop()
        Live.visible = false
        Live.unlisten(onClockMessage, onClockState)
        Live.link?.priority(false)
    }

    /** Makes sure the permanent link is running (it connects and reconnects by itself). */
    private fun connectIfPaired() {
        if (store.address == null || !pageReady || !Perms.canConnect(this)) return
        js("clockState", (link != null).toString())
        LinkService.start(this)
    }

    /** Lets the app read the music and call notifications (the system's own screen). */
    private fun allowPhone() {
        val ask: () -> Unit = {
            val cdm = getSystemService(CompanionDeviceManager::class.java)
            runCatching { cdm.requestNotificationAccess(android.content.ComponentName(this, PhoneWatch::class.java)) }
                .onFailure { startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)) }
            Unit
        }
        // Android 13+ blocks notification access for apps installed outside a store ("restricted
        // settings") until the user allows it in the app's own page: say how, and offer both screens
        if (Build.VERSION.SDK_INT < 33) return ask()
        AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
            .setTitle("Música y llamadas en el reloj")
            .setMessage(
                "Android protege este permiso en las apps instaladas fuera de Google Play. Si al activarlo dice " +
                    "«Se ha denegado el acceso», primero:\n\n1. Pulsa «Ajustes de Marea».\n2. Arriba a la derecha, " +
                    "toca ⋮ → «Permitir ajustes restringidos» y confírmalo.\n3. Vuelve aquí y pulsa «Activar».",
            )
            .setPositiveButton("Activar") { _, _ -> ask() }
            .setNeutralButton("Ajustes de Marea") { _, _ ->
                startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:$packageName")))
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    /** Calls window.fn(arg) in the page; from any thread (the WebView only accepts the main one). */
    private fun js(fn: String, arg: String) = runOnUiThread {
        if (pageReady) web.evaluateJavascript("window.$fn&&$fn($arg)", null)
    }

    // ---- pairing: the system's companion chooser, then the Bluetooth bond (code on the clock) ----

    private fun pick() {
        val filter = BluetoothLeDeviceFilter.Builder()
            .setScanFilter(ScanFilter.Builder().setServiceUuid(ParcelUuid(Protocol.SERVICE)).build())
            .build()
        val req = AssociationRequest.Builder().addDeviceFilter(filter).setSingleDevice(false).build()
        val cdm = getSystemService(CompanionDeviceManager::class.java)
        val cb = object : CompanionDeviceManager.Callback() {
            override fun onAssociationPending(sender: IntentSender) = chooser.launch(IntentSenderRequest.Builder(sender).build())
            @Deprecated("Android 12 and older")
            override fun onDeviceFound(sender: IntentSender) = chooser.launch(IntentSenderRequest.Builder(sender).build())
            override fun onFailure(error: CharSequence?) {
                js("clockPickFailed", JSONObject.quote("No se encontró ningún reloj (${error ?: "?"})"))
            }
        }
        if (Build.VERSION.SDK_INT >= 33) cdm.associate(req, Executor { it.run() }, cb)
        else @Suppress("DEPRECATION") cdm.associate(req, cb, null)
    }

    private fun associatedAddress(data: Intent?): String? {
        if (data == null) return null
        if (Build.VERSION.SDK_INT >= 33) {
            data.getParcelableExtra(CompanionDeviceManager.EXTRA_ASSOCIATION, AssociationInfo::class.java)
                ?.deviceMacAddress?.toString()?.uppercase()?.let { return it }
        }
        @Suppress("DEPRECATION")
        return when (val d = data.getParcelableExtra<android.os.Parcelable>(CompanionDeviceManager.EXTRA_DEVICE)) {
            is ScanResult -> d.device.address
            is BluetoothDevice -> d.address
            else -> null
        }
    }

    private fun paired(address: String) {
        lifecycleScope.launch {
            try {
                bond(address)
                store.address = address
                SyncWorker.schedule(this@MainActivity)
                Presence.observe(this@MainActivity, address)
                LinkService.stop(this@MainActivity)  // a link to another clock, if any
                delay(500)
                connectIfPaired()
            } catch (e: Exception) {
                js("clockPickFailed", JSONObject.quote(e.message ?: "No se pudo emparejar"))
            }
        }
    }

    /** Android asks for the 6-digit code the clock shows; waits until the bond exists. */
    private suspend fun bond(address: String) {
        val dev = getSystemService(BluetoothManager::class.java).adapter.getRemoteDevice(address)
        if (dev.bondState == BluetoothDevice.BOND_BONDED) return
        val done = CompletableDeferred<Int>()
        val rx = object : BroadcastReceiver() {
            override fun onReceive(c: Context, i: Intent) {
                @Suppress("DEPRECATION")
                val d = i.getParcelableExtra<BluetoothDevice>(BluetoothDevice.EXTRA_DEVICE)
                if (d?.address != address) return
                val s = i.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.BOND_NONE)
                if (s != BluetoothDevice.BOND_BONDING) done.complete(s)
            }
        }
        ContextCompat.registerReceiver(this, rx, IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED), ContextCompat.RECEIVER_EXPORTED)
        try {
            check(dev.createBond()) { "El móvil no pudo empezar el emparejamiento" }
            val s = withTimeout(90_000) { done.await() }
            check(s == BluetoothDevice.BOND_BONDED) { "No se emparejó: clave incorrecta o pasaron los 30 s. Vuelve a pulsar Buscar el reloj" }
        } finally {
            unregisterReceiver(rx)
        }
    }

    private fun forget() {
        val address = store.address ?: return
        lifecycleScope.launch {
            runCatching { link?.send("unpair") }  // the clock forgets this phone too
            LinkService.stop(this@MainActivity)
            Live.link?.close()
            SyncWorker.cancel(this@MainActivity)
            Presence.stop(this@MainActivity, address)
            store.address = null
            val cdm = getSystemService(CompanionDeviceManager::class.java)
            if (Build.VERSION.SDK_INT >= 33) cdm.myAssociations.filter { it.deviceMacAddress?.toString().equals(address, true) }.forEach { cdm.disassociate(it.id) }
            else @Suppress("DEPRECATION") cdm.disassociate(address)
            runCatching {  // drop the bond as well (hidden API, but the only way short of system settings)
                val dev = getSystemService(BluetoothManager::class.java).adapter.getRemoteDevice(address)
                dev.javaClass.getMethod("removeBond").invoke(dev)
            }
            web.reload()
        }
    }

    /** Opens the system screen that lets the app run in the background (HyperOS and others are strict). */
    private fun allowBackground() {
        val pm = getSystemService(PowerManager::class.java)
        val i = if (pm.isIgnoringBatteryOptimizations(packageName)) {
            Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:$packageName"))
        } else {
            @SuppressLint("BatteryLife")
            Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:$packageName"))
        }
        startActivity(i)
    }

    // ---- the app's own updates (AppUpdate); the page shows them in its "Actualizaciones" card ----
    private var appOffer: JSONObject? = null
    private var installing = false

    private fun checkApp() {
        lifecycleScope.launch {
            appOffer = AppUpdate.check(this@MainActivity)
            js("appOffer", appOffer?.let { JSONObject.quote(it.toString()) } ?: "null")
        }
    }

    private fun installApp() {
        val app = appOffer ?: return
        if (!AppUpdate.mayInstall(this)) {
            js("appUpdateState", JSONObject.quote("Activa «Permitir de esta fuente» para Marea, vuelve y pulsa otra vez"))
            startActivity(AppUpdate.allowInstallIntent(this))
            return
        }
        if (installing) return
        installing = true
        lifecycleScope.launch {
            try {
                AppUpdate.install(this@MainActivity, app) { js("appUpdateState", JSONObject.quote(it)) }
                js("appUpdateState", JSONObject.quote("Confirma la instalación en la ventana de Android"))
            } catch (e: Exception) {
                js("appUpdateState", JSONObject.quote("No se pudo actualizar: ${e.message}"))
            } finally {
                installing = false
            }
        }
    }

    // ---- the clock's firmware, through the phone (ClockUpdate): no Wi-Fi needed on the clock ----
    private var updatingClock = false

    private fun updateClock(entry: JSONObject) {
        fun say(t: String) = js("clockUpdateState", JSONObject.quote(t))
        val l = link ?: return say("El reloj no está conectado")
        if (updatingClock) return
        updatingClock = true
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)  // the transfer takes a few minutes
        lifecycleScope.launch {
            var file: java.io.File? = null
            try {
                say("Descargando la ${entry.getString("version")}…")
                file = ClockUpdate.download(this@MainActivity, entry) { say("Descargando… $it %") }
                say("Pasando al reloj…")
                ClockUpdate.send(l, file, entry) { say("Pasando al reloj… $it %") }
                say("Listo: el reloj se reinicia con la ${entry.getString("version")}")
            } catch (e: Exception) {
                say("No se pudo actualizar: ${e.message}. El reloj sigue con su versión.")
            } finally {
                file?.delete()
                updatingClock = false
                window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            }
        }
    }

    /** What the page may call. Runs on a WebView thread: work goes to the UI thread / lifecycle scope. */
    inner class Bridge {
        @JavascriptInterface fun ready() = runOnUiThread {
            pageReady = true
            js("clockState", (link?.ready == true).toString())
            connectIfPaired()
        }
        @JavascriptInterface fun paired() = store.address != null
        @JavascriptInterface fun pick() = runOnUiThread { this@MainActivity.pick() }
        @JavascriptInterface fun send(cmd: String): Boolean {
            val l = link?.takeIf { it.ready } ?: return false
            lifecycleScope.launch { runCatching { l.send(cmd) }.onFailure { Log.w(TAG, "send: ${it.message}") } }
            return true
        }
        @JavascriptInterface fun info(): String = JSONObject()
            .put("address", store.address ?: "")
            .put("lastSync", store.lastSyncMs)
            .put("lastResult", store.lastResult)
            .put("every", SyncWorker.PERIOD_MIN)
            .put("permanent", LinkService.running)
            .put("phone", Phone.hasAccess(this@MainActivity))
            .put("background", getSystemService(PowerManager::class.java).isIgnoringBatteryOptimizations(packageName))
            .toString()
        @JavascriptInterface fun syncNow() {
            val l = link ?: return SyncWorker.runNow(this@MainActivity)
            lifecycleScope.launch {  // the open link syncs at once; without it, the worker connects
                runCatching { SyncWorker.record(store, SyncSession(l, Net::weatherCommand).run()) }
                    .onFailure { store.lastResult = "falló: ${it.message}" }
            }
        }
        @JavascriptInterface fun forget() = runOnUiThread { this@MainActivity.forget() }
        @JavascriptInterface fun allowBackground() = runOnUiThread { this@MainActivity.allowBackground() }
        @JavascriptInterface fun allowPhone() = runOnUiThread { this@MainActivity.allowPhone() }
        @JavascriptInterface fun appVersion(): String = AppUpdate.installedName(this@MainActivity)
        @JavascriptInterface fun checkApp() = runOnUiThread { this@MainActivity.checkApp() }
        @JavascriptInterface fun installApp() = runOnUiThread { this@MainActivity.installApp() }
        @JavascriptInterface fun canUpdateClock() = Live.link?.takeIf { it.ready }?.canOta == true
        @JavascriptInterface fun updateClock(entry: String) {
            val e = runCatching { JSONObject(entry) }.getOrNull() ?: return
            runOnUiThread { this@MainActivity.updateClock(e) }
        }
    }

    companion object {
        private const val TAG = "C6Clock"
    }
}
