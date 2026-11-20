#include "uvs.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if UVS_IS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if UVS_IS_WINDOWS
static wchar_t *utf8_to_wide(const char *s) {
    if (!s) {
        return NULL;
    }
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) {
        return NULL;
    }
    wchar_t *w = malloc((size_t)n * sizeof(wchar_t));
    if (!w) {
        fprintf(stderr, "error: out of memory\n");
        exit(1);
    }
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static char *wide_to_utf8_arena(Arena *a, const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) {
        return arena_strdup(a, "");
    }
    char *s = arena_alloc(a, (size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}

static FILE *fopen_utf8(const char *path, const char *mode) {
    wchar_t *wpath = utf8_to_wide(path);
    wchar_t *wmode = utf8_to_wide(mode);
    FILE *f = NULL;
    if (wpath && wmode) {
        f = _wfopen(wpath, wmode);
    }
    free(wpath);
    free(wmode);
    return f;
}

static int64_t filetime_to_ns(const FILETIME *ft) {
    uint64_t ticks = ((uint64_t)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
    /* 100ns intervals since 1601; Unix epoch is 11644473600 seconds later. */
    const uint64_t epoch = 11644473600ULL * 10000000ULL;
    if (ticks < epoch) {
        return 0;
    }
    return (int64_t)((ticks - epoch) * 100ULL);
}
#else
static FILE *fopen_utf8(const char *path, const char *mode) {
    return fopen(path, mode);
}
#endif

char **uvs_cmdline_argv(int *argc) {
#if UVS_IS_WINDOWS
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    int n = 0;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &n);
    if (!wargv) {
        return NULL;
    }
    char **argv = calloc((size_t)n + 1, sizeof(char *));
    if (!argv) {
        LocalFree(wargv);
        fprintf(stderr, "error: out of memory\n");
        exit(1);
    }
    for (int i = 0; i < n; i++) {
        int bytes = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL);
        argv[i] = malloc((size_t)bytes);
        if (!argv[i]) {
            fprintf(stderr, "error: out of memory\n");
            exit(1);
        }
        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, argv[i], bytes, NULL, NULL);
    }
    LocalFree(wargv);
    *argc = n;
    return argv;
#else
    (void)argc;
    return NULL;
#endif
}

int fs_stat(const char *path, FsStat *out) {
    memset(out, 0, sizeof(*out));
#if UVS_IS_WINDOWS
    wchar_t *wpath = utf8_to_wide(path);
    if (!wpath) {
        return 0;
    }
    WIN32_FILE_ATTRIBUTE_DATA data;
    BOOL ok = GetFileAttributesExW(wpath, GetFileExInfoStandard, &data);
    free(wpath);
    if (!ok) {
        return 0;
    }
    out->exists = 1;
    out->is_dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    out->is_file = !out->is_dir;
    out->size = ((int64_t)data.nFileSizeHigh << 32) | (int64_t)data.nFileSizeLow;
    out->mtime_ns = filetime_to_ns(&data.ftLastWriteTime);
    return 1;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    out->exists = 1;
    out->is_dir = S_ISDIR(st.st_mode);
    out->is_file = S_ISREG(st.st_mode);
    out->size = (int64_t)st.st_size;
#if defined(__linux__)
    out->mtime_ns = (int64_t)st.st_mtim.tv_sec * 1000000000LL + (int64_t)st.st_mtim.tv_nsec;
#else
    out->mtime_ns = (int64_t)st.st_mtime * 1000000000LL;
#endif
    return 1;
#endif
}

int fs_exists(const char *path) {
    FsStat st;
    return fs_stat(path, &st) && st.exists;
}

int fs_is_file(const char *path) {
    FsStat st;
    return fs_stat(path, &st) && st.is_file;
}

int fs_is_dir(const char *path) {
    FsStat st;
    return fs_stat(path, &st) && st.is_dir;
}

int fs_mkdirs(const char *dir) {
    if (!dir || !dir[0] || strcmp(dir, ".") == 0) {
        return 1;
    }
    if (fs_is_dir(dir)) {
        return 1;
    }
    char copy[UVS_PATH_MAX];
    size_t n = strlen(dir);
    if (n >= sizeof(copy)) {
        return 0;
    }
    memcpy(copy, dir, n + 1);
    for (char *p = copy; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    size_t start = 0;
#if UVS_IS_WINDOWS
    if (copy[0] && copy[1] == ':') {
        start = 2;
        if (copy[2] == '/') {
            start = 3;
        }
    }
#endif
    if (copy[0] == '/') {
        start = 1;
    }
    for (size_t i = start; i <= n; i++) {
        if (copy[i] == '/' || copy[i] == 0) {
            char saved = copy[i];
            copy[i] = 0;
            if (copy[0] && !fs_is_dir(copy)) {
#if UVS_IS_WINDOWS
                wchar_t *wpath = utf8_to_wide(copy);
                if (!wpath) {
                    return 0;
                }
                BOOL ok = CreateDirectoryW(wpath, NULL);
                DWORD err = GetLastError();
                free(wpath);
                if (!ok && err != ERROR_ALREADY_EXISTS) {
                    return 0;
                }
#else
                if (mkdir(copy, 0755) != 0 && errno != EEXIST) {
                    return 0;
                }
#endif
            }
            copy[i] = saved;
        }
    }
    return fs_is_dir(dir);
}

int fs_mkdirs_parent(const char *file_path) {
    char tmp[UVS_PATH_MAX];
    size_t n = strlen(file_path);
    if (n >= sizeof(tmp)) {
        return 0;
    }
    memcpy(tmp, file_path, n + 1);
    char *slash = strrchr(tmp, '/');
    char *bslash = strrchr(tmp, '\\');
    if (bslash && (!slash || bslash > slash)) {
        slash = bslash;
    }
    if (!slash) {
        return 1;
    }
    *slash = 0;
    return fs_mkdirs(tmp);
}

char *fs_read_all(Arena *a, const char *path, size_t *len_out) {
    FILE *f = fopen_utf8(path, "rb");
    if (!f) {
        if (len_out) {
            *len_out = 0;
        }
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    char *buf = arena_alloc(a, (size_t)sz + 1);
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = 0;
    if (len_out) {
        *len_out = got;
    }
    return buf;
}

static int write_bytes(const char *path, const char *data, size_t len, int append) {
    if (!fs_mkdirs_parent(path)) {
        fprintf(stderr, "error: cannot create directory for %s\n", path);
        return 0;
    }
    FILE *f = fopen_utf8(path, append ? "ab" : "wb");
    if (!f) {
        fprintf(stderr, "error: cannot write %s\n", path);
        return 0;
    }
    size_t wrote = fwrite(data, 1, len, f);
    int err = ferror(f);
    fclose(f);
    return wrote == len && !err;
}

int fs_write_all(const char *path, const char *data, size_t len) {
    return write_bytes(path, data, len, 0);
}

int fs_append_all(const char *path, const char *data, size_t len) {
    return write_bytes(path, data, len, 1);
}

typedef struct {
    WalkCb cb;
    void *ud;
    int follow;
    StrList visited;
} WalkState;

static int walk_enter_dir(Arena *scratch, WalkState *st, const char *dir);

#if UVS_IS_WINDOWS
static int walk_dir(Arena *scratch, const char *dir, WalkState *st) {
    char *pattern = path_join(scratch, dir, "*");
    wchar_t *wpattern = utf8_to_wide(pattern);
    if (!wpattern) {
        return 0;
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wpattern, &fd);
    free(wpattern);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    int ok = 1;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
            continue;
        }
        char *name = wide_to_utf8_arena(scratch, fd.cFileName);
        char *child = path_join(scratch, dir, name);
        int is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        int is_reparse = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        if (is_dir && strcmp(name, ".git") == 0) {
            continue;
        }
        int rc = st->cb(child, is_dir, st->ud);
        if (rc < 0) {
            ok = 0;
            break;
        }
        if (is_dir && rc == 0 && (!is_reparse || st->follow)) {
            if (!walk_enter_dir(scratch, st, child)) {
                ok = 0;
                break;
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}
#else
static int walk_dir(Arena *scratch, const char *dir, WalkState *st) {
    DIR *d = opendir(dir);
    if (!d) {
        return 0;
    }
    int ok = 1;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        if (strcmp(ent->d_name, ".git") == 0) {
            continue;
        }
        char *child = path_join(scratch, dir, ent->d_name);
        struct stat stbuf;
        if (lstat(child, &stbuf) != 0) {
            continue;
        }
        int is_dir = S_ISDIR(stbuf.st_mode);
        int is_link = S_ISLNK(stbuf.st_mode);
        int report_dir = is_dir && (!is_link || st->follow);
        int rc = st->cb(child, report_dir, st->ud);
        if (rc < 0) {
            ok = 0;
            break;
        }
        if (is_dir && rc == 0 && (!is_link || st->follow)) {
            if (!walk_enter_dir(scratch, st, child)) {
                ok = 0;
                break;
            }
        }
    }
    closedir(d);
    return ok;
}
#endif

static int walk_enter_dir(Arena *scratch, WalkState *st, const char *dir) {
    if (st->follow) {
        char *canon = path_canonical(scratch, dir);
        if (strlist_contains(&st->visited, canon)) {
            return 1;
        }
        strlist_push(&st->visited, canon);
    }
    return walk_dir(scratch, dir, st);
}

int fs_walk_ex(const char *root, WalkCb cb, void *ud, int follow_symlinks) {
    Arena *scratch = arena_new();
    char *abs = path_absolute(scratch, root);
    WalkState st;
    st.cb = cb;
    st.ud = ud;
    st.follow = follow_symlinks;
    strlist_init(&st.visited, scratch);
    int result = 1;
    if (fs_is_file(abs)) {
        result = cb(abs, 0, ud) >= 0;
    } else if (fs_is_dir(abs)) {
        int rc = cb(abs, 1, ud);
        if (rc < 0) {
            result = 0;
        } else if (rc == 0) {
            char *canon = path_canonical(scratch, abs);
            strlist_push(&st.visited, canon);
            result = walk_dir(scratch, abs, &st);
        } else {
            result = 1;
        }
    } else {
        result = 0;
    }
    arena_free(scratch);
    return result;
}

int fs_walk(const char *root, WalkCb cb, void *ud) {
    return fs_walk_ex(root, cb, ud, 0);
}

int uvs_run_capture(Arena *a, const char *cmd, char **out, size_t *len_out) {
    if (out) {
        *out = arena_strdup(a, "");
    }
    if (len_out) {
        *len_out = 0;
    }
    if (!cmd) {
        return 0;
    }
#if UVS_IS_WINDOWS
    FILE *f = _popen(cmd, "rb");
#else
    FILE *f = popen(cmd, "r");
#endif
    if (!f) {
        return 0;
    }
    Buf buf;
    buf_init(&buf, a);
    char tmp[4096];
    size_t n;
    while ((n = fread(tmp, 1, sizeof(tmp), f)) > 0) {
        buf_append(&buf, tmp, n);
    }
    buf_null_terminate(&buf);
#if UVS_IS_WINDOWS
    int rc = _pclose(f);
#else
    int rc = pclose(f);
#endif
    if (out) {
        *out = buf.data ? buf.data : arena_strdup(a, "");
    }
    if (len_out) {
        *len_out = buf.len;
    }
    return rc == 0;
}

#if UVS_IS_WINDOWS
static void win_append_arg(Buf *b, const char *arg) {
    int quote = !*arg;
    for (const char *p = arg; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '"') {
            quote = 1;
            break;
        }
    }
    if (!quote) {
        buf_append_str(b, arg);
        return;
    }
    buf_append_char(b, '"');
    size_t bs = 0;
    for (const char *p = arg;; p++) {
        if (*p == '\\') {
            bs++;
            continue;
        }
        if (*p == '"' || *p == 0) {
            size_t n = (*p == '"') ? bs * 2 + 1 : bs;
            for (size_t i = 0; i < n; i++) {
                buf_append_char(b, '\\');
            }
            if (*p == 0) {
                break;
            }
            buf_append_char(b, '"');
            bs = 0;
            continue;
        }
        for (size_t i = 0; i < bs; i++) {
            buf_append_char(b, '\\');
        }
        bs = 0;
        buf_append_char(b, *p);
    }
    buf_append_char(b, '"');
}
#endif

int uvs_run_capture_argv(Arena *a, char **argv, char **out, size_t *len_out) {
    if (out) {
        *out = arena_strdup(a, "");
    }
    if (len_out) {
        *len_out = 0;
    }
    if (!argv || !argv[0] || !argv[0][0]) {
        fprintf(stderr, "error: empty command\n");
        return 127;
    }
    Buf buf;
    buf_init(&buf, a);
#if UVS_IS_WINDOWS
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE rd = NULL;
    HANDLE wr = NULL;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        fprintf(stderr, "error: could not run %s\n", argv[0]);
        return 127;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    Arena *tmp = arena_new();
    Buf cmdline;
    buf_init(&cmdline, tmp);
    for (size_t i = 0; argv[i]; i++) {
        if (i) {
            buf_append_char(&cmdline, ' ');
        }
        win_append_arg(&cmdline, argv[i]);
    }
    buf_null_terminate(&cmdline);
    wchar_t *wcmd = utf8_to_wide(cmdline.data ? cmdline.data : "");
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = wr;
    si.hStdError = wr;
    memset(&pi, 0, sizeof(pi));
    BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    free(wcmd);
    arena_free(tmp);
    CloseHandle(wr);
    if (!ok) {
        CloseHandle(rd);
        fprintf(stderr, "error: could not run %s\n", argv[0]);
        return 127;
    }
    char chunk[4096];
    DWORD n = 0;
    for (;;) {
        if (!ReadFile(rd, chunk, sizeof(chunk), &n, NULL) || n == 0) {
            break;
        }
        buf_append(&buf, chunk, (size_t)n);
    }
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    buf_null_terminate(&buf);
    if (out) {
        *out = buf.data ? buf.data : arena_strdup(a, "");
    }
    if (len_out) {
        *len_out = buf.len;
    }
    return (int)code;
#else
    int fds[2];
    if (pipe(fds) != 0) {
        fprintf(stderr, "error: could not run %s\n", argv[0]);
        return 127;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        fprintf(stderr, "error: could not run %s\n", argv[0]);
        return 127;
    }
    if (pid == 0) {
        close(fds[0]);
        if (dup2(fds[1], 1) < 0 || dup2(fds[1], 2) < 0) {
            _exit(127);
        }
        if (fds[1] != 1 && fds[1] != 2) {
            close(fds[1]);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    char chunk[4096];
    ssize_t n;
    while ((n = read(fds[0], chunk, sizeof(chunk))) > 0) {
        buf_append(&buf, chunk, (size_t)n);
    }
    close(fds[0]);
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return 127;
    }
    buf_null_terminate(&buf);
    if (out) {
        *out = buf.data ? buf.data : arena_strdup(a, "");
    }
    if (len_out) {
        *len_out = buf.len;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 127;
#endif
}

int uvs_run_argv(char **argv) {
    if (!argv || !argv[0] || !argv[0][0]) {
        fprintf(stderr, "error: empty command\n");
        return 127;
    }
#if UVS_IS_WINDOWS
    Arena *tmp = arena_new();
    Buf cmdline;
    buf_init(&cmdline, tmp);
    for (size_t i = 0; argv[i]; i++) {
        if (i) {
            buf_append_char(&cmdline, ' ');
        }
        win_append_arg(&cmdline, argv[i]);
    }
    buf_null_terminate(&cmdline);
    wchar_t *wcmd = utf8_to_wide(cmdline.data ? cmdline.data : "");
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    free(wcmd);
    arena_free(tmp);
    if (!ok) {
        fprintf(stderr, "error: could not run %s\n", argv[0]);
        return 127;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
#else
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "error: could not run %s\n", argv[0]);
        return 127;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "error: could not run %s\n", argv[0]);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return 127;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 127;
#endif
}

void uvs_sleep_seconds(double seconds) {
    if (seconds < 0) {
        seconds = 0;
    }
#if UVS_IS_WINDOWS
    DWORD ms = (DWORD)(seconds * 1000.0 + 0.5);
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1000000000.0);
    if (ts.tv_nsec < 0) {
        ts.tv_nsec = 0;
    }
    nanosleep(&ts, NULL);
#endif
}
