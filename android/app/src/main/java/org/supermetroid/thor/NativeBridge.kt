package org.supermetroid.thor

object NativeBridge {
    init { System.loadLibrary("thor") }
    external fun loadRom(bytes: ByteArray)
    external fun advance(timestamp: Long, buttons: Int)
    external fun suspend()
    external fun pause()
    external fun options(widescreen: Boolean, interpolation: Boolean)
    external fun selectRoom(index: Int)
    external fun newGame()
    external fun loadGame()
    external fun saveGame()
    external fun rooms(): String
    external fun doors(): String
    external fun traverseDoor(expectedRoom: Int, index: Int)
    external fun companion(): String
    external fun importSram(bytes: ByteArray)
    external fun exportSram(): ByteArray
    external fun selectSlot(slot: Int)
    external fun glInit()
    external fun glResize(width: Int, height: Int)
    external fun glDraw()
}
