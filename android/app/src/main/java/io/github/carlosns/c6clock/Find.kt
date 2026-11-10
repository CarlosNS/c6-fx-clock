package io.github.carlosns.c6clock

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.media.AudioAttributes
import android.media.AudioManager
import android.media.MediaPlayer
import android.media.RingtoneManager
import android.os.Handler
import android.os.Looper
import android.os.VibrationEffect
import android.os.Vibrator
import android.util.Log
import androidx.core.app.NotificationCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

/**
 * Finding each other (protocol in src/find.h).
 *
 * Find the watch: "find 1" makes it ring and flash; when the watch is not connected the request waits
 * and goes out as soon as the link is back ([pending]). The page shows how close it is from the
 * link's signal strength while it rings.
 *
 * Find the phone: the watch's quick settings send {"t":"findphone","st":"on"}; the phone plays its
 * alarm sound at full alarm volume (heard even in silent mode), vibrates and shows a notification
 * with "Stop", for at most 5 minutes. Stopped here, it tells the watch ("findphone 0").
 */
object Find {
    private const val TAG = "Find"
    private const val MAX_MS = 5 * 60_000L
    private val main = Handler(Looper.getMainLooper())
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    /** The watch is ringing (as far as the phone knows). */
    @Volatile var watchRinging = false
        private set
    /** Asked while the watch was away: sent on the next connection. */
    @Volatile var pending = false
        private set
    @Volatile var phoneRinging = false
        private set

    /** Starts or stops the watch ringing; true when it went out now (false: waiting for the link). */
    fun watch(on: Boolean): Boolean {
        val l = Live.link?.takeIf { it.ready }
        if (!on) {
            pending = false
            watchRinging = false
            l?.let { scope.launch { runCatching { it.send("find 0") } } }
            return true
        }
        if (l == null) {
            pending = true
            return false
        }
        watchRinging = true
        pending = false
        scope.launch { runCatching { l.send("find 1") }.onFailure { pending = true; watchRinging = false } }
        return true
    }

    /** LinkService: the link is up again. */
    fun connected(l: ClockBle) {
        if (pending) {
            pending = false
            watchRinging = true
            scope.launch { runCatching { l.send("find 1") }.onFailure { pending = true; watchRinging = false } }
        }
    }

    /** Messages from the watch; true when it was one of ours. */
    fun fromClock(c: Context, text: String): Boolean {
        if (!text.startsWith("{\"t\":\"find")) return false
        val j = runCatching { org.json.JSONObject(text) }.getOrNull() ?: return false
        val on = j.optString("st") == "on"
        when (j.optString("t")) {
            "find" -> watchRinging = false  // stopped on the watch (the page hears it too)
            "findphone" -> main.post { if (on) ringPhone(c) else stopPhone(c, tellWatch = false) }
        }
        return false  // still passed on, so the page updates
    }

    // ---- the phone rings

    private var player: MediaPlayer? = null
    private var savedVolume = -1

    private fun ringPhone(c: Context) {
        if (phoneRinging) return
        val ctx = c.applicationContext
        phoneRinging = true
        Log.i(TAG, "the watch is looking for the phone")
        val am = ctx.getSystemService(AudioManager::class.java)
        // The alarm stream sounds in silent mode; at its loudest while it rings, then back as it was
        savedVolume = am.getStreamVolume(AudioManager.STREAM_ALARM)
        runCatching { am.setStreamVolume(AudioManager.STREAM_ALARM, am.getStreamMaxVolume(AudioManager.STREAM_ALARM), 0) }
        val uri = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM) ?: RingtoneManager.getDefaultUri(RingtoneManager.TYPE_RINGTONE)
        player = runCatching {
            MediaPlayer().apply {
                setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_ALARM)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build())
                setDataSource(ctx, uri)
                isLooping = true
                prepare()
                start()
            }
        }.onFailure { Log.w(TAG, "sound: ${it.message}") }.getOrNull()
        runCatching {
            ctx.getSystemService(Vibrator::class.java)?.vibrate(VibrationEffect.createWaveform(longArrayOf(0, 600, 400), 0))
        }
        notify(ctx)
        main.removeCallbacksAndMessages(TOKEN)
        main.postAtTime({ stopPhone(ctx, tellWatch = true) }, TOKEN, android.os.SystemClock.uptimeMillis() + MAX_MS)
    }

    /** Stopped on the phone (notification, app) or by the watch. */
    fun stopPhone(c: Context, tellWatch: Boolean) {
        main.post {
            if (!phoneRinging) return@post
            phoneRinging = false
            val ctx = c.applicationContext
            main.removeCallbacksAndMessages(TOKEN)
            player?.let { runCatching { it.stop(); it.release() } }
            player = null
            runCatching { ctx.getSystemService(Vibrator::class.java)?.cancel() }
            if (savedVolume >= 0) runCatching {
                ctx.getSystemService(AudioManager::class.java).setStreamVolume(AudioManager.STREAM_ALARM, savedVolume, 0)
            }
            savedVolume = -1
            ctx.getSystemService(NotificationManager::class.java).cancel(NOTIFICATION_ID)
            if (tellWatch) Live.link?.let { l -> scope.launch { runCatching { l.send("findphone 0") } } }
            Live.message("{\"t\":\"findphone\",\"st\":\"off\"}")  // the page, if open
        }
    }

    private fun notify(c: Context) {
        val nm = c.getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CHANNEL, c.getString(R.string.channel_find), NotificationManager.IMPORTANCE_HIGH))
        val stop = PendingIntent.getBroadcast(c, 0, Intent(c, FindStop::class.java), PendingIntent.FLAG_IMMUTABLE)
        val open = PendingIntent.getActivity(c, 1, Intent(c, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val n = NotificationCompat.Builder(c, CHANNEL)
            .setSmallIcon(R.drawable.ic_clock)
            .setContentTitle(c.getString(R.string.find_phone_title))
            .setContentText(c.getString(R.string.find_phone_text))
            .setCategory(Notification.CATEGORY_ALARM)
            .setPriority(NotificationCompat.PRIORITY_MAX)
            .setOngoing(true)
            .setContentIntent(open)
            .setDeleteIntent(stop)
            .addAction(0, c.getString(R.string.find_stop), stop)
            .build()
        runCatching { nm.notify(NOTIFICATION_ID, n) }
    }

    private val TOKEN = Any()
    private const val CHANNEL = "find"
    private const val NOTIFICATION_ID = 30
}

/** The notification's "Stop". */
class FindStop : BroadcastReceiver() {
    override fun onReceive(c: Context, i: Intent) = Find.stopPhone(c, tellWatch = true)
}
