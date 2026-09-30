package org.supermetroid.thor

import android.app.Activity
import android.app.Presentation
import android.app.ActivityOptions
import android.content.ActivityNotFoundException
import android.hardware.input.InputManager
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

class MainActivity : Activity(), Choreographer.FrameCallback, DisplayManager.DisplayListener, InputManager.InputDeviceListener {
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
    private var callbackCount = 0L
    lateinit var controls: ControllerInput; private set
    private lateinit var inputs: InputManager
    private var fallbackActivity: CompanionActivity? = null
    internal val hasFallbackActivity get() = fallbackActivity != null
    private var automaticInline = false
    internal var rejectPresentationForTest = false
    internal var denyActivityForTest = false
    internal val companionRouter: CompanionRouter = CompanionRouter(object : CompanionRouter.Host {
        override fun close() {
            presentation?.setOnDismissListener(null)
            presentation?.dismiss(); presentation = null
            fallbackActivity?.finish(); fallbackActivity = null
            if (automaticInline) { inlineHelper?.let { root.removeView(it) }; inlineHelper = null; automaticInline = false }
            clearInput()
        }
        override fun presentation(target: CompanionRouter.Target): Boolean {
            val screen = displays.getDisplay(target.id) ?: return false
            try {
                if (BuildConfig.DEBUG && rejectPresentationForTest) throw WindowManager.InvalidDisplayException("Test rejection")
                val window = object : Presentation(this@MainActivity, screen) {
                    override fun onCreate(state: Bundle?) {
                        super.onCreate(state)
                        this.window?.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                        this.window?.let { immerse(it); RefreshPolicy.request(it, display, 60) }
                        setContentView(CompanionView(context))
                        Log.i("ThorNative", "companion mode=presentation display=${display.displayId}")
                    }
                }
                window.show(); presentation = window
                window.setOnDismissListener {
                    handler.post {
                        if (presentation === window) { companionRouter.stop(); refreshDisplays() }
                    }
                }
                return true
            } catch (e: WindowManager.InvalidDisplayException) {
                Log.w("ThorNative", "Presentation rejected on ${target.id}; trying activity", e)
            } catch (e: SecurityException) {
                Log.w("ThorNative", "Presentation permission denied on ${target.id}; trying activity", e)
            }
            return false
        }
        override fun activity(target: CompanionRouter.Target, generation: Long): Boolean {
            val intent = Intent(this@MainActivity, CompanionActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_MULTIPLE_TASK)
                .putExtra("companionGeneration", generation)
            try {
                val manager = getSystemService(android.app.ActivityManager::class.java)
                if ((BuildConfig.DEBUG && denyActivityForTest) || !manager.isActivityStartAllowedOnDisplay(this@MainActivity, target.id, intent)) return false
                startActivity(intent, ActivityOptions.makeBasic().setLaunchDisplayId(target.id).toBundle())
                Log.i("ThorNative", "companion mode=activity display=${target.id} generation=$generation")
                return true
            } catch (e: SecurityException) {
                Log.w("ThorNative", "Companion launch denied", e)
            } catch (e: ActivityNotFoundException) {
                Log.w("ThorNative", "Companion activity unavailable", e)
            } catch (e: IllegalArgumentException) {
                Log.w("ThorNative", "Companion display disappeared during launch", e)
            }
            return false
        }
        override fun inline() {
            if (inlineHelper == null) showInlineHelper()
            automaticInline = true
            Log.i("ThorNative", "companion mode=inline; secondary windows unavailable")
        }
    })

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
        controls = ControllerInput(this)
        inputs = getSystemService(InputManager::class.java)
        inputs.registerInputDeviceListener(this, handler)
        SessionHost.initialize(applicationContext)
        if (BuildConfig.DEBUG) intent.getIntExtra("development_hz", 0).takeIf { it == 60 || it == 120 }?.let {
            SessionHost.renderHz = it // Transient test request; leaves the saved user preference intact.
        }
        if (SessionHost.loaded) romLoaded()
        if (BuildConfig.DEBUG) intent.getStringExtra("development_rom")?.let {
            SessionHost.debugImport(applicationContext, it)
        }
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
        NativeBridge.suspend(); lastReport = 0; callbackCount = 0
        if (!callbackInstalled) { callbackInstalled = true; Choreographer.getInstance().postFrameCallback(this) }
        if (this::displays.isInitialized) refreshDisplays()
    }
    override fun onResume() {
        super.onResume(); NativeBridge.suspend()
        if (this::displays.isInitialized) refreshDisplays()
    }
    override fun onPause() {
        // On Android multi-display, a visible activity may be paused while the
        // companion receives focus. Keep its surface alive until onStop.
        clearInput()
        NativeBridge.suspend()
        super.onPause()
    }
    override fun onStop() {
        running = false; callbackInstalled = false
        Choreographer.getInstance().removeFrameCallback(this)
        clearInput()
        NativeBridge.suspend(); game.onPause()
        companionRouter.stop()
        super.onStop()
    }
    override fun onDestroy() {
        displays.unregisterDisplayListener(this)
        inputs.unregisterInputDeviceListener(this)
        companionRouter.stop()
        if (SessionHost.mainActivity === this) SessionHost.mainActivity = null
        super.onDestroy()
    }
    override fun doFrame(frameTimeNanos: Long) {
        callbackInstalled = false
        if (!running) return
        if (SessionHost.resumeRequested) { NativeBridge.suspend(); SessionHost.resumeRequested = false }
        if (!SessionHost.busy) NativeBridge.advance(frameTimeNanos, controls.state.buttons or SessionHost.buttons)
        else NativeBridge.suspend()
        game.requestRender()
        if (lastReport == 0L) { lastReport = frameTimeNanos; lastFrames = game.frames.get() }
        else callbackCount++
        if (frameTimeNanos - lastReport >= 1_000_000_000L) {
            val count = game.frames.get()
            val fps = (count - lastFrames) * 1_000_000_000.0 / (frameTimeNanos - lastReport)
            val state = SessionHost.state()
            status.text = if (SessionHost.loaded) "${state.optString("room").replace('_', ' ')} · %.1f render fps · tick %d\nDevelopment slice · collision / room scripts incomplete".format(fps, state.optLong("tick"))
            else SessionHost.loadingMessage
            val callbackHz = callbackCount * 1_000_000_000.0 / (frameTimeNanos - lastReport)
            Log.i("ThorNative", "framesPerSecond=$fps callbackHz=$callbackHz requestedHz=${SessionHost.renderHz} tick=${state.optLong("tick")} displayHz=${display?.refreshRate} longestDrawMs=${game.longestDrawNs.getAndSet(0) / 1e6} x=${state.optDouble("x")} y=${state.optDouble("y")} paused=${state.optBoolean("paused")}")
            lastReport = frameTimeNanos; lastFrames = count; callbackCount = 0
        }
        callbackInstalled = true; Choreographer.getInstance().postFrameCallback(this)
    }

    private fun showInlineHelper() {
        inlineHelper?.let { root.removeView(it); inlineHelper = null; return }
        inlineHelper = CompanionView(this).also { root.addView(it, FrameLayout.LayoutParams((resources.displayMetrics.widthPixels * .55f).toInt(), -1, Gravity.END)) }
    }
    fun refreshDisplays() {
        if (!running) return
        RefreshPolicy.request(window, display, SessionHost.renderHz)
        game.requestRefresh(SessionHost.renderHz)
        presentation?.window?.let { RefreshPolicy.request(it, presentation?.display, 60) }
        fallbackActivity?.let { RefreshPolicy.request(it.window, it.display, 60) }
        val preferred = getSharedPreferences("thor", 0).getString("helperDisplay", null)
        companionRouter.reconcile(true, display?.displayId,
            displays.displays.filter { it.isValid }.map { CompanionRouter.Target(it.displayId, displayKey(it)) }, preferred)
    }
    internal fun attachCompanion(activity: CompanionActivity, generation: Long): Boolean {
        if (!running || !companionRouter.acceptsActivity(activity.display?.displayId ?: -1, generation)) return false
        fallbackActivity?.takeIf { it !== activity }?.finish()
        fallbackActivity = activity
        return true
    }
    internal fun detachCompanion(activity: CompanionActivity) {
        if (fallbackActivity === activity) {
            fallbackActivity = null
            companionRouter.activityUnavailable(companionRouter.generation)
        }
    }
    internal fun companionUnavailable(generation: Long) { companionRouter.activityUnavailable(generation) }
    private fun clearInput() {
        if (this::controls.isInitialized) controls.state.clear()
        SessionHost.buttons = 0
    }
    override fun onDisplayAdded(id: Int) { refreshDisplays() }
    override fun onDisplayRemoved(id: Int) { refreshDisplays() }
    override fun onDisplayChanged(id: Int) { refreshDisplays() }
    override fun onInputDeviceAdded(id: Int) { controls.state.remove(id) }
    override fun onInputDeviceRemoved(id: Int) { controls.state.remove(id) }
    override fun onInputDeviceChanged(id: Int) { controls.state.remove(id) }
    override fun dispatchKeyEvent(event: KeyEvent): Boolean =
        controls.key(event) { NativeBridge.pause() } || super.dispatchKeyEvent(event)
    override fun onGenericMotionEvent(event: MotionEvent): Boolean =
        controls.motion(event) || super.onGenericMotionEvent(event)
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
