package io.github.carlosns.c6clock

import android.companion.AssociationInfo
import android.companion.CompanionDeviceManager
import android.companion.CompanionDeviceService
import android.content.Context
import android.os.Build
import android.util.Log
import androidx.annotation.RequiresApi

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

    private fun appeared(address: String) {
        val store = Store(applicationContext)
        if (!address.equals(store.address, true)) return
        // Each sync drops the link and the clock advertises again: don't let that start another one
        val ago = System.currentTimeMillis() - store.lastSyncMs
        Log.i(TAG, "clock appeared, last sync ${ago / 1000} s ago")
        if (ago > MIN_GAP_MS) SyncWorker.runSoon(applicationContext)
    }

    private companion object {
        const val TAG = "ClockPresence"
        const val MIN_GAP_MS = 2 * 60_000L
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
