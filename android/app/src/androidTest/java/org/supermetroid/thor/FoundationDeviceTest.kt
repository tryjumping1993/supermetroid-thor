package org.supermetroid.thor

import android.content.Intent
import android.graphics.SurfaceTexture
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyEvent
import android.view.Surface
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Device contracts use real Android windows/events. Rejection is explicitly injected;
 * virtual-display removal exercises the OS callback, not physical panel unplugging. */
@RunWith(AndroidJUnit4::class)
class FoundationDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private lateinit var activity: MainActivity
    private var savedPreference: String? = null
    private var savedHz = 120
    private var savedJump = 0
    private var savedPause = 0
    private var virtual: VirtualDisplay? = null
    private var surface: Surface? = null
    private var texture: SurfaceTexture? = null
    private fun <T> onMain(block: () -> T): T {
        var value: T? = null; var failure: Throwable? = null
        instrumentation.runOnMainSync { try { value = block() } catch (e: Throwable) { failure = e } }
        failure?.let { throw it }
        @Suppress("UNCHECKED_CAST") return value as T
    }
    private fun await(description: String, predicate: () -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + 8000
        while (SystemClock.uptimeMillis() < deadline) {
            if (onMain(predicate)) return
            SystemClock.sleep(50)
        }
        fail("Timed out: $description")
    }
    @Before fun start() {
        activity = instrumentation.startActivitySync(Intent(instrumentation.targetContext, MainActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
        onMain {
            savedPreference = activity.getSharedPreferences("thor", 0).getString("helperDisplay", null)
            savedHz = SessionHost.renderHz; savedJump = activity.controls.jump; savedPause = activity.controls.pause
        }
        await("companion visible") { activity.companionRouter.mode == CompanionRouter.Mode.PRESENTATION }
    }
    @After fun restore() {
        onMain {
            virtual?.release(); virtual = null; surface?.release(); texture?.release()
            activity.rejectPresentationForTest = false; activity.denyActivityForTest = false
            activity.getSharedPreferences("thor", 0).edit().putString("helperDisplay", savedPreference).apply()
            activity.controls.remap(true, savedJump); activity.controls.remap(false, savedPause)
            SessionHost.renderHz = savedHz; SessionHost.saveOptions(activity)
            activity.companionRouter.stop(); activity.refreshDisplays()
        }
    }
    @Test fun rejectedPresentationLaunchesOneActivityAndCanRecover() {
        onMain {
            activity.rejectPresentationForTest = true
            activity.companionRouter.stop(); activity.refreshDisplays()
        }
        await("fallback activity attached") { activity.hasFallbackActivity }
        val generation = onMain { activity.companionRouter.generation }
        onMain { repeat(20) { activity.refreshDisplays() } }
        assertEquals(generation, onMain { activity.companionRouter.generation })
        assertEquals(CompanionRouter.Mode.ACTIVITY, onMain { activity.companionRouter.mode })
        onMain { activity.rejectPresentationForTest = false; activity.companionRouter.stop(); activity.refreshDisplays() }
        await("presentation recovered") { !activity.hasFallbackActivity && activity.companionRouter.mode == CompanionRouter.Mode.PRESENTATION }
    }
    @Test fun deniedActivityLaunchUsesInlineCompanion() {
        onMain {
            activity.rejectPresentationForTest = true; activity.denyActivityForTest = true
            activity.companionRouter.stop(); activity.refreshDisplays()
        }
        assertEquals(CompanionRouter.Mode.INLINE, onMain { activity.companionRouter.mode })
        assertFalse(onMain { activity.hasFallbackActivity })
    }
    @Test fun nonPresentationDisplayIsHandledAndRemoved() {
        onMain {
            texture = SurfaceTexture(false).apply { setDefaultBufferSize(800, 600) }
            surface = Surface(texture)
            virtual = activity.getSystemService(DisplayManager::class.java).createVirtualDisplay(
                "Thor activity-only test", 800, 600, 160, surface,
                DisplayManager.VIRTUAL_DISPLAY_FLAG_OWN_CONTENT_ONLY)
            checkNotNull(virtual)
            assertEquals(0, virtual!!.display.flags and android.view.Display.FLAG_PRESENTATION)
            activity.getSharedPreferences("thor", 0).edit()
                .putString("helperDisplay", MainActivity.displayKey(virtual!!.display)).apply()
            activity.refreshDisplays()
        }
        // This Thor firmware accepts Presentation even without FLAG_PRESENTATION.
        // A stricter OS may choose the activity path. Both must own the selected display.
        await("companion owns non-presentation display") {
            activity.companionRouter.target?.id == virtual!!.display.displayId &&
                (activity.companionRouter.mode == CompanionRouter.Mode.PRESENTATION || activity.hasFallbackActivity)
        }
        onMain { virtual!!.release(); virtual = null }
        await("physical companion recovered") { activity.companionRouter.mode == CompanionRouter.Mode.PRESENTATION }
    }
    @Test fun actualDisplayRemovalAndImmediateReassignmentPreserveSession() {
        await("cached ROM decoded") { SessionHost.loaded && !SessionHost.busy }
        val initiallyPaused = SessionHost.state().getBoolean("paused")
        if (!initiallyPaused) onMain { NativeBridge.pause() }
        val before = SessionHost.state()
        val original = onMain { activity.companionRouter.target!!.id }
        onMain {
            texture = SurfaceTexture(false).apply { setDefaultBufferSize(800, 600) }
            surface = Surface(texture)
            virtual = activity.getSystemService(DisplayManager::class.java).createVirtualDisplay(
                "Thor foundation test", 800, 600, 160, surface,
                DisplayManager.VIRTUAL_DISPLAY_FLAG_PUBLIC or DisplayManager.VIRTUAL_DISPLAY_FLAG_PRESENTATION or
                    DisplayManager.VIRTUAL_DISPLAY_FLAG_OWN_CONTENT_ONLY)
            checkNotNull(virtual)
            activity.getSharedPreferences("thor", 0).edit()
                .putString("helperDisplay", MainActivity.displayKey(virtual!!.display)).apply()
            activity.refreshDisplays()
        }
        await("companion reassigned") { activity.companionRouter.target?.id == virtual!!.display.displayId }
        onMain { virtual!!.release(); virtual = null }
        await("removed display recovered") { activity.companionRouter.target?.id == original }
        val after = SessionHost.state()
        assertEquals(before.getLong("tick"), after.getLong("tick"))
        assertEquals(before.getInt("roomIndex"), after.getInt("roomIndex"))
        assertEquals(before.getDouble("x"), after.getDouble("x"), 0.0)
        assertTrue(after.getBoolean("paused"))
        if (!initiallyPaused) onMain { NativeBridge.pause() }
    }
    private fun event(code: Int, action: Int = KeyEvent.ACTION_DOWN, repeat: Int = 0) = KeyEvent(
        0, SystemClock.uptimeMillis(), action, code, repeat, 0, 91, 0, 0, InputDevice.SOURCE_GAMEPAD)
    @Test fun remappedEventsReachMainAndPauseRepeatsAreIgnored() {
        await("cached ROM decoded") { SessionHost.loaded && !SessionHost.busy }
        onMain {
            activity.controls.remap(true, KeyEvent.KEYCODE_BUTTON_A)
            activity.controls.remap(false, KeyEvent.KEYCODE_BUTTON_Y)
            assertTrue(activity.dispatchKeyEvent(event(KeyEvent.KEYCODE_BUTTON_A)))
            assertEquals(0x8000, activity.controls.state.buttons)
            activity.dispatchKeyEvent(event(KeyEvent.KEYCODE_BUTTON_A, KeyEvent.ACTION_UP))
            assertEquals(0, activity.controls.state.buttons)
            val paused = SessionHost.state().getBoolean("paused")
            activity.dispatchKeyEvent(event(KeyEvent.KEYCODE_BUTTON_Y))
            assertEquals(!paused, SessionHost.state().getBoolean("paused"))
            activity.dispatchKeyEvent(event(KeyEvent.KEYCODE_BUTTON_Y, repeat = 1))
            activity.dispatchKeyEvent(event(KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.ACTION_UP))
            assertEquals(!paused, SessionHost.state().getBoolean("paused"))
            activity.dispatchKeyEvent(event(KeyEvent.KEYCODE_BUTTON_Y))
            assertEquals(paused, SessionHost.state().getBoolean("paused"))
            activity.dispatchKeyEvent(event(KeyEvent.KEYCODE_BUTTON_A))
            activity.onInputDeviceRemoved(91); assertEquals(0, activity.controls.state.buttons)
            activity.dispatchKeyEvent(event(KeyEvent.KEYCODE_BUTTON_A))
            activity.controls.remap(true, KeyEvent.KEYCODE_BUTTON_B)
            assertEquals(0, activity.controls.state.buttons)
            val reloaded = ControllerInput(activity)
            assertEquals(KeyEvent.KEYCODE_BUTTON_B, reloaded.jump)
            assertEquals(KeyEvent.KEYCODE_BUTTON_Y, reloaded.pause)
        }
    }
    @Test fun fallbackStopsWithMainAndResumesWithoutDuplicateTasks() {
        onMain { activity.rejectPresentationForTest = true; activity.companionRouter.stop(); activity.refreshDisplays() }
        await("fallback attached") { activity.hasFallbackActivity }
        val generation = onMain { activity.companionRouter.generation }
        instrumentation.uiAutomation.executeShellCommand("input -d ${onMain { activity.display!!.displayId }} keyevent KEYCODE_HOME").close()
        await("main stopped and fallback closed") { activity.companionRouter.mode == CompanionRouter.Mode.NONE && !activity.hasFallbackActivity }
        instrumentation.targetContext.startActivity(Intent(instrumentation.targetContext, MainActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_REORDER_TO_FRONT))
        await("fallback resumed") { activity.hasFallbackActivity }
        assertTrue(onMain { activity.companionRouter.generation } > generation)
    }
    @Test fun refreshRequestsSelectAdvertisedModeAtCurrentResolution() {
        onMain {
            for (hz in listOf(60, 120)) {
                SessionHost.renderHz = hz; SessionHost.saveOptions(activity)
                val selected = activity.display!!.supportedModes.first { it.modeId == activity.window.attributes.preferredDisplayModeId }
                assertEquals(hz.toFloat(), selected.refreshRate, .1f)
                assertEquals(activity.display!!.mode.physicalWidth, selected.physicalWidth)
                assertEquals(activity.display!!.mode.physicalHeight, selected.physicalHeight)
            }
        }
    }
}
