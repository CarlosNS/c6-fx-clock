package io.github.carlosns.c6clock

import org.json.JSONObject
import java.util.UUID
import kotlin.math.roundToInt

// The clock's Bluetooth protocol (docs/PLAN-bluetooth.md, section 3). The same rules live in
// tests/clocklib/protocol.py (PC tests) and src/transport.js (the page).
object Protocol {
    val SERVICE: UUID = UUID.fromString("7f1c0001-6c0c-4e0b-a1c6-0000c6c10c00")
    val CMD: UUID = UUID.fromString("7f1c0002-6c0c-4e0b-a1c6-0000c6c10c00")
    val EVT: UUID = UUID.fromString("7f1c0003-6c0c-4e0b-a1c6-0000c6c10c00")
    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    const val MTU = 247

    const val LAST = 0x80
    const val FIRST = 0x40
    const val ID_MASK = 0x3F

    /** Splits one message into notifications of at most [chunk] bytes, header included. */
    fun frame(id: Int, payload: ByteArray, chunk: Int): List<ByteArray> {
        val body = chunk - 1
        require(body >= 1) { "chunk too small" }
        val parts = (payload.indices step body).map { payload.copyOfRange(it, minOf(it + body, payload.size)) }
            .ifEmpty { listOf(ByteArray(0)) }
        return parts.mapIndexed { i, p ->
            var h = id and ID_MASK
            if (i == 0) h = h or FIRST
            if (i == parts.lastIndex) h = h or LAST
            byteArrayOf(h.toByte()) + p
        }
    }

    /** "time <epoch> <ms>": the clock keeps its own time between syncs. */
    fun timeCommand(nowMs: Long) = "time ${nowMs / 1000} ${nowMs % 1000}"

    /** Builds the "wx ..." command from an Open-Meteo forecast, exactly as the page does. */
    fun weatherCommand(forecast: JSONObject): String {
        val c = forecast.getJSONObject("current")
        val d = forecast.getJSONObject("daily")
        fun t10(v: Double) = (v * 10).roundToInt()
        return "wx ${t10(c.getDouble("temperature_2m"))} ${c.getInt("weather_code")} ${c.getInt("relative_humidity_2m")} " +
            "${c.getDouble("wind_speed_10m").roundToInt()} ${t10(d.getJSONArray("temperature_2m_max").getDouble(0))} " +
            "${t10(d.getJSONArray("temperature_2m_min").getDouble(0))} ${c.getInt("is_day")}"
    }

    fun weatherUrl(lat: Double, lon: Double) =
        "https://api.open-meteo.com/v1/forecast?latitude=$lat&longitude=$lon" +
            "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,is_day" +
            "&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1"
}

/** Joins notifications back into whole messages. Out-of-sequence data drops the partial message. */
class Reassembler {
    private var id: Int? = null
    private val buf = java.io.ByteArrayOutputStream()
    var dropped = 0
        private set

    fun feed(data: ByteArray): ByteArray? {
        if (data.isEmpty()) return null
        val h = data[0].toInt() and 0xFF
        val mid = h and Protocol.ID_MASK
        when {
            h and Protocol.FIRST != 0 -> {
                if (id != null) dropped++
                id = mid
                buf.reset()
            }
            id == mid -> Unit
            else -> {
                dropped++
                id = null
                buf.reset()
                return null
            }
        }
        buf.write(data, 1, data.size - 1)
        if (h and Protocol.LAST == 0) return null
        id = null
        return buf.toByteArray().also { buf.reset() }
    }
}
