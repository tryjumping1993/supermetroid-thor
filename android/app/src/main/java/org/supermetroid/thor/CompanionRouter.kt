package org.supermetroid.thor

/** Main-thread ownership of one companion window, including pending activity launches. */
class CompanionRouter(private val host: Host) {
    data class Target(val id: Int, val key: String)
    enum class Mode { NONE, PRESENTATION, ACTIVITY, INLINE }
    interface Host {
        fun close()
        fun presentation(target: Target): Boolean
        fun activity(target: Target, generation: Long): Boolean
        fun inline()
    }
    var target: Target? = null; private set
    var mode = Mode.NONE; private set
    var generation = 0L; private set

    fun reconcile(running: Boolean, primary: Int?, displays: List<Target>, preferred: String?) {
        val candidates = displays.filter { it.id != primary }
        val next = candidates.firstOrNull { it.key == preferred } ?: candidates.minByOrNull { it.id }
        if (!running) { stop(); return }
        if (next == null && target == null && mode == Mode.NONE) return
        if (next == target && mode != Mode.NONE) return
        stop()
        target = next
        if (next == null) return
        mode = when {
            host.presentation(next) -> Mode.PRESENTATION
            host.activity(next, generation) -> Mode.ACTIVITY
            else -> { host.inline(); Mode.INLINE }
        }
    }

    fun acceptsActivity(id: Int, token: Long) = mode == Mode.ACTIVITY && target?.id == id && generation == token
    fun activityUnavailable(token: Long) {
        if (mode != Mode.ACTIVITY || generation != token) return
        generation++; mode = Mode.INLINE
        host.close(); host.inline()
    }
    fun stop() {
        // Invalidate before closing: dismissal/destruction callbacks may be synchronous.
        generation++; target = null; mode = Mode.NONE; host.close()
    }
}
