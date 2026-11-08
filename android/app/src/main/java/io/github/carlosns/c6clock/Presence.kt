package io.github.carlosns.c6clock

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.companion.AssociationInfo
import android.companion.CompanionDeviceManager
import android.companion.CompanionDeviceService
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import androidx.annotation.RequiresApi
import androidx.core.content.ContextCompat

/**
 * Android wakes this service when the paired clock comes into range or is switched on (it starts
 * advertising again). The clock may have lost the time with the power, so it gets a sync right away
 * instead of waiting up to 30 min for the periodic one. Android 12+; older phones only have the period.
 */
@RequiresApi(31)
class ClockPresence : CompanionDeviceService() {

    @Deprecated("Android 12 entry point; 13+ calls the AssociationInfo one (and may still call this too)")
    override fun onDeviceAppeared(address: String) {
        if (Build.VERSION.SDK_INT < 33) appeared(address)
    }

    @RequiresApi(33)
    override fun onDeviceAppeared(associationInfo: AssociationInfo) =
        appeared(associationInfo.deviceMacAddress?.toString() ?: "")

    @Deprecated("Android 12 entry point; 13+ calls the AssociationInfo one (and may still call this too)")
    override fun onDeviceDisappeared(address: String) {
        if (Build.VERSION.SDK_INT < 33) disappeared(address)
    }

    @RequiresApi(33)
    override fun onDeviceDisappeared(associationInfo: AssociationInfo) =
        disappeared(associationInfo.deviceMacAddress?.toString() ?: "")

    private fun disappeared(address: String) {
        val store = Store(applicationContext)
        if (!address.equals(store.address, true)) return
        if (store.goneSinceMs == 0L) store.goneSinceMs = System.currentTimeMillis()
        LinkService.away(applicationContext)
    }

    private fun appeared(address: String) {
        val store = Store(applicationContext)
        if (!address.equals(store.address, true)) return
        // The permanent link: it connects, syncs and stays (it reconnects by itself after a drop).
        // Only if the system refuses to start it, the one-off sync below
        if (LinkService.start(applicationContext)) {
            store.goneSinceMs = 0
            return
        }
        val now = System.currentTimeMillis()
        val ago = now - store.lastSyncMs
        val gone = if (store.goneSinceMs > 0) now - store.goneSinceMs else 0
        store.goneSinceMs = 0
        // Android's presence scan loses a nearby clock for a few seconds now and then: only a real
        // absence (switched off, carried away; a paired clock that boots without the time stays
        // silent 60 s on purpose, src/ble.c) or a long time without sync is worth a connection.
        val sync = ago > STALE_MS || gone >= AWAY_MS
        Log.i(TAG, "clock appeared, gone ${gone / 1000} s, last sync ${ago / 1000} s ago" + if (sync) ": sync" else "")
        if (sync) SyncWorker.runSoon(applicationContext)
        // A quick power cycle can go unnoticed: the clock says in its advertising whether it has the time
        else if (ago > MIN_GAP_MS) Advert.peek(applicationContext, address) { hasTime ->
            Log.i(TAG, "advert: time ${if (hasTime) "ok" else "missing: sync"}")
            if (!hasTime) SyncWorker.runSoon(applicationContext)
        }
    }

    private companion object {
        const val TAG = "ClockPresence"
        const val MIN_GAP_MS = 2 * 60_000L
        const val AWAY_MS = 30_000L
        const val STALE_MS = 30 * 60_000L
    }
}

/** Turns ClockPresence on and off; does nothing before Android 12. */
object Presence {
    /** Asks Android to watch for the clock; safe to call again (on every app start). */
    fun observe(c: Context, address: String) {
        if (Build.VERSION.SDK_INT < 31) return
        runCatching { c.getSystemService(CompanionDeviceManager::class.java).startObservingDevicePresence(address) }
            .onFailure { Log.w("ClockPresence", "observe: ${it.message}") }
    }

    fun stop(c: Context, address: String) {
        if (Build.VERSION.SDK_INT < 31) return
        runCatching { c.getSystemService(CompanionDeviceManager::class.java).stopObservingDevicePresence(address) }
    }
}

/**
 * Reads the clock's advertising for a moment: manufacturer data under the test company id 0xFFFF,
 * first byte bit 0 = the clock has the time (src/ble.c). Calls [result] once, only if the clock was
 * heard within [TIMEOUT_MS]; older firmware without the byte counts as "has the time".
 */
@SuppressLint("MissingPermission")  // checked in canScan
object Advert {
    private const val COMPANY = 0xFFFF
    private const val TIMEOUT_MS = 4000L

    fun canScan(c: Context) = Build.VERSION.SDK_INT >= 31 &&
        ContextCompat.checkSelfPermission(c, Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED

    fun peek(c: Context, address: String, result: (hasTime: Boolean) -> Unit) {
        if (!canScan(c)) return
        val scanner = c.getSystemService(BluetoothManager::class.java)?.adapter?.bluetoothLeScanner ?: return
        val main = Handler(Looper.getMainLooper())
        var done = false
        val cb = object : ScanCallback() {
            override fun onScanResult(type: Int, r: ScanResult) {
                if (done) return
                done = true
                runCatching { scanner.stopScan(this) }
                val data = r.scanRecord?.getManufacturerSpecificData(COMPANY)
                result(data == null || data.isEmpty() || (data[0].toInt() and 1) == 1)
            }
        }
        val filter = ScanFilter.Builder().setDeviceAddress(address.uppercase()).build()
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        runCatching { scanner.startScan(listOf(filter), settings, cb) }.onFailure { return }
        main.postDelayed({ if (!done) { done = true; runCatching { scanner.stopScan(cb) } } }, TIMEOUT_MS)
    }
}
