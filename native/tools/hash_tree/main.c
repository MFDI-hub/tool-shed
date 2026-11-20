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
    const char *out_path;
    Arena *arena;
} Config;

static void usage(FILE *fp) {
    fprintf(fp,
            "Usage: hash_tree [paths ...] [options]\n"
            "\n"
            "One content hash of the walked tree (change detection, not a paste).\n"
            "\n"
            "  --dir PATH            Root to scan (repeatable; default: .)\n"
            "  --ext EXTS            Suffix filter. Example: py,ts,md\n"
            "  --include GLOB        Must match at least one (repeatable)\n"
            "  --exclude GLOB        Skip matches (repeatable)\n"
            "  --include-gitignored GLOB\n"
            "  --ignore-file PATH    Extra gitignore-style rules\n"
            "  --hidden / --no-hidden\n"
            "  --follow-symlinks\n"
            "  --no-gitignore\n"
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

static void rel_forward(char *s) {
    for (; *s; s++) {
        if (*s == '\\') {
            *s = '/';
        }
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
    opts.skip_non_text = 0;

    UvsFileList files;
    uvs_discover(a, &opts, NULL, NULL, &files);

    UvsSha256 ctx;
    uvs_sha256_init(&ctx);
    for (size_t i = 0; i < files.len; i++) {
        char *rel = arena_strdup(a, files.items[i].rel);
        rel_forward(rel);
        uvs_sha256_update(&ctx, rel, strlen(rel));
        uvs_sha256_update(&ctx, "\0", 1);
        size_t len = 0;
        char *body = fs_read_all(a, files.items[i].abs, &len);
        if (body) {
            uvs_sha256_update(&ctx, body, len);
        }
        uvs_sha256_update(&ctx, "\0", 1);
    }
    unsigned char digest[32];
    uvs_sha256_final(&ctx, digest);
    char hex[65];
    uvs_sha256_hex(digest, hex);
    char line[66];
    memcpy(line, hex, 64);
    line[64] = '\n';
    line[65] = 0;
    int ok = write_out(cfg.out_path, line, 65);
    arena_free(a);
    return ok ? 0 : 1;
}
