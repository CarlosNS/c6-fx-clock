package io.github.carlosns.c6clock

import java.text.Normalizer
import java.time.LocalTime

/**
 * Turns what the recognizer heard ("pon una alarma a las siete y media de lunes a viernes") into a
 * watch command ("alarm add 7 30 31") and a line that says what it will do, for the watch to ask
 * before doing it. Spanish and English, numbers in words or digits. Plain Kotlin, so it is unit tested
 * (VoiceCommandsTest).
 */
object VoiceCommands {
    data class Action(val command: String, val describe: String)
    data class Face(val index: Int, val name: String)

    /** The first of the recognizer's guesses that means something, or null. */
    fun parse(texts: List<String>, english: Boolean, faces: List<Face>, now: LocalTime = LocalTime.now()): Action? =
        texts.firstNotNullOfOrNull { parseOne(it, english, faces, now) }

    fun parseOne(text: String, en: Boolean, faces: List<Face>, now: LocalTime = LocalTime.now()): Action? {
        val t = numbers(normalize(text), en)
        val w = " $t "
        fun has(vararg k: String) = k.any { w.contains(" $it ") }
        val off = if (en) has("off", "disable", "deactivate", "stop", "turn off", "switch off")
        else has("apaga", "apagar", "apague", "desactiva", "desactivar", "quita", "quitar", "desconecta", "deshabilita", "sin")
        fun onOff(v: Boolean) = if (v) 1 else 0
        fun said(es: String, enText: String) = if (en) enText else es
        fun state(es: String, enText: String) = if (off) said("Desactivar $es", "Turn $enText off") else said("Activar $es", "Turn $enText on")

        // Alarm: a time is needed (otherwise "open the alarms" may be meant)
        val alarmWord = if (en) has("alarm", "wake me", "wake me up") else has("alarma", "despiertame", "despierta", "despertador")
        if (alarmWord) {
            val hm = time(t, en, now)
            if (hm != null) {
                val d = days(t, en)
                val hh = "%02d:%02d".format(hm.first, hm.second)
                return Action("alarm add ${hm.first} ${hm.second} $d", said("Alarma a las $hh, ${daysEs(d)}", "Alarm at $hh, ${daysEn(d)}"))
            }
        }
        // Settings that are switched on or off
        if (has("no molestar", "do not disturb", "dont disturb")) return Action("dnd ${onOff(!off)}", state("no molestar", "do not disturb"))
        if (has("wifi", "wi fi")) return Action("wifi ${onOff(!off)}", if (off) said("Apagar el WiFi", "Turn the Wi-Fi off") else said("Encender el WiFi", "Turn the Wi-Fi on"))
        if (has("siempre encendida", "pantalla siempre", "always on")) return Action("aon ${onOff(!off)}", state("pantalla siempre encendida", "always on"))
        if (has("modo noche", "modo nocturno", "night mode")) return Action("non ${onOff(!off)}", state("modo noche", "night mode"))
        if (has("cambio automatico", "automatico", "auto change", "automatic change", "change automatically"))
            return Action("auto ${onOff(!off)}", state("cambio automático de esfera", "automatic face change"))
        // Brightness and volume: a number, or the ends
        if (has("brillo", "brightness")) level(t, en, 5)?.let { return Action("bri $it", said("Brillo al $it %", "Brightness $it %")) }
        if (has("volumen", "volume")) level(t, en, 0)?.let { return Action("vol $it", said("Volumen al $it %", "Volume $it %")) }
        // A face by its name
        val faceWord = if (en) has("face", "watch face", "dial") else has("esfera", "pon", "cambia", "poner", "cambiar", "muestra")
        if (faceWord) face(t, faces)?.let { return Action("fx ${it.index}", said("Poner la esfera ${it.name}", "Show the ${it.name} face")) }
        // Open an app (the flashlight lights up when it opens)
        val apps = if (en) APPS_EN else APPS_ES
        val opens = if (en) has("open", "show", "start", "turn on", "switch on", "launch") else has("abre", "abrir", "muestra", "ensename", "enciende", "pon", "inicia")
        if (opens || apps.keys.any { t == it }) {
            for ((k, i) in apps) if (has(k)) return Action("ui app $i", said("Abrir ${APP_NAMES_ES[i]}", "Open ${APP_NAMES_EN[i]}"))
        }
        return null
    }

    // ---- words to numbers

    /** Lower case, no accents, no punctuation but the ":" of times, single spaces. */
    fun normalize(s: String): String {
        val n = Normalizer.normalize(s.lowercase(), Normalizer.Form.NFD).replace(Regex("\\p{M}+"), "")
        return n.replace(Regex("(\\d)[:.h](\\d\\d)\\b"), "$1:$2").replace("%", " por ciento ")
            .replace("'", "").replace(Regex("[^a-z0-9: ]"), " ").replace(Regex("\\s+"), " ").trim()
    }

    private val ES_UNITS = mapOf(
        "cero" to 0, "un" to 1, "una" to 1, "uno" to 1, "dos" to 2, "tres" to 3, "cuatro" to 4, "cinco" to 5, "seis" to 6,
        "siete" to 7, "ocho" to 8, "nueve" to 9, "diez" to 10, "once" to 11, "doce" to 12, "trece" to 13, "catorce" to 14,
        "quince" to 15, "dieciseis" to 16, "diecisiete" to 17, "dieciocho" to 18, "diecinueve" to 19, "veinte" to 20,
        "veintiuno" to 21, "veintiuna" to 21, "veintiun" to 21, "veintidos" to 22, "veintitres" to 23, "veinticuatro" to 24,
        "veinticinco" to 25, "veintiseis" to 26, "veintisiete" to 27, "veintiocho" to 28, "veintinueve" to 29, "cien" to 100,
    )
    private val ES_TENS = mapOf("treinta" to 30, "cuarenta" to 40, "cincuenta" to 50, "sesenta" to 60, "setenta" to 70, "ochenta" to 80, "noventa" to 90)
    private val EN_UNITS = mapOf(
        "zero" to 0, "oh" to 0, "one" to 1, "two" to 2, "three" to 3, "four" to 4, "five" to 5, "six" to 6, "seven" to 7,
        "eight" to 8, "nine" to 9, "ten" to 10, "eleven" to 11, "twelve" to 12, "thirteen" to 13, "fourteen" to 14,
        "fifteen" to 15, "sixteen" to 16, "seventeen" to 17, "eighteen" to 18, "nineteen" to 19, "hundred" to 100,
    )
    private val EN_TENS = mapOf("twenty" to 20, "thirty" to 30, "forty" to 40, "fifty" to 50, "sixty" to 60, "seventy" to 70, "eighty" to 80, "ninety" to 90)

    /** Number words become digits: "las siete y treinta y cinco" -> "las 7 y 35", "seven forty five" -> "7 45". */
    fun numbers(s: String, en: Boolean): String {
        val units = if (en) EN_UNITS else ES_UNITS
        val tens = if (en) EN_TENS else ES_TENS
        val w = s.split(" ")
        val out = mutableListOf<String>()
        var i = 0
        while (i < w.size) {
            val tv = tens[w[i]]
            if (tv != null) {
                // "treinta y cinco", "forty five", "forty-five" (already split)
                val j = if (!en && w.getOrNull(i + 1) == "y") i + 2 else i + 1
                val u = w.getOrNull(j)?.let { units[it] }
                if (u != null && u in 1..9) { out += (tv + u).toString(); i = j + 1 } else { out += tv.toString(); i++ }
                continue
            }
            // "seven oh five": the minutes are 05
            if (en && w[i] == "oh" && out.lastOrNull()?.all { it.isDigit() } == true) {
                val u = w.getOrNull(i + 1)?.let { units[it] }
                if (u != null && u in 1..9) { out += "0$u"; i += 2; continue }
            }
            // "oh" is a number only inside a time
            val uv = units[w[i]]
            if (uv != null && !(en && w[i] == "oh" && out.lastOrNull()?.all { it.isDigit() } != true)) out += uv.toString() else out += w[i]
            i++
        }
        return out.joinToString(" ")
    }

    // ---- times and days

    /** Hour and minute in what was said, or null. Relative times ("en diez minutos") count from now. */
    fun time(t: String, en: Boolean, now: LocalTime): Pair<Int, Int>? {
        val rel = Regex(if (en) "\\bin (\\d+) (minute|minutes|hour|hours)\\b" else "\\b(?:en|dentro de) (\\d+) (minuto|minutos|hora|horas)\\b").find(t)
        if (rel != null) {
            val n = rel.groupValues[1].toLong()
            val at = if (rel.groupValues[2].startsWith(if (en) "h" else "h")) now.plusHours(n) else now.plusMinutes(n)
            return at.hour to at.minute
        }
        var h: Int
        var m = 0
        val clock = Regex("\\b(\\d{1,2}):(\\d\\d)\\b").find(t)
        if (clock != null) {
            h = clock.groupValues[1].toInt()
            m = clock.groupValues[2].toInt()
        } else if (en) {
            val past = Regex("\\b(half|quarter|\\d+) (?:minutes )?(past|after|to|before) (\\d{1,2})\\b").find(t)
            val plain = Regex("\\b(?:at|for) (\\d{1,2})(?: (\\d{1,2}))?\\b").find(t) ?: Regex("\\b(\\d{1,2})(?: (\\d{2}))?(?= ?(?:am|pm|a m|p m|o clock|oclock|in the|at night|tonight))").find(t)
                ?: Regex("\\b(\\d{1,2}) (\\d{2})\\b").find(t)
            if (past != null) {
                val q = when (past.groupValues[1]) { "half" -> 30; "quarter" -> 15; else -> past.groupValues[1].toInt() }
                h = past.groupValues[3].toInt()
                if (past.groupValues[2] == "to" || past.groupValues[2] == "before") { h -= 1; m = 60 - q } else m = q
            } else if (plain != null) {
                h = plain.groupValues[1].toInt()
                m = plain.groupValues[2].takeIf { it.isNotEmpty() }?.toInt() ?: 0
            } else return null
        } else {
            // "a las 7", "a la 1", "las 7 y media", "7 menos cuarto", "7 y 20", "7 30"
            val r = Regex("\\b(?:a las|a la|las|la|a) (\\d{1,2})\\b").find(t) ?: Regex("\\b(\\d{1,2}) (?:y|menos|en punto|de la)\\b").find(t)
                ?: Regex("\\b(\\d{1,2}) (\\d{2})\\b").find(t) ?: return null
            h = r.groupValues[1].toInt()
            val rest = t.substring(r.range.last + 1).trim()
            val mm = Regex("^(\\d{2})\\b").find(rest)
            when {
                rest.startsWith("y media") -> m = 30
                rest.startsWith("y cuarto") -> m = 15
                rest.startsWith("menos cuarto") -> { h -= 1; m = 45 }
                Regex("^menos (\\d{1,2})\\b").find(rest) != null -> { h -= 1; m = 60 - Regex("^menos (\\d{1,2})").find(rest)!!.groupValues[1].toInt() }
                Regex("^y (\\d{1,2})\\b").find(rest) != null -> m = Regex("^y (\\d{1,2})").find(rest)!!.groupValues[1].toInt()
                mm != null -> m = mm.groupValues[1].toInt()
            }
        }
        if (h < 0) h += 12
        // Morning or evening, said or (for 1..11 with nothing said) left as the 24 h reading
        val pm = if (en) Regex("\\b(pm|p m|in the evening|in the afternoon|at night|tonight)\\b").containsMatchIn(t)
        else Regex("\\b(de la tarde|de la noche|por la tarde|por la noche|esta tarde|esta noche)\\b").containsMatchIn(t)
        val am = if (en) Regex("\\b(am|a m|in the morning)\\b").containsMatchIn(t)
        else Regex("\\b(de la manana|de la madrugada|por la manana)\\b").containsMatchIn(t)
        if (pm && h in 1..11) h += 12
        if (am && h == 12) h = 0
        if (!en && h == 12 && Regex("\\b12 de la noche\\b").containsMatchIn(t)) h = 0
        if (h !in 0..23 || m !in 0..59) return null
        return h to m
    }

    private val ES_DAYS = listOf("lunes", "martes", "miercoles", "jueves", "viernes", "sabado", "domingo")
    private val EN_DAYS = listOf("monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday")

    /** Days mask, bit 0 = Monday; 0 = just once. */
    fun days(t: String, en: Boolean): Int {
        val w = " $t "
        fun has(vararg k: String) = k.any { w.contains(" $it ") }
        if (en) {
            if (has("every day", "everyday", "daily", "all days")) return 127
            if (has("weekdays", "week days", "workdays", "monday to friday", "monday through friday")) return 31
            if (has("weekend", "weekends")) return 96
        } else {
            if (has("todos los dias", "cada dia", "diaria", "a diario", "siempre")) return 127
            if (has("entre semana", "laborables", "dias laborables", "de lunes a viernes")) return 31
            if (has("fin de semana", "fines de semana", "los findes", "finde")) return 96
        }
        val names = if (en) EN_DAYS else ES_DAYS
        // "de lunes a jueves" / "from monday to thursday"
        for (range in Regex(if (en) "(\\w+day)s? (?:to|through|till|until) (\\w+day)" else "(\\w+) a (?:el )?(\\w+)").findAll(t)) {
            fun day(x: String) = names.indexOf(x).takeIf { it >= 0 } ?: names.indexOf(x.removeSuffix("s"))
            val a = day(range.groupValues[1]); val b = day(range.groupValues[2])
            if (a >= 0 && b >= 0) return (0..6).filter { k -> if (a <= b) k in a..b else k >= a || k <= b }.fold(0) { m, k -> m or (1 shl k) }
        }
        var mask = 0
        names.forEachIndexed { k, n -> if (Regex("\\b${n}s?\\b").containsMatchIn(t)) mask = mask or (1 shl k) }
        return mask
    }

    fun daysEs(d: Int) = when (d) {
        0 -> "una vez"; 127 -> "todos los días"; 31 -> "de lunes a viernes"; 96 -> "el fin de semana"
        else -> listOf("lunes", "martes", "miércoles", "jueves", "viernes", "sábado", "domingo").filterIndexed { k, _ -> d shr k and 1 == 1 }.joinToString(", ")
    }

    fun daysEn(d: Int) = when (d) {
        0 -> "once"; 127 -> "every day"; 31 -> "weekdays"; 96 -> "weekends"
        else -> listOf("Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday").filterIndexed { k, _ -> d shr k and 1 == 1 }.joinToString(", ")
    }

    // ---- levels, faces, apps

    private fun level(t: String, en: Boolean, min: Int): Int? {
        val w = " $t "
        if (if (en) w.contains(" max") || w.contains(" full ") else w.contains(" maximo ") || w.contains(" tope ")) return 100
        if (if (en) w.contains(" min") || w.contains(" lowest ") else w.contains(" minimo ")) return min
        val n = Regex("\\b(\\d{1,3})\\b").find(t)?.groupValues?.get(1)?.toInt() ?: return null
        return n.coerceIn(min, 100)
    }

    /** The face whose name was said: whole name first, then the closest word (a recognizer may spell it oddly). */
    fun face(t: String, faces: List<Face>): Face? {
        val w = " $t "
        val byName = faces.map { it to normalize(it.name) }.filter { it.second.isNotEmpty() }
        byName.filter { w.contains(" ${it.second} ") }.maxByOrNull { it.second.length }?.let { return it.first }
        val words = t.split(" ").filter { it.length >= 3 && it !in STOP }
        var best: Face? = null
        var score = 0.0
        for ((f, n) in byName) for (word in words) for (part in n.split(" ").filter { it.length >= 3 }) {
            val s = similarity(word, part)
            if (s > score) { score = s; best = f }
        }
        return if (score >= 0.8) best else null
    }

    private val STOP = setOf("pon", "poner", "cambia", "cambiar", "esfera", "la", "las", "por", "una", "show", "the", "face", "watch", "set", "change", "muestra")

    private fun similarity(a: String, b: String): Double {
        val d = Array(a.length + 1) { IntArray(b.length + 1) }
        for (i in 0..a.length) d[i][0] = i
        for (j in 0..b.length) d[0][j] = j
        for (i in 1..a.length) for (j in 1..b.length)
            d[i][j] = minOf(d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + if (a[i - 1] == b[j - 1]) 0 else 1)
        return 1.0 - d[a.length][b.length].toDouble() / maxOf(a.length, b.length)
    }

    // The launcher's order (src/sys.c, s_apps)
    private val APP_NAMES_ES = listOf("Esferas", "Alarmas", "Pasos", "Cronómetro", "Música", "Linterna", "Archivos", "Ajustes")
    private val APP_NAMES_EN = listOf("Faces", "Alarms", "Steps", "Stopwatch", "Music", "Flashlight", "Files", "Settings")
    private val APPS_ES = linkedMapOf(
        "esferas" to 0, "alarmas" to 1, "pasos" to 2, "podometro" to 2, "cronometro" to 3, "musica" to 4,
        "linterna" to 5,"archivos" to 6, "ajustes" to 7, "configuracion" to 7,
    )
    private val APPS_EN = linkedMapOf(
        "faces" to 0, "alarms" to 1, "steps" to 2, "stopwatch" to 3, "music" to 4, "flashlight" to 5, "torch" to 5,
        "light" to 5, "files" to 6, "settings" to 7,
    )
}
