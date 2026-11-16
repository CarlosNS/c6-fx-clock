package io.github.carlosns.c6clock

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONObject

/**
 * The GATT link to one paired clock. connect() opens it (MTU, services, notifications on EVT; the
 * encrypted CCCD write makes Android pair if the bond is missing), then commands go out one by one
 * and every whole message is handed to [onMessage] and to waiters. Android allows one GATT
 * operation at a time, hence the mutex.
 */
@SuppressLint("MissingPermission")  // callers check BLUETOOTH_CONNECT
class ClockBle(context: Context, val address: String) : ClockChannel {
    private val context: Context = context.applicationContext  // outlives the activity (shared with the worker)
    var onMessage: (String) -> Unit = {}
    var onState: (Boolean) -> Unit = {}
    @Volatile var ready = false
        private set

    private var gatt: BluetoothGatt? = null
    private var cmd: BluetoothGattCharacteristic? = null
    private var ota: BluetoothGattCharacteristic? = null  // firmware packets (clocks from 3.0.0-beta.6)
    @Volatile var mtu = 23
        private set
    private var refreshed = false  // service cache already thrown away on this connection
    // A service discovery in flight, another one asked for meanwhile ("Service Changed"), and how many
    // times this connection looked again after finding nothing
    @Volatile private var inFlight = false
    @Volatile private var again = false
    private var retries = 0
    /** The GATT link is up and being set up (services, notifications): not to be cut by a new attempt. */
    @Volatile var linking = false
        private set
    // The MTU answer may never come: Android answers only one of the apps asking on the same link (after
    // the watch restarts, several reconnect at once), and the setup must not wait on it for ever
    @Volatile private var discovering = false
    private var mtuWait: CompletableDeferred<Unit>? = null
    private val main = Handler(Looper.getMainLooper())
    /** The clock takes its firmware over Bluetooth (ClockUpdate). */
    val canOta get() = ota != null
    private val ops = Mutex()
    private val GATT_INSUFFICIENT_RESOURCES = 0x11
    private var pending: CompletableDeferred<Int>? = null  // status of the GATT operation in flight
    private val messages = MutableSharedFlow<JSONObject>(extraBufferCapacity = 32)
    private val setup = Channel<Int>(Channel.CONFLATED)
    private val reassembler = Reassembler()

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, state: Int) {
            Log.i(TAG, "connection state $state (status $status)")
            if (state == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
                refreshed = false
                discovering = false
                inFlight = false
                again = false
                retries = 0
                linking = true
                // The setup must end in a while, or the attempt fails and the link is closed: a stuck
                // one (Android's GATT queue out of step) would otherwise wait for ever and, being
                // "linking", keep any new attempt (and the Reconnect button) away
                main.postDelayed({
                    if (gatt === g && !ready && linking) {
                        Log.w(TAG, "setup did not finish in ${SETUP_MS / 1000} s")
                        setup.trySend(-4)
                    }
                }, SETUP_MS)
                g.requestMtu(Protocol.MTU)
                main.postDelayed({
                    if (!discovering && gatt === g) {
                        Log.i(TAG, "no MTU answer: going on without it")
                        discovering = true
                        discover(g)
                    }
                }, 2_500)
            } else {
                val was = ready
                ready = false
                linking = false
                cmd = null
                ota = null
                pending?.complete(-1)
                setup.trySend(-1)
                g.close()
                if (gatt === g) gatt = null
                if (was) onState(false)
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) this@ClockBle.mtu = mtu
            mtuWait?.complete(Unit)
            if (!discovering) {
                discovering = true
                discover(g)
            }
        }

        override fun onServiceChanged(g: BluetoothGatt) {
            // The watch's services table changed (a new firmware, Android 12+). It may arrive while a
            // discovery is running: Android then throws its copy away halfway and that discovery finds
            // nothing, so look again once it ends instead of starting a second one on top
            Log.i(TAG, "services changed, discovering again")
            if (inFlight) again = true else discover(g)
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            inFlight = false
            if (again) {
                again = false
                discover(g)
                return
            }
            val svc = g.getService(Protocol.SERVICE)
            val evt = svc?.getCharacteristic(Protocol.EVT)
            // Nothing found (or a failed discovery): usually Android's cache emptied by a "Service
            // Changed" just after a firmware update, not another device. Throw the cache away and look
            // again a moment later, three times, before giving up
            if ((status != BluetoothGatt.GATT_SUCCESS || svc == null) && retries < 3 && gatt === g) {
                retries++
                val ok = runCatching { g.javaClass.getMethod("refresh").invoke(g) as Boolean }.getOrDefault(false)
                Log.i(TAG, "no clock service (status $status): looking again ($retries, cache refreshed $ok)")
                main.postDelayed({ if (gatt === g) discover(g) }, 600L * retries)
                return
            }
            // Android keeps a cached copy of a paired device's services, and the watch never says they
            // changed ("Service Changed" upset Android's GATT queue): when one the app uses is missing,
            // once per connection, throw the cache away and look again
            val missing = svc != null && (evt == null || svc.getCharacteristic(Protocol.CMD) == null || svc.getCharacteristic(Protocol.OTA) == null)
            if (missing && !refreshed) {
                refreshed = true
                val ok = runCatching { g.javaClass.getMethod("refresh").invoke(g) as Boolean }.getOrDefault(false)
                Log.i(TAG, "a characteristic is missing: refreshing the service cache ($ok)")
                if (ok) { discover(g); if (inFlight) return }
            }
            cmd = svc?.getCharacteristic(Protocol.CMD)
            ota = svc?.getCharacteristic(Protocol.OTA)
            if (ready) return  // a second discovery on a working link: only the references change
            val cccd = evt?.getDescriptor(Protocol.CCCD)
            if (evt == null || cccd == null || cmd == null) {
                Log.w(TAG, "not a Marea clock (status $status)")
                setup.trySend(-2)
                return
            }
            g.setCharacteristicNotification(evt, true)
            val on = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            if (Build.VERSION.SDK_INT >= 33) g.writeDescriptor(cccd, on)
            else @Suppress("DEPRECATION") run { cccd.value = on; g.writeDescriptor(cccd) }
        }

        override fun onReadRemoteRssi(g: BluetoothGatt, rssi: Int, status: Int) {
            rssiWait?.complete(if (status == BluetoothGatt.GATT_SUCCESS) rssi else null)
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            setup.trySend(status)
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            pending?.complete(status)
        }

        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) {
            received(value)
        }

        @Deprecated("Android 12 and older")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            @Suppress("DEPRECATION")
            if (Build.VERSION.SDK_INT < 33) received(c.value)
        }
    }

    private fun discover(g: BluetoothGatt) {
        inFlight = g.discoverServices()
    }

    private fun received(value: ByteArray) {
        val whole = reassembler.feed(value) ?: return
        val text = whole.toString(Charsets.UTF_8)
        val msg = runCatching { JSONObject(text) }.getOrNull() ?: return
        messages.tryEmit(msg)
        onMessage(text)
    }

    /**
     * Opens the link; throws on failure. Safe to call when already connected. auto: Android's own
     * background connection, which waits (as long as it takes, cheaply) and connects as soon as the
     * watch advertises; otherwise a direct attempt that searches actively for timeoutMs.
     */
    suspend fun connect(timeoutMs: Long = 20_000, auto: Boolean = false) {
        if (ready) return
        val bt = context.getSystemService(BluetoothManager::class.java).adapter
        val dev: BluetoothDevice = bt.getRemoteDevice(address)
        while (setup.tryReceive().isSuccess) Unit
        gatt = dev.connectGatt(context, auto, callback, BluetoothDevice.TRANSPORT_LE)
        val status = runCatching { withTimeout(timeoutMs) { setup.receive() } }.getOrDefault(-3)
        if (status != BluetoothGatt.GATT_SUCCESS) {
            close()
            throw IllegalStateException(
                when (status) {
                    -2 -> "Ese dispositivo no es un reloj Marea"
                    -3 -> "El reloj no responde"
                    -4 -> "La conexión no terminó de prepararse"
                    else -> "No se pudo conectar ($status)"
                },
            )
        }
        // Set up without the MTU answer: ask once more now that nothing else is in flight (the bigger
        // packets matter for files and firmware), and go on whatever happens
        if (mtu <= 23) {
            val w = CompletableDeferred<Unit>()
            mtuWait = w
            if (gatt?.requestMtu(Protocol.MTU) == true) withTimeoutOrNull(2_000) { w.await() }
            mtuWait = null
            Log.i(TAG, "MTU $mtu")
        }
        ready = true
        linking = false
        onState(true)
    }

    override suspend fun send(cmd: String) {
        write(this.cmd, cmd.toByteArray(Charsets.UTF_8), BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
    }

    /** One firmware packet, without response: Android still reports when it left, which paces us. */
    suspend fun writeOta(data: ByteArray) {
        write(ota, data, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE)
    }

    private suspend fun write(c: BluetoothGattCharacteristic?, data: ByteArray, type: Int) {
        ops.withLock {
            val g = gatt ?: throw IllegalStateException("sin conexión")
            c ?: throw IllegalStateException("sin conexión")
            // The watch may answer "insufficient resources" (0x11) for a moment, while its buffers are
            // busy sending: that write is tried again a little later, up to three times
            for (attempt in 1..3) {
            val done = CompletableDeferred<Int>().also { pending = it }
            // The stack may be busy for a moment (its queue is full): wait and try again
            var tries = 0
            while (true) {
                val started = if (Build.VERSION.SDK_INT >= 33) {
                    g.writeCharacteristic(c, data, type) == BluetoothStatusCodes.SUCCESS
                } else @Suppress("DEPRECATION") run {
                    c.writeType = type
                    c.value = data
                    g.writeCharacteristic(c)
                }
                if (started) break
                if (++tries > 100 || gatt == null) {
                    // Two seconds of refusals: Android has lost the link without telling us ("no
                    // connection" in its log) and this one would look connected for ever
                    drop("writes refused")
                    throw IllegalStateException("no se pudo enviar")
                }
                delay(20)
            }
            val status = withTimeoutOrNull(5_000) { done.await() }
            if (status == null) {  // the stack never finished the write: it will refuse every next one
                drop("write never finished")
                throw IllegalStateException("el reloj no responde")
            }
            if (status == BluetoothGatt.GATT_SUCCESS) return@withLock
            if (status != GATT_INSUFFICIENT_RESOURCES || attempt == 3) throw IllegalStateException("envío rechazado ($status)")
            Log.i(TAG, "write refused for lack of resources, again in a moment")
            delay(300)
            }
        }
    }

    /** Gives up on a link Android dropped silently: closes it and tells [onState], so it reconnects. */
    fun drop(why: String) {
        val was = ready
        Log.w(TAG, "link lost: $why")
        close()
        pending?.complete(-1)
        if (was) onState(false)
    }

    /** Android still has the link: false when its GATT stack lost it without a callback. */
    fun alive(): Boolean {
        val bm = context.getSystemService(BluetoothManager::class.java)
        val dev = runCatching { bm.adapter.getRemoteDevice(address) }.getOrNull() ?: return false
        return runCatching { bm.getConnectionState(dev, BluetoothProfile.GATT) == BluetoothProfile.STATE_CONNECTED }.getOrDefault(true)
    }

    private var rssiWait: CompletableDeferred<Int?>? = null

    /** The link's signal strength in dBm (how close the watch is), or null. */
    suspend fun rssi(): Int? = ops.withLock {
        val g = gatt ?: return@withLock null
        val w = CompletableDeferred<Int?>().also { rssiWait = it }
        if (!g.readRemoteRssi()) return@withLock null
        withTimeoutOrNull(2_000) { w.await() }
    }

    /** Every whole message from the clock (also given to [onMessage]). */
    fun events(): SharedFlow<JSONObject> = messages

    /** The fastest connection interval, for sending firmware. */
    fun fast() {
        gatt?.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH)
    }

    override suspend fun exchange(cmd: String, t: String, timeoutMs: Long, match: (JSONObject) -> Boolean): JSONObject =
        coroutineScope {
            withTimeout(timeoutMs) {
                val answer = async(start = CoroutineStart.UNDISPATCHED) { messages.first { it.optString("t") == t && match(it) } }
                send(cmd)
                answer.await()
            }
        }

    /** Quick connection interval while someone looks at the page, low power the rest of the time. */
    fun priority(high: Boolean) {
        gatt?.requestConnectionPriority(
            if (high) BluetoothGatt.CONNECTION_PRIORITY_BALANCED else BluetoothGatt.CONNECTION_PRIORITY_LOW_POWER,
        )
    }

    fun close() {
        ready = false
        linking = false
        gatt?.let {
            it.disconnect()
            it.close()
        }
        gatt = null
    }

    companion object {
        private const val TAG = "ClockBle"
        private const val SETUP_MS = 20_000L
    }
}
