package org.supermetroid.thor

/** Held state is tracked per physical key and device, so one release cannot clear another. */
class InputState {
    private val keys = mutableMapOf<Pair<Int, Int>, Int>()
    private val axes = mutableMapOf<Int, Int>()
    val buttons: Int get() = (keys.values + axes.values).fold(0) { bits, value -> bits or value }
    fun key(device: Int, code: Int, bit: Int, down: Boolean) {
        if (down) keys[device to code] = bit else keys.remove(device to code)
    }
    fun axis(device: Int, x: Float, y: Float, hatX: Float, hatY: Float, flatX: Float, flatY: Float) {
        fun direction(stick: Float, hat: Float, flat: Float, negative: Int, positive: Int): Int {
            // Hat takes precedence; summing it with an opposing stick can cancel a D-pad press.
            val value = if (kotlin.math.abs(hat) > .25f) hat else stick
            val dead = maxOf(.25f, flat)
            return if (value < -dead) negative else if (value > dead) positive else 0
        }
        axes[device] = direction(x, hatX, flatX, 0x200, 0x100) or direction(y, hatY, flatY, 0x800, 0x400)
    }
    fun remove(device: Int) { keys.keys.removeAll { it.first == device }; axes.remove(device) }
    fun clear() { keys.clear(); axes.clear() }
}
