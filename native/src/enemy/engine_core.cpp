// Bank $A0 common enemy engine, part 1: instruction processing, loading, spawning, main loop.
// Every routine mirrors the labelled assembly in reference/sm-disassembly/src/bank_A0.asm.
#include "thor/enemy_engine.hpp"
#include "reference_labels.hpp"

namespace thor::enemy {
namespace {
constexpr uint16_t kNone = 0xFFFF;

uint8_t bank_of(Game& g, uint16_t x) { return uint8_t(g.Enemy_bank(x)); }
uint16_t bank_w(Game& g, uint16_t x, uint16_t address) { return g.rom_w(bank_of(g, x), address); }

// Executes an AI pointer (a 16-bit address in the enemy's own bank), as `JML [EnemyAIPointer]`.
void run_ai(Game& g, uint16_t x, uint16_t pointer) {
    g.word_at(Game::EnemyAIPointer_address) = pointer;
    g.byte_at(Game::EnemyAIPointer_address + 2) = bank_of(g, x);
    g.call_function((uint32_t(bank_of(g, x)) << 16) | pointer, x);
}
}

// ---- Common instruction commands ($A0:806B..8186) ---------------------------------------------
namespace {
InstructionResult Inst_Common_Enemy0FB2_InY(Game& g, uint16_t x, uint16_t y) {
    g.Enemy_var5(x) = bank_w(g, x, y); return {uint16_t(y + 2)};
}
InstructionResult Inst_Common_SetEnemy0FB2ToRTS(Game& g, uint16_t x, uint16_t y) {
    g.Enemy_var5(x) = uint16_t(labels::RTS_A0807B & 0xFFFF); return {y};
}
InstructionResult Inst_Common_DeleteEnemy(Game& g, uint16_t x, uint16_t y) {
    g.Enemy_properties(x) |= 0x0200; return {y, true};
}
InstructionResult Inst_Common_CallFunctionInY(Game& g, uint16_t x, uint16_t y) {
    const uint16_t function = bank_w(g, x, y);
    g.call_function((uint32_t(bank_of(g, x)) << 16) | function, x);
    return {uint16_t(y + 2)};
}
InstructionResult Inst_Common_CallFunctionInY_WithA(Game& g, uint16_t x, uint16_t y) {
    const uint16_t function = bank_w(g, x, y);
    g.A = bank_w(g, x, uint16_t(y + 2));
    g.call_function((uint32_t(bank_of(g, x)) << 16) | function, x);
    return {uint16_t(y + 4)};
}
InstructionResult Inst_Common_GotoY(Game& g, uint16_t x, uint16_t y) { return {bank_w(g, x, y)}; }
uint16_t goto_plus_y(Game& g, uint16_t x, uint16_t y) {
    // Signed byte at [Y] added to Y.
    const int8_t offset = int8_t(g.rom_b((uint32_t(bank_of(g, x)) << 16) | y));
    return uint16_t(y + offset);
}
InstructionResult Inst_Common_GotoY_PlusY(Game& g, uint16_t x, uint16_t y) { return {goto_plus_y(g, x, y)}; }
InstructionResult Inst_Common_DecrementTimer_GotoYIfNonZero(Game& g, uint16_t x, uint16_t y) {
    if (--g.Enemy_loopCounter(x) != 0) return {bank_w(g, x, y)};
    return {uint16_t(y + 2)};
}
InstructionResult Inst_Common_DecrementTimer_GotoY_PlusY_IfNonZero(Game& g, uint16_t x, uint16_t y) {
    // 8-bit decrement of the low byte only.
    Byte low = g.byte_at(Game::Enemy_loopCounter_address + x);
    --low;
    if (uint8_t(low) != 0) return {goto_plus_y(g, x, y)};
    return {uint16_t(y + 1)};
}
InstructionResult Inst_Common_TimerInY(Game& g, uint16_t x, uint16_t y) {
    g.Enemy_loopCounter(x) = bank_w(g, x, y); return {uint16_t(y + 2)};
}
InstructionResult Inst_Common_SkipNextInstruction(Game&, uint16_t, uint16_t y) { return {uint16_t(y + 2)}; }
InstructionResult Inst_Common_Sleep(Game& g, uint16_t x, uint16_t y) {
    g.Enemy_instList(x) = uint16_t(y - 2); return {y, true};
}
InstructionResult Inst_Common_WaitYFrames(Game& g, uint16_t x, uint16_t y) {
    g.Enemy_instTimer(x) = bank_w(g, x, y);
    g.Enemy_instList(x) = uint16_t(y + 2); return {uint16_t(y + 2), true};
}
InstructionResult Inst_Common_TransferYBytesInYToVRAM(Game&, uint16_t, uint16_t y) {
    return {uint16_t(y + 7)};   // TODO(vram): queue the DMA when the VRAM layer exists.
}
InstructionResult Inst_Common_EnableOffScreenProcessing(Game& g, uint16_t x, uint16_t y) {
    g.Enemy_properties(x) |= 0x0800; return {y};
}
InstructionResult Inst_Common_DisableOffScreenProcessing(Game& g, uint16_t x, uint16_t y) {
    g.Enemy_properties(x) &= 0xF7FF; return {y};
}
}

// ---- $A0:C26A Process enemy instructions -------------------------------------------------------
void ProcessEnemyInstructions(Game& g) {
    const uint16_t x = g.EnemyIndex();
    if (g.Enemy_AI(x) & 0x0004) return;
    if (--g.Enemy_instTimer(x) != 0) { g.Enemy_properties2(x) &= 0x7FFF; return; }
    uint16_t y = g.Enemy_instList(x);
    const uint8_t bank = bank_of(g, x);
    for (unsigned guard = 0; guard < 4096; ++guard) {
        const uint16_t value = g.rom_w(bank, y);
        if (value < 0x8000) {   // timer entry
            g.Enemy_instTimer(x) = value;
            g.Enemy_spritemap(x) = g.rom_w(bank, uint16_t(y + 2));
            g.Enemy_instList(x) = uint16_t(y + 4);
            g.Enemy_properties2(x) |= 0x8000;
            return;
        }
        g.word_at(Game::EnemyAIPointer_address) = value;
        y += 2;
        bool stop = false;
        if (!g.call_instruction((uint32_t(bank) << 16) | value, x, y, stop)) {
            // Unported command: park on it so the enemy neither runs away nor crashes.
            g.Enemy_instList(x) = uint16_t(y - 2); g.Enemy_instTimer(x) = 1; return;
        }
        if (stop) return;
    }
}

// ---- Grapple / touch / shot / frozen AI wrappers ------------------------------------------------
void NormalEnemyFrozenAI(Game& g) {
    const uint16_t x = g.EnemyIndex();
    g.Enemy_flashTimer(x) = 0;
    if (g.Enemy_freezeTimer(x)) {
        --g.Enemy_freezeTimer(x);
        if (g.EquippedBeams() & 0x0002) { g.A = 1; return; }
    }
    g.Enemy_AI(x) &= 0xFFFB;
    g.Enemy_freezeTimer(x) = g.Enemy_AI(x);
    g.A = 0;
}
namespace {
void GrappleAI_SwitchEnemyAIToMainAI(Game& g, uint16_t) {
    const uint16_t x = g.EnemyIndex();
    g.Enemy_AI(x) = 0; g.Enemy_invincibilityTimer(x) = 0; g.Enemy_freezeTimer(x) = 0; g.Enemy_shakeTimer(x) = 0;
}
void GrappleAI_SamusLatchesOnWithGrapple(Game& g, uint16_t) {
    const uint16_t x = g.EnemyIndex();
    g.GrappleBeam_EndXPosition() = g.Enemy_XPosition(x); g.GrappleBeam_EndYPosition() = g.Enemy_YPosition(x);
    if (g.Enemy_freezeTimer(x)) { g.Enemy_AI(x) = 4; return; }
    uint16_t time = g.rom_b(header_address(g.Enemy_ID(x)) + header::hurt_ai_time);
    g.Enemy_flashTimer(x) = time ? time : 4;
    g.Enemy_AI(x) = 0;
}
void GrappleAI_EnemyGrappleDeath(Game& g, uint16_t) {
    const uint16_t x = g.EnemyIndex();
    g.word_at(Game::EnemyTileData_address + 2 + x) = 4;
    EnemyDeath(g, 0);
    g.Enemy_AI(x) = 0;
}
void GrappleAI_SwitchToFrozenAI(Game& g, uint16_t) { g.Enemy_AI(g.EnemyIndex()) = 4; }
void GrappleAI_SamusLatchesOnWithGrapple_NoInvincibility(Game& g, uint16_t) {
    const uint16_t x = g.EnemyIndex();
    if (g.Enemy_freezeTimer(x)) {
        g.GrappleBeam_EndXPosition() = g.Enemy_XPosition(x); g.GrappleBeam_EndYPosition() = g.Enemy_YPosition(x);
        g.Enemy_AI(x) = 4; return;
    }
    run_ai(g, x, g.rom_w(header_address(g.Enemy_ID(x)) + header::main_ai));
    g.GrappleBeam_EndXPosition() = g.Enemy_XPosition(x); g.GrappleBeam_EndYPosition() = g.Enemy_YPosition(x);
    g.Enemy_AI(x) = 0;
}
void GrappleAI_SamusLatchesOnWithGrapple_ParalyzeEnemy(Game& g, uint16_t) {
    const uint16_t x = g.EnemyIndex();
    uint16_t time = g.rom_b(header_address(g.Enemy_ID(x)) + header::hurt_ai_time);
    g.Enemy_flashTimer(x) = time ? time : 4;
    g.Enemy_AI(x) = 0; g.Enemy_properties2(x) |= 1;
}
}

// ---- $A0:A45E Suit damage division; $91:DF51 Deal damage; $91:DF80 Restore missiles -------------
uint16_t Suit_Damage_Division(Game& g, uint16_t damage) {
    const uint16_t items = g.EquippedItems();
    if (items & 0x0020) return uint16_t(damage >> 2);
    if (items & 0x0001) return uint16_t(damage >> 1);
    return damage;
}
void Deal_A_Damage_to_Samus(Game& g, uint16_t damage) {
    if (damage == 0x012C) return;
    if (g.TimeIsFrozenFlag()) return;
    const int16_t remaining = int16_t(g.Energy() - damage);
    g.Energy() = remaining < 0 ? 0 : uint16_t(remaining);
}
void Restore_A_Energy_ToSamus(Game& g, uint16_t amount) {
    g.Energy() = uint16_t(g.Energy() + amount);
    if (int16_t(g.Energy() - g.MaxEnergy()) < 0) return;
    uint16_t reserve = uint16_t(g.Energy() - g.MaxEnergy() + g.ReserveEnergy());
    if (int16_t(reserve - g.MaxReserveEnergy()) >= 0) reserve = g.MaxReserveEnergy();
    g.ReserveEnergy() = reserve;
    if (reserve && !g.ReserveTankMode()) g.ReserveTankMode() = 1;
    g.Energy() = g.MaxEnergy();
}
void Restore_A_Missiles_ToSamus(Game& g, uint16_t amount) {
    g.Missiles() = uint16_t(g.Missiles() + amount);
    if (int16_t(g.Missiles() - g.MaxMissiles()) < 0) return;
    const uint16_t overflow = uint16_t(g.Missiles() - g.MaxMissiles());
    if (int16_t(g.MaxMissiles() - 99) >= 0) {
        g.ReserveMissiles() = uint16_t(g.ReserveMissiles() + overflow);
        if (int16_t(g.ReserveMissiles() - 99) >= 0) g.ReserveMissiles() = 99;
    }
    g.Missiles() = g.MaxMissiles();
}

// ---- Loading ($A0:8A1E..8EB5) -------------------------------------------------------------------
namespace {
constexpr uint32_t kEnemyPopulations = 0xA10000, kEnemySets = 0xB40000;

// $8BF3 Load enemy GFX indices: population entry -> tile/palette slot for the enemy's species.
void LoadEnemyGFXIndices(Game& g, uint16_t population_ptr, uint16_t y) {
    const uint16_t id = g.rom_w(kEnemyPopulations + population_ptr);
    uint16_t set = g.EnemySetPointer();
    uint16_t tiles = 0;
    for (unsigned guard = 0; guard < 64; ++guard) {
        const uint16_t set_id = g.rom_w(kEnemySets + set);
        if (set_id == id) {
            const uint16_t palette = uint16_t((g.rom_w(kEnemySets + set + 2) & 0x000F) << 9);
            g.Enemy_palette(y) = palette; g.EnemySpawnData_paletteIndex(y) = palette;
            g.Enemy_GFXOffset(y) = tiles; g.EnemySpawnData_VRAMTilesIndex(y) = tiles; return;
        }
        if (set_id == kNone) break;
        tiles = uint16_t(tiles + (g.rom_w(header_address(set_id) + header::tile_data_size) >> 5));
        set = uint16_t(set + 4);
    }
    g.Enemy_GFXOffset(y) = 0; g.EnemySpawnData_VRAMTilesIndex(y) = 0;
    g.Enemy_palette(y) = 0x0A00; g.EnemySpawnData_paletteIndex(y) = 0x0A00;
}

// $8D64 Process enemy set: load the species palettes into the target palettes and record
// where each species' tile data lands in the enemy tile RAM.
void ProcessEnemySet_LoadPalettesAndEnemyLoadingData(Game& g) {
    g.EnemyTileData_StackPointer() = 0;
    uint16_t offset = 0x0800;
    for (unsigned i = 0; i < 4; ++i) {
        g.word_at(Game::EnemyGFXData_IDs_address + i * 2) = 0;
        g.word_at(Game::EnemyGFXData_TilesIndex_address + i * 2) = 0;
        g.word_at(Game::EnemyGFXData_PaletteIndices_address + i * 2) = 0;
    }
    g.EnemyGFXData_StackPointer() = 0; g.EnemyGFXData_NextTilesIndex() = 0;
    uint16_t set = g.EnemySetPointer();
    for (unsigned guard = 0; guard < 64; ++guard) {
        const uint16_t id = g.rom_w(kEnemySets + set);
        if (id == kNone) return;
        const uint32_t header_at = header_address(id);
        const uint16_t size_word = g.rom_w(header_at + header::tile_data_size);
        const uint16_t palette_ptr = g.rom_w(header_at + header::palette);
        const uint8_t palette_bank = g.rom_b(header_at + header::bank);
        const uint16_t palette_index = uint16_t(g.rom_w(kEnemySets + set + 2) & 0x00FF);
        const uint32_t target = Game::TargetPalettes_BGP0_address + uint32_t(palette_index + 8) * 0x20;
        for (unsigned i = 0; i < 16; ++i)
            g.word_at(target + i * 2) = g.rom_w(palette_bank, uint16_t(palette_ptr + i * 2));
        const uint32_t tile_source = g.rom_l(header_at + header::tile_data);
        const uint16_t stack = g.EnemyTileData_StackPointer();
        g.EnemyTileData_Size(stack) = size_word & 0x7FFF;
        g.EnemyTileData_Pointer(stack) = tile_source;
        uint16_t dest = offset;
        if (size_word & 0x8000) dest = uint16_t((g.rom_w(kEnemySets + set + 2) & 0x3000) >> 3);
        g.EnemyTileData_Offset(stack) = dest;
        g.EnemyTileData_StackPointer() = uint16_t(stack + 7);
        const uint16_t gfx = g.EnemyGFXData_StackPointer();
        g.word_at(Game::EnemyGFXData_TilesIndex_address + gfx) = g.EnemyGFXData_NextTilesIndex();
        g.word_at(Game::EnemyGFXData_IDs_address + gfx) = id;
        g.word_at(Game::EnemyGFXData_PaletteIndices_address + gfx) = g.rom_w(kEnemySets + set + 2);
        g.EnemyGFXData_StackPointer() = uint16_t(gfx + 2);
        g.EnemyGFXData_NextTilesIndex() = uint16_t(g.EnemyGFXData_NextTilesIndex() + (size_word >> 5));
        offset = uint16_t(offset + g.rom_w(header_at + header::tile_data_size));
        set = uint16_t(set + 4);
    }
}
}

void ClearEnemyData_ProcessEnemySet(Game& g) {
    for (uint32_t i = 0; i < 0x800; i += 2) g.word_at(Game::Enemy_ID_address + i) = 0;
    if (g.rom_w(kEnemyPopulations + g.EnemyPopulationPointer()) == kNone) return;
    ProcessEnemySet_LoadPalettesAndEnemyLoadingData(g);
}

void Load_Enemies(Game& g) {
    g.BossID() = 0;
    g.EnemyBG2TilemapSize() = 0x0800;
    g.DisableSamusVsProjectileInteraction() = 0;
    ClearEnemyData_ProcessEnemySet(g);
    // $8C6C Load enemy tile data: copy the species tile blobs into enemy tile RAM ($7E7000).
    const auto sprite1 = g.rom().slice(labels::Tiles_Standard_Sprite_1, 0x200);
    for (uint32_t i = 0; i < 0x200; ++i) g.byte_at(Game::EnemyTileData_address + i) = sprite1[i];
    if (g.EnemyTileData_StackPointer()) {
        for (uint16_t entry = 0; entry != g.EnemyTileData_StackPointer(); entry = uint16_t(entry + 7)) {
            const uint32_t source = g.EnemyTileData_Pointer(entry);
            const uint16_t size = g.EnemyTileData_Size(entry), offset = g.EnemyTileData_Offset(entry);
            for (uint32_t i = 0; i < size && offset + i < 0x2800; ++i)
                g.byte_at(Game::EnemyTileData_address + offset + i) = g.rom_b(source + i);
        }
        g.EnemyTileData_StackPointer() = 0;
    }
    g.EnemyTileData_SrcAddr() = 0;
}

void Initialise_Enemies(Game& g) {
    for (uint32_t i = 0; i < 0x2800; i += 2) g.word_at(Game::EnemyTileData_address + i) = 0;
    g.NumberOfEnemiesKilled() = 0; g.GlobalOffScreenEnemyProcessingFlag() = 0;
    g.enemies_drawn.clear();
    for (uint32_t i = 0; i < 0x120; i += 2) g.word_at(Game::EnemyProjectileData_CollisionOptions_address + i) = 0;
    for (uint32_t i = 0; i < 0x24; i += 2) g.word_at(Game::EnemyProjectileData_KilledEnemyIndex_address + i) = kNone;
    uint16_t population = g.EnemyPopulationPointer();
    if (g.rom_w(kEnemyPopulations + population) == kNone) return;
    uint16_t y = 0;
    for (unsigned guard = 0; guard < 32; ++guard) {
        const uint32_t p = kEnemyPopulations + population;
        LoadEnemyGFXIndices(g, population, y);
        const uint16_t id = g.rom_w(p);
        const uint32_t header_at = header_address(id);
        g.Enemy_XHitboxRadius(y) = g.rom_w(header_at + header::width);
        g.Enemy_YHitboxRadius(y) = g.rom_w(header_at + header::height);
        g.Enemy_health(y) = g.rom_w(header_at + header::health);
        g.Enemy_layer(y) = g.rom_b(header_at + header::layer);
        g.Enemy_bank(y) = g.rom_w(header_at + header::bank);
        if (g.rom_w(header_at + header::boss_id)) g.BossID() = g.rom_w(header_at + header::boss_id);
        g.Enemy_ID(y) = id;
        g.Enemy_XPosition(y) = g.rom_w(p + 2); g.Enemy_YPosition(y) = g.rom_w(p + 4);
        g.Enemy_instList(y) = g.rom_w(p + 6);
        g.Enemy_properties(y) = g.rom_w(p + 8); g.Enemy_properties2(y) = g.rom_w(p + 10);
        g.Enemy_init0(y) = g.rom_w(p + 12); g.Enemy_init1(y) = g.rom_w(p + 14);
        g.Enemy_frameCounter(y) = 0; g.Enemy_loopCounter(y) = 0; g.Enemy_instTimer(y) = 1;
        g.EnemyIndex() = y;
        run_ai(g, y, g.rom_w(header_at + header::init_ai));
        g.Enemy_spritemap(y) = 0;
        if (g.Enemy_properties(y) & 0x2000)
            g.Enemy_spritemap(y) = (g.Enemy_properties2(y) & 4) ? uint16_t(labels::ExtendedSpritemap_Common_Nothing & 0xFFFF)
                                                                 : uint16_t(labels::Spritemap_Common_Nothing & 0xFFFF);
        y = uint16_t(y + 0x40); population = uint16_t(population + 0x10);
        if (g.rom_w(kEnemyPopulations + population) == kNone) break;
    }
    g.FirstFreeEnemyIndex() = y;
    g.NumberOfEnemiesRequiredToKill() = g.rom_w(kEnemyPopulations + population + 2) & 0x00FF;
}

// ---- $A0:8EB6 Determine which enemies to process --------------------------------------------------
void Determine_Which_Enemies_to_Process(Game& g) {
    g.EnemyIndex() = 0; g.ActiveEnemyIndicesStackPointer() = 0; g.InteractiveEnemyIndicesStackPointer() = 0;
    const bool everything = g.GlobalOffScreenEnemyProcessingFlag() != 0;
    for (uint16_t x = 0; x < 0x800; x = uint16_t(x + 0x40)) {
        g.EnemyIndex() = x;
        const uint16_t id = g.Enemy_ID(x);
        if (!id || id == uint16_t(labels::EnemyHeaders_Respawn & 0xFFFF)) continue;
        const uint16_t properties = g.Enemy_properties(x);
        if (properties & 0x0200) { g.Enemy_ID(x) = 0; continue; }
        bool active = everything;
        if (!everything) {
            const uint16_t xr = g.Enemy_XHitboxRadius(x);
            active = (properties & 0x0800) || (g.Enemy_AI(x) & 0x0004);
            if (!active) {
                const int16_t layer_x = int16_t(g.Layer1XPosition()), layer_y = int16_t(g.Layer1YPosition());
                active = !(int16_t(g.Enemy_XPosition(x) + xr - layer_x) < 0) &&
                    !(int16_t(layer_x + 0x100 + xr - g.Enemy_XPosition(x)) < 0) &&
                    !(int16_t(g.Enemy_YPosition(x) + 8 - layer_y) < 0) &&
                    !(int16_t(layer_y + 0xF8 - g.Enemy_YPosition(x)) < 0);
            }
        }
        if (!active) continue;
        g.word_at(Game::ActiveEnemyIndices_address + g.ActiveEnemyIndicesStackPointer()) = x;
        if (everything) g.word_at(Game::InteractiveEnemyIndices_address + g.ActiveEnemyIndicesStackPointer()) = x;
        g.ActiveEnemyIndicesStackPointer() += 2;
        if (properties & 0x0400) continue;
        g.word_at(Game::InteractiveEnemyIndices_address + g.InteractiveEnemyIndicesStackPointer()) = x;
        g.InteractiveEnemyIndicesStackPointer() += 2;
    }
    g.word_at(Game::ActiveEnemyIndices_address + g.ActiveEnemyIndicesStackPointer()) = kNone;
    g.word_at(Game::InteractiveEnemyIndices_address + g.InteractiveEnemyIndicesStackPointer()) = kNone;
}

void DecrementSamusHurtTimers_ClearActiveEnemyIndicesLists(Game& g) {
    if (g.SamusInvincibilityTimer()) --g.SamusInvincibilityTimer();
    if (g.SamusKnockbackTimer()) --g.SamusKnockbackTimer();
    if (g.ProjectileInvincibilityTimer()) --g.ProjectileInvincibilityTimer();
    g.word_at(Game::InteractiveEnemyIndices_address) = kNone;
    g.word_at(Game::ActiveEnemyIndices_address) = kNone;
}

// ---- $A0:8687 Handle room shaking -------------------------------------------------------------------
void Handle_Room_Shaking(Game& g) {
    if (g.EarthquakeTimer() && !g.TimeIsFrozenFlag() && g.EarthquakeType() < 0x24) {
        const uint32_t entry = labels::BGShakeDisplacements + uint32_t(g.EarthquakeType()) * 8;
        const uint16_t d[4] = {g.rom_w(entry), g.rom_w(entry + 2), g.rom_w(entry + 4), g.rom_w(entry + 6)};
        Word* dummy = nullptr; (void)dummy;
        const bool negate = (g.EarthquakeTimer() & 2) != 0;
        auto shift = [&](uint32_t address, uint16_t amount) {
            g.word_at(address) = uint16_t(g.word_at(address).get() + (negate ? uint16_t(0 - amount) : amount));
        };
        shift(Game::DP_BG1XScroll_address, d[0]); shift(Game::DP_BG1YScroll_address, d[1]);
        shift(Game::DP_BG2XScroll_address, d[2]); shift(Game::DP_BG2YScroll_address, d[3]);
        --g.EarthquakeTimer();
        if (g.EarthquakeType() >= 0x12) {
            for (uint16_t i = 0;; i = uint16_t(i + 2)) {
                const uint16_t index = g.word_at(Game::ActiveEnemyIndices_address + i);
                if (index == kNone) break;
                g.Enemy_shakeTimer(index) = 2;
            }
        }
    }
    ++g.NumberOfTimesRoomShakingExecuted();
}

// ---- $A0:9275 / 92DB Spawn enemy ---------------------------------------------------------------------
void SpawnEnemy_AlwaysSucceed(Game& g, uint32_t population, uint16_t y, uint16_t parts) {
    for (unsigned part = 0; part < 16; ++part) {
        const uint16_t id = g.rom_w(population);
        // Match the species against the loaded GFX slots.
        int slot = -1;
        for (int i = 0; i < 4; ++i) if (g.word_at(Game::EnemyGFXData_IDs_address + i * 2) == id) { slot = i; break; }
        if (slot >= 0) {
            g.Enemy_GFXOffset(y) = g.word_at(Game::EnemyGFXData_TilesIndex_address + slot * 2);
            g.Enemy_palette(y) = uint16_t(g.word_at(Game::EnemyGFXData_PaletteIndices_address + slot * 2) << 9);
        } else { g.Enemy_GFXOffset(y) = 0; g.Enemy_palette(y) = 0; }
        const uint32_t header_at = header_address(id);
        g.Enemy_XHitboxRadius(y) = g.rom_w(header_at + header::width);
        g.Enemy_YHitboxRadius(y) = g.rom_w(header_at + header::height);
        g.Enemy_health(y) = g.rom_w(header_at + header::health);
        g.Enemy_layer(y) = g.rom_b(header_at + header::layer);
        g.Enemy_bank(y) = g.rom_w(header_at + header::bank);
        g.Enemy_ID(y) = id;
        g.Enemy_XPosition(y) = g.rom_w(population + 2); g.Enemy_YPosition(y) = g.rom_w(population + 4);
        g.Enemy_instList(y) = g.rom_w(population + 6);
        g.Enemy_properties(y) = g.rom_w(population + 8); g.Enemy_properties2(y) = g.rom_w(population + 10);
        g.Enemy_init0(y) = g.rom_w(population + 12); g.Enemy_init1(y) = g.rom_w(population + 14);
        g.Enemy_frameCounter(y) = 0; g.Enemy_loopCounter(y) = 0;
        g.Enemy_var0(y) = 0; g.Enemy_var1(y) = 0; g.Enemy_var2(y) = 0; g.Enemy_var3(y) = 0;
        g.Enemy_var4(y) = 0; g.Enemy_var5(y) = 0; g.Enemy_instTimer(y) = 1;
        g.EnemyIndex() = y;
        const uint16_t init = g.rom_w(header_at + header::init_ai);
        if (init >= 0x8000) run_ai(g, y, init);
        if (g.Enemy_properties(y) & 0x2000) g.Enemy_spritemap(y) = uint16_t(labels::Spritemap_Common_Nothing & 0xFFFF);
        if (parts == 0 || --parts == 0) break;
        y = uint16_t(y + 0x40); population += 0x10;
    }
}

bool SpawnEnemy(Game& g, uint32_t population, uint16_t* new_index) {
    const uint16_t backup_index = g.EnemyIndex();
    const uint32_t backup_ai = g.word_at(Game::EnemyAIPointer_address).get() | (uint32_t(g.byte_at(Game::EnemyAIPointer_address + 2)) << 16);
    const uint16_t id = g.rom_w(population);
    int16_t parts = int16_t(g.rom_w(header_address(id) + header::parts)) - 1;
    if (parts < 0) parts = 0;
    for (uint16_t index = 0; index < 0x800; index = uint16_t(index + 0x40)) {
        if (g.Enemy_ID(index)) continue;
        int16_t remaining = parts; uint16_t probe = index; bool ok = true;
        while (remaining) {
            probe = uint16_t(probe + 0x40);
            if (probe >= 0x800 || g.Enemy_ID(probe)) { ok = false; break; }
            --remaining;
        }
        if (!ok) continue;
        SpawnEnemy_AlwaysSucceed(g, population, index, uint16_t(parts));
        g.EnemyIndex() = backup_index;
        g.word_at(Game::EnemyAIPointer_address) = uint16_t(backup_ai);
        g.byte_at(Game::EnemyAIPointer_address + 2) = uint8_t(backup_ai >> 16);
        if (new_index) *new_index = index;
        return true;
    }
    return false;
}

void DeleteEnemyAndAnyConnectedEnemies(Game& g) {
    uint16_t x = g.EnemyIndex();
    uint16_t count = g.rom_w(header_address(g.Enemy_ID(x)) + header::parts);
    if (!count) count = 1;
    for (; count; --count, x = uint16_t(x + 0x40)) g.Enemy_ID(x) = 0;
}

// ---- Registration ----------------------------------------------------------------------------------
void register_engine(Game& g) {
    using namespace labels;
    auto fn = [&](uint32_t address, void (*f)(Game&, uint16_t)) { g.add_function(address, f); };
    fn(Common_GrappleAI_NoInteraction, GrappleAI_SwitchEnemyAIToMainAI);
    fn(Common_GrappleAI_SamusLatchesOn, GrappleAI_SamusLatchesOnWithGrapple);
    fn(Common_GrappleAI_KillEnemy, GrappleAI_EnemyGrappleDeath);
    fn(Common_GrappleAI_CancelGrappleBeam, GrappleAI_SwitchToFrozenAI);
    fn(Common_GrappleAI_SamusLatchesOn_NoInvincibility, GrappleAI_SamusLatchesOnWithGrapple_NoInvincibility);
    fn(UNUSED_Common_GrappleAI_SamusLatchesOn_ParalyzeEnemy_A08019, GrappleAI_SamusLatchesOnWithGrapple_ParalyzeEnemy);
    fn(Common_GrappleAI_HurtSamus, GrappleAI_SwitchToFrozenAI);
    fn(Common_NormalEnemyTouchAI, [](Game& g2, uint16_t) { NormalEnemyTouchAI(g2); });
    fn(Common_NormalTouchAI_NoDeathCheck, [](Game& g2, uint16_t) { NormalEnemyTouchAI_NoDeathCheck(g2); });
    fn(Common_NormalEnemyShotAI, [](Game& g2, uint16_t) { NormalEnemyShotAI(g2); });
    fn(Common_NormalEnemyShotAI_NoDeathCheck_NoEnemyShotGraphic,
        [](Game& g2, uint16_t) { NormalEnemyShotAI_NoDeathCheck_NoEnemyShotGraphic(g2); });
    fn(Common_NormalEnemyPowerBombAI, [](Game& g2, uint16_t) { NormalEnemyPowerBombAI(g2); });
    fn(Common_NormalEnemyPowerBombAI_NoDeathCheck, [](Game& g2, uint16_t) { NormalEnemyPowerBombAI_NoDeathCheck(g2); });
    fn(Common_NormalEnemyFrozenAI, [](Game& g2, uint16_t) { NormalEnemyFrozenAI(g2); });
    fn(Common_CreateADudShot, [](Game& g2, uint16_t) { CreateADudShot(g2); });
    fn(RTS_A0804B, [](Game&, uint16_t) {});
    fn(RTL_A0804C, [](Game&, uint16_t) {});
    fn(RTS_A0807B, [](Game&, uint16_t) {});
    auto ins = [&](uint32_t address, InstructionResult (*f)(Game&, uint16_t, uint16_t)) { g.add_instruction(address, f); };
    ins(Instruction_Common_Enemy0FB2_InY, Inst_Common_Enemy0FB2_InY);
    ins(Instruction_Common_SetEnemy0FB2ToRTS, Inst_Common_SetEnemy0FB2ToRTS);
    ins(Instruction_Common_DeleteEnemy, Inst_Common_DeleteEnemy);
    ins(Instruction_Common_CallFunctionInY, Inst_Common_CallFunctionInY);
    ins(Instruction_Common_CallFunctionInY_WithA, Inst_Common_CallFunctionInY_WithA);
    ins(Instruction_Common_GotoY, Inst_Common_GotoY);
    ins(Instruction_Common_GotoY_PlusY, Inst_Common_GotoY_PlusY);
    ins(Instruction_Common_DecrementTimer_GotoYIfNonZero, Inst_Common_DecrementTimer_GotoYIfNonZero);
    ins(Instruction_Common_DecrementTimer_GotoYIfNonZero_duplicate, Inst_Common_DecrementTimer_GotoYIfNonZero);
    ins(Instruction_Common_DecrementTimer_GotoY_PlusY_IfNonZero, Inst_Common_DecrementTimer_GotoY_PlusY_IfNonZero);
    ins(Instruction_Common_TimerInY, Inst_Common_TimerInY);
    ins(Instruction_Common_SkipNextInstruction, Inst_Common_SkipNextInstruction);
    ins(Instruction_Common_Sleep, Inst_Common_Sleep);
    ins(Instruction_Common_WaitYFrames, Inst_Common_WaitYFrames);
    ins(Instruction_Common_TransferYBytesInYToVRAM, Inst_Common_TransferYBytesInYToVRAM);
    ins(Instruction_Common_EnableOffScreenProcessing, Inst_Common_EnableOffScreenProcessing);
    ins(Instruction_Common_DisableOffScreenProcessing, Inst_Common_DisableOffScreenProcessing);
}
}
