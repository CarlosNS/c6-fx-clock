package io.github.carlosns.c6clock

/** IMA ADPCM, 4 bits a sample, low nibble first: how the watch sends its microphone (src/voice.c). */
object Adpcm {
    private val STEP = intArrayOf(
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
        107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
        876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
        5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
        27086, 29794, 32767,
    )
    private val IDX = intArrayOf(-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)

    /** Two samples per byte, starting from the encoder state the watch sent with them. */
    fun decode(data: ByteArray, predictor: Int, index: Int): ShortArray {
        var pred = predictor
        var idx = index.coerceIn(0, 88)
        val out = ShortArray(data.size * 2)
        for (k in out.indices) {
            val code = (data[k / 2].toInt() shr (if (k % 2 == 0) 0 else 4)) and 0x0F
            val step = STEP[idx]
            var delta = step shr 3
            if (code and 4 != 0) delta += step
            if (code and 2 != 0) delta += step shr 1
            if (code and 1 != 0) delta += step shr 2
            pred = (if (code and 8 != 0) pred - delta else pred + delta).coerceIn(-32768, 32767)
            idx = (idx + IDX[code]).coerceIn(0, 88)
            out[k] = pred.toShort()
        }
        return out
    }

    /** The watch's encoder, for the unit test. */
    internal fun encodeForTest(pcm: ShortArray): ByteArray {
        var pred = 0
        var idx = 0
        val out = ByteArray((pcm.size + 1) / 2)
        for (k in pcm.indices) {
            var diff = pcm[k] - pred
            var code = 0
            if (diff < 0) { code = 8; diff = -diff }
            var step = STEP[idx]
            var delta = step shr 3
            if (diff >= step) { code = code or 4; diff -= step; delta += step }
            step = step shr 1
            if (diff >= step) { code = code or 2; diff -= step; delta += step }
            step = step shr 1
            if (diff >= step) { code = code or 1; delta += step }
            pred = (if (code and 8 != 0) pred - delta else pred + delta).coerceIn(-32768, 32767)
            idx = (idx + IDX[code]).coerceIn(0, 88)
            out[k / 2] = (out[k / 2].toInt() or (code shl (if (k % 2 == 0) 0 else 4))).toByte()
        }
        return out
    }
}
