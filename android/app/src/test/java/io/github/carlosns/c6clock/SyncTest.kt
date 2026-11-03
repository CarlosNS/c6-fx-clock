package io.github.carlosns.c6clock

import kotlinx.coroutines.test.runTest
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** The background sync against a pretend clock that answers like the real one (ble.c). */
class SyncTest {
    private class FakeClock : ClockChannel {
        val sent = mutableListOf<String>()
        var syncs = 4

        override suspend fun send(cmd: String) {
            sent += cmd
        }

        override suspend fun exchange(cmd: String, t: String, timeoutMs: Long, match: (JSONObject) -> Boolean): JSONObject {
            send(cmd)
            val answer = when (cmd) {
                "get status" -> JSONObject("""{"t":"status","wlat":37.3891,"wlon":-5.9845,"wloc":"Sevilla","fx":22}""")
                "get info" -> JSONObject("""{"t":"info","ver":"2.0.0","mode":"ble","sync":{"last":0,"count":$syncs},"upd":{"state":"none","at":0}}""")
                "sync done" -> JSONObject("""{"t":"ack","cmd":"sync"}""").also { syncs++ }
                else -> error("unexpected $cmd")
            }
            assertEquals(t, answer.getString("t"))
            assertTrue(match(answer))
            return answer
        }
    }

    @Test fun sendsTimeWeatherAndSyncDoneInOrder() = runTest {
        val clock = FakeClock()
        var asked: Pair<Double, Double>? = null
        val r = SyncSession(clock, { lat, lon -> asked = lat to lon; "wx 185 3 63 13 250 110 1" }, { 1_762_160_000_123 }).run()
        assertEquals(listOf("time 1762160000 123", "get status", "wx 185 3 63 13 250 110 1", "get info", "sync done"), clock.sent)
        assertEquals(37.3891 to -5.9845, asked)  // the weather is for the clock's own city
        assertEquals("2.0.0", r.version)
        assertEquals(4, r.syncCount)
        assertEquals("Sevilla", r.place)
        assertTrue(r.weatherSent)
        assertEquals("none", r.update!!.getString("state"))
    }

    @Test fun withoutInternetTheClockStillGetsTheTime() = runTest {
        val clock = FakeClock()
        val r = SyncSession(clock, { _, _ -> throw java.io.IOException("no internet") }).run()
        assertFalse(r.weatherSent)
        assertTrue(clock.sent.first().startsWith("time "))
        assertFalse(clock.sent.any { it.startsWith("wx ") })
        assertEquals("sync done", clock.sent.last())
    }

    @Test fun syncsEveryHalfHour() {
        assertEquals(30L, SyncWorker.PERIOD_MIN)  // docs/PLAN-bluetooth.md 3.6; WorkManager's minimum is 15
    }
}
