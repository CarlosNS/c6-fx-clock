package io.github.carlosns.c6clock

import android.Manifest
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
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.activity.ComponentActivity
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import androidx.webkit.WebViewAssetLoader
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Job
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
    private var link: ClockBle? = null
    private var keeper: Job? = null
    private var pageReady = false

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
            webChromeClient = WebChromeClient()  // confirm() dialogs
            webViewClient = object : WebViewClient() {
                override fun shouldInterceptRequest(view: WebView, req: WebResourceRequest): WebResourceResponse? =
                    assets.shouldInterceptRequest(req.url)
            }
            addJavascriptInterface(Bridge(), "ClockBridge")
        }
        setContentView(web)
        web.loadUrl("https://appassets.androidplatform.net/assets/web/index.html")
        val wanted = buildList {
            if (Build.VERSION.SDK_INT >= 31) add(Manifest.permission.BLUETOOTH_CONNECT)
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
        if (i?.getBooleanExtra("sync", false) == true) SyncWorker.runNow(this)
    }

    override fun onStart() {
        super.onStart()
        connectIfPaired()
    }

    // In the background the periodic sync connects by itself: the open link would only cost battery
    override fun onStop() {
        super.onStop()
        disconnect()
    }

    /** Keeps the link up while the app is visible, retrying with growing pauses. */
    private fun connectIfPaired() {
        val address = store.address ?: return
        if (!pageReady || !Perms.canConnect(this) || keeper?.isActive == true) return
        keeper = lifecycleScope.launch {
            var wait = 1_000L
            while (true) {
                val l = link?.takeIf { it.ready } ?: ClockBle(this@MainActivity, address).also { l ->
                    l.onMessage = { s -> runOnUiThread { js("clockMessage", JSONObject.quote(s)) } }
                    l.onState = { up -> runOnUiThread { js("clockState", up.toString()) } }
                }
                try {
                    l.connect()
                    link = l
                    Live.link = l
                    wait = 1_000L
                    while (l.ready) delay(1_000)
                } catch (e: Exception) {
                    Log.i(TAG, "connect: ${e.message}")
                    l.close()
                }
                delay(wait)
                wait = minOf(wait * 2, 15_000)
            }
        }
    }

    private fun disconnect() {
        keeper?.cancel()
        keeper = null
        link?.close()
        link = null
        Live.link = null
        js("clockState", "false")
    }

    private fun js(fn: String, arg: String) {
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
                disconnect()
                connectIfPaired()
                SyncWorker.runNow(this@MainActivity)
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
            disconnect()
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
            .put("background", getSystemService(PowerManager::class.java).isIgnoringBatteryOptimizations(packageName))
            .toString()
        @JavascriptInterface fun syncNow() = SyncWorker.runNow(this@MainActivity)
        @JavascriptInterface fun forget() = runOnUiThread { this@MainActivity.forget() }
        @JavascriptInterface fun allowBackground() = runOnUiThread { this@MainActivity.allowBackground() }
    }

    companion object {
        private const val TAG = "C6Clock"
    }
}
