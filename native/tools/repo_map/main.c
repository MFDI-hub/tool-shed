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
    size_t max_tokens;
    size_t max_files;
    const char *out_path;
    Arena *arena;
} Config;

static void usage(FILE *fp) {
    fprintf(fp,
            "Usage: repo_map [paths ...] [options]\n"
            "\n"
            "Budgeted symbol index for AI navigation. Not a paste (use skeleton_dump)\n"
            "and not a full dump (use sync_chosen_files).\n"
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
            "  --seed PATH           Boost this file and its neighbors (repeatable)\n"
            "  --max-tokens N        Stop around this many tokens (default: 2048)\n"
            "  --max-files N         Cap files in the map\n"
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
    strlist_init(&cfg->seeds, a);
    cfg->all_exts = 1;
    cfg->use_gitignore = 1;
    cfg->hidden = 1;
    cfg->max_tokens = 2048;
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

static size_t parse_size(const char *s) {
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    return (size_t)v;
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
        if (uvs_flag_eq(arg, "--seed")) {
            strlist_push(&cfg->seeds, uvs_consume_value(&i, argc, argv, "--seed"));
            continue;
        }
        if (uvs_flag_eq(arg, "--max-tokens")) {
            cfg->max_tokens = parse_size(uvs_consume_value(&i, argc, argv, "--max-tokens"));
            continue;
        }
        if (uvs_flag_eq(arg, "--max-files")) {
            cfg->max_files = parse_size(uvs_consume_value(&i, argc, argv, "--max-files"));
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

static size_t est_tokens(size_t bytes) {
    return (bytes + 3) / 4;
}

typedef struct {
    size_t idx;
    int boost;
    size_t in_degree;
    const char *rel;
} Ranked;

static int cmp_ranked(const void *a, const void *b) {
    const Ranked *ra = a;
    const Ranked *rb = b;
    if (ra->boost != rb->boost) {
        return rb->boost - ra->boost;
    }
    if (ra->in_degree != rb->in_degree) {
        return ra->in_degree < rb->in_degree ? 1 : -1;
    }
    return strcmp(ra->rel, rb->rel);
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

    int *boost = arena_alloc(a, (g.len ? g.len : 1) * sizeof(*boost));
    memset(boost, 0, (g.len ? g.len : 1) * sizeof(*boost));
    for (size_t s = 0; s < cfg.seeds.len; s++) {
        int idx = uvs_codegraph_index(&g, cfg.seeds.items[s]);
        if (idx < 0) {
            continue;
        }
        boost[idx] = 2;
        UvsCgNode *n = &g.nodes[idx];
        for (size_t i = 0; i < n->n_importees; i++) {
            if (!boost[n->importees[i]]) {
                boost[n->importees[i]] = 1;
            }
        }
        for (size_t i = 0; i < n->n_importers; i++) {
            if (!boost[n->importers[i]]) {
                boost[n->importers[i]] = 1;
            }
        }
    }

    Ranked *ord = arena_alloc(a, (g.len ? g.len : 1) * sizeof(*ord));
    for (size_t i = 0; i < g.len; i++) {
        ord[i].idx = i;
        ord[i].boost = boost[i];
        ord[i].in_degree = g.nodes[i].n_importers;
        ord[i].rel = g.nodes[i].rel;
    }
    if (g.len > 1) {
        qsort(ord, g.len, sizeof(*ord), cmp_ranked);
    }

    Buf body;
    buf_init(&body, a);
    size_t printed = 0;
    const char *hdr0 = "# repo map\n# files: 0  tokens: ~0\n";
    size_t hdr_pad = strlen(hdr0) + 16;
    for (size_t i = 0; i < g.len; i++) {
        if (cfg.max_files && printed >= cfg.max_files) {
            break;
        }
        UvsCgNode *n = &g.nodes[ord[i].idx];
        Buf block;
        buf_init(&block, a);
        buf_append_fmt(&block, "\n%s:\n", n->rel);
        for (size_t s = 0; s < n->sigs.len; s++) {
            buf_append_fmt(&block, "  %s\n", n->sigs.items[s]);
        }
        size_t next = hdr_pad + body.len + block.len;
        if (cfg.max_tokens && est_tokens(next) > cfg.max_tokens && printed > 0) {
            break;
        }
        if (cfg.max_tokens == 0) {
            break;
        }
        buf_append(&body, block.data ? block.data : "", block.len);
        printed++;
    }

    Buf out;
    buf_init(&out, a);
    buf_append_str(&out, "# repo map\n");
    buf_append_fmt(&out, "# files: %zu  tokens: ~%zu\n", printed, (size_t)0);
    buf_append(&out, body.data ? body.data : "", body.len);
    buf_null_terminate(&out);
    size_t tok = est_tokens(out.len);
    Buf final;
    buf_init(&final, a);
    buf_append_str(&final, "# repo map\n");
    buf_append_fmt(&final, "# files: %zu  tokens: ~%zu\n", printed, tok);
    buf_append(&final, body.data ? body.data : "", body.len);
    buf_null_terminate(&final);

    int ok = write_out(cfg.out_path, final.data ? final.data : "", final.len);
    arena_free(a);
    return ok ? 0 : 1;
}
