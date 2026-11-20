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
    StrList seeds;
    int use_gitignore;
    int hidden;
    int follow_symlinks;
    int from_git;
    const char *git_rev;
    int depth;
    int dir_imports;
    int dir_importers;
    const char *out_path;
    Arena *arena;
} Config;

static void usage(FILE *fp) {
    fprintf(fp,
            "Usage: related [paths ...] [options]\n"
            "\n"
            "Import cone around seed files. --from-git is impact (changed files plus\n"
            "neighbors). Paths only — not a dump or skeleton.\n"
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
            "  --file PATH           Seed file (repeatable)\n"
            "  --from-git [REV]      Seeds from git diff vs REV (default HEAD)\n"
            "  --depth N             Hops (default: 1)\n"
            "  --direction both|imports|importers\n"
            "  --out PATH            File or - for stdout (default: -)\n"
            "  -h, --help\n");
}

static const char *consume_optional(int *i, int argc, char **argv, const char *fallback) {
    if (*i + 1 < argc && argv[*i + 1][0] != '-') {
        return argv[++(*i)];
    }
    return fallback;
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
    strlist_init(&cfg->seeds, a);
    cfg->all_exts = 1;
    cfg->use_gitignore = 1;
    cfg->hidden = 1;
    cfg->depth = 1;
    cfg->dir_imports = 1;
    cfg->dir_importers = 1;
    cfg->git_rev = "HEAD";
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

static void parse_direction(Config *cfg, const char *s) {
    if (strcmp(s, "both") == 0) {
        cfg->dir_imports = 1;
        cfg->dir_importers = 1;
        return;
    }
    if (strcmp(s, "imports") == 0) {
        cfg->dir_imports = 1;
        cfg->dir_importers = 0;
        return;
    }
    if (strcmp(s, "importers") == 0) {
        cfg->dir_imports = 0;
        cfg->dir_importers = 1;
        return;
    }
    fprintf(stderr, "error: unknown --direction %s (both, imports, importers)\n", s);
    exit(2);
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
        if (uvs_flag_eq(arg, "--file")) {
            strlist_push(&cfg->seeds, uvs_consume_value(&i, argc, argv, "--file"));
            continue;
        }
        if (uvs_flag_eq(arg, "--from-git")) {
            cfg->from_git = 1;
            char *eq = strchr(argv[i], '=');
            if (eq && eq[1]) {
                cfg->git_rev = eq + 1;
            } else {
                cfg->git_rev = consume_optional(&i, argc, argv, "HEAD");
            }
            continue;
        }
        if (uvs_flag_eq(arg, "--depth")) {
            cfg->depth = atoi(uvs_consume_value(&i, argc, argv, "--depth"));
            if (cfg->depth < 0) {
                cfg->depth = 0;
            }
            continue;
        }
        if (uvs_flag_eq(arg, "--direction")) {
            parse_direction(cfg, uvs_consume_value(&i, argc, argv, "--direction"));
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
        char *abs = path_absolute(a, arg);
        if (fs_is_file(abs)) {
            strlist_push(&cfg->seeds, arg);
            strlist_push(&cfg->roots, abs);
        } else {
            strlist_push(&cfg->roots, abs);
        }
    }
    if (cfg->roots.len == 0) {
        strlist_push(&cfg->roots, path_cwd(a));
    }
}

static void push_git_names(Arena *a, StrList *seeds, const char *cmd) {
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
            strlist_push(seeds, uvs_rel_slash(a, arena_strndup(a, p, n)));
        }
        p = eol ? eol + 1 : p + n;
    }
}

static void load_git_seeds(Arena *a, Config *cfg) {
    if (!cfg->from_git) {
        return;
    }
    const char *rev = cfg->git_rev && cfg->git_rev[0] ? cfg->git_rev : "HEAD";
    char *cmd = arena_sprintf(a, "git diff --name-only --diff-filter=ACMR %s", rev);
    push_git_names(a, &cfg->seeds, cmd);
    if (strcmp(rev, "HEAD") == 0) {
        push_git_names(a, &cfg->seeds, "git ls-files --others --exclude-standard");
    }
}

static char *file_stem(Arena *a, const char *rel) {
    const char *base = path_basename(rel);
    const char *dot = strrchr(base, '.');
    if (!dot || dot == base) {
        return arena_strdup(a, base);
    }
    return arena_strndup(a, base, (size_t)(dot - base));
}

static int looks_like_test_for(Arena *a, const char *rel, const char *stem) {
    const char *base = path_basename(rel);
    char *b = arena_strdup(a, base);
    for (char *p = b; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') {
            *p = (char)(*p - 'A' + 'a');
        }
    }
    char *st = arena_strdup(a, stem);
    for (char *p = st; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') {
            *p = (char)(*p - 'A' + 'a');
        }
    }
    if (strncmp(b, "test_", 5) == 0 && strncmp(b + 5, st, strlen(st)) == 0) {
        return 1;
    }
    size_t sl = strlen(st);
    if (strncmp(b, st, sl) == 0 && (strncmp(b + sl, "_test.", 6) == 0 || strncmp(b + sl, ".test.", 6) == 0 ||
                                    strncmp(b + sl, ".spec.", 6) == 0)) {
        return 1;
    }
    return 0;
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

int main(int argc, char **argv) {
    char **utf8_argv = uvs_cmdline_argv(&argc);
    if (utf8_argv) {
        argv = utf8_argv;
    }
    Arena *a = arena_new();
    Config cfg;
    parse_args(a, &cfg, argc, argv);
    load_git_seeds(a, &cfg);
    if (cfg.seeds.len == 0 && !cfg.from_git) {
        fprintf(stderr, "error: need --file, a positional file, or --from-git\n");
        arena_free(a);
        return 2;
    }

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

    UvsCodeGraph g;
    uvs_codegraph_build(a, &files, &g);

    unsigned char *mark = arena_alloc(a, g.len ? g.len : 1);
    memset(mark, 0, g.len ? g.len : 1);
    size_t *frontier = arena_alloc(a, (g.len ? g.len : 1) * sizeof(*frontier));
    size_t flen = 0;
    size_t seed_hits = 0;
    for (size_t s = 0; s < cfg.seeds.len; s++) {
        int idx = uvs_codegraph_index(&g, cfg.seeds.items[s]);
        if (idx < 0) {
            continue;
        }
        if (!mark[idx]) {
            mark[idx] = 1;
            frontier[flen++] = (size_t)idx;
            seed_hits++;
        }
    }

    size_t start = 0;
    size_t end = flen;
    for (int d = 0; d < cfg.depth; d++) {
        size_t next_end = end;
        for (size_t i = start; i < end; i++) {
            UvsCgNode *n = &g.nodes[frontier[i]];
            if (cfg.dir_imports) {
                for (size_t k = 0; k < n->n_importees; k++) {
                    size_t t = n->importees[k];
                    if (!mark[t]) {
                        mark[t] = 1;
                        frontier[next_end++] = t;
                    }
                }
            }
            if (cfg.dir_importers) {
                for (size_t k = 0; k < n->n_importers; k++) {
                    size_t t = n->importers[k];
                    if (!mark[t]) {
                        mark[t] = 1;
                        frontier[next_end++] = t;
                    }
                }
            }
        }
        start = end;
        end = next_end;
        flen = next_end;
    }

    for (size_t i = 0; i < g.len; i++) {
        if (!mark[i]) {
            continue;
        }
        char *stem = file_stem(a, g.nodes[i].rel);
        for (size_t j = 0; j < g.len; j++) {
            if (mark[j]) {
                continue;
            }
            if (looks_like_test_for(a, g.nodes[j].rel, stem)) {
                mark[j] = 1;
            }
        }
    }

    Buf buf;
    buf_init(&buf, a);
    buf_append_fmt(&buf, "# related\n# seeds: %zu\n", seed_hits);
    for (size_t i = 0; i < g.len; i++) {
        if (mark[i]) {
            buf_append_str(&buf, g.nodes[i].rel);
            buf_append_char(&buf, '\n');
        }
    }
    buf_null_terminate(&buf);
    int ok = write_out(cfg.out_path, buf.data ? buf.data : "", buf.len);
    arena_free(a);
    return ok ? 0 : 1;
}
