#ifndef UVS_H
#define UVS_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef _WIN32
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define UVS_IS_WINDOWS 1
#else
#define UVS_IS_WINDOWS 0
#endif

#define UVS_TEXT_PROBE 8192
#if UVS_IS_WINDOWS
#define UVS_PATH_MAX 32768
#else
#define UVS_PATH_MAX 4096
#endif

typedef struct Arena Arena;

Arena *arena_new(void);
void arena_reset(Arena *a);
void arena_free(Arena *a);
void *arena_alloc(Arena *a, size_t n);
char *arena_strdup(Arena *a, const char *s);
char *arena_strndup(Arena *a, const char *s, size_t n);
char *arena_sprintf(Arena *a, const char *fmt, ...);

typedef struct {
    const char **items;
    size_t len;
    size_t cap;
    Arena *arena;
} StrList;

void strlist_init(StrList *list, Arena *arena);
void strlist_push(StrList *list, const char *s);
void strlist_sort(StrList *list);
int strlist_contains(const StrList *list, const char *s);
void strlist_copy(Arena *a, StrList *dst, const StrList *src);
void uvs_parse_csv(Arena *a, StrList *list, const char *spec);
void uvs_parse_exts(Arena *a, StrList *exts, const char *spec);
int uvs_flag_eq(const char *arg, const char *name);
const char *uvs_consume_value(int *i, int argc, char **argv, const char *opt);

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    Arena *arena;
} Buf;

void buf_init(Buf *buf, Arena *arena);
void buf_append(Buf *buf, const char *s, size_t n);
void buf_append_str(Buf *buf, const char *s);
void buf_append_char(Buf *buf, char c);
void buf_append_fmt(Buf *buf, const char *fmt, ...);
void buf_null_terminate(Buf *buf);

int uvs_utf8_valid(const char *s, size_t n);
int uvs_has_nul(const char *s, size_t n, size_t probe);
int uvs_is_non_text(const char *s, size_t n);

char **uvs_cmdline_argv(int *argc);

char *path_cwd(Arena *a);
int path_is_absolute(const char *path);
char *path_normalize(Arena *a, const char *path);
char *path_join(Arena *a, const char *left, const char *right);
char *path_absolute(Arena *a, const char *path);
char *path_dirname(Arena *a, const char *path);
const char *path_basename(const char *path);
char *path_relative(Arena *a, const char *path, const char *root);
int path_has_component(const char *path, const char *name);
int path_has_hidden_component(const char *path);
int path_equal(const char *a, const char *b);
int path_is_within(const char *path, const char *root);
char *path_canonical(Arena *a, const char *path);

typedef struct {
    int64_t mtime_ns;
    int64_t size;
    int exists;
    int is_dir;
    int is_file;
} FsStat;

int fs_stat(const char *path, FsStat *out);
int fs_exists(const char *path);
int fs_is_file(const char *path);
int fs_is_dir(const char *path);
int fs_mkdirs(const char *dir);
int fs_mkdirs_parent(const char *file_path);
char *fs_read_all(Arena *a, const char *path, size_t *len_out);
int fs_write_all(const char *path, const char *data, size_t len);
int fs_append_all(const char *path, const char *data, size_t len);

/* WalkCb return: 0 continue (and descend into directories), 1 skip this
 * directory (do not descend), <0 abort the walk. */
typedef int (*WalkCb)(const char *path, int is_dir, void *ud);
int fs_walk(const char *root, WalkCb cb, void *ud);
int fs_walk_ex(const char *root, WalkCb cb, void *ud, int follow_symlinks);

typedef struct IgnoreSet IgnoreSet;

IgnoreSet *ignore_new(Arena *a);
IgnoreSet *ignore_new_manual(Arena *a);
void ignore_load_for_path(IgnoreSet *set, const char *abs_path);
void ignore_load_file(IgnoreSet *set, const char *path, const char *base);
/* force_root: an explicit scan directory; gitignore rules that exclude this
 * directory (or its ancestors) do not hide its contents. Nested rules still apply.
 * Pass NULL for normal git behavior. */
int ignore_is_ignored(IgnoreSet *set, const char *abs_path, int is_dir, const char *force_root);

int uvs_glob_match(const char *pattern, const char *text);
int uvs_glob_path_match(const char *pattern, const char *rel);
int uvs_glob_list_match(const StrList *list, const char *rel);
int uvs_ext_matches(const StrList *exts, int all_exts, const char *path);

typedef struct {
    const char *abs;
    const char *rel;
    int64_t mtime_ns;
    int64_t size;
    int explicit_file;
} UvsFileInfo;

typedef struct {
    UvsFileInfo *items;
    size_t len;
    size_t cap;
    Arena *arena;
} UvsFileList;

void uvs_filelist_init(UvsFileList *list, Arena *arena);
void uvs_filelist_push(UvsFileList *list, UvsFileInfo info);
int uvs_filelist_changed(const UvsFileList *a, const UvsFileList *b);

typedef int (*UvsKeepFn)(const UvsFileInfo *file, void *ud, const char **reason);

typedef struct {
    const StrList *roots;
    const StrList *exts;
    int all_exts;
    const StrList *include;
    const StrList *exclude;
    const StrList *include_gitignored;
    const StrList *ignore_files;
    const StrList *skip_abs;
    int use_gitignore;
    int hidden;
    int follow_symlinks;
    int skip_secrets;
    int skip_non_text;
    int log_skips;
    int list_hits;
    size_t max_files;
    const char *cwd;
} UvsDiscoverOpts;

void uvs_discover_opts_init(UvsDiscoverOpts *opts);
int uvs_is_secret_name(const char *path);
void uvs_discover(Arena *a, const UvsDiscoverOpts *opts, UvsKeepFn keep, void *ud, UvsFileList *out);

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    unsigned char data[64];
    size_t datalen;
} UvsSha256;

void uvs_sha256_init(UvsSha256 *ctx);
void uvs_sha256_update(UvsSha256 *ctx, const void *data, size_t len);
void uvs_sha256_final(UvsSha256 *ctx, unsigned char out[32]);
void uvs_sha256_hex(const unsigned char digest[32], char out[65]);

int uvs_clipboard_set(const char *utf8);
/* Wait up to timeout_seconds for a filesystem change under paths.
 * Returns 1 on change, 0 on timeout, -1 if native watch is unavailable. */
int uvs_watch_wait(const char **paths, size_t npaths, double timeout_seconds);

void uvs_sleep_seconds(double seconds);
int uvs_run_capture(Arena *a, const char *cmd, char **out, size_t *len_out);
int uvs_run_argv(char **argv);
/* Run argv, merge stdout+stderr into *out. Returns the process exit code (127 if spawn failed). */
int uvs_run_capture_argv(Arena *a, char **argv, char **out, size_t *len_out);

typedef struct {
    const char *rel;
    const char *abs;
    StrList sigs;
    size_t *importees;
    size_t n_importees;
    size_t *importers;
    size_t n_importers;
} UvsCgNode;

typedef struct {
    UvsCgNode *nodes;
    size_t len;
    Arena *arena;
} UvsCodeGraph;

void uvs_codegraph_build(Arena *a, const UvsFileList *files, UvsCodeGraph *out);
int uvs_codegraph_index(const UvsCodeGraph *g, const char *rel_or_path);
char *uvs_rel_slash(Arena *a, const char *rel);
char *uvs_sig_text(Arena *a, const char *stripped);
void uvs_sigs_from_src(Arena *a, const char *src, size_t n, StrList *out);

#endif
