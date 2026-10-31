/* Oracle hook DLL for ROADRASH.EXE (SHA-256 73d7f974...). Replaces AWEMAN32.DLL.
   The game statically imports AWEMAN32.dll ordinal 2; this DLL forwards it to the
   renamed original (AWEMAN_REAL.DLL) and patches call sites to dump game state. */
#include <stdint.h>
#include <string.h>

/* Verify an E8 rel32 call at site targeting expected_target, then redirect it to hook. */
static int rr_patch_bytes(uint8_t* site, uint32_t expected_target, void* hook) {
    int32_t rel;
    if (site[0] != 0xE8) return 0;
    memcpy(&rel, site + 1, 4);
    if ((uint32_t)((uintptr_t)(site + 5) + rel) != expected_target) return 0;
    rel = (int32_t)((uintptr_t)hook - (uintptr_t)(site + 5));
    memcpy(site + 1, &rel, 4);
    return 1;
}

#ifdef RR_PATCH_NO_WINDOWS
int rr_patch_call(uint8_t* site, uint32_t expected_target, void* hook) {
    return rr_patch_bytes(site, expected_target, hook);
}
#else
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static FILE* g_out;
static char g_scenario[256] = "unnamed";
static int g_capture_course;
static int g_frontend_probe;
/* Display level 1..5 selected through the original level popup setter
 * (0x004453f0); 0 leaves the bootstrapped startup level (display 1) alone. */
static int g_capture_level;
static int g_groups;
static int g_drive;
static int readable(uintptr_t p, size_t size);

/* Plan 10 simulation oracle: per-tick dumps of riders and cars. */
#include "sim_fields.h"
void rr_log(const char* key, long long value);
static uint32_t* sim_rand_state(void);
#ifndef RD32
#define RD32(a) (*(volatile int32_t*)(uintptr_t)(a))
#define RD8(a) (*(volatile uint8_t*)(uintptr_t)(a))
#endif
static int g_sim;
static int g_seed = -1, g_seed_applied;
static int g_race_started;
static FILE* g_record;
static FILE* g_input;
static int g_input_exhausted_tick = -1;
static uint32_t g_sim_frames;
static int g_quit_after = -1;
static int32_t g_first_tick = -1;
static int g_solo;
static int g_human;
/* RR_ORACLE_KNOCK=<tick>: knock the player off through the original crash
 * entry 0x00403d60 (reason 1, default vectors) before that tick runs. */
static int g_knock_tick;
/* RR_ORACLE_ORDER=1: per tick, the scheduler-ring order of nodes
 * within 0x1000 of the player (ord.<k> address, .t type, .p position, .l
 * lateral, .s slice, .v +0x108) and the
 * player half's address, +0x108 and +0x2c: the 0x00449e90 walk order; also
 * the race-end timer DAT_004753c4 and outcome DAT_004c5d7c (4 = arrest). */
static int g_order;
static int g_police_probe;
static int g_terrain_seeded;
static int g_terrain_trace;
static void capture_terrain_seeded_and_exit(void);
static void palette_hook_install(void);
static uint32_t g_frame;

/* Solo isolation (plan 10 task 5): freeze every scheduler node except
 * the player bike and the player rider-half. 0x00437e60 skips NULL
 * think (+0xe0) and NULL move (+0xe4), so zeroing both safely parks a
 * node against every dispatch path (scheduler and direct calls). The
 * traffic pool is nulled, so 0x0044df60 never places cars. Frozen
 * opponents still occupy the track; contact with them is deterministic.
 * The player half (think 0x00434660 at the player's position on the
 * first step) stays live. */
static void sim_freeze_others(void) {
    const uintptr_t player = (uint32_t)RD32(0x004642d8);
    const int32_t ppos = readable(player + 0x20, 4) ? (int32_t)*(uint32_t*)(player + 0x1c) : 0;
    if (!readable(0x0046dcc8, 4)) return;
    const uintptr_t S = (uint32_t)RD32(0x0046dcc8);
    if (!readable(S, 4)) return;
    const uintptr_t end = (uint32_t)*(uint32_t*)S;
    uintptr_t node = 0;
    if (readable(S + 4, 4)) {
        const uintptr_t cursor = (uint32_t)*(uint32_t*)(S + 4);
        if (readable(cursor + 4, 4)) node = (uint32_t)*(uint32_t*)(cursor + 4);
    }
    for (int guard = 0; node && node != end && guard < 512; ++guard) {
        if (!readable(node, 0xe8)) break;
        if (node != player) {
            const uint32_t think = (uint32_t)*(uint32_t*)(node + 0xe0);
            int spare = 0;
            if (think == 0x00434660 && readable(node + 0x20, 4)) {
                const int32_t npos = (int32_t)*(uint32_t*)(node + 0x1c);
                if (npos > ppos - 256 && npos < ppos + 256) spare = 1;
            }
            if (!spare) {
                DWORD old;
                if (VirtualProtect((void*)node, 0xe8, PAGE_EXECUTE_READWRITE, &old)) {
                    *(uint32_t*)(node + 0xe0) = 0;
                    *(uint32_t*)(node + 0xe4) = 0;
                    VirtualProtect((void*)node, 0xe8, old, &old);
                }
            }
        }
        node = (uint32_t)*(uint32_t*)(node + 4);
    }
    RD32(0x0049e654) = 0;
}

// Test-only bootstrap through the original race setup. Avoid waiting for the
// interactive intro/menu during capture; never modify the installed executable.
static uint32_t __fastcall hook_menu_video(int screen) {
    if (g_capture_course || g_frontend_probe) return 1;
    return ((uint32_t(__fastcall*)(int))0x00449150)(screen);
}
static void __fastcall hook_menu_enter(int screen) {
    // Course entries 1..5 perform the original menu exit themselves. Entering
    // credits first would leave its timer running after race setup freed the
    // menu surfaces, making subsequent oracle frames fault in that timer.
    ((void(__fastcall*)(int))0x00421df0)(g_capture_course ? g_capture_course
                                                          : (g_frontend_probe && screen == 7 ? 9 : screen));
    if (g_sim && g_seed >= 0 && !g_race_started) {
        // Pin the stream before race setup (rider building consumes rand):
        // identical runs then build identical grids. Only the first entry
        // seeds; later menus must not reset a live race stream.
        uint32_t* st = sim_rand_state();
        if (st) *st = (uint32_t)g_seed;
        g_seed_applied = 1;
    }
    /* Difficulty selection (RR_ORACLE_LEVEL, plan 10 scenario
     * `napa-field-l5` and police captures): the bootstrap bypasses the
     * restroom popup, so run the original level setter 0x004453f0 here -
     * byte 0x004b8a12 = level-1, qualified-course bits cleared, player level
     * bike 0x004b8a14 rebuilt from 0x004641c8. It draws no randomness (its
     * FUN_004306e0 profile sync runs only when DAT_004c5af9 is set, which the
     * offline capture build never sets), so the pinned seed is untouched.
     * Applied before FUN_004160c0 -> 0x0044b250 -> 0x00405600 reads the level. */
    if (g_capture_level && !g_race_started) ((void(__fastcall*)(int))0x004453f0)(g_capture_level);
    /* Diagnostic startup fixture only: reproduce recorded pre-race speed
     * jitter without changing any live step or RNG anchor. Six extra brains,
     * first three level speeds each. Never enabled by normal captures. */
    if (!g_race_started) {
        const char* speed_text = getenv("RR_ORACLE_EXTRA_SPEEDS");
        if (speed_text) {
            int values[18], count = 0;
            char* end;
            while (count < 18) {
                values[count++] = (int)strtol(speed_text, &end, 10);
                if (end == speed_text) {
                    count = 0;
                    break;
                }
                speed_text = end;
                if (*speed_text == ',')
                    ++speed_text;
                else
                    break;
            }
            if (count == 18 && *speed_text == 0)
                for (int i = 0; i < 18; ++i)
                    RD32(0x004c5f20 + (i / 3) * 0x204 + 0x1bc + (i % 3) * 4) = values[i];
        }
    }
    g_race_started = 1;
    palette_hook_install();
}
static uint32_t __cdecl hook_controls(void) {
    // Input files are consumed per step in hook_step (one word per tick);
    // consuming here per poll would eat words on stepless menu frames.
    if (g_input) return ((uint32_t(__cdecl*)(void))0x0044f800)();
    if (!g_drive) return ((uint32_t(__cdecl*)(void))0x0044f800)();
    uint32_t controls = 1;          // original accelerate bit
    if (g_drive == 3) return 1 | 4; // plan 11a Task 3 Step 5: accelerate + hold left (crest hunting)
    if (g_drive == 2) {
        const uintptr_t rider = *(uint32_t*)0x004642d8;
        if (readable(rider, 0x144)) {
            const uintptr_t slice = *(uint32_t*)(rider + 0x140);
            if (readable(slice, 0xd0)) {
                const int32_t x = (*(int32_t*)(rider + 0x28) >> 8) - *(int32_t*)(slice + 0xcc);
                if (x < -60)
                    controls |= 0x200;
                else if (x > 60)
                    controls |= 4;
            }
        }
    }
    return controls;
}

/* Live CRT rand state (plan 10 task 1): 0x00455eb0 advances the MSVC seed at
 * getptd()+0x14 (0x004592b0 is _getptd). DAT_0049bce8 is only live in rand
 * mode 3, so the seed hook writes here instead. */
static uint32_t* sim_rand_state(void) {
    void* ptd = ((void*(__cdecl*)(void))0x004592b0)();
    if (!ptd || !readable((uintptr_t)ptd, 0x18)) return 0;
    return (uint32_t*)((char*)ptd + 0x14);
}

/* Diagnostic rand-call counter (RR_ORACLE_RANDCOUNT=1): entry-patches
 * 0x00455eb0 (first 5 bytes are `call 0x4592b0`) with a jump to an exact
 * reimplementation that counts calls and records the tick-1 caller. */
static uint8_t g_rand_orig[5];
static uint32_t g_rand_calls, g_step_start_calls;
static uint32_t g_tick1_caller;
static int32_t g_randtrace_tick = -1;
static uint32_t g_randtrace_callers[64];
static int g_randtrace_count;
static int g_randlog_all;
static uint32_t g_randlog_tick[32768];
static uint32_t g_randlog_caller[32768];
static int g_randlog_count;
static int g_randlog_flushed;
static void dump_randlog(void) {
    char key[64];
    while (g_randlog_flushed < g_randlog_count) {
        snprintf(key, sizeof key, "randlog.%d", g_randlog_flushed);
        rr_log(key, (long long)g_randlog_tick[g_randlog_flushed] * 4294967296ll +
                        g_randlog_caller[g_randlog_flushed]);
        ++g_randlog_flushed;
    }
}
static uint32_t sim_rand_step(void) {
    uint32_t* st = sim_rand_state();
    if (!st) return 0;
    uint32_t s = *st;
    *st = s * 0x343fdu + 0x269ec3u;
    return (*st >> 16) & 0x7fff;
}
static uint32_t __cdecl hook_rand_count(void) {
    void* caller = __builtin_return_address(0);
    ++g_rand_calls;
    if (g_first_tick < 0 && !g_tick1_caller) g_tick1_caller = (uint32_t)(uintptr_t)caller;
    if (g_randtrace_tick >= 0 && RD32(0x004b7ecc) == g_randtrace_tick && g_randtrace_count < 64)
        g_randtrace_callers[g_randtrace_count++] = (uint32_t)(uintptr_t)caller;
    if (g_randlog_all && g_race_started && g_randlog_count < 32768) {
        g_randlog_tick[g_randlog_count] = (uint32_t)RD32(0x004b7ecc);
        g_randlog_caller[g_randlog_count] = (uint32_t)(uintptr_t)caller;
        ++g_randlog_count;
    }
    return sim_rand_step();
}
static void sim_patch_rand_entry(void) {
    static const uint8_t expect[5] = {0xe8, 0xfb, 0x33, 0, 0};
    uint8_t* site = (uint8_t*)0x00455eb0;
    if (memcmp(site, expect, 5)) return;
    memcpy(g_rand_orig, site, 5);
    DWORD old;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) return;
    site[0] = 0xe9;
    int32_t rel = (int32_t)((uint8_t*)hook_rand_count - (site + 5));
    memcpy(site + 1, &rel, 4);
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
}

static long long sim_field_value(uintptr_t base, uint32_t offset, uint8_t width, uint8_t is_signed) {
    if (width == 1) {
        if (!readable(base + offset, 1)) return -1;
        uint8_t v = *(volatile uint8_t*)(base + offset);
        return is_signed ? (long long)(int8_t)v : (long long)v;
    }
    if (!readable(base + offset, 4)) return -1;
    uint32_t v = *(volatile uint32_t*)(base + offset);
    return is_signed ? (long long)(int32_t)v : (long long)v;
}

/* Read-only fixed race walkers, not scene iterators sharing terrain code.
 * 0x00453030 selects section outer+0x28+active*0x1f4; 0x00452cb0
 * embeds its terrain iterator at +0xb4. All offsets below are bytes. */
static void dump_terrain_walker(const char* name, uintptr_t outer) {
    static const struct {
        const char* name;
        int offset;
    } fields[] = {{"start", 0x44}, {"end", 0x64}, {"x", 0x84}, {"height", 0x88}, {"dx", 0xc4}, {"dh", 0xc8}};
    char key[128];
    const int active = readable(outer + 4, 4) ? RD32(outer + 4) : -1;
    const uintptr_t section = active >= 0 && active <= 1 ? outer + 0x28 + (uintptr_t)active * 0x1f4 : 0;
    const uintptr_t terrain = section ? section + 0xb4 : 0;
    const uintptr_t definition = terrain && readable(terrain, 0x104) ? (uint32_t)RD32(terrain) : 0;
    const int available = definition && readable(definition, 4);
    snprintf(key, sizeof key, "ring.walk.%s.active", name);
    rr_log(key, active);
    snprintf(key, sizeof key, "ring.walk.%s.position", name);
    rr_log(key, section && readable(section + 8, 4) ? RD32(section + 8) : -1);
    snprintf(key, sizeof key, "ring.walk.%s.terrain.available", name);
    rr_log(key, available);
    if (!available) return;
    snprintf(key, sizeof key, "ring.walk.%s.terrain.position", name);
    rr_log(key, RD32(terrain + 0xc));
    snprintf(key, sizeof key, "ring.walk.%s.terrain.last_sample", name);
    rr_log(key, RD32(terrain + 0x10));
    snprintf(key, sizeof key, "ring.walk.%s.terrain.record", name);
    rr_log(key, RD32(terrain + 0x14));
    for (int point = 0; point < 8; ++point)
        for (unsigned f = 0; f < sizeof fields / sizeof fields[0]; ++f) {
            const int stride = f < 2 ? 4 : 8;
            snprintf(key, sizeof key, "ring.walk.%s.terrain.point.%d.%s", name, point, fields[f].name);
            rr_log(key, RD32(terrain + fields[f].offset + point * stride));
        }
}

/* One golden frame per simulation tick. Riders key by discovery order with
 * the player (node == DAT_004642d8) pinned to rider.0; grid-slot mapping of
 * opponents is open (plan 10 task 6). Cars and other nodes are dumped raw. */
static void dump_sim_tick(void) {
    char key[96];
    const int32_t tick = RD32(0x004b7ecc);
    if (g_first_tick < 0) {
        g_first_tick = tick;
        fprintf(g_out, "frame\t0\n");
        rr_log("course", g_capture_course);
        rr_log("level", RD8(0x004b8a12));
        rr_log("bike", -1);
        rr_log("character", -1);
        rr_log("seed", g_seed);
        rr_log("solo", g_solo);
        rr_log("ring.car_schema", 2); // byte offsets corrected from decompiler element indices
        if (g_terrain_trace) rr_log("ring.terrain.schema", 1);
        if (g_tick1_caller) rr_log("tick1_caller", (long long)g_tick1_caller);
    } else {
        fprintf(g_out, "frame\t%u\n", g_sim_frames);
    }
    rr_log("tick", tick);
    uint32_t* rst = sim_rand_state();
    rr_log("rng", rst ? (long long)*rst : -1);
    rr_log("rng_calls", (long long)(g_rand_calls - g_step_start_calls));
    if (g_randtrace_tick == tick)
        for (int i = 0; i < g_randtrace_count; ++i) {
            snprintf(key, sizeof key, "randtrace.%d", i);
            rr_log(key, (long long)g_randtrace_callers[i]);
        }
    rr_log("controls", (long long)(uint32_t)RD32(0x004b7ed0));
    /* Player-control branch proof (plan 10 Task 5 Step 2a): DAT_004753c8
     * sampled every tick. 0 = human (0x00434660 consumes the control word),
     * 1 = attract-demo AI branch. Read-only; the offline probe elsewhere
     * already save/restores this byte. */
    rr_log("player.demo_hold", (long long)RD8(0x004753c8));
    /* Ring cursors for the +0x5100 offset and zone-density timing check
     * (plan 24 Task 1 Step 2/5): front 0x004b6e50, back 0x0049f240,
     * camera 0x004b6e5c. */
    rr_log("ring.front", (long long)(int32_t)RD32(0x004b6e50));
    /* Nitro globals (0x0040b110/0x0040b1a0): charge accumulator Q8 and
     * charges left. */
    rr_log("nitro.charge", (long long)(int32_t)RD32(0x004c5f10));
    rr_log("nitro.charges", (long long)(int32_t)RD32(0x00465a34));
    rr_log("ring.back", (long long)(int32_t)RD32(0x0049f240));
    rr_log("ring.camera", (long long)(int32_t)RD32(0x004b6e5c));
    if (g_order) { /* race-end timer (0x00416010/0x00442e10) and outcome code; 4 = arrest */
        rr_log("race.end_timer", (long long)(int32_t)RD32(0x004753c4));
        rr_log("race.outcome", (long long)(int32_t)RD32(0x004c5d7c));
    }
    if (g_terrain_trace) {
        dump_terrain_walker("back", 0x0049ea20);
        dump_terrain_walker("front", 0x0049ee30);
    }
    const uintptr_t player = (uint32_t)RD32(0x004642d8);
    int rider_index = 1, node_index = 0;
    /* Scheduler ring (0x00438170, see disassembly): S = [0x46dcc8],
     * end = [S], start = [[S+4]+4], links at +0x4, think at +0xe0. */
    if (readable(0x0046dcc8, 4)) {
        const uintptr_t S = (uint32_t)RD32(0x0046dcc8);
        const uintptr_t end = (readable(S, 4)) ? (uint32_t)*(uint32_t*)S : 0;
        uintptr_t node = 0;
        if (readable(S + 4, 4)) {
            const uintptr_t cursor = (uint32_t)*(uint32_t*)(S + 4);
            if (readable(cursor + 4, 4)) node = (uint32_t)*(uint32_t*)(cursor + 4);
        }
        if (g_sim_frames == 0) {
            rr_log("probe.S", (long long)S);
            rr_log("probe.end", (long long)end);
            rr_log("probe.player", (long long)player);
            rr_log("probe.frozen_words", 1);
        }
        int addr_index = 0;
        for (int guard = 0; node && node != end && guard < 512; ++guard) {
            if (!readable(node, 0xe4)) break;
            if (g_order && g_sim_frames > 0 && player && readable(player, 0x300)) {
                const uint32_t type = (uint32_t)sim_field_value(node, 0xc4, 4, 0);
                const int32_t d = (int32_t)RD32(node + 0x1c) - (int32_t)RD32(player + 0x1c);
                if (d > -0x1000 && d < 0x1000) {
                    snprintf(key, sizeof key, "ord.%d", guard);
                    rr_log(key, (long long)node);
                    snprintf(key, sizeof key, "ord.%d.t", guard);
                    rr_log(key, type);
                    snprintf(key, sizeof key, "ord.%d.p", guard);
                    rr_log(key, (int32_t)RD32(node + 0x1c));
                    snprintf(key, sizeof key, "ord.%d.l", guard);
                    rr_log(key, (int32_t)RD32(node + 0x28));
                    snprintf(key, sizeof key, "ord.%d.s", guard);
                    rr_log(key, (long long)(uint32_t)RD32(node + 0x2c));
                    snprintf(key, sizeof key, "ord.%d.v", guard);
                    rr_log(key, (int32_t)RD32(node + 0x108));
                }
                if (node == player) {
                    const uintptr_t h = (uint32_t)RD32(player + 0x2fc);
                    rr_log("rider.0.half_addr", (long long)h);
                    if (h && readable(h, 0x110)) {
                        rr_log("rider.0.half_108", (int32_t)RD32(h + 0x108));
                        rr_log("rider.0.half_2c", (long long)(uint32_t)RD32(h + 0x2c));
                    }
                }
            }
            if (g_sim_frames == 0) {
                snprintf(key, sizeof key, "nodeaddr.%d", addr_index);
                rr_log(key, (long long)node);
                snprintf(key, sizeof key, "nodeaddr.%d.think", addr_index);
                rr_log(key, sim_field_value(node, 0xe0, 4, 0));
                snprintf(key, sizeof key, "nodeaddr.%d.position", addr_index);
                rr_log(key, sim_field_value(node, 0x1c, 4, 1));
                snprintf(key, sizeof key, "nodeaddr.%d.kind", addr_index);
                rr_log(key, sim_field_value(node, 0x73, 1, 0));
                snprintf(key, sizeof key, "nodeaddr.%d.type", addr_index);
                rr_log(key, sim_field_value(node, 0xc4, 4, 0));
                snprintf(key, sizeof key, "nodeaddr.%d.lateral", addr_index);
                rr_log(key, sim_field_value(node, 0x28, 4, 1));
                snprintf(key, sizeof key, "nodeaddr.%d.half", addr_index);
                rr_log(key, sim_field_value(node, 0x2fc, 4, 0));
                /* Plan 10 initialization evidence, including parked bikes whose
                 * think callback is zero. Previous copies and velocity words
                 * can retain different values from the current placement. */
                if (sim_field_value(node, 0xc4, 4, 0) == 4) {
                    static const uint32_t words[] = {0x30,  0x34,  0x38,  0x3c, 0x40,
                                                     0x108, 0x10c, 0x110, 0x2f4};
                    const int available = readable(node, 0x2f8);
                    snprintf(key, sizeof key, "nodeaddr.%d.frozen.available", addr_index);
                    rr_log(key, available);
                    if (available) {
                        for (unsigned w = 0; w < sizeof words / sizeof words[0]; ++w) {
                            snprintf(key, sizeof key, "nodeaddr.%d.bike_%x", addr_index, words[w]);
                            rr_log(key, sim_field_value(node, words[w], 4, 1));
                        }
                        snprintf(key, sizeof key, "nodeaddr.%d.bike_c0", addr_index);
                        rr_log(key, sim_field_value(node, 0xc0, 1, 0));
                    }
                    const uintptr_t slice = (uint32_t)sim_field_value(node, 0x2c, 4, 0);
                    uintptr_t next = (slice && readable(slice + 0x10, 4)) ? (uint32_t)RD32(slice + 0x10) : 0;
                    snprintf(key, sizeof key, "nodeaddr.%d.grid.next", addr_index);
                    rr_log(key, (long long)next);
                    const int grid_ok = next && readable(next, 0xd0);
                    snprintf(key, sizeof key, "nodeaddr.%d.grid.available", addr_index);
                    rr_log(key, grid_ok);
                    if (grid_ok) {
                        snprintf(key, sizeof key, "nodeaddr.%d.grid.next_2c", addr_index);
                        rr_log(key, RD32(next + 0x2c));
                        snprintf(key, sizeof key, "nodeaddr.%d.grid.next_cc", addr_index);
                        rr_log(key, RD32(next + 0xcc));
                    }
                    unsigned bends = 0;
                    for (; bends < 8 && next && readable(next, 0xb8); ++bends) {
                        snprintf(key, sizeof key, "nodeaddr.%d.grid.bend.%u", addr_index, bends);
                        rr_log(key, RD32(next + 0xb4));
                        next = (uint32_t)RD32(next + 0x10);
                    }
                    snprintf(key, sizeof key, "nodeaddr.%d.grid.bends", addr_index);
                    rr_log(key, bends);
                }
                snprintf(key, sizeof key, "nodeaddr.%d.net", addr_index);
                rr_log(key, sim_field_value(node, 0x324, 4, 1));
                { /* Slice road-half byte (+0x36): the 0x00449ce0 scan requires
                 * (slice36 & own36 & 3); Napa reads 7 on every node. */
                    const uintptr_t slice = (uint32_t)sim_field_value(node, 0x140, 4, 0);
                    snprintf(key, sizeof key, "nodeaddr.%d.slice36", addr_index);
                    rr_log(key,
                           (slice && readable(slice + 0x36, 1)) ? sim_field_value(slice, 0x36, 1, 0) : -1);
                }
                { /* Half speed-copy (+0x108): the 0x00449e90 closing-rate
                 * adjust reads it off candidate halves. */
                    const uintptr_t half = (uint32_t)sim_field_value(node, 0x2fc, 4, 0);
                    snprintf(key, sizeof key, "nodeaddr.%d.half108", addr_index);
                    rr_log(key,
                           (half && readable(half + 0x108, 4)) ? sim_field_value(half, 0x108, 4, 1) : -1);
                }
                snprintf(key, sizeof key, "nodeaddr.%d.w2c", addr_index);
                rr_log(key, sim_field_value(node, 0x2c, 4, 0));
                snprintf(key, sizeof key, "nodeaddr.%d.link0", addr_index);
                rr_log(key, sim_field_value(node, 0x0, 4, 0));
                snprintf(key, sizeof key, "nodeaddr.%d.link1", addr_index);
                rr_log(key, sim_field_value(node, 0x4, 4, 0));
                { /* Half slice word (+0x2c): the 0x00449ce0 walk filter. */
                    const uintptr_t half = (uint32_t)sim_field_value(node, 0x2fc, 4, 0);
                    snprintf(key, sizeof key, "nodeaddr.%d.half2c", addr_index);
                    rr_log(key, (half && readable(half + 0x2c, 4)) ? sim_field_value(half, 0x2c, 4, 0) : -1);
                }
                { /* Brain statics (header frame only): rating currents and
                 * targets (+0x0..+0xf0), ease steps (+0x12c..+0x134) and the
                 * overtake index byte (+0x165). Keys: nodeaddr.<i>.p.<off>. */
                    const uintptr_t half = (uint32_t)sim_field_value(node, 0x2fc, 4, 0);
                    const uintptr_t brain =
                        (half && readable(half + 0x1d8, 4)) ? (uint32_t)*(uint32_t*)(half + 0x1d8) : 0;
                    if (brain) {
                        for (unsigned w = 0; w < 60; ++w) {
                            snprintf(key, sizeof key, "nodeaddr.%d.p.%x", addr_index, 4 * w);
                            rr_log(key, sim_field_value(brain, 4 * w, 4, 1));
                        }
                        for (unsigned e = 0; e < 3; ++e) {
                            snprintf(key, sizeof key, "nodeaddr.%d.p.%x", addr_index, 0x12c + 4 * e);
                            rr_log(key, sim_field_value(brain, 0x12c + 4 * e, 4, 1));
                        }
                        snprintf(key, sizeof key, "nodeaddr.%d.p.165", addr_index);
                        rr_log(key, sim_field_value(brain, 0x165, 1, 0));
                        if (sim_field_value(node, 0xc4, 4, 0) == 4 && readable(brain, 0x19c)) {
                            snprintf(key, sizeof key, "nodeaddr.%d.p.18c", addr_index);
                            rr_log(key, (signed char)RD8(brain + 0x18c));
                            for (unsigned off = 0x190; off <= 0x198; off += 4) {
                                snprintf(key, sizeof key, "nodeaddr.%d.p.%x", addr_index, off);
                                rr_log(key, RD32(brain + off));
                            }
                        }
                    }
                }
                ++addr_index;
            }
            int slot = -1;
            if (node == player)
                slot = 0;
            else {
                const uint32_t think = (uint32_t)*(uint32_t*)(node + 0xe0);
                if (think == 0x00407a90 || think == 0x00434660) slot = rider_index++;
            }
            if (slot >= 0) {
                snprintf(key, sizeof key, "rider.%d.think", slot);
                rr_log(key, sim_field_value(node, 0xe0, 4, 0));
                /* Player crash body (the rider half, +0x2fc): motion words while
                 * the bike is crashed (+0x2f4 != 0); napa-cops-crashed. */
                if (slot ==
                    0) { /* the half's position word every tick: the cop reads it (0x00407f30/0x00409ca0 target) */
                    const uintptr_t half0 = (uint32_t)sim_field_value(node, 0x2fc, 4, 0);
                    if (half0 && readable(half0, 0x300)) {
                        rr_log("rider.0.half_1c", RD32(half0 + 0x1c));
                        rr_log("rider.0.half_28", RD32(half0 + 0x28));
                        rr_log("rider.0.half_ec", RD32(half0 + 0xec));
                    }
                }
                if (slot == 0 && sim_field_value(node, 0x2f4, 4, 1) != 0) {
                    const uintptr_t half = (uint32_t)sim_field_value(node, 0x2fc, 4, 0);
                    static const int offs[] = {0x1c, 0x20,  0x24,  0x28,  0xc0,  0xec,  0xf0,
                                               0xf4, 0x108, 0x10c, 0x110, 0x114, 0x118, 0x2f4};
                    if (half && readable(half, 0x300))
                        for (unsigned k = 0; k < sizeof offs / sizeof offs[0]; ++k) {
                            snprintf(key, sizeof key, "rider.0.body_%x", offs[k]);
                            rr_log(key, RD32(half + offs[k]));
                        }
                }
                /* Police marker: DAT_004642dc holds the single live cop bike
                 * (0x00406b80), so the cop is a rider slot whose node it names.
                 * Keyed only once a cop exists; absent keys read as no cop. */
                if (RD32(0x004642dc) && (uint32_t)RD32(0x004642dc) == (uint32_t)node) {
                    snprintf(key, sizeof key, "rider.%d.cop", slot);
                    rr_log(key, 1);
                    rr_log("police.slot", slot);
                    rr_log("police.cycle", RD8(0x004642e0));
                    /* Ring bend words (+0xb4 = atan(bend)<<14, 0x00454240) of the
                     * cop's slice and the next 15 (+0x10 links): the input of
                     * the curve limit 0x00409020 via 0x00449b40. */
                    {
                        uintptr_t sl = (uint32_t)sim_field_value(node, 0x140, 4, 0);
                        for (int k = 0; k < 16 && sl && readable(sl, 0xb8); ++k) {
                            snprintf(key, sizeof key, "police.bend.%d", k);
                            rr_log(key, RD32(sl + 0xb4));
                            sl = (uint32_t)RD32(sl + 0x10);
                        }
                    }
                }
                for (unsigned f = 0; f < RR_SIM_RIDER_FIELD_COUNT; ++f) {
                    const uint32_t off = rr_sim_rider_fields[f].offset;
                    /* Only bike-half words are mapped (see REVERSE_ENGINEERING.md).
                     * The slice pointer is process-specific: keep it as debug-only
                     * slice_ptr. Rider-half words (stamina/weapon/ai_*) are
                     * omitted until their object is traced. */
                    if (off != 0x1c && off != 0x28 && off != 0x20 && off != 0xf4 && off != 0xec &&
                        off != 0x23c && off != 0x2f0 && off != 0x2f4 && off != 0xc0 && off != 0x140 &&
                        off != 0x288 && off != 0xf0 && off != 0x10c && off != 0x160)
                        continue;
                    if (off == 0x140) {
                        snprintf(key, sizeof key, "rider.%d.slice_ptr", slot);
                        rr_log(key, sim_field_value(node, off, 4, 0));
                        continue;
                    }
                    snprintf(key, sizeof key, "rider.%d.%s", slot, rr_sim_rider_fields[f].name);
                    rr_log(key, sim_field_value(node, off, rr_sim_rider_fields[f].width,
                                                rr_sim_rider_fields[f].is_signed));
                }
                /* Rider-half raw window: the brain lives at *(bike+0x2fc)
                 * (weapon +0x1d4, invuln +0x1d0, AI words, lookahead +0x1dc..+0x1f0).
                 * Values are compared across runs; pointers are stable in practice. */
                const uintptr_t rh = readable(node + 0x2fc, 4) ? (uint32_t)*(uint32_t*)(node + 0x2fc) : 0;
                /* Stamina lives in the rider-table row *(half+0x1e0)
                 * (0x004050c0: +0x38 maximum, +0x3c current). */
                /* The rider's riding/attack animation block is on the half
                 * (0x00434660/0x00407f30 advance half+0x70; 0x0043ae10:
                 * +0x80 animation, +0x81 frame, +0x82 queued, +0x84
                 * deadline, +0x88 loops). The riding bike's own block only
                 * mirrors +0x81 from half+0x1e9 (0x00407a90). */
                if ((node == player || getenv("RR_ORACLE_RATINGS")) && rh && readable(rh + 0x80, 12)) {
                    for (unsigned f = 0; f < RR_SIM_RIDER_FIELD_COUNT; ++f) {
                        const uint32_t off = rr_sim_rider_fields[f].offset;
                        if (off != 0x80 && off != 0x81 && off != 0x82 && off != 0x84 && off != 0x88) continue;
                        snprintf(key, sizeof key, "rider.%d.%s", slot, rr_sim_rider_fields[f].name);
                        rr_log(key, sim_field_value(rh, off, rr_sim_rider_fields[f].width,
                                                    rr_sim_rider_fields[f].is_signed));
                    }
                }
                if (node == player && rh && readable(rh + 0x1e0, 4)) {
                    const uintptr_t row = (uint32_t)*(uint32_t*)(rh + 0x1e0);
                    if (row && readable(row + 0x3c, 4)) {
                        snprintf(key, sizeof key, "rider.%d.stamina_max", slot);
                        rr_log(key, sim_field_value(row, 0x38, 4, 1));
                        snprintf(key, sizeof key, "rider.%d.stamina_current", slot);
                        rr_log(key, sim_field_value(row, 0x3c, 4, 1));
                    }
                }
                if (rh) {
                    for (unsigned w = 0; w < 32; ++w) {
                        snprintf(key, sizeof key, "rider.%d.raw_%x", slot, 0x180 + 4 * w);
                        rr_log(key, sim_field_value(rh, 0x180 + 4 * w, 4, 1));
                    }
                }
                /* AI brain chase: rider_half+0x1d8 points at the brain struct
                 * (state +0x160, replan +0x188, targets +0x194..+0x19c, lookahead
                 * +0x1dc..+0x200). This is what 0x00407bd0 steps. */
                if (rh && readable(rh + 0x1d8, 4)) {
                    const uintptr_t brain = (uint32_t)*(uint32_t*)(rh + 0x1d8);
                    if (getenv("RR_ORACLE_RATINGS")) {
                        for (unsigned w = 0; w < 30; ++w) {
                            snprintf(key, sizeof key, "rider.%d.rating_%x", slot, 4 * w);
                            rr_log(key, sim_field_value(brain, 4 * w, 4, 1));
                        }
                    }
                    /* Through +0x1cc: the level 3/4 speed words that level-4
                     * replan reads from the jittered copies (plan 10 L5). */
                    for (unsigned w = 0; w < 38; ++w) {
                        snprintf(key, sizeof key, "rider.%d.brain_%x", slot, 0x138 + 4 * w);
                        rr_log(key, sim_field_value(brain, 0x138 + 4 * w, 4, 1));
                    }
                }
                /* Bike-half lateral chain (+0x104/+0x108/+0x10c) and yaw (+0x60):
                 * pinpoints steer-vs-bend-push divergences. */
                snprintf(key, sizeof key, "rider.%d.lat104", slot);
                rr_log(key, sim_field_value(node, 0x104, 4, 1));
                snprintf(key, sizeof key, "rider.%d.lat108", slot);
                rr_log(key, sim_field_value(node, 0x108, 4, 1));
                snprintf(key, sizeof key, "rider.%d.lat10c", slot);
                rr_log(key, sim_field_value(node, 0x10c, 4, 1));
                snprintf(key, sizeof key, "rider.%d.yaw60", slot);
                rr_log(key, sim_field_value(node, 0x60, 4, 1));
                /* Steer-rate and limit words (+0x2a0..+0x2b0), tick delta (+0x18),
                 * scheduler metadata (+0x0c next, +0x10 last, +0x14 interval),
                 * drive-ramp words (+0x240 throttle kiosk, +0x114 force,
                 * +0x260 cap, +0x23c gear): pins down launch-force gaps.
                 * Dumped only; the replay snapshot does not compare them. */
                static const uint32_t extra[] = {
                    0x18,  0x0c,  0x10,  0x14,  0x240, 0x114, 0x260, 0x23c, 0x2a0, 0x2a4,
                    0x2a8, 0x2ac, 0x29c, 0x2b0, 0x2d0, 0x2d4, 0x2dc, 0x2e8, 0x44}; // plan 11a Task 2 Step 5: rpm + caches + snap
                for (unsigned e = 0; e < sizeof extra / sizeof extra[0]; ++e) {
                    snprintf(key, sizeof key, "rider.%d.bike_%x", slot, extra[e]);
                    rr_log(key, sim_field_value(node, extra[e], 4, 1));
                }
            } else {
                snprintf(key, sizeof key, "node.%d.think", node_index);
                rr_log(key, sim_field_value(node, 0xdc, 4, 0));
                snprintf(key, sizeof key, "node.%d.position", node_index);
                rr_log(key, sim_field_value(node, 0x1c, 4, 1));
                snprintf(key, sizeof key, "node.%d.kind", node_index);
                rr_log(key, sim_field_value(node, 0x73, 1, 0));
                /* Scheduler metadata (+0x0c next, +0x10 last, +0x14 interval,
                 * +0x18 dt): 0x00437e60 rewrites last/next/dt on every
                 * scheduled run even with NULL think/move, so frozen nodes
                 * keep evolving while only the interval recompute is skipped.
                 * Keyed by node address like the ring.%x car slots; the
                 * first-frame nodeaddr block maps address to grid position. */
                if (readable(node, 0x1c) && readable(node + 0xc4, 4) &&
                    sim_field_value(node, 0xc4, 4, 0) == 4) {
                    snprintf(key, sizeof key, "sched.%x.next", node);
                    rr_log(key, sim_field_value(node, 0x0c, 4, 1));
                    snprintf(key, sizeof key, "sched.%x.last", node);
                    rr_log(key, sim_field_value(node, 0x10, 4, 1));
                    snprintf(key, sizeof key, "sched.%x.interval", node);
                    rr_log(key, sim_field_value(node, 0x14, 4, 1));
                    snprintf(key, sizeof key, "sched.%x.dt", node);
                    rr_log(key, sim_field_value(node, 0x18, 4, 1));
                }
                /* Car pool slots (0x0049e654, 16 x 0x210, template 0x00475698,
                 * type 0x10): dump rr_sim_car_fields as ring.<addr>.<field>
                 * (plan 24 Task 1 Step 5). The native replay matches slots by
                 * kind+position+lateral (see oracle-sim-test car_rows). */
                if (readable(node + 0xc4, 4) && (uint32_t)*(uint32_t*)(node + 0xc4) == 0x10) {
                    for (unsigned f = 0; f < RR_SIM_CAR_FIELD_COUNT; ++f) {
                        if (rr_sim_car_fields[f].offset == 0xffffffffu)
                            continue; // active: ring membership implies live
                        snprintf(key, sizeof key, "ring.%x.%s", node, rr_sim_car_fields[f].name);
                        rr_log(key,
                               sim_field_value(node, rr_sim_car_fields[f].offset, rr_sim_car_fields[f].width,
                                               rr_sim_car_fields[f].is_signed));
                    }
                    /* 0x00437e60 branch words (diagnostic-only, not in
                     * rr_sim_car_fields so old goldens still load): +0x2c
                     * selects the window branch (param_3), +0xe8 is the
                     * else-callback invoked on mark. sim_field_value
                     * returns -1 when unreadable. */
                    snprintf(key, sizeof key, "ring.%x.w2c", node);
                    rr_log(key, sim_field_value(node, 0x2c, 4, 0));
                    snprintf(key, sizeof key, "ring.%x.we8", node);
                    rr_log(key, sim_field_value(node, 0xe8, 4, 0));
                    /* 0x0044e710 lane-validity inputs (diagnostic-only):
                     * +0x140 slice pointer, slice +0x2c/+0x30 lane words,
                     * +0x28 slot pointer and *(slot+0x2c) link selector,
                     * +0x08/+0x18 linked slices and their +0x2c/+0x30.
                     * Pointers are process-specific (golden-only, never
                     * compared); the lane words decide the block. */
                    {
                        const uintptr_t slice = (uint32_t)sim_field_value(node, 0x140, 4, 0);
                        const int sok = slice && readable(slice + 0x30, 4);
                        long long sl2c = -1, sl30 = -1;
                        uintptr_t slot = 0, lk8 = 0, lk18 = 0;
                        long long s28v = -1, lk8_2c = -1, lk8_30 = -1, lk18_2c = -1, lk18_30 = -1;
                        if (sok) {
                            sl2c = sim_field_value(slice, 0x2c, 4, 1);
                            sl30 = sim_field_value(slice, 0x30, 4, 1);
                            slot = readable(slice + 0x28, 4) ? (uint32_t)*(uint32_t*)(slice + 0x28) : 0;
                            lk8 = readable(slice + 0x8, 4) ? (uint32_t)*(uint32_t*)(slice + 0x8) : 0;
                            lk18 = readable(slice + 0x18, 4) ? (uint32_t)*(uint32_t*)(slice + 0x18) : 0;
                            if (slot && readable(slot + 0x2c, 4)) s28v = sim_field_value(slot, 0x2c, 4, 1);
                            if (lk8 && readable(lk8 + 0x30, 4)) {
                                lk8_2c = sim_field_value(lk8, 0x2c, 4, 1);
                                lk8_30 = sim_field_value(lk8, 0x30, 4, 1);
                            }
                            if (lk18 && readable(lk18 + 0x30, 4)) {
                                lk18_2c = sim_field_value(lk18, 0x2c, 4, 1);
                                lk18_30 = sim_field_value(lk18, 0x30, 4, 1);
                            }
                        }
                        snprintf(key, sizeof key, "ring.%x.w140", node);
                        rr_log(key, (long long)slice);
                        snprintf(key, sizeof key, "ring.%x.sl2c", node);
                        rr_log(key, sl2c);
                        snprintf(key, sizeof key, "ring.%x.sl30", node);
                        rr_log(key, sl30);
                        snprintf(key, sizeof key, "ring.%x.s28v", node);
                        rr_log(key, s28v);
                        snprintf(key, sizeof key, "ring.%x.lk8", node);
                        rr_log(key, (long long)lk8);
                        snprintf(key, sizeof key, "ring.%x.lk8_2c", node);
                        rr_log(key, lk8_2c);
                        snprintf(key, sizeof key, "ring.%x.lk8_30", node);
                        rr_log(key, lk8_30);
                        snprintf(key, sizeof key, "ring.%x.lk18", node);
                        rr_log(key, (long long)lk18);
                        snprintf(key, sizeof key, "ring.%x.lk18_2c", node);
                        rr_log(key, lk18_2c);
                        snprintf(key, sizeof key, "ring.%x.lk18_30", node);
                        rr_log(key, lk18_30);
                    }
                }
                ++node_index;
            }
            node = (uint32_t)*(uint32_t*)(node + 4);
        }
    }
    rr_log("riders", rider_index);
    ++g_sim_frames;
    if (g_randlog_all) dump_randlog();
    fflush(g_out);
    if ((g_quit_after > 0 && g_sim_frames >= (uint32_t)g_quit_after) ||
        (g_input_exhausted_tick >= 0 && tick >= g_input_exhausted_tick + 0xb4)) {
        fputs("end\n", g_out);
        fclose(g_out);
        g_out = NULL;
        ExitProcess(0);
    }
}

/* Explicit oracle scenario: exercise the original offline cop constructor on
 * the live isolated race ring, preserving its ordinary allocation/AI setup. */
static void capture_police_spawn(void) {
    const uintptr_t player = (uint32_t)RD32(0x4642d8);
    char key[96];
    if (!readable(player, 0x32c) || RD32(0x4642dc)) return;
    g_police_probe = 0;
    const uintptr_t rider = (uint32_t)RD32(player + 0x2fc), personality = (uint32_t)RD32(rider + 0x1d8);
    rr_log("police.spawn.level", RD8(0x4b8a12));
    rr_log("police.spawn.cycle", RD8(0x4642e0));
    rr_log("police.spawn.player.position", RD32(player + 0x1c));
    rr_log("police.spawn.player.place", RD32(rider + 0x1fc));
    rr_log("police.spawn.player.personality", (signed char)RD8(personality + 0x165));
    const int ring = ((int(__fastcall*)(int, int))0x449910)(RD32(player + 0x2c), -0x2700);
    ((void(__fastcall*)(int, int))0x406b80)(ring, -0x2700);
    const uintptr_t cop = (uint32_t)RD32(0x4642dc);
    rr_log("police.spawn.present", cop != 0);
    if (!readable(cop, 0x32c)) return;
    const uintptr_t body = (uint32_t)RD32(cop + 0x2fc), brain = (uint32_t)RD32(body + 0x1d8),
                    stats = (uint32_t)RD32(body + 0x1e0);
    const int fields[] = {0x18,  0x1c,  0x20,  0x24,  0x28,  0xc0,  0xc4,  0xdc,  0xe0,  0xe4,
                          0xe8,  0xec,  0xf0,  0xf4,  0x148, 0x14c, 0x150, 0x154, 0x15c, 0x160,
                          0x168, 0x16c, 0x170, 0x174, 0x178, 0x17c, 0x180, 0x184, 0x188, 0x18c,
                          0x190, 0x194, 0x198, 0x1a0, 0x1a4, 0x1a8, 0x1ac, 0x1b0, 0x1b4, 0x1b8,
                          0x1d0, 0x1e0, 0x1f0, 0x200, 0x260, 0x2ec, 0x2f0, 0x2f4, 0x324, 0x328};
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; ++i)
        for (int a = 0; a < 2; ++a) {
            if (a && fields[i] >= 0x208) continue;
            snprintf(key, sizeof key, "police.spawn.%s.%x", a ? "rider" : "bike", fields[i]);
            rr_log(key, RD32((a ? body : cop) + fields[i]));
        }
    for (int i = 0; i < 0x204; i += 4) {
        snprintf(key, sizeof key, "police.spawn.brain.%x", i);
        rr_log(key, RD32(brain + i));
    }
    for (int i = 0; i < 0x40; i += 4) {
        snprintf(key, sizeof key, "police.spawn.stats.%x", i);
        rr_log(key, RD32(stats + i));
    }
    for (int a = 0; a < 2; ++a) {
        const uintptr_t obj = a ? body : cop;
        const char* who = a ? "rider" : "bike";
        snprintf(key, sizeof key, "police.spawn.%s.animation", who);
        rr_log(key, (signed char)RD8(obj + 0x80));
        snprintf(key, sizeof key, "police.spawn.%s.queued", who);
        rr_log(key, (signed char)RD8(obj + 0x82));
    }
}

/* Per-step hook at 0x0044b324 (call 0x0044b3b0). The tick counters increment
 * before the call, so the dump after the original sees the new tick. Steps
 * before the bootstrapped race entry (startup 0x0044b2e0(1) calls) are
 * skipped via g_race_started. */
static void __cdecl hook_step(void) {
    // The seed fixes the live stream before the race's first step consumes
    // it, so the golden rng after tick 0 equals f(seed) on both sides.
    if (g_sim && g_race_started && !g_seed_applied && g_seed >= 0) {
        uint32_t* st = sim_rand_state();
        if (st) *st = (uint32_t)g_seed;
        g_seed_applied = 1;
    }
    // Human branch (plan 10 Task 5 Step 2a): the course-menu bootstrap leaves
    // the attract-demo hold set, so replayed human inputs would be ignored
    // by 0x00434660. Clearing DAT_004753c8 selects the genuine player-control
    // branch (think, steer authority, skid, move-push scaling, scheduler
    // +0xc copy, wreck rule); nothing re-arms it mid-race (only 0x00421430
    // writes it, off the race path). Same fix-the-value pattern as the seed.
    if (g_sim && g_race_started && g_human) RD8(0x004753c8) = 0;
    g_step_start_calls = g_rand_calls;
    if (g_sim && g_race_started && g_solo) sim_freeze_others();
    if (g_sim && g_race_started && g_input && g_out) {
        // Control replay (plan 10): one hex word per tick, overriding the
        // frame poll. Menus produce no steps, so no words are lost there.
        // At end of file the player coasts (0); the dump quits 0xb4 ticks
        // after exhaustion.
        unsigned w;
        if (fscanf(g_input, "%x", &w) == 1)
            RD32(0x004b7ed0) = (int32_t)w;
        else {
            RD32(0x004b7ed0) = 0;
            if (g_input_exhausted_tick < 0) g_input_exhausted_tick = RD32(0x004b7ecc);
        }
    }
    if (g_knock_tick && g_sim && g_race_started && RD32(0x004b7ecc) == g_knock_tick) {
        static int32_t rider_impulse[3] = {-256, 0, 1792}, bike_impulse[3] = {0, 0, 1024};
        const uintptr_t player = (uint32_t)RD32(0x004642d8);
        if (readable(player, 0x32c))
            ((void(__fastcall*)(uintptr_t, int32_t*, int32_t*, int))0x00403d60)(player, rider_impulse,
                                                                                bike_impulse, 1);
        g_knock_tick = 0;
    }
    if (g_police_probe && g_race_started && g_out) capture_police_spawn();
    ((void(__cdecl*)(void))0x0044b3b0)();
    if (!g_sim || !g_race_started || !g_out) return;
    dump_sim_tick();
}

/* Ordinal 2 of the original AWEMAN32.DLL, loaded from AWEMAN_REAL.DLL.
   GNU ld cannot forward by ordinal, so export a jump trampoline instead. */
void* g_real2;
__attribute__((naked)) void Forward2(void) {
    __asm__("jmp *_g_real2");
}

int rr_patch_call(uint8_t* site, uint32_t expected_target, void* hook) {
    DWORD old;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) return 0;
    int ok = rr_patch_bytes(site, expected_target, hook);
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return ok;
}

static void env(const char* name, char* buf, DWORD size, const char* fallback) {
    DWORD count = GetEnvironmentVariableA(name, buf, size);
    if (!count || count >= size) {
        strncpy(buf, fallback, size - 1);
        buf[size - 1] = 0;
    }
}

void rr_log(const char* key, long long value) {
    if (g_out) fprintf(g_out, "%s\t%lld\n", key, value);
}

typedef void(__cdecl* rr_frame_fn)(void);
static rr_frame_fn const orig_frame = (rr_frame_fn)0x004439f0;
static int g_pixels, g_pixel_every = 30, g_pixel_written;
static int g_pixel_width, g_pixel_height, g_pixel_pitch;
static char g_output[MAX_PATH];
static int g_raster, g_save;
static int g_menu_screen = -1; // RR_ORACLE_MENU target screen id (plan 13 Task 6 Step 2)
static struct {
    int vertices[8], input[8], clip[4], colour, width, height;
} g_raster_cases[64];
static int g_raster_count;
static int g_view_inputs[12] = {0,      65535,  65536, 65537,  -1,         -65535,
                                -65536, -65537, 32768, -32768, 0x7fffffff, (int)0x80000000u};
static int g_view_outputs[2][12];
#define RD32(a) (*(volatile int32_t*)(uintptr_t)(a))
#define RD8(a) (*(volatile uint8_t*)(uintptr_t)(a))

static int readable(uintptr_t p, size_t size) {
    MEMORY_BASIC_INFORMATION info;
    if (!p || !VirtualQuery((void*)p, &info, sizeof info)) return 0;
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    return size <= (uintptr_t)info.BaseAddress + info.RegionSize - p;
}

// Isolated synthetic inputs exercise the original filler. These images contain
// no disc graphics and are kept as local oracle output, not embedded goldens.
static void capture_raster(uintptr_t base, int width, int height, int pitch) {
    static const int fixture[8][8] = {{1, 1, 5, 1, 5, 5, 1, 5},
                                      {3, 0, 6, 3, 3, 6, 0, 3},
                                      {-20, 40, 100, 40, 60, 40, 0, 40},
                                      {-40, -30, 80, -30, 80, 30, -40, 30},
                                      {20, 20, 20, 20, 120, 120, 20, 120},
                                      {50, 20, 100, 50, 50, 130, 10, 70},
                                      {200, 20, 500, 35, 450, 170, 150, 155},
                                      {-10, 479, 650, 479, 650, 479, -10, 479}};
    int saved[8];
    const uint32_t globals[8] = {0x004b7eb0, 0x004b7eb4, 0x004b7eb8, 0x004b7ebc,
                                 0x004c5d60, 0x004c5d64, 0x004c5d68, 0x004c5d6c};
    for (int i = 0; i < 8; ++i) saved[i] = RD32(globals[i]);
    uint32_t seed = 1;
    for (int c = 0; c < 64; ++c) {
        int vertices[8];
        if (c < 8)
            memcpy(vertices, fixture[c], sizeof vertices);
        else {
            int values[6];
            for (int i = 0; i < 6; ++i) {
                seed = seed * 1664525u + 1013904223u;
                values[i] = (int)(seed >> 16);
            }
            int top = values[0] % 900 - 300, bottom = top + 1 + values[1] % 500;
            int left_top = values[2] % 1200 - 300, left_bottom = values[3] % 1200 - 300;
            int generated[8] = {left_top,
                                top,
                                left_top + 1 + values[4] % 800,
                                top,
                                left_bottom + 1 + values[5] % 800,
                                bottom,
                                left_bottom,
                                bottom};
            for (int i = 0; i < 4; ++i) {
                int v = (i + c) % 4;
                vertices[i * 2] = generated[v * 2];
                vertices[i * 2 + 1] = generated[v * 2 + 1];
            }
            if (c % 7 == 0) {
                int v = (c / 7) % 4, j = (v + 1) % 4;
                vertices[v * 2] = vertices[j * 2];
                vertices[v * 2 + 1] = vertices[j * 2 + 1];
            }
        }
        if (c == 62) {
            int extreme[8] = {-65536, -65536, 65534, -65536, 65534, 65534, -65536, 65534};
            memcpy(vertices, extreme, sizeof extreme);
        }
        if (c == 63) {
            int huge[8] = {-16384, -16384, 16384, -16384, 16384, 16384, -16384, 16384};
            memcpy(vertices, huge, sizeof huge);
        }
        for (int row = 0; row < height; ++row) memset((void*)(base + (size_t)row * pitch), 0, width);
        int clip[4] = {0, 0, width, height - 1};
        if (c == 3) {
            clip[0] = 7;
            clip[1] = 3;
            clip[2] = 60;
            clip[3] = 25;
        }
        int minx = vertices[0], maxx = minx, miny = vertices[1], maxy = miny;
        for (int v = 1; v < 4; ++v) {
            if (vertices[2 * v] < minx) minx = vertices[2 * v];
            if (vertices[2 * v] > maxx) maxx = vertices[2 * v];
            if (vertices[2 * v + 1] < miny) miny = vertices[2 * v + 1];
            if (vertices[2 * v + 1] > maxy) maxy = vertices[2 * v + 1];
        }
        for (int i = 0; i < 4; ++i) RD32(globals[i]) = clip[i];
        RD32(0x004c5d60) = minx;
        RD32(0x004c5d64) = miny;
        RD32(0x004c5d68) = maxx;
        RD32(0x004c5d6c) = maxy;
        memcpy(g_raster_cases[c].input, vertices, sizeof vertices);
        const int colour = 20 + c;
        ((void(__fastcall*)(int*, unsigned char))0x0044d620)(vertices, (unsigned char)colour);
        memcpy(g_raster_cases[c].vertices, vertices, sizeof vertices);
        memcpy(g_raster_cases[c].clip, clip, sizeof clip);
        g_raster_cases[c].colour = colour;
        g_raster_cases[c].width = width;
        g_raster_cases[c].height = height;
        char path[1024];
        const char* slash = strrchr(g_output, '\\');
        const size_t prefix = slash ? (size_t)(slash - g_output) + 1 : 0;
        if (prefix > sizeof path - 64) continue;
        memcpy(path, g_output, prefix);
        int count = snprintf(path + prefix, sizeof path - prefix, "%s-raster%u.raw", g_scenario, (unsigned)c);
        if (count < 0 || (size_t)count >= sizeof path - prefix) continue;
        FILE* output = fopen(path, "wb");
        if (!output) continue;
        int ok = 1;
        for (int row = 0; row < height; ++row)
            if (fwrite((void*)(base + (size_t)row * pitch), 1, width, output) != (size_t)width) {
                ok = 0;
                break;
            }
        if (fclose(output)) ok = 0;
        if (ok) g_raster_count = c + 1;
    }
    for (int i = 0; i < 8; ++i) RD32(globals[i]) = saved[i];
}

// Natural-frame scene dump (before 0x00450420 flushes the display list): the ring of 128 course
// slices (0x0049f250, 0x2f8 bytes), the 33 projected entries (0x004bc800, 0x224), the per-slice
// scratch blocks (0x004ba190/0x004bb4b0), and every pending display node (0xa8 bytes) with its
// resource header and an FNV-1a hash of mip 0 so a native decoder can identify the resource.
static int g_scene_written;
static uint32_t g_scene_nodes;
static uint32_t fnv(const unsigned char* p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}
static uint32_t group_seen[4096];
static int group_count;
static int group_dimension(uint32_t encoded) {
    return encoded & 0x80000000u ? (int)(1u << (encoded & 31)) : (int)(encoded & 511);
}
static void capture_group(uint32_t group, uint32_t root, int family) {
    for (int i = 0; i < group_count; ++i)
        if (group_seen[i] == group) return;
    if (group_count >= 4096 || !readable(group, 28)) return;
    uint32_t ccb = (uint32_t)RD32(group + 8);
    if (!readable(ccb, 68)) return;
    unsigned char saved[68];
    memcpy(saved, (void*)(uintptr_t)ccb, 68);
    const int index = group_count++;
    group_seen[index] = group;
    char key[100];
    snprintf(key, sizeof key, "group.%d.family", index);
    rr_log(key, family);
    snprintf(key, sizeof key, "group.%d.offset", index);
    rr_log(key, group - root);
    static const int sizes[8][2] = {{0, 0},
                                    {65536, 65536},
                                    {7 * 65536, 4 * 65536},
                                    {16 * 65536, 8 * 65536},
                                    {32 * 65536, 16 * 65536},
                                    {64 * 65536, 32 * 65536},
                                    {128 * 65536, 64 * 65536},
                                    {256 * 65536, 512 * 65536}};
    for (int s = 0; s < 8; ++s) {
        uint32_t out =
            ((uint32_t(__fastcall*)(uint32_t, int, int))0x0044a870)(group, sizes[s][0], sizes[s][1]);
        if (!readable(out, 68)) continue;
        uint32_t res = (uint32_t)RD32(out + 8), w = 0, h = 0, hash = 0;
        if (readable(res, 60) && (uint32_t)RD32(res) == 0xbc614e) {
            w = (uint32_t)RD32(res + 32);
            h = (uint32_t)RD32(res + 36);
            uint32_t mip = (uint32_t)RD32(res + 56);
            if (w && w <= 256 && h && h <= 512 && readable(mip, w * h))
                hash = fnv((void*)(uintptr_t)mip, w * h);
        }
        snprintf(key, sizeof key, "group.%d.size.%d.width", index, s);
        rr_log(key, group_dimension((uint32_t)RD32(out + 60)));
        snprintf(key, sizeof key, "group.%d.size.%d.height", index, s);
        rr_log(key, group_dimension((uint32_t)RD32(out + 64)));
        snprintf(key, sizeof key, "group.%d.size.%d.hash", index, s);
        rr_log(key, hash);
        snprintf(key, sizeof key, "group.%d.size.%d.bytes", index, s);
        rr_log(key, w * h);
    }
    memcpy((void*)(uintptr_t)ccb, saved, 68);
}
static void walk_groups(uint32_t address, uint32_t root, int family, int is_root, int depth) {
    if (depth > 12 || !readable(address, 4)) return;
    uint32_t tag = (uint32_t)RD32(address);
    if (tag == 0x434c4750) {
        capture_group(address, root, family);
        return;
    }
    if (tag == 0x5252414e || tag == 0x414e494d || tag == 0x43414e53 || tag == 0x464f524d ||
        tag == 0x4f465353 || tag == 0x43434220 || tag == 0x504c5554)
        return;
    uint32_t count = is_root ? tag : (readable(address - 4, 4) ? (uint32_t)RD32(address - 4) : 0x1000);
    if (count >= 0x1000 || !readable(address + (is_root ? 4 : 0), count * 4)) return;
    for (uint32_t i = 0; i < count; ++i)
        walk_groups((uint32_t)RD32(address + (is_root ? 4 : 0) + i * 4), root, family, 0, depth + 1);
}
static void capture_hill_strips(void) {
    const uint32_t addresses[] = {0x49e660, 0x49e668, 0x46667c, 0x4bc7d8, 0x4bc7dc};
    int saved[5];
    for (int i = 0; i < 5; ++i) saved[i] = RD32(addresses[i]);
    int output[16][42], ccb[17] = {0}, parameters[14] = {0}, points[8];
    const int depths[] = {210, 211,  242,  243,  465,  466,  497,  977,
                          978, 1000, 1233, 1234, 2000, 4096, 8192, 256};
    ccb[15] = (int)0x80000005u;
    ccb[16] = (int)0x80000005u;
    char key[128];
    for (int c = 0; c < 128; ++c) {
        memset(output, 0, sizeof output);
        RD32(0x49e660) = (int)(uintptr_t)output;
        RD32(0x49e668) = (int)(uintptr_t)(output + 15);
        RD32(0x46667c) = c % 3 == 0 ? 640 : 320;
        RD32(0x4bc7d8) = (c * 177) % 900 - 450;
        RD32(0x4bc7dc) = 150 + c % 7 * 17;
        parameters[0] = c % 2;
        parameters[1] = depths[c % 16];
        parameters[2] = (c % 11 == 0 ? 80 : 240) * 65536;
        const int direction = c % 2 ? 1 : -1;
        parameters[3] = direction * (150 + c * 3);
        parameters[4] = c % 5 * 40;
        parameters[5] = parameters[3] + direction * (200 + c % 7 * 50);
        parameters[6] = c % 7 * 30;
        parameters[7] = parameters[5] + (c % 9 - 4) * 50;
        parameters[8] = parameters[6] + (c % 5 - 2) * 20;
        parameters[9] = parameters[3] + (c % 7 - 3) * 50;
        parameters[10] = parameters[4] + (c % 9 - 4) * 20;
        if (c >= 112) {
            parameters[3] = direction * 50000;
            parameters[5] = direction * 55000;
            parameters[7] = direction * 60000;
            parameters[9] = direction * 53000;
        }
        for (int i = 0; i < 4; ++i) {
            points[i * 2] = ((c * 31 + i * 77) % 800 - 200) * 65536 + 1777 * i;
            points[i * 2 + 1] = ((c * 13 + i * 31) % 300 - 30) * 65536 + i * 911;
        }
        parameters[11] = (int)(uintptr_t)ccb;
        parameters[12] = (int)(uintptr_t)points;
        parameters[13] = c % 4 == 0;
#define STRIP_LOG(label, value)                                                                              \
    do {                                                                                                     \
        snprintf(key, sizeof key, "hill.strip.%d.%s", c, label);                                             \
        rr_log(key, value);                                                                                  \
    } while (0)
        STRIP_LOG("width", RD32(0x46667c));
        STRIP_LOG("camera_x", RD32(0x4bc7d8));
        STRIP_LOG("camera_height", RD32(0x4bc7dc));
        for (int i = 0; i < 11; ++i) {
            snprintf(key, sizeof key, "hill.strip.%d.input.%d", c, i);
            rr_log(key, parameters[i]);
        }
        STRIP_LOG("seam", parameters[13]);
        for (int i = 0; i < 8; ++i) {
            snprintf(key, sizeof key, "hill.strip.%d.projected.%d", c, i);
            rr_log(key, points[i]);
        }
        ((void(__fastcall*)(int*))0x44bd40)(parameters);
        const int count = ((uintptr_t)RD32(0x49e660) - (uintptr_t)output) / 168;
        STRIP_LOG("count", count);
        for (int n = 0; n < count && n < 16; ++n)
            for (int i = 0; i < 8; ++i) {
                snprintf(key, sizeof key, "hill.strip.%d.%d.point.%d", c, n, i);
                rr_log(key, output[n][17 + i]);
            }
#undef STRIP_LOG
    }
    rr_log("hill.strip.count", 128);
    for (int i = 0; i < 5; ++i) RD32(addresses[i]) = saved[i];
}
static void capture_hill_panels(void) {
    const uint32_t addresses[] = {0x49e660, 0x49e668, 0x46667c, 0x4bc7d8, 0x4bc7dc, 0x466678};
    int saved[6];
    for (int i = 0; i < 6; ++i) saved[i] = RD32(addresses[i]);
    unsigned char scratch[64];
    memcpy(scratch, (void*)0x4c5ce0, 64);
    const unsigned char overlap = RD8(0x468960);
    int output[40][42], current[65], previous[65], sub[96], old_sub[96], entry[8], cache[36], ccb[17],
        group[8], row[1];
    memset(ccb, 0, sizeof ccb);
    memset(group, 0, sizeof group);
    group[0] = 0x434c4750;
    group[2] = (int)(uintptr_t)ccb;
    group[7] = (int)(uintptr_t)row;
    row[0] = 17;
    ((unsigned char*)group)[21] = 7;
    ((unsigned char*)group)[26] = 5;
    ((unsigned char*)group)[27] = 5;
    const int depths[] = {465, 466, 977, 978, 1233, 1234, 2000, 6000};
    char key[128];
    for (int c = 0; c < 256; ++c) {
        memset(current, 0, sizeof current);
        memset(previous, 0, sizeof previous);
        memset(sub, 0, sizeof sub);
        memset(old_sub, 0, sizeof old_sub);
        memset(entry, 0, sizeof entry);
        memset(cache, 0, sizeof cache);
        memset(output, 0, sizeof output);
        for (int i = 1; i < 9; ++i) cache[i] = (int)(uintptr_t)group;
        const int side = c % 2, direction = side ? 1 : -1, width = c % 3 == 0 ? 640 : 320;
        RD32(0x46667c) = width;
        RD32(0x4bc7d8) = (c * 17) % 200 - 100;
        RD32(0x4bc7dc) = 150 + c % 7 * 17;
        RD8(0x468960) = c % 4 < 2;
        current[side] = (int)(uintptr_t)previous;
        current[2] = (int)(uintptr_t)entry;
        current[3] = (int)(uintptr_t)sub;
        current[63] = (int)(uintptr_t)cache;
        previous[3] = (int)(uintptr_t)old_sub;
        current[6] = depths[c % 8];
        previous[6] = current[6] + 256;
        entry[4] = (c % 13 == 0 ? 80 : 240) * 65536;
        ((unsigned char*)old_sub)[0x3c] = (c / 8) % 5;
        current[4] = (c * 71) % 400 - 200;
        previous[4] = current[4] + c % 7 * 10;
        current[5] = c % 5 * 10;
        previous[5] = current[5] + c % 3 * 5;
        for (int i = 0; i < 4; ++i) {
            const int nearx = width / 2 + direction * (70 + i * 50) - c % 11 * 20,
                      farx = width / 2 + direction * (30 + i * 20) - c % 7 * 10;
            const int near_y = 160 + i * (c % 9 - 4) * 7, far_y = 100 + i * (c % 7 - 3) * 5;
            const int offset = 0x60 + side * 8 + (i ? i * 16 : 0);
            current[offset / 4] = nearx * 65536 + c * 11;
            current[offset / 4 + 1] = near_y * 65536 + c * 7;
            previous[offset / 4] = farx * 65536 + c * 13;
            previous[offset / 4 + 1] = far_y * 65536 + c * 17;
            if (i == 0) {
                sub[0xe8 / 4 + side] = direction * 350;
                old_sub[0xe8 / 4 + side] = direction * 350;
            } else {
                const int p = 0xf0 + (i - 1) * 16 + side * 8;
                sub[p / 4] = direction * (350 + i * 500);
                old_sub[p / 4] = direction * (350 + i * 500) + c % 5 * 50;
                sub[p / 4 + 1] = (c % 7 - 3) * i * 40;
                old_sub[p / 4 + 1] = (c % 5 - 2) * i * 40;
            }
        }
#define HILL_LOG(label, value)                                                                               \
    do {                                                                                                     \
        snprintf(key, sizeof key, "hill.panels.%d.%s", c, label);                                            \
        rr_log(key, value);                                                                                  \
    } while (0)
        HILL_LOG("side", side);
        HILL_LOG("width", width);
        HILL_LOG("camera_x", RD32(0x4bc7d8));
        HILL_LOG("camera_height", RD32(0x4bc7dc));
        HILL_LOG("overlap", RD8(0x468960));
        HILL_LOG("occlusion", entry[4]);
        HILL_LOG("far_type", ((unsigned char*)old_sub)[0x3c]);
        HILL_LOG("near_depth", current[6]);
        HILL_LOG("far_depth", previous[6]);
        for (int i = 0; i < 4; ++i) {
            const int p = 0x60 + side * 8 + (i ? i * 16 : 0),
                      w = i == 0 ? 0xe8 + side * 4 : 0xf0 + (i - 1) * 16 + side * 8;
            for (int axis = 0; axis < 2; ++axis) {
                snprintf(key, sizeof key, "hill.panels.%d.near.projected.%d", c, i * 2 + axis);
                rr_log(key, current[p / 4 + axis]);
                snprintf(key, sizeof key, "hill.panels.%d.far.projected.%d", c, i * 2 + axis);
                rr_log(key, previous[p / 4 + axis]);
                const int a = axis == 0 ? sub[w / 4] + current[4] : (i ? sub[w / 4 + 1] : 0) + current[5];
                const int b =
                    axis == 0 ? old_sub[w / 4] + previous[4] : (i ? old_sub[w / 4 + 1] : 0) + previous[5];
                snprintf(key, sizeof key, "hill.panels.%d.near.world.%d", c, i * 2 + axis);
                rr_log(key, a);
                snprintf(key, sizeof key, "hill.panels.%d.far.world.%d", c, i * 2 + axis);
                rr_log(key, b);
            }
        }
        RD32(0x49e660) = (int)(uintptr_t)output;
        RD32(0x49e668) = (int)(uintptr_t)(output + 39);
        ((void(__fastcall*)(int*, int, int))0x41c390)(current, -1, side);
        const int count = ((uintptr_t)RD32(0x49e660) - (uintptr_t)output) / 168;
        HILL_LOG("count", count);
        for (int n = 0; n < count && n < 40; ++n) {
            const unsigned char* b = (void*)output[n];
            snprintf(key, sizeof key, "hill.panels.%d.%d.flip_u", c, n);
            rr_log(key, b[100]);
            snprintf(key, sizeof key, "hill.panels.%d.%d.flip_v", c, n);
            rr_log(key, b[101]);
            for (int i = 0; i < 8; ++i) {
                snprintf(key, sizeof key, "hill.panels.%d.%d.point.%d", c, n, i);
                rr_log(key, output[n][17 + i]);
            }
        }
#undef HILL_LOG
        // Exercise the neighboring shoulder builder using the same owned test group.
        ((unsigned char*)sub)[0x3c] = 4;
        ((unsigned char*)sub)[0x34] = c % 5 != 0;
        sub[0xe0 / 4 + side] = direction * 250;
        old_sub[0xe0 / 4 + side] = direction * 250;
        old_sub[0x14c / 4 + side * 3] = direction * 450;
        current[0x50 / 4 + side * 2] = current[0x60 / 4 + side * 2] - direction * 20 * 65536;
        previous[0x50 / 4 + side * 2] = previous[0x60 / 4 + side * 2] - direction * 10 * 65536;
        RD32(0x466678) = 240;
#define SHOULDER_LOG(label, value)                                                                           \
    do {                                                                                                     \
        snprintf(key, sizeof key, "shoulder.%d.%s", c, label);                                               \
        rr_log(key, value);                                                                                  \
    } while (0)
        SHOULDER_LOG("side", side);
        SHOULDER_LOG("width", width);
        SHOULDER_LOG("height", 240);
        SHOULDER_LOG("near_depth", current[6]);
        SHOULDER_LOG("far_depth", previous[6]);
        SHOULDER_LOG("lanes", c % 5 != 0);
        SHOULDER_LOG("camera_x", RD32(0x4bc7d8));
        SHOULDER_LOG("camera_height", RD32(0x4bc7dc));
        const int old_type = ((unsigned char*)old_sub)[0x3c],
                  outer = (old_type == 0 || old_type == 4) ? 0x60 : 0x70;
        const int sx[4] = {previous[0x50 / 4 + side * 2], previous[outer / 4 + side * 2],
                           current[0x60 / 4 + side * 2], current[0x50 / 4 + side * 2]};
        const int wx[4] = {old_sub[0xe0 / 4 + side] + previous[4],
                           old_sub[(outer == 0x60 ? 0xe8 + side * 4 : 0x14c + side * 12) / 4] + previous[4],
                           sub[0xe8 / 4 + side] + current[4], sub[0xe0 / 4 + side] + current[4]};
        for (int i = 0; i < 4; ++i) {
            const int sy = i < 2 ? previous[0x64 / 4 + side * 2] : current[0x64 / 4 + side * 2],
                      wy = i < 2 ? previous[5] : current[5];
            snprintf(key, sizeof key, "shoulder.%d.projected.%d", c, i * 2);
            rr_log(key, sx[i]);
            snprintf(key, sizeof key, "shoulder.%d.projected.%d", c, i * 2 + 1);
            rr_log(key, sy);
            snprintf(key, sizeof key, "shoulder.%d.world.%d", c, i * 2);
            rr_log(key, wx[i]);
            snprintf(key, sizeof key, "shoulder.%d.world.%d", c, i * 2 + 1);
            rr_log(key, wy);
        }
        memset(output, 0, sizeof output);
        RD32(0x49e660) = (int)(uintptr_t)output;
        ((void(__fastcall*)(int*, int))0x41d260)(current, side);
        const int shoulders = ((uintptr_t)RD32(0x49e660) - (uintptr_t)output) / 168;
        SHOULDER_LOG("count", shoulders);
        for (int n = 0; n < shoulders && n < 40; ++n) {
            const unsigned char* b = (void*)output[n];
            snprintf(key, sizeof key, "shoulder.%d.%d.flip_u", c, n);
            rr_log(key, b[100]);
            snprintf(key, sizeof key, "shoulder.%d.%d.flip_v", c, n);
            rr_log(key, b[101]);
            for (int i = 0; i < 8; ++i) {
                snprintf(key, sizeof key, "shoulder.%d.%d.point.%d", c, n, i);
                rr_log(key, output[n][17 + i]);
            }
        }
#undef SHOULDER_LOG
    }
    rr_log("hill.panels.count", 256);
    memcpy((void*)0x4c5ce0, scratch, 64);
    RD8(0x468960) = overlap;
    for (int i = 0; i < 6; ++i) RD32(addresses[i]) = saved[i];
}
static void capture_building_panels(void) {
    const uint32_t addresses[] = {0x49e660, 0x49e668, 0x46667c, 0x4bc7d8, 0x4bc7dc};
    int saved[5];
    for (int i = 0; i < 5; ++i) saved[i] = RD32(addresses[i]);
    int output[20][42], current[65], previous[65], sub[96], old_sub[96], cache[36], old_cache[36], ccb[17],
        group[8], row[1], pixel[16], entry[8];
    memset(ccb, 0, sizeof ccb);
    memset(group, 0, sizeof group);
    memset(pixel, 0, sizeof pixel);
    ccb[2] = (int)(uintptr_t)pixel;
    ccb[15] = 5;
    ccb[16] = 5;
    group[0] = 0x434c4750;
    group[2] = (int)(uintptr_t)ccb;
    group[7] = (int)(uintptr_t)row;
    row[0] = (int)(uintptr_t)pixel;
    ((unsigned char*)group)[21] = 7;
    ((unsigned char*)group)[26] = 5;
    ((unsigned char*)group)[27] = 5;
    char key[128];
    for (int c = 0; c < 128; ++c) {
        memset(current, 0, sizeof current);
        memset(previous, 0, sizeof previous);
        memset(sub, 0, sizeof sub);
        memset(old_sub, 0, sizeof old_sub);
        memset(cache, 0, sizeof cache);
        memset(old_cache, 0, sizeof old_cache);
        memset(output, 0, sizeof output);
        const int side = c % 2, direction = side ? 1 : -1, width = c % 3 ? 320 : 640;
        RD32(0x46667c) = width;
        current[side] = (int)(uintptr_t)previous;
        current[3] = (int)(uintptr_t)sub;
        previous[3] = (int)(uintptr_t)old_sub;
        current[63] = (int)(uintptr_t)cache;
        previous[63] = (int)(uintptr_t)old_cache;
        cache[1] = cache[2] = old_cache[1] = old_cache[2] = (int)(uintptr_t)group;
        ((unsigned char*)sub)[0x148 + side] = c % 7 ? 0 : 255;
        ((unsigned char*)old_sub)[0x3c] = (c / 32) % 5;
        current[0x60 / 4 + side * 2] = (width / 2 + direction * (90 - c % 11 * 15)) * 65536 + c * 9;
        previous[0x60 / 4 + side * 2] = (width / 2 + direction * (40 - c % 13 * 9)) * 65536 + c * 7;
        current[0x54 / 4 + side * 2] = 180 * 65536 + c * 3;
        previous[0x54 / 4 + side * 2] = (100 + c % 5 * 25) * 65536 + c * 5;
        current[0x74 / 4 + side * 2] = c % 7 * 15 * 65536;
        current[0x84 / 4 + side * 2] = c % 5 * 11 * 65536;
        previous[0x74 / 4 + side * 2] = (c / 3) % 7 * 17 * 65536;
        current[0x70 / 4 + side * 2] = direction * current[0x74 / 4 + side * 2] / 2;
        current[0x80 / 4 + side * 2] = direction * current[0x84 / 4 + side * 2] / 2;
        previous[0x70 / 4 + side * 2] = direction * previous[0x74 / 4 + side * 2] / 2;
        previous[0x80 / 4 + side * 2] = direction * (c % 4) * 9 * 65536;
        current[0x90 / 4 + side * 2] = c % 3 == 0;
#define BUILD_LOG(label, value)                                                                              \
    do {                                                                                                     \
        snprintf(key, sizeof key, "building.%d.%s", c, label);                                               \
        rr_log(key, value);                                                                                  \
    } while (0)
        BUILD_LOG("side", side);
        BUILD_LOG("width", width);
        BUILD_LOG("selector", ((unsigned char*)sub)[0x148 + side]);
        BUILD_LOG("far_type", ((unsigned char*)old_sub)[0x3c]);
        BUILD_LOG("cut_roof", current[0x90 / 4 + side * 2]);
        const int offsets[] = {0x60, 0x54, 0x74, 0x84, 0x70, 0x80};
        for (int i = 0; i < 6; ++i) {
            snprintf(key, sizeof key, "building.%d.near.%d", c, i);
            rr_log(key, current[offsets[i] / 4 + side * 2]);
            snprintf(key, sizeof key, "building.%d.far.%d", c, i);
            rr_log(key, previous[offsets[i] / 4 + side * 2]);
        }
        RD32(0x49e660) = (int)(uintptr_t)output;
        RD32(0x49e668) = (int)(uintptr_t)(output + 19);
        ((void(__fastcall*)(int*, int))0x40c770)(current, side);
        int count = ((uintptr_t)RD32(0x49e660) - (uintptr_t)output) / 168;
        BUILD_LOG("count", count);
        for (int n = 0; n < count && n < 20; ++n) {
            snprintf(key, sizeof key, "building.%d.%d.flip_u", c, n);
            rr_log(key, ((unsigned char*)output[n])[100]);
            snprintf(key, sizeof key, "building.%d.%d.flip_v", c, n);
            rr_log(key, ((unsigned char*)output[n])[101]);
            for (int i = 0; i < 8; ++i) {
                snprintf(key, sizeof key, "building.%d.%d.point.%d", c, n, i);
                rr_log(key, output[n][17 + i]);
            }
        }
        // Raised shoulder and vertical curb beside the facade.
        memset(output, 0, sizeof output);
        memset(entry, 0, sizeof entry);
        memset(current, 0, sizeof current);
        memset(previous, 0, sizeof previous);
        memset(sub, 0, sizeof sub);
        memset(old_sub, 0, sizeof old_sub);
        const int depths[] = {465, 466, 977, 978, 1233, 1234, 2000, 6000};
        const int nd = depths[c % 8], fd = nd + 256, ns = RD32(0x46f098 + (nd & ~1) * 2),
                  fs = RD32(0x46f098 + (fd & ~1) * 2);
        RD32(0x4bc7d8) = 0;
        RD32(0x4bc7dc) = 150;
        current[side] = (int)(uintptr_t)previous;
        current[2] = (int)(uintptr_t)entry;
        current[3] = (int)(uintptr_t)sub;
        previous[3] = (int)(uintptr_t)old_sub;
        current[63] = (int)(uintptr_t)cache;
        for (int i = 1; i < 12; ++i) cache[i] = (int)(uintptr_t)group;
        current[6] = nd;
        previous[6] = fd;
        entry[4] = (c % 9 ? 240 : 80) * 65536;
        current[4] = c % 11 * 40 - 200;
        previous[4] = current[4] + c % 5 * 20;
        current[5] = c % 5 * 7;
        previous[5] = c % 7 * 5;
        const int nh = c % 4 * 8, fh = c % 6 * 5;
        sub[0xe0 / 4 + side] = old_sub[0xe0 / 4 + side] = direction * 250;
        sub[0x138 / 4 + side] = 100 + c % 5 * 30;
        old_sub[0x138 / 4 + side] = 100 + c % 7 * 20;
        sub[0x140 / 4 + side] = nh;
        old_sub[0x140 / 4 + side] = fh;
        current[0x50 / 4 + side * 2] = (current[4] + direction * 250) * ns + width / 2 * 65536;
        previous[0x50 / 4 + side * 2] = (previous[4] + direction * 250) * fs + width / 2 * 65536;
        current[0x60 / 4 + side * 2] = current[0x50 / 4 + side * 2] + direction * sub[0x138 / 4 + side] * ns;
        previous[0x60 / 4 + side * 2] =
            previous[0x50 / 4 + side * 2] + direction * old_sub[0x138 / 4 + side] * fs;
        current[0x4c / 4] = (150 - current[5]) * ns + 90 * 65536;
        previous[0x4c / 4] = (150 - previous[5]) * fs + 90 * 65536;
        current[0x54 / 4 + side * 2] = current[0x4c / 4] - nh * ns;
        previous[0x54 / 4 + side * 2] = previous[0x4c / 4] - fh * fs;
#define BS_LOG(label, value)                                                                                 \
    do {                                                                                                     \
        snprintf(key, sizeof key, "building.shoulder.%d.%s", c, label);                                      \
        rr_log(key, value);                                                                                  \
    } while (0)
        BS_LOG("side", side);
        BS_LOG("width", width);
        BS_LOG("near_depth", nd);
        BS_LOG("far_depth", fd);
        BS_LOG("ceiling", entry[4]);
        BS_LOG("near_base", current[0x4c / 4]);
        BS_LOG("far_base", previous[0x4c / 4]);
        BS_LOG("near_world_base", current[5]);
        BS_LOG("far_world_base", previous[5]);
        const int px[] = {previous[0x50 / 4 + side * 2], previous[0x60 / 4 + side * 2],
                          current[0x60 / 4 + side * 2], current[0x50 / 4 + side * 2]};
        const int wx[] = {
            previous[4] + direction * 250, previous[4] + direction * (250 + old_sub[0x138 / 4 + side]),
            current[4] + direction * (250 + sub[0x138 / 4 + side]), current[4] + direction * 250};
        for (int i = 0; i < 4; ++i) {
            snprintf(key, sizeof key, "building.shoulder.%d.projected.%d", c, i * 2);
            rr_log(key, px[i]);
            snprintf(key, sizeof key, "building.shoulder.%d.projected.%d", c, i * 2 + 1);
            rr_log(key, i < 2 ? previous[0x54 / 4 + side * 2] : current[0x54 / 4 + side * 2]);
            snprintf(key, sizeof key, "building.shoulder.%d.world.%d", c, i * 2);
            rr_log(key, wx[i]);
            snprintf(key, sizeof key, "building.shoulder.%d.world.%d", c, i * 2 + 1);
            rr_log(key, i < 2 ? previous[5] + fh : current[5] + nh);
        }
        RD32(0x49e660) = (int)(uintptr_t)output;
        ((void(__fastcall*)(int*, int, int))0x40bde0)(current, side, -1);
        count = ((uintptr_t)RD32(0x49e660) - (uintptr_t)output) / 168;
        BS_LOG("count", count);
        for (int n = 0; n < count && n < 20; ++n) {
            snprintf(key, sizeof key, "building.shoulder.%d.%d.flip_u", c, n);
            rr_log(key, ((unsigned char*)output[n])[100]);
            snprintf(key, sizeof key, "building.shoulder.%d.%d.flip_v", c, n);
            rr_log(key, ((unsigned char*)output[n])[101]);
            for (int i = 0; i < 8; ++i) {
                snprintf(key, sizeof key, "building.shoulder.%d.%d.point.%d", c, n, i);
                rr_log(key, output[n][17 + i]);
            }
        }
#undef BS_LOG
#undef BUILD_LOG
    }
    rr_log("building.count", 128);
    for (int i = 0; i < 5; ++i) RD32(addresses[i]) = saved[i];
}
static void capture_actor_selection(uint32_t*, int, int, int);
static void capture_vehicle_descriptors(void) {
    const int count = RD8(0x474d88);
    char key[128];
    uint32_t output[5];
    rr_log("vehicle.graphics.count", count);
    for (int v = 0; v < count; ++v) {
        uint32_t* pair = (uint32_t*)(0x474db8 + v * 8);
        uint32_t* snac = (uint32_t*)(uintptr_t)pair[0];
        const uint32_t groups = snac[snac[2] == 1 ? 11 : 3];
        uint32_t* ccb = (uint32_t*)(uintptr_t)RD32(pair[1] + 12);
        uint32_t saved[20];
        memcpy(saved, ccb, sizeof saved);
        snprintf(key, sizeof key, "vehicle.graphics.%d.mask", v);
        rr_log(key, (uint32_t)RD32(0x4b8fe0 + v * 4));
        snprintf(key, sizeof key, "vehicle.graphics.%d.groups", v);
        rr_log(key, groups);
        for (uint32_t g = 0; g < groups; ++g) {
            uint32_t* metadata = ((uint32_t*(__fastcall*)(uint32_t*, int))0x40b5a0)(pair, g);
            snprintf(key, sizeof key, "vehicle.graphics.%d.%u.count", v, g);
            rr_log(key, metadata[1]);
            for (uint32_t f = 0; f < metadata[1]; ++f) {
                uint32_t flags = 0;
                uint32_t* selected =
                    ((uint32_t*(__fastcall*)(uint32_t*, int, int, uint32_t*))0x40b6f0)(pair, g, f, &flags);
                snprintf(key, sizeof key, "vehicle.graphics.%d.%u.%u.frame", v, g, f);
                rr_log(key, ((uintptr_t)selected - pair[1] - 28) / 28);
                snprintf(key, sizeof key, "vehicle.graphics.%d.%u.%u.flags", v, g, f);
                rr_log(key, flags);
            }
        }
        for (int code = 0; code < 3; ++code) {
            memset(output, 0, sizeof output);
            const int available = ((uint32_t(__fastcall*)(uint32_t*, int*, uint32_t*))0x40b200)(
                                      pair, (int*)(0x474da0 + code * 8), output) &
                                  255;
            snprintf(key, sizeof key, "vehicle.graphics.%d.code.%d.available", v, code);
            rr_log(key, available);
            for (int b = 0; b < 16; ++b) {
                snprintf(key, sizeof key, "vehicle.graphics.%d.code.%d.byte.%d", v, code, b);
                rr_log(key, ((unsigned char*)output)[b]);
            }
            if (available) capture_actor_selection(output, v + 2, v, code);
        }
        memcpy(ccb, saved, sizeof saved);
    }
}
static int32_t* arrest_probe_cop;
static int __fastcall arrest_probe_search(int unused) {
    (void)unused;
    return (int)(uintptr_t)arrest_probe_cop;
}
static void capture_police_arrest(void) {
    unsigned char* search = (unsigned char*)0x407860;
    const unsigned char expected[5] = {0x53, 0x56, 0x57, 0x8b, 0xf1};
    unsigned char saved[5];
    DWORD old;
    if (memcmp(search, expected, 5) || !VirtualProtect(search, 5, PAGE_EXECUTE_READWRITE, &old)) {
        rr_log("police.arrest.count", 0);
        return;
    }
    memcpy(saved, search, 5);
    search[0] = 0xe9;
    const int32_t jump = (int32_t)((uintptr_t)arrest_probe_search - (uintptr_t)(search + 5));
    memcpy(search + 1, &jump, 4);
    FlushInstructionCache(GetCurrentProcess(), search, 5);
    const uint32_t tick = RD32(0x4b7ecc), timer = RD32(0x4753c4), mode = RD32(0x4c5d7c);
    const unsigned char ended = RD8(0x4753b4), net = RD8(0x4c5af9);
    const int speeds[] = {-1800, -1799, -1, 0, 299, 300, 1799, 1800}, positions[] = {-512, -511, 0, 511, 512},
              relative[] = {-5000, -4999, 0, 4999, 5000};
    int32_t bike[256], rider[256], cop[256];
    char key[128];
    RD32(0x4b7ecc) = 100;
    RD8(0x4c5af9) = 0;
    for (int c = 0; c < 1024; ++c) {
        memset(bike, 0, sizeof bike);
        memset(rider, 0, sizeof rider);
        memset(cop, 0, sizeof cop);
        bike[0x2fc / 4] = (int32_t)(uintptr_t)rider;
        bike[0x324 / 4] = c % 7 == 0 ? 1 : c % 11 == 0 ? -1 : 0;
        bike[0x2f4 / 4] = c % 3 == 0 ? 2 : 0;
        bike[0xec / 4] = speeds[c % 8];
        cop[0xec / 4] = bike[0xec / 4] + relative[c / 8 % 5];
        rider[0x1c / 4] = 0x5000;
        cop[0x1c / 4] = 0x5000 + positions[c / 40 % 5];
        ((unsigned char*)rider)[0xc0] = c % 17 == 0 ? 0x10 : 0;
        rider[0x78 / 4] = RD32(0x474d78);
        rider[0x80 / 4] = 2;
        ((signed char*)rider)[0x82] = -1;
        rider[0x84 / 4] = -1;
        arrest_probe_cop = c % 13 == 0 ? NULL : cop;
        RD32(0x4753c4) = 0;
        RD8(0x4753b4) = 0;
        RD32(0x4c5d7c) = 1;
#define PA_LOG(label, value)                                                                                 \
    do {                                                                                                     \
        snprintf(key, sizeof key, "police.arrest.%d.%s", c, label);                                          \
        rr_log(key, value);                                                                                  \
    } while (0)
        PA_LOG("owner", bike[0x324 / 4]);
        PA_LOG("crash", bike[0x2f4 / 4]);
        PA_LOG("rider_flags", ((unsigned char*)rider)[0xc0]);
        PA_LOG("found", arrest_probe_cop != NULL);
        PA_LOG("bike_speed", bike[0xec / 4]);
        PA_LOG("cop_speed", cop[0xec / 4]);
        PA_LOG("rider_position", rider[0x1c / 4]);
        PA_LOG("cop_position", cop[0x1c / 4]);
        fflush(g_out);
        ((void(__fastcall*)(int32_t*))0x407f30)(bike);
        PA_LOG("arrested", RD32(0x4c5d7c) == 4);
        PA_LOG("delay", RD32(0x4753c4));
#undef PA_LOG
    }
    rr_log("police.arrest.count", 1024);
    fflush(g_out);
    RD32(0x4b7ecc) = tick;
    RD32(0x4753c4) = timer;
    RD32(0x4c5d7c) = mode;
    RD8(0x4753b4) = ended;
    RD8(0x4c5af9) = net;
    rr_log("police.arrest.search_address", (uintptr_t)search);
    rr_log("police.arrest.saved_address", (uintptr_t)saved);
    fflush(g_out);
    memcpy(search, saved, 5);
    rr_log("police.arrest.bytes_restored", 1);
    fflush(g_out);
    FlushInstructionCache(GetCurrentProcess(), search, 5);
    rr_log("police.arrest.cache_flushed", 1);
    fflush(g_out);
    DWORD ignored;
    VirtualProtect(search, 5, old, &ignored);
    arrest_probe_cop = NULL;
    rr_log("police.arrest.restored", 1);
    fflush(g_out);
}
static void capture_crash_remount(void) {
    const uint32_t saved_landmark = RD32(0x4b6e58), saved_cop = RD32(0x4642dc), saved_tick = RD32(0x4b7ecc);
    const unsigned char saved_net = RD8(0x4c5b00), saved_net2 = RD8(0x4c5af9);
    int32_t bike[256], actor[256], stamina[64], ring[4];
    char key[128];
    const int offsets[] = {0x1c,  0x20,  0x24,  0x28,  0x2c,  0x30,  0x34,  0x38,  0x3c,  0x40,  0x58,  0x5c,
                           0x60,  0xc0,  0xec,  0xf0,  0xf4,  0x104, 0x108, 0x10c, 0x110, 0x114, 0x118, 0x134,
                           0x138, 0x13c, 0x140, 0x154, 0x174, 0x178, 0x17c, 0x244, 0x288, 0x2f4};
    RD32(0x4b6e58) = 0x5000;
    RD32(0x4642dc) = 0;
    RD8(0x4c5b00) = RD8(0x4c5af9) = 0;
    for (int c = 0; c < 64; ++c) {
        for (int i = 0; i < 256; ++i) {
            bike[i] = (c + 1) * 71 + i * 7;
            actor[i] = (c + 3) * 97 + i * 11;
        }
        memset(stamina, 0, sizeof stamina);
        memset(ring, 0, sizeof ring);
        bike[0x2c / 4] = (int32_t)(uintptr_t)ring;
        bike[0x2fc / 4] = (int32_t)(uintptr_t)actor;
        actor[0x1e0 / 4] = (int32_t)(uintptr_t)stamina;
        actor[0x1c4 / 4] = (int32_t)(uintptr_t)bike;
        bike[0x324 / 4] = 2;
        stamina[0x38 / 4] = 103;
        for (int a = 0; a < 2; ++a) {
            int32_t* obj = a ? actor : bike;
            obj[0x78 / 4] = RD32(0x474d78);
            obj[0x80 / 4] = 41;
            ((signed char*)obj)[0x82] = -1;
            obj[0x84 / 4] = -1;
            ((unsigned char*)obj)[0x88] = c % 4;
        }
        RD32(0x4b7ecc) = 100 + c;
#define RM_LOG(label, value)                                                                                 \
    do {                                                                                                     \
        snprintf(key, sizeof key, "crash.remount.%d.%s", c, label);                                          \
        rr_log(key, value);                                                                                  \
    } while (0)
        RM_LOG("tick", RD32(0x4b7ecc));
        RM_LOG("input.loops", c % 4);
        for (size_t i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
            snprintf(key, sizeof key, "crash.remount.%d.input.bike.%x", c, offsets[i]);
            rr_log(key, bike[offsets[i] / 4]);
            snprintf(key, sizeof key, "crash.remount.%d.input.rider.%x", c, offsets[i]);
            rr_log(key, actor[offsets[i] / 4]);
        }
        ((void(__fastcall*)(int32_t*))0x404560)(bike);
        for (size_t i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
            snprintf(key, sizeof key, "crash.remount.%d.output.bike.%x", c, offsets[i]);
            rr_log(key, bike[offsets[i] / 4]);
            snprintf(key, sizeof key, "crash.remount.%d.output.rider.%x", c, offsets[i]);
            rr_log(key, actor[offsets[i] / 4]);
        }
        RM_LOG("stamina", stamina[0x3c / 4]);
        for (int a = 0; a < 2; ++a) {
            int32_t* obj = a ? actor : bike;
            const char* who = a ? "rider" : "bike";
#define RM_STATE(label, value)                                                                               \
    do {                                                                                                     \
        snprintf(key, sizeof key, "crash.remount.%d.%s.%s", c, who, label);                                  \
        rr_log(key, value);                                                                                  \
    } while (0)
            RM_STATE("animation", ((signed char*)obj)[0x80]);
            RM_STATE("frame", ((signed char*)obj)[0x81]);
            RM_STATE("queued", ((signed char*)obj)[0x82]);
            RM_STATE("loops", ((unsigned char*)obj)[0x88]);
            RM_STATE("deadline", (uint32_t)obj[0x84 / 4]);
#undef RM_STATE
        }
#undef RM_LOG
    }
    rr_log("crash.remount.count", 64);
    RD32(0x4b6e58) = saved_landmark;
    RD32(0x4642dc) = saved_cop;
    RD32(0x4b7ecc) = saved_tick;
    RD8(0x4c5b00) = saved_net;
    RD8(0x4c5af9) = saved_net2;
}
static void capture_crash_walking(void) {
    const uint32_t saved_player = RD32(0x4642d8), saved_cop = RD32(0x4642dc), saved_controls = RD32(0x4b7ed0),
                   saved_mode = RD32(0x4c5d7c), saved_tick = RD32(0x4b7ecc);
    const unsigned char saved_demo = RD8(0x4753c8);
    int32_t bike[256], actor[256], brain[256], ring[256];
    char key[128];
    const int boff[] = {0x1c, 0x28, 0x2f0, 0x2f4}, aoff[] = {0x1c, 0x28, 0x5c, 0xec, 0x128};
    RD32(0x4642dc) = 0;
    RD32(0x4c5d7c) = 1;
    RD8(0x4753c8) = 0;
    RD32(0x4b7ecc) = 100;
    for (int c = 0; c < 256; ++c) {
        memset(bike, 0, sizeof bike);
        memset(actor, 0, sizeof actor);
        memset(brain, 0, sizeof brain);
        memset(ring, 0, sizeof ring);
        bike[0x324 / 4] = 0;
        bike[0x2fc / 4] = (int32_t)(uintptr_t)actor;
        bike[0x140 / 4] = actor[0x140 / 4] = (int32_t)(uintptr_t)ring;
        actor[0x1d8 / 4] = (int32_t)(uintptr_t)brain;
        RD32(0x4642d8) = (int32_t)(uintptr_t)bike;
        actor[0x78 / 4] = RD32(0x474d78);
        actor[0x80 / 4] = 38;
        ((signed char*)actor)[0x82] = -1;
        actor[0x84 / 4] = -1;
        ((signed char*)brain)[0x160] = 8;
        brain[(0x138 + 8 * 4) / 4] = 1200;
        bike[0x1c / 4] = 0x5000;
        actor[0x1c / 4] = 0x5000 + (c / 16 % 5 - 2) * 9;
        bike[0x28 / 4] = 0x3000;
        actor[0x28 / 4] = 0x3000 - 6000 + (c / 80 % 3 - 1) * 6001;
        bike[0x2f0 / 4] = 87;
        bike[0x2f4 / 4] = 2;
        actor[0xec / 4] = 397;
        actor[0x5c / 4] = actor[0x128 / 4] = 0x70000;
        RD32(0x4b7ed0) = (c % 16) * 0x800;
#define CW_LOG(label, value)                                                                                 \
    do {                                                                                                     \
        snprintf(key, sizeof key, "crash.walk.%d.%s", c, label);                                             \
        rr_log(key, value);                                                                                  \
    } while (0)
        CW_LOG("controls", RD32(0x4b7ed0));
        CW_LOG("elapsed", 1200);
        for (size_t i = 0; i < sizeof boff / sizeof boff[0]; ++i) {
            snprintf(key, sizeof key, "crash.walk.%d.input.bike.%x", c, boff[i]);
            rr_log(key, bike[boff[i] / 4]);
        }
        for (size_t i = 0; i < sizeof aoff / sizeof aoff[0]; ++i) {
            snprintf(key, sizeof key, "crash.walk.%d.input.rider.%x", c, aoff[i]);
            rr_log(key, actor[aoff[i] / 4]);
        }
        ((void(__fastcall*)(int32_t*))0x40a600)(bike);
        for (size_t i = 0; i < sizeof boff / sizeof boff[0]; ++i) {
            snprintf(key, sizeof key, "crash.walk.%d.output.bike.%x", c, boff[i]);
            rr_log(key, bike[boff[i] / 4]);
        }
        for (size_t i = 0; i < sizeof aoff / sizeof aoff[0]; ++i) {
            snprintf(key, sizeof key, "crash.walk.%d.output.rider.%x", c, aoff[i]);
            rr_log(key, actor[aoff[i] / 4]);
        }
        CW_LOG("output.elapsed", brain[(0x138 + 8 * 4) / 4]);
        CW_LOG("animation", ((signed char*)actor)[0x80]);
        CW_LOG("queued", ((signed char*)actor)[0x82]);
#undef CW_LOG
    }
    rr_log("crash.walk.count", 256);
    RD32(0x4642d8) = saved_player;
    RD32(0x4642dc) = saved_cop;
    RD32(0x4b7ed0) = saved_controls;
    RD32(0x4c5d7c) = saved_mode;
    RD32(0x4b7ecc) = saved_tick;
    RD8(0x4753c8) = saved_demo;
}
static void capture_crash_recovery(void) {
    const uint32_t saved_tick = RD32(0x4b7ecc), saved_cop = RD32(0x4642dc), saved_controls = RD32(0x4b7ed0);
    int32_t bike[256], actor[256], brain[256];
    char key[128];
    const int codes[] = {36, 37, 38, 41};
    const int boff[] = {0x1c, 0x28, 0x2ec, 0x2f0, 0x2f4};
    const int aoff[] = {0x1c, 0x28, 0x5c, 0xec, 0x128};
    RD32(0x4642dc) = 0;
    RD32(0x4b7ed0) = 0;
    for (int c = 0; c < 256; ++c) {
        memset(bike, 0, sizeof bike);
        memset(actor, 0, sizeof actor);
        memset(brain, 0, sizeof brain);
        bike[0x324 / 4] = 2;
        bike[0x2fc / 4] = (int32_t)(uintptr_t)actor;
        actor[0x1d8 / 4] = (int32_t)(uintptr_t)brain;
        actor[0x78 / 4] = RD32(0x474d78);
        actor[0x80 / 4] = codes[c % 4];
        ((signed char*)actor)[0x82] = -1;
        actor[0x84 / 4] = -1;
        ((unsigned char*)actor)[0x88] = c % 4;
        ((signed char*)brain)[0x160] = 8;
        brain[(0x138 + 8 * 4) / 4] = c % 5 == 0 ? 1201 : 1200;
        bike[0x1c / 4] = 0x5000;
        actor[0x1c / 4] = 0x5000 + (c % 7 - 3) * 9;
        bike[0x28 / 4] = 0x3000;
        actor[0x28 / 4] = 0x3000 - 6000 + (c % 9 - 4) * 2000;
        bike[0x2ec / 4] = 103;
        bike[0x2f0 / 4] = c % 3 ? 87 : 0;
        bike[0x2f4 / 4] = c % 13 == 0 ? 1 : 2;
        actor[0xec / 4] = 397;
        actor[0x5c / 4] = actor[0x128 / 4] = 0x70000;
        RD32(0x4b7ecc) = 100 + c;
#define RC_LOG(label, value)                                                                                 \
    do {                                                                                                     \
        snprintf(key, sizeof key, "crash.recovery.%d.%s", c, label);                                         \
        rr_log(key, value);                                                                                  \
    } while (0)
        RC_LOG("tick", RD32(0x4b7ecc));
        RC_LOG("elapsed", brain[(0x138 + 8 * 4) / 4]);
        RC_LOG("input.animation", ((signed char*)actor)[0x80]);
        RC_LOG("input.loops", ((unsigned char*)actor)[0x88]);
        for (size_t i = 0; i < sizeof boff / sizeof boff[0]; ++i) {
            snprintf(key, sizeof key, "crash.recovery.%d.input.bike.%x", c, boff[i]);
            rr_log(key, bike[boff[i] / 4]);
        }
        for (size_t i = 0; i < sizeof aoff / sizeof aoff[0]; ++i) {
            snprintf(key, sizeof key, "crash.recovery.%d.input.rider.%x", c, aoff[i]);
            rr_log(key, actor[aoff[i] / 4]);
        }
        ((void(__fastcall*)(int32_t*))0x40a600)(bike);
        for (size_t i = 0; i < sizeof boff / sizeof boff[0]; ++i) {
            snprintf(key, sizeof key, "crash.recovery.%d.output.bike.%x", c, boff[i]);
            rr_log(key, bike[boff[i] / 4]);
        }
        for (size_t i = 0; i < sizeof aoff / sizeof aoff[0]; ++i) {
            snprintf(key, sizeof key, "crash.recovery.%d.output.rider.%x", c, aoff[i]);
            rr_log(key, actor[aoff[i] / 4]);
        }
        RC_LOG("animation", ((signed char*)actor)[0x80]);
        RC_LOG("frame", ((signed char*)actor)[0x81]);
        RC_LOG("queued", ((signed char*)actor)[0x82]);
        RC_LOG("loops", ((unsigned char*)actor)[0x88]);
        RC_LOG("deadline", (uint32_t)actor[0x84 / 4]);
#undef RC_LOG
    }
    rr_log("crash.recovery.count", 256);
    RD32(0x4b7ecc) = saved_tick;
    RD32(0x4642dc) = saved_cop;
    RD32(0x4b7ed0) = saved_controls;
}
static void capture_actor_motion(void) {
    const uint32_t saved_accel = RD32(0x4c3cec), saved_cop = RD32(0x4642dc);
    int32_t actor[256], bike[256], ring[256];
    char key[128];
    const int offsets[] = {0x18,  0x1c,  0x20,  0x24,  0x28,  0x2c,  0x30,  0x34,  0x38,
                           0x3c,  0x40,  0xec,  0xf0,  0xf4,  0x108, 0x10c, 0x128, 0x12c,
                           0x130, 0x134, 0x138, 0x13c, 0x140, 0x184, 0x188, 0x18c, 0x19c};
    RD32(0x4642dc) = 0;
    for (int c = 0; c < 512; ++c) {
        memset(actor, 0, sizeof actor);
        memset(bike, 0, sizeof bike);
        memset(ring, 0, sizeof ring);
        ring[8 / 4] = ring[12 / 4] = ring[24 / 4] = ring[28 / 4] = (int32_t)(uintptr_t)ring;
        actor[0x2c / 4] = actor[0x13c / 4] = actor[0x140 / 4] = (int32_t)(uintptr_t)ring;
        actor[0xc4 / 4] = 8;
        actor[0x1c4 / 4] = (int32_t)(uintptr_t)bike;
        bike[0x324 / 4] = 0;
        actor[0x18 / 4] = c % 4 + 1;
        actor[0x1c / 4] = 128 + c * 256;
        actor[0x20 / 4] = c % 7 * 71;
        actor[0x24 / 4] = c % 5 * 23;
        actor[0x28 / 4] = (c % 9 - 4) * 1000;
        actor[0xec / 4] = (c % 21 - 10) * 997;
        actor[0xf0 / 4] = (c % 11 - 5) * 97;
        actor[0xf4 / 4] = (c % 13 - 6) * 113;
        actor[0x128 / 4] = c % 16 ? ((c % 16) * 0x100000 + (c % 5) * 4096) : 0;
        actor[0x184 / 4] = (c % 7 - 3) * 123;
        actor[0x188 / 4] = (c % 9 - 4) * 137;
        actor[0x18c / 4] = (c % 11 - 5) * 151;
        actor[0x19c / 4] = c % 2;
        RD32(0x4c3cec) = (c % 9 - 4) * 101;
#define AM_LOG(label, value)                                                                                 \
    do {                                                                                                     \
        snprintf(key, sizeof key, "actor.motion.%d.%s", c, label);                                           \
        rr_log(key, value);                                                                                  \
    } while (0)
        AM_LOG("accel", RD32(0x4c3cec));
        for (size_t i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
            snprintf(key, sizeof key, "actor.motion.%d.input.%x", c, offsets[i]);
            rr_log(key, actor[offsets[i] / 4]);
        }
        ((void(__fastcall*)(int32_t*))0x439920)(actor);
        for (size_t i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
            snprintf(key, sizeof key, "actor.motion.%d.output.%x", c, offsets[i]);
            rr_log(key, actor[offsets[i] / 4]);
        }
#undef AM_LOG
    }
    rr_log("actor.motion.count", 512);
    RD32(0x4c3cec) = saved_accel;
    RD32(0x4642dc) = saved_cop;
}
static void capture_crash_entry(void) {
    const uint32_t saved_landmark = RD32(0x4b6e58), saved_cop = RD32(0x4642dc), saved_tick = RD32(0x4b7ecc);
    const unsigned char saved_net = RD8(0x4c5b00), saved_net2 = RD8(0x4c5af9);
    int32_t bike[256], actor[256], ring[4], rider_impulse[3], bike_impulse[3];
    char key[128];
    const int offsets[] = {0x1c,  0x20,  0x24,  0x28,  0x2c,  0x30,  0x34,  0x38,  0x3c,  0x40,
                           0x58,  0x5c,  0x60,  0xbc,  0xc0,  0xec,  0xf0,  0xf4,  0x104, 0x108,
                           0x10c, 0x110, 0x114, 0x118, 0x11c, 0x120, 0x124, 0x128, 0x12c, 0x130,
                           0x134, 0x138, 0x13c, 0x140, 0x154, 0x158, 0x160, 0x164, 0x16c, 0x174,
                           0x178, 0x17c, 0x198, 0x19c, 0x1a0, 0x244, 0x288, 0x2f4, 0x2f8};
    RD32(0x4b6e58) = 0x5000;
    RD32(0x4642dc) = 0;
    RD8(0x4c5b00) = RD8(0x4c5af9) = 0;
    for (int c = 0; c < 64; ++c) {
        for (int i = 0; i < 256; ++i) {
            bike[i] = (c + 1) * 97 + i * 11;
            actor[i] = (c + 3) * 71 + i * 5;
        }
        memset(ring, 0, sizeof ring);
        bike[0x2c / 4] = (int32_t)(uintptr_t)ring;
        bike[0x2fc / 4] = (int32_t)(uintptr_t)actor;
        bike[0x324 / 4] = 2;
        bike[0x2f4 / 4] = c % 11 == 0 ? 1 : 0;
        actor[0x1c4 / 4] = (int32_t)(uintptr_t)bike;
        actor[0x78 / 4] = RD32(0x474d78);
        actor[0x80 / 4] = 2;
        ((signed char*)actor)[0x82] = -1;
        actor[0x84 / 4] = -1;
        ((unsigned char*)actor)[0x88] = 0;
        for (int i = 0; i < 3; ++i) {
            rider_impulse[i] = (c % 5 - 2) * (i + 1) * 137;
            bike_impulse[i] = (c % 7 - 3) * (i + 1) * 211;
        }
        RD32(0x4b7ecc) = 100 + c;
#define CE_LOG(label, value)                                                                                 \
    do {                                                                                                     \
        snprintf(key, sizeof key, "crash.entry.%d.%s", c, label);                                            \
        rr_log(key, value);                                                                                  \
    } while (0)
        CE_LOG("reason", c % 8);
        CE_LOG("tick", RD32(0x4b7ecc));
        CE_LOG("rider_impulse", c % 3 != 0);
        CE_LOG("bike_impulse", c % 4 != 0);
        for (int i = 0; i < 3; ++i) {
            snprintf(key, sizeof key, "crash.entry.%d.rider_impulse.%d", c, i);
            rr_log(key, rider_impulse[i]);
            snprintf(key, sizeof key, "crash.entry.%d.bike_impulse.%d", c, i);
            rr_log(key, bike_impulse[i]);
        }
        for (size_t i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
            snprintf(key, sizeof key, "crash.entry.%d.input.bike.%x", c, offsets[i]);
            rr_log(key, bike[offsets[i] / 4]);
            snprintf(key, sizeof key, "crash.entry.%d.input.rider.%x", c, offsets[i]);
            rr_log(key, actor[offsets[i] / 4]);
        }
        ((void(__fastcall*)(int32_t*, int32_t*, int32_t*, int))0x403d60)(bike, c % 3 ? rider_impulse : NULL,
                                                                         c % 4 ? bike_impulse : NULL, c % 8);
        for (size_t i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
            snprintf(key, sizeof key, "crash.entry.%d.output.bike.%x", c, offsets[i]);
            rr_log(key, bike[offsets[i] / 4]);
            snprintf(key, sizeof key, "crash.entry.%d.output.rider.%x", c, offsets[i]);
            rr_log(key, actor[offsets[i] / 4]);
        }
        CE_LOG("animation", ((signed char*)actor)[0x80]);
        CE_LOG("frame", ((signed char*)actor)[0x81]);
        CE_LOG("queued", ((signed char*)actor)[0x82]);
        CE_LOG("loops", ((unsigned char*)actor)[0x88]);
        CE_LOG("deadline", (uint32_t)actor[0x84 / 4]);
#undef CE_LOG
    }
    rr_log("crash.entry.count", 64);
    RD32(0x4b6e58) = saved_landmark;
    RD32(0x4642dc) = saved_cop;
    RD32(0x4b7ecc) = saved_tick;
    RD8(0x4c5b00) = saved_net;
    RD8(0x4c5af9) = saved_net2;
}
static void capture_crash_rider_animation(void) {
    const uint32_t saved_tick = RD32(0x4b7ecc), saved_mode = RD32(0x4753b0), saved_random = RD32(0x49bce8);
    int32_t bike[256], actor[256];
    char key[128];
    const int8_t initial[] = {33, 35, 34, 29, 28, 30, 31, 32, 36, 37, 2, -1};
    const int32_t speeds[] = {0, 100, 2499, 2500, 7000, 7001, 8000, 8001, 16000};
    RD32(0x4753b0) = 3;
    for (int c = 0; c < 64; ++c) {
        memset(bike, 0, sizeof bike);
        memset(actor, 0, sizeof actor);
        bike[0x324 / 4] = 1;
        bike[0x2fc / 4] = (int32_t)(uintptr_t)actor;
        bike[0x2f4 / 4] = c % 4 ? 1 : 2;
        actor[0x78 / 4] = RD32(0x474d78);
        ((signed char*)actor)[0x80] = initial[c % 12];
        ((signed char*)actor)[0x82] = -1;
        actor[0x84 / 4] = -1;
        snprintf(key, sizeof key, "actor.crash_rider.%d.initial", c);
        rr_log(key, initial[c % 12]);
        snprintf(key, sizeof key, "actor.crash_rider.%d.initial_crash", c);
        rr_log(key, bike[0x2f4 / 4]);
        for (int t = 0; t < 32; ++t) {
            RD32(0x4b7ecc) = 100 + t * 3;
            RD32(0x49bce8) = (c + t) % 4;
            actor[0xec / 4] = speeds[(c + t) % 9];
            actor[0x20 / 4] = c % 3 == 0 ? 4096 : c % 3 == 1 ? 4095 : 0;
#define CR_LOG(label, value)                                                                                 \
    do {                                                                                                     \
        snprintf(key, sizeof key, "actor.crash_rider.%d.%d.%s", c, t, label);                                \
        rr_log(key, value);                                                                                  \
    } while (0)
            CR_LOG("tick", RD32(0x4b7ecc));
            CR_LOG("speed", actor[0xec / 4]);
            CR_LOG("height", actor[0x20 / 4]);
            CR_LOG("random", RD32(0x49bce8));
            ((void(__fastcall*)(int32_t*))0x407f30)(bike);
            CR_LOG("crash", bike[0x2f4 / 4]);
            CR_LOG("animation", ((signed char*)actor)[0x80]);
            CR_LOG("frame", ((signed char*)actor)[0x81]);
            CR_LOG("queued", ((signed char*)actor)[0x82]);
            CR_LOG("loops", ((unsigned char*)actor)[0x88]);
            CR_LOG("deadline", (uint32_t)actor[0x84 / 4]);
            const unsigned char* selected = (unsigned char*)(uintptr_t)(uint32_t)actor[0x7c / 4];
            for (int b = 0; b < 14; ++b) {
                snprintf(key, sizeof key, "actor.crash_rider.%d.%d.selected.%d", c, t, b);
                rr_log(key, selected ? selected[b] : -1);
            }
#undef CR_LOG
        }
    }
    rr_log("actor.crash_rider.count", 64);
    RD32(0x4b7ecc) = saved_tick;
    RD32(0x4753b0) = saved_mode;
    RD32(0x49bce8) = saved_random;
}
static void capture_crash_bike_animation(void) {
    const uint32_t saved_tick = RD32(0x4b7ecc), saved_mode = RD32(0x4753b0), saved_random = RD32(0x49bce8);
    int32_t bike[256];
    char key[128];
    const int8_t initial[] = {-1, 2, 44, 45, 46, 47, 48, 9};
    RD32(0x4753b0) = 3;
    for (int c = 0; c < 32; ++c) {
        memset(bike, 0, sizeof bike);
        bike[0x324 / 4] = 0;
        bike[0x2f4 / 4] = c % 2 + 1;
        bike[0x78 / 4] = RD32(0x474d78);
        ((signed char*)bike)[0x80] = initial[c % 8];
        ((signed char*)bike)[0x82] = -1;
        bike[0x84 / 4] = -1;
        snprintf(key, sizeof key, "actor.crash_bike.%d.initial", c);
        rr_log(key, initial[c % 8]);
        for (int t = 0; t < 32; ++t) {
            RD32(0x4b7ecc) = 100 + t * 3;
            RD32(0x49bce8) = (c + t) % 2;
            bike[0xec / 4] = t > 16 && c % 3 ? 0 : 2000 - t * 15;
#define CB_LOG(label, value)                                                                                 \
    do {                                                                                                     \
        snprintf(key, sizeof key, "actor.crash_bike.%d.%d.%s", c, t, label);                                 \
        rr_log(key, value);                                                                                  \
    } while (0)
            CB_LOG("tick", RD32(0x4b7ecc));
            CB_LOG("speed", bike[0xec / 4]);
            CB_LOG("random", RD32(0x49bce8));
            ((void(__fastcall*)(int32_t*))0x407a90)(bike);
            CB_LOG("animation", ((signed char*)bike)[0x80]);
            CB_LOG("frame", ((signed char*)bike)[0x81]);
            CB_LOG("queued", ((signed char*)bike)[0x82]);
            CB_LOG("loops", ((unsigned char*)bike)[0x88]);
            CB_LOG("deadline", (uint32_t)bike[0x84 / 4]);
            const unsigned char* selected = (unsigned char*)(uintptr_t)(uint32_t)bike[0x7c / 4];
            for (int b = 0; b < 14; ++b) {
                snprintf(key, sizeof key, "actor.crash_bike.%d.%d.selected.%d", c, t, b);
                rr_log(key, selected ? selected[b] : -1);
            }
#undef CB_LOG
        }
    }
    rr_log("actor.crash_bike.count", 32);
    RD32(0x4b7ecc) = saved_tick;
    RD32(0x4753b0) = saved_mode;
    RD32(0x49bce8) = saved_random;
}
static void capture_riding_animation(void) {
    const uint32_t saved_player = RD32(0x4642d8), saved_tick = RD32(0x4b7ecc);
    const unsigned char saved_limit = RD8(0x4c7ff0);
    int32_t bike[256], actor[256];
    char key[128];
    const int8_t initial[] = {-1, 2, 8, 3, 4, 0, 1, 5, 6, 9};
    const int32_t speeds[] = {-1, 0, 299, 300, 1799, 1800, 9499, 9500, 9501, 30000};
    RD8(0x4c7ff0) = 0;
    for (int c = 0; c < 64; ++c) {
        memset(bike, 0, sizeof bike);
        memset(actor, 0, sizeof actor);
        bike[0x2fc / 4] = (int32_t)(uintptr_t)actor;
        bike[0x324 / 4] = 1;
        RD32(0x4642d8) = (uint32_t)(uintptr_t)bike;
        actor[0x78 / 4] = RD32(0x474d78);
        ((signed char*)actor)[0x80] = initial[c % 10];
        ((signed char*)actor)[0x82] = -1;
        actor[0x84 / 4] = -1;
        snprintf(key, sizeof key, "actor.riding.%d.initial", c);
        rr_log(key, initial[c % 10]);
        for (int t = 0; t < 16; ++t) {
            RD32(0x4b7ecc) = 100 + t;
            bike[0xec / 4] = speeds[(c + t) % 10];
            bike[0x114 / 4] = (c % 3) * 30 + t;
            bike[0x100 / 4] = (c % 3 - 1) * 4096 - (t % 2);
            bike[0xf4 / 4] = c % 2 ? 99 : 100;
            ((unsigned char*)bike)[0x278] = c % 4 ? 0 : 0x20;
            ((unsigned char*)bike)[0xc0] = c % 7 == 0 && t > 8 ? 0x10 : 0;
#define RA_LOG(label, value)                                                                                 \
    do {                                                                                                     \
        snprintf(key, sizeof key, "actor.riding.%d.%d.%s", c, t, label);                                     \
        rr_log(key, value);                                                                                  \
    } while (0)
            RA_LOG("tick", RD32(0x4b7ecc));
            RA_LOG("speed", bike[0xec / 4]);
            RA_LOG("throttle", bike[0x114 / 4]);
            RA_LOG("lateral_speed", bike[0x100 / 4]);
            RA_LOG("boost", bike[0xf4 / 4]);
            RA_LOG("flags", ((unsigned char*)bike)[0x278]);
            RA_LOG("wrecked", ((unsigned char*)bike)[0xc0] & 0x10);
            ((void(__fastcall*)(int32_t*))0x407f30)(bike);
            RA_LOG("animation", ((signed char*)actor)[0x80]);
            RA_LOG("frame", ((signed char*)actor)[0x81]);
            RA_LOG("queued", ((signed char*)actor)[0x82]);
            RA_LOG("loops", ((unsigned char*)actor)[0x88]);
            RA_LOG("deadline", (uint32_t)actor[0x84 / 4]);
            const unsigned char* selected = (unsigned char*)(uintptr_t)(uint32_t)actor[0x7c / 4];
            for (int i = 0; i < 14; ++i) {
                snprintf(key, sizeof key, "actor.riding.%d.%d.selected.%d", c, t, i);
                rr_log(key, selected ? selected[i] : -1);
            }
#undef RA_LOG
        }
    }
    rr_log("actor.riding.count", 64);
    RD32(0x4642d8) = saved_player;
    RD32(0x4b7ecc) = saved_tick;
    RD8(0x4c7ff0) = saved_limit;
}
static void capture_actor_projection(void) {
    const uint32_t addresses[] = {0x49e660, 0x49e668, 0x46667c, 0x4bc7d8, 0x4bc7dc, 0x475618};
    int32_t saved[6];
    for (int i = 0; i < 6; ++i) saved[i] = RD32(addresses[i]);
    int32_t output[2][42], ccb[20], pixel[16], current[16], following[16], sub[32], entry[8], params[6],
        actor[64], angles[3];
    char key[128];
    for (int c = 0; c < 256; ++c) {
        memset(output, 0, sizeof output);
        memset(ccb, 0, sizeof ccb);
        memset(pixel, 0, sizeof pixel);
        memset(current, 0, sizeof current);
        memset(following, 0, sizeof following);
        memset(sub, 0, sizeof sub);
        memset(entry, 0, sizeof entry);
        memset(params, 0, sizeof params);
        const int width = c % 3 ? 320 : 640;
        RD32(0x46667c) = width;
        RD32(0x475618) = width / 2;
        RD32(0x4bc7d8) = c % 5 * 20 - 40;
        RD32(0x4bc7dc) = 150;
        ccb[2] = (int32_t)(uintptr_t)pixel;
        ccb[15] = 24;
        ccb[16] = 64;
        current[0] = (int32_t)(uintptr_t)following;
        current[2] = (int32_t)(uintptr_t)entry;
        current[3] = (int32_t)(uintptr_t)sub;
        following[3] = (int32_t)(uintptr_t)sub;
        current[4] = c % 7 * 40 - 120;
        following[4] = current[4] + c % 5 * 17;
        current[5] = c % 9 * 10 - 40;
        following[5] = current[5] + c % 3 * 7;
        current[6] = 256 + (c % 16) * 256;
        entry[4] = (c % 5 ? 240 : 90) * 65536;
        params[0] = (int32_t)(uintptr_t)ccb;
        params[2] = c % 4 == 0 ? 0 : c % 4 == 1 ? 24 : c % 4 == 2 ? 12 : 40;
        params[3] = c % 4 == 0 ? 0 : c % 4 == 1 ? 64 : c % 4 == 2 ? 48 : 80;
        params[4] = (c % 2 ? 1 : 0) | (c % 5 == 1 ? 0x10000 : 0) | (c % 7 == 1 ? 0x20000 : 0);
        const int size = c % 7 ? 512 : 0, lateral = c % 11 * 70 - 350, ground = c % 8 * 9, fraction = c % 256,
                  clip = c % 3 != 0;
#define AP_LOG(label, value)                                                                                 \
    do {                                                                                                     \
        snprintf(key, sizeof key, "actor.projection.%d.%s", c, label);                                       \
        rr_log(key, value);                                                                                  \
    } while (0)
        AP_LOG("width", width);
        AP_LOG("road_x", current[4]);
        AP_LOG("far_x", following[4]);
        AP_LOG("road_y", current[5]);
        AP_LOG("far_y", following[5]);
        AP_LOG("depth", current[6]);
        AP_LOG("occlusion", entry[4]);
        AP_LOG("hotspot_x", params[2]);
        AP_LOG("hotspot_y", params[3]);
        AP_LOG("flags", params[4]);
        AP_LOG("size", size);
        AP_LOG("lateral", lateral);
        AP_LOG("ground", ground);
        AP_LOG("fraction", fraction);
        AP_LOG("clip", clip);
        AP_LOG("camera_x", RD32(0x4bc7d8));
        AP_LOG("camera_height", RD32(0x4bc7dc));
        memset(actor, 0, sizeof actor);
        actor[0x44 / 4] = fraction;
        actor[0x50 / 4] = lateral * 256 + (c % 3 ? 127 : -127);
        actor[0x68 / 4] = (c % 9 - 4) * 0x300000;
        actor[0x6c / 4] = (c % 7 - 3) * 0x200000;
        actor[0xc4 / 4] = c % 2 ? 8 : 4;
        current[0x1c / 4] = (c % 8) * 0x200000;
        ((void(__fastcall*)(int32_t*, int32_t*, int32_t*))0x413ae0)(actor, angles, current);
        AP_LOG("orientation_lateral", actor[0x50 / 4]);
        AP_LOG("orientation_heading", actor[0x68 / 4]);
        AP_LOG("orientation_roll", actor[0x6c / 4]);
        AP_LOG("orientation_road_heading", current[0x1c / 4]);
        AP_LOG("orientation_rider", actor[0xc4 / 4] == 8);
        AP_LOG("orientation_yaw", angles[1]);
        AP_LOG("orientation_view", actor[0xd0 / 4]);
        RD32(0x49e660) = (int32_t)(uintptr_t)output;
        RD32(0x49e668) = (int32_t)(uintptr_t)(output + 1);
        ((void(__fastcall*)(int32_t*, int32_t*, int, int, int, uint32_t, char))0x44ba40)(
            current, params, size, lateral, ground, fraction, (char)clip);
        const int count = ((uintptr_t)RD32(0x49e660) - (uintptr_t)output) / 168;
        AP_LOG("count", count);
        for (int n = 0; n < count && n < 2; ++n)
            for (int i = 0; i < 8; ++i) {
                snprintf(key, sizeof key, "actor.projection.%d.%d.point.%d", c, n, i);
                rr_log(key, output[n][17 + i]);
            }
#undef AP_LOG
    }
    rr_log("actor.projection.count", 256);
    for (int i = 0; i < 6; ++i) RD32(addresses[i]) = saved[i];
}
static void capture_actor_selection(uint32_t* descriptor, int bank, int binding, int frame) {
    int32_t actor[128], projection[16], angles[3];
    char key[128];
    uint32_t* pair = (uint32_t*)(bank >= 2 ? 0x474db8 + (bank - 2) * 8 : 0x4b9220 + bank * 8);
    uint32_t* ccb = (uint32_t*)(uintptr_t)RD32(pair[1] + 12);
    uint32_t saved[20];
    memcpy(saved, ccb, sizeof saved);
    const int32_t yaw[] = {-0xff0000, -0x800000, -0x400000, -1,       0,        1,
                           0x400000,  0x7effff,  0x7f0000,  0x800000, 0xff0000, 0x1000000};
    for (int c = 0; c < 48; ++c) {
        memset(actor, 0, sizeof actor);
        memset(projection, 0, sizeof projection);
        actor[0x8c / 4] = pair[0];
        actor[0x90 / 4] = pair[1];
        actor[0x98 / 4] = (int32_t)(uintptr_t)descriptor;
        actor[0x68 / 4] = yaw[c % 12];
        actor[0x6c / 4] = yaw[(c / 12) * 3];
        projection[0x18 / 4] = 256;
        projection[0x10 / 4] = RD32(0x4bc7d8);
        ((void(__fastcall*)(int32_t*, int32_t*, int32_t*))0x413ae0)(actor, angles, projection);
#define AS_LOG(label, value)                                                                                 \
    do {                                                                                                     \
        snprintf(key, sizeof key, "%s.%d.%d.%d.%s", bank >= 2 ? "vehicle.selection" : "actor.selection",     \
                 binding, frame, c, label);                                                                  \
        rr_log(key, value);                                                                                  \
    } while (0)
        AS_LOG("yaw", angles[1]);
        AS_LOG("roll", angles[2]);
        AS_LOG("heading", actor[0x68 / 4]);
        AS_LOG("view_angle", actor[0xd0 / 4]);
        ((void(__fastcall*)(int32_t*, int32_t*))0x43af60)(actor, projection);
        AS_LOG("frame", ccb[0x40 / 4]);
        AS_LOG("flags", actor[0xb8 / 4]);
        AS_LOG("selected_roll", actor[0xbc / 4]);
        AS_LOG("hotspot_x", actor[0xb0 / 4]);
        AS_LOG("hotspot_y", actor[0xb4 / 4]);
#undef AS_LOG
    }
    memcpy(ccb, saved, sizeof saved);
}
static void capture_actor_descriptors(void) {
    const uint32_t bindings[][3] = {
        {0, 0x004746c0, 1}, {0, 0x004746c8, 1}, {0, 0x004746d0, 1}, {0, 0x004746d8, 1}, {0, 0x004746e0, 1},
        {0, 0x004746e8, 2}, {0, 0x004746f8, 2}, {0, 0x00474708, 1}, {0, 0x00474710, 1}, {0, 0x00474718, 2},
        {0, 0x00474728, 5}, {0, 0x00474750, 5}, {0, 0x00474778, 5}, {0, 0x004747a0, 5}, {0, 0x004747c8, 2},
        {0, 0x004747d8, 2}, {0, 0x004747e8, 3}, {0, 0x00474800, 3}, {0, 0x00474818, 3}, {0, 0x00474830, 3},
        {0, 0x00474868, 3}, {0, 0x00474880, 3}, {0, 0x00474898, 3}, {0, 0x004748b0, 3}, {0, 0x004748c8, 2},
        {0, 0x004748d8, 2}, {0, 0x004748e8, 7}, {0, 0x00474920, 8}, {0, 0x00474960, 4}, {0, 0x00474980, 4},
        {0, 0x004749a0, 4}, {0, 0x004749c0, 5}, {0, 0x004749e8, 4}, {0, 0x00474a08, 2}, {0, 0x00474a18, 5},
        {0, 0x00474a40, 4}, {0, 0x00474a60, 6}, {0, 0x00474a90, 5}, {0, 0x00474ab8, 5}, {0, 0x00474ae0, 5},
        {0, 0x00474b08, 5}, {0, 0x00474b30, 5}, {0, 0x00474b58, 4}, {0, 0x00474b78, 4}, {0, 0x00474b98, 3},
        {0, 0x00474bb0, 3}, {0, 0x00474bc8, 1}, {0, 0x00474bd0, 1}, {1, 0x004746d0, 1}, {1, 0x00474710, 1},
        {1, 0x00474718, 2}, {1, 0x004747e8, 3}, {1, 0x00474800, 3}, {1, 0x004748e8, 7}, {1, 0x004749c0, 5},
        {1, 0x00474b30, 5}, {1, 0x00474bc8, 1}, {1, 0x00474b98, 3}, {1, 0x00474a40, 4}};
    uint32_t output[5];
    char key[128];
    for (size_t c = 0; c < sizeof bindings / sizeof bindings[0]; ++c) {
        snprintf(key, sizeof key, "actor.descriptor.%u.bank", (unsigned)c);
        rr_log(key, bindings[c][0]);
        snprintf(key, sizeof key, "actor.descriptor.%u.source", (unsigned)c);
        rr_log(key, bindings[c][1]);
        snprintf(key, sizeof key, "actor.descriptor.%u.count", (unsigned)c);
        rr_log(key, bindings[c][2]);
        for (unsigned f = 0; f < bindings[c][2]; ++f) {
            memset(output, 0, sizeof output);
            const uint32_t result = ((uint32_t(__fastcall*)(uint32_t*, int*, uint32_t*))0x40b200)(
                                        (uint32_t*)(0x4b9220 + bindings[c][0] * 8),
                                        (int*)(uintptr_t)(bindings[c][1] + f * 8), output) &
                                    255;
            snprintf(key, sizeof key, "actor.descriptor.%u.%u.available", (unsigned)c, f);
            rr_log(key, result);
            for (int b = 0; b < 16; ++b) {
                snprintf(key, sizeof key, "actor.descriptor.%u.%u.byte.%d", (unsigned)c, f, b);
                rr_log(key, ((unsigned char*)output)[b]);
            }
            if (!result) break;
            capture_actor_selection(output, bindings[c][0], (int)c, (int)f);
        }
    }
    rr_log("actor.descriptor.count", sizeof bindings / sizeof bindings[0]);
}
static void capture_actor_views(void) {
    char key[128];
    for (int bank = 0; bank < 2; ++bank) {
        uint32_t* pair = (uint32_t*)(0x4b9220 + bank * 8);
        uint32_t* snac = (uint32_t*)(uintptr_t)pair[0];
        const uint32_t count = snac[snac[2] == 1 ? 11 : 3];
        uint32_t* ccb = (uint32_t*)(uintptr_t)RD32(pair[1] + 12);
        uint32_t saved[20];
        memcpy(saved, ccb, sizeof saved);
        snprintf(key, sizeof key, "actor.views.%d.count", bank);
        rr_log(key, count);
        for (uint32_t group = 0; group < count; ++group) {
            uint32_t* metadata = ((uint32_t*(__fastcall*)(uint32_t*, int))0x40b5a0)(pair, group);
            snprintf(key, sizeof key, "actor.views.%d.%u.count", bank, group);
            rr_log(key, metadata[1]);
            for (uint32_t frame = 0; frame < metadata[1]; ++frame) {
                uint32_t flags = 0;
                uint32_t* selected = ((uint32_t*(__fastcall*)(uint32_t*, int, int, uint32_t*))0x40b6f0)(
                    pair, group, frame, &flags);
                snprintf(key, sizeof key, "actor.views.%d.%u.%u.frame", bank, group, frame);
                rr_log(key, ((uintptr_t)selected - pair[1] - 28) / 28);
                snprintf(key, sizeof key, "actor.views.%d.%u.%u.flags", bank, group, frame);
                rr_log(key, flags);
            }
        }
        memcpy(ccb, saved, sizeof saved);
    }
}
static void capture_actor_animation(void) {
    const uint32_t saved_tick = RD32(0x4b7ecc);
    char key[128];
    unsigned char state[32], sequence[3][80];
    uint32_t bank[4];
    for (int c = 0; c < 64; ++c) {
        memset(state, 0, sizeof state);
        memset(sequence, 0, sizeof sequence);
        for (int k = 0; k < 3; ++k) {
            bank[k] = (uint32_t)(uintptr_t)sequence[k];
            for (int f = 0; f < 4; ++f) {
                sequence[k][f * 20 + 12] = (unsigned char)(f == 0 ? c % 4 : (f == 1 ? 1 : (f == 2 ? 3 : 0)));
                sequence[k][f * 20 + 13] = f == 3 ? 2 : (f == 2 ? 0x11 : 0);
                *(uint32_t*)(sequence[k] + f * 20 + 16) = 1;
            }
        }
        sequence[0][13] = 1;
        sequence[1][2 * 20 + 13] = c % 3 == 0 ? 2 : 0x11;
        sequence[2][12] = c % 7 == 0 ? 255 : 0;
        sequence[2][13] = c % 4 == 0 ? 1 : 0;
        bank[3] = 0;
        *(uint32_t*)(state + 8) = (uint32_t)(uintptr_t)bank;
        state[16] = 255;
        state[18] = 255;
#define AA_LOG(label, value)                                                                                 \
    do {                                                                                                     \
        snprintf(key, sizeof key, "actor.animation.%d.%s", c, label);                                        \
        rr_log(key, value);                                                                                  \
    } while (0)
        for (int k = 0; k < 3; ++k)
            for (int f = 0; f < 4; ++f) {
                snprintf(key, sizeof key, "actor.animation.%d.sequence.%d.%d.duration", c, k, f);
                rr_log(key, (int)(signed char)sequence[k][f * 20 + 12]);
                snprintf(key, sizeof key, "actor.animation.%d.sequence.%d.%d.control", c, k, f);
                rr_log(key, sequence[k][f * 20 + 13]);
            }
        for (int t = 0; t < 32; ++t) {
            const uint32_t tick = (c % 8 == 0 ? 0xffffff00u : 100u) + t * (c % 5 + 1);
            RD32(0x4b7ecc) = tick;
            const int code = t == 0 ? (c % 5 == 4 ? -1 : c % 4) : -2,
                      queued = t == 4 ? (c % 4 == 3 ? -1 : c % 3) : -2;
            int set = -1, queue = -1;
            if (code != -2)
                set = ((uint32_t(__fastcall*)(unsigned char*, char))0x40ac70)(state, (char)code) & 255;
            if (queued != -2)
                queue = ((uint32_t(__fastcall*)(unsigned char*, char))0x43ae60)(state, (char)queued) & 255;
            ((void(__fastcall*)(unsigned char*))0x43ae90)(state);
#define AA_TLOG(label, value)                                                                                \
    do {                                                                                                     \
        snprintf(key, sizeof key, "actor.animation.%d.%d.%s", c, t, label);                                  \
        rr_log(key, value);                                                                                  \
    } while (0)
            AA_TLOG("tick", tick);
            AA_TLOG("set_code", code);
            AA_TLOG("queue_code", queued);
            AA_TLOG("set_result", set);
            AA_TLOG("queue_result", queue);
            AA_TLOG("animation", (int)(signed char)state[16]);
            AA_TLOG("frame", (int)(signed char)state[17]);
            AA_TLOG("queued", (int)(signed char)state[18]);
            AA_TLOG("deadline", *(uint32_t*)(state + 20));
            AA_TLOG("loops", state[24]);
            const uintptr_t selected = *(uint32_t*)(state + 12);
            int selected_sequence = -1, selected_frame = -1;
            for (int k = 0; k < 3; ++k)
                if (selected >= (uintptr_t)sequence[k] && selected < (uintptr_t)sequence[k] + 80) {
                    selected_sequence = k;
                    selected_frame = (selected - (uintptr_t)sequence[k]) / 20;
                }
            AA_TLOG("selected_sequence", selected_sequence);
            AA_TLOG("selected_frame", selected_frame);
#undef AA_TLOG
        }
#undef AA_LOG
    }
    rr_log("actor.animation.count", 64);
    RD32(0x4b7ecc) = saved_tick;
}
static void capture_terrain_ground(void) {
    int current[96], next[96];
    char key[128];
    for (int c = 0; c < 32; ++c) {
        memset(current, 0, sizeof current);
        memset(next, 0, sizeof next);
        current[3] = (int)(uintptr_t)next;
        ((unsigned char*)current)[0x3c] = c % 5 == 0 ? 0 : 4;
        ((unsigned char*)current)[0x37] = c % 5 == 0 ? (c / 5) % 4 * 8 : 0;
        ((unsigned char*)current)[0x148] = c % 2 ? 255 : 0;
        ((unsigned char*)current)[0x149] = c % 3 ? 0 : 255;
        current[0xe0 / 4] = -250;
        current[0xe4 / 4] = 250;
        current[0xe8 / 4] = -350;
        current[0xec / 4] = 350;
        current[0x140 / 4] = c % 4 * 7;
        current[0x144 / 4] = c % 3 * 9;
        next[0xe0 / 4] = -250 - c % 3 * 10;
        next[0xe4 / 4] = 250 + c % 5 * 7;
        next[0xe8 / 4] = -350 - c % 5 * 13;
        next[0xec / 4] = 350 + c % 7 * 9;
        next[0x140 / 4] = c % 3 * 11;
        next[0x144 / 4] = c % 4 * 13;
        for (int band = 0; band < 3; ++band)
            for (int side = 0; side < 2; ++side) {
                const int offset = 0xf0 + band * 16 + side * 8, sign = side ? 1 : -1;
                current[offset / 4] = sign * (650 + band * 600 + c % 4 * 30);
                current[offset / 4 + 1] = (c % 7 - 3) * (band + 1) * 25;
                next[offset / 4] = sign * (630 + band * 610 + c % 5 * 25);
                next[offset / 4 + 1] = (c % 5 - 2) * (band + 1) * 35;
            }
#define GROUND_LOG(label, value)                                                                             \
    do {                                                                                                     \
        snprintf(key, sizeof key, "ground.%d.%s", c, label);                                                 \
        rr_log(key, value);                                                                                  \
    } while (0)
        GROUND_LOG("type", ((unsigned char*)current)[0x3c]);
        GROUND_LOG("flags", ((unsigned char*)current)[0x37]);
        GROUND_LOG("selector_left", ((unsigned char*)current)[0x148]);
        GROUND_LOG("selector_right", ((unsigned char*)current)[0x149]);
        const int offsets[] = {0xe0, 0xe4,  0xe8,  0xec,  0x140, 0x144, 0xf0,  0xf4,  0xf8,
                               0xfc, 0x100, 0x104, 0x108, 0x10c, 0x110, 0x114, 0x118, 0x11c};
        for (int i = 0; i < 18; ++i) {
            snprintf(key, sizeof key, "ground.%d.near.%d", c, i);
            rr_log(key, current[offsets[i] / 4]);
            snprintf(key, sizeof key, "ground.%d.far.%d", c, i);
            rr_log(key, next[offsets[i] / 4]);
        }
        for (int i = 0; i < 64; ++i) {
            const int x = (i - 32) * 75 * 256 + c * 3, frac = (i * 13 + c * 7) & 255;
            snprintf(key, sizeof key, "ground.%d.%d.x", c, i);
            rr_log(key, x);
            snprintf(key, sizeof key, "ground.%d.%d.fraction", c, i);
            rr_log(key, frac);
            snprintf(key, sizeof key, "ground.%d.%d.height", c, i);
            rr_log(key, ((int(__fastcall*)(int*, int, uint32_t))0x439b10)(current, x, frac));
        }
#undef GROUND_LOG
    }
    rr_log("ground.count", 32);
}
static void terrain_word(unsigned char*, int, uint32_t);
static void capture_scenery(void) {
    const int prior_mode = RD32(0x4753b0), prior_random = RD32(0x49bce8), prior_level = RD8(0x4b8a12);
    const int random_values[3] = {0, 123, 32767};
    char key[128];
    RD32(0x4753b0) = 3;
    for (int c = 0; c < 32; ++c) {
        uint32_t table[9] = {0x52524f42,
                             36,
                             4,
                             32,
                             0,
                             0x014c0182u ^ ((uint32_t)(c % 2) << 28) ^ ((uint32_t)(c % 3 == 0) << 31) ^
                                 ((uint32_t)(c % 3 == 1) << 30) ^ ((uint32_t)(c % 4 == 0) << 11),
                             0x034d3084u,
                             0x01560082u,
                             0x033f0081u};
        int iterator[12] = {0};
        unsigned char road[0x2f8] = {0};
        RD32(0x49bce8) = random_values[c % 3];
#define SCENERY_LOG(field, value)                                                                            \
    do {                                                                                                     \
        snprintf(key, sizeof key, "scenery.%d.%s", c, field);                                                \
        rr_log(key, (value));                                                                                \
    } while (0)
        SCENERY_LOG("random", random_values[c % 3]);
        SCENERY_LOG("left", -500 - c * 10);
        SCENERY_LOG("right", 500 + c * 7);
        for (int i = 0; i < 4; ++i) {
            snprintf(key, sizeof key, "scenery.%d.profile.%d", c, i);
            rr_log(key, table[5 + i]);
        }
        ((void(__fastcall*)(int*, void*, char))0x451b60)(iterator, table, 1);
        for (int j = 0; j < 32; ++j) {
            if (j) ((void(__fastcall*)(int*, int))0x451e20)(iterator, 256);
            memset(road, 0, sizeof road);
            terrain_word(road, 0xe0, (uint32_t)(-500 - c * 10));
            terrain_word(road, 0xe4, 500 + c * 7);
            ((void(__fastcall*)(int*, void*))0x451cb0)(iterator, road);
            const int count = road[0x60];
            snprintf(key, sizeof key, "scenery.%d.sample.%d.count", c, j);
            rr_log(key, count);
            for (int k = 0; k < count && k < 8; ++k) {
                snprintf(key, sizeof key, "scenery.%d.sample.%d.x.%d", c, j, k);
                int value;
                memcpy(&value, road + 0x6c + k * 4, 4);
                rr_log(key, value);
            }
            snprintf(key, sizeof key, "scenery.%d.sample.%d.selector", c, j);
            rr_log(key, road[0x63]);
            snprintf(key, sizeof key, "scenery.%d.sample.%d.size", c, j);
            rr_log(key, road[0x62]);
            snprintf(key, sizeof key, "scenery.%d.sample.%d.mirror", c, j);
            rr_log(key, road[0x64] & 1);
        }
#undef SCENERY_LOG
    }
    RD32(0x4753b0) = prior_mode;
    RD32(0x49bce8) = prior_random;
    RD8(0x4b8a12) = (unsigned char)prior_level;
}

static void capture_napa_events(void) {
    FILE* input = fopen("D:\\ROADRASH\\DATA\\COURSES\\NAPA.CRS", "rb");
    if (!input) return;
    fseek(input, 0, SEEK_END);
    const long size = ftell(input);
    rewind(input);
    if (size < 24 || size > 10000000) {
        fclose(input);
        return;
    }
    unsigned char* bytes = malloc(size);
    if (!bytes) {
        fclose(input);
        return;
    }
    if (fread(bytes, 1, size, input) != (size_t)size) {
        free(bytes);
        fclose(input);
        return;
    }
    fclose(input);
#define COURSE_WORD(p) (*(uint32_t*)(bytes + (p)))
    const uint32_t table = COURSE_WORD(16);
    uint32_t segments = 0;
    if (table > (uint32_t)size || (uint32_t)size - table < 16) goto done;
    const uint32_t entries = COURSE_WORD(table + 8);
    if (entries > ((uint32_t)size - table - 16) / 32) goto done;
    for (uint32_t i = 0; i < entries; ++i) {
        const uint32_t row = table + 16 + i * 32;
        if (COURSE_WORD(row) == 0x53475320u) segments = COURSE_WORD(row + 8);
    }
    if (!segments || segments > (uint32_t)size || (uint32_t)size - segments < 16) goto done;
    const uint32_t count = COURSE_WORD(segments + 8);
    char key[128];
    if (count > ((uint32_t)size - segments - 16) / 52) goto done;
    for (uint32_t section = 0; section < count; ++section) {
        const uint32_t row = segments + 16 + section * 52, path = segments + COURSE_WORD(row + 4),
                       events = COURSE_WORD(row + 0x24) ? segments + COURSE_WORD(row + 0x24) : 0;
        if (path > (uint32_t)size || (uint32_t)size - path < 16 ||
            (events && (events > (uint32_t)size || (uint32_t)size - events < 20)))
            continue;
        const uint32_t samples = COURSE_WORD(path + 8);
        if (samples > 65536) continue;
        int state[5] = {0};
        ((void(__fastcall*)(int*, int, char))0x452610)(state, events ? (int)(uintptr_t)(bytes + events) : 0,
                                                       1);
        snprintf(key, sizeof key, "course.events.%u.count", section);
        rr_log(key, samples);
        for (uint32_t sample = 0; sample < samples; ++sample) {
            if (sample) ((void(__fastcall*)(int*, int))0x452690)(state, 256);
            const int flags = ((int(__fastcall*)(int*))0x452710)(state);
            unsigned char values[4];
            ((void(__fastcall*)(int*, unsigned char*, unsigned char*, unsigned char*,
                                unsigned char*))0x4527d0)(state, values, values + 1, values + 2, values + 3);
            snprintf(key, sizeof key, "course.events.%u.%u.flags", section, sample);
            rr_log(key, (unsigned char)flags);
            for (int i = 0; i < 4; ++i) {
                snprintf(key, sizeof key, "course.events.%u.%u.value.%d", section, sample, i);
                rr_log(key, values[i]);
            }
        }
    }
    rr_log("course.events.sections", count);
done:
    free(bytes);
#undef COURSE_WORD
}
static void dump_groups(void) {
    if (!g_groups) return;
    group_count = 0;
    for (int slot = 0; slot < 4; ++slot) {
        uint32_t root = (uint32_t)RD32(0x0049e550 + 4 * slot);
        int family = RD32(0x004b7f10 + 4 * slot);
        if (family > 0) walk_groups(root, root, family, 1, 0);
    }
    rr_log("group.count", group_count);
    capture_hill_strips();
    capture_hill_panels();
    capture_napa_events();
    capture_scenery();
    capture_building_panels();
    capture_terrain_ground();
    capture_actor_animation();
    capture_actor_descriptors();
    capture_actor_views();
    capture_actor_projection();
    capture_riding_animation();
    capture_crash_bike_animation();
    capture_crash_rider_animation();
    capture_crash_entry();
    capture_actor_motion();
    capture_crash_recovery();
    capture_crash_walking();
    capture_crash_remount();
    capture_police_arrest();
    capture_vehicle_descriptors();
}
// Region records: {base, size} then bytes. Version 2 dumps all of .data/.bss and a two-level
// closure of heap blocks referenced from the course ring, projected slices and scratch blocks.
static int heap_pointer(uint32_t v) {
    return v >= 0x00500000u && v < 0x7ff00000u && readable(v, 0x200);
}
// Snapshot of .data/.bss plus a two-level heap closure of the course ring, projected slices and
// scratch blocks. Taken before 0x00443d70 projects the frame (pre) and before 0x00450420 flushes
// the display list (post).
typedef struct {
    uint32_t base, size;
    unsigned char* bytes;
} snap_region;
static snap_region g_pre[4097];
static uint32_t g_npre;
static int g_pre_frame = -1;
static uint32_t closure(uint32_t* blocks) {
    uint32_t nblocks = 0;
    const uint32_t roots[3][2] = {
        {0x0049f250, 128 * 0x2f8}, {0x004bc800, 33 * 0x224}, {0x004ba190, 0x1320 + 33 * 0x240}};
    for (int r = 0; r < 3; ++r)
        for (uint32_t o = 0; o + 4 <= roots[r][1]; o += 4) {
            const uint32_t v = (uint32_t)RD32(roots[r][0] + o);
            if (!heap_pointer(v)) continue;
            uint32_t k = 0;
            for (; k < nblocks && blocks[k] != v; ++k);
            if (k == nblocks && nblocks < 4096) blocks[nblocks++] = v;
        }
    const uint32_t first = nblocks;
    for (uint32_t b = 0; b < first; ++b)
        for (uint32_t o = 0; o < 0x200; o += 4) {
            const uint32_t v = (uint32_t)RD32(blocks[b] + o);
            if (!heap_pointer(v)) continue;
            uint32_t k = 0;
            for (; k < nblocks && blocks[k] != v; ++k);
            if (k == nblocks && nblocks < 4096) blocks[nblocks++] = v;
        }
    return nblocks;
}
static void snapshot(snap_region* out, uint32_t* n) {
    static uint32_t blocks[4096];
    for (uint32_t i = 0; i < *n; ++i) free(out[i].bytes);
    const uint32_t nb = closure(blocks);
    *n = 0;
    out[(*n)++] = (snap_region){0x00463000, 0x661c8, NULL};
    for (uint32_t b = 0; b < nb; ++b) out[(*n)++] = (snap_region){blocks[b], 0x200, NULL};
    for (uint32_t i = 0; i < *n; ++i) {
        out[i].bytes = malloc(out[i].size);
        if (out[i].bytes) memcpy(out[i].bytes, (void*)(uintptr_t)out[i].base, out[i].size);
    }
}
// Rider/bike object dump for --native-actors (plan 11a Task 3 Step 2): the
// display-list nodes alone cannot rebuild actor state (position, lateral,
// height, animation selection +0x98/+0x90). The scheduler ring root S lives
// on the heap and is never inside the two-level closure, so walk it here and
// append full bike (0x340) + rider-half bytes per scheduler node to the
// scene file. Version 4 = v3 regions + node blobs + rider blobs.
static void dump_rider_objects(FILE* out) {
    uint32_t count = 0;
    if (readable(0x0046dcc8, 4)) {
        const uintptr_t S = (uint32_t)RD32(0x0046dcc8);
        if (readable(S, 4) && readable(S + 4, 4)) {
            const uintptr_t end = (uint32_t)*(uint32_t*)S;
            const uintptr_t cursor = (uint32_t)*(uint32_t*)(S + 4);
            uintptr_t node = (readable(cursor + 4, 4)) ? (uint32_t)*(uint32_t*)(cursor + 4) : 0;
            for (int guard = 0; node && node != end && guard < 512; ++guard) {
                if (!readable(node, 0x340)) break;
                ++count;
                node = (uint32_t)*(uint32_t*)(node + 4);
            }
        }
    }
    fwrite(&count, 4, 1, out);
    if (!count || !readable(0x0046dcc8, 4)) return;
    const uintptr_t S = (uint32_t)RD32(0x0046dcc8);
    const uintptr_t end = (uint32_t)*(uint32_t*)S;
    const uintptr_t cursor = (uint32_t)*(uint32_t*)(S + 4);
    uintptr_t node = (uint32_t)*(uint32_t*)(cursor + 4);
    for (int guard = 0; node && node != end && guard < 512; ++guard) {
        if (!readable(node, 0x340)) break;
        const uintptr_t half = (readable(node + 0x2fc, 4)) ? (uint32_t)*(uint32_t*)(node + 0x2fc) : 0;
        const uint32_t half_ok = (half && readable(half, 0x340)) ? 1 : 0;
        fwrite(&node, 4, 1, out);
        unsigned char bike[0x340];
        memcpy(bike, (void*)(uintptr_t)node, 0x340);
        fwrite(bike, 1, 0x340, out);
        fwrite(&half_ok, 4, 1, out);
        if (half_ok) {
            unsigned char rh[0x340];
            memcpy(rh, (void*)(uintptr_t)half, 0x340);
            fwrite(rh, 1, 0x340, out);
        }
        node = (uint32_t)*(uint32_t*)(node + 4);
    }
}
static int capture_frame(void) {
    return g_out && g_pixels && !(g_frame % (uint32_t)g_pixel_every);
}
static void __fastcall hook_project(uint32_t a, int* b, int c, int* d, int e) {
    if (capture_frame()) {
        snapshot(g_pre, &g_npre);
        g_pre_frame = (int)g_frame;
    }
    ((void(__fastcall*)(uint32_t, int*, int, int*, int))0x00443d70)(a, b, c, d, e);
}
// Per-actor dispatch records for --native-actors (plan 11a Task 3): hook the
// two 0x0043af60 view-selection calls and two 0x0044ba40 billboard calls in
// 0x00446250 at their call sites. Buffered during 0x004439f0 and flushed in
// dump_frame (records belong to the frame being dumped).
#include "actor_record_hooks.h"
static void flush_actor_records(void) {
    char key[96];
    rr_log("actor.sel.count", g_nsel);
    for (uint32_t i = 0; i < g_nsel; ++i) {
        snprintf(key, sizeof key, "actor.sel.%u.bike", i);
        rr_log(key, g_sel[i].bike);
        snprintf(key, sizeof key, "actor.sel.%u.ctx", i);
        rr_log(key, g_sel[i].ctx);
        for (int w = 0; w < 5; ++w) {
            snprintf(key, sizeof key, "actor.sel.%u.d%d", i, w);
            rr_log(key, g_sel[i].desc[w]);
        }
        snprintf(key, sizeof key, "actor.sel.%u.a8", i);
        rr_log(key, g_sel[i].a8);
        snprintf(key, sizeof key, "actor.sel.%u.b0", i);
        rr_log(key, g_sel[i].b0);
        snprintf(key, sizeof key, "actor.sel.%u.b4", i);
        rr_log(key, g_sel[i].b4);
        snprintf(key, sizeof key, "actor.sel.%u.bc", i);
        rr_log(key, g_sel[i].bc);
        snprintf(key, sizeof key, "actor.sel.%u.view", i);
        rr_log(key, g_sel[i].view);
        snprintf(key, sizeof key, "actor.sel.%u.f1c", i);
        rr_log(key, g_sel[i].f1c);
        snprintf(key, sizeof key, "actor.sel.%u.f28", i);
        rr_log(key, g_sel[i].f28);
        snprintf(key, sizeof key, "actor.sel.%u.f44", i);
        rr_log(key, g_sel[i].f44);
        snprintf(key, sizeof key, "actor.sel.%u.f50", i);
        rr_log(key, g_sel[i].f50);
    }
    rr_log("actor.bb.count", g_nbb);
    for (uint32_t i = 0; i < g_nbb; ++i) {
        snprintf(key, sizeof key, "actor.bb.%u.size", i);
        rr_log(key, g_bb[i].size);
        snprintf(key, sizeof key, "actor.bb.%u.lateral", i);
        rr_log(key, g_bb[i].lateral);
        snprintf(key, sizeof key, "actor.bb.%u.ground", i);
        rr_log(key, g_bb[i].ground);
        snprintf(key, sizeof key, "actor.bb.%u.pos", i);
        rr_log(key, g_bb[i].pos);
        snprintf(key, sizeof key, "actor.bb.%u.clip", i);
        rr_log(key, g_bb[i].clip);
        snprintf(key, sizeof key, "actor.bb.%u.nbefore", i);
        rr_log(key, g_bb[i].nbefore);
        snprintf(key, sizeof key, "actor.bb.%u.nafter", i);
        rr_log(key, g_bb[i].nafter);
        snprintf(key, sizeof key, "actor.bb.%u.pre_lat", i);
        rr_log(key, g_bb[i].pre_lat);
        snprintf(key, sizeof key, "actor.bb.%u.occ_owner", i);
        rr_log(key, g_bb[i].occ_owner);
        snprintf(key, sizeof key, "actor.bb.%u.occ_row", i);
        rr_log(key, g_bb[i].occ_row);
        snprintf(key, sizeof key, "actor.bb.%u.ctx4", i);
        rr_log(key, g_bb[i].ctx4);
        snprintf(key, sizeof key, "actor.bb.%u.ctx5", i);
        rr_log(key, g_bb[i].ctx5);
        snprintf(key, sizeof key, "actor.bb.%u.ctx6", i);
        rr_log(key, g_bb[i].ctx6);
        snprintf(key, sizeof key, "actor.bb.%u.emu", i);
        rr_log(key, g_bb[i].emu);
        for (int w = 0; w < 6; ++w) {
            snprintf(key, sizeof key, "actor.bb.%u.emu%d", i, w);
            rr_log(key, g_bb[i].emuw[w]);
        }
    }
    g_nsel = 0;
    g_nbb = 0;
}
static void put_regions(FILE* out, snap_region* r, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        if (!r[i].bytes) continue;
        uint32_t h[2] = {r[i].base, r[i].size};
        fwrite(h, 4, 2, out);
        fwrite(r[i].bytes, 1, r[i].size, out);
    }
}
static void dump_scene(void) {
    char path[1024];
    const char* slash = strrchr(g_output, '\\');
    const size_t prefix = slash ? (size_t)(slash - g_output) + 1 : 0;
    if (prefix > sizeof path - 64) return;
    memcpy(path, g_output, prefix);
    snprintf(path + prefix, sizeof path - prefix, "%s-scene%u.raw", g_scenario, g_frame);
    FILE* out = fopen(path, "wb");
    if (!out) return;
    const uint32_t base = (uint32_t)RD32(0x0049e66c), end = (uint32_t)RD32(0x0049e660);
    uint32_t count = end >= base ? (end - base) / 0xa8 : 0;
    if (!readable(base, (size_t)count * 0xa8)) count = 0;
    static snap_region post[4097];
    static uint32_t npost;
    snapshot(post, &npost);
    uint32_t npre = g_pre_frame == (int)g_frame ? g_npre : 0, pre_ok = 0, post_ok = 0;
    for (uint32_t i = 0; i < npre; ++i) pre_ok += g_pre[i].bytes != NULL;
    for (uint32_t i = 0; i < npost; ++i) post_ok += post[i].bytes != NULL;
    const uint32_t header[8] = {0x52524e53u, 4,      g_frame, count, base, (uint32_t)RD32(0x0049e664),
                                pre_ok,      post_ok};
    fwrite(header, 4, 8, out);
    put_regions(out, g_pre, npre);
    put_regions(out, post, npost);
    for (uint32_t i = 0; i < count; ++i) {
        const unsigned char* n = (const unsigned char*)(uintptr_t)(base + i * 0xa8);
        fwrite(n, 1, 0xa8, out);
        uint32_t res = 0;
        memcpy(&res, n + 8, 4);
        unsigned char rh[0x60];
        uint32_t hash = 0, size = 0;
        memset(rh, 0, sizeof rh);
        if (readable(res, 0x60)) {
            memcpy(rh, (void*)(uintptr_t)res, 0x60);
            uint32_t magic, w, h, mip;
            memcpy(&magic, rh, 4);
            memcpy(&w, rh + 0x20, 4);
            memcpy(&h, rh + 0x24, 4);
            memcpy(&mip, rh + 0x38, 4);
            if (magic == 0xbc614e && w > 0 && w <= 1024 && h > 0 && h <= 1024 &&
                readable(mip, (size_t)w * h)) {
                size = w * h;
                hash = fnv((const unsigned char*)(uintptr_t)mip, size);
            }
        }
        fwrite(rh, 1, 0x60, out);
        fwrite(&hash, 4, 1, out);
        fwrite(&size, 4, 1, out);
    }
    dump_rider_objects(out);
    fclose(out);
    g_scene_written = 1;
    g_scene_nodes = count;
}

// 0x0044d300 locks the surface while drawing and stores its view-origin pixel
// pointer/pitch at 0x004b7ec0/0x004b7ec4. Capture before 0x0044c780 unlocks it.
// Post-dashboard capture (plan 11a Task 2 Step 5): wrap the 0x00446c30 call at
// 0x0044c7b9 inside 0x0044c780. The dashboard draws via GetDC after the 3D unlock;
// dump the same base/pitch surface right after it returns, before present.
static void dump_post_dash(void) {
    const uintptr_t base = (uint32_t)RD32(0x004b7ec0);
    const int width = RD32(0x0047539c), height = RD32(0x004753a0), pitch = RD32(0x004b7ec4);
    if (width < 1 || width > 1280 || height < 1 || height > 960 || pitch < width || pitch > 16384) return;
    if (!readable(base, (size_t)(height - 1) * pitch + width)) return;
    char path[MAX_PATH];
    const char* slash = strrchr(g_output, '\\');
    const size_t prefix = slash ? (size_t)(slash - g_output) + 1 : 0;
    if (prefix > sizeof path - 64) return;
    memcpy(path, g_output, prefix);
    int count = snprintf(path + prefix, sizeof path - prefix, "%s-d%u.raw", g_scenario, g_frame);
    if (count < 0 || (size_t)count >= sizeof path - prefix) return;
    FILE* pixels = fopen(path, "wb");
    if (!pixels) return;
    int ok = 1;
    for (int row = 0; row < height; ++row)
        if (fwrite((void*)(base + (size_t)row * pitch), 1, width, pixels) != (size_t)width) {
            ok = 0;
            break;
        }
    if (fclose(pixels)) ok = 0;
    (void)ok;
}
static void __cdecl hook_dash(void) {
    ((void(__cdecl*)(void))0x00446c30)();
    if (capture_frame()) dump_post_dash();
}
static void __cdecl hook_draw(void) {
    if (capture_frame() && RD8(0x00475620)) dump_scene();
    ((rr_frame_fn)0x00450420)();
    g_pixel_written = 0;
    if (!g_out || !RD8(0x00475620)) return;
    const uintptr_t base = (uint32_t)RD32(0x004b7ec0);
    const int width = RD32(0x0047539c), height = RD32(0x004753a0), pitch = RD32(0x004b7ec4);
    if (width < 1 || width > 1280 || height < 1 || height > 960 || pitch < width || pitch > 16384) return;
    if (!readable(base, (size_t)(height - 1) * pitch + width)) return;
    if (g_raster && g_frame == 0) capture_raster(base, width, height, pitch);
    if (!g_pixels || g_frame % (uint32_t)g_pixel_every) return;
    char path[MAX_PATH];
    const char* slash = strrchr(g_output, '\\');
    const size_t prefix = slash ? (size_t)(slash - g_output) + 1 : 0;
    if (prefix > sizeof path - 64) return;
    memcpy(path, g_output, prefix);
    int count = snprintf(path + prefix, sizeof path - prefix, "%s-f%u.raw", g_scenario, g_frame);
    if (count < 0 || (size_t)count >= sizeof path - prefix) return;
    FILE* pixels = fopen(path, "wb");
    if (!pixels) return;
    int ok = 1;
    for (int row = 0; row < height; ++row)
        if (fwrite((void*)(base + (size_t)row * pitch), 1, width, pixels) != (size_t)width) {
            ok = 0;
            break;
        }
    if (fclose(pixels)) ok = 0;
    if (ok) {
        g_pixel_written = 1;
        g_pixel_width = width;
        g_pixel_height = height;
        g_pixel_pitch = pitch;
    }
}

static void dump_raster(void) {
    char key[96];
    if (g_raster_count) {
        rr_log("raster.count", g_raster_count);
        for (int c = 0; c < g_raster_count; ++c) {
            snprintf(key, sizeof key, "raster.%d.width", c);
            rr_log(key, g_raster_cases[c].width);
            snprintf(key, sizeof key, "raster.%d.height", c);
            rr_log(key, g_raster_cases[c].height);
            snprintf(key, sizeof key, "raster.%d.colour", c);
            rr_log(key, g_raster_cases[c].colour);
            for (int i = 0; i < 8; ++i) {
                snprintf(key, sizeof key, "raster.%d.vertex.%d", c, i);
                rr_log(key, g_raster_cases[c].vertices[i]);
                snprintf(key, sizeof key, "raster.%d.input.%d", c, i);
                rr_log(key, g_raster_cases[c].input[i]);
            }
            for (int i = 0; i < 4; ++i) {
                snprintf(key, sizeof key, "raster.%d.clip.%d", c, i);
                rr_log(key, g_raster_cases[c].clip[i]);
            }
        }
    }
}

static void capture_view(void) {
    const int shift = RD32(0x00481fa8), mask = RD32(0x00481ff4);
    for (int mode = 0; mode < 2; ++mode) {
        RD32(0x00481fa8) = mode ? 15 : 16;
        RD32(0x00481ff4) = mode ? (int)0xfffffffeu : -1;
        for (int i = 0; i < 12; ++i) {
            int source[32] = {0}, destination[32] = {0};
            // No sprite pointer: 0x00410080 returns after copying null.
            source[0x44 / 4] = g_view_inputs[i];
            ((void(__fastcall*)(int*, int*))0x0040ffc0)(destination, source);
            g_view_outputs[mode][i] = destination[0x18 / 4];
        }
    }
    RD32(0x00481fa8) = shift;
    RD32(0x00481ff4) = mask;
    char key[96];
    for (int mode = 0; mode < 2; ++mode)
        for (int i = 0; i < 12; ++i) {
            snprintf(key, sizeof key, "view.map.%d.%d.input", mode, i);
            rr_log(key, g_view_inputs[i]);
            snprintf(key, sizeof key, "view.map.%d.%d.output", mode, i);
            rr_log(key, g_view_outputs[mode][i]);
        }
}

static void terrain_word(unsigned char* b, int p, uint32_t v) {
    memcpy(b + p, &v, 4);
}
static void capture_terrain(void) {
    const int prior_mode = RD32(0x004753b0), prior_random = RD32(0x0049bce8);
    RD32(0x004753b0) = 3; // original rand wrapper returns supplied nonnegative value
    const int values[3] = {0, 123, 32767};
    char key[128];
    for (int kind = 0; kind < 3; ++kind)
        for (int random = 0; random < 3; ++random)
            for (int reverse = 0; reverse < 2; ++reverse) {
                unsigned char b[248] = {0};
                int iterator[80] = {0}, output[100] = {0};
                const int size = kind == 0 ? 144 : 248;
                memcpy(b, kind == 0 ? "DLBR" : kind == 1 ? "NTMR" : "LIHR", 4);
                terrain_word(b, 4, size);
                terrain_word(b, 8, kind == 2 ? 3 : 64);
                int selectors = 16;
                if (kind == 1) {
                    terrain_word(b, 12, 3);
                    terrain_word(b, 16, 3);
                    selectors = 120;
                    for (int side = 0; side < 2; ++side)
                        for (int j = 0; j < 3; ++j) {
                            const int p = 24 + (side * 3 + j) * 16;
                            const unsigned char params[8] = {
                                0, (unsigned char)(3 + j), (unsigned char)(20 + side * 5), 4, 30, 6, 5, 40};
                            terrain_word(b, p, j == 2 ? 64 : j * 20);
                            memcpy(b + p + 4, params, 8);
                            terrain_word(b, p + 12, side ? -40 : 70);
                        }
                } else if (kind == 2) {
                    for (int j = 0; j < 3; ++j) {
                        const int p = 20 + 76 * j;
                        terrain_word(b, p, j * 20);
                        terrain_word(b, p + 4, (j + 1) * 20);
                        b[p + 8] = 10 + j;
                        b[p + 9] = 20 + j;
                        for (int point = 0; point < 8; ++point) {
                            const unsigned char params[8] = {(unsigned char)(10 + point * 12),
                                                             (unsigned char)(20 + point * 12),
                                                             3,
                                                             (unsigned char)(-10 + point),
                                                             (unsigned char)(12 + point),
                                                             4,
                                                             1,
                                                             5};
                            memcpy(b + p + 12 + 8 * point, params, 8);
                        }
                    }
                }
                if (kind != 2)
                    for (int j = 0; j < 64; ++j) {
                        b[selectors + 2 * j] = j;
                        b[selectors + 2 * j + 1] = 63 - j;
                    }
                RD32(0x0049bce8) = values[random];
                ((void(__fastcall*)(int*, void*, int))0x004510c0)(iterator, b, reverse ? 48 * 256 : 0);
                for (int tick = 0; tick < 48; ++tick) {
                    memset(output, 0, sizeof output);
                    output[0xe8 / 4] = -500;
                    output[0xec / 4] = 500;
                    ((void(__fastcall*)(int*, int*))0x00451910)(iterator, output);
                    snprintf(key, sizeof key, "terrain.%d.%d.%d.%d.type", kind, random, reverse, tick);
                    rr_log(key, ((unsigned char*)output)[0x3c]);
                    for (int side = 0; side < 2; ++side) {
                        snprintf(key, sizeof key, "terrain.%d.%d.%d.%d.selector.%d", kind, random, reverse,
                                 tick, side);
                        rr_log(key, ((unsigned char*)output)[0x148 + side]);
                    }
                    if (kind == 1)
                        for (int j = 0; j < 6; ++j) {
                            snprintf(key, sizeof key, "terrain.%d.%d.%d.%d.wall.%d", kind, random, reverse,
                                     tick, j);
                            rr_log(key, output[0x14c / 4 + j]);
                        }
                    if (kind == 2)
                        for (int j = 0; j < 12; ++j) {
                            snprintf(key, sizeof key, "terrain.%d.%d.%d.%d.point.%d", kind, random, reverse,
                                     tick, j);
                            rr_log(key, output[0xf0 / 4 + j]);
                        }
                    if (kind == 2)
                        for (int j = 0; j < 6; ++j) {
                            snprintf(key, sizeof key, "terrain.%d.%d.%d.%d.shade.%d", kind, random, reverse,
                                     tick, j);
                            rr_log(key, output[0x120 / 4 + j]);
                        }
                    ((void(__fastcall*)(int*, int))0x004516e0)(iterator, reverse ? -256 : 256);
                }
            }
    RD32(0x004753b0) = prior_mode;
    RD32(0x0049bce8) = prior_random;
}

/* Plan 10: seeded component coverage of all eight hill point windows. Calls
 * actual 0x004510c0/0x004516e0; no native-model or rand detour substitutes. */
static int capture_terrain_seeded(void) {
    static const uint32_t seeds[] = {0, 1, 123, 32767, 0x12345678u, 0xdeadbeefu};
    static const struct {
        const char* name;
        int offset;
    } fields[] = {{"start", 0x44}, {"end", 0x64}, {"x", 0x84}, {"height", 0x88}, {"dx", 0xc4}, {"dh", 0xc8}};
    uint32_t* state = sim_rand_state();
    if (!state) return 0;
    const uint32_t prior_state = *state;
    const int prior_mode = RD32(0x004753b0), prior_random = RD32(0x0049bce8);
    unsigned char b[248] = {0};
    char key[128];
    /* Byte-for-byte fixture(2) from tests/terrain_test.cpp. */
    memcpy(b, "LIHR", 4);
    terrain_word(b, 4, sizeof b);
    terrain_word(b, 8, 3);
    for (int j = 0; j < 3; ++j) {
        const int p = 20 + 76 * j;
        terrain_word(b, p, j * 20);
        terrain_word(b, p + 4, (j + 1) * 20);
        b[p + 8] = 10 + j;
        b[p + 9] = 20 + j;
        for (int point = 0; point < 8; ++point) {
            const unsigned char params[8] = {(unsigned char)(10 + point * 12),
                                             (unsigned char)(20 + point * 12),
                                             3,
                                             (unsigned char)(-10 + point),
                                             (unsigned char)(12 + point),
                                             4,
                                             1,
                                             5};
            memcpy(b + p + 12 + 8 * point, params, 8);
        }
    }
    RD32(0x004753b0) = 0; /* 0x00441750: CRT stream rather than fixed mode 3. */
    rr_log("terrain.seeded.schema", 1);
    for (int seed = 0; seed < 6; ++seed)
        for (int path = 0; path < 3; ++path) {
            int iterator[80] = {0};
            *state = seeds[seed];
            ((void(__fastcall*)(int*, void*, int))0x004510c0)(iterator, b, path == 1 ? 48 * 256 : 0);
            const int steps = path == 2 ? 64 : 48;
            for (int tick = 0; tick < steps; ++tick) {
                snprintf(key, sizeof key, "terrain.seeded.%d.%d.%d.rng", seed, path, tick);
                rr_log(key, (long long)*state);
                snprintf(key, sizeof key, "terrain.seeded.%d.%d.%d.position", seed, path, tick);
                rr_log(key, iterator[0xc / 4]);
                snprintf(key, sizeof key, "terrain.seeded.%d.%d.%d.last_sample", seed, path, tick);
                rr_log(key, iterator[0x10 / 4]);
                snprintf(key, sizeof key, "terrain.seeded.%d.%d.%d.record", seed, path, tick);
                rr_log(key, iterator[0x14 / 4]);
                for (int point = 0; point < 8; ++point)
                    for (unsigned f = 0; f < sizeof fields / sizeof fields[0]; ++f) {
                        const int stride = f < 2 ? 4 : 8;
                        snprintf(key, sizeof key, "terrain.seeded.%d.%d.%d.point.%d.%s", seed, path, tick,
                                 point, fields[f].name);
                        rr_log(key, iterator[(fields[f].offset + point * stride) / 4]);
                    }
                const int delta = path == 1 || (path == 2 && tick >= 32) ? -256 : 256;
                ((void(__fastcall*)(int*, int))0x004516e0)(iterator, delta);
            }
            snprintf(key, sizeof key, "terrain.seeded.%d.%d.final_rng", seed, path);
            rr_log(key, (long long)*state);
        }
    *state = prior_state;
    RD32(0x004753b0) = prior_mode;
    RD32(0x0049bce8) = prior_random;
    return 1;
}

/* Run from a normal frame hook, after DllMain has returned. ExitProcess is
 * intentionally outside the loader lock; this mode never runs a race. */
static void capture_terrain_seeded_and_exit(void) {
    if (!g_out) ExitProcess(1);
    fputs("frame\t0\n", g_out);
    if (!capture_terrain_seeded()) {
        fputs("# seeded terrain CRT state unavailable\n", g_out);
        fclose(g_out);
        g_out = NULL;
        ExitProcess(1);
    }
    fputs("end\n", g_out);
    const int failed = ferror(g_out) || fflush(g_out) != 0;
    const int close_failed = fclose(g_out) != 0;
    g_out = NULL;
    ExitProcess(failed || close_failed ? 1 : 0);
}

static void capture_hill_projection(void) {
    const int old_x = RD32(0x004bc7d8), old_height = RD32(0x004bc7dc), old_width = RD32(0x0046667c);
    const int scales[6] = {32768, 65535, 65536, 98304, 163840, 0x7fffffff};
    const int positions[8] = {0, -600, 600, -40000, 40000, 0x7fffffff, (int)0x80000000u, 1200};
    char key[96];
    for (int c = 0; c < 96; ++c) {
        int source[100] = {0}, road[100] = {0};
        road[0xc / 4] = (int)(uintptr_t)source;
        road[0x10 / 4] = positions[c % 8];
        road[0x14 / 4] = (c % 5) * 80 - 160;
        road[0x20 / 4] = scales[c / 16];
        RD32(0x004bc7d8) = positions[(c + 3) % 8];
        RD32(0x004bc7dc) = 150 + c * 17;
        RD32(0x0046667c) = c % 2 ? 321 : 320;
        road[0x4c / 4] =
            (int)((uint32_t)(RD32(0x004bc7dc) - road[0x14 / 4]) * (uint32_t)road[0x20 / 4] + 0x5a0000u);
        for (int i = 0; i < 6; ++i) {
            source[0xf0 / 4 + i * 2] = (i % 2 ? 1 : -1) * (i * 200 + c * 80);
            source[0xf0 / 4 + i * 2 + 1] = i * 37 - c * 11;
        }
        const int row = ((int(__fastcall*)(int*))0x0041d6d0)(road);
#define HILL_LOG(label, value)                                                                               \
    do {                                                                                                     \
        snprintf(key, sizeof key, "hill.project.%d.%s", c, label);                                           \
        rr_log(key, value);                                                                                  \
    } while (0)
        HILL_LOG("world_x", road[0x10 / 4]);
        HILL_LOG("world_y", road[0x14 / 4]);
        HILL_LOG("camera_x", RD32(0x004bc7d8));
        HILL_LOG("camera_height", RD32(0x004bc7dc));
        HILL_LOG("width", RD32(0x0046667c));
        HILL_LOG("scale", road[0x20 / 4]);
        HILL_LOG("maximum_row", row);
#undef HILL_LOG
        for (int i = 0; i < 12; ++i) {
            snprintf(key, sizeof key, "hill.project.%d.input.%d", c, i);
            rr_log(key, source[0xf0 / 4 + i]);
            snprintf(key, sizeof key, "hill.project.%d.output.%d", c, i);
            rr_log(key, road[0x70 / 4 + i]);
        }
    }
    RD32(0x004bc7d8) = old_x;
    RD32(0x004bc7dc) = old_height;
    RD32(0x0046667c) = old_width;
}

static void capture_polygon(void) {
    const int prior_node = RD32(0x004c5d88);
    uint32_t random = 1;
    char key[96];
    for (int c = 0; c < 2048; ++c) {
        int points[8], source[8];
        unsigned char node[80] = {0};
        for (int i = 0; i < 8; ++i) {
            random = random * 1664525u + 1013904223u;
            points[i] = (int)(random >> 16) % 2001 - 1000;
        }
        if (c % 7 == 0) {
            points[2] = points[0];
            points[3] = points[1];
        }
        if (c % 11 == 0)
            for (int i = 0; i < 4; ++i) points[2 * i + 1] = 10;
        if (c % 13 == 0)
            for (int i = 0; i < 8; ++i) points[i] *= 60;
        const int flags = c % 3 == 0 ? 0 : c % 3 == 1 ? 8 : 4;
        node[0x3d] = c % 2;
        RD32(0x004c5d88) = (int)(uintptr_t)node;
        memcpy(source, points, sizeof points);
        int accepted = ((int(__fastcall*)(int*, int))0x0044ce60)(points, flags) & 255;
        snprintf(key, sizeof key, "polygon.%d.flags", c);
        rr_log(key, flags);
        snprintf(key, sizeof key, "polygon.%d.accepted", c);
        rr_log(key, accepted);
        snprintf(key, sizeof key, "polygon.%d.flip", c);
        rr_log(key, node[0x3d]);
        for (int i = 0; i < 8; ++i) {
            snprintf(key, sizeof key, "polygon.%d.input.%d", c, i);
            rr_log(key, source[i]);
            snprintf(key, sizeof key, "polygon.%d.output.%d", c, i);
            rr_log(key, points[i]);
        }
    }
    RD32(0x004c5d88) = prior_node;
}

static void capture_texture_spans(void) {
    const int prior_texture = RD32(0x004c5d20), prior_output = RD32(0x004c5d50);
    const uint32_t functions[2][9] = {
        {0x418910, 0x418bb0, 0x418e50, 0x4190f0, 0x419390, 0x419630, 0x4198d0, 0x419b70, 0x419e10},
        {0x41a0b0, 0x41a420, 0x41a780, 0x41aaf0, 0x41ae60, 0x41b1d0, 0x41b540, 0x41b8b0, 0x41bc20}};
    const int counts[7] = {0, 1, 2, 3, 4, 7, 17};
    unsigned char* texture = malloc(8192);
    if (!texture) return;
    for (int i = 0; i < 8192; ++i) texture[i] = (unsigned char)(i * 37 + i / 7);
    RD32(0x004c5d20) = (int)(uintptr_t)texture;
    char key[96];
    int c = 0;
    for (int transparent = 0; transparent < 2; ++transparent)
        for (int shift = 0; shift < 9; ++shift)
            for (int alignment = 0; alignment < 4; ++alignment)
                for (int length = 0; length < 7; ++length)
                    for (int moving = 0; moving < 2; ++moving, ++c) {
                        uint32_t output[16];
                        memset(output, 125, sizeof output);
                        const int row = 8 * 65536 + (c % 2 ? 65535 : 32767),
                                  column = (1 << shift) / 2 * 65536 + 12345;
                        const int row_step = moving ? (c % 3 ? 2048 : -768) : 0,
                                  column_step = c % 2 ? 512 : -128;
                        RD32(0x004c5d50) = (int)(uintptr_t)((unsigned char*)output + alignment);
                        ((void(__fastcall*)(int, int, int, int, int))(
                            uintptr_t)functions[transparent][shift])(counts[length], row, column, column_step,
                                                                     row_step);
#define SPAN_LOG(label, value)                                                                               \
    do {                                                                                                     \
        snprintf(key, sizeof key, "texture.span.%d.%s", c, label);                                           \
        rr_log(key, value);                                                                                  \
    } while (0)
                        SPAN_LOG("transparent", transparent);
                        SPAN_LOG("shift", shift);
                        SPAN_LOG("alignment", alignment);
                        SPAN_LOG("count", counts[length]);
                        SPAN_LOG("row", row);
                        SPAN_LOG("column", column);
                        SPAN_LOG("row_step", row_step);
                        SPAN_LOG("column_step", column_step);
#undef SPAN_LOG
                        for (int i = 0; i < 32; ++i) {
                            snprintf(key, sizeof key, "texture.span.%d.output.%d", c, i);
                            rr_log(key, ((unsigned char*)output)[i]);
                        }
                    }
    RD32(0x004c5d20) = prior_texture;
    RD32(0x004c5d50) = prior_output;
    free(texture);
}

static void capture_texture_quads(void) {
    const uint32_t addresses[12] = {0x4b7eb0, 0x4b7eb4, 0x4b7eb8, 0x4b7ebc, 0x4b7ec0, 0x4b7ec4,
                                    0x4c5d88, 0x4c5d60, 0x4c5d64, 0x4c5d68, 0x4c5d6c, 0x484278};
    int saved[12];
    for (int i = 0; i < 12; ++i) saved[i] = RD32(addresses[i]);
    unsigned char* pixels = malloc(640 * 480);
    unsigned char* texture = malloc(1344);
    unsigned char* edges = malloc(11520);
    unsigned char uv_saved[32];
    const int prior_texture = RD32(0x4c5d20), prior_output = RD32(0x4c5d50);
    if (!pixels || !texture || !edges) {
        free(pixels);
        free(texture);
        free(edges);
        return;
    }
    memcpy(edges, (void*)0x484280, 5760);
    memcpy(edges + 5760, (void*)0x482bf8, 5760);
    memcpy(uv_saved, (void*)0x4c5d30, 32);
    for (int level = 0; level < 3; ++level) {
        int offset = level == 0   ? 0
                     : level == 1 ? 1024
                                  : 1280,
            size = level == 0   ? 1024
                   : level == 1 ? 256
                                : 64;
        for (int i = 0; i < size; ++i) texture[offset + i] = (unsigned char)(i * 37 + i / 7 + level * 53);
    }
    RD32(0x4b7eb0) = 0;
    RD32(0x4b7eb4) = 0;
    RD32(0x4b7eb8) = 640;
    RD32(0x4b7ebc) = 479;
    RD32(0x4b7ec0) = (int)(uintptr_t)pixels;
    RD32(0x4b7ec4) = 640;
    char key[96];
    for (int c = 0; c < 64; ++c) {
        int resource[32] = {0}, node[20] = {0};
        short vertices[16];
        resource[3] = c % 2 ? 2 : 0;
        resource[0x20 / 4] = 32;
        resource[0x28 / 4] = 3;
        resource[0x38 / 4] = (int)(uintptr_t)texture;
        resource[0x44 / 4] = (int)(uintptr_t)(texture + 1024);
        resource[0x50 / 4] = (int)(uintptr_t)(texture + 1280);
        node[1] = (int)(uintptr_t)resource;
        node[2] = 0;
        node[3] = 0;
        node[4] = 32;
        node[5] = 32;
        RD32(0x4c5d88) = (int)(uintptr_t)node;
        int top = c % 8 * 70 - 80, bottom = top + 20 + (c * 31) % 400, left = c % 5 * 90 - 100,
            right = left + 20 + (c * 67) % 700;
        if (c >= 48) {
            left = 20 + 30 * (c % 5);
            right = left + 1 + (c * 7) % 25;
            bottom = top + 1 + (c * 11) % 25;
        }
        if (c == 50 || c == 51) {
            left = 80;
            top = 80;
            right = 86;
            bottom = 86;
        }
        int points[8] = {left,
                         top,
                         right,
                         top,
                         right - (c % 4) * ((right - left) / 8),
                         bottom,
                         left + (c % 3) * ((right - left) / 9),
                         bottom};
        if (c % 7 == 0 && c < 48) {
            points[3] = top + 9;
            points[7] = bottom - 11;
        }
        RD32(0x4c5d60) = left;
        RD32(0x4c5d68) = right;
        RD32(0x4c5d64) = top;
        RD32(0x4c5d6c) = bottom;
        for (int i = 0; i < 4; ++i) {
            vertices[i * 4] = (short)points[i * 2];
            vertices[i * 4 + 1] = (short)points[i * 2 + 1];
            vertices[i * 4 + 2] = i < 2 ? 1 : 31;
            vertices[i * 4 + 3] = i == 0 || i == 3 ? 1 : 31;
        }
        memcpy((void*)0x4c5d30, vertices, sizeof vertices);
        memset(pixels, 125, 640 * 480);
        ((void(__cdecl*)(void))0x4180b0)();
        int level = RD32(0x4c5d20) == (int)(uintptr_t)texture            ? 0
                    : RD32(0x4c5d20) == (int)(uintptr_t)(texture + 1024) ? 1
                                                                         : 2;
        snprintf(key, sizeof key, "texture.quad.%d.level", c);
        rr_log(key, level);
        snprintf(key, sizeof key, "texture.quad.%d.transparent", c);
        rr_log(key, c % 2);
        for (int i = 0; i < 16; ++i) {
            snprintf(key, sizeof key, "texture.quad.%d.vertex.%d", c, i);
            rr_log(key, (unsigned short)vertices[i]);
        }
        char path[1024];
        const char* slash = strrchr(g_output, '\\');
        const size_t prefix = slash ? (size_t)(slash - g_output) + 1 : 0;
        if (prefix < sizeof path - 64) {
            memcpy(path, g_output, prefix);
            snprintf(path + prefix, sizeof path - prefix, "%s-texture%d.raw", g_scenario, c);
            FILE* out = fopen(path, "wb");
            if (out) {
                fwrite(pixels, 1, 640 * 480, out);
                fclose(out);
            }
        }
    }
    memcpy((void*)0x484280, edges, 5760);
    memcpy((void*)0x482bf8, edges + 5760, 5760);
    memcpy((void*)0x4c5d30, uv_saved, 32);
    RD32(0x4c5d20) = prior_texture;
    RD32(0x4c5d50) = prior_output;
    for (int i = 0; i < 12; ++i) RD32(addresses[i]) = saved[i];
    free(edges);
    free(texture);
    free(pixels);
}

static uint32_t cel_word(const unsigned char* b, size_t p) {
    uint32_t v;
    memcpy(&v, b + p, 4);
    return v;
}
static int cel_dimension(uint32_t value) {
    return value & 0x80000000u ? (int)(1u << (value & 31)) : value & 511;
}
static void capture_cels(void) {
    const char* files[6] = {"GLOBAL.RSC",         "COURSES\\CANYN.CRS", "COURSES\\CITY.CRS",
                            "COURSES\\HIWAY.CRS", "COURSES\\MEDLY.CRS", "COURSES\\NAPA.CRS"};
    unsigned char dimension_table[4096];
    memcpy(dimension_table, (void*)0x4b6e70, sizeof dimension_table);
    ((void(__cdecl*)(void))0x44fb10)();
    char key[128];
    int count = 0;
    for (int file = 0; file < 6; ++file) {
        char source_path[256];
        snprintf(source_path, sizeof source_path, "D:\\ROADRASH\\DATA\\%s", files[file]);
        FILE* in = fopen(source_path, "rb");
        if (!in) continue;
        fseek(in, 0, SEEK_END);
        long size = ftell(in);
        rewind(in);
        if (size < 24 || size > 10000000) {
            fclose(in);
            continue;
        }
        unsigned char* bytes = malloc(size);
        if (!bytes) {
            fclose(in);
            continue;
        }
        if (fread(bytes, 1, size, in) != (size_t)size) {
            free(bytes);
            fclose(in);
            continue;
        }
        fclose(in);
        const uint32_t table = cel_word(bytes, 16);
        if (table > (uint32_t)size || 16 > (uint32_t)size - table) {
            free(bytes);
            continue;
        }
        const uint32_t entries = cel_word(bytes, table + 8);
        if (entries > ((uint32_t)size - table - 16) / 32) {
            free(bytes);
            continue;
        }
        for (uint32_t i = 0; i < entries; ++i) {
            const size_t row = table + 16 + i * 32;
            if (cel_word(bytes, row) != 0x43454c20) continue;
            const uint32_t id = cel_word(bytes, row + 4), offset = cel_word(bytes, row + 8),
                           extent = cel_word(bytes, row + 12);
            if (offset > (uint32_t)size || extent > (uint32_t)size - offset || extent < 12) continue;
            int* ccb = ((int*(__fastcall*)(void*, void*))0x40b800)(bytes + offset, NULL);
            if (!ccb) continue;
            ++count;
            const uint32_t data = (uint32_t)ccb[2];
#define CEL_LOG(label, value)                                                                                \
    do {                                                                                                     \
        snprintf(key, sizeof key, "cel.%d.%u.%s", file, id, label);                                          \
        rr_log(key, value);                                                                                  \
    } while (0)
            CEL_LOG("width", cel_dimension(ccb[15]));
            CEL_LOG("height", cel_dimension(ccb[16]));
            CEL_LOG("flags", (uint32_t)ccb[0]);
            const uint32_t magic = readable(data, 4) ? cel_word((void*)(uintptr_t)data, 0) : 0;
            CEL_LOG("magic", magic);
            if (magic == 0xbc614e && readable(data, 56)) {
                const unsigned char* body = (void*)(uintptr_t)data;
                const uint32_t width = cel_word(body, 32), height = cel_word(body, 36),
                               levels = cel_word(body, 40);
                CEL_LOG("texture_flags", cel_word(body, 12));
                CEL_LOG("levels", levels);
                for (int bound = 0; bound < 4; ++bound) {
                    short value;
                    memcpy(&value, body + 4 + bound * 2, 2);
                    snprintf(key, sizeof key, "cel.%d.%u.bound.%d", file, id, bound);
                    rr_log(key, value);
                }
                if (width && width <= 256 && height && height <= 512 && levels <= 9)
                    for (uint32_t level = 0; level < levels; ++level) {
                        const uint32_t pointer = cel_word(body, 56 + 12 * level);
                        const size_t pixels = (width >> level) * ((height + (1u << level) - 1) >> level);
                        if (!readable(pointer, pixels)) continue;
                        char output_path[1024];
                        const char* slash = strrchr(g_output, '\\');
                        const size_t prefix = slash ? (size_t)(slash - g_output) + 1 : 0;
                        memcpy(output_path, g_output, prefix);
                        snprintf(output_path + prefix, sizeof output_path - prefix, "%s-cel%d-%u-mip%u.raw",
                                 g_scenario, file, id, level);
                        FILE* out = fopen(output_path, "wb");
                        if (out) {
                            fwrite((void*)(uintptr_t)pointer, 1, pixels, out);
                            fclose(out);
                        }
                    }
            }
#undef CEL_LOG
        }
        free(bytes);
    }
    rr_log("cel.count", count);
    memcpy((void*)0x4b6e70, dimension_table, sizeof dimension_table);
}

static void capture_node_render(void) {
    const uint32_t addresses[] = {0x4b7eb0, 0x4b7eb4, 0x4b7eb8, 0x4b7ebc, 0x4b7ec0, 0x4b7ec4, 0x4c5d88,
                                  0x4c5d60, 0x4c5d64, 0x4c5d68, 0x4c5d6c, 0x484278, 0x4c5d20, 0x4c5d50,
                                  0x466680, 0x481fe0, 0x481fa8, 0x481ff4, 0x475648, 0x481fb0, 0x481fbc};
    int saved[sizeof addresses / sizeof addresses[0]];
    for (unsigned i = 0; i < sizeof addresses / sizeof addresses[0]; ++i) saved[i] = RD32(addresses[i]);
    const unsigned char locked = RD8(0x475620);
    RD8(0x475620) = 1;
    RD32(0x466680) = 1;
    RD32(0x481fb0) = 0x10;
    RD32(0x481fbc) = 0x10;
    unsigned char* pixels = malloc(640 * 480);
    unsigned char* texture = malloc(8192 + 65536);
    unsigned char* edges = malloc(11520);
    unsigned char uv_saved[32];
    if (!pixels || !texture || !edges) {
        free(pixels);
        free(texture);
        free(edges);
        goto restore;
    }
    memcpy(edges, (void*)0x484280, 5760);
    memcpy(edges + 5760, (void*)0x482bf8, 5760);
    memcpy(uv_saved, (void*)0x4c5d30, 32);
    for (int level = 0; level < 3; ++level) {
        const int offset = level == 0 ? 0 : level == 1 ? 1024 : 1280, size = 1024 >> (level * 2);
        for (int i = 0; i < size; ++i) texture[offset + i] = (unsigned char)(i * 37 + i / 7 + level * 53);
    }
    const int masks[3] = {16, 48, 112}, flags[6] = {0, 2, 4, 8, 16, 32};
    char key[96];
    for (int c = 0; c < 33; ++c) {
        if (c == 32)
            for (int i = 0; i < 8192 + 65536; ++i) texture[i] = (unsigned char)(i * 37 + i / 7);
        const int high = c == 32 ? 1 : c % 2, width = high ? 640 : 320, height = high ? 480 : 240;
        int source[12][42] = {{0}}, resource[12][32] = {{0}}, legacy = 0;
        RD32(0x481fa8) = high ? 15 : 16;
        RD32(0x481ff4) = high ? (int)0xfffffffeu : -1;
        RD32(0x475648) = masks[c % 3];
        RD32(0x4b7eb0) = 0;
        RD32(0x4b7eb4) = 0;
        RD32(0x4b7eb8) = width;
        RD32(0x4b7ebc) = height - 1;
        RD32(0x4b7ec0) = (int)(uintptr_t)pixels;
        RD32(0x4b7ec4) = width;
        memset(pixels, 125, width * height);
        for (int n = 0; n < 12; ++n) {
            source[n][1] = n < 11 ? (int)(uintptr_t)source[n + 1] : 0;
            source[n][2] = (int)(uintptr_t)(n % 4 == 0 ? &legacy : resource[n]);
            source[n][3] = n == 11 ? 0x40000000 : 0;
            resource[n][0] = 0xbc614e;
            resource[n][2] = 31 | (31 << 16);
            resource[n][3] = flags[(c + n) % 6];
            resource[n][8] = 32;
            resource[n][9] = 32;
            resource[n][10] = 3;
            resource[n][11] = 196 + n;
            resource[n][14] = (int)(uintptr_t)texture;
            resource[n][17] = (int)(uintptr_t)(texture + 1024);
            resource[n][20] = (int)(uintptr_t)(texture + 1280);
            unsigned char* bytes = (void*)source[n];
            bytes[0x64] = (c + n) % 2;
            bytes[0x65] = (c + n) % 3 == 0;
            bytes[0x66] = n % 4 == 0;
            bytes[0x67] = 20 + n;
            const int left = (n * 37 + c * 13) % 420 - 60, top = (n * 29 + c * 17) % 320 - 40,
                      right = left + 20 + (n * 23 + c * 7) % 180, bottom = top + 10 + (n * 13 + c * 3) % 100;
            const int points[8] = {left,      top,    right,    top + ((n + c) % 3),
                                   right - 3, bottom, left + 5, bottom};
            for (int i = 0; i < 8; ++i)
                source[n][0x44 / 4 + i] = points[i] * 65536 + ((c + n + i) * 1777) % 65536;
            if (c == 32) {
                source[n][2] = (int)(uintptr_t)resource[n];
                resource[n][2] = 255 | (31 << 16);
                resource[n][3] = 0;
                resource[n][8] = 256;
                resource[n][9] = 32;
                resource[n][10] = 2;
                resource[n][17] = (int)(uintptr_t)(texture + 8192);
                bytes[0x64] = 0;
                bytes[0x65] = 1;
                bytes[0x66] = 0;
                const int edge[8] = {15934114, 5943941, 20028589, 5784981,
                                     19218217, 5744193, 16886329, 5904753};
                for (int i = 0; i < 8; ++i) source[n][0x44 / 4 + i] = n ? 1000 * 65536 : edge[i];
            }
            snprintf(key, sizeof key, "nodes.%d.%d.flat", c, n);
            rr_log(key, c == 32 ? -1 : n % 4 == 0 ? 20 + n : -1);
            snprintf(key, sizeof key, "nodes.%d.%d.flags", c, n);
            rr_log(key, resource[n][3]);
            snprintf(key, sizeof key, "nodes.%d.%d.index", c, n);
            rr_log(key, resource[n][11]);
            snprintf(key, sizeof key, "nodes.%d.%d.flip_u", c, n);
            rr_log(key, bytes[0x64]);
            snprintf(key, sizeof key, "nodes.%d.%d.flip_v", c, n);
            rr_log(key, bytes[0x65]);
            for (int i = 0; i < 8; ++i) {
                snprintf(key, sizeof key, "nodes.%d.%d.point.%d", c, n, i);
                rr_log(key, source[n][0x44 / 4 + i]);
            }
        }
        ((int(__fastcall*)(void*))0x40ff40)(source[0]);
        snprintf(key, sizeof key, "nodes.%d.mask", c);
        rr_log(key, masks[c % 3]);
        snprintf(key, sizeof key, "nodes.%d.high", c);
        rr_log(key, high);
        char path[1024];
        const char* slash = strrchr(g_output, '\\');
        const size_t prefix = slash ? (size_t)(slash - g_output) + 1 : 0;
        memcpy(path, g_output, prefix);
        snprintf(path + prefix, sizeof path - prefix, "%s-nodes%d.raw", g_scenario, c);
        FILE* out = fopen(path, "wb");
        if (out) {
            fwrite(pixels, 1, width * height, out);
            fclose(out);
        }
    }
    memcpy((void*)0x484280, edges, 5760);
    memcpy((void*)0x482bf8, edges + 5760, 5760);
    memcpy((void*)0x4c5d30, uv_saved, 32);
    free(edges);
    free(texture);
    free(pixels);
restore:
    RD8(0x475620) = locked;
    for (unsigned i = 0; i < sizeof addresses / sizeof addresses[0]; ++i) RD32(addresses[i]) = saved[i];
}

static void capture_road_panels(void) {
    const uint32_t addresses[] = {0x49e660, 0x49e668, 0x4b7e74, 0x46667c, 0x4c3cc0, 0x4c3cb0,
                                  0x4c3cd0, 0x4c3cd4, 0x4c3cd8, 0x4c3ce0, 0x4c3ce4, 0x4c3ce8};
    int saved[12];
    for (int i = 0; i < 12; ++i) saved[i] = RD32(addresses[i]);
    int templates[8][17] = {{0}}, output[16][42], current[65], previous[65], terrain[96], old_terrain[96];
    for (int i = 0; i < 8; ++i) {
        templates[i][2] = i;
        templates[i][15] = 5;
        templates[i][16] = 5;
    }
    RD32(0x4b7e74) = (int)(uintptr_t)templates[0];
    RD32(0x4c3cc0) = (int)(uintptr_t)templates[0];
    RD32(0x4c3cb0) = (int)(uintptr_t)templates[1];
    for (int i = 0; i < 3; ++i) {
        RD32(0x4c3cd0 + i * 4) = (int)(uintptr_t)templates[2 + i];
        RD32(0x4c3ce0 + i * 4) = (int)(uintptr_t)templates[5 + i];
    }
    const uint32_t draw_addresses[] = {0x4b7eb0, 0x4b7eb4, 0x4b7eb8, 0x4b7ebc, 0x4b7ec0, 0x4b7ec4, 0x4c5d88,
                                       0x4c5d60, 0x4c5d64, 0x4c5d68, 0x4c5d6c, 0x484278, 0x4c5d20, 0x4c5d50,
                                       0x466680, 0x481fe0, 0x481fa8, 0x481ff4, 0x475648};
    int draw_saved[19];
    for (int i = 0; i < 19; ++i) draw_saved[i] = RD32(draw_addresses[i]);
    unsigned char* pixels = malloc(640 * 480);
    unsigned char* edges = malloc(11520);
    unsigned char uv_saved[32], dimension_table[4096];
    const unsigned char locked = RD8(0x475620);
    RD8(0x475620) = 1;
    RD32(0x466680) = 1;
    if (edges) {
        memcpy(edges, (void*)0x484280, 5760);
        memcpy(edges + 5760, (void*)0x482bf8, 5760);
    }
    memcpy(uv_saved, (void*)0x4c5d30, 32);
    memcpy(dimension_table, (void*)0x4b6e70, 4096);
    ((void(__cdecl*)(void))0x44fb10)();
    unsigned char* global = NULL;
    int* real_ccb[8] = {0};
    FILE* input = fopen("D:\\ROADRASH\\DATA\\GLOBAL.RSC", "rb");
    if (input) {
        fseek(input, 0, SEEK_END);
        long size = ftell(input);
        rewind(input);
        if (size > 0 && size < 10000000) {
            global = malloc(size);
            if (global && fread(global, 1, size, input) == (size_t)size) {
                const uint32_t table = cel_word(global, 16), entries = cel_word(global, table + 8);
                for (int slot = 0; slot < 8; ++slot) {
                    const uint32_t destination = slot == 0   ? 0x4c3cc0
                                                 : slot == 1 ? 0x4c3cb0
                                                 : slot < 5  ? 0x4c3cd0 + (slot - 2) * 4
                                                             : 0x4c3ce0 + (slot - 5) * 4;
                    uint32_t id = 0;
                    for (int* descriptor = (void*)0x46dfb0; descriptor[0]; descriptor += 5)
                        if ((uint32_t)descriptor[0] == destination) id = descriptor[2] + 1;
                    if (slot == 3) {
                        real_ccb[3] = real_ccb[2];
                        continue;
                    }
                    for (uint32_t row = 0; row < entries; ++row) {
                        const unsigned char* entry = global + table + 16 + row * 32;
                        if (cel_word(entry, 0) == 0x43454c20 && cel_word(entry, 4) == id)
                            real_ccb[slot] = ((int*(__fastcall*)(void*, void*))0x40b800)(
                                global + cel_word(entry, 8), NULL);
                    }
                }
            }
        }
        fclose(input);
    }
    const int marking_flags[8] = {0, 8, 16, 32, 64, 24, 80, 255};
    char key[128];
    for (int c = 0; c < 128; ++c) {
        memset(current, 0, sizeof current);
        memset(previous, 0, sizeof previous);
        memset(terrain, 0, sizeof terrain);
        memset(old_terrain, 0, sizeof old_terrain);
        memset(output, 0, sizeof output);
        unsigned char* t = (void*)terrain;
        unsigned char* old = (void*)old_terrain;
        current[0] = (int)(uintptr_t)previous;
        current[3] = (int)(uintptr_t)terrain;
        previous[3] = (int)(uintptr_t)old_terrain;
        current[0x40] = c % 17 != 0;
        current[0xc] = ((c * 71) % 900 - 400) * 65536;
        current[0x10] = ((c * 53) % 900 - 400) * 65536;
        current[10] = ((c * 17) % 260 - 20) * 65536 + c * 1223;
        current[14] = ((c * 23) % 260 - 20) * 65536 + c * 1777;
        previous[9] = ((c * 37) % 400 - 100) * 65536;
        current[9] = ((c * 43) % 400 - 100) * 65536;
        terrain[0x30 / 4] = (c % 6) * 256 + ((c * 113) % 256) - 128;
        terrain[0x2c / 4] = (c % 4) * 256 + ((c * 177) % 256) - 128;
        t[0x34] = c % 5;
        t[0x35] = (c / 5) % 5;
        t[0x36] = c % 3 == 0 ? 32 : 0;
        t[0x37] = marking_flags[c % 8];
        t[0x3c] = c % 5;
        old[0x3c] = (c / 5) % 5;
#define PANEL_LOG(label, value)                                                                              \
    do {                                                                                                     \
        snprintf(key, sizeof key, "panels.%d.%s", c, label);                                                 \
        rr_log(key, value);                                                                                  \
    } while (0)
        PANEL_LOG("first_left", current[0xc]);
        PANEL_LOG("second_left", current[0x10]);
        PANEL_LOG("first_y", current[10]);
        PANEL_LOG("second_y", current[14]);
        PANEL_LOG("first_step", previous[9]);
        PANEL_LOG("second_step", current[9]);
        PANEL_LOG("left_q8", terrain[0x30 / 4]);
        PANEL_LOG("right_q8", terrain[0x2c / 4]);
        PANEL_LOG("left_count", t[0x34]);
        PANEL_LOG("right_count", t[0x35]);
        PANEL_LOG("surface", t[0x36]);
        PANEL_LOG("marking", t[0x37]);
        PANEL_LOG("type", t[0x3c]);
        PANEL_LOG("next_type", old[0x3c]);
        PANEL_LOG("visible", current[0x40]);
        RD32(0x46667c) = c % 2 ? 640 : 320;
        PANEL_LOG("width", RD32(0x46667c));
        RD32(0x49e660) = (int)(uintptr_t)output;
        RD32(0x49e668) = (int)(uintptr_t)(output + 15);
        ((void(__fastcall*)(int*))0x43c170)(current);
        const int count = ((uintptr_t)RD32(0x49e660) - (uintptr_t)output) / 168;
        PANEL_LOG("count", count);
        for (int n = 0; n < count && n < 16; ++n) {
            snprintf(key, sizeof key, "panels.%d.%d.material", c, n);
            rr_log(key, output[n][2]);
            snprintf(key, sizeof key, "panels.%d.%d.pre0", c, n);
            rr_log(key, (uint32_t)output[n][12]);
            const unsigned char* bytes = (void*)output[n];
            snprintf(key, sizeof key, "panels.%d.%d.flip_u", c, n);
            rr_log(key, bytes[100]);
            snprintf(key, sizeof key, "panels.%d.%d.flip_v", c, n);
            rr_log(key, bytes[101]);
            for (int i = 0; i < 8; ++i) {
                snprintf(key, sizeof key, "panels.%d.%d.point.%d", c, n, i);
                rr_log(key, output[n][17 + i]);
            }
        }
        const int high = c % 2, width = high ? 640 : 320, height = high ? 480 : 240,
                  mask = c % 3 == 0   ? 16
                         : c % 3 == 1 ? 48
                                      : 112;
        PANEL_LOG("draw_high", high);
        PANEL_LOG("draw_mask", mask);
        if (pixels && edges) {
            for (int n = 0; n < count && n < 16; ++n) {
                const int slot = output[n][2];
                output[n][2] = real_ccb[slot] ? real_ccb[slot][2] : 0;
                output[n][1] = n + 1 < count ? (int)(uintptr_t)output[n + 1] : 0;
                output[n][3] = n + 1 == count ? 0x40000000 : 0;
            }
            RD32(0x481fa8) = high ? 15 : 16;
            RD32(0x481ff4) = high ? (int)0xfffffffeu : -1;
            RD32(0x475648) = mask;
            RD32(0x4b7eb0) = 0;
            RD32(0x4b7eb4) = 0;
            RD32(0x4b7eb8) = width;
            RD32(0x4b7ebc) = height - 1;
            RD32(0x4b7ec0) = (int)(uintptr_t)pixels;
            RD32(0x4b7ec4) = width;
            memset(pixels, 125, width * height);
            if (count) ((void(__fastcall*)(void*))0x40ff40)(output);
            char path[1024];
            const char* slash = strrchr(g_output, '\\');
            const size_t prefix = slash ? (size_t)(slash - g_output) + 1 : 0;
            memcpy(path, g_output, prefix);
            snprintf(path + prefix, sizeof path - prefix, "%s-panels%d.raw", g_scenario, c);
            FILE* out = fopen(path, "wb");
            if (out) {
                fwrite(pixels, 1, width * height, out);
                fclose(out);
            }
        }
#undef PANEL_LOG
    }
    if (edges) {
        memcpy((void*)0x484280, edges, 5760);
        memcpy((void*)0x482bf8, edges + 5760, 5760);
    }
    memcpy((void*)0x4c5d30, uv_saved, 32);
    memcpy((void*)0x4b6e70, dimension_table, 4096);
    free(global);
    free(edges);
    free(pixels);
    RD8(0x475620) = locked;
    for (int i = 0; i < 19; ++i) RD32(draw_addresses[i]) = draw_saved[i];
    for (int i = 0; i < 12; ++i) RD32(addresses[i]) = saved[i];
}

/* Palette upload log (RR_ORACLE_PALETTE=1). Every original palette upload is
 * `IDirectDrawPalette::SetEntries` (vtable +0x18) on DAT_00475424 with flags 0,
 * start 0, count 0x100: half 0x4b8030 (start no-fade 0x416414, pump 0x443217),
 * full 0x4b8490 (countdown expiry 0x442f16, pump restore 0x443337), fade
 * buffer 0x482358 (fade-in 0x415e53, fade-out 0x415f33), 0x482760 (0x41664e)
 * and a stack copy (0x41e767). The slot is patched once the object exists
 * (fades call from the timeSetEvent thread). Records go to the companion
 * `<scenario>-palette.raw`: "RRPL" + version 1, then per upload 9 uint32
 * {seq, frame, ms, return address, source, start, count, countdown 0x4753c0,
 * fade counter 0x482350} and count PALETTEENTRY bytes (r, g, b, flags). */
typedef HRESULT(__stdcall* rr_set_entries_fn)(void*, DWORD, DWORD, DWORD, void*);
static int g_palette;
static int g_palette_exit_fadeout;
static FILE* g_palette_out;
static CRITICAL_SECTION g_palette_lock;
static rr_set_entries_fn g_set_entries;
static volatile LONG g_palette_uploads;
static HRESULT __stdcall hook_set_entries(void* self, DWORD flags, DWORD start, DWORD count, void* entries) {
    const uint32_t site = (uint32_t)(uintptr_t)__builtin_return_address(0);
    const HRESULT result = g_set_entries(self, flags, start, count, entries);
    if (self != (void*)(uintptr_t)(uint32_t)RD32(0x00475424)) return result;
    const uint32_t fade_counter = (uint32_t)RD32(0x00482350);
    EnterCriticalSection(&g_palette_lock);
    const uint32_t seq = (uint32_t)g_palette_uploads;
    if (g_palette_out) {
        const uint32_t n = count <= 256 && readable((uintptr_t)entries, count * 4) ? count : 0;
        const uint32_t record[9] = {seq,
                                    g_frame,
                                    timeGetTime(),
                                    site,
                                    (uint32_t)(uintptr_t)entries,
                                    start,
                                    n,
                                    (uint32_t)RD32(0x004753c0),
                                    fade_counter};
        fwrite(record, 4, 9, g_palette_out);
        if (n) fwrite(entries, 4, n, g_palette_out);
        fflush(g_palette_out);
    }
    InterlockedIncrement(&g_palette_uploads);
    // Fade-out (0x415e90, call at 0x415f49): the counter is decremented before
    // the upload, so counter 1 is the last upload. The main thread is blocked
    // on the fade semaphore; end the capture here instead of entering menus.
    if (g_palette_exit_fadeout && site == 0x00415f4c && fade_counter == 1) {
        if (g_palette_out) {
            fclose(g_palette_out);
            g_palette_out = NULL;
        }
        if (g_out) {
            fputs("end\n", g_out);
            fclose(g_out);
            g_out = NULL;
        }
        LeaveCriticalSection(&g_palette_lock);
        ExitProcess(0);
    }
    LeaveCriticalSection(&g_palette_lock);
    return result;
}
static void palette_hook_install(void) {
    if (!g_palette || g_set_entries) return;
    const uintptr_t object = (uint32_t)RD32(0x00475424);
    if (!object || !readable(object, 4)) return;
    void** vtable = *(void***)object;
    if (!readable((uintptr_t)vtable, 0x1c)) return;
    DWORD old;
    if (!VirtualProtect(&vtable[6], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return;
    g_set_entries = (rr_set_entries_fn)vtable[6];
    vtable[6] = (void*)hook_set_entries;
    VirtualProtect(&vtable[6], sizeof(void*), old, &old);
    if (g_out) fprintf(g_out, "# palette SetEntries hooked at frame %u\n", g_frame);
}
static void palette_open(void) {
    char path[MAX_PATH];
    const char* slash = strrchr(g_output, '\\');
    const size_t prefix = slash ? (size_t)(slash - g_output) + 1 : 0;
    if (prefix > sizeof path - 64) return;
    memcpy(path, g_output, prefix);
    snprintf(path + prefix, sizeof path - prefix, "%s-palette.raw", g_scenario);
    InitializeCriticalSection(&g_palette_lock);
    g_palette_out = fopen(path, "wb");
    if (g_palette_out) {
        const uint32_t header[2] = {0x4c505252u /* "RRPL" */, 1};
        fwrite(header, 4, 2, g_palette_out);
    }
}

/* Scripted keys (RR_ORACLE_KEYS="frame:vk,frame:vk,..."): after race frame
 * `frame` renders, post WM_KEYDOWN then WM_KEYUP for `vk` to the game's
 * window. The wndproc acts on WM_KEYUP (0x423345: Escape, VK_PAUSE, ...); the
 * pump 0x004431d0 renders frames through hook 3, so its keys use the same
 * frame clock. An optional third field "frame:vk:mode" writes game mode
 * 0x004753b0 before posting (diagnostic only: the course bootstrap races in
 * attract mode 0, where VK_PAUSE is ignored and Escape ends the race without
 * the 0x004431d0 quit prompt). The mode stays until a later key changes it. */
static struct {
    uint32_t frame;
    uint32_t vk;
    int mode;
} g_keys[16];
static int g_key_count, g_key_next;
static BOOL CALLBACK find_game_window(HWND window, LPARAM out) {
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return TRUE;
    *(HWND*)out = window;
    return FALSE;
}
static void post_scripted_keys(void) {
    while (g_key_next < g_key_count && g_keys[g_key_next].frame == g_frame) {
        HWND window = NULL;
        EnumThreadWindows(GetCurrentThreadId(), find_game_window, (LPARAM)&window);
        const UINT vk = g_keys[g_key_next].vk, scan = MapVirtualKeyA(vk, 0);
        if (g_keys[g_key_next].mode >= 0) RD32(0x004753b0) = g_keys[g_key_next].mode;
        if (window) {
            PostMessageA(window, WM_KEYDOWN, vk, 1 | (scan << 16));
            PostMessageA(window, WM_KEYUP, vk, 1 | (scan << 16) | 0xc0000000u);
        }
        if (g_out)
            fprintf(g_out, "# key %u mode %d posted at frame %u window %p\n", vk, g_keys[g_key_next].mode,
                    g_frame, (void*)window);
        ++g_key_next;
    }
}

static void dump_frame(uint32_t site) {
    char key[96];
    fprintf(g_out, "frame\t%u\n", g_frame);
    rr_log("site", site);
    rr_log("course", RD8(0x00467a6d));
    rr_log("view.high", RD8(0x00475380));
    rr_log("camera.position", RD32(0x004bc7e4));
    rr_log("camera.x", RD32(0x004bc7d8));
    rr_log("camera.height", RD32(0x004bc7dc));
    rr_log("camera.half", RD8(0x00473498));
    rr_log("view.coordinate_shift", RD32(0x00481fa8));
    rr_log("view.coordinate_mask", (uint32_t)RD32(0x00481ff4));
    if (g_frame == 0) {
        dump_raster();
        dump_groups();
    }
    rr_log("view.quality_mask", RD32(0x00475648));
    if (g_palette) {
        rr_log("palette.uploads", g_palette_uploads);
        rr_log("countdown", RD32(0x004753c0));
        rr_log("race.mode", RD32(0x004753b0));
        rr_log("pump.active", RD8(0x004753b8));
    }
    if (g_scene_written) {
        rr_log("scene.file", g_frame);
        rr_log("scene.nodes", g_scene_nodes);
        g_scene_written = 0;
    }
    // Dispatch records buffered during this frame's projection/draw.
    if (!(g_frame % (uint32_t)g_pixel_every))
        flush_actor_records();
    else {
        g_nsel = 0;
        g_nbb = 0;
    }
    if (g_pixel_written) {
        rr_log("pixels.file", g_frame);
        rr_log("pixels.width", g_pixel_width);
        rr_log("pixels.height", g_pixel_height);
        rr_log("pixels.pitch", g_pixel_pitch);
        for (int i = 0; i < 256; ++i) {
            const uintptr_t p = 0x0049e138 + i * 4;
            snprintf(key, sizeof key, "palette.%d", i);
            rr_log(key, ((uint32_t)RD8(p) << 16) | ((uint32_t)RD8(p + 1) << 8) | RD8(p + 2));
        }
    }
    for (int s = 0; s < 33; ++s) {
        uintptr_t entry = 0x004bc800 + (uintptr_t)s * 548;
        int kind = RD32(entry + 4);
        snprintf(key, sizeof key, "slice.%d.kind", s);
        rr_log(key, kind);
        snprintf(key, sizeof key, "slice.%d.sample", s);
        rr_log(key, RD32(entry + 8));
        for (int r = 0; r < kind && r < 2; ++r) {
            uintptr_t p = (uint32_t)RD32(entry + (5 + r) * 4);
            if (!readable(p, 0x104)) continue;
            static const struct {
                const char* name;
                int off, byte;
            } fields[] = {{"depth", 0x18, 0},
                          {"scale", 0x20, 0},
                          {"x", 0x10, 0},
                          {"y", 0x14, 0},
                          {"centre", 0x48, 0},
                          {"screen_y", 0x4c, 0},
                          {"left", 0x50, 0},
                          {"right", 0x58, 0},
                          {"lane_left", 0x40, 0},
                          {"lane_right", 0x44, 0},
                          {"shoulder_left", 0x60, 0},
                          {"shoulder_right", 0x68, 0},
                          {"height_left_row", 0x54, 0},
                          {"height_right_row", 0x5c, 0},
                          {"visible", 0x100, 1}};
            for (unsigned f = 0; f < sizeof fields / sizeof fields[0]; ++f) {
                snprintf(key, sizeof key, "slice.%d.road.%d.%s", s, r, fields[f].name);
                rr_log(key, fields[f].byte ? RD8(p + fields[f].off) : RD32(p + fields[f].off));
            }
            uintptr_t sub = (uint32_t)RD32(p + 0x0c);
            snprintf(key, sizeof key, "slice.%d.road.%d.type", s, r);
            rr_log(key, readable(sub, 0x3d) ? RD8(sub + 0x3c) : -1);
            if (readable(sub, 0x164)) {
                for (int offset = 0xf0; offset <= 0x160; offset += 4) {
                    snprintf(key, sizeof key, "slice.%d.road.%d.sub.%d", s, r, offset);
                    rr_log(key, RD32(sub + offset));
                }
            }
        }
    }
    fflush(g_out);
}

#define FRAME_HOOK(n, site)                                                                                  \
    static void __cdecl hook_frame_##n(void) {                                                               \
        if (g_terrain_seeded) capture_terrain_seeded_and_exit();                                             \
        reset_actor_records();                                                                               \
        palette_hook_install();                                                                              \
        orig_frame();                                                                                        \
        if (g_out && !g_sim) dump_frame(site);                                                               \
        post_scripted_keys();                                                                                \
        ++g_frame;                                                                                           \
        if (!g_sim && g_quit_after > 0 && g_frame >= (uint32_t)g_quit_after) {                               \
            if (g_out) {                                                                                     \
                fputs("end\n", g_out);                                                                       \
                fclose(g_out);                                                                               \
                g_out = NULL;                                                                                \
            }                                                                                                \
            ExitProcess(0);                                                                                  \
        }                                                                                                    \
    }
FRAME_HOOK(0, 0x004163fa)
FRAME_HOOK(1, 0x00416406)
FRAME_HOOK(2, 0x0041656d)
FRAME_HOOK(3, 0x00443249)

/* Original save-file writer capture (RR_ORACLE_SAVE=1, plan 13 Task 6 Step 1).
   Isolated call at attach time: seeds the 32-byte game state (0x004b8a10)
   and the 10 personality triples for rider index 2 (0x004c6b40,
   0x00467a6c), points the dialog path (0x004b8900) at C:\save-seed.RRS and
   calls the original writer 0x0042f000, then hex-dumps its 0xa0-byte buffer
   (0x00497090). The reader 0x0042f110 is not called here: it runs the UI
   string setter 0x00444fb0, which is unsafe before a window exists. */
static void capture_save(void) {
    static const uint8_t seed_state[32] = {0x00, 0x03, 0x02, 0x05, 0x07, 0x04, 0x01, 0x01, 0x78, 0x56, 0x34,
                                           0x12, 'S',  'A',  'V',  'E',  'T',  'E',  'S',  'T',  0,    0,
                                           0,    0,    0,    0,    0,    0,    0,    0,    0,    0};
    for (int i = 0; i < 32; ++i) RD8(0x004b8a10 + i) = seed_state[i];
    RD8(0x00467a6c) = 2;
    for (int p = 0; p < 10; ++p)
        for (int k = 0; k < 3; ++k) {
            int32_t v;
            if (p == 9 && k == 2)
                v = 0x7fffffff;
            else {
                v = (int32_t)(0x01010101u * (uint32_t)p + 0x101u * (uint32_t)k + 7u);
                if (((p + k) & 1)) v = -v;
            }
            RD32(0x004c6b40 + p * 0x204 + 2 * 12 + k * 4) = v;
        }
    char oldpath[260];
    for (int i = 0; i < 260; ++i) oldpath[i] = (char)RD8(0x004b8900 + i);
    const char* sp = "C:\\save-seed.RRS";
    int i = 0;
    while (sp[i]) {
        RD8(0x004b8900 + i) = (uint8_t)sp[i];
        ++i;
    }
    RD8(0x004b8900 + i) = 0;
    ((void(__cdecl*)(void))0x0042f000)();
    for (int j = 0; j < 260; ++j) RD8(0x004b8900 + j) = (uint8_t)oldpath[j];
    if (g_out) {
        fputs("save.begin\n", g_out);
        for (int j = 0; j < 0xa0; ++j) fprintf(g_out, "%02x", RD8(0x00497090 + j));
        fputs("\nsave.end\n", g_out);
        fflush(g_out);
    }
}

/* Watchdog thread (RR_ORACLE_MENU): waits for 0x0046a660 to hold the target
   screen, then screenshots the game window client area to C:\menu-<id>.bmp.
   Read-only w.r.t. game state; the driver kills the game after the BMP lands.
   A separate thread is used because menus are message-driven with no hooked
   per-frame call; GDI reads from another thread are legal. */
static BOOL CALLBACK find_game_process_window(HWND window, LPARAM out) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(window) && !GetWindow(window, GW_OWNER)) {
        *(HWND*)out = window;
        return FALSE;
    }
    return TRUE;
}

static DWORD WINAPI menu_watchdog(LPVOID arg) {
    const int target = (int)(intptr_t)arg;
    int stable = 0;
    for (int i = 0; i < 600 && stable < 6; ++i) {
        Sleep(500);
        if (!g_out) return 0;
        const int cur = RD32(0x0046a660);
        stable = (cur == target) ? stable + 1 : 0;
        if (i % 20 == 19) {
            fprintf(g_out, "# menu: poll %d screen %d stable %d\n", i, cur, stable);
            fflush(g_out);
        }
    }
    if (stable < 6 || !g_out) {
        if (g_out) {
            fputs("# menu: target never settled\n", g_out);
            fflush(g_out);
        }
        return 0;
    }
    { // Menu colour table (0x0041e6b0/0x0041e6e0) is built at init by
        // 0x0041e090; dump the live table now (attach-time reads zeros).
        FILE* tab = fopen("C:\\menu-table.bin", "wb");
        if (tab) {
            for (uint32_t i = 0; i < 0x10000; ++i) {
                const uint32_t v = (uint32_t)RD32(0x00485df8 + i * 2) & 0xffff;
                fputc(v & 0xff, tab);
                fputc((v >> 8) & 0xff, tab);
            }
            fclose(tab);
            if (g_out) {
                fputs("menu.table\t1\n", g_out);
                fflush(g_out);
            }
        }
    }
    HWND window = NULL;
    EnumWindows(find_game_process_window, (LPARAM)&window);
    if (!window) {
        fputs("# menu: no game window\n", g_out);
        fflush(g_out);
        return 0;
    }
    RECT rc;
    GetClientRect(window, &rc);
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int ok = 0;
    if (w > 0 && w <= 4096 && h > 0 && h <= 4096) {
        HDC wdc = GetDC(window), mem = CreateCompatibleDC(wdc);
        BITMAPINFO bi;
        memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 24;
        void* bits = NULL;
        HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (bmp && bits) {
            HGDIOBJ old = SelectObject(mem, bmp);
            ok = BitBlt(mem, 0, 0, w, h, wdc, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            if (ok) {
                char path[64];
                snprintf(path, sizeof path, "C:\\menu-%02d.bmp", target);
                FILE* f = fopen(path, "wb");
                if (f) {
                    const int stride = (w * 3 + 3) & ~3;
                    uint8_t hdr[54];
                    memset(hdr, 0, sizeof hdr);
                    hdr[0] = 'B';
                    hdr[1] = 'M';
                    uint32_t size = 54 + (uint32_t)(stride * h), off = 54, hs = 40;
                    memcpy(hdr + 2, &size, 4);
                    memcpy(hdr + 10, &off, 4);
                    memcpy(hdr + 14, &hs, 4);
                    memcpy(hdr + 18, &w, 4);
                    memcpy(hdr + 22, &h, 4);
                    hdr[26] = 1;
                    hdr[28] = 24;
                    fwrite(hdr, 1, 54, f);
                    for (int y = h - 1; y >= 0; --y)
                        fwrite((uint8_t*)bits + (size_t)y * stride, 1, stride, f);
                    fclose(f);
                } else
                    ok = 0;
            }
            DeleteObject(bmp);
        }
        DeleteDC(mem);
        ReleaseDC(window, wdc);
    }
    if (g_out) {
        fprintf(g_out, "menu.screen\t%d\nmenu.size\t%d %d\nmenu.captured\t%d\n", target, w, h, ok);
        fflush(g_out);
    }
    return 0;
}

#include "frontend_probe.h"

static int rr_install_hooks(void) {
    char buf[32], path[MAX_PATH];
    if (GetEnvironmentVariableA("RR_ORACLE_FRONTEND", buf, sizeof buf)) g_frontend_probe = atoi(buf) != 0;
    if (GetEnvironmentVariableA("RR_ORACLE_FRAMES", buf, sizeof buf)) g_quit_after = atoi(buf);
    if (GetEnvironmentVariableA("RR_ORACLE_PIXELS", buf, sizeof buf)) g_pixels = atoi(buf) != 0;
    if (GetEnvironmentVariableA("RR_ORACLE_GROUPS", buf, sizeof buf)) g_groups = atoi(buf) != 0;
    if (GetEnvironmentVariableA("RR_ORACLE_DRIVE", buf, sizeof buf)) {
        g_drive = atoi(buf);
        if (g_drive < 0 || g_drive > 3) return 0;
    }
    if (GetEnvironmentVariableA("RR_ORACLE_SIM", buf, sizeof buf)) g_sim = atoi(buf) != 0;
    if (GetEnvironmentVariableA("RR_ORACLE_SEED", buf, sizeof buf)) g_seed = atoi(buf);
    if (GetEnvironmentVariableA("RR_ORACLE_RECORD", path, sizeof path) && path[0])
        g_record = fopen(path, "w");
    if (GetEnvironmentVariableA("RR_ORACLE_INPUT", path, sizeof path) && path[0]) g_input = fopen(path, "r");
    if (GetEnvironmentVariableA("RR_ORACLE_RANDCOUNT", buf, sizeof buf) && atoi(buf)) sim_patch_rand_entry();
    if (GetEnvironmentVariableA("RR_ORACLE_RANDTRACE", buf, sizeof buf)) g_randtrace_tick = atoi(buf);
    if (GetEnvironmentVariableA("RR_ORACLE_RANDLOG", buf, sizeof buf)) g_randlog_all = atoi(buf) != 0;
    if (GetEnvironmentVariableA("RR_ORACLE_POLICE", buf, sizeof buf)) g_police_probe = atoi(buf) != 0;
    if (GetEnvironmentVariableA("RR_ORACLE_TERRAIN_SEEDED", buf, sizeof buf))
        g_terrain_seeded = atoi(buf) != 0;
    if (GetEnvironmentVariableA("RR_ORACLE_TERRAIN_TRACE", buf, sizeof buf)) g_terrain_trace = atoi(buf) != 0;
    if (GetEnvironmentVariableA("RR_ORACLE_SOLO", buf, sizeof buf)) g_solo = atoi(buf) != 0;
    if (GetEnvironmentVariableA("RR_ORACLE_HUMAN", buf, sizeof buf)) g_human = atoi(buf) != 0;
    if (GetEnvironmentVariableA("RR_ORACLE_KNOCK", buf, sizeof buf)) g_knock_tick = atoi(buf);
    if (GetEnvironmentVariableA("RR_ORACLE_ORDER", buf, sizeof buf)) g_order = atoi(buf) != 0;
    if (GetEnvironmentVariableA("RR_ORACLE_PALETTE", buf, sizeof buf)) g_palette = atoi(buf) != 0;
    if (GetEnvironmentVariableA("RR_ORACLE_EXIT_FADEOUT", buf, sizeof buf))
        g_palette_exit_fadeout = atoi(buf) != 0;
    if (GetEnvironmentVariableA("RR_ORACLE_KEYS", path, sizeof path)) {
        // "frame:vk,..." in ascending frame order; malformed lists fail the install.
        for (char* item = strtok(path, ","); item; item = strtok(NULL, ",")) {
            unsigned frame, vk;
            int mode = -1;
            const int fields = sscanf(item, "%u:%u:%d", &frame, &vk, &mode);
            if (g_key_count == 16 || fields < 2 || vk == 0 || vk > 0xfe ||
                (fields == 3 && (mode < 0 || mode > 3)) ||
                (g_key_count && frame < g_keys[g_key_count - 1].frame)) {
                if (g_out) fputs("# invalid RR_ORACLE_KEYS\n", g_out);
                return 0;
            }
            g_keys[g_key_count].frame = frame;
            g_keys[g_key_count].vk = vk;
            g_keys[g_key_count].mode = mode;
            ++g_key_count;
        }
    }
    if (g_palette) palette_open();
    env("RR_ORACLE_RASTER", buf, sizeof buf, "0");
    g_raster = atoi(buf);
    if (GetEnvironmentVariableA("RR_ORACLE_SAVE", buf, sizeof buf)) g_save = atoi(buf) != 0;
    if (GetEnvironmentVariableA("RR_ORACLE_MENU", buf, sizeof buf)) g_menu_screen = atoi(buf);
    if (GetEnvironmentVariableA("RR_ORACLE_SHOT_EVERY", buf, sizeof buf) && atoi(buf) > 0)
        g_pixel_every = atoi(buf);
    if (GetEnvironmentVariableA("RR_ORACLE_LEVEL", buf, sizeof buf)) {
        g_capture_level = atoi(buf);
        if (g_capture_level < 1 || g_capture_level > 5) {
            if (g_out) fputs("# invalid RR_ORACLE_LEVEL (expected 1..5)\n", g_out);
            return 0;
        }
    }
    if (GetEnvironmentVariableA("RR_ORACLE_COURSE", buf, sizeof buf)) {
        g_capture_course = atoi(buf);
        if (g_capture_course < 1 || g_capture_course > 5) {
            if (g_out) fputs("# invalid RR_ORACLE_COURSE (expected 1..5)\n", g_out);
            return 0;
        }
    }
    static const struct {
        uint32_t site, target;
        void* hook;
        uint8_t bytes[5];
    } calls[] = {{0x004163fa, 0x004439f0, hook_frame_0, {0xe8, 0xf1, 0xd5, 0x02, 0}},
                 {0x00416406, 0x004439f0, hook_frame_1, {0xe8, 0xe5, 0xd5, 0x02, 0}},
                 {0x0041656d, 0x004439f0, hook_frame_2, {0xe8, 0x7e, 0xd4, 0x02, 0}},
                 {0x00443249, 0x004439f0, hook_frame_3, {0xe8, 0xa2, 0x07, 0, 0}},
                 {0x004439f5, 0x00450420, hook_draw, {0xe8, 0x26, 0xca, 0, 0}},
                 {0x0044c7b9, 0x00446c30, hook_dash, {0xe8, 0x72, 0xa4, 0xff, 0xff}},
                 {0x00443a5c, 0x00443d70, hook_project, {0xe8, 0x0f, 0x03, 0, 0}},
                 {0x00442ea8, 0x0044f800, hook_controls, {0xe8, 0x53, 0xc9, 0, 0}},
                 {0x0044b324, 0x0044b3b0, hook_step, {0xe8, 0x87, 0, 0, 0}},
                 {0x00448381, 0x00449150, hook_menu_video, {0xe8, 0xca, 0x0d, 0, 0}},
                 {0x00448392, 0x00421df0, hook_menu_enter, {0xe8, 0x59, 0x9a, 0xfd, 0xff}},
                 // 0x00446250 dispatch: view selection (0x0043af60) and billboard
                 // (0x0044ba40) call sites, verified against the reference EXE bytes.
                 {0x00446293, 0x0043af60, hook_select, {0xe8, 0xc8, 0x4c, 0xff, 0xff}},
                 {0x004462e4, 0x0043af60, hook_select, {0xe8, 0x77, 0x4c, 0xff, 0xff}},
                 {0x004462d5, 0x0044ba40, hook_billboard, {0xe8, 0x66, 0x57, 0x00, 0x00}},
                 {0x00446338, 0x0044ba40, hook_billboard, {0xe8, 0x03, 0x57, 0x00, 0x00}},
                 // 0x004463e0 second bike path (selects draw the emitted nodes).
                 {0x00446420, 0x0043af60, hook_select, {0xe8, 0x3b, 0x4b, 0xff, 0xff}},
                 {0x00446468, 0x0043af60, hook_select, {0xe8, 0xf3, 0x4a, 0xff, 0xff}},
                 {0x00446459, 0x0044ba40, hook_billboard, {0xe8, 0xe2, 0x55, 0x00, 0x00}},
                 {0x004464f3, 0x0044ba40, hook_billboard, {0xe8, 0x48, 0x55, 0x00, 0x00}}};
    const unsigned count = sizeof calls / sizeof calls[0];
    // Validate ALL sites before changing ANY. A foreign binary is never patched.
    for (unsigned i = 0; i < count; ++i) {
        if (memcmp((void*)(uintptr_t)calls[i].site, calls[i].bytes, 5)) {
            if (g_out) fprintf(g_out, "# patch verification failed at %08x\n", calls[i].site);
            return 0;
        }
    }
    for (unsigned i = 0; i < count; ++i) {
        if (!rr_patch_call((uint8_t*)(uintptr_t)calls[i].site, calls[i].target, calls[i].hook)) {
            // Restore any earlier call sites on a protection failure.
            for (unsigned j = 0; j < i; ++j) {
                DWORD old;
                if (VirtualProtect((void*)(uintptr_t)calls[j].site, 5, PAGE_EXECUTE_READWRITE, &old)) {
                    memcpy((void*)(uintptr_t)calls[j].site, calls[j].bytes, 5);
                    VirtualProtect((void*)(uintptr_t)calls[j].site, 5, old, &old);
                    FlushInstructionCache(GetCurrentProcess(), (void*)(uintptr_t)calls[j].site, 5);
                }
            }
            if (g_out) fprintf(g_out, "# patch installation failed at %08x\n", calls[i].site);
            return 0;
        }
    }
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)inst;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        char path[MAX_PATH];
        env("RR_ORACLE_OUT", path, sizeof path, "C:\\oracle.tsv");
        memcpy(g_output, path, sizeof path);
        env("RR_ORACLE_SCENARIO", g_scenario, sizeof g_scenario, "unnamed");
        g_out = fopen(path, "wb");
        if (g_out) {
            setvbuf(g_out, NULL, _IOFBF, 1 << 20);
            fprintf(
                g_out,
                "# rr-oracle "
                "1\nexe\t73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2\nscenario\t%s\n",
                g_scenario);
        }
        HMODULE real = LoadLibraryA("AWEMAN_REAL.DLL");
        g_real2 = real ? (void*)GetProcAddress(real, MAKEINTRESOURCEA(2)) : NULL;
        if (!g_real2) {
            if (g_out) {
                fputs("# AWEMAN_REAL.DLL ordinal 2 not found\n", g_out);
                fclose(g_out);
                g_out = NULL;
            }
            return FALSE;
        }
        if (!rr_install_hooks()) {
            if (g_out) {
                fclose(g_out);
                g_out = NULL;
            }
            return FALSE;
        }
        if (g_raster == 2 && g_out) {
            const int width = 640, height = 480;
            void* pixels = malloc((size_t)width * height);
            if (pixels) {
                int base = RD32(0x004b7ec0), pitch = RD32(0x004b7ec4);
                RD32(0x004b7ec0) = (int)(uintptr_t)pixels;
                RD32(0x004b7ec4) = width;
                capture_raster((uintptr_t)pixels, width, height, width);
                RD32(0x004b7ec0) = base;
                RD32(0x004b7ec4) = pitch;
                free(pixels);
                fputs("frame\t0\n", g_out);
                dump_raster();
                capture_view();
                capture_terrain();
                capture_hill_projection();
                capture_polygon();
                capture_texture_spans();
                capture_texture_quads();
                capture_cels();
                capture_node_render();
                capture_road_panels();
                fputs("end\n", g_out);
                fclose(g_out);
                g_out = NULL;
            }
        }
        if (g_save && g_out) {
            capture_save();
            fputs("end\n", g_out);
            fclose(g_out);
            g_out = NULL;
        }
        if (g_frontend_probe) {
            HANDLE thread = CreateThread(NULL, 0, frontend_probe, NULL, 0, NULL);
            if (thread) CloseHandle(thread);
        }
        if (g_menu_screen >= 0) {
            HANDLE thread = CreateThread(NULL, 0, menu_watchdog, (LPVOID)(intptr_t)g_menu_screen, 0, NULL);
            if (thread) CloseHandle(thread);
        }
        if (g_out) fflush(g_out);
    } else if (reason == DLL_PROCESS_DETACH && g_out) {
        fputs("end\n", g_out);
        fclose(g_out);
        g_out = NULL;
    }
    return TRUE;
}
#endif
