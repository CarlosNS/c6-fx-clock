package io.github.carlosns.c6clock

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.net.Uri
import android.os.Build
import android.provider.Settings
import android.util.Log
import android.widget.Toast
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File

/**
 * The app updates itself from the same manifest as the firmware (its "app" block, written by
 * tools/release.py): downloads the APK, checks size and SHA-256 and hands it to the system installer,
 * which asks the user to confirm. Android only accepts it if it is signed with the same key.
 */
object AppUpdate {
    private const val TAG = "AppUpdate"

    fun installedCode(c: Context): Long = c.packageManager.getPackageInfo(c.packageName, 0).longVersionCode
    fun installedName(c: Context): String = c.packageManager.getPackageInfo(c.packageName, 0).versionName ?: ""

    /** The newer app on offer, or null (none, or no internet). */
    suspend fun check(c: Context): JSONObject? = withContext(Dispatchers.IO) {
        runCatching { Firmware.newerApp(Net.getJson(Firmware.manifestUrl), installedCode(c)) }
            .onFailure { Log.w(TAG, "check: ${it.message}") }.getOrNull()
    }

    /** Android asks once per app before it may install others ("Instalar apps desconocidas"). */
    fun mayInstall(c: Context) = c.packageManager.canRequestPackageInstalls()

    fun allowInstallIntent(c: Context) =
        Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:${c.packageName}"))

    /** Downloads and checks [app], then starts the system install; [progress] gets texts for the page. */
    suspend fun install(c: Context, app: JSONObject, progress: (String) -> Unit) {
        val size = app.getLong("size")
        val file = File(c.cacheDir, "update.apk")
        downloadChecked(app.getString("url"), size, app.getString("sha256"), file) { progress("Descargando… $it %") }
        progress("Instalando…")
        withContext(Dispatchers.IO) {
            val installer = c.packageManager.packageInstaller
            val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL)
            params.setAppPackageName(c.packageName)
            val id = installer.createSession(params)
            installer.openSession(id).use { s ->
                s.openWrite("app.apk", 0, size).use { out ->
                    file.inputStream().use { it.copyTo(out) }
                    s.fsync(out)
                }
                // The installer fills in the result, so the PendingIntent must be mutable (explicit target)
                val flags = PendingIntent.FLAG_UPDATE_CURRENT or (if (Build.VERSION.SDK_INT >= 31) PendingIntent.FLAG_MUTABLE else 0)
                val pi = PendingIntent.getBroadcast(c, id, Intent(c, InstallResult::class.java), flags)
                s.commit(pi.intentSender)
            }
        }
        file.delete()
    }
}

/** The system installer's answer: show its confirmation screen, or say why it failed. */
class InstallResult : BroadcastReceiver() {
    override fun onReceive(c: Context, i: Intent) {
        when (val status = i.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)) {
            PackageInstaller.STATUS_PENDING_USER_ACTION -> {
                @Suppress("DEPRECATION")
                val confirm = i.getParcelableExtra<Intent>(Intent.EXTRA_INTENT) ?: return
                c.startActivity(confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            }
            PackageInstaller.STATUS_SUCCESS -> Log.i("AppUpdate", "installed")  // the new version restarts the app
            else -> {
                val msg = i.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE) ?: "error $status"
                Log.w("AppUpdate", "install failed: $status $msg")
                if (status != PackageInstaller.STATUS_FAILURE_ABORTED)  // "Cancelar" in the dialog needs no toast
                    Toast.makeText(c, "No se pudo actualizar la app: $msg", Toast.LENGTH_LONG).show()
            }
        }
    }
}
