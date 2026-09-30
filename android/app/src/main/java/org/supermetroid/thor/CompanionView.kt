package org.supermetroid.thor

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.hardware.display.DisplayManager
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.View
import android.widget.Button
import android.widget.CheckBox
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.ArrayAdapter
import android.widget.ScrollView
import android.widget.TextView
import org.json.JSONArray
import org.json.JSONObject

class CompanionView(context: Context) : LinearLayout(context) {
    private val handler = Handler(Looper.getMainLooper())
    private val title = TextView(context)
    private val body = LinearLayout(context)
    private val map = MapView(context)
    private val inventory = TextView(context)
    private val pauseButton = Button(context)
    private var selectedTab = "Map"
    private val refresh = object : Runnable {
        override fun run() {
            val state = SessionHost.state()
            title.text = if (state.optBoolean("loaded")) "${state.optString("room").replace('_', ' ')}  ·  Companion" else "THOR COMPANION"
            map.state = state; map.invalidate()
            pauseButton.text = if (state.optBoolean("paused")) "Resume" else "Pause"
            val sram = state.optJSONObject("sram")
            val items = state.optInt("items")
            inventory.text = "Energy  ${state.optInt("health")} / ${state.optInt("maxHealth")}\n" +
                "Missiles  ${state.optInt("missiles")} / ${state.optInt("maxMissiles")}  ·  ${if (state.optBoolean("selectedMissiles")) "selected" else "Power Beam"}\n" +
                "Morph Ball  ${if (items and 4 != 0) "acquired" else "not acquired"}\n" +
                "Bombs  ${if (items and 0x1000 != 0) "acquired" else "not acquired"}\n\n" +
                (if (state.optBoolean("dead")) "Samus has fallen. Load a saved game or start a new game.\n\n" else "") +
                (if (state.optBoolean("saveAvailable")) "Save available here.\n\n" else "Save at the ship or a save station.\n\n") +
                (if (sram?.optBoolean("valid") == true) "Slot ${sram.optInt("slot") + 1}: saved energy ${sram.optInt("health")}, missiles ${sram.optInt("missiles")}"
                    else "Slot ${(sram?.optInt("slot") ?: 0) + 1}: empty")
            handler.postDelayed(this, 17) // Upper bound ~60 Hz, independent from top-screen frames.
        }
    }

    init {
        orientation = VERTICAL; setPadding(20, 12, 20, 12); setBackgroundColor(Color.rgb(13, 21, 34))
        title.setTextColor(Color.WHITE); title.textSize = 18f; addView(title)
        val tabs = LinearLayout(context)
        for (name in listOf("Map", "Inventory", "Guidance", "Settings")) {
            tabs.addView(Button(context).apply { text = name; setOnClickListener { tab(name) } }, LayoutParams(0, -2, 1f))
        }
        addView(tabs)
        body.orientation = VERTICAL; addView(body, LayoutParams(-1, 0, 1f))
        inventory.setTextColor(Color.WHITE); inventory.textSize = 17f; inventory.setPadding(12, 18, 12, 12)
        val controls = LinearLayout(context).apply { gravity = Gravity.CENTER }
        fun held(text: String, bit: Int) = Button(context).apply {
            this.text = text
            setOnTouchListener { _, event ->
                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN -> SessionHost.buttons = SessionHost.buttons or bit
                    MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> SessionHost.buttons = SessionHost.buttons and bit.inv()
                }
                true
            }
        }
        controls.addView(held("◀", 0x200), LayoutParams(0, -2, 1f))
        pauseButton.text = "Pause"; pauseButton.setOnClickListener { NativeBridge.pause() }
        controls.addView(pauseButton, LayoutParams(0, -2, 1f))
        controls.addView(held("Jump", 0x8000), LayoutParams(0, -2, 1f))
        controls.addView(held("Shoot", 0x40), LayoutParams(0, -2, 1f))
        controls.addView(held("▶", 0x100), LayoutParams(0, -2, 1f))
        val aiming = LinearLayout(context)
        for ((label, bit) in listOf("↑" to 0x800, "↓" to 0x400, "Aim up" to 0x20, "Aim down" to 0x10, "Run" to 0x4000, "Missile" to 0x2000))
            aiming.addView(held(label, bit), LayoutParams(0, -2, 1f))
        addView(aiming)
        addView(controls)
        tab("Map")
    }
    override fun onAttachedToWindow() { super.onAttachedToWindow(); handler.post(refresh) }
    override fun onDetachedFromWindow() { handler.removeCallbacks(refresh); SessionHost.buttons = 0; super.onDetachedFromWindow() }
    private fun note(text: String) = TextView(context).apply { this.text = text; setTextColor(Color.LTGRAY); textSize = 16f; setPadding(12, 16, 12, 16) }
    // Presentation uses a TYPE_PRESENTATION window context. An AlertDialog
    // creates TYPE_APPLICATION and crashes on Thor/Android 13. Keep selection
    // inside the existing companion window instead of creating another one.
    private fun choose(title: String, entries: Array<String>, selected: (Int) -> Unit) {
        body.removeAllViews()
        body.addView(Button(context).apply { text = "Back to settings"; setOnClickListener { tab("Settings") } })
        body.addView(note(title))
        val choices = ListView(context)
        choices.adapter = object : ArrayAdapter<String>(context, android.R.layout.simple_list_item_1, entries) {
            override fun getView(position: Int, convertView: View?, parent: android.view.ViewGroup): View {
                return (super.getView(position, convertView, parent) as TextView).apply { setTextColor(Color.WHITE); textSize = 18f }
            }
        }
        choices.setOnItemClickListener { _, _, index, _ -> selected(index); tab("Settings") }
        body.addView(choices, LayoutParams(-1, 0, 1f))
    }
    private fun tab(name: String) {
        selectedTab = name; body.removeAllViews()
        when (name) {
            "Map" -> { body.addView(map, LayoutParams(-1, 0, 1f)); body.addView(note("Explored cells only · pinch to zoom · drag to pan · tap for a waypoint")) }
            "Inventory" -> {
                (inventory.parent as? android.view.ViewGroup)?.removeView(inventory)
                val slots = LinearLayout(context)
                for (i in 0..2) slots.addView(Button(context).apply { text = "Slot ${i + 1}"; setOnClickListener { NativeBridge.selectSlot(i); context.getSharedPreferences("thor", 0).edit().putInt("saveSlot", i).apply() } }, LayoutParams(0, -2, 1f))
                body.addView(slots)
                body.addView(ScrollView(context).apply { addView(inventory) }, LayoutParams(-1, 0, 1f))
                val game = LinearLayout(context)
                for ((label, action) in listOf("New game" to "new", "Load game" to "load", "Save game" to "save"))
                    game.addView(Button(context).apply { text = label; setOnClickListener { SessionHost.game(context, action) } }, LayoutParams(0, -2, 1f))
                body.addView(game)
                body.addView(Button(context).apply { text = "Import SRAM"; setOnClickListener { SessionHost.mainActivity?.importSram() } })
                body.addView(Button(context).apply { text = "Export SRAM"; setOnClickListener { SessionHost.mainActivity?.exportSram() } })
            }
            "Guidance" -> body.addView(note("Hints, item tracking and progression routes are planned for Milestone 4.\n\nThe map reveals only cells visited in the current movement session.\nNo external hint service is used."))
            "Settings" -> {
                val options = LinearLayout(context).apply { orientation = VERTICAL }
                body.addView(ScrollView(context).apply { addView(options) }, LayoutParams(-1, 0, 1f))
                fun check(label: String, enabled: Boolean, change: (Boolean) -> Unit) {
                    options.addView(CheckBox(context).apply {
                        text = label; setTextColor(Color.WHITE); isChecked = enabled
                        setOnCheckedChangeListener { _, on -> change(on); SessionHost.saveOptions(context) }
                    })
                }
                options.addView(Button(context).apply {
                    text = "Requested render refresh: ${SessionHost.renderHz} Hz"
                    setOnClickListener {
                        SessionHost.renderHz = if (SessionHost.renderHz == 120) 60 else 120
                        SessionHost.saveOptions(context); tab("Settings")
                    }
                })
                for (label in listOf("Jump", "Pause", "Shoot")) {
                    options.addView(Button(context).apply {
                        val input = SessionHost.mainActivity?.controls
                        val code = when (label) { "Jump" -> input?.jump; "Pause" -> input?.pause; else -> input?.shoot }
                        text = "$label controller button: ${ControllerInput.choices.firstOrNull { it.second == code }?.first}"
                        setOnClickListener {
                            choose("Choose $label button; conflicting bindings swap.", ControllerInput.choices.map { it.first }.toTypedArray()) { index ->
                                val selected = ControllerInput.choices[index].second
                                if (label == "Shoot") input?.remapShoot(selected) else input?.remap(label == "Jump", selected)
                            }
                        }
                    })
                }
                check("Widescreen", SessionHost.wide) { SessionHost.wide = it }
                check("Stable interpolation", SessionHost.interpolate) { SessionHost.interpolate = it }
                check("Reveal room map (development spoilers)", SessionHost.spoilers) { SessionHost.spoilers = it }
                options.addView(Button(context).apply {
                    text = "Traverse room exit · development tool"
                    setOnClickListener {
                        val snapshot = JSONObject(NativeBridge.doors())
                        val source = snapshot.getInt("roomIndex")
                        val doors = snapshot.getJSONArray("doors")
                        val directions = arrayOf("right", "left", "down", "up")
                        val entries = Array(doors.length()) {
                            val door = doors.getJSONObject(it)
                            "${it + 1}. ${door.getString("destination").replace('_', ' ')} · ${directions[door.getInt("direction")]}" +
                                if (!door.getBoolean("supported")) " · unavailable" else ""
                        }
                        choose("Development traversal bypasses locks and scripts; placement is provisional.", entries) { index ->
                            SessionHost.traverseDoor(context, source, doors.getJSONObject(index).getInt("index"))
                        }
                    }
                })
                options.addView(Button(context).apply {
                    text = "Select room · development tool"
                    setOnClickListener {
                        val names = JSONArray(NativeBridge.rooms())
                        val entries = Array(names.length()) { names.getString(it).replace('_', ' ') }
                        choose("Reference room viewer · no room scripts", entries) { index -> SessionHost.selectRoom(context, index) }
                    }
                })
                options.addView(Button(context).apply {
                    text = "Assign companion display"
                    setOnClickListener {
                        val displays = context.getSystemService(DisplayManager::class.java).displays.filter { it.isValid && it.displayId != SessionHost.mainActivity?.display?.displayId }
                        val entries = displays.map { "${it.name}: ${it.displayId} · ${it.mode.physicalWidth}×${it.mode.physicalHeight}" }.toTypedArray()
                        choose("Companion display (applies immediately)", entries) { index ->
                            context.getSharedPreferences("thor", 0).edit().putString("helperDisplay", MainActivity.displayKey(displays[index])).apply()
                            SessionHost.mainActivity?.refreshDisplays()
                        }
                    }
                })
                options.addView(note("D-pad or left stick: move/aim · Jump, Pause and Shoot controller buttons: configurable above · Keyboard: WASD, Space, J\n\nPower Beam opens blue caps. Five missiles open red caps. Select toggles missiles; L/R aim diagonally. Down crouches, then morphs after acquiring Morph Ball. Shoot while morphed lays bombs. Native play covers the opening route; audio is planned for Milestone 3."))
            }
        }
    }

    private class MapView(context: Context) : View(context) {
        var state = JSONObject()
            set(value) {
                field = value
                visited.clear()
                value.optJSONArray("explored")?.let { cells ->
                    for (i in 0 until cells.length()) visited.add(cells.getInt(i))
                }
            }
        private val visited = HashSet<Int>()
        private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
        private var panX = 0f; private var panY = 0f
        private var lastX = 0f; private var lastY = 0f
        private var downX = 0f; private var downY = 0f
        private var waypoint: Pair<Float, Float>? = null
        private var roomIndex = -1
        private var zoom = 1f
        private var gestureScaled = false
        private val scale = ScaleGestureDetector(context, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
            override fun onScale(detector: ScaleGestureDetector): Boolean {
                gestureScaled = true
                zoom = (zoom * detector.scaleFactor).coerceIn(.5f, 4f)
                invalidate(); return true
            }
        })
        override fun onDraw(canvas: Canvas) {
            super.onDraw(canvas)
            if (!state.optBoolean("loaded")) { paint.color = Color.LTGRAY; paint.textSize = 26f; canvas.drawText("Import your ROM on the top screen", 20f, height / 2f, paint); return }
            if (roomIndex != state.optInt("roomIndex")) { roomIndex = state.optInt("roomIndex"); panX = 0f; panY = 0f; zoom = 1f; waypoint = null }
            val columns = state.optInt("width") / 256; val rows = state.optInt("height") / 256
            if (columns <= 0 || rows <= 0) return
            val size = minOf((width - 40f) / columns, (height - 40f) / rows) * zoom
            val ox = (width - columns * size) / 2 + panX; val oy = (height - rows * size) / 2 + panY
            for (y in 0 until rows) for (x in 0 until columns) {
                if (!SessionHost.spoilers && y * columns + x !in visited) continue
                paint.color = if (y * columns + x in visited) Color.rgb(32, 123, 153) else Color.rgb(34, 48, 68)
                canvas.drawRoundRect(ox + x * size + 2, oy + y * size + 2, ox + (x + 1) * size - 2, oy + (y + 1) * size - 2, 4f, 4f, paint)
            }
            paint.color = Color.rgb(255, 211, 96)
            canvas.drawCircle(ox + state.optDouble("x").toFloat() / 256 * size, oy + state.optDouble("y").toFloat() / 256 * size, maxOf(5f, size * .09f), paint)
            waypoint?.let { (x, y) -> paint.color = Color.MAGENTA; canvas.drawCircle(ox + x * size, oy + y * size, 7f, paint) }
        }
        override fun onTouchEvent(event: MotionEvent): Boolean {
            scale.onTouchEvent(event)
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> { gestureScaled = false; lastX = event.x; lastY = event.y; downX = event.x; downY = event.y; parent.requestDisallowInterceptTouchEvent(true) }
                MotionEvent.ACTION_MOVE -> { if (!scale.isInProgress) { panX += event.x - lastX; panY += event.y - lastY }; lastX = event.x; lastY = event.y; invalidate() }
                MotionEvent.ACTION_UP -> {
                    if (!gestureScaled && kotlin.math.abs(event.x - downX) + kotlin.math.abs(event.y - downY) < 12f) {
                        val columns = state.optInt("width") / 256; val rows = state.optInt("height") / 256
                        if (columns > 0 && rows > 0) {
                            val size = minOf((width - 40f) / columns, (height - 40f) / rows) * zoom
                            val x = (event.x - (width - columns * size) / 2 - panX) / size
                            val y = (event.y - (height - rows * size) / 2 - panY) / size
                            if (x >= 0 && x < columns && y >= 0 && y < rows) waypoint = Pair(x, y)
                        }
                        performClick(); invalidate()
                    }
                }
            }
            return true
        }
        override fun performClick(): Boolean { super.performClick(); return true }
    }
}
