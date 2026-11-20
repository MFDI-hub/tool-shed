#include "uvs.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#if UVS_IS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#else
#include <unistd.h>
#endif

static int is_sep(char c) {
    return c == '/' || c == '\\';
}

int path_is_absolute(const char *path) {
    if (!path || !path[0]) {
        return 0;
    }
    if (is_sep(path[0])) {
        return 1;
    }
#if UVS_IS_WINDOWS
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':' && is_sep(path[2])) {
        return 1;
    }
    if (path[0] && path[1] == ':' && path[2] == '\0') {
        return 1;
    }
#endif
    return 0;
}

char *path_cwd(Arena *a) {
#if UVS_IS_WINDOWS
    DWORD n = GetCurrentDirectoryW(0, NULL);
    if (n == 0) {
        return arena_strdup(a, ".");
    }
    wchar_t *w = malloc(n * sizeof(wchar_t));
    if (!w) {
        fprintf(stderr, "error: out of memory\n");
        exit(1);
    }
    if (GetCurrentDirectoryW(n, w) == 0) {
        free(w);
        return arena_strdup(a, ".");
    }
    int bytes = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *out = arena_alloc(a, (size_t)bytes);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, bytes, NULL, NULL);
    free(w);
    return path_normalize(a, out);
#else
    char buf[UVS_PATH_MAX];
    if (!getcwd(buf, sizeof(buf))) {
        return arena_strdup(a, ".");
    }
    return path_normalize(a, buf);
#endif
}

char *path_normalize(Arena *a, const char *path) {
    if (!path || !path[0]) {
        return arena_strdup(a, ".");
    }
    char tmp[UVS_PATH_MAX];
    size_t n = 0;
    for (const char *p = path; *p && n + 1 < sizeof(tmp); p++) {
        tmp[n++] = is_sep(*p) ? '/' : *p;
    }
    tmp[n] = 0;

    int abs = path_is_absolute(tmp);
    char prefix[8] = {0};
    const char *rest = tmp;
#if UVS_IS_WINDOWS
    if (((tmp[0] >= 'A' && tmp[0] <= 'Z') || (tmp[0] >= 'a' && tmp[0] <= 'z')) && tmp[1] == ':') {
        prefix[0] = (char)toupper((unsigned char)tmp[0]);
        prefix[1] = ':';
        rest = tmp + 2;
        if (*rest == '/') {
            prefix[2] = '/';
            rest++;
        }
        abs = 1;
    } else if (tmp[0] == '/' && tmp[1] == '/') {
        prefix[0] = '/';
        prefix[1] = '/';
        rest = tmp + 2;
        abs = 1;
    }
#endif
    if (tmp[0] == '/') {
        prefix[0] = '/';
        rest = tmp + 1;
        abs = 1;
    }

    const char *parts[256];
    size_t nparts = 0;
    const char *p = rest;
    while (*p) {
        while (*p == '/') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *start = p;
        while (*p && *p != '/') {
            p++;
        }
        size_t len = (size_t)(p - start);
        if (len == 1 && start[0] == '.') {
            continue;
        }
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (nparts > 0) {
                nparts--;
            } else if (!abs) {
                parts[nparts++] = start;
            }
            continue;
        }
        if (nparts < 256) {
            parts[nparts++] = start;
        }
    }

    Buf buf;
    buf_init(&buf, a);
    buf_append_str(&buf, prefix[0] ? prefix : (abs ? "/" : ""));
    if (!prefix[0] && !abs && nparts == 0) {
        return arena_strdup(a, ".");
    }
    for (size_t i = 0; i < nparts; i++) {
        if (buf.len && buf.data[buf.len - 1] != '/') {
            buf_append_char(&buf, '/');
        }
        const char *part = parts[i];
        while (*part && *part != '/') {
            buf_append_char(&buf, *part++);
        }
    }
    if (buf.len == 0) {
        buf_append_str(&buf, abs ? "/" : ".");
    }
    buf_null_terminate(&buf);
    return buf.data;
}

char *path_join(Arena *a, const char *left, const char *right) {
    if (!right || !right[0]) {
        return path_normalize(a, left ? left : "");
    }
    if (path_is_absolute(right)) {
        return path_normalize(a, right);
    }
    if (!left || !left[0] || strcmp(left, ".") == 0) {
        return path_normalize(a, right);
    }
    return path_normalize(a, arena_sprintf(a, "%s/%s", left, right));
}

char *path_absolute(Arena *a, const char *path) {
    if (path_is_absolute(path)) {
        return path_normalize(a, path);
    }
    return path_join(a, path_cwd(a), path);
}

char *path_dirname(Arena *a, const char *path) {
    char *norm = path_normalize(a, path);
    char *slash = strrchr(norm, '/');
    if (!slash) {
        return arena_strdup(a, ".");
    }
    if (slash == norm) {
        return arena_strdup(a, "/");
    }
#if UVS_IS_WINDOWS
    if (slash == norm + 2 && norm[1] == ':') {
        return arena_strndup(a, norm, 3);
    }
#endif
    return arena_strndup(a, norm, (size_t)(slash - norm));
}

const char *path_basename(const char *path) {
    if (!path) {
        return "";
    }
    const char *best = path;
    for (const char *p = path; *p; p++) {
        if (is_sep(*p) && p[1]) {
            best = p + 1;
        }
    }
    return best;
}

static int path_prefix_eq(const char *path, const char *root, size_t n) {
#if UVS_IS_WINDOWS
    for (size_t i = 0; i < n; i++) {
        unsigned char a = (unsigned char)path[i];
        unsigned char b = (unsigned char)root[i];
        if (a >= 'A' && a <= 'Z') {
            a = (unsigned char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (unsigned char)(b - 'A' + 'a');
        }
        if (a != b) {
            return 0;
        }
    }
    return 1;
#else
    return strncmp(path, root, n) == 0;
#endif
}

char *path_relative(Arena *a, const char *path, const char *root) {
    char *p = path_normalize(a, path);
    char *r = path_normalize(a, root);
    size_t rn = strlen(r);
    if (rn == 1 && r[0] == '/') {
        if (p[0] == '/') {
            return p[1] ? p + 1 : arena_strdup(a, ".");
        }
        return p;
    }
    if (path_prefix_eq(p, r, rn) && (p[rn] == 0 || p[rn] == '/')) {
        if (p[rn] == 0) {
            return arena_strdup(a, ".");
        }
        return p + rn + 1;
    }
    return p;
}

int path_has_hidden_component(const char *path) {
    if (!path) {
        return 0;
    }
    const char *p = path;
#if UVS_IS_WINDOWS
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':') {
        p += 2;
    }
#endif
    while (*p) {
        while (is_sep(*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *start = p;
        while (*p && !is_sep(*p)) {
            p++;
        }
        size_t n = (size_t)(p - start);
        if (n >= 1 && start[0] == '.' && !(n == 1 || (n == 2 && start[1] == '.'))) {
            return 1;
        }
    }
    return 0;
}

int path_has_component(const char *path, const char *name) {
    size_t nlen = strlen(name);
    const char *p = path;
    while (*p) {
        while (is_sep(*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *start = p;
        while (*p && !is_sep(*p)) {
            p++;
        }
        size_t n = (size_t)(p - start);
        if (n == nlen && strncmp(start, name, n) == 0) {
            return 1;
        }
    }
    return 0;
}

int path_equal(const char *a, const char *b) {
    if (!a || !b) {
        return a == b;
    }
#if UVS_IS_WINDOWS
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca == '\\') {
            ca = '/';
        }
        if (cb == '\\') {
            cb = '/';
        }
        if (ca >= 'A' && ca <= 'Z') {
            ca = (unsigned char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (unsigned char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
    }
    return *a == 0 && *b == 0;
#else
    return strcmp(a, b) == 0;
#endif
}

int path_is_within(const char *path, const char *root) {
    size_t rn = strlen(root);
    if (!path_prefix_eq(path, root, rn)) {
        return 0;
    }
    return path[rn] == 0 || path[rn] == '/';
}

char *path_canonical(Arena *a, const char *path) {
    char *norm = path_absolute(a, path);
#if UVS_IS_WINDOWS
    wchar_t *wpath = NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, norm, -1, NULL, 0);
    if (n <= 0) {
        return norm;
    }
    wpath = malloc((size_t)n * sizeof(wchar_t));
    if (!wpath) {
        return norm;
    }
    MultiByteToWideChar(CP_UTF8, 0, norm, -1, wpath, n);
    HANDLE h = CreateFileW(wpath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS, NULL);
    free(wpath);
    if (h == INVALID_HANDLE_VALUE) {
        return norm;
    }
    wchar_t wout[UVS_PATH_MAX];
    DWORD got = GetFinalPathNameByHandleW(h, wout, UVS_PATH_MAX, FILE_NAME_NORMALIZED);
    CloseHandle(h);
    if (got == 0 || got >= UVS_PATH_MAX) {
        return norm;
    }
    const wchar_t *wsrc = wout;
    if (wcsncmp(wout, L"\\\\?\\UNC\\", 8) == 0) {
        wsrc = wout + 6;
        wout[6] = '\\';
    } else if (wcsncmp(wout, L"\\\\?\\", 4) == 0) {
        wsrc = wout + 4;
    }
    int bytes = WideCharToMultiByte(CP_UTF8, 0, wsrc, -1, NULL, 0, NULL, NULL);
    char *utf8 = arena_alloc(a, (size_t)bytes);
    WideCharToMultiByte(CP_UTF8, 0, wsrc, -1, utf8, bytes, NULL, NULL);
    return path_normalize(a, utf8);
#else
    char resolved[UVS_PATH_MAX];
    if (!realpath(norm, resolved)) {
        return norm;
    }
    return path_normalize(a, resolved);
#endif
}
