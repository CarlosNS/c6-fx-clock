package io.github.carlosns.c6clock

import org.json.JSONObject
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.util.Random

/** Framing and the "wx" command, checked against tests/app/vectors.json (built from protocol.py). */
class ProtocolTest {
    private val vectors = JSONObject(File(System.getProperty("repo.root"), "tests/app/vectors.json").readText())

    private fun hex(s: String) = ByteArray(s.length / 2) { s.substring(it * 2, it * 2 + 2).toInt(16).toByte() }

    @Test fun framesMatchThePythonOnesByteForByte() {
        val list = vectors.getJSONArray("framing")
        for (i in 0 until list.length()) {
            val v = list.getJSONObject(i)
            val payload = hex(v.getString("payload"))
            val got = Protocol.frame(v.getInt("id"), payload, v.getInt("chunk"))
            val want = v.getJSONArray("frames")
            assertEquals("frame count, vector $i", want.length(), got.size)
            got.forEachIndexed { j, f -> assertArrayEquals("vector $i frame $j", hex(want.getString(j)), f) }
        }
    }

    @Test fun reassemblesThePythonFrames() {
        val list = vectors.getJSONArray("framing")
        for (i in 0 until list.length()) {
            val v = list.getJSONObject(i)
            val r = Reassembler()
            val frames = v.getJSONArray("frames")
            var out: ByteArray? = null
            for (j in 0 until frames.length()) {
                out = r.feed(hex(frames.getString(j)))
                if (j < frames.length() - 1) assertNull(out)
            }
            assertArrayEquals("vector $i", hex(v.getString("payload")), out)
        }
    }

    @Test fun roundTripsEverySize() {
        for (size in listOf(0, 1, 243, 244, 245, 6000)) for (chunk in listOf(20, 244)) {
            val p = ByteArray(size).also { Random(size.toLong()).nextBytes(it) }
            val frames = Protocol.frame(5, p, chunk)
            assertTrue(frames.all { it.size <= chunk })
            val r = Reassembler()
            assertArrayEquals(p, frames.map { r.feed(it) }.last())
        }
    }

    @Test fun aNewMessageDropsTheUnfinishedOne() {
        val r = Reassembler()
        val a = Protocol.frame(1, ByteArray(100), 20)
        val b = Protocol.frame(2, "bbbbbbbbbb".toByteArray(), 20)
        assertNull(r.feed(a[0]))
        assertEquals("bbbbbbbbbb", String(r.feed(b[0])!!))
        assertEquals(1, r.dropped)
    }

    @Test fun aStrayMiddleFrameIsIgnored() {
        val r = Reassembler()
        val a = Protocol.frame(3, ByteArray(100), 20)
        assertNull(r.feed(a[2]))
        assertEquals(1, r.dropped)
        a.forEach { r.feed(it) }
    }

    @Test fun weatherCommandMatchesThePage() {
        val w = vectors.getJSONObject("weather")
        assertEquals(w.getString("command"), Protocol.weatherCommand(w.getJSONObject("forecast")))
    }

    @Test fun timeCommandHasSecondsAndMillis() {
        assertEquals("time 1762160000 123", Protocol.timeCommand(1_762_160_000_123))
    }

    @Test fun firmwareVersionsCompareNumerically() {
        assertTrue(Firmware.compare("2.10.0", "2.9.1") > 0)
        assertTrue(Firmware.compare("2.0.0", "2.0.0") == 0)
        assertTrue(Firmware.compare("2.0", "2.0.1") < 0)
        assertEquals("2.1.0", Firmware.newer(JSONObject("""{"version":"2.1.0"}"""), "2.0.0"))
        assertNull(Firmware.newer(JSONObject("""{"version":"2.0.0"}"""), "2.0.0"))
        assertNull(Firmware.newer(JSONObject("""{"version":"2.1.0"}"""), ""))
        assertNull(Firmware.newer(JSONObject("{}"), "2.0.0"))
    }

    @Test fun appUpdateComparesVersionCodeAndNeedsACompleteEntry() {
        val sha = "a".repeat(64)
        fun m(app: String) = JSONObject("""{"version":"2.1.0","app":$app}""")
        val ok = """{"version":"0.3.1","code":4,"url":"https://x/a.apk","size":10,"sha256":"$sha"}"""
        assertEquals("0.3.1", Firmware.newerApp(m(ok), 3)?.optString("version"))
        assertNull(Firmware.newerApp(m(ok), 4))  // same build
        assertNull(Firmware.newerApp(m(ok), 5))  // never downgrade
        assertNull(Firmware.newerApp(JSONObject("""{"version":"2.1.0"}"""), 3))  // manifests before 3.0.0
        assertNull(Firmware.newerApp(m(ok.replace(sha, "abc")), 3))
        assertNull(Firmware.newerApp(m(ok.replace("\"size\":10", "\"size\":0")), 3))
        assertNull(Firmware.newerApp(m(ok.replace("https://x/a.apk", "")), 3))
    }
}
