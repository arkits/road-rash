/* Frontend acceptance driver. Only enabled by RR_ORACLE_FRONTEND=1.
 * Commands are explicit instrumentation; screen entry uses the original's
 * cross-thread PostMessage path, input uses its normal child-window handlers.
 * Never writes career state or calls race outcomes/save serialisers directly. */
static void frontend_dump(const char* label) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "C:\\fe-%s.state", label);
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite((void*)0x004b8a10, 1, 32, f);
        const unsigned rider = RD8(0x00467a6c);
        for (unsigned p = 0; p < 10; ++p)
            fwrite((void*)(uintptr_t)(0x004c6b40 + p * 0x204 + rider * 12), 1, 12, f);
        fclose(f);
    }
    /* Raw decoded RGB555 image cache, before either surface conversion. */
    for (unsigned i = 0; i < 9; ++i) {
        const uint32_t row = 0x004859c0 + i * 0x58;
        const uint32_t pixels = (uint32_t)RD32(row + 4);
        if (!readable(pixels, 640 * 480 * 2)) continue;
        snprintf(path, sizeof path, "C:\\fe-%s-cache%u.raw", label, i);
        f = fopen(path, "wb");
        if (f) {
            fwrite((void*)(uintptr_t)pixels, 2, 640 * 480, f);
            fclose(f);
        }
        if (g_out) fprintf(g_out, "# fe cache %u %s\n", i, (char*)(uintptr_t)(row + 8));
    }
}

static int frontend_shot(const char* label) {
    HWND window = NULL;
    EnumWindows(find_game_process_window, (LPARAM)&window);
    if (!window) return 0;
    RECT rc;
    GetClientRect(window, &rc);
    const int w = rc.right, h = rc.bottom;
    if (w != 640 || h != 480) return 0;
    HDC source = GetDC(window), dc = CreateCompatibleDC(source);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    void* bits = NULL;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    int ok = 0;
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(dc, bmp);
        ok = BitBlt(dc, 0, 0, w, h, source, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        if (ok) {
            char path[MAX_PATH];
            snprintf(path, sizeof path, "C:\\fe-%s.bmp", label);
            FILE* f = fopen(path, "wb");
            if (f) {
                BITMAPFILEHEADER bh;
                memset(&bh, 0, sizeof bh);
                bh.bfType = 0x4d42;
                bh.bfOffBits = 54;
                bh.bfSize = 54 + w * h * 3;
                bi.bmiHeader.biHeight = h;
                fwrite(&bh, 1, sizeof bh, f);
                fwrite(&bi.bmiHeader, 1, 40, f);
                for (int y = h - 1; y >= 0; --y) fwrite((uint8_t*)bits + y * w * 3, 3, w, f);
                fclose(f);
            } else
                ok = 0;
        }
        DeleteObject(bmp);
    }
    DeleteDC(dc);
    ReleaseDC(window, source);
    return ok;
}

static void frontend_key(unsigned vk) {
    const uint32_t item = (uint32_t)RD32(0x004961f8);
    HWND window = readable(item, 4) ? (HWND)(uintptr_t)RD32(item) : NULL;
    if (!window) window = (HWND)(uintptr_t)RD32(0x004753dc);
    const unsigned scan = MapVirtualKeyA(vk, 0);
    PostMessageA(window, WM_KEYDOWN, vk, 1 | (scan << 16));
    PostMessageA(window, WM_KEYUP, vk, 1 | (scan << 16) | 0xc0000000u);
}

static HWND frontend_dialog;
static BOOL CALLBACK frontend_find_dialog(HWND window, LPARAM ignored) {
    (void)ignored;
    char name[64];
    GetClassNameA(window, name, sizeof name);
    if (IsWindowVisible(window) && !strcmp(name, "#32770")) {
        frontend_dialog = window;
        return FALSE;
    }
    return TRUE;
}
static void frontend_activate(unsigned index) {
    if (index >= (unsigned)RD32(0x00496c18)) return;
    HWND window = (HWND)(uintptr_t)RD32(0x00496028 + index * 4);
    PostMessageA(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
    PostMessageA(window, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
}

static DWORD WINAPI frontend_probe(LPVOID unused) {
    (void)unused;
    char script[MAX_PATH], line[256];
    env("RR_ORACLE_FRONTEND_SCRIPT", script, sizeof script, "C:\\frontend.script");
    FILE* f = fopen(script, "r");
    if (!f) return 1;
    for (int i = 0; i < 600 && RD32(0x0046a660) != 9; ++i) Sleep(100);
    while (fgets(line, sizeof line, f)) {
        char command[32], arg[128];
        int value = 0;
        arg[0] = 0;
        if (sscanf(line, "%31s %127s", command, arg) < 1 || command[0] == '#') continue;
        if (g_out) {
            fprintf(g_out, "# fe command %s", line);
            fflush(g_out);
        }
        if (!strcmp(command, "wait")) {
            value = atoi(arg);
            int i = 0;
            for (; i < 1200 && RD32(0x0046a660) != value; ++i) Sleep(100);
            if (i == 1200) {
                if (g_out) fprintf(g_out, "# fe FAIL wait %d got %d\n", value, RD32(0x0046a660));
                break;
            }
            Sleep(600);
        } else if (!strcmp(command, "sleep"))
            Sleep(atoi(arg));
        else if (!strcmp(command, "key"))
            frontend_key(strtoul(arg, NULL, 0));
        else if (!strcmp(command, "enter"))
            ((void(__fastcall*)(int))0x00421df0)(atoi(arg));
        else if (!strcmp(command, "activate"))
            frontend_activate(atoi(arg));
        else if (!strcmp(command, "wait_result")) {
            int i = 0;
            for (; i < 12000; ++i) {
                const int screen = RD32(0x0046a660);
                if (screen >= 28 && screen <= 31) break;
                if (i % 100 == 0 && g_out) {
                    fprintf(g_out, "# fe race frame %u screen %d outcome %d mode %d human %d\n", g_frame,
                            screen, RD32(0x004c5d7c), RD32(0x004753b0), RD8(0x004753c8) == 0);
                    fflush(g_out);
                }
                Sleep(100);
            }
            if (i == 12000) {
                if (g_out) fputs("# fe FAIL race did not finish\n", g_out);
                break;
            }
            Sleep(800);
        } else if (!strcmp(command, "saveas")) {
            frontend_activate(2);
            frontend_dialog = NULL;
            for (int i = 0; i < 100 && !frontend_dialog; ++i) {
                EnumThreadWindows(RD32(0x004753f0), frontend_find_dialog, 0);
                Sleep(100);
            }
            if (!frontend_dialog) {
                if (g_out) fputs("# fe FAIL no save dialog\n", g_out);
                break;
            }
            SetDlgItemTextA(frontend_dialog, 0x480, arg);
            PostMessageA(frontend_dialog, WM_COMMAND, IDOK, 0);
            Sleep(1000);
        } else if (!strcmp(command, "capture")) {
            const int ok = frontend_shot(arg);
            frontend_dump(arg);
            if (g_out) {
                fprintf(g_out, "# fe captured %s %d screen %d\n", arg, ok, RD32(0x0046a660));
                fflush(g_out);
            }
        } else if (!strcmp(command, "exit")) {
            if (g_out) {
                fputs("# fe complete\n", g_out);
                fflush(g_out);
            }
            fclose(f);
            ExitProcess(0);
        }
        Sleep(200);
    }
    fclose(f);
    if (g_out) {
        fputs("# fe stopped\n", g_out);
        fflush(g_out);
    }
    return 0;
}
