package org.supermetroid.thor

import android.app.Activity
import android.os.Bundle
import android.view.WindowManager
import android.view.KeyEvent
import android.view.MotionEvent

class CompanionActivity : Activity() {
    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        MainActivity.immerse(window)
        setContentView(CompanionView(this))
    }
    override fun onResume() { super.onResume(); SessionHost.resumeRequested = true }
    override fun dispatchKeyEvent(event: KeyEvent): Boolean =
        SessionHost.mainActivity?.dispatchKeyEvent(event) ?: super.dispatchKeyEvent(event)
    override fun onGenericMotionEvent(event: MotionEvent): Boolean =
        SessionHost.mainActivity?.onGenericMotionEvent(event) ?: super.onGenericMotionEvent(event)
}
