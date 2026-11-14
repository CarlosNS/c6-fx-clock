package io.github.carlosns.c6clock

import android.content.Context
import android.util.Base64
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.security.MessageDigest

/**
 * "Mis esferas": the custom faces made in the page's editor. Each one is kept on the phone (a backup
 * the watch's microSD can be restored from) in files/esferas/<id>/:
 *   face.json    the settings, as the watch reads them, plus the editor's (each stretch's crop and source)
 *   bg_N.jpg or anim_N.bin   what the watch shows in each stretch of the day (src/fx_custom.c); faces
 *                from before the stretches have one bg.jpg or anim.bin
 *   source_N.*   each picture as picked, to crop it again (source.* for the older faces)
 *   thumb.jpg    for the list
 * The page reads them at https://appassets.androidplatform.net/mis/<id>/... (MainActivity) and the
 * watch gets face.json and bg.jpg / anim.bin into /esferas/<id>/ on its card (src/bleota.h, file mode).
 */
object CustomFaces {
    fun dir(c: Context) = File(c.filesDir, "esferas").apply { mkdirs() }

    /** What the phone keeps: [{id, name, anim}] */
    fun local(c: Context): JSONArray {
        val out = JSONArray()
        dir(c).listFiles()?.mapNotNull { it.name.toIntOrNull()?.let { id -> id to it } }?.sortedBy { it.first }?.forEach { (id, d) ->
            val j = runCatching { JSONObject(File(d, "face.json").readText()) }.getOrNull() ?: return@forEach
            out.put(JSONObject().put("id", id).put("name", j.optString("name")).put("anim", j.optInt("anim"))
                .put("v", File(d, "face.json").lastModified()))
        }
        return out
    }

    private fun b64(s: String): ByteArray = Base64.decode(s.substringAfter(","), Base64.DEFAULT)
    private val SAFE = Regex("^[A-Za-z0-9_.-]{1,40}$")

    /** The pictures a face shows (one per stretch of the day), as named in its face.json. */
    fun pictures(json: JSONObject): List<String> {
        val slots = json.optJSONArray("slots")
        if (slots == null || slots.length() == 0) return listOf(if (json.optInt("anim") == 1) "anim.bin" else "bg.jpg")
        return (0 until slots.length()).mapNotNull { slots.optJSONObject(it)?.optString("pic")?.takeIf { p -> SAFE.matches(p) } }
    }

    /**
     * Stores a face on the phone; returns its folder. [files] maps file names (pictures, sources, the
     * thumbnail) to base64; files of stretches no longer in the face are removed.
     */
    fun store(c: Context, id: Int, json: String, files: JSONObject): File {
        val d = File(dir(c), id.toString()).apply { mkdirs() }
        val j = JSONObject(json)
        File(d, "face.json").writeText(json)
        for (name in files.keys()) {
            require(SAFE.matches(name)) { "nombre de archivo no válido" }
            File(d, name).writeBytes(b64(files.getString(name)))
        }
        val keep = pictures(j).toMutableSet()
        j.optJSONArray("slots")?.let { s -> for (i in 0 until s.length()) s.optJSONObject(i)?.optString("src")?.let { keep += it } }
        d.listFiles()?.filter { f -> Regex("^(bg|anim|source)[._].*").matches(f.name) && f.name !in keep }?.forEach { it.delete() }
        return d
    }

    fun delete(c: Context, id: Int) = File(dir(c), id.toString()).deleteRecursively()

    /** Sends a stored face to the watch (its pictures, or only [only], then the settings); [progress] 0..100. */
    suspend fun send(link: ClockBle, d: File, only: Set<String>? = null, progress: (Int) -> Unit) {
        val id = d.name
        val json = File(d, "face.json").readBytes()
        val pics = pictures(JSONObject(String(json))).filter { only == null || it in only }.map { File(d, it) }.filter { it.exists() }
        val sizes = pics.map { it.length() }
        val total = sizes.sum().coerceAtLeast(1)
        var done = 0L
        for ((i, pic) in pics.withIndex()) {
            val data = withContext(Dispatchers.IO) { pic.readBytes() }
            sendFile(link, data, "/esferas/$id/${pic.name}") { progress(((done + sizes[i] * it / 100) * 95 / total).toInt()) }
            done += sizes[i]
        }
        // the settings last: the watch only lists a face once its face.json is there
        sendFile(link, json, "/esferas/$id/face.json") { }
        link.send("cust reload $id")
        progress(100)
    }

    /**
     * One file to the watch's card, the way firmware goes (src/bleota.h, file mode): "file begin <size>
     * <sha256> <path>", then packets <offset><bytes> on the OTA characteristic within the window it
     * gives, acknowledged as it stores them; "done" when it is on the card.
     */
    suspend fun sendFile(link: ClockBle, data: ByteArray, path: String, progress: (Int) -> Unit) = coroutineScope {
        check(link.canOta) { "este reloj aún no admite archivos por Bluetooth" }
        val sha = MessageDigest.getInstance("SHA-256").digest(data).joinToString("") { "%02x".format(it) }
        val replies = Channel<JSONObject>(Channel.UNLIMITED)
        val listener = launch(start = CoroutineStart.UNDISPATCHED) {
            link.events().filter { it.optString("t") == "file" }.collect { replies.send(it) }
        }
        var acked = 0
        var done = false
        fun handle(m: JSONObject) {
            when (m.optString("st")) {
                "ack" -> { acked = m.optInt("off"); progress((acked.toLong() * 100 / data.size).toInt()) }
                "done" -> done = true
                "err" -> throw IllegalStateException(m.optString("msg").ifEmpty { "el reloj lo rechazó" })
            }
        }
        suspend fun next(ms: Long): JSONObject {
            val m = try {
                withTimeout(ms) { replies.receive() }
            } catch (e: TimeoutCancellationException) {
                throw IllegalStateException("el reloj dejó de responder")
            }
            handle(m)
            return m
        }
        try {
            link.fast()
            link.send("file begin ${data.size} $sha $path")
            val ready = next(15_000)
            val window = ready.optInt("win", 16 * 1024)
            val chunk = (link.mtu - 3 - 4).coerceIn(16, 240)
            var off = 0
            while (off < data.size) {
                while (true) handle(replies.tryReceive().getOrNull() ?: break)
                if (off - acked + chunk > window) {
                    next(20_000)
                    continue
                }
                val n = minOf(chunk, data.size - off)
                val pkt = ByteArray(4 + n)
                for (i in 0 until 4) pkt[i] = (off shr (8 * i)).toByte()
                System.arraycopy(data, off, pkt, 4, n)
                link.writeOta(pkt)
                off += n
            }
            while (!done) next(30_000)
            progress(100)
        } catch (e: Exception) {
            withContext(NonCancellable) { runCatching { link.send("file abort") } }
            throw e
        } finally {
            listener.cancel()
            link.priority(Live.visible)
        }
    }
}
