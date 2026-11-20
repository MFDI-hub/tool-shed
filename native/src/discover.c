#include "uvs.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    Arena *arena;
    const UvsDiscoverOpts *opts;
    IgnoreSet *git_ignore;
    IgnoreSet *extra_ignore;
    UvsFileList *files;
    const char *cwd;
    const char *force_root;
    UvsKeepFn keep;
    void *keep_ud;
} DiscoverCtx;

int uvs_flag_eq(const char *arg, const char *name) {
    if (strcmp(arg, name) == 0) {
        return 1;
    }
    size_t n = strlen(name);
    return strncmp(arg, name, n) == 0 && arg[n] == '=';
}

const char *uvs_consume_value(int *i, int argc, char **argv, const char *opt) {
    char *eq = strchr(argv[*i], '=');
    if (eq && eq[1]) {
        return eq + 1;
    }
    if (*i + 1 >= argc) {
        fprintf(stderr, "error: %s requires a value\n", opt);
        exit(2);
    }
    return argv[++(*i)];
}

void uvs_parse_csv(Arena *a, StrList *list, const char *spec) {
    const char *p = spec;
    while (*p) {
        while (*p == ',' || *p == ' ') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *start = p;
        while (*p && *p != ',') {
            p++;
        }
        size_t n = (size_t)(p - start);
        while (n > 0 && (start[n - 1] == ' ' || start[n - 1] == '\t')) {
            n--;
        }
        if (n) {
            strlist_push(list, arena_strndup(a, start, n));
        }
    }
}

void uvs_parse_exts(Arena *a, StrList *exts, const char *spec) {
    const char *p = spec;
    while (*p) {
        while (*p == ',' || *p == ' ') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *start = p;
        while (*p && *p != ',') {
            p++;
        }
        while (start < p && *start == '.') {
            start++;
        }
        size_t n = (size_t)(p - start);
        while (n > 0 && (start[n - 1] == ' ' || start[n - 1] == '\t')) {
            n--;
        }
        if (n) {
            strlist_push(exts, arena_strndup(a, start, n));
        }
    }
}

int uvs_glob_list_match(const StrList *list, const char *rel) {
    if (!list) {
        return 0;
    }
    for (size_t i = 0; i < list->len; i++) {
        if (uvs_glob_path_match(list->items[i], rel)) {
            return 1;
        }
    }
    return 0;
}

static int ascii_fold_eq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;
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
}

static int ascii_fold_starts(const char *s, const char *pfx) {
    for (; *pfx; s++, pfx++) {
        if (!*s) {
            return 0;
        }
        unsigned char ca = (unsigned char)*s;
        unsigned char cb = (unsigned char)*pfx;
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
    return 1;
}

int uvs_ext_matches(const StrList *exts, int all_exts, const char *path) {
    if (all_exts || !exts || exts->len == 0) {
        return 1;
    }
    const char *base = path_basename(path);
    const char *dot = strrchr(base, '.');
    if (!dot || !dot[1] || dot == base) {
        return 0;
    }
    const char *ext = dot + 1;
    for (size_t i = 0; i < exts->len; i++) {
        if (ascii_fold_eq(ext, exts->items[i])) {
            return 1;
        }
    }
    return 0;
}

int uvs_is_secret_name(const char *path) {
    const char *base = path_basename(path);
    if (ascii_fold_eq(base, ".env") || ascii_fold_starts(base, ".env.")) {
        return 1;
    }
    if (ascii_fold_eq(base, "credentials.json") || ascii_fold_eq(base, "secrets.json")) {
        return 1;
    }
    if (ascii_fold_eq(base, ".netrc")) {
        return 1;
    }
    if (ascii_fold_eq(base, "id_rsa") || ascii_fold_eq(base, "id_dsa") || ascii_fold_eq(base, "id_ecdsa") ||
        ascii_fold_eq(base, "id_ed25519")) {
        return 1;
    }
    const char *dot = strrchr(base, '.');
    if (dot && dot != base && dot[1]) {
        const char *ext = dot + 1;
        if (ascii_fold_eq(ext, "pem") || ascii_fold_eq(ext, "key") || ascii_fold_eq(ext, "p12") ||
            ascii_fold_eq(ext, "pfx") || ascii_fold_eq(ext, "p8")) {
            return 1;
        }
    }
    return 0;
}

void uvs_filelist_init(UvsFileList *list, Arena *arena) {
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
    list->arena = arena;
}

void uvs_filelist_push(UvsFileList *list, UvsFileInfo info) {
    if (list->len + 1 > list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 16;
        UvsFileInfo *items = arena_alloc(list->arena, cap * sizeof(*items));
        if (list->items) {
            memcpy(items, list->items, list->len * sizeof(*items));
        }
        list->items = items;
        list->cap = cap;
    }
    list->items[list->len++] = info;
}

int uvs_filelist_changed(const UvsFileList *a, const UvsFileList *b) {
    if (a->len != b->len) {
        return 1;
    }
    for (size_t i = 0; i < a->len; i++) {
        if (!path_equal(a->items[i].abs, b->items[i].abs) || a->items[i].mtime_ns != b->items[i].mtime_ns ||
            a->items[i].size != b->items[i].size) {
            return 1;
        }
    }
    return 0;
}

void uvs_discover_opts_init(UvsDiscoverOpts *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->use_gitignore = 1;
    opts->hidden = 1;
    opts->skip_non_text = 1;
    opts->all_exts = 1;
}

static int cmp_fileinfo(const void *a, const void *b) {
    const UvsFileInfo *fa = a;
    const UvsFileInfo *fb = b;
    return strcmp(fa->rel, fb->rel);
}

static void skip_log(const DiscoverCtx *ctx, const char *rel, const char *reason) {
    if (ctx->opts->log_skips) {
        fprintf(stdout, "- %s  %s\n", rel, reason);
    }
}

static int already_have(const UvsFileList *files, const char *abs) {
    for (size_t i = 0; i < files->len; i++) {
        if (path_equal(files->items[i].abs, abs)) {
            return 1;
        }
    }
    return 0;
}

static int is_skipped_abs(const StrList *skip_abs, const char *abs) {
    if (!skip_abs) {
        return 0;
    }
    for (size_t i = 0; i < skip_abs->len; i++) {
        if (path_equal(skip_abs->items[i], abs)) {
            return 1;
        }
    }
    return 0;
}

static int should_include_file(DiscoverCtx *ctx, const char *abs, int explicit_file) {
    char *rel = path_relative(ctx->arena, abs, ctx->cwd);
    if (path_has_component(abs, ".git")) {
        skip_log(ctx, rel, "git-dir");
        return 0;
    }
    if (is_skipped_abs(ctx->opts->skip_abs, abs)) {
        skip_log(ctx, rel, "output");
        return 0;
    }
    if (!ctx->opts->hidden && path_has_hidden_component(rel)) {
        skip_log(ctx, rel, "hidden");
        return 0;
    }
    if (!explicit_file && !uvs_ext_matches(ctx->opts->exts, ctx->opts->all_exts, abs)) {
        skip_log(ctx, rel, "ext");
        return 0;
    }
    if (!explicit_file && ctx->opts->exclude && ctx->opts->exclude->len &&
        uvs_glob_list_match(ctx->opts->exclude, rel)) {
        skip_log(ctx, rel, "exclude");
        return 0;
    }
    if (!explicit_file && ctx->opts->include && ctx->opts->include->len &&
        !uvs_glob_list_match(ctx->opts->include, rel)) {
        skip_log(ctx, rel, "include");
        return 0;
    }
    if (!explicit_file && ctx->opts->use_gitignore) {
        int git_ignored = ignore_is_ignored(ctx->git_ignore, abs, 0, ctx->force_root);
        if (git_ignored && !uvs_glob_list_match(ctx->opts->include_gitignored, rel)) {
            skip_log(ctx, rel, "gitignore");
            return 0;
        }
    }
    if (!explicit_file && ignore_is_ignored(ctx->extra_ignore, abs, 0, ctx->force_root)) {
        skip_log(ctx, rel, "syncignore");
        return 0;
    }
    if (ctx->opts->skip_secrets && uvs_is_secret_name(abs)) {
        skip_log(ctx, rel, "secret");
        return 0;
    }
    FsStat st;
    if (!fs_stat(abs, &st) || !st.is_file) {
        skip_log(ctx, rel, "unreadable");
        return 0;
    }
    UvsFileInfo info;
    info.abs = abs;
    info.rel = rel;
    info.mtime_ns = st.mtime_ns;
    info.size = st.size;
    info.explicit_file = explicit_file;
    if (ctx->keep) {
        const char *reason = "keep";
        if (!ctx->keep(&info, ctx->keep_ud, &reason)) {
            skip_log(ctx, rel, reason ? reason : "keep");
            return 0;
        }
    }
    if (ctx->opts->skip_non_text) {
        size_t len = 0;
        char *body = fs_read_all(ctx->arena, abs, &len);
        if (!body) {
            skip_log(ctx, rel, "unreadable");
            return 0;
        }
        if (uvs_is_non_text(body, len)) {
            fprintf(stderr, "warning: skipping non-text file: %s\n", abs);
            skip_log(ctx, rel, "non-text");
            return 0;
        }
    }
    return 1;
}

static void add_file(DiscoverCtx *ctx, const char *abs, int explicit_file) {
    if (already_have(ctx->files, abs)) {
        return;
    }
    if (ctx->opts->max_files && ctx->files->len >= ctx->opts->max_files) {
        skip_log(ctx, path_relative(ctx->arena, abs, ctx->cwd), "max-files");
        return;
    }
    FsStat st;
    if (!fs_stat(abs, &st) || !st.is_file) {
        return;
    }
    UvsFileInfo info;
    info.abs = arena_strdup(ctx->arena, abs);
    info.rel = path_relative(ctx->arena, abs, ctx->cwd);
    info.mtime_ns = st.mtime_ns;
    info.size = st.size;
    info.explicit_file = explicit_file;
    uvs_filelist_push(ctx->files, info);
    if (ctx->opts->list_hits) {
        fprintf(stdout, "+ %s\n", info.rel);
    }
}

static int walk_cb(const char *path, int is_dir, void *ud) {
    DiscoverCtx *ctx = ud;
    char *rel = path_relative(ctx->arena, path, ctx->cwd);
    if (is_dir) {
        if (path_has_component(path, ".git")) {
            return 1;
        }
        if (!ctx->opts->hidden && path_has_hidden_component(rel) && strcmp(rel, ".") != 0) {
            return 1;
        }
        if (ctx->opts->exclude && ctx->opts->exclude->len && uvs_glob_list_match(ctx->opts->exclude, rel)) {
            skip_log(ctx, rel, "exclude");
            return 1;
        }
        if (ctx->opts->use_gitignore && ignore_is_ignored(ctx->git_ignore, path, 1, ctx->force_root) &&
            !uvs_glob_list_match(ctx->opts->include_gitignored, rel)) {
            skip_log(ctx, rel, "gitignore");
            return 1;
        }
        if (ignore_is_ignored(ctx->extra_ignore, path, 1, ctx->force_root)) {
            skip_log(ctx, rel, "syncignore");
            return 1;
        }
        return 0;
    }
    if (should_include_file(ctx, path, 0)) {
        add_file(ctx, path, 0);
    }
    return 0;
}

static void load_extra_ignores(DiscoverCtx *ctx) {
    char *cwd_si = path_join(ctx->arena, ctx->cwd, ".syncignore");
    if (fs_is_file(cwd_si)) {
        ignore_load_file(ctx->extra_ignore, cwd_si, ctx->cwd);
    }
    if (ctx->opts->roots) {
        for (size_t i = 0; i < ctx->opts->roots->len; i++) {
            const char *root = ctx->opts->roots->items[i];
            if (fs_is_dir(root)) {
                char *si = path_join(ctx->arena, root, ".syncignore");
                if (fs_is_file(si)) {
                    ignore_load_file(ctx->extra_ignore, si, root);
                }
            }
        }
    }
    if (ctx->opts->ignore_files) {
        for (size_t i = 0; i < ctx->opts->ignore_files->len; i++) {
            const char *p = ctx->opts->ignore_files->items[i];
            if (fs_is_file(p)) {
                ignore_load_file(ctx->extra_ignore, p, path_dirname(ctx->arena, p));
            }
        }
    }
}

void uvs_discover(Arena *a, const UvsDiscoverOpts *opts, UvsKeepFn keep, void *ud, UvsFileList *out) {
    DiscoverCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.arena = a;
    ctx.opts = opts;
    ctx.git_ignore = ignore_new(a);
    ctx.extra_ignore = ignore_new_manual(a);
    ctx.files = out;
    ctx.cwd = opts->cwd ? opts->cwd : path_cwd(a);
    ctx.force_root = NULL;
    ctx.keep = keep;
    ctx.keep_ud = ud;
    uvs_filelist_init(out, a);
    if (!opts->roots || opts->roots->len == 0) {
        return;
    }
    load_extra_ignores(&ctx);

    for (size_t i = 0; i < opts->roots->len; i++) {
        const char *root = opts->roots->items[i];
        ignore_load_for_path(ctx.git_ignore, root);
        if (!fs_exists(root)) {
            continue;
        }
        if (fs_is_file(root)) {
            ctx.force_root = NULL;
            if (should_include_file(&ctx, root, 1)) {
                add_file(&ctx, root, 1);
            }
            continue;
        }
        if (!fs_is_dir(root)) {
            continue;
        }
        ctx.force_root = root;
        fs_walk_ex(root, walk_cb, &ctx, opts->follow_symlinks);
        ctx.force_root = NULL;
    }
    if (out->len > 1) {
        qsort(out->items, out->len, sizeof(*out->items), cmp_fileinfo);
    }
}
