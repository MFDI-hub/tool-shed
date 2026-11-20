#include "uvs.h"

#include <stdlib.h>
#include <string.h>

#if UVS_IS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__linux__)
#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#define UVS_HAS_KQUEUE 1
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/event.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#endif

static unsigned timeout_ms(double seconds) {
    if (seconds < 0) {
        seconds = 0;
    }
    double ms = seconds * 1000.0 + 0.5;
    if (ms > 86400000.0) {
        ms = 86400000.0;
    }
    return (unsigned)ms;
}

#if UVS_IS_WINDOWS
static wchar_t *watch_utf8_to_wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) {
        return NULL;
    }
    wchar_t *w = malloc((size_t)n * sizeof(wchar_t));
    if (!w) {
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

int uvs_watch_wait(const char **paths, size_t npaths, double timeout_seconds) {
    if (!paths || npaths == 0) {
        uvs_sleep_seconds(timeout_seconds);
        return 0;
    }
    HANDLE handles[64];
    HANDLE dirs[64];
    OVERLAPPED ovs[64];
    char *bufs[64];
    size_t n = 0;
    DWORD notify = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES |
                   FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;
    for (size_t i = 0; i < npaths && n < 64; i++) {
        const char *p = paths[i];
        char watch_path[UVS_PATH_MAX];
        if (fs_is_file(p)) {
            Arena *tmp = arena_new();
            char *dir = path_dirname(tmp, p);
            size_t len = strlen(dir);
            if (len >= sizeof(watch_path)) {
                arena_free(tmp);
                continue;
            }
            memcpy(watch_path, dir, len + 1);
            arena_free(tmp);
            p = watch_path;
        }
        if (!fs_is_dir(p)) {
            continue;
        }
        wchar_t *w = watch_utf8_to_wide(p);
        if (!w) {
            continue;
        }
        HANDLE dir = CreateFileW(w, FILE_LIST_DIRECTORY,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
        free(w);
        if (dir == INVALID_HANDLE_VALUE) {
            continue;
        }
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!ev) {
            CloseHandle(dir);
            continue;
        }
        memset(&ovs[n], 0, sizeof(ovs[n]));
        ovs[n].hEvent = ev;
        bufs[n] = malloc(65536);
        if (!bufs[n]) {
            CloseHandle(ev);
            CloseHandle(dir);
            continue;
        }
        if (!ReadDirectoryChangesW(dir, bufs[n], 65536, TRUE, notify, NULL, &ovs[n], NULL)) {
            free(bufs[n]);
            CloseHandle(ev);
            CloseHandle(dir);
            continue;
        }
        dirs[n] = dir;
        handles[n] = ev;
        n++;
    }
    if (n == 0) {
        return -1;
    }
    DWORD wait = WaitForMultipleObjects((DWORD)n, handles, FALSE, (DWORD)timeout_ms(timeout_seconds));
    int changed = 0;
    if (wait >= WAIT_OBJECT_0 && wait < WAIT_OBJECT_0 + n) {
        changed = 1;
    }
    for (size_t i = 0; i < n; i++) {
        CancelIo(dirs[i]);
        CloseHandle(dirs[i]);
        CloseHandle(handles[i]);
        free(bufs[i]);
    }
    if (wait == WAIT_FAILED) {
        return -1;
    }
    return changed;
}

#elif defined(__linux__)
static int add_inotify_tree(int fd, const char *dir) {
    int wd = inotify_add_watch(fd, dir, IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MODIFY | IN_MOVE | IN_ATTRIB);
    if (wd < 0) {
        return 0;
    }
    DIR *d = opendir(dir);
    if (!d) {
        return 1;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0 || strcmp(ent->d_name, ".git") == 0) {
            continue;
        }
        char child[UVS_PATH_MAX];
        size_t n = (size_t)snprintf(child, sizeof(child), "%s/%s", dir, ent->d_name);
        if (n >= sizeof(child)) {
            continue;
        }
        struct stat st;
        if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
            add_inotify_tree(fd, child);
        }
    }
    closedir(d);
    return 1;
}

int uvs_watch_wait(const char **paths, size_t npaths, double timeout_seconds) {
    int fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0) {
        return -1;
    }
    int any = 0;
    for (size_t i = 0; i < npaths; i++) {
        const char *p = paths[i];
        char buf[UVS_PATH_MAX];
        if (fs_is_file(p)) {
            Arena *tmp = arena_new();
            char *dir = path_dirname(tmp, p);
            snprintf(buf, sizeof(buf), "%s", dir);
            arena_free(tmp);
            p = buf;
        }
        if (fs_is_dir(p)) {
            any |= add_inotify_tree(fd, p);
        }
    }
    if (!any) {
        close(fd);
        return -1;
    }
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    int pr = poll(&pfd, 1, (int)timeout_ms(timeout_seconds));
    char dump[4096];
    while (read(fd, dump, sizeof(dump)) > 0) {
    }
    close(fd);
    if (pr < 0) {
        return -1;
    }
    return pr > 0 ? 1 : 0;
}

#elif defined(UVS_HAS_KQUEUE)
static int collect_dirs(const char *dir, int *fds, int *nfds, int cap) {
    if (*nfds >= cap) {
        return 1;
    }
    int fd = open(dir, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        return 1;
    }
    fds[(*nfds)++] = fd;
    DIR *d = opendir(dir);
    if (!d) {
        return 1;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0 || strcmp(ent->d_name, ".git") == 0) {
            continue;
        }
        char child[UVS_PATH_MAX];
        if ((size_t)snprintf(child, sizeof(child), "%s/%s", dir, ent->d_name) >= sizeof(child)) {
            continue;
        }
        struct stat st;
        if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
            collect_dirs(child, fds, nfds, cap);
        }
    }
    closedir(d);
    return 1;
}

int uvs_watch_wait(const char **paths, size_t npaths, double timeout_seconds) {
    int kq = kqueue();
    if (kq < 0) {
        return -1;
    }
    int fds[256];
    int nfds = 0;
    for (size_t i = 0; i < npaths; i++) {
        const char *p = paths[i];
        char buf[UVS_PATH_MAX];
        if (fs_is_file(p)) {
            Arena *tmp = arena_new();
            char *dir = path_dirname(tmp, p);
            snprintf(buf, sizeof(buf), "%s", dir);
            arena_free(tmp);
            p = buf;
        }
        if (fs_is_dir(p)) {
            collect_dirs(p, fds, &nfds, 256);
        }
    }
    if (nfds == 0) {
        close(kq);
        return -1;
    }
    struct kevent *evs = calloc((size_t)nfds, sizeof(struct kevent));
    if (!evs) {
        for (int i = 0; i < nfds; i++) {
            close(fds[i]);
        }
        close(kq);
        return -1;
    }
    for (int i = 0; i < nfds; i++) {
        EV_SET(&evs[i], fds[i], EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_ATTRIB | NOTE_EXTEND,
               0, NULL);
    }
    kevent(kq, evs, nfds, NULL, 0, NULL);
    struct timespec ts;
    ts.tv_sec = (time_t)timeout_seconds;
    ts.tv_nsec = (long)((timeout_seconds - (double)ts.tv_sec) * 1000000000.0);
    struct kevent out;
    int n = kevent(kq, NULL, 0, &out, 1, &ts);
    for (int i = 0; i < nfds; i++) {
        close(fds[i]);
    }
    free(evs);
    close(kq);
    if (n < 0) {
        return -1;
    }
    return n > 0 ? 1 : 0;
}

#else
int uvs_watch_wait(const char **paths, size_t npaths, double timeout_seconds) {
    (void)paths;
    (void)npaths;
    (void)timeout_seconds;
    return -1;
}
#endif
