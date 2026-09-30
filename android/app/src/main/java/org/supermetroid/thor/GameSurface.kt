package org.supermetroid.thor

import android.content.Context
import android.opengl.GLSurfaceView
import android.util.Log
import android.view.Surface
import java.util.concurrent.atomic.AtomicLong
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class GameSurface(context: Context) : GLSurfaceView(context), GLSurfaceView.Renderer {
    val frames = AtomicLong()
    val longestDrawNs = AtomicLong()
    @Volatile private var requestedHz = 120
    fun requestRefresh(hz: Int) {
        if (requestedHz == hz) return
        requestedHz = hz
        if (holder.surface.isValid) holder.surface.setFrameRate(hz.toFloat(), Surface.FRAME_RATE_COMPATIBILITY_DEFAULT)
    }
    init {
        setEGLContextClientVersion(3)
        setEGLConfigChooser(8, 8, 8, 8, 0, 0)
        preserveEGLContextOnPause = true
        setRenderer(this)
        renderMode = RENDERMODE_WHEN_DIRTY
    }
    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        NativeBridge.glInit()
        // Preference only; actual cadence is measured in the diagnostics.
        holder.surface.setFrameRate(requestedHz.toFloat(), Surface.FRAME_RATE_COMPATIBILITY_DEFAULT)
        Log.i("ThorNative", "GLES surface created; requested $requestedHz Hz")
    }
    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) { NativeBridge.glResize(width, height) }
    override fun onDrawFrame(gl: GL10?) {
        val start = System.nanoTime()
        NativeBridge.glDraw()
        longestDrawNs.updateAndGet { maxOf(it, System.nanoTime() - start) }
        frames.incrementAndGet()
    }
}
