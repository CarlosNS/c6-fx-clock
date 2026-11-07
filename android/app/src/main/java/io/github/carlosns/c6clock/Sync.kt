package io.github.carlosns.c6clock

import org.json.JSONObject

/** What a sync needs from the link to the clock; ClockBle implements it, the unit tests fake it. */
interface ClockChannel {
    suspend fun send(cmd: String)
    /** Sends [cmd] and returns the first message of type [t] that [match] accepts (listening starts first). */
    suspend fun exchange(cmd: String, t: String, timeoutMs: Long = 10_000, match: (JSONObject) -> Boolean = { true }): JSONObject
}

/** "get <what>" and the clock's {"t":"<what>",...} answer. */
suspend fun ClockChannel.request(what: String) = exchange("get $what", what)

data class SyncResult(
    val version: String,
    /** Board id ("c6lcd", "s3amoled"); empty from firmware older than the S3 port, which means C6. */
    val board: String,
    val syncCount: Int,
    val weatherSent: Boolean,
    val place: String,
    /** The clock's last update attempt, {"state":...,"at":n}, for the app to report (phase 6). */
    val update: JSONObject?,
)

/**
 * One periodic sync (docs/PLAN-bluetooth.md, 3.6), the same steps as tests/clocklib/phone.py:
 * time, weather for the clock's own city, info, "sync done". The caller connects and disconnects.
 */
class SyncSession(
    private val link: ClockChannel,
    /** Returns the "wx ..." command for a place, or null without internet. */
    private val weather: suspend (lat: Double, lon: Double) -> String?,
    private val nowMs: () -> Long = System::currentTimeMillis,
) {
    suspend fun run(): SyncResult {
        link.send(Protocol.timeCommand(nowMs()))
        val status = link.request("status")
        val wx = runCatching { weather(status.getDouble("wlat"), status.getDouble("wlon")) }.getOrNull()
        if (wx != null) link.send(wx)
        val info = link.request("info")
        link.exchange("sync done", "ack") { it.optString("cmd") == "sync" }
        return SyncResult(
            version = info.optString("ver"),
            board = info.optString("board"),
            syncCount = info.optJSONObject("sync")?.optInt("count") ?: 0,
            weatherSent = wx != null,
            place = status.optString("wloc"),
            update = info.optJSONObject("upd"),
        )
    }
}
