#include "uvs.h"

#include <stdlib.h>
#include <string.h>

#if UVS_IS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

int uvs_clipboard_set(const char *utf8) {
    if (!utf8) {
        utf8 = "";
    }
#if UVS_IS_WINDOWS
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (n <= 0) {
        return 0;
    }
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (size_t)n * sizeof(wchar_t));
    if (!mem) {
        return 0;
    }
    wchar_t *w = GlobalLock(mem);
    if (!w) {
        GlobalFree(mem);
        return 0;
    }
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, n);
    GlobalUnlock(mem);
    if (!OpenClipboard(NULL)) {
        GlobalFree(mem);
        return 0;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, mem)) {
        CloseClipboard();
        GlobalFree(mem);
        return 0;
    }
    CloseClipboard();
    return 1;
#else
    const char *cmd = NULL;
    if (system("command -v pbcopy >/dev/null 2>&1") == 0) {
        cmd = "pbcopy";
    } else if (system("command -v wl-copy >/dev/null 2>&1") == 0) {
        cmd = "wl-copy";
    } else if (system("command -v xclip >/dev/null 2>&1") == 0) {
        cmd = "xclip -selection clipboard";
    } else {
        return 0;
    }
    FILE *f = popen(cmd, "w");
    if (!f) {
        return 0;
    }
    fwrite(utf8, 1, strlen(utf8), f);
    return pclose(f) == 0;
#endif
}
