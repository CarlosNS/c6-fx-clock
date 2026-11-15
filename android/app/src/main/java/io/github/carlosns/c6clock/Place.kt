package io.github.carlosns.c6clock

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.location.Geocoder
import android.location.Location
import android.location.LocationManager
import android.os.Build
import android.os.CancellationSignal
import android.util.Log
import androidx.core.content.ContextCompat
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import java.text.Normalizer
import java.util.Locale
import kotlin.coroutines.resume

/**
 * "Seguir al móvil": the watch's weather city follows the phone. Off by default; with it on, every sync
 * (on connecting and every 30 min) takes a coarse fix (Wi-Fi and cells, never GPS) and, if the phone is
 * more than [MOVE_KM] away from the watch's city, sends "loc <lat> <lon> <name>" before the weather.
 * Only the coarse location permission; the link service adds the location type while the app is on
 * screen (like the microphone), so it keeps working with the app closed.
 */
object Place {
    private const val KEY = "followPhone"
    private const val MOVE_KM = 5f
    private const val TAG = "Place"

    fun enabled(c: Context) = c.getSharedPreferences("clock", Context.MODE_PRIVATE).getBoolean(KEY, false)
    fun setEnabled(c: Context, on: Boolean) =
        c.getSharedPreferences("clock", Context.MODE_PRIVATE).edit().putBoolean(KEY, on).apply()
    fun allowed(c: Context) =
        ContextCompat.checkSelfPermission(c, Manifest.permission.ACCESS_COARSE_LOCATION) == PackageManager.PERMISSION_GRANTED
    fun active(c: Context) = enabled(c) && allowed(c)

    /** For SyncSession: the "loc ..." command when the phone is somewhere else than the watch's city. */
    fun here(c: Context): suspend (Double, Double) -> String? = { lat, lon -> locCommand(c, lat, lon) }

    suspend fun locCommand(c: Context, wlat: Double, wlon: Double): String? {
        if (!active(c)) return null
        val loc = fix(c) ?: return null.also { Log.i(TAG, "no location") }
        val d = FloatArray(1)
        Location.distanceBetween(wlat, wlon, loc.latitude, loc.longitude, d)
        if (d[0] < MOVE_KM * 1000) return null
        val name = name(c, loc.latitude, loc.longitude)
        Log.i(TAG, "moved ${(d[0] / 1000).toInt()} km: $name")
        return Protocol.locCommand(loc.latitude, loc.longitude, name)
    }

    /** A fresh network fix (up to 15 s), or else the newest one Android already has. */
    private suspend fun fix(c: Context): Location? = withContext(Dispatchers.IO) {
        val lm = c.getSystemService(LocationManager::class.java) ?: return@withContext null
        val fresh = if (Build.VERSION.SDK_INT >= 30 && runCatching { lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER) }.getOrDefault(false))
            withTimeoutOrNull(15_000) {
                suspendCancellableCoroutine<Location?> { cont ->
                    val cancel = CancellationSignal()
                    cont.invokeOnCancellation { cancel.cancel() }
                    runCatching {
                        lm.getCurrentLocation(LocationManager.NETWORK_PROVIDER, cancel, c.mainExecutor) { if (cont.isActive) cont.resume(it) }
                    }.onFailure { if (cont.isActive) cont.resume(null) }
                }
            }
        else null
        fresh ?: listOf(LocationManager.NETWORK_PROVIDER, LocationManager.PASSIVE_PROVIDER, LocationManager.GPS_PROVIDER)
            .mapNotNull { runCatching { lm.getLastKnownLocation(it) }.getOrNull() }
            .maxByOrNull { it.time }
    }

    /** The town's name in the watch's font (ASCII, 31 characters at most). */
    @Suppress("DEPRECATION")
    private suspend fun name(c: Context, lat: Double, lon: Double): String = withContext(Dispatchers.IO) {
        val a = runCatching { Geocoder(c, Locale.forLanguageTag("es")).getFromLocation(lat, lon, 1)?.firstOrNull() }.getOrNull()
        val n = a?.locality ?: a?.subAdminArea ?: a?.adminArea ?: "Mi ubicacion"
        Normalizer.normalize(n, Normalizer.Form.NFD).replace(Regex("\\p{M}"), "")
            .replace(Regex("[^\\x20-\\x7e]"), "").trim().take(31).ifEmpty { "Mi ubicacion" }
    }
}
