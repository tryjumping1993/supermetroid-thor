package org.supermetroid.thor

import android.app.Activity
import android.content.Context
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.util.AtomicFile
import android.widget.Toast
import org.json.JSONObject
import java.io.File
import java.security.MessageDigest
import java.util.concurrent.Executors

/** One process-wide session shared by every display. Native state uses a mutex;
 * simulation advances only through MainActivity's Choreographer callback. */
object SessionHost {
    private val worker = Executors.newSingleThreadExecutor()
    private val main = Handler(Looper.getMainLooper())
    @Volatile var loaded = false
    @Volatile var busy = false
    @Volatile var loadingMessage = "Import your original NTSC ROM to begin."
    var wide = true
    var interpolate = true
    var spoilers = false
    var buttons = 0
    var resumeRequested = false
    var mainActivity: MainActivity? = null

    fun initialize(context: Context) {
        val prefs = context.getSharedPreferences("thor", 0)
        wide = prefs.getBoolean("wide", true)
        interpolate = prefs.getBoolean("interpolate", true)
        spoilers = prefs.getBoolean("spoilers", false)
        NativeBridge.options(wide, interpolate)
        if (!loaded && !busy) {
            val cached = File(context.filesDir, "content/ntsc.sfc")
            if (cached.exists()) load(context) { cached.readBytes() }
        }
    }

    fun saveOptions(context: Context) {
        NativeBridge.options(wide, interpolate)
        context.getSharedPreferences("thor", 0).edit().putBoolean("wide", wide)
            .putBoolean("interpolate", interpolate).putBoolean("spoilers", spoilers).apply()
    }

    fun importRom(context: Context, uri: Uri) = load(context) {
        context.contentResolver.openInputStream(uri)?.use { it.readNBytes(0x300201) }
            ?: error("Cannot open ROM")
    }

    // Debug-only ADB provisioning bypasses SAF for repeatable hardware tests.
    fun debugImport(context: Context, path: String) {
        check(BuildConfig.DEBUG)
        val file = File(path).canonicalFile
        require(file.toPath().startsWith(context.filesDir.canonicalFile.toPath()))
        load(context) { file.readBytes() }
    }

    private fun load(context: Context, read: () -> ByteArray) {
        if (busy) return
        busy = true; loadingMessage = "Validating and decoding your ROM…"
        worker.execute {
            try {
                var bytes = read()
                if (bytes.size == 0x300200) bytes = bytes.copyOfRange(512, bytes.size)
                require(bytes.size == 0x300000) { "Expected the original 3 MB NTSC ROM." }
                val hash = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it.toInt() and 255) }
                require(hash == "12b77c4bc9c1832cee8881244659065ee1d84c70c3d29e6eaf92e6798cc2ca72") {
                    "This is not the supported original NTSC Super Metroid ROM."
                }
                val destination = File(context.filesDir, "content/ntsc.sfc")
                destination.parentFile!!.mkdirs()
                atomicWrite(destination, bytes)
                NativeBridge.loadRom(bytes)
                val saved = File(context.filesDir, "saves/imported.srm")
                if (saved.exists()) runCatching { NativeBridge.importSram(saved.readBytes()) }
                loaded = true; loadingMessage = "Native movement slice ready."
                main.post { mainActivity?.romLoaded() }
            } catch (e: Exception) {
                loadingMessage = e.message ?: "ROM import failed"
                main.post { Toast.makeText(context, loadingMessage, Toast.LENGTH_LONG).show() }
            } finally { busy = false }
        }
    }

    fun selectRoom(context: Context, index: Int) {
        if (busy || !loaded) return
        busy = true
        worker.execute {
            try { NativeBridge.selectRoom(index) }
            catch (e: Exception) { main.post { Toast.makeText(context, e.message, Toast.LENGTH_LONG).show() } }
            finally { busy = false }
        }
    }

    fun importSram(context: Context, uri: Uri) {
        worker.execute {
            try {
                val bytes = context.contentResolver.openInputStream(uri)?.use { it.readNBytes(8193) } ?: error("Cannot read SRAM")
                NativeBridge.importSram(bytes)
                val file = File(context.filesDir, "saves/imported.srm"); file.parentFile!!.mkdirs()
                atomicWrite(file, bytes)
                main.post { Toast.makeText(context, "SRAM imported for inspection. Gameplay restoration is pending.", Toast.LENGTH_LONG).show() }
            } catch (e: Exception) { main.post { Toast.makeText(context, e.message, Toast.LENGTH_LONG).show() } }
        }
    }

    fun traverseDoor(context: Context, expectedRoom: Int, index: Int) {
        if (busy || !loaded) return
        busy = true; buttons = 0
        worker.execute {
            try { NativeBridge.traverseDoor(expectedRoom, index) }
            catch (e: Exception) { main.post { Toast.makeText(context, e.message, Toast.LENGTH_LONG).show() } }
            finally { busy = false }
        }
    }

    fun exportSram(context: Context, uri: Uri) {
        worker.execute {
            try {
                val data = NativeBridge.exportSram()
                require(data.size == 8192) { "Import an SRAM file first." }
                context.contentResolver.openOutputStream(uri, "wt")?.use { it.write(data) } ?: error("Cannot write SRAM")
            } catch (e: Exception) { main.post { Toast.makeText(context, e.message, Toast.LENGTH_LONG).show() } }
        }
    }

    private fun atomicWrite(file: File, bytes: ByteArray) {
        val atomic = AtomicFile(file)
        val stream = atomic.startWrite()
        try { stream.write(bytes); atomic.finishWrite(stream) }
        catch (e: Exception) { atomic.failWrite(stream); throw e }
    }

    fun state(): JSONObject = JSONObject(NativeBridge.companion())
}
