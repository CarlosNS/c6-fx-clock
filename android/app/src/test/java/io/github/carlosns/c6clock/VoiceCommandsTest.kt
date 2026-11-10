package io.github.carlosns.c6clock

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import java.time.LocalTime

class VoiceCommandsTest {
    private val faces = listOf(
        VoiceCommands.Face(0, "Plasma"), VoiceCommands.Face(14, "Arena"), VoiceCommands.Face(24, "Ónix"),
        VoiceCommands.Face(21, "Clásico"), VoiceCommands.Face(9, "Agujero negro"),
    )
    private val noon = LocalTime.of(12, 0)

    private fun es(s: String) = VoiceCommands.parseOne(s, false, faces, noon)?.command
    private fun en(s: String) = VoiceCommands.parseOne(s, true, faces, noon)?.command

    @Test fun spanishAlarms() {
        assertEquals("alarm add 7 30 31", es("pon una alarma a las siete y media de lunes a viernes"))
        assertEquals("alarm add 7 30 31", es("Pon una alarma a las 7:30 de lunes a viernes"))
        assertEquals("alarm add 6 45 0", es("alarma a las siete menos cuarto"))
        assertEquals("alarm add 19 15 127", es("despiértame a las siete y cuarto de la tarde todos los días"))
        assertEquals("alarm add 8 35 96", es("alarma a las ocho y treinta y cinco el fin de semana"))
        assertEquals("alarm add 1 0 0", es("alarma a la una"))
        assertEquals("alarm add 22 0 5", es("alarma a las diez de la noche los lunes y miércoles"))
        assertEquals("alarm add 12 10 0", es("pon una alarma en diez minutos"))
        assertEquals("alarm add 6 5 14", es("alarma a las seis y cinco de martes a jueves"))
    }

    @Test fun englishAlarms() {
        assertEquals("alarm add 7 30 31", en("Set an alarm for seven thirty on weekdays"))
        assertEquals("alarm add 18 0 0", en("wake me up at 6 pm"))
        assertEquals("alarm add 6 45 127", en("alarm at quarter to seven every day"))
        assertEquals("alarm add 7 5 0", en("set an alarm for seven oh five"))
        assertEquals("alarm add 13 0 0", en("alarm in one hour"))
    }

    @Test fun switches() {
        assertEquals("dnd 1", es("activa no molestar"))
        assertEquals("dnd 0", es("quita el no molestar"))
        assertEquals("wifi 0", es("apaga el wifi"))
        assertEquals("wifi 1", en("turn on the wi-fi"))
        assertEquals("bri 50", es("pon el brillo al cincuenta por ciento"))
        assertEquals("bri 100", es("brillo al máximo"))
        assertEquals("vol 30", en("volume thirty"))
        assertEquals("auto 0", es("desactiva el cambio automático"))
    }

    @Test fun facesAndApps() {
        assertEquals("fx 24", es("pon la esfera ónix"))
        assertEquals("fx 9", es("cambia a la esfera agujero negro"))
        assertEquals("fx 21", en("show the classic face".replace("classic", "clasico")))
        assertEquals("fx 14", es("pon arena"))
        assertEquals("ui app 3", es("abre el cronómetro"))
        assertEquals("ui app 5", es("enciende la linterna"))
        assertEquals("ui app 1", es("abre las alarmas"))
        assertEquals("ui app 5", en("turn on the flashlight"))
    }

    @Test fun nothing() {
        assertNull(es("hola qué tal"))
        assertNull(es("alarma"))
        assertNull(en("what's the weather like"))
    }

    @Test fun describes() {
        assertEquals("Alarma a las 07:30, de lunes a viernes",
            VoiceCommands.parseOne("alarma a las siete y media entre semana", false, faces, noon)?.describe)
        assertEquals("Alarm at 18:00, once", VoiceCommands.parseOne("wake me up at 6 pm", true, faces, noon)?.describe)
    }

    @Test fun adpcmRoundTrip() {
        // A 440 Hz tone through the watch's encoder (same algorithm as voice.c) and back
        val pcm = ShortArray(1600) { (8000 * Math.sin(2 * Math.PI * 440 * it / 16000)).toInt().toShort() }
        val enc = Adpcm.encodeForTest(pcm)
        val dec = Adpcm.decode(enc, 0, 0)
        var err = 0L
        for (i in 200 until pcm.size) err += Math.abs(pcm[i] - dec[i])
        assert(err / (pcm.size - 200) < 400) { "mean error ${err / (pcm.size - 200)}" }
    }
}
