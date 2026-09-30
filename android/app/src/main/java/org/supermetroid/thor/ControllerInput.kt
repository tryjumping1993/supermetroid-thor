package org.supermetroid.thor

import android.content.Context
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent

class ControllerInput(context: Context) {
    val state = InputState()
    private val prefs = context.getSharedPreferences("thor", 0)
    var jump = prefs.getInt("jumpKey", KeyEvent.KEYCODE_BUTTON_B); private set
    var pause = prefs.getInt("pauseKey", KeyEvent.KEYCODE_BUTTON_START); private set
    var shoot = prefs.getInt("shootKey", KeyEvent.KEYCODE_BUTTON_Y).let { saved ->
        if (saved != jump && saved != pause && choices.any { it.second == saved }) saved
        else choices.first { it.second != jump && it.second != pause }.second
    }; private set
    fun remap(isJump: Boolean, code: Int) {
        remapAction(if (isJump) 0 else 1, code)
    }
    fun remapShoot(code: Int) { remapAction(2, code) }
    private fun remapAction(action: Int, code: Int) {
        require(code in choices.map { it.second })
        val bindings = intArrayOf(jump, pause, shoot)
        val conflict = bindings.indexOf(code)
        if (conflict >= 0) bindings[conflict] = bindings[action]
        bindings[action] = code
        jump = bindings[0]; pause = bindings[1]; shoot = bindings[2]
        state.clear()
        prefs.edit().putInt("jumpKey", jump).putInt("pauseKey", pause).putInt("shootKey", shoot).apply()
    }
    fun key(event: KeyEvent, togglePause: () -> Unit): Boolean {
        val controller = event.source and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD ||
            event.source and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK
        if (controller && event.keyCode == pause) {
            if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0 && !event.isCanceled) togglePause()
            return true
        }
        val bit = when {
            controller && event.keyCode == jump -> 0x8000
            controller && event.keyCode == shoot -> 0x40
            else -> when (event.keyCode) {
                KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> 0x200
                KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> 0x100
                KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> 0x800
                KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> 0x400
                KeyEvent.KEYCODE_SPACE -> 0x8000
                KeyEvent.KEYCODE_J -> 0x40
                KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_TAB -> 0x2000
                KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_K -> 0x4000
                KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_Q -> 0x20
                KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_E -> 0x10
                else -> 0
            }
        }
        if (bit == 0) return false
        if (event.action != KeyEvent.ACTION_MULTIPLE)
            state.key(event.deviceId, event.keyCode, bit, event.action == KeyEvent.ACTION_DOWN && !event.isCanceled)
        return true
    }
    fun motion(event: MotionEvent): Boolean {
        if (event.source and InputDevice.SOURCE_JOYSTICK != InputDevice.SOURCE_JOYSTICK) return false
        fun flat(axis: Int) = event.device?.getMotionRange(axis, event.source)?.flat ?: 0f
        state.axis(event.deviceId, event.getAxisValue(MotionEvent.AXIS_X), event.getAxisValue(MotionEvent.AXIS_Y),
            event.getAxisValue(MotionEvent.AXIS_HAT_X), event.getAxisValue(MotionEvent.AXIS_HAT_Y),
            flat(MotionEvent.AXIS_X), flat(MotionEvent.AXIS_Y))
        return true
    }
    companion object {
        val choices = listOf("A" to KeyEvent.KEYCODE_BUTTON_A, "B" to KeyEvent.KEYCODE_BUTTON_B,
            "X" to KeyEvent.KEYCODE_BUTTON_X, "Y" to KeyEvent.KEYCODE_BUTTON_Y,
            "L1" to KeyEvent.KEYCODE_BUTTON_L1, "R1" to KeyEvent.KEYCODE_BUTTON_R1,
            "Select" to KeyEvent.KEYCODE_BUTTON_SELECT, "Start" to KeyEvent.KEYCODE_BUTTON_START)
    }
}
