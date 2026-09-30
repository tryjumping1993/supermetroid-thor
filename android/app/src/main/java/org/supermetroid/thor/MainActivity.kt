package org.supermetroid.thor

import android.app.Activity
import android.app.Presentation
import android.app.ActivityOptions
import android.content.Intent
import android.graphics.Color
import android.hardware.display.DisplayManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.Choreographer
import android.view.Display
import android.view.Gravity
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.view.Window
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView

class MainActivity : Activity(), Choreographer.FrameCallback, DisplayManager.DisplayListener {
    private lateinit var game: GameSurface
    private lateinit var root: FrameLayout
    private lateinit var welcome: LinearLayout
    private lateinit var status: TextView
    private lateinit var displays: DisplayManager
    private var presentation: Presentation? = null
    private var inlineHelper: CompanionView? = null
    private var running = false
    private var callbackInstalled = false
    private val handler = Handler(Looper.getMainLooper())
    private var lastReport = 0L
    private var lastFrames = 0L
    private var keyboardButtons = 0
    private var axisButtons = 0

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        SessionHost.mainActivity = this
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        immerse(window)
        root = FrameLayout(this)
        game = GameSurface(this); root.addView(game)
        status = TextView(this).apply {
            setTextColor(Color.WHITE); textSize = 12f; setPadding(12, 8, 12, 8)
            setBackgroundColor(0xB0000000.toInt())
        }
        root.addView(status, FrameLayout.LayoutParams(-2, -2, Gravity.TOP or Gravity.START))
        val helper = Button(this).apply { text = "Companion"; setOnClickListener { showInlineHelper() } }
        root.addView(helper, FrameLayout.LayoutParams(-2, -2, Gravity.TOP or Gravity.END))
        welcome = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER
            setBackgroundColor(0xF0101928.toInt()); setPadding(36, 24, 36, 24)
            addView(TextView(this@MainActivity).apply {
                text = "SUPER METROID · THOR\nNative port development build\n\nOriginal room artwork + provisional movement.\nEnemies, progression, scripts and audio are pending."
                textSize = 22f; setTextColor(Color.WHITE); gravity = Gravity.CENTER
            })
            addView(Button(this@MainActivity).apply { text = "Import NTSC ROM"; setOnClickListener { importRom() } })
        }
        root.addView(welcome, FrameLayout.LayoutParams(-1, -1))
        setContentView(root)
        displays = getSystemService(DisplayManager::class.java)
        displays.registerDisplayListener(this, handler)
        SessionHost.initialize(applicationContext)
        if (SessionHost.loaded) romLoaded()
        if (BuildConfig.DEBUG) intent.getStringExtra("development_rom")?.let {
            SessionHost.debugImport(applicationContext, it)
        }
        showCompanion()
    }

    fun romLoaded() { welcome.visibility = View.GONE; game.requestRender() }
    fun importRom() { startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE), 1) }
    fun importSram() { startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE), 2) }
    fun exportSram() {
        startActivityForResult(Intent(Intent.ACTION_CREATE_DOCUMENT).setType("application/octet-stream")
            .putExtra(Intent.EXTRA_TITLE, "Super Metroid.srm").addCategory(Intent.CATEGORY_OPENABLE), 3)
    }
    @Deprecated("Platform activity result API retained to avoid additional framework dependencies")
    override fun onActivityResult(request: Int, result: Int, data: Intent?) {
        super.onActivityResult(request, result, data)
        if (result != RESULT_OK) return
        data?.data?.let { uri -> when (request) {
            1 -> SessionHost.importRom(applicationContext, uri)
            2 -> SessionHost.importSram(applicationContext, uri)
            3 -> SessionHost.exportSram(applicationContext, uri)
        } }
    }

    override fun onStart() {
        super.onStart(); running = true; game.onResume()
        NativeBridge.suspend(); lastReport = 0
        if (!callbackInstalled) { callbackInstalled = true; Choreographer.getInstance().postFrameCallback(this) }
        if (this::displays.isInitialized) showCompanion()
    }
    override fun onResume() {
        super.onResume(); NativeBridge.suspend()
        if (this::displays.isInitialized) showCompanion()
    }
    override fun onPause() {
        // On Android multi-display, a visible activity may be paused while the
        // companion receives focus. Keep its surface alive until onStop.
        keyboardButtons = 0; axisButtons = 0; SessionHost.buttons = 0
        NativeBridge.suspend()
        super.onPause()
    }
    override fun onStop() {
        running = false; callbackInstalled = false
        Choreographer.getInstance().removeFrameCallback(this)
        keyboardButtons = 0; axisButtons = 0; SessionHost.buttons = 0
        NativeBridge.suspend(); game.onPause()
        presentation?.dismiss(); presentation = null
        super.onStop()
    }
    override fun onDestroy() {
        displays.unregisterDisplayListener(this)
        presentation?.dismiss()
        if (SessionHost.mainActivity === this) SessionHost.mainActivity = null
        super.onDestroy()
    }
    override fun doFrame(frameTimeNanos: Long) {
        callbackInstalled = false
        if (!running) return
        if (SessionHost.resumeRequested) { NativeBridge.suspend(); SessionHost.resumeRequested = false }
        if (!SessionHost.busy) NativeBridge.advance(frameTimeNanos, keyboardButtons or axisButtons or SessionHost.buttons)
        else NativeBridge.suspend()
        game.requestRender()
        if (lastReport == 0L) { lastReport = frameTimeNanos; lastFrames = game.frames.get() }
        if (frameTimeNanos - lastReport >= 1_000_000_000L) {
            val count = game.frames.get()
            val fps = (count - lastFrames) * 1_000_000_000.0 / (frameTimeNanos - lastReport)
            val state = SessionHost.state()
            status.text = if (SessionHost.loaded) "${state.optString("room").replace('_', ' ')} · %.1f render fps · tick %d\nDevelopment slice · collision / room scripts incomplete".format(fps, state.optLong("tick"))
            else SessionHost.loadingMessage
            Log.i("ThorNative", "framesPerSecond=$fps tick=${state.optLong("tick")} displayHz=${display?.refreshRate} longestDrawMs=${game.longestDrawNs.getAndSet(0) / 1e6} x=${state.optDouble("x")} y=${state.optDouble("y")} paused=${state.optBoolean("paused")}")
            lastReport = frameTimeNanos; lastFrames = count
        }
        callbackInstalled = true; Choreographer.getInstance().postFrameCallback(this)
    }

    private fun showInlineHelper() {
        inlineHelper?.let { root.removeView(it); inlineHelper = null; return }
        inlineHelper = CompanionView(this).also { root.addView(it, FrameLayout.LayoutParams((resources.displayMetrics.widthPixels * .55f).toInt(), -1, Gravity.END)) }
    }
    private fun showCompanion() {
        val candidates = displays.displays.filter { it.displayId != display?.displayId }
        val preferred = getSharedPreferences("thor", 0).getString("helperDisplay", null)
        val target = candidates.firstOrNull { displayKey(it) == preferred } ?: candidates.firstOrNull() ?: return
        if (presentation?.display?.displayId == target.displayId && presentation?.isShowing == true) return
        presentation?.dismiss()
        try {
            presentation = object : Presentation(this, target) {
                override fun onCreate(state: Bundle?) {
                    super.onCreate(state)
                    window?.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    window?.let { immerse(it) }
                    setContentView(CompanionView(context))
                    Log.i("ThorNative", "Companion presentation on display ${display.displayId} ${display.mode}")
                }
            }.also { it.show() }
        } catch (e: WindowManager.InvalidDisplayException) {
            Log.w("ThorNative", "Presentation unavailable; launching companion activity", e)
            presentation = null
            val intent = Intent(this, CompanionActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_MULTIPLE_TASK)
            val manager = getSystemService(android.app.ActivityManager::class.java)
            if (manager.isActivityStartAllowedOnDisplay(this, target.displayId, intent)) {
                startActivity(intent, ActivityOptions.makeBasic().setLaunchDisplayId(target.displayId).toBundle())
            }
        }
    }
    override fun onDisplayAdded(id: Int) { if (running) showCompanion() }
    override fun onDisplayRemoved(id: Int) {
        if (presentation?.display?.displayId == id) { presentation?.dismiss(); presentation = null }
        if (running) showCompanion()
    }
    override fun onDisplayChanged(id: Int) { if (running) showCompanion() }

    private fun button(key: Int): Int = when (key) {
        KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> 0x200
        KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> 0x100
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> 0x800
        KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> 0x400
        KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_SPACE -> 0x8000
        else -> 0
    }
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.keyCode == KeyEvent.KEYCODE_BUTTON_START && event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) { NativeBridge.pause(); return true }
        val bit = button(event.keyCode)
        if (bit != 0) {
            keyboardButtons = if (event.action == KeyEvent.ACTION_UP) keyboardButtons and bit.inv() else keyboardButtons or bit
            return true
        }
        return super.dispatchKeyEvent(event)
    }
    override fun onGenericMotionEvent(event: MotionEvent): Boolean {
        if ((event.source and android.view.InputDevice.SOURCE_JOYSTICK) == android.view.InputDevice.SOURCE_JOYSTICK) {
            val x = event.getAxisValue(MotionEvent.AXIS_X) + event.getAxisValue(MotionEvent.AXIS_HAT_X)
            val y = event.getAxisValue(MotionEvent.AXIS_Y) + event.getAxisValue(MotionEvent.AXIS_HAT_Y)
            axisButtons = (if (x < -.25f) 0x200 else if (x > .25f) 0x100 else 0) or
                (if (y < -.25f) 0x800 else if (y > .25f) 0x400 else 0)
            return true
        }
        return super.onGenericMotionEvent(event)
    }
    companion object {
        fun displayKey(display: Display) = "${display.name}/${display.mode.physicalWidth}x${display.mode.physicalHeight}"
        fun immerse(window: Window) {
            val decor = window.decorView // Initializes PhoneWindow's decor on Thor firmware.
            window.setDecorFitsSystemWindows(false)
            decor.post {
                decor.windowInsetsController?.let {
                    it.hide(WindowInsets.Type.systemBars())
                    it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                }
            }
        }
    }
}
