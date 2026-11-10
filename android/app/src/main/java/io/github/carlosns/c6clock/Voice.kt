package io.github.carlosns.c6clock

import android.content.Context
import android.content.Intent
import android.media.AudioFormat
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.ParcelFileDescriptor
import android.speech.RecognitionListener
import android.speech.RecognizerIntent
import android.speech.SpeechRecognizer
import android.util.Log
import java.io.FileOutputStream
import java.util.concurrent.LinkedBlockingQueue
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

/**
 * Speech recognition of audio that does not come from the phone's microphone: the watch's, sent over
 * Bluetooth. Android 13+ lets a recognizer read 16 kHz 16-bit mono PCM from a pipe (EXTRA_AUDIO_SOURCE);
 * the watch's audio is written into it as it arrives, and closing it ends the utterance.
 * One session at a time; everything runs on the main thread, as SpeechRecognizer requires.
 */
object Voice {
    fun allowed(c: Context) = VoiceSession.allowed(c)

    /** The recognizer lacks this language: ask Android to download it (it does so in the background). */
    fun download(c: Context, lang: String) {
        if (Build.VERSION.SDK_INT < 33) return
        Handler(Looper.getMainLooper()).post {
            runCatching {
                val r = SpeechRecognizer.createSpeechRecognizer(c.applicationContext)
                r.triggerModelDownload(Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).putExtra(RecognizerIntent.EXTRA_LANGUAGE, lang))
                Log.i("Voice", "download $lang asked")
            }.onFailure { Log.w("Voice", "download: ${it.message}") }
        }
    }
}

class VoiceSession(
    context: Context,
    private val language: String,  // "es-ES" / "en-US"
    private val done: (texts: List<String>, error: String?) -> Unit,
) {
    private val context = context.applicationContext
    private val main = Handler(Looper.getMainLooper())
    private var recognizer: SpeechRecognizer? = null
    private var write: ParcelFileDescriptor? = null
    private var read: ParcelFileDescriptor? = null
    private val queue = LinkedBlockingQueue<ByteArray>()
    private var writer: Thread? = null
    @Volatile private var finished = false
    private var partial: List<String> = emptyList()

    private fun texts(b: Bundle) = b.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION).orEmpty().filter { it.isNotBlank() }

    /** Starts listening; false when this phone cannot recognize audio from the watch. */
    fun start(): Boolean {
        if (Build.VERSION.SDK_INT < 33 || !SpeechRecognizer.isRecognitionAvailable(context)) return false
        val (read, w) = ParcelFileDescriptor.createPipe()
        write = w
        val r = SpeechRecognizer.createSpeechRecognizer(context)
        recognizer = r
        r.setRecognitionListener(object : RecognitionListener {
            override fun onResults(results: Bundle) {
                // Some recognizers (Google's offline one, fed from a pipe) hand an empty final bundle
                // and only said it in the partial results: fall back to the last of those
                val list = texts(results).ifEmpty { partial }
                Log.i(TAG, "onResults ${results.keySet().joinToString { k -> "$k=${results.get(k)}" }}")
                finish(list, if (list.isEmpty()) "nada" else null)
            }
            override fun onError(error: Int) { Log.i(TAG, "onError $error"); finish(emptyList(), errorName(error)) }
            override fun onReadyForSpeech(params: Bundle?) {}
            override fun onBeginningOfSpeech() {}
            override fun onRmsChanged(rmsdB: Float) {}
            override fun onBufferReceived(buffer: ByteArray?) {}
            override fun onEndOfSpeech() {}
            override fun onPartialResults(partialResults: Bundle?) {
                partialResults?.let { b -> texts(b).takeIf { it.isNotEmpty() }?.let { partial = it } }
            }
            override fun onSegmentResults(segmentResults: Bundle) {
                Log.i(TAG, "segment ${texts(segmentResults)}")
                texts(segmentResults).takeIf { it.isNotEmpty() }?.let { partial = it }
            }
            override fun onEvent(eventType: Int, params: Bundle?) {}
        })
        val intent = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH)
            .putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
            .putExtra(RecognizerIntent.EXTRA_LANGUAGE, language)
            .putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 5)
            .putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true)
            .putExtra(RecognizerIntent.EXTRA_AUDIO_SOURCE, read)
            .putExtra(RecognizerIntent.EXTRA_AUDIO_SOURCE_CHANNEL_COUNT, 1)
            .putExtra(RecognizerIntent.EXTRA_AUDIO_SOURCE_ENCODING, AudioFormat.ENCODING_PCM_16BIT)
            .putExtra(RecognizerIntent.EXTRA_AUDIO_SOURCE_SAMPLING_RATE, RATE)
        r.startListening(intent)  // binds the service first: the pipe's read end must stay open until the end
        this.read = read
        // A blocking pipe: a thread writes what arrives, so the Bluetooth callbacks never wait on it
        writer = Thread {
            runCatching {
                FileOutputStream(w.fileDescriptor).use { out ->
                    while (true) {
                        val b = queue.take()
                        if (b.isEmpty()) break
                        out.write(b)
                    }
                }
            }.onFailure { Log.w(TAG, "pipe: ${it.message}") }
            runCatching { w.close() }
        }.apply { start() }
        return true
    }

    /** Little-endian 16-bit samples from the watch. */
    fun feed(pcm: ByteArray) {
        if (!finished) queue.put(pcm)
    }

    /** The user let go: no more audio, the recognizer gives its answer. */
    fun end() = queue.put(ByteArray(0))

    fun cancel() {
        queue.put(ByteArray(0))
        main.post { finish(emptyList(), "cancelado") }
    }

    private fun finish(texts: List<String>, error: String?) {
        if (finished) return
        finished = true
        queue.put(ByteArray(0))
        recognizer?.destroy()
        recognizer = null
        runCatching { read?.close() }
        read = null
        Log.i(TAG, "result ${texts.joinToString(" | ")} ${error ?: ""}")
        done(texts, error)
    }

    private fun errorName(e: Int) = when (e) {
        SpeechRecognizer.ERROR_NO_MATCH, SpeechRecognizer.ERROR_SPEECH_TIMEOUT -> "nada"
        SpeechRecognizer.ERROR_NETWORK, SpeechRecognizer.ERROR_NETWORK_TIMEOUT, SpeechRecognizer.ERROR_SERVER -> "red"
        SpeechRecognizer.ERROR_LANGUAGE_NOT_SUPPORTED, SpeechRecognizer.ERROR_LANGUAGE_UNAVAILABLE -> "idioma"
        SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS -> "permiso"
        SpeechRecognizer.ERROR_RECOGNIZER_BUSY -> "ocupado"
        else -> "error $e"
    }

    companion object {
        /** Android's recognizer needs the microphone permission, even with the audio from the watch. */
        fun allowed(c: Context) = c.checkSelfPermission(android.Manifest.permission.RECORD_AUDIO) ==
            android.content.pm.PackageManager.PERMISSION_GRANTED
        private const val TAG = "Voice"

        /** For the tests: what each recognizer says about Spanish and English (and download them). */
        fun logSupport(c: Context, download: Boolean) {
            if (Build.VERSION.SDK_INT < 33) return
            val kinds = buildList {
                add("default" to { SpeechRecognizer.createSpeechRecognizer(c) })
                if (SpeechRecognizer.isOnDeviceRecognitionAvailable(c)) add("device" to { SpeechRecognizer.createOnDeviceSpeechRecognizer(c) })
            }
            for ((name, make) in kinds) for (lang in listOf("es-ES", "en-US")) {
                val r = make()
                val intent = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).putExtra(RecognizerIntent.EXTRA_LANGUAGE, lang)
                r.checkRecognitionSupport(intent, c.mainExecutor, object : android.speech.RecognitionSupportCallback {
                    override fun onSupportResult(s: android.speech.RecognitionSupport) {
                        Log.i(TAG, "$name $lang installed=${s.installedOnDeviceLanguages} pending=${s.pendingOnDeviceLanguages} " +
                            "supported=${s.supportedOnDeviceLanguages.take(12)} online=${s.onlineLanguages.take(12)}")
                        if (download && lang !in s.installedOnDeviceLanguages) r.triggerModelDownload(intent)
                    }
                    override fun onError(error: Int) { Log.i(TAG, "$name $lang support error $error") }
                })
            }
        }
        const val RATE = 16000
    }
}

/**
 * The watch's side of a voice command (protocol in src/voice.h): its audio arrives over the permanent
 * link while BOOT is held, goes to a [VoiceSession], and what was understood goes back as
 * "voice ask <command>|<heard>|<what it will do>" for the watch to confirm (or "voice fail" / "voice err").
 */
object VoiceLink {
    private const val TAG = "VoiceLink"
    private val main = Handler(Looper.getMainLooper())
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private var session: VoiceSession? = null
    private var english = false
    private var faces: List<VoiceCommands.Face> = emptyList()
    @Volatile private var peak = 2000

    /** Every message from the watch; returns true when it was voice audio (not worth passing on). */
    fun fromClock(c: Context, text: String): Boolean {
        if (!text.startsWith("{\"t\":\"v")) return false
        val j = runCatching { org.json.JSONObject(text) }.getOrNull() ?: return false
        when (j.optString("t")) {
            "va" -> {
                val data = runCatching { android.util.Base64.decode(j.optString("d"), android.util.Base64.DEFAULT) }.getOrNull() ?: return true
                val pcm = Adpcm.decode(data, j.optInt("p"), j.optInt("i"))
                session?.feed(louder(pcm))
                return true
            }
            "voice" -> main.post {
                when (j.optString("st")) {
                    "start" -> start(c, j.optString("lang", "es-ES"))
                    "end" -> session?.end()
                    "cancel" -> { session?.cancel(); session = null; Live.link?.priority(Live.visible) }
                }
            }
        }
        return false
    }

    private fun start(c: Context, lang: String) {
        session?.cancel()
        session = null
        english = lang.startsWith("en")
        peak = 2000
        if (Build.VERSION.SDK_INT < 33) return reply("voice err nomovil")
        if (!Voice.allowed(c)) return reply("voice err permiso")
        Live.link?.fast()  // 11 KB/s of audio: the quickest connection interval while it lasts
        val s = VoiceSession(c, lang) { texts, err -> answer(c, lang, texts, err) }
        if (!s.start()) return reply("voice err nomovil")
        session = s
        // The face names, to recognize "pon la esfera Ónix" (fetched while the user speaks)
        scope.launch {
            val l = Live.link ?: return@launch
            runCatching {
                val r = l.exchange("get faces", "faces", 5_000) { true }
                val list = r.getJSONArray("faces")
                faces = (0 until list.length()).map { list.getJSONObject(it) }.mapIndexedNotNull { i, f ->
                    if (f.optBoolean("x") || f.optInt("x") == 1) null else VoiceCommands.Face(i, f.optString("label"))
                }
            }.onFailure { Log.w(TAG, "faces: ${it.message}") }
        }
    }

    private fun answer(c: Context, lang: String, texts: List<String>, err: String?) {
        session = null
        Live.link?.priority(Live.visible)
        if (texts.isEmpty()) {
            if (err == "idioma") Voice.download(c, lang)
            return reply("voice err ${err ?: "nada"}")
        }
        val a = VoiceCommands.parse(texts, english, faces)
        val heard = clean(texts.first()).take(80)
        reply(if (a != null) "voice ask ${a.command}|$heard|${clean(a.describe)}" else "voice fail $heard")
    }

    private fun clean(s: String) = s.replace('|', ' ').replace('\n', ' ').trim()

    private fun reply(line: String) {
        Log.i(TAG, line)
        val l = Live.link ?: return
        scope.launch { runCatching { l.send(line) }.onFailure { Log.w(TAG, "send: ${it.message}") } }
    }

    /** A little automatic gain: quiet speech from a wrist's distance up to a level the recognizer likes. */
    private fun louder(pcm: ShortArray): ByteArray {
        var p = 1
        for (v in pcm) p = maxOf(p, kotlin.math.abs(v.toInt()))
        peak = maxOf((peak * 0.9).toInt(), p, 800)
        val g = (12000f / peak).coerceIn(1f, 6f)
        val out = ByteArray(pcm.size * 2)
        for (k in pcm.indices) {
            val v = (pcm[k] * g).toInt().coerceIn(-32768, 32767)
            out[2 * k] = v.toByte()
            out[2 * k + 1] = (v shr 8).toByte()
        }
        return out
    }
}
