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
import androidx.work.OutOfQuotaPolicy
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
    /** When Android last reported the clock out of range (0: present). */
    var goneSinceMs: Long
        get() = p.getLong("goneSince", 0)
        set(v) = p.edit().putLong("goneSince", v).apply()
    /** Last moment the watch was connected ("seen" on the find card). */
    var lastSeenMs: Long
        get() = p.getLong("lastSeen", 0)
        set(v) = p.edit().putLong("lastSeen", v).apply()
    var lastResult: String
        get() = p.getString("lastResult", "") ?: ""
        set(v) = p.edit().putString("lastResult", v).apply()
    var clockVersion: String
        get() = p.getString("clockVersion", "") ?: ""
        set(v) = p.edit().putString("clockVersion", v).apply()
    /** Board of the paired clock, which picks its image in the update manifest. */
    var clockBoard: String
        get() = p.getString("clockBoard", "") ?: ""
        set(v) = p.edit().putString("clockBoard", v).apply()
    /** Newest firmware already announced, so each version is notified once. */
    var notifiedVersion: String
        get() = p.getString("notified", "") ?: ""
        set(v) = p.edit().putString("notified", v).apply()
    /** Same for the app itself (versionCode). */
    var notifiedApp: Long
        get() = p.getLong("notifiedApp", 0)
        set(v) = p.edit().putLong("notifiedApp", v).apply()
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

/** What is published on GitHub Pages: the firmware and, in the same manifest, the app itself. */
object Firmware {
    const val MANIFEST = "https://carlosns.github.io/c6-fx-clock/manifest.json"
    /** Where updates are read from; the phone tests point it at the PC (MainActivity, extra "manifest"). */
    @Volatile var manifestUrl = MANIFEST

    /** -1, 0 or 1, comparing "major.minor.patch" numerically ("2.10.0" is newer than "2.9.1"). */
    /**
     * Semantic versions: "3.0.0-beta.6" < "3.0.0-beta.10" < "3.0.0" < "3.0.1". A pre-release comes
     * before its release, its parts compare as numbers when they are numbers, and "+build" is ignored.
     * The same rules live in src/index.html (verCmp).
     */
    fun compare(a: String, b: String): Int {
        fun split(v: String): Pair<List<Int>, List<String>?> {
            val core = v.substringBefore('+')
            val main = core.substringBefore('-').split('.').map { it.toIntOrNull() ?: 0 }
            val pre = if ('-' in core) core.substringAfter('-').split('.') else null
            return main to pre
        }
        val (am, ap) = split(a)
        val (bm, bp) = split(b)
        for (i in 0 until maxOf(am.size, bm.size)) {
            val d = am.getOrElse(i) { 0 }.compareTo(bm.getOrElse(i) { 0 })
            if (d != 0) return d
        }
        if (ap == null || bp == null) return (if (ap == null) 1 else 0) - (if (bp == null) 1 else 0)
        for (i in 0 until maxOf(ap.size, bp.size)) {
            val x = ap.getOrNull(i) ?: return -1
            val y = bp.getOrNull(i) ?: return 1
            val xi = x.toIntOrNull()
            val yi = y.toIntOrNull()
            val d = when {
                xi != null && yi != null -> xi.compareTo(yi)
                xi != null -> -1  // numbers sort before words
                yi != null -> 1
                else -> x.compareTo(y)
            }
            if (d != 0) return d
        }
        return 0
    }

    /** This board's image in the manifest: the top level for the C6 (as always, also for an empty board
     *  id from older firmware), boards.<id> for the others; null if there is none for it. */
    fun entry(manifest: JSONObject, board: String): JSONObject? =
        if (board.isEmpty() || board == "c6lcd") manifest else manifest.optJSONObject("boards")?.optJSONObject(board)

    /** The newer version on offer for a [board] clock running [running], or null. */
    fun newer(manifest: JSONObject, running: String, board: String = ""): String? =
        entry(manifest, board)?.optString("version")?.takeIf { it.isNotEmpty() && running.isNotEmpty() && compare(it, running) > 0 }

    /** The manifest's "app" block when it is newer than the installed [versionCode] and complete, or null. */
    fun newerApp(manifest: JSONObject, versionCode: Long): JSONObject? = manifest.optJSONObject("app")?.takeIf {
        it.optLong("code") > versionCode && it.optString("url").isNotEmpty() && it.optString("sha256").length == 64 &&
            it.optLong("size") > 0
    }
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
        // The permanent link syncs on every connection and every 30 min: only look for updates
        if (LinkService.running) {
            checkUpdates(store)
            return Result.success()
        }
        val shared = Live.link?.takeIf { it.ready && it.address == address }
        val link = shared ?: ClockBle(applicationContext, address)
        try {
            if (shared == null) link.connect()
            record(store, SyncSession(link, Net::weatherCommand).run())
        } catch (e: Exception) {
            Log.w(TAG, "sync failed: ${e.message}")
            store.lastResult = "falló: ${e.message}"
        } finally {
            if (shared == null) link.close()
        }
        checkUpdates(store)  // even when the clock is out of reach: the app may have one of its own
        return Result.success()  // the next period tries again; a retry storm would only drain the battery
    }

    /** New firmware (for the clock version last seen) or a new app: one notification per version. */
    private suspend fun checkUpdates(store: Store) {
        val manifest = runCatching { withContext(Dispatchers.IO) { Net.getJson(Firmware.manifestUrl) } }.getOrNull() ?: return
        val running = store.clockVersion
        Firmware.newer(manifest, running, store.clockBoard)?.takeIf { it != store.notifiedVersion }?.let {
            store.notifiedVersion = it
            Notify.newFirmware(applicationContext, it, running)
        }
        Firmware.newerApp(manifest, AppUpdate.installedCode(applicationContext))?.takeIf { it.optLong("code") != store.notifiedApp }?.let {
            store.notifiedApp = it.optLong("code")
            Notify.newApp(applicationContext, it.optString("version"))
        }
    }

    companion object {
        private const val TAG = "SyncWorker"
        private const val PERIODIC = "sync"
        private const val NOW = "sync-now"
        const val PERIOD_MIN = 30L

        /** Remembers the outcome of a sync, for the page and the update check. */
        fun record(store: Store, r: SyncResult) {
            store.lastSyncMs = System.currentTimeMillis()
            store.clockVersion = r.version
            store.clockBoard = r.board
            store.lastResult = "ok" + if (r.weatherSent) "" else " (sin tiempo: no hay internet)"
            Log.i(TAG, "sync ok: ${r.version}, ${r.place}, #${r.syncCount}")
        }

        fun schedule(context: Context) {
            val req = PeriodicWorkRequestBuilder<SyncWorker>(PERIOD_MIN, TimeUnit.MINUTES).build()
            WorkManager.getInstance(context).enqueueUniquePeriodicWork(PERIODIC, ExistingPeriodicWorkPolicy.UPDATE, req)
        }

        fun runNow(context: Context) {
            WorkManager.getInstance(context)
                .enqueueUniqueWork(NOW, ExistingWorkPolicy.REPLACE, OneTimeWorkRequestBuilder<SyncWorker>().build())
        }

        /** From the background (the clock just appeared): expedited, so the system runs it at once.
         *  Only used on Android 12+, where expedited work needs no foreground notification. */
        fun runSoon(context: Context) {
            val req = OneTimeWorkRequestBuilder<SyncWorker>()
                .setExpedited(OutOfQuotaPolicy.RUN_AS_NON_EXPEDITED_WORK_REQUEST).build()
            WorkManager.getInstance(context).enqueueUniqueWork(NOW, ExistingWorkPolicy.KEEP, req)
        }

        fun cancel(context: Context) {
            WorkManager.getInstance(context).cancelUniqueWork(PERIODIC)
        }
    }
}

object Perms {
    fun canConnect(c: Context) = Build.VERSION.SDK_INT < 31 ||
        ContextCompat.checkSelfPermission(c, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
}

object Notify {
    private const val CHANNEL = "updates"

    fun newFirmware(c: Context, version: String, running: String) =
        show(c, 1, "Nueva versión del reloj: $version", "Tu reloj tiene la $running. Abre la app para actualizarlo.")

    fun newApp(c: Context, version: String) =
        show(c, 2, "Nueva versión de la app: $version", "Abre la app y pulsa Actualizar la app.")

    private fun show(c: Context, id: Int, title: String, text: String) {
        if (Build.VERSION.SDK_INT >= 33 &&
            ContextCompat.checkSelfPermission(c, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) return
        val nm = c.getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CHANNEL, c.getString(R.string.channel_updates), NotificationManager.IMPORTANCE_DEFAULT))
        val open = PendingIntent.getActivity(c, 0, Intent(c, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val n = NotificationCompat.Builder(c, CHANNEL)
            .setSmallIcon(R.drawable.ic_clock)
            .setContentTitle(title)
            .setContentText(text)
            .setContentIntent(open)
            .setAutoCancel(true)
            .build()
        nm.notify(id, n)
    }
}
