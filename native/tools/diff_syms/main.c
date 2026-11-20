#include "uvs.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    StrList roots;
    StrList exts;
    int all_exts;
    StrList include;
    StrList exclude;
    StrList include_gitignored;
    StrList ignore_files;
    int use_gitignore;
    int hidden;
    int follow_symlinks;
    const char *rev;
    const char *out_path;
    Arena *arena;
} Config;

static void usage(FILE *fp) {
    fprintf(fp,
            "Usage: diff_syms [paths ...] [options]\n"
            "\n"
            "Symbols gained, lost, or kept vs a git revision. Not a dump and not\n"
            "related --from-git (that lists files, not names).\n"
            "\n"
            "  --dir PATH            Root to scan (repeatable; default: .)\n"
            "  --ext EXTS            Suffix filter. Example: py,c,ts\n"
            "  --include GLOB        Must match at least one (repeatable)\n"
            "  --exclude GLOB        Skip matches (repeatable)\n"
            "  --include-gitignored GLOB\n"
            "  --ignore-file PATH    Extra gitignore-style rules\n"
            "  --hidden / --no-hidden\n"
            "  --follow-symlinks\n"
            "  --no-gitignore\n"
            "  --rev REV             Compare against this revision (default: HEAD)\n"
            "  --out PATH            File or - for stdout (default: -)\n"
            "  -h, --help\n");
}

static void cfg_init(Arena *a, Config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->arena = a;
    strlist_init(&cfg->roots, a);
    strlist_init(&cfg->exts, a);
    strlist_init(&cfg->include, a);
    strlist_init(&cfg->exclude, a);
    strlist_init(&cfg->include_gitignored, a);
    strlist_init(&cfg->ignore_files, a);
    cfg->all_exts = 1;
    cfg->use_gitignore = 1;
    cfg->hidden = 1;
    cfg->rev = "HEAD";
    cfg->out_path = "-";
}

static void apply_ext_spec(Arena *a, Config *cfg, const char *spec) {
    strlist_init(&cfg->exts, a);
    uvs_parse_exts(a, &cfg->exts, spec);
    cfg->all_exts = 0;
    if (cfg->exts.len == 1 && (strcmp(cfg->exts.items[0], "*") == 0 || strcmp(cfg->exts.items[0], "all") == 0)) {
        cfg->all_exts = 1;
    }
    if (cfg->exts.len == 0) {
        cfg->all_exts = 1;
    }
}

static void parse_args(Arena *a, Config *cfg, int argc, char **argv) {
    cfg_init(a, cfg);
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            exit(0);
        }
        if (strcmp(arg, "--no-gitignore") == 0) {
            cfg->use_gitignore = 0;
            continue;
        }
        if (strcmp(arg, "--hidden") == 0) {
            cfg->hidden = 1;
            continue;
        }
        if (strcmp(arg, "--no-hidden") == 0) {
            cfg->hidden = 0;
            continue;
        }
        if (strcmp(arg, "--follow-symlinks") == 0) {
            cfg->follow_symlinks = 1;
            continue;
        }
        if (uvs_flag_eq(arg, "--dir")) {
            strlist_push(&cfg->roots, path_absolute(a, uvs_consume_value(&i, argc, argv, "--dir")));
            continue;
        }
        if (uvs_flag_eq(arg, "--ext")) {
            apply_ext_spec(a, cfg, uvs_consume_value(&i, argc, argv, "--ext"));
            continue;
        }
        if (uvs_flag_eq(arg, "--include")) {
            uvs_parse_csv(a, &cfg->include, uvs_consume_value(&i, argc, argv, "--include"));
            continue;
        }
        if (uvs_flag_eq(arg, "--exclude")) {
            uvs_parse_csv(a, &cfg->exclude, uvs_consume_value(&i, argc, argv, "--exclude"));
            continue;
        }
        if (uvs_flag_eq(arg, "--include-gitignored")) {
            uvs_parse_csv(a, &cfg->include_gitignored, uvs_consume_value(&i, argc, argv, "--include-gitignored"));
            continue;
        }
        if (uvs_flag_eq(arg, "--ignore-file")) {
            strlist_push(&cfg->ignore_files, path_absolute(a, uvs_consume_value(&i, argc, argv, "--ignore-file")));
            continue;
        }
        if (uvs_flag_eq(arg, "--rev")) {
            cfg->rev = uvs_consume_value(&i, argc, argv, "--rev");
            continue;
        }
        if (uvs_flag_eq(arg, "--out")) {
            cfg->out_path = uvs_consume_value(&i, argc, argv, "--out");
            continue;
        }
        if (arg[0] == '-') {
            fprintf(stderr, "error: unknown option %s\n", arg);
            exit(2);
        }
        strlist_push(&cfg->roots, path_absolute(a, arg));
    }
    if (cfg->roots.len == 0) {
        strlist_push(&cfg->roots, path_cwd(a));
    }
}

static int write_out(const char *path, const char *data, size_t len) {
    if (!path || strcmp(path, "-") == 0) {
        fwrite(data, 1, len, stdout);
        return 1;
    }
    if (!fs_mkdirs_parent(path) || !fs_write_all(path, data, len)) {
        fprintf(stderr, "error: could not write %s\n", path);
        return 0;
    }
    return 1;
}

static void push_git_names(Arena *a, StrList *list, const char *cmd) {
    char *out = NULL;
    if (!uvs_run_capture(a, cmd, &out, NULL) || !out) {
        return;
    }
    char *p = out;
    while (*p) {
        char *eol = strchr(p, '\n');
        size_t n = eol ? (size_t)(eol - p) : strlen(p);
        while (n > 0 && (p[n - 1] == '\r' || p[n - 1] == ' ')) {
            n--;
        }
        if (n) {
            strlist_push(list, uvs_rel_slash(a, arena_strndup(a, p, n)));
        }
        p = eol ? eol + 1 : p + n;
    }
}

static const UvsFileInfo *find_file(const UvsFileList *files, const char *rel) {
    for (size_t i = 0; i < files->len; i++) {
        if (path_equal(files->items[i].rel, rel) || strcmp(files->items[i].rel, rel) == 0) {
            return &files->items[i];
        }
    }
    return NULL;
}

static int list_has(const StrList *list, const char *s) {
    for (size_t i = 0; i < list->len; i++) {
        if (strcmp(list->items[i], s) == 0) {
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    char **utf8_argv = uvs_cmdline_argv(&argc);
    if (utf8_argv) {
        argv = utf8_argv;
    }
    Arena *a = arena_new();
    Config cfg;
    parse_args(a, &cfg, argc, argv);

    UvsDiscoverOpts opts;
    uvs_discover_opts_init(&opts);
    opts.roots = &cfg.roots;
    opts.exts = &cfg.exts;
    opts.all_exts = cfg.all_exts;
    opts.include = &cfg.include;
    opts.exclude = &cfg.exclude;
    opts.include_gitignored = &cfg.include_gitignored;
    opts.ignore_files = &cfg.ignore_files;
    opts.use_gitignore = cfg.use_gitignore;
    opts.hidden = cfg.hidden;
    opts.follow_symlinks = cfg.follow_symlinks;
    opts.skip_secrets = 0;
    opts.skip_non_text = 1;

    UvsFileList files;
    uvs_discover(a, &opts, NULL, NULL, &files);
    for (size_t i = 0; i < files.len; i++) {
        files.items[i].rel = uvs_rel_slash(a, files.items[i].rel);
    }

    const char *rev = cfg.rev && cfg.rev[0] ? cfg.rev : "HEAD";
    StrList changed;
    strlist_init(&changed, a);
    char *diff_cmd = arena_sprintf(a, "git diff --name-only --diff-filter=ACMR %s", rev);
    push_git_names(a, &changed, diff_cmd);
    if (strcmp(rev, "HEAD") == 0) {
        push_git_names(a, &changed, "git ls-files --others --exclude-standard");
    }

    Buf buf;
    buf_init(&buf, a);
    buf_append_str(&buf, "# diff_syms\n");
    buf_append_fmt(&buf, "# rev: %s\n", rev);

    for (size_t i = 0; i < changed.len; i++) {
        const char *rel = changed.items[i];
        const UvsFileInfo *fi = find_file(&files, rel);
        if (!fi) {
            continue;
        }
        size_t now_len = 0;
        char *now_src = fs_read_all(a, fi->abs, &now_len);
        StrList now;
        strlist_init(&now, a);
        if (now_src) {
            uvs_sigs_from_src(a, now_src, now_len, &now);
        }
        StrList old;
        strlist_init(&old, a);
        char *show = NULL;
        char *show_cmd = arena_sprintf(a, "git show %s:%s", rev, rel);
        if (uvs_run_capture(a, show_cmd, &show, NULL) && show && show[0]) {
            uvs_sigs_from_src(a, show, strlen(show), &old);
        }

        int any = 0;
        Buf block;
        buf_init(&block, a);
        buf_append_fmt(&block, "\n%s:\n", rel);
        for (size_t s = 0; s < now.len; s++) {
            if (list_has(&old, now.items[s])) {
                buf_append_fmt(&block, "  = %s\n", now.items[s]);
            } else {
                buf_append_fmt(&block, "  + %s\n", now.items[s]);
                any = 1;
            }
        }
        for (size_t s = 0; s < old.len; s++) {
            if (!list_has(&now, old.items[s])) {
                buf_append_fmt(&block, "  - %s\n", old.items[s]);
                any = 1;
            }
        }
        if (any) {
            buf_append(&buf, block.data ? block.data : "", block.len);
        }
    }
    buf_null_terminate(&buf);
    int ok = write_out(cfg.out_path, buf.data ? buf.data : "", buf.len);
    arena_free(a);
    return ok ? 0 : 1;
}
