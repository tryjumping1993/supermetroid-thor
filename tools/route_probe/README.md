# Route probe

Interactive/batch driver used to record `tools/routes/*.txt` (raw controller input replayed by
`thor_tests ROM --route FILE`). Build with `-DTHOR_BUILD_TOOLS=ON`, then:

```
ROUTE_OUT=out.txt route_probe ROM.sfc < landing_site_to_bomb_torizo.probe.txt
```

Each input line is `HEXBUTTONS COUNT` (Session::step for COUNT frames). Special commands (hex
value in the button column): `fffd` list enemies/doors/pickups/shot blocks, `ffff` dump the room
block map, `fffa N` local map window around Samus, `fff7` pickups, `fffc N` search-ascend a tall
shaft, `fffe/fffb N` walk-and-shoot helpers, `fff6 N` Bomb Torizo fight bot. `ROUTE_OUT` writes the
run-length encoded raw input actually stepped, which is what the replay test consumes.
Button bits are `thor::Button` in `native/include/thor/session.hpp`.
