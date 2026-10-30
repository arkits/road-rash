/* Synthetic execution of the production actor wrappers: no disc or Wine needed. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef __fastcall
#define __fastcall
#endif
static FILE* g_out;
static uint32_t list_base = 0x1000, list_end = 0x1000;
static unsigned select_calls, billboard_calls;
static uint32_t expected_bike;
static int expected_ctx;
static int* expected_p1;
static unsigned* expected_p2;
static unsigned char memory[65536];
static uintptr_t region_start, region_end;
static uint32_t read_guest(uintptr_t p) {
    uint32_t value;
    assert(p >= 0x20000000u && p <= 0x20010000u - 4);
    memcpy(&value, memory + (p - 0x20000000u), 4);
    return value;
}
#define RR_ACTOR_READ_U32(p) read_guest(p)
#define RR_ACTOR_READ_I32(p) ((int32_t)read_guest(p))
static int readable(uintptr_t p, size_t n) {
    return (p >= region_start && p <= region_end && n <= region_end - p) ||
           (p >= 0x20000000u && p <= 0x20010000u && n <= 0x20010000u - p);
}
static uint32_t test_rd32(uintptr_t p) {
    assert(p == 0x0049e66c || p == 0x0049e660);
    return p == 0x0049e66c ? list_base : list_end;
}
#define RD32(a) test_rd32(a)
static void mock_select(uint32_t bike, int ctx) {
    assert(bike == expected_bike && ctx == expected_ctx);
    ++select_calls;
    /* Post-call actor fields must be sampled after the real selector. */
    *(uint32_t*)(memory + 0xa8) = 0x12345678;
}
static void mock_billboard(int* p1, unsigned* p2, int size, int lateral, int ground, unsigned pos,
                           signed char clip) {
    assert(p1 == expected_p1 && p2 == expected_p2);
    assert(size == 17 && lateral == -23 && ground == -41);
    assert(pos == 0xfedcba98u && clip == -7);
    ++billboard_calls;
    list_end += 0xa8;
    p2[0] += 1; /* The record must retain the input before forwarding. */
}
#define RR_ACTOR_SELECT_ORIGINAL mock_select
#define RR_ACTOR_BILLBOARD_ORIGINAL mock_billboard
#include "actor_record_hooks.h"

int main(void) {
    /* Guest addresses map into synthetic host storage through the production
       header's direct-read seam; no low-address mmap or Wine is required. */
    region_start = (uintptr_t)memory;
    region_end = region_start + sizeof memory;
    unsigned char* bike = memory;
    uint32_t* desc = (uint32_t*)(bike + 0x400);
    unsigned* emu = (unsigned*)(bike + 0x500);
    int* context = (int*)(bike + 0x600);
    uint32_t* owner = (uint32_t*)(bike + 0x700);
    expected_bike = 0x20000000u;
    expected_ctx = -19;
    expected_p1 = context;
    expected_p2 = emu;
    *(uint32_t*)(bike + 0x98) = 0x20000400u;
    for (unsigned i = 0; i < 5; ++i) desc[i] = 101 + i;
    *(int32_t*)(bike + 0x1c) = -101;
    *(int32_t*)(bike + 0x28) = -102;
    *(int32_t*)(bike + 0x44) = -103;
    *(int32_t*)(bike + 0x50) = -104;
    context[2] = 0x20000700u;
    context[4] = -201;
    context[5] = 202;
    context[6] = -203;
    owner[4] = 0xabcdef01;
    for (unsigned i = 0; i < 6; ++i) emu[i] = 300 + i;
    g_out = stdout;
    reset_actor_records();
    hook_select(expected_bike, expected_ctx);
    hook_billboard(context, emu, 17, -23, -41, 0xfedcba98u, -7);
    assert(select_calls == 1 && billboard_calls == 1);
    assert(g_nsel == 1 && g_nbb == 1);
    assert(g_sel[0].desc[0] == 101 && g_sel[0].desc[4] == 105);
    assert(g_sel[0].a8 == 0x12345678 && g_sel[0].f1c == -101);
    assert(g_sel[0].f28 == -102 && g_sel[0].f44 == -103 && g_sel[0].f50 == -104);
    assert(g_bb[0].emu == (uint32_t)(uintptr_t)emu && g_bb[0].emuw[0] == 300);
    assert(g_bb[0].nbefore == 0 && g_bb[0].nafter == 1);
    assert(g_bb[0].occ_owner == 0x20000700u);
    assert(g_bb[0].occ_row == 0xabcdef01);
    assert(g_bb[0].ctx4 == -201 && g_bb[0].ctx5 == 202 && g_bb[0].ctx6 == -203);
    /* Saturation may stop recording, but never stop original game behavior. */
    for (unsigned i = 1; i < 4096; ++i) {
        hook_select(expected_bike, expected_ctx);
        hook_billboard(context, emu, 17, -23, -41, 0xfedcba98u, -7);
    }
    assert(select_calls == 4096 && billboard_calls == 4096);
    assert(g_nsel == 512 && g_nbb == 512);
    assert(g_bb[511].nbefore == 511 && g_bb[511].nafter == 512);
    assert(g_bb[511].emuw[0] == 811);
    unsigned char saved_bb[sizeof g_bb], saved_sel[sizeof g_sel];
    memcpy(saved_bb, g_bb, sizeof g_bb);
    memcpy(saved_sel, g_sel, sizeof g_sel);
    hook_select(expected_bike, expected_ctx);
    hook_billboard(context, emu, 17, -23, -41, 0xfedcba98u, -7);
    assert(!memcmp(saved_bb, g_bb, sizeof g_bb));
    assert(!memcmp(saved_sel, g_sel, sizeof g_sel));
    reset_actor_records();
    assert(g_nsel == 0 && g_nbb == 0);
    hook_select(expected_bike, expected_ctx);
    hook_billboard(context, emu, 17, -23, -41, 0xfedcba98u, -7);
    assert(g_nsel == 1 && g_nbb == 1 && g_bb[0].nbefore == 4097);
    /* Disabled output likewise must preserve the original calls. */
    g_out = NULL;
    reset_actor_records();
    hook_select(expected_bike, expected_ctx);
    hook_billboard(context, emu, 17, -23, -41, 0xfedcba98u, -7);
    assert(g_nsel == 0 && g_nbb == 0);
    assert(select_calls == 4099 && billboard_calls == 4099);
    puts("actor record wrappers: forwarding, snapshots, bounds and reset passed");
    return 0;
}
