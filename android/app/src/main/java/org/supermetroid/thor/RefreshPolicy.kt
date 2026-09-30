package org.supermetroid.thor

import android.util.Log
import android.view.Display
import android.view.Window

object RefreshPolicy {
    fun request(window: Window, display: Display?, hz: Int) {
        display ?: return
        val current = display.mode
        val mode = display.supportedModes.filter {
            it.physicalWidth == current.physicalWidth && it.physicalHeight == current.physicalHeight
        }.minByOrNull { kotlin.math.abs(it.refreshRate - hz) } ?: return
        val attributes = window.attributes
        if (attributes.preferredDisplayModeId == mode.modeId) return
        attributes.preferredDisplayModeId = mode.modeId
        window.attributes = attributes
        Log.i("ThorNative", "refreshRequest display=${display.displayId} requestedHz=$hz mode=${mode.modeId} advertisedHz=${mode.refreshRate} activeHz=${display.refreshRate}")
    }
}
