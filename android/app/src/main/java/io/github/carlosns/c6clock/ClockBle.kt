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
    /** The clock takes its firmware over Bluetooth (ClockUpdate). */
    val canOta get() = ota != null
    private val ops = Mutex()
    private var pending: CompletableDeferred<Int>? = null  // status of the GATT operation in flight
    private val messages = MutableSharedFlow<JSONObject>(extraBufferCapacity = 32)
    private val setup = Channel<Int>(Channel.CONFLATED)
    private val reassembler = Reassembler()

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, state: Int) {
            Log.i(TAG, "connection state $state (status $status)")
            if (state == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
                refreshed = false
                g.requestMtu(Protocol.MTU)
            } else {
                val was = ready
                ready = false
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
            g.discoverServices()
        }

        override fun onServiceChanged(g: BluetoothGatt) {
            // The watch got a new firmware and says its services changed (Android 12+)
            Log.i(TAG, "services changed, discovering again")
            g.discoverServices()
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val svc = g.getService(Protocol.SERVICE)
            val evt = svc?.getCharacteristic(Protocol.EVT)
            // Android keeps a cached copy of a paired device's services: after a firmware update it may
            // miss new characteristics. Once per connection, throw the cache away and look again.
            if (svc != null && svc.getCharacteristic(Protocol.OTA) == null && !refreshed) {
                refreshed = true
                val ok = runCatching { g.javaClass.getMethod("refresh").invoke(g) as Boolean }.getOrDefault(false)
                Log.i(TAG, "no firmware characteristic: refreshing the service cache ($ok)")
                if (ok && g.discoverServices()) return
            }
            cmd = svc?.getCharacteristic(Protocol.CMD)
            ota = svc?.getCharacteristic(Protocol.OTA)
            if (ready) return  // a second discovery on a working link: only the references change
            val cccd = evt?.getDescriptor(Protocol.CCCD)
            if (evt == null || cccd == null || cmd == null) {
                Log.w(TAG, "not a Marea clock")
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

    private fun received(value: ByteArray) {
        val whole = reassembler.feed(value) ?: return
        val text = whole.toString(Charsets.UTF_8)
        val msg = runCatching { JSONObject(text) }.getOrNull() ?: return
        messages.tryEmit(msg)
        onMessage(text)
    }

    /** Opens the link; throws on failure. Safe to call when already connected. */
    suspend fun connect(timeoutMs: Long = 20_000) {
        if (ready) return
        val bt = context.getSystemService(BluetoothManager::class.java).adapter
        val dev: BluetoothDevice = bt.getRemoteDevice(address)
        while (setup.tryReceive().isSuccess) Unit
        gatt = dev.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE)
        val status = runCatching { withTimeout(timeoutMs) { setup.receive() } }.getOrDefault(-3)
        if (status != BluetoothGatt.GATT_SUCCESS) {
            close()
            throw IllegalStateException(
                when (status) {
                    -2 -> "Ese dispositivo no es un reloj Marea"
                    -3 -> "El reloj no responde"
                    else -> "No se pudo conectar ($status)"
                },
            )
        }
        ready = true
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
            if (status != BluetoothGatt.GATT_SUCCESS) throw IllegalStateException("envío rechazado ($status)")
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
        gatt?.let {
            it.disconnect()
            it.close()
        }
        gatt = null
    }

    companion object {
        private const val TAG = "ClockBle"
    }
}
