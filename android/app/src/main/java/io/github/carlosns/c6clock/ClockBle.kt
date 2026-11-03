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
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeout
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
    private val ops = Mutex()
    private var pending: CompletableDeferred<Int>? = null  // status of the GATT operation in flight
    private val messages = MutableSharedFlow<JSONObject>(extraBufferCapacity = 32)
    private val setup = Channel<Int>(Channel.CONFLATED)
    private val reassembler = Reassembler()

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, state: Int) {
            Log.i(TAG, "connection state $state (status $status)")
            if (state == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
                g.requestMtu(Protocol.MTU)
            } else {
                val was = ready
                ready = false
                cmd = null
                pending?.complete(-1)
                setup.trySend(-1)
                g.close()
                if (gatt === g) gatt = null
                if (was) onState(false)
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            g.discoverServices()
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val svc = g.getService(Protocol.SERVICE)
            val evt = svc?.getCharacteristic(Protocol.EVT)
            cmd = svc?.getCharacteristic(Protocol.CMD)
            val cccd = evt?.getDescriptor(Protocol.CCCD)
            if (evt == null || cccd == null || cmd == null) {
                Log.w(TAG, "not a C6 clock")
                setup.trySend(-2)
                return
            }
            g.setCharacteristicNotification(evt, true)
            val on = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            if (Build.VERSION.SDK_INT >= 33) g.writeDescriptor(cccd, on)
            else @Suppress("DEPRECATION") run { cccd.value = on; g.writeDescriptor(cccd) }
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
                    -2 -> "Ese dispositivo no es un reloj C6"
                    -3 -> "El reloj no responde"
                    else -> "No se pudo conectar ($status)"
                },
            )
        }
        ready = true
        onState(true)
    }

    override suspend fun send(cmd: String) {
        ops.withLock {
            val g = gatt ?: throw IllegalStateException("sin conexión")
            val c = this.cmd ?: throw IllegalStateException("sin conexión")
            val data = cmd.toByteArray(Charsets.UTF_8)
            val done = CompletableDeferred<Int>().also { pending = it }
            val type = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            val started = if (Build.VERSION.SDK_INT >= 33) {
                g.writeCharacteristic(c, data, type) == BluetoothStatusCodes.SUCCESS
            } else @Suppress("DEPRECATION") run {
                c.writeType = type
                c.value = data
                g.writeCharacteristic(c)
            }
            if (!started) throw IllegalStateException("no se pudo enviar")
            val status = withTimeout(5_000) { done.await() }
            if (status != BluetoothGatt.GATT_SUCCESS) throw IllegalStateException("envío rechazado ($status)")
        }
    }

    override suspend fun exchange(cmd: String, t: String, timeoutMs: Long, match: (JSONObject) -> Boolean): JSONObject =
        coroutineScope {
            withTimeout(timeoutMs) {
                val answer = async(start = CoroutineStart.UNDISPATCHED) { messages.first { it.optString("t") == t && match(it) } }
                send(cmd)
                answer.await()
            }
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
