package io.github.carlosns.c6clock

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.util.concurrent.CopyOnWriteArrayList

/**
 * The one link to the clock, shared by everything in the app: the permanent link service keeps it
 * open, the page and the periodic sync reuse it, and anyone can listen to what the clock says.
 */
object Live {
    @Volatile var link: ClockBle? = null
    private val messageListeners = CopyOnWriteArrayList<(String) -> Unit>()
    private val stateListeners = CopyOnWriteArrayList<(Boolean) -> Unit>()

    val ready get() = link?.ready == true
    /** The app is on screen: the link runs at a quick interval for the page, otherwise at low power. */
    @Volatile var visible = false

    fun listen(onMessage: (String) -> Unit, onState: (Boolean) -> Unit) {
        messageListeners += onMessage
        stateListeners += onState
    }

    fun unlisten(onMessage: (String) -> Unit, onState: (Boolean) -> Unit) {
        messageListeners -= onMessage
        stateListeners -= onState
    }

    internal fun message(text: String) = messageListeners.forEach { runCatching { it(text) } }
    internal fun state(up: Boolean) = stateListeners.forEach { runCatching { it(up) } }
}

/**
 * Keeps the Bluetooth link to the clock open while it is near (docs/PLAN-marea.md, phase 7), as a
 * foreground service of type "connected device": Android starts it when the clock appears
 * (ClockPresence) or the app opens, and it reconnects by itself after a drop. On every connection it
 * syncs the time and the weather, then again every 30 min, and it carries the music and call
 * messages both ways (Phone). Connection interval: low power, except while the app is on screen.
 */
class LinkService : Service() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private var loop: Job? = null
    @Volatile private var away = false
    private lateinit var store: Store

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        running = true
        store = Store(this)
        if (!foreground(true)) stopSelf()
    }

    /**
     * Foreground as a connected device, and also as a microphone user when voice commands are allowed:
     * Android only lets the recognizer take the watch's audio while the app counts as in use. The
     * microphone type may be refused when started from the background; then it goes without it, and
     * the next time the app is opened (ACTION_MIC) adds it.
     */
    private fun foreground(withMic: Boolean): Boolean {
        val link = if (Build.VERSION.SDK_INT >= 29) ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE else 0
        val mic = if (withMic && Build.VERSION.SDK_INT >= 30 && Voice.allowed(this)) ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE else 0
        if (mic != 0) {
            val r = runCatching { ServiceCompat.startForeground(this, NOTIFICATION_ID, notification(Live.ready), link or mic) }
            if (r.isSuccess) { Log.i(TAG, "foreground with the microphone"); return true }
            Log.i(TAG, "microphone refused: ${r.exceptionOrNull()?.message}")
        }
        return runCatching { ServiceCompat.startForeground(this, NOTIFICATION_ID, notification(Live.ready), link) }
            .onFailure { Log.w(TAG, "foreground refused: ${it.message}") }.isSuccess
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> { stopSelf(); return START_NOT_STICKY }
            ACTION_MIC -> { foreground(true); if (loop?.isActive == true) return START_STICKY }
            ACTION_AWAY -> { away = true; if (!Live.ready) stopSelf(); return START_NOT_STICKY }
            else -> away = false
        }
        if (loop?.isActive != true) loop = scope.launch { keep() }
        return START_STICKY
    }

    override fun onDestroy() {
        running = false
        scope.cancel()
        Live.link?.takeIf { it === mine }?.let { it.close(); Live.link = null; Live.state(false) }
        super.onDestroy()
    }

    private var mine: ClockBle? = null

    private suspend fun keep() {
        var wait = 2_000L
        while (scope.isActive) {
            val address = store.address ?: break
            if (!Perms.canConnect(this)) break
            val l = Live.link?.takeIf { it.ready && it.address == address } ?: ClockBle(this, address)
            l.onMessage = { text ->
                if (!VoiceLink.fromClock(this, text)) {
                    Find.fromClock(this, text)
                    Live.message(text)
                    Phone.fromClock(text)
                }
            }
            l.onState = { up -> Live.state(up); show(up) }
            try {
                l.connect()
                mine = l
                Live.link = l  // connect() has told the listeners already
                wait = 2_000L
                Find.connected(l)  // asked to ring while it was away
                var last = 0L
                while (l.ready) {
                    if (!l.alive()) {  // Android dropped it without a callback: start again
                        l.drop("no longer connected")
                        break
                    }
                    store.lastSeenMs = System.currentTimeMillis()
                    if (System.currentTimeMillis() - last > RESYNC_MS) {
                        last = System.currentTimeMillis()
                        runCatching { SyncWorker.record(store, SyncSession(l, Net::weatherCommand).run()) }
                            .onFailure { Log.w(TAG, "sync: ${it.message}"); store.lastResult = "falló: ${it.message}" }
                        Phone.toClock()  // what is playing and any call ringing right now
                        l.priority(Live.visible)
                    }
                    delay(2_000)
                }
            } catch (e: Exception) {
                Log.i(TAG, "connect: ${e.message}")
            } finally {
                l.close()
                if (Live.link === l) Live.link = null
                mine = null
                Live.state(false)
                show(false)
            }
            if (away) break
            delay(wait)
            wait = minOf(wait * 2, 60_000L)
        }
        stopSelf()
    }

    private fun show(up: Boolean) {
        val nm = getSystemService(NotificationManager::class.java)
        runCatching { nm.notify(NOTIFICATION_ID, notification(up)) }
    }

    private fun notification(up: Boolean): Notification {
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CHANNEL, getString(R.string.channel_link), NotificationManager.IMPORTANCE_MIN))
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        return NotificationCompat.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_clock)
            .setContentTitle(if (up) getString(R.string.link_up) else getString(R.string.link_searching))
            .setContentText(getString(R.string.link_text))
            .setContentIntent(open)
            .setOngoing(true)
            .setSilent(true)
            .setPriority(NotificationCompat.PRIORITY_MIN)
            .build()
    }

    companion object {
        private const val TAG = "LinkService"
        private const val CHANNEL = "link"
        private const val NOTIFICATION_ID = 10
        private const val ACTION_STOP = "stop"
        private const val ACTION_AWAY = "away"
        private const val ACTION_MIC = "mic"
        private const val RESYNC_MS = SyncWorker.PERIOD_MIN * 60_000L
        /** The service is alive (connected or looking for the clock): the periodic sync leaves it the link. */
        @Volatile var running = false
            private set

        /** Starts (or nudges) the link; from the background only Android 12+ companion apps may. */
        fun start(c: Context): Boolean {
            if (Store(c).address == null || !Perms.canConnect(c)) return false
            return runCatching { ContextCompat.startForegroundService(c, Intent(c, LinkService::class.java)) }
                .onFailure { Log.w(TAG, "start: ${it.message}") }.isSuccess
        }

        /** The clock went out of range: stop once the link is gone, instead of retrying for nothing. */
        fun away(c: Context) {
            runCatching { c.startService(Intent(c, LinkService::class.java).setAction(ACTION_AWAY)) }
        }

        /** The app is on screen (or the microphone was just allowed): let the service use the microphone. */
        fun mic(c: Context) {
            if (!running || !Voice.allowed(c)) return
            runCatching { c.startService(Intent(c, LinkService::class.java).setAction(ACTION_MIC)) }
        }

        fun stop(c: Context) {
            runCatching { c.startService(Intent(c, LinkService::class.java).setAction(ACTION_STOP)) }
        }
    }
}
