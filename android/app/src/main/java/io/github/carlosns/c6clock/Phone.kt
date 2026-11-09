package io.github.carlosns.c6clock

import android.app.Notification
import android.app.PendingIntent
import android.content.ComponentName
import android.content.Context
import android.media.AudioManager
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import org.json.JSONObject
import java.text.Normalizer

/**
 * What the phone tells the clock and what the clock asks back (src/phone.h):
 *   to the clock:   "music <playing 0|1> <volume 0..100>|<title>|<artist>", "music off",
 *                   "call ring|<who>", "call end"
 *   from the clock: {"t":"media","cmd":"play|pause|next|prev|vol+|vol-"}, {"t":"call","cmd":"reject|silence"}
 * Music comes from the media sessions and calls from the dialer's notification, both through
 * PhoneWatch (notification access), so no phone or contacts permission is needed.
 */
object Phone {
    @Volatile var watch: PhoneWatch? = null
    @Volatile private var music = "music off"
    @Volatile private var call = "call end"
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    /** Characters the clock's fonts have (tools/make_fonts.py); others lose their accents or become "?". */
    private const val CLOCK_CHARS = "¡¿ÁÉÍÓÚÜÑáéíóúüñ°·ºª«»€…–—×"

    fun hasAccess(c: Context): Boolean {
        val flat = Settings.Secure.getString(c.contentResolver, "enabled_notification_listeners") ?: return false
        return flat.split(':').any { ComponentName.unflattenFromString(it)?.packageName == c.packageName }
    }

    /** Text for one field of a clock command: no separators, only the clock's characters, at most [bytes] in UTF-8. */
    fun clean(s: String, bytes: Int): String {
        val out = StringBuilder()
        var used = 0
        for (ch in s.replace('|', '/').replace('\n', ' ').replace('\r', ' ')) {
            val t = when {
                ch.code in 32..126 || ch in CLOCK_CHARS -> ch.toString()
                else -> Normalizer.normalize(ch.toString(), Normalizer.Form.NFD).filter { it.code in 32..126 }.ifEmpty { "?" }
            }
            val n = t.toByteArray(Charsets.UTF_8).size
            if (used + n > bytes) break
            out.append(t)
            used += n
        }
        return out.toString().trim()
    }

    fun musicLine(c: Context, mc: MediaController?): String {
        if (mc == null) return "music off"
        val md = mc.metadata
        val title = md?.getString(MediaMetadata.METADATA_KEY_TITLE) ?: md?.getString(MediaMetadata.METADATA_KEY_DISPLAY_TITLE) ?: ""
        val artist = md?.getString(MediaMetadata.METADATA_KEY_ARTIST) ?: md?.getString(MediaMetadata.METADATA_KEY_ALBUM_ARTIST) ?: ""
        val playing = mc.playbackState?.state == PlaybackState.STATE_PLAYING
        if (title.isEmpty() && !playing) return "music off"
        val am = c.getSystemService(AudioManager::class.java)
        val max = am.getStreamMaxVolume(AudioManager.STREAM_MUSIC).coerceAtLeast(1)
        val vol = am.getStreamVolume(AudioManager.STREAM_MUSIC) * 100 / max
        return "music ${if (playing) 1 else 0} $vol|${clean(title, 60)}|${clean(artist, 44)}"
    }

    fun music(c: Context, mc: MediaController?) {
        val line = musicLine(c, mc)
        if (line == music) return
        music = line
        push(line)
    }

    fun ring(who: String) {
        val line = "call ring|${clean(who.ifEmpty { "Llamada" }, 44)}"
        if (line == call) return
        call = line
        push(line)
    }

    fun callEnded() {
        if (call == "call end") return
        call = "call end"
        push(call)
    }

    /** On every (re)connection: the clock may have missed changes while the link was down. */
    fun toClock() {
        push(music)
        if (call != "call end") push(call)
    }

    private fun push(line: String) {
        val l = Live.link?.takeIf { it.ready } ?: return
        scope.launch { runCatching { l.send(line) }.onFailure { Log.w("Phone", "send: ${it.message}") } }
    }

    /** A message from the clock: media keys and call buttons. */
    fun fromClock(text: String) {
        val j = runCatching { JSONObject(text) }.getOrNull() ?: return
        val cmd = j.optString("cmd")
        when (j.optString("t")) {
            "media" -> Handler(Looper.getMainLooper()).post { watch?.media(cmd) }
            "call" -> Handler(Looper.getMainLooper()).post { watch?.call(cmd) }
        }
    }
}

/** Notification access: follows the media sessions and the incoming call notification. */
class PhoneWatch : NotificationListenerService() {
    private var msm: MediaSessionManager? = null
    private val controllers = mutableListOf<MediaController>()
    private var current: MediaController? = null
    private var callKey: String? = null
    private var callNotification: Notification? = null
    private val main = Handler(Looper.getMainLooper())

    private val callback = object : MediaController.Callback() {
        override fun onPlaybackStateChanged(state: PlaybackState?) = choose()
        override fun onMetadataChanged(metadata: MediaMetadata?) = choose()
        override fun onSessionDestroyed() {
            main.post { refresh() }
        }
    }
    private val sessions = MediaSessionManager.OnActiveSessionsChangedListener { list -> track(list) }

    override fun onListenerConnected() {
        Phone.watch = this
        msm = getSystemService(MediaSessionManager::class.java)
        runCatching { msm?.addOnActiveSessionsChangedListener(sessions, ComponentName(this, PhoneWatch::class.java), main) }
        refresh()
        runCatching { activeNotifications?.forEach { onNotificationPosted(it) } }
    }

    override fun onListenerDisconnected() {
        if (Phone.watch === this) Phone.watch = null
        runCatching { msm?.removeOnActiveSessionsChangedListener(sessions) }
        controllers.forEach { it.unregisterCallback(callback) }
        controllers.clear()
    }

    private fun refresh() =
        track(runCatching { msm?.getActiveSessions(ComponentName(this, PhoneWatch::class.java)) }.getOrNull())

    /** Watches every active session, so the one that starts playing wins. */
    private fun track(list: List<MediaController>?) {
        controllers.forEach { it.unregisterCallback(callback) }
        controllers.clear()
        list?.forEach { it.registerCallback(callback, main); controllers += it }
        choose()
    }

    private fun choose() {
        current = controllers.firstOrNull { it.playbackState?.state == PlaybackState.STATE_PLAYING }
            ?: current?.takeIf { c -> controllers.any { it.sessionToken == c.sessionToken } }
            ?: controllers.firstOrNull()
        Phone.music(this, current)
    }

    fun media(cmd: String) {
        val am = getSystemService(AudioManager::class.java)
        val tc = current?.transportControls
        when (cmd) {
            "play" -> tc?.play()
            "pause" -> tc?.pause()
            "next" -> tc?.skipToNext()
            "prev" -> tc?.skipToPrevious()
            "vol+" -> am.adjustStreamVolume(AudioManager.STREAM_MUSIC, AudioManager.ADJUST_RAISE, 0)
            "vol-" -> am.adjustStreamVolume(AudioManager.STREAM_MUSIC, AudioManager.ADJUST_LOWER, 0)
        }
        // The volume has no callback of its own: tell the clock after the change
        main.postDelayed({ Phone.music(this, current) }, 300)
    }

    fun call(cmd: String) {
        when (cmd) {
            "reject" -> {
                // The dialer's own "decline" button (the first action of an incoming call)
                val actions = callNotification?.actions ?: return
                val decline = actions.firstOrNull { a ->
                    val t = a.title?.toString()?.lowercase() ?: ""
                    listOf("rechaz", "declin", "colgar", "hang", "reject", "ignor").any { it in t }
                } ?: actions.firstOrNull()
                runCatching { decline?.actionIntent?.send() }.onFailure { if (it !is PendingIntent.CanceledException) Log.w(TAG, "reject: $it") }
            }
            "silence" -> runCatching {
                getSystemService(AudioManager::class.java).adjustStreamVolume(AudioManager.STREAM_RING, AudioManager.ADJUST_MUTE, 0)
                main.postDelayed({
                    runCatching { getSystemService(AudioManager::class.java).adjustStreamVolume(AudioManager.STREAM_RING, AudioManager.ADJUST_UNMUTE, 0) }
                }, 60_000)
            }.onFailure { Log.w(TAG, "silence: ${it.message}") }
        }
    }

    /** The ring ends on the clock: answered, rejected or the caller gave up. */
    private fun endCall(why: String) {
        Log.i(TAG, "call end: $why")
        main.removeCallbacks(endLater)
        callKey = null
        callNotification = null
        Phone.callEnded()
    }

    // The dialer often replaces its notification while ringing (HyperOS removes and reposts it when the
    // full-screen call UI comes up), so a removal only ends the call if nothing takes its place
    private val endLater = Runnable { endCall("notification removed") }

    // Answering switches the phone's audio to call mode: the surest sign, with no phone permission
    private var modeListener: AudioManager.OnModeChangedListener? = null

    override fun onCreate() {
        super.onCreate()
        if (Build.VERSION.SDK_INT >= 31) {
            val l = AudioManager.OnModeChangedListener { mode ->
                Log.i(TAG, "audio mode $mode (ringing: ${callKey != null})")
                if (callKey != null && (mode == AudioManager.MODE_IN_CALL || mode == AudioManager.MODE_IN_COMMUNICATION)) endCall("audio mode $mode")
            }
            runCatching { getSystemService(AudioManager::class.java).addOnModeChangedListener(mainExecutor, l) }
                .onSuccess { modeListener = l }
        }
    }

    override fun onDestroy() {
        modeListener?.let { l -> runCatching { getSystemService(AudioManager::class.java).removeOnModeChangedListener(l) } }
        super.onDestroy()
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        val n = sbn.notification ?: return
        if (n.category != Notification.CATEGORY_CALL) return
        // CallStyle says what kind of call it is (Android 12+). Dialers without it: a running call
        // shows a chronometer; a ringing one can take the whole screen. Anything else (an update of
        // the ringing notification without those marks) changes nothing.
        val type = n.extras.getInt(EXTRA_CALL_TYPE, -1)
        val answered = type == CALL_TYPE_ONGOING || (type < 0 && n.extras.getBoolean(Notification.EXTRA_SHOW_CHRONOMETER))
        val incoming = type == CALL_TYPE_INCOMING || (type < 0 && !answered && n.fullScreenIntent != null)
        val ours = sbn.key == callKey
        Log.i(TAG, "call notification ${sbn.packageName} key=${sbn.key.takeLast(24)} type=$type chrono=${n.extras.getBoolean(Notification.EXTRA_SHOW_CHRONOMETER)} " +
            "fullscreen=${n.fullScreenIntent != null} ongoing=${n.flags and Notification.FLAG_ONGOING_EVENT != 0} -> answered=$answered incoming=$incoming ours=$ours")
        when {
            answered -> if (ours || callKey != null) endCall("notification says answered")
            incoming || (callKey != null && type < 0 && !answered) -> {
                main.removeCallbacks(endLater)
                callKey = sbn.key
                callNotification = n
                val person = if (Build.VERSION.SDK_INT >= 33) n.extras.getParcelable(EXTRA_CALL_PERSON, android.app.Person::class.java)
                else @Suppress("DEPRECATION") n.extras.getParcelable<android.app.Person>(EXTRA_CALL_PERSON)
                val who = person?.name?.toString() ?: n.extras.getCharSequence(Notification.EXTRA_TITLE)?.toString() ?: ""
                Phone.ring(who)
            }
        }
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        if (sbn.notification?.category == Notification.CATEGORY_CALL) Log.i(TAG, "call notification removed key=${sbn.key.takeLast(24)} ours=${sbn.key == callKey}")
        if (sbn.key != callKey) return
        main.removeCallbacks(endLater)
        main.postDelayed(endLater, 1500)
    }

    private companion object {
        const val TAG = "PhoneWatch"
        // Notification.EXTRA_CALL_TYPE / CALL_TYPE_INCOMING / EXTRA_CALL_PERSON (API 31), by value for older SDKs
        const val EXTRA_CALL_TYPE = "android.callType"
        const val CALL_TYPE_INCOMING = 1
        const val CALL_TYPE_ONGOING = 2
        const val EXTRA_CALL_PERSON = "android.callPerson"
    }
}
