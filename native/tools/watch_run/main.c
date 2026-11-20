#include "uvs.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig) {
    (void)sig;
    g_stop = 1;
}

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
    double interval;
    double debounce;
    int once;
    int poll;
    int no_initial;
    char **cmd;
    Arena *arena;
} Config;

static void usage(FILE *fp) {
    fprintf(fp,
            "Usage: watch_run [paths ...] [options] -- command [args...]\n"
            "\n"
            "Run a command when files change. Selection matches the dump walker\n"
            "(gitignore, include/exclude, extensions). Unlike sync_chosen_files,\n"
            "this runs your command instead of rewriting a dump.\n"
            "\n"
            "  --dir PATH            Root to watch (repeatable; default: .)\n"
            "  --ext EXTS            Suffix filter. Example: py,ts,md\n"
            "  --include GLOB        Must match at least one (repeatable)\n"
            "  --exclude GLOB        Skip matches (repeatable)\n"
            "  --include-gitignored GLOB\n"
            "  --ignore-file PATH    Extra gitignore-style rules\n"
            "  --hidden / --no-hidden\n"
            "  --follow-symlinks     Follow directory symlinks\n"
            "  --no-gitignore        Do not apply gitignore\n"
            "  --interval SECONDS    Poll / native-watch timeout (default: 2)\n"
            "  --debounce SECONDS    Quiet period before a rerun (default: 0.25)\n"
            "  --poll                Force polling instead of native watch\n"
            "  --once                Run the command once and exit\n"
            "  --no-initial          Wait for a change before the first run\n"
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
    cfg->interval = 2.0;
    cfg->debounce = 0.25;
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
    int cmd_at = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            cmd_at = i + 1;
            break;
        }
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            exit(0);
        }
        if (strcmp(arg, "--once") == 0) {
            cfg->once = 1;
            continue;
        }
        if (strcmp(arg, "--no-initial") == 0) {
            cfg->no_initial = 1;
            continue;
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
        if (strcmp(arg, "--poll") == 0) {
            cfg->poll = 1;
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
        if (uvs_flag_eq(arg, "--interval")) {
            cfg->interval = strtod(uvs_consume_value(&i, argc, argv, "--interval"), NULL);
            continue;
        }
        if (uvs_flag_eq(arg, "--debounce")) {
            cfg->debounce = strtod(uvs_consume_value(&i, argc, argv, "--debounce"), NULL);
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
        if (arg[0] == '-') {
            fprintf(stderr, "error: unknown option %s\n", arg);
            exit(2);
        }
        strlist_push(&cfg->roots, path_absolute(a, arg));
    }
    if (cmd_at < 0 || cmd_at >= argc) {
        fprintf(stderr, "error: missing command (put it after --)\n");
        usage(stderr);
        exit(2);
    }
    cfg->cmd = argv + cmd_at;
    if (cfg->roots.len == 0) {
        strlist_push(&cfg->roots, path_cwd(a));
    }
}

static void fill_opts(Config *cfg, UvsDiscoverOpts *opts) {
    uvs_discover_opts_init(opts);
    opts->roots = &cfg->roots;
    opts->exts = &cfg->exts;
    opts->all_exts = cfg->all_exts;
    opts->include = &cfg->include;
    opts->exclude = &cfg->exclude;
    opts->include_gitignored = &cfg->include_gitignored;
    opts->ignore_files = &cfg->ignore_files;
    opts->use_gitignore = cfg->use_gitignore;
    opts->hidden = cfg->hidden;
    opts->follow_symlinks = cfg->follow_symlinks;
    opts->skip_secrets = 0;
    opts->skip_non_text = 0;
}

static void snapshot(Arena *a, Config *cfg, UvsFileList *files) {
    UvsDiscoverOpts opts;
    fill_opts(cfg, &opts);
    uvs_discover(a, &opts, NULL, NULL, files);
}

static int run_cmd(Config *cfg) {
    printf("watch_run: running");
    for (char **p = cfg->cmd; *p; p++) {
        printf(" %s", *p);
    }
    printf("\n");
    fflush(stdout);
    return uvs_run_argv(cfg->cmd);
}

static void copy_list(Arena *dst, UvsFileList *out, const UvsFileList *src) {
    uvs_filelist_init(out, dst);
    for (size_t i = 0; i < src->len; i++) {
        UvsFileInfo info = src->items[i];
        info.abs = arena_strdup(dst, src->items[i].abs);
        info.rel = arena_strdup(dst, src->items[i].rel);
        uvs_filelist_push(out, info);
    }
}

int main(int argc, char **argv) {
    char **utf8_argv = uvs_cmdline_argv(&argc);
    if (utf8_argv) {
        argv = utf8_argv;
    }
    Arena *a = arena_new();
    Config cfg;
    parse_args(a, &cfg, argc, argv);

    if (cfg.once) {
        int rc = run_cmd(&cfg);
        arena_free(a);
        return rc;
    }

    signal(SIGINT, on_sigint);
#ifdef SIGTERM
    signal(SIGTERM, on_sigint);
#endif

    UvsFileList prev;
    snapshot(a, &cfg, &prev);
    if (!cfg.no_initial) {
        run_cmd(&cfg);
    }
    printf("watching %zu file(s); interval=%gs debounce=%gs (Ctrl+C to stop)\n", prev.len, cfg.interval, cfg.debounce);
    fflush(stdout);

    int native = !cfg.poll;
    while (!g_stop) {
        int waited = 0;
        if (native) {
            int wr = uvs_watch_wait(cfg.roots.items, cfg.roots.len, cfg.interval);
            if (wr < 0) {
                native = 0;
            } else {
                waited = 1;
                if (wr > 0 && cfg.debounce > 0) {
                    uvs_sleep_seconds(cfg.debounce);
                }
            }
        }
        if (!waited) {
            uvs_sleep_seconds(cfg.interval);
            if (cfg.debounce > 0) {
                uvs_sleep_seconds(cfg.debounce);
            }
        }
        if (g_stop) {
            break;
        }
        Arena *tick = arena_new();
        UvsFileList current;
        snapshot(tick, &cfg, &current);
        if (uvs_filelist_changed(&prev, &current)) {
            run_cmd(&cfg);
            arena_reset(a);
            parse_args(a, &cfg, argc, argv);
            copy_list(a, &prev, &current);
        }
        arena_free(tick);
    }
    printf("\nstopped\n");
    arena_free(a);
    return 0;
}
