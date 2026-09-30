# Third-party references

The original repository's WLA disassembly and legacy assets remain in their existing directories. The new Android target does not package them.

The reference submodule [InsaneFirebat/sm_disassembly](https://github.com/InsaneFirebat/sm_disassembly) is pinned to `11c906f547edc1b57f5a5923cf977fe7b50a3694`. Its [LICENSE.txt](../reference/sm-disassembly/LICENSE.txt) provides the Zero-Clause BSD terms. Generated metadata records this revision and contains addresses/names only. Native decompression, room-state conditions, collision geometry and data interpretation were implemented using this reference.

The reference's `tools/rip_assets.py` retains its embedded MIT license (copyright 2022 yuriks). The bundled Asar tool retains its own license files under `reference/sm-disassembly/tools`. These tools are used for development/reference verification and are not included in the APK.

Imported ROM bytes, artwork, audio and SRAM are runtime user inputs. Private extraction/build data is ignored by Git. No SPC emulator, Oboe, Swappy or other game/runtime backend has been integrated in this development slice.
