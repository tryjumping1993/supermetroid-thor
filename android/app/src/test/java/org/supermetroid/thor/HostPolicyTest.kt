package org.supermetroid.thor

import org.junit.Assert.*
import org.junit.Test

class HostPolicyTest {
    private class Host : CompanionRouter.Host {
        var allowPresentation = true; var allowActivity = true
        var presentations = 0; var activities = 0; var closes = 0; var inlines = 0
        override fun close() { closes++ }
        override fun presentation(target: CompanionRouter.Target): Boolean { presentations++; return allowPresentation }
        override fun activity(target: CompanionRouter.Target, generation: Long): Boolean { activities++; return allowActivity }
        override fun inline() { inlines++ }
    }
    private val main = CompanionRouter.Target(7, "main")
    private val bottom = CompanionRouter.Target(11, "bottom")
    @Test fun primaryExcludedAndDuplicateCallbacksDoNotLaunchAgain() {
        val host = Host(); val router = CompanionRouter(host)
        repeat(10) { router.reconcile(true, main.id, listOf(main, bottom), "main") }
        assertEquals(bottom, router.target); assertEquals(1, host.presentations)
        assertEquals(CompanionRouter.Mode.PRESENTATION, router.mode)
    }
    @Test fun rejectedPresentationHasOnePendingActivityAndStaleLaunchCannotAttach() {
        val host = Host().apply { allowPresentation = false }; val router = CompanionRouter(host)
        repeat(10) { router.reconcile(true, main.id, listOf(main, bottom), null) }
        val token = router.generation
        assertEquals(1, host.activities); assertTrue(router.acceptsActivity(bottom.id, token))
        router.reconcile(false, main.id, listOf(main, bottom), null)
        assertFalse(router.acceptsActivity(bottom.id, token)); assertNull(router.target)
        router.reconcile(true, main.id, listOf(main, bottom), null)
        assertFalse(router.acceptsActivity(bottom.id, token)); assertEquals(2, host.activities)
    }
    @Test fun launchDeniedUsesInlineWithoutRepeatedAttempts() {
        val host = Host().apply { allowPresentation = false; allowActivity = false }; val router = CompanionRouter(host)
        repeat(10) { router.reconcile(true, main.id, listOf(main, bottom), null) }
        assertEquals(CompanionRouter.Mode.INLINE, router.mode)
        assertEquals(1, host.inlines); assertEquals(1, host.activities)
    }
    @Test fun redirectedActivityFallsBackInlineAndIgnoresStaleFailure() {
        val host = Host().apply { allowPresentation = false }; val router = CompanionRouter(host)
        router.reconcile(true, main.id, listOf(main, bottom), null)
        router.activityUnavailable(router.generation - 1)
        assertEquals(CompanionRouter.Mode.ACTIVITY, router.mode)
        router.activityUnavailable(router.generation)
        repeat(10) { router.reconcile(true, main.id, listOf(main, bottom), null) }
        assertEquals(CompanionRouter.Mode.INLINE, router.mode); assertEquals(1, host.activities)
    }
    @Test fun removalReassignmentReconnectAndPrimaryMigration() {
        val host = Host(); val router = CompanionRouter(host)
        val external = CompanionRouter.Target(23, "external")
        router.reconcile(true, main.id, listOf(main, bottom, external), "external")
        assertEquals(external, router.target)
        router.reconcile(true, main.id, listOf(main, bottom), "external")
        assertEquals(bottom, router.target)
        router.reconcile(true, main.id, listOf(main), "external")
        assertNull(router.target)
        val reconnected = external.copy(id = 41)
        router.reconcile(true, main.id, listOf(main, reconnected), "external")
        assertEquals(reconnected, router.target)
        router.reconcile(true, reconnected.id, listOf(main, reconnected), "external")
        assertEquals(main, router.target)
    }
    @Test fun overlappingKeysAndDevicesReleaseIndependently() {
        val state = InputState()
        state.key(1, 10, 0x200, true); state.key(1, 11, 0x200, true); state.key(2, 10, 0x200, true)
        state.key(1, 10, 0x200, false); assertEquals(0x200, state.buttons)
        state.remove(1); assertEquals(0x200, state.buttons)
        state.key(2, 10, 0x200, false); assertEquals(0, state.buttons)
    }
    @Test fun hatOverridesOpposingStickAndDeadZoneIsRespected() {
        val state = InputState()
        state.axis(1, -.9f, .4f, 1f, 0f, .25f, .5f); assertEquals(0x100, state.buttons)
        state.key(2, 10, 0x8000, true)
        state.remove(1); assertEquals(0x8000, state.buttons)
        state.clear(); assertEquals(0, state.buttons)
    }
}
