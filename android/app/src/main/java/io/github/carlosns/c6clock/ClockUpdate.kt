package io.github.carlosns.c6clock

import android.content.Context
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
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

/** Downloads [url] into [file], checking it is exactly [size] bytes with the given SHA-256. */
suspend fun downloadChecked(url: String, size: Long, sha256: String, file: File, progress: (Int) -> Unit) =
    withContext(Dispatchers.IO) {
        val conn = URL(url).openConnection() as HttpURLConnection
        conn.connectTimeout = 15_000
        conn.readTimeout = 30_000
        val sha = MessageDigest.getInstance("SHA-256")
        var got = 0L
        try {
            check(conn.responseCode == 200) { "HTTP ${conn.responseCode}" }
            conn.inputStream.use { input ->
                file.outputStream().use { out ->
                    val buf = ByteArray(64 * 1024)
                    var shown = -1
                    while (true) {
                        val n = input.read(buf)
                        if (n < 0) break
                        check(got + n <= size) { "la descarga es más grande de lo anunciado" }
                        out.write(buf, 0, n)
                        sha.update(buf, 0, n)
                        got += n
                        val pct = (got * 100 / size).toInt()
                        if (pct / 10 != shown) { shown = pct / 10; progress(pct) }
                    }
                }
            }
        } finally {
            conn.disconnect()
        }
        check(got == size) { "descarga incompleta" }
        val hex = sha.digest().joinToString("") { "%02x".format(it) }
        check(hex.equals(sha256, true)) { "la descarga no coincide (SHA-256)" }
    }

/**
 * Updates the clock's firmware without its Wi-Fi: the phone downloads the image from the manifest and
 * streams it over Bluetooth (protocol in src/bleota.h). The clock checks the SHA-256 again, keeps its
 * current firmware on any failure and reboots into the new one when it is whole.
 */
object ClockUpdate {
    /** Downloads the image of the manifest [entry] (boards.<id>) into the cache. */
    suspend fun download(c: Context, entry: JSONObject, progress: (Int) -> Unit): File {
        val file = File(c.cacheDir, "marea-ota.bin")
        downloadChecked(entry.getString("url"), entry.getLong("size"), entry.getString("sha256"), file, progress)
        return file
    }

    /** Sends [file] to the clock; [progress] gets the percentage the clock has written. Throws on failure. */
    suspend fun send(link: ClockBle, file: File, entry: JSONObject, progress: (Int) -> Unit) = coroutineScope {
        check(link.canOta) { "este reloj aún no admite actualizarse por Bluetooth" }
        val data = withContext(Dispatchers.IO) { file.readBytes() }
        val replies = Channel<JSONObject>(Channel.UNLIMITED)
        val listener = launch(start = CoroutineStart.UNDISPATCHED) {
            link.events().filter { it.optString("t") == "ota" }.collect { replies.send(it) }
        }
        var acked = 0
        var done = false
        fun handle(m: JSONObject) {
            when (m.optString("st")) {
                "ack" -> { acked = m.optInt("off"); progress((acked.toLong() * 100 / data.size).toInt()) }
                "done" -> done = true
                "err" -> throw IllegalStateException(m.optString("msg").ifEmpty { "el reloj la rechazó" })
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
            link.send("ota begin ${data.size} ${entry.getString("sha256")} ${entry.getString("version")}")
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
            // The last pieces are written and the image checked; then it says "done" and reboots
            while (!done) next(30_000)
            progress(100)
        } catch (e: Exception) {
            withContext(NonCancellable) { runCatching { link.send("ota abort") } }
            throw e
        } finally {
            listener.cancel()
        }
    }
}
