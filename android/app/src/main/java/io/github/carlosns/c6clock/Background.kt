package io.github.carlosns.c6clock

import android.Manifest
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import androidx.work.CoroutineWorker
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.ExistingWorkPolicy
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.PeriodicWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.WorkerParameters
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.TimeUnit

/** What the app remembers: the paired clock and the outcome of the last sync. */
class Store(context: Context) {
    private val p = context.getSharedPreferences("clock", Context.MODE_PRIVATE)
    var address: String?
        get() = p.getString("address", null)
        set(v) = p.edit().putString("address", v).apply()
    var lastSyncMs: Long
        get() = p.getLong("lastSync", 0)
        set(v) = p.edit().putLong("lastSync", v).apply()
    var lastResult: String
        get() = p.getString("lastResult", "") ?: ""
        set(v) = p.edit().putString("lastResult", v).apply()
    var clockVersion: String
        get() = p.getString("clockVersion", "") ?: ""
        set(v) = p.edit().putString("clockVersion", v).apply()
    /** Newest firmware already announced, so each version is notified once. */
    var notifiedVersion: String
        get() = p.getString("notified", "") ?: ""
        set(v) = p.edit().putString("notified", v).apply()
}

object Net {
    fun getJson(url: String, timeoutMs: Int = 15_000): JSONObject {
        val c = URL(url).openConnection() as HttpURLConnection
        c.connectTimeout = timeoutMs
        c.readTimeout = timeoutMs
        try {
            check(c.responseCode == 200) { "HTTP ${c.responseCode}" }
            return JSONObject(c.inputStream.bufferedReader().readText())
        } finally {
            c.disconnect()
        }
    }

    suspend fun weatherCommand(lat: Double, lon: Double): String =
        withContext(Dispatchers.IO) { Protocol.weatherCommand(getJson(Protocol.weatherUrl(lat, lon))) }
}

/** Firmware published on GitHub Pages (phase 6 fills it in; until then the check finds nothing). */
object Firmware {
    const val MANIFEST = "https://carlosns.github.io/c6-fx-clock/manifest.json"

    /** -1, 0 or 1, comparing "major.minor.patch" numerically ("2.10.0" is newer than "2.9.1"). */
    fun compare(a: String, b: String): Int {
        val x = a.split('.').map { it.toIntOrNull() ?: 0 }
        val y = b.split('.').map { it.toIntOrNull() ?: 0 }
        for (i in 0 until maxOf(x.size, y.size)) {
            val d = x.getOrElse(i) { 0 }.compareTo(y.getOrElse(i) { 0 })
            if (d != 0) return d
        }
        return 0
    }

    /** The newer version on offer for a clock running [running], or null. */
    fun newer(manifest: JSONObject, running: String): String? =
        manifest.optString("version").takeIf { it.isNotEmpty() && running.isNotEmpty() && compare(it, running) > 0 }
}

/**
 * The periodic sync (docs/PLAN-bluetooth.md, 3.6): every 30 min, even with the app closed. If the
 * app is open and connected it reuses that link, otherwise it connects for a few seconds.
 */
class SyncWorker(context: Context, params: WorkerParameters) : CoroutineWorker(context, params) {
    override suspend fun doWork(): Result {
        val store = Store(applicationContext)
        val address = store.address ?: return Result.success()
        if (!Perms.canConnect(applicationContext)) return Result.success()
        val shared = Live.link?.takeIf { it.ready && it.address == address }
        val link = shared ?: ClockBle(applicationContext, address)
        return try {
            if (shared == null) link.connect()
            val r = SyncSession(link, Net::weatherCommand).run()
            store.lastSyncMs = System.currentTimeMillis()
            store.clockVersion = r.version
            store.lastResult = "ok" + if (r.weatherSent) "" else " (sin tiempo: no hay internet)"
            Log.i(TAG, "sync ok: ${r.version}, ${r.place}, #${r.syncCount}")
            checkFirmware(store, r.version)
            Result.success()
        } catch (e: Exception) {
            Log.w(TAG, "sync failed: ${e.message}")
            store.lastResult = "falló: ${e.message}"
            Result.success()  // the next period tries again; a retry storm would only drain the battery
        } finally {
            if (shared == null) link.close()
        }
    }

    private suspend fun checkFirmware(store: Store, running: String) {
        val manifest = runCatching { withContext(Dispatchers.IO) { Net.getJson(Firmware.MANIFEST) } }.getOrNull() ?: return
        val v = Firmware.newer(manifest, running) ?: return
        if (v == store.notifiedVersion) return
        store.notifiedVersion = v
        Notify.newFirmware(applicationContext, v, running)
    }

    companion object {
        private const val TAG = "SyncWorker"
        private const val PERIODIC = "sync"
        private const val NOW = "sync-now"
        const val PERIOD_MIN = 30L

        fun schedule(context: Context) {
            val req = PeriodicWorkRequestBuilder<SyncWorker>(PERIOD_MIN, TimeUnit.MINUTES).build()
            WorkManager.getInstance(context).enqueueUniquePeriodicWork(PERIODIC, ExistingPeriodicWorkPolicy.UPDATE, req)
        }

        fun runNow(context: Context) {
            WorkManager.getInstance(context)
                .enqueueUniqueWork(NOW, ExistingWorkPolicy.REPLACE, OneTimeWorkRequestBuilder<SyncWorker>().build())
        }

        fun cancel(context: Context) {
            WorkManager.getInstance(context).cancelUniqueWork(PERIODIC)
        }
    }
}

/** The link the open app is using, shared with the worker so there is never a second connection. */
object Live {
    @Volatile var link: ClockBle? = null
}

object Perms {
    fun canConnect(c: Context) = Build.VERSION.SDK_INT < 31 ||
        ContextCompat.checkSelfPermission(c, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
}

object Notify {
    private const val CHANNEL = "updates"

    fun newFirmware(c: Context, version: String, running: String) {
        if (Build.VERSION.SDK_INT >= 33 &&
            ContextCompat.checkSelfPermission(c, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) return
        val nm = c.getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CHANNEL, c.getString(R.string.channel_updates), NotificationManager.IMPORTANCE_DEFAULT))
        val open = PendingIntent.getActivity(c, 0, Intent(c, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val n = NotificationCompat.Builder(c, CHANNEL)
            .setSmallIcon(R.drawable.ic_clock)
            .setContentTitle("Nueva versión del reloj: $version")
            .setContentText("Tu reloj tiene la $running. Abre la app para actualizarlo.")
            .setContentIntent(open)
            .setAutoCancel(true)
            .build()
        nm.notify(1, n)
    }
}
