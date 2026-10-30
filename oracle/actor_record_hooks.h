/* Exact oracle actor-call wrappers, shared with the synthetic native regression. */
#ifndef RR_ACTOR_RECORD_HOOKS_H
#define RR_ACTOR_RECORD_HOOKS_H
#ifndef RR_ACTOR_READ_U32
#define RR_ACTOR_READ_U32(p) (*(uint32_t*)(uintptr_t)(p))
#endif
#ifndef RR_ACTOR_READ_I32
#define RR_ACTOR_READ_I32(p) ((int32_t)RR_ACTOR_READ_U32(p))
#endif
#ifndef RR_ACTOR_SELECT_ORIGINAL
#define RR_ACTOR_SELECT_ORIGINAL ((void(__fastcall*)(uint32_t, int))0x0043af60)
#endif
#ifndef RR_ACTOR_BILLBOARD_ORIGINAL
#define RR_ACTOR_BILLBOARD_ORIGINAL                                                                          \
    ((void(__fastcall*)(int*, unsigned*, int, int, int, unsigned, signed char))0x0044ba40)
#endif
static struct {
    uint32_t bike, ctx, desc[5], a8, b0, b4, bc, view;
    int32_t f1c, f28, f44, f50;
} g_sel[512];
static uint32_t g_nsel;
static struct {
    int32_t size, lateral, ground, pre_lat;
    uint32_t pos;
    int32_t clip, nbefore, nafter;
    uint32_t occ_owner, occ_row;
    int32_t ctx4, ctx5, ctx6;
    uint32_t emu, emuw[6];
} g_bb[512];
static uint32_t g_nbb;
static void __fastcall hook_select(uint32_t bike, int ctx) {
    uint32_t desc = 0, d0 = 0, d1 = 0, d2 = 0, d3 = 0, d4 = 0;
    int32_t f1c = 0, f28 = 0, f44 = 0, f50 = 0;
    if (readable(bike + 0x98, 4)) {
        desc = (uint32_t)RR_ACTOR_READ_U32(bike + 0x98);
        if (readable(desc, 20)) {
            d0 = RR_ACTOR_READ_U32(desc);
            d1 = RR_ACTOR_READ_U32(desc + 4);
            d2 = RR_ACTOR_READ_U32(desc + 8);
            d3 = RR_ACTOR_READ_U32(desc + 12);
            d4 = RR_ACTOR_READ_U32(desc + 16);
        }
    }
    // Actor lateral/position fields at dispatch time (the billboard hook
    // only sees derived inputs, not the object).
    if (readable(bike + 0x54, 4)) {
        f1c = RR_ACTOR_READ_I32(bike + 0x1c);
        f28 = RR_ACTOR_READ_I32(bike + 0x28);
        f44 = RR_ACTOR_READ_I32(bike + 0x44);
        f50 = RR_ACTOR_READ_I32(bike + 0x50);
    }
    RR_ACTOR_SELECT_ORIGINAL(bike, ctx);
    if (!g_out || g_nsel >= 512) return;
    if (!readable(bike + 0xe0, 4)) return;
    g_sel[g_nsel].bike = bike;
    g_sel[g_nsel].ctx = (uint32_t)ctx;
    g_sel[g_nsel].desc[0] = d0;
    g_sel[g_nsel].desc[1] = d1;
    g_sel[g_nsel].desc[2] = d2;
    g_sel[g_nsel].desc[3] = d3;
    g_sel[g_nsel].desc[4] = d4;
    g_sel[g_nsel].a8 = (uint32_t)RR_ACTOR_READ_U32(bike + 0xa8);
    g_sel[g_nsel].b0 = (uint32_t)RR_ACTOR_READ_U32(bike + 0xb0);
    g_sel[g_nsel].b4 = (uint32_t)RR_ACTOR_READ_U32(bike + 0xb4);
    g_sel[g_nsel].bc = (uint32_t)RR_ACTOR_READ_U32(bike + 0xbc);
    g_sel[g_nsel].view = (uint32_t)RR_ACTOR_READ_U32(bike + 0xd0);
    g_sel[g_nsel].f1c = f1c;
    g_sel[g_nsel].f28 = f28;
    g_sel[g_nsel].f44 = f44;
    g_sel[g_nsel].f50 = f50;
    ++g_nsel;
}
static void __fastcall hook_billboard(int* p1, unsigned* p2, int size, int lateral, int ground, unsigned pos,
                                      signed char clip) {
    const uint32_t base = (uint32_t)RD32(0x0049e66c), end = (uint32_t)RD32(0x0049e660);
    const int32_t nbefore = end >= base ? (int32_t)((end - base) / 0xa8) : -1;
    const int32_t pre_lat = lateral; // params may not survive the forwarded call; snapshot first
    // Node emu (param_2) words before the call: [0]=+a8 resource, [2]=+b0,
    // [3]=+b4, [4]=+b8, [5]=+bc roll. Settles the emu-vs-address question.
    uintptr_t emu = (uintptr_t)p2;
    uint32_t emuw[6] = {0, 0, 0, 0, 0, 0};
    if (readable(emu, 24))
        for (int w = 0; w < 6; ++w) emuw[w] = p2[w];
    RR_ACTOR_BILLBOARD_ORIGINAL(p1, p2, size, lateral, ground, pos, clip);
    if (!g_out || g_nbb >= 512) return;
    const uint32_t e2 = (uint32_t)RD32(0x0049e660);
    g_bb[g_nbb].size = size;
    g_bb[g_nbb].lateral = lateral;
    g_bb[g_nbb].ground = ground;
    g_bb[g_nbb].pre_lat = pre_lat;
    g_bb[g_nbb].pos = pos;
    g_bb[g_nbb].clip = clip;
    g_bb[g_nbb].nbefore = nbefore;
    g_bb[g_nbb].nafter = e2 >= base ? (int32_t)((e2 - base) / 0xa8) : -1;
    g_bb[g_nbb].emu = emu;
    for (int w = 0; w < 6; ++w) g_bb[g_nbb].emuw[w] = emuw[w];
    // Slice context (param_1): [2] occlusion owner, [4]/[5] road origin,
    // [6] depth word. The occlusion row lives at [2]+0x10.
    g_bb[g_nbb].occ_owner = 0;
    g_bb[g_nbb].occ_row = 0;
    g_bb[g_nbb].ctx4 = 0;
    g_bb[g_nbb].ctx5 = 0;
    g_bb[g_nbb].ctx6 = 0;
    if (readable((uintptr_t)&p1[2], 4)) {
        const uint32_t owner = *(uint32_t*)&p1[2];
        g_bb[g_nbb].occ_owner = owner;
        if (readable(owner + 0x10, 4)) g_bb[g_nbb].occ_row = RR_ACTOR_READ_U32(owner + 0x10);
    }
    if (readable((uintptr_t)&p1[4], 12)) {
        g_bb[g_nbb].ctx4 = p1[4];
        g_bb[g_nbb].ctx5 = p1[5];
        g_bb[g_nbb].ctx6 = p1[6];
    }
    ++g_nbb;
}

/* Records belong to one present, including SIM presents that skip dump_frame. */
static void reset_actor_records(void) {
    g_nsel = 0;
    g_nbb = 0;
}
#endif
