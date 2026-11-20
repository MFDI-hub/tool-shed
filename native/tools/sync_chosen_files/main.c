#include "uvs.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_FILE_LIST "scripts/chosen_files.txt"
#define DEFAULT_CODE_OUT "outputs/builds/source_dump.txt"

static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig) {
    (void)sig;
    g_stop = 1;
}

typedef enum { MODE_CODE, MODE_CONCAT, MODE_LINES, MODE_MARKDOWN, MODE_CXML, MODE_JSON } Mode;

typedef struct {
    StrList out_paths;
    Mode mode;
    StrList exts;
    int all_exts;
    StrList include;
    StrList exclude;
    StrList include_gitignored;
    int dedupe;
    int sort_longest;
    int include_header;
    int include_tree;
    int line_numbers;
    int64_t max_bytes;
    size_t max_files;
    int64_t max_file_bytes;
    int truncate_large;
    int64_t chunk_bytes;
    int append;
    int copy;
} OutputSpec;

typedef struct {
    StrList roots;
    StrList explicit_files;
    const char *file_list;
    double interval;
    double debounce;
    int once;
    int use_gitignore;
    int allow_secrets;
    int hidden;
    int follow_symlinks;
    int dry_run;
    int list_only;
    int poll;
    int diff;
    const char *diff_git;
    int64_t newer_than_ns;
    int64_t older_than_ns;
    StrList ignore_files;
    OutputSpec *outputs;
    size_t noutputs;
    size_t cap_outputs;
    Arena *arena;
} Config;

typedef UvsFileInfo FileInfo;
typedef UvsFileList FileList;

static void die(const char *msg) {
    fprintf(stderr, "error: %s\n", msg);
    exit(1);
}

static void usage(FILE *fp) {
    fprintf(fp,
            "Usage: sync_chosen_files [paths ...] [options]\n"
            "\n"
            "Watch files of any kind and dump them to text.\n"
            "\n"
            "Inputs:\n"
            "  paths, --dir PATH, --file-list PATH (default: %s)\n"
            "\n"
            "Outputs:\n"
            "  --out PATH            File, - for stdout, clipboard to copy (repeatable)\n"
            "  --also                Start another output with its own filters\n"
            "  --mode {code,concat,lines,markdown,cxml,json}\n"
            "  --copy                Copy the dump to the clipboard\n"
            "  --append              Append instead of replace\n"
            "  --no-header, --tree / --no-tree, --line-numbers\n"
            "\n"
            "Selection:\n"
            "  --ext EXTS            Suffix filter (default: all text). Example: py,ts,md\n"
            "  --include GLOB        Must match at least one (repeatable)\n"
            "  --exclude GLOB        Skip matches (repeatable)\n"
            "  --include-gitignored GLOB\n"
            "                        Keep gitignored files that match\n"
            "  --ignore-file PATH    Extra gitignore-style rules (repeatable)\n"
            "  --hidden / --no-hidden\n"
            "  --follow-symlinks     Follow directory symlinks\n"
            "  --no-gitignore        Do not apply gitignore\n"
            "  --allow-secrets       Include .env, *.pem, credentials.json, ...\n"
            "  --diff                Only files newer than the output file\n"
            "  --diff-git [REV]      Only files changed vs git REV (default HEAD)\n"
            "  --newer-than WHEN     WHEN is ISO date, unix seconds, or git rev\n"
            "  --older-than WHEN\n"
            "\n"
            "Size:\n"
            "  --max-files N, --max-bytes N, --max-file-bytes N\n"
            "  --truncate            Truncate files over --max-file-bytes instead of skipping\n"
            "  --chunk-bytes N       Split into out-1, out-2, ... at file boundaries\n"
            "  Sizes accept k/m/g suffixes.\n"
            "\n"
            "Watch:\n"
            "  --once                Write once and exit\n"
            "  --interval SECONDS    Poll / native-watch timeout (default: 2)\n"
            "  --debounce SECONDS    Quiet period before a rewrite (default: 0.25)\n"
            "  --poll                Force polling instead of native watch\n"
            "  --list                Print included paths and exit\n"
            "  --dry-run             Print included and skipped paths, do not write\n"
            "  --config PATH         Extra args (also auto-loads .sync_chosen_files)\n"
            "  --no-config           Skip auto config file\n"
            "  --no-dedupe, --sort-longest   (lines mode)\n"
            "  -h, --help\n"
            "\n"
            "Default output: %s\n"
            "Directory scans follow gitignore the way git does. Pass a path explicitly\n"
            "to dump it anyway. .git and non-text files are always skipped.\n"
            "Secret filenames (.env, *.pem, credentials.json, ...) are skipped unless\n"
            "--allow-secrets. A .syncignore next to the working directory is extra rules.\n",
            DEFAULT_FILE_LIST, DEFAULT_CODE_OUT);
}

static int starts_with(const char *s, const char *pfx) {
    return strncmp(s, pfx, strlen(pfx)) == 0;
}

static const char *consume_value(int *i, int argc, char **argv, const char *opt) {
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

static const char *consume_optional(int *i, int argc, char **argv, const char *fallback) {
    if (*i + 1 < argc && argv[*i + 1][0] != '-') {
        return argv[++(*i)];
    }
    return fallback;
}

static int is_true_word(const char *s) {
    return strcmp(s, "1") == 0 || strcmp(s, "true") == 0 || strcmp(s, "yes") == 0 || strcmp(s, "on") == 0;
}

static int is_false_word(const char *s) {
    return strcmp(s, "0") == 0 || strcmp(s, "false") == 0 || strcmp(s, "no") == 0 || strcmp(s, "off") == 0;
}

static int is_stdout_path(const char *p) {
    return p && strcmp(p, "-") == 0;
}

static int is_clip_path(const char *p) {
    return p && (strcmp(p, "clipboard") == 0 || strcmp(p, "clip") == 0);
}

static int is_special_out(const char *p) {
    return is_stdout_path(p) || is_clip_path(p);
}

static Mode parse_mode(const char *s) {
    if (strcmp(s, "code") == 0) {
        return MODE_CODE;
    }
    if (strcmp(s, "concat") == 0) {
        return MODE_CONCAT;
    }
    if (strcmp(s, "lines") == 0) {
        return MODE_LINES;
    }
    if (strcmp(s, "markdown") == 0 || strcmp(s, "md") == 0) {
        return MODE_MARKDOWN;
    }
    if (strcmp(s, "cxml") == 0 || strcmp(s, "xml") == 0) {
        return MODE_CXML;
    }
    if (strcmp(s, "json") == 0) {
        return MODE_JSON;
    }
    fprintf(stderr, "error: unknown mode '%s'\n", s);
    exit(2);
}

static const char *mode_name(Mode m) {
    switch (m) {
        case MODE_CONCAT:
            return "concat";
        case MODE_LINES:
            return "lines";
        case MODE_MARKDOWN:
            return "markdown";
        case MODE_CXML:
            return "cxml";
        case MODE_JSON:
            return "json";
        default:
            return "code";
    }
}

static int64_t parse_size(const char *s) {
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s) {
        fprintf(stderr, "error: invalid size '%s'\n", s);
        exit(2);
    }
    while (*end == ' ') {
        end++;
    }
    double mul = 1;
    if (*end == 'k' || *end == 'K') {
        mul = 1000;
        end++;
    } else if (*end == 'm' || *end == 'M') {
        mul = 1000 * 1000;
        end++;
    } else if (*end == 'g' || *end == 'G') {
        mul = 1000.0 * 1000 * 1000;
        end++;
    }
    if (*end == 'b' || *end == 'B') {
        end++;
    }
    if (*end) {
        fprintf(stderr, "error: invalid size '%s'\n", s);
        exit(2);
    }
    if (v < 0) {
        v = 0;
    }
    return (int64_t)(v * mul + 0.5);
}

static int64_t git_rev_time_ns(Arena *a, const char *rev) {
    char *out = NULL;
    char *cmd = arena_sprintf(a, "git log -1 --format=%%ct %s", rev);
    if (!uvs_run_capture(a, cmd, &out, NULL) || !out || !out[0]) {
        fprintf(stderr, "error: not a date or git revision: %s\n", rev);
        exit(2);
    }
    return (int64_t)strtoll(out, NULL, 10) * 1000000000LL;
}

static int64_t parse_when(Arena *a, const char *s) {
    int all_digit = 1;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') {
            all_digit = 0;
            break;
        }
    }
    if (all_digit && strlen(s) >= 9) {
        return (int64_t)strtoll(s, NULL, 10) * 1000000000LL;
    }
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
    int n = sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec);
    if (n < 3) {
        n = sscanf(s, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &sec);
    }
    if (n < 3) {
        n = sscanf(s, "%d-%d-%d", &y, &mo, &d);
    }
    if (n >= 3) {
        struct tm t;
        memset(&t, 0, sizeof(t));
        t.tm_year = y - 1900;
        t.tm_mon = mo - 1;
        t.tm_mday = d;
        t.tm_hour = h;
        t.tm_min = mi;
        t.tm_sec = sec;
        t.tm_isdst = -1;
        time_t x = mktime(&t);
        if (x == (time_t)-1) {
            fprintf(stderr, "error: invalid date '%s'\n", s);
            exit(2);
        }
        return (int64_t)x * 1000000000LL;
    }
    return git_rev_time_ns(a, s);
}

static void apply_ext_spec(Arena *a, OutputSpec *out, const char *spec) {
    strlist_init(&out->exts, a);
    uvs_parse_exts(a, &out->exts, spec);
    out->all_exts = 0;
    if (out->exts.len == 1 && (strcmp(out->exts.items[0], "*") == 0 || strcmp(out->exts.items[0], "all") == 0)) {
        out->all_exts = 1;
    }
    if (out->exts.len == 0) {
        out->all_exts = 1;
    }
}

static void output_init(Arena *a, OutputSpec *o) {
    memset(o, 0, sizeof(*o));
    strlist_init(&o->out_paths, a);
    strlist_init(&o->exts, a);
    strlist_init(&o->include, a);
    strlist_init(&o->exclude, a);
    strlist_init(&o->include_gitignored, a);
    o->mode = MODE_CODE;
    o->all_exts = 1;
    o->dedupe = 1;
    o->include_header = 1;
    o->include_tree = 1;
}

static void output_copy(Arena *a, OutputSpec *dst, const OutputSpec *src) {
    output_init(a, dst);
    strlist_copy(a, &dst->out_paths, &src->out_paths);
    strlist_copy(a, &dst->exts, &src->exts);
    strlist_copy(a, &dst->include, &src->include);
    strlist_copy(a, &dst->exclude, &src->exclude);
    strlist_copy(a, &dst->include_gitignored, &src->include_gitignored);
    dst->mode = src->mode;
    dst->all_exts = src->all_exts;
    dst->dedupe = src->dedupe;
    dst->sort_longest = src->sort_longest;
    dst->include_header = src->include_header;
    dst->include_tree = src->include_tree;
    dst->line_numbers = src->line_numbers;
    dst->max_bytes = src->max_bytes;
    dst->max_files = src->max_files;
    dst->max_file_bytes = src->max_file_bytes;
    dst->truncate_large = src->truncate_large;
    dst->chunk_bytes = src->chunk_bytes;
    dst->append = src->append;
    dst->copy = src->copy;
}

static OutputSpec *cfg_add_output(Config *cfg) {
    if (cfg->noutputs + 1 > cfg->cap_outputs) {
        size_t cap = cfg->cap_outputs ? cfg->cap_outputs * 2 : 4;
        OutputSpec *items = arena_alloc(cfg->arena, cap * sizeof(*items));
        if (cfg->outputs) {
            memcpy(items, cfg->outputs, cfg->noutputs * sizeof(*items));
        }
        cfg->outputs = items;
        cfg->cap_outputs = cap;
    }
    OutputSpec *o = &cfg->outputs[cfg->noutputs++];
    output_init(cfg->arena, o);
    return o;
}

static OutputSpec *cfg_cur(Config *cfg) {
    return &cfg->outputs[cfg->noutputs - 1];
}

static void cfg_init(Arena *a, Config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->arena = a;
    strlist_init(&cfg->roots, a);
    strlist_init(&cfg->explicit_files, a);
    strlist_init(&cfg->ignore_files, a);
    cfg->interval = 2.0;
    cfg->debounce = 0.25;
    cfg->use_gitignore = 1;
    cfg->allow_secrets = 0;
    cfg->hidden = 1;
    cfg_add_output(cfg);
}

static char *trim_copy(Arena *a, const char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r') {
        s++;
    }
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) {
        n--;
    }
    return arena_strndup(a, s, n);
}

static void dash_key(char *key) {
    for (char *p = key; *p; p++) {
        if (*p == '_') {
            *p = '-';
        }
    }
}

static void config_line_to_args(Arena *a, StrList *args, const char *raw) {
    char *line = trim_copy(a, raw);
    if (!line[0] || line[0] == '#') {
        return;
    }
    if (starts_with(line, "--")) {
        char *sp = strpbrk(line, " \t");
        if (!sp) {
            strlist_push(args, line);
            return;
        }
        *sp = 0;
        strlist_push(args, line);
        strlist_push(args, trim_copy(a, sp + 1));
        return;
    }
    char *eq = strchr(line, '=');
    if (!eq) {
        strlist_push(args, line);
        return;
    }
    *eq = 0;
    char *key = trim_copy(a, line);
    char *val = trim_copy(a, eq + 1);
    if ((val[0] == '"' && val[strlen(val) - 1] == '"') || (val[0] == '\'' && val[strlen(val) - 1] == '\'')) {
        val[strlen(val) - 1] = 0;
        val++;
    }
    dash_key(key);
    if (is_true_word(val)) {
        strlist_push(args, arena_sprintf(a, "--%s", key));
        return;
    }
    if (is_false_word(val)) {
        if (starts_with(key, "no-")) {
            strlist_push(args, arena_sprintf(a, "--%s", key + 3));
        } else {
            strlist_push(args, arena_sprintf(a, "--no-%s", key));
        }
        return;
    }
    strlist_push(args, arena_sprintf(a, "--%s", key));
    strlist_push(args, val);
}

static void load_config_file(Arena *a, StrList *args, const char *path) {
    size_t len = 0;
    char *text = fs_read_all(a, path, &len);
    if (!text) {
        fprintf(stderr, "error: cannot read config %s\n", path);
        exit(1);
    }
    char *p = text;
    while (*p) {
        char *eol = strchr(p, '\n');
        size_t n = eol ? (size_t)(eol - p) : strlen(p);
        char *line = arena_strndup(a, p, n);
        config_line_to_args(a, args, line);
        p = eol ? eol + 1 : p + n;
    }
}

static int flag_eq(const char *arg, const char *name) {
    if (strcmp(arg, name) == 0) {
        return 1;
    }
    size_t n = strlen(name);
    return strncmp(arg, name, n) == 0 && arg[n] == '=';
}

static void parse_args_inner(Config *cfg, int argc, char **argv);

static void parse_args(Arena *a, Config *cfg, int argc, char **argv) {
    cfg_init(a, cfg);
    int no_config = 0;
    const char *extra_config = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-config") == 0) {
            no_config = 1;
        } else if (flag_eq(argv[i], "--config")) {
            extra_config = consume_value(&i, argc, argv, "--config");
        }
    }
    StrList merged;
    strlist_init(&merged, a);
    strlist_push(&merged, argv[0]);
    if (!no_config) {
        char *cwd = path_cwd(a);
        char *auto1 = path_join(a, cwd, ".sync_chosen_files");
        char *auto2 = path_join(a, cwd, "sync_chosen_files.conf");
        if (fs_is_file(auto1)) {
            load_config_file(a, &merged, auto1);
        } else if (fs_is_file(auto2)) {
            load_config_file(a, &merged, auto2);
        }
    }
    if (extra_config) {
        load_config_file(a, &merged, extra_config);
    }
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-config") == 0) {
            continue;
        }
        if (flag_eq(argv[i], "--config")) {
            char *eq = strchr(argv[i], '=');
            if (!eq || !eq[1]) {
                i++;
            }
            continue;
        }
        strlist_push(&merged, argv[i]);
    }
    parse_args_inner(cfg, (int)merged.len, (char **)merged.items);
}

static void parse_args_inner(Config *cfg, int argc, char **argv) {
    Arena *a = cfg->arena;
    int any_input = 0;
    int file_list_set = 0;
    OutputSpec *cur = cfg_cur(cfg);

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            exit(0);
        }
        if (strcmp(arg, "--once") == 0) {
            cfg->once = 1;
            continue;
        }
        if (strcmp(arg, "--no-dedupe") == 0) {
            cur->dedupe = 0;
            continue;
        }
        if (strcmp(arg, "--sort-longest") == 0) {
            cur->sort_longest = 1;
            continue;
        }
        if (strcmp(arg, "--no-header") == 0) {
            cur->include_header = 0;
            continue;
        }
        if (strcmp(arg, "--tree") == 0) {
            cur->include_tree = 1;
            continue;
        }
        if (strcmp(arg, "--no-tree") == 0) {
            cur->include_tree = 0;
            continue;
        }
        if (strcmp(arg, "--line-numbers") == 0) {
            cur->line_numbers = 1;
            continue;
        }
        if (strcmp(arg, "--no-gitignore") == 0) {
            cfg->use_gitignore = 0;
            continue;
        }
        if (strcmp(arg, "--allow-secrets") == 0) {
            cfg->allow_secrets = 1;
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
        if (strcmp(arg, "--dry-run") == 0) {
            cfg->dry_run = 1;
            cfg->once = 1;
            continue;
        }
        if (strcmp(arg, "--list") == 0) {
            cfg->list_only = 1;
            cfg->once = 1;
            continue;
        }
        if (strcmp(arg, "--poll") == 0) {
            cfg->poll = 1;
            continue;
        }
        if (strcmp(arg, "--diff") == 0) {
            cfg->diff = 1;
            continue;
        }
        if (flag_eq(arg, "--diff-git")) {
            char *eq = strchr(argv[i], '=');
            if (eq && eq[1]) {
                cfg->diff_git = eq + 1;
            } else {
                cfg->diff_git = consume_optional(&i, argc, argv, "HEAD");
            }
            continue;
        }
        if (strcmp(arg, "--copy") == 0) {
            cur->copy = 1;
            continue;
        }
        if (strcmp(arg, "--append") == 0) {
            cur->append = 1;
            continue;
        }
        if (strcmp(arg, "--truncate") == 0) {
            cur->truncate_large = 1;
            continue;
        }
        if (strcmp(arg, "--also") == 0) {
            OutputSpec *next = cfg_add_output(cfg);
            output_copy(a, next, cur);
            strlist_init(&next->out_paths, a);
            cur = next;
            continue;
        }
        if (flag_eq(arg, "--dir")) {
            strlist_push(&cfg->roots, path_absolute(a, consume_value(&i, argc, argv, "--dir")));
            any_input = 1;
            continue;
        }
        if (flag_eq(arg, "--file-list")) {
            cfg->file_list = consume_value(&i, argc, argv, "--file-list");
            file_list_set = 1;
            any_input = 1;
            continue;
        }
        if (flag_eq(arg, "--out")) {
            strlist_push(&cur->out_paths, consume_value(&i, argc, argv, "--out"));
            continue;
        }
        if (flag_eq(arg, "--mode")) {
            cur->mode = parse_mode(consume_value(&i, argc, argv, "--mode"));
            continue;
        }
        if (flag_eq(arg, "--ext")) {
            apply_ext_spec(a, cur, consume_value(&i, argc, argv, "--ext"));
            continue;
        }
        if (flag_eq(arg, "--interval")) {
            cfg->interval = strtod(consume_value(&i, argc, argv, "--interval"), NULL);
            continue;
        }
        if (flag_eq(arg, "--debounce")) {
            cfg->debounce = strtod(consume_value(&i, argc, argv, "--debounce"), NULL);
            continue;
        }
        if (flag_eq(arg, "--include")) {
            uvs_parse_csv(a, &cur->include, consume_value(&i, argc, argv, "--include"));
            continue;
        }
        if (flag_eq(arg, "--exclude")) {
            uvs_parse_csv(a, &cur->exclude, consume_value(&i, argc, argv, "--exclude"));
            continue;
        }
        if (flag_eq(arg, "--include-gitignored")) {
            uvs_parse_csv(a, &cur->include_gitignored, consume_value(&i, argc, argv, "--include-gitignored"));
            continue;
        }
        if (flag_eq(arg, "--ignore-file")) {
            strlist_push(&cfg->ignore_files, path_absolute(a, consume_value(&i, argc, argv, "--ignore-file")));
            continue;
        }
        if (flag_eq(arg, "--max-bytes")) {
            cur->max_bytes = parse_size(consume_value(&i, argc, argv, "--max-bytes"));
            continue;
        }
        if (flag_eq(arg, "--max-files")) {
            cur->max_files = (size_t)strtoull(consume_value(&i, argc, argv, "--max-files"), NULL, 10);
            continue;
        }
        if (flag_eq(arg, "--max-file-bytes")) {
            cur->max_file_bytes = parse_size(consume_value(&i, argc, argv, "--max-file-bytes"));
            continue;
        }
        if (flag_eq(arg, "--chunk-bytes")) {
            cur->chunk_bytes = parse_size(consume_value(&i, argc, argv, "--chunk-bytes"));
            continue;
        }
        if (flag_eq(arg, "--newer-than")) {
            cfg->newer_than_ns = parse_when(a, consume_value(&i, argc, argv, "--newer-than"));
            continue;
        }
        if (flag_eq(arg, "--older-than")) {
            cfg->older_than_ns = parse_when(a, consume_value(&i, argc, argv, "--older-than"));
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            for (int j = i + 1; j < argc; j++) {
                strlist_push(&cfg->roots, path_absolute(a, argv[j]));
                any_input = 1;
            }
            break;
        }
        if (arg[0] == '-') {
            fprintf(stderr, "error: unknown option %s\n", arg);
            usage(stderr);
            exit(2);
        }
        strlist_push(&cfg->roots, path_absolute(a, arg));
        any_input = 1;
    }

    for (size_t i = 0; i < cfg->noutputs; i++) {
        OutputSpec *o = &cfg->outputs[i];
        if (o->out_paths.len == 0) {
            strlist_push(&o->out_paths, DEFAULT_CODE_OUT);
        }
        for (size_t j = 0; j < o->out_paths.len; j++) {
            const char *p = o->out_paths.items[j];
            if (!is_special_out(p)) {
                o->out_paths.items[j] = path_absolute(a, p);
            }
        }
    }

    if (!any_input) {
        cfg->file_list = DEFAULT_FILE_LIST;
        file_list_set = 1;
    }
    if (file_list_set && cfg->file_list) {
        char *list_path = path_absolute(a, cfg->file_list);
        if (!fs_is_file(list_path)) {
            fprintf(stderr, "error: File list not found: %s\n", list_path);
            exit(1);
        }
        size_t len = 0;
        char *text = fs_read_all(a, list_path, &len);
        if (!text) {
            die("cannot read file list");
        }
        char *p = text;
        while (*p) {
            char *eol = strchr(p, '\n');
            size_t n = eol ? (size_t)(eol - p) : strlen(p);
            while (n > 0 && (p[n - 1] == '\r' || p[n - 1] == ' ' || p[n - 1] == '\t')) {
                n--;
            }
            size_t lead = 0;
            while (lead < n && (p[lead] == ' ' || p[lead] == '\t')) {
                lead++;
            }
            if (n > lead && p[lead] != '#') {
                char *line = arena_strndup(a, p + lead, n - lead);
                strlist_push(&cfg->roots, path_absolute(a, line));
            }
            p = eol ? eol + 1 : p + n;
        }
    }
    if (cfg->roots.len == 0) {
        die("No inputs. Pass file/dir paths, --dir, and/or --file-list.");
    }
}

typedef struct {
    Config *cfg;
    OutputSpec *out;
    Arena *arena;
    StrList git_diff_files;
    int has_git_diff;
    int64_t out_mtime_ns;
    int has_out_mtime;
} DumpFilter;

static int git_diff_allows(DumpFilter *ctx, const char *rel) {
    if (!ctx->has_git_diff) {
        return 1;
    }
    for (size_t i = 0; i < ctx->git_diff_files.len; i++) {
        if (path_equal(ctx->git_diff_files.items[i], rel) || strcmp(ctx->git_diff_files.items[i], rel) == 0) {
            return 1;
        }
    }
    return 0;
}

static void load_git_diff(DumpFilter *ctx) {
    ctx->has_git_diff = 0;
    strlist_init(&ctx->git_diff_files, ctx->arena);
    if (!ctx->cfg->diff_git) {
        return;
    }
    ctx->has_git_diff = 1;
    const char *rev = ctx->cfg->diff_git[0] ? ctx->cfg->diff_git : "HEAD";
    char *out = NULL;
    char *cmd = arena_sprintf(ctx->arena, "git diff --name-only --diff-filter=ACMR %s", rev);
    if (uvs_run_capture(ctx->arena, cmd, &out, NULL) && out) {
        char *p = out;
        while (*p) {
            char *eol = strchr(p, '\n');
            size_t n = eol ? (size_t)(eol - p) : strlen(p);
            while (n > 0 && (p[n - 1] == '\r' || p[n - 1] == ' ')) {
                n--;
            }
            if (n) {
                strlist_push(&ctx->git_diff_files, arena_strndup(ctx->arena, p, n));
            }
            p = eol ? eol + 1 : p + n;
        }
    }
    int vs_head = strcmp(rev, "HEAD") == 0;
    if (vs_head) {
        char *untracked = NULL;
        if (uvs_run_capture(ctx->arena, "git ls-files --others --exclude-standard", &untracked, NULL) && untracked) {
            char *p = untracked;
            while (*p) {
                char *eol = strchr(p, '\n');
                size_t n = eol ? (size_t)(eol - p) : strlen(p);
                while (n > 0 && (p[n - 1] == '\r' || p[n - 1] == ' ')) {
                    n--;
                }
                if (n) {
                    strlist_push(&ctx->git_diff_files, arena_strndup(ctx->arena, p, n));
                }
                p = eol ? eol + 1 : p + n;
            }
        }
    }
}

static int dump_keep(const UvsFileInfo *file, void *ud, const char **reason) {
    DumpFilter *ctx = ud;
    if (ctx->cfg->diff && ctx->has_out_mtime && file->mtime_ns <= ctx->out_mtime_ns) {
        *reason = "diff";
        return 0;
    }
    if (ctx->cfg->newer_than_ns && file->mtime_ns < ctx->cfg->newer_than_ns) {
        *reason = "newer-than";
        return 0;
    }
    if (ctx->cfg->older_than_ns && file->mtime_ns >= ctx->cfg->older_than_ns) {
        *reason = "older-than";
        return 0;
    }
    if (!git_diff_allows(ctx, file->rel)) {
        *reason = "diff-git";
        return 0;
    }
    if (ctx->out->max_file_bytes > 0 && file->size > ctx->out->max_file_bytes && !ctx->out->truncate_large &&
        !file->explicit_file) {
        *reason = "max-file-bytes";
        return 0;
    }
    return 1;
}

static void discover(Arena *a, Config *cfg, OutputSpec *out, FileList *files) {
    DumpFilter filter;
    memset(&filter, 0, sizeof(filter));
    filter.cfg = cfg;
    filter.out = out;
    filter.arena = a;
    load_git_diff(&filter);
    filter.has_out_mtime = 0;
    if (cfg->diff) {
        for (size_t i = 0; i < out->out_paths.len; i++) {
            const char *p = out->out_paths.items[i];
            if (is_special_out(p)) {
                continue;
            }
            FsStat st;
            if (fs_stat(p, &st) && st.is_file) {
                filter.out_mtime_ns = st.mtime_ns;
                filter.has_out_mtime = 1;
                break;
            }
        }
    }

    StrList skip_abs;
    strlist_init(&skip_abs, a);
    for (size_t i = 0; i < cfg->noutputs; i++) {
        const OutputSpec *o = &cfg->outputs[i];
        for (size_t j = 0; j < o->out_paths.len; j++) {
            const char *p = o->out_paths.items[j];
            if (!is_special_out(p)) {
                strlist_push(&skip_abs, p);
            }
        }
    }

    UvsDiscoverOpts opts;
    uvs_discover_opts_init(&opts);
    opts.roots = &cfg->roots;
    opts.exts = &out->exts;
    opts.all_exts = out->all_exts;
    opts.include = &out->include;
    opts.exclude = &out->exclude;
    opts.include_gitignored = &out->include_gitignored;
    opts.ignore_files = &cfg->ignore_files;
    opts.skip_abs = &skip_abs;
    opts.use_gitignore = cfg->use_gitignore;
    opts.hidden = cfg->hidden;
    opts.follow_symlinks = cfg->follow_symlinks;
    opts.skip_secrets = !cfg->allow_secrets;
    opts.skip_non_text = 1;
    opts.log_skips = cfg->dry_run;
    opts.list_hits = cfg->list_only || cfg->dry_run;
    opts.max_files = out->max_files;
    uvs_discover(a, &opts, dump_keep, &filter, files);
}

static char *read_text_file(Arena *a, const char *path, size_t *len_out) {
    size_t len = 0;
    char *body = fs_read_all(a, path, &len);
    if (!body) {
        if (len_out) {
            *len_out = 0;
        }
        return NULL;
    }
    if (uvs_is_non_text(body, len)) {
        if (len_out) {
            *len_out = 0;
        }
        return NULL;
    }
    if (len_out) {
        *len_out = len;
    }
    return body;
}

static char *file_body(Arena *a, const OutputSpec *o, const FileInfo *info, size_t *len_out) {
    size_t len = 0;
    char *body = read_text_file(a, info->abs, &len);
    if (!body) {
        if (len_out) {
            *len_out = 0;
        }
        return NULL;
    }
    if (o->max_file_bytes > 0 && (int64_t)len > o->max_file_bytes && o->truncate_large) {
        body = arena_strndup(a, body, (size_t)o->max_file_bytes);
        size_t n = (size_t)o->max_file_bytes;
        body = arena_sprintf(a, "%s\n# [truncated]\n", arena_strndup(a, body, n));
        len = strlen(body);
    }
    if (len_out) {
        *len_out = len;
    }
    return body;
}

static char *rstrip_copy(Arena *a, const char *s, size_t n) {
    while (n > 0) {
        unsigned char c = (unsigned char)s[n - 1];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != '\v') {
            break;
        }
        n--;
    }
    return arena_strndup(a, s, n);
}

static void append_numbered(Buf *buf, const char *body) {
    size_t n = 1;
    const char *p = body;
    if (!*p) {
        return;
    }
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        if (len > 0 && p[len - 1] == '\r') {
            len--;
        }
        buf_append_fmt(buf, "%6zu| ", n++);
        buf_append(buf, p, len);
        buf_append_char(buf, '\n');
        if (!eol) {
            break;
        }
        p = eol + 1;
        if (*p == '\0') {
            break;
        }
    }
}

static void append_body(Buf *buf, const OutputSpec *o, const char *body) {
    if (o->line_numbers) {
        append_numbered(buf, body);
    } else {
        buf_append_str(buf, body);
        if (!body[0] || body[strlen(body) - 1] != '\n') {
            buf_append_char(buf, '\n');
        }
    }
}

static void buf_append_xml(Buf *buf, const char *s) {
    for (; *s; s++) {
        if (*s == '&') {
            buf_append_str(buf, "&amp;");
        } else if (*s == '<') {
            buf_append_str(buf, "&lt;");
        } else if (*s == '>') {
            buf_append_str(buf, "&gt;");
        } else {
            buf_append_char(buf, *s);
        }
    }
}

static void buf_append_json_str(Buf *buf, const char *s) {
    buf_append_char(buf, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            buf_append_char(buf, '\\');
            buf_append_char(buf, (char)c);
        } else if (c == '\n') {
            buf_append_str(buf, "\\n");
        } else if (c == '\r') {
            buf_append_str(buf, "\\r");
        } else if (c == '\t') {
            buf_append_str(buf, "\\t");
        } else if (c < 32) {
            buf_append_fmt(buf, "\\u%04x", c);
        } else {
            buf_append_char(buf, (char)c);
        }
    }
    buf_append_char(buf, '"');
}

static void build_tree(Buf *buf, const FileList *files, const char *prefix) {
    const char *stack[64];
    size_t depth = 0;
    for (size_t i = 0; i < files->len; i++) {
        const char *rel = files->items[i].rel;
        const char *parts[64];
        size_t nparts = 0;
        char *copy = arena_strdup(buf->arena, rel);
        char *p = copy;
        while (*p && nparts < 64) {
            parts[nparts++] = p;
            char *sl = strchr(p, '/');
            if (!sl) {
                break;
            }
            *sl = 0;
            p = sl + 1;
        }
        size_t common = 0;
        while (common < depth && common < nparts - 1 && strcmp(stack[common], parts[common]) == 0) {
            common++;
        }
        depth = common;
        for (size_t c = common; c < nparts; c++) {
            for (size_t k = 0; k < c; k++) {
                buf_append_str(buf, prefix);
                buf_append_str(buf, "  ");
            }
            buf_append_str(buf, prefix);
            buf_append_str(buf, parts[c]);
            if (c + 1 < nparts) {
                buf_append_str(buf, "/\n");
                if (depth < 64) {
                    stack[depth++] = parts[c];
                }
            } else {
                buf_append_char(buf, '\n');
            }
        }
    }
}

static size_t count_newlines(const char *s) {
    size_t n = 0;
    for (; *s; s++) {
        if (*s == '\n') {
            n++;
        }
    }
    return n;
}

static size_t total_bytes(const FileList *files) {
    size_t n = 0;
    for (size_t i = 0; i < files->len; i++) {
        if (files->items[i].size > 0) {
            n += (size_t)files->items[i].size;
        }
    }
    return n;
}

static void append_stats(Buf *buf, const FileList *files, const char *pfx) {
    size_t bytes = total_bytes(files);
    size_t lines = 0;
    for (size_t i = 0; i < files->len; i++) {
        size_t len = 0;
        char *body = read_text_file(buf->arena, files->items[i].abs, &len);
        if (body) {
            lines += count_newlines(body);
            if (len && body[len - 1] != '\n') {
                lines++;
            }
        }
    }
    size_t tokens = (bytes + 3) / 4;
    buf_append_fmt(buf, "%sfiles: %zu\n", pfx, files->len);
    buf_append_fmt(buf, "%sbytes: %zu\n", pfx, bytes);
    buf_append_fmt(buf, "%slines: %zu\n", pfx, lines);
    buf_append_fmt(buf, "%stokens: ~%zu\n", pfx, tokens);
}

static const char *fence_lang(const char *rel) {
    const char *base = path_basename(rel);
    const char *dot = strrchr(base, '.');
    if (!dot || !dot[1] || dot == base) {
        return "";
    }
    return dot + 1;
}

typedef struct {
    const char *line;
    size_t len;
} LineItem;

static int cmp_line_longest(const void *a, const void *b) {
    const LineItem *la = a;
    const LineItem *lb = b;
    if (la->len != lb->len) {
        return la->len > lb->len ? -1 : 1;
    }
    return strcmp(la->line, lb->line);
}

static void collect_plain_lines(Arena *a, const FileList *files, const OutputSpec *o, StrList *out) {
    strlist_init(out, a);
    for (size_t i = 0; i < files->len; i++) {
        size_t len = 0;
        char *body = file_body(a, o, &files->items[i], &len);
        if (!body) {
            continue;
        }
        char *p = body;
        while (*p) {
            char *eol = strchr(p, '\n');
            size_t n = eol ? (size_t)(eol - p) : strlen(p);
            if (n > 0 && p[n - 1] == '\r') {
                n--;
            }
            while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t')) {
                n--;
            }
            size_t lead = 0;
            while (lead < n && (p[lead] == ' ' || p[lead] == '\t')) {
                lead++;
            }
            if (n > lead) {
                char *line = arena_strndup(a, p + lead, n - lead);
                int skip = 0;
                if (o->dedupe) {
                    for (size_t k = 0; k < out->len; k++) {
                        if (strcmp(out->items[k], line) == 0) {
                            skip = 1;
                            break;
                        }
                    }
                }
                if (!skip) {
                    strlist_push(out, line);
                }
            }
            p = eol ? eol + 1 : p + n;
        }
    }
    if (o->sort_longest && out->len > 1) {
        LineItem *items = arena_alloc(a, out->len * sizeof(*items));
        for (size_t i = 0; i < out->len; i++) {
            items[i].line = out->items[i];
            items[i].len = strlen(out->items[i]);
        }
        qsort(items, out->len, sizeof(*items), cmp_line_longest);
        for (size_t i = 0; i < out->len; i++) {
            out->items[i] = items[i].line;
        }
    }
}

static int over_max_bytes(const OutputSpec *o, size_t n) {
    return o->max_bytes > 0 && (int64_t)n > o->max_bytes;
}

static char *build_output(Arena *a, const FileList *files, const OutputSpec *o) {
    Buf buf;
    buf_init(&buf, a);
    size_t used = files->len;
    if (o->mode == MODE_JSON) {
        buf_append_char(&buf, '{');
        if (o->include_header) {
            buf_append_str(&buf, "\"files\":");
            buf_append_fmt(&buf, "%zu", files->len);
            buf_append_str(&buf, ",\"bytes\":");
            buf_append_fmt(&buf, "%zu", total_bytes(files));
            buf_append_str(&buf, ",\"tokens\":");
            buf_append_fmt(&buf, "%zu", (total_bytes(files) + 3) / 4);
            if (o->include_tree) {
                Buf tree;
                buf_init(&tree, a);
                build_tree(&tree, files, "");
                buf_null_terminate(&tree);
                buf_append_str(&buf, ",\"tree\":");
                buf_append_json_str(&buf, tree.data ? tree.data : "");
            }
            buf_append_char(&buf, ',');
        }
        buf_append_str(&buf, "\"items\":[");
        int first = 1;
        for (size_t i = 0; i < files->len; i++) {
            size_t len = 0;
            char *body = file_body(a, o, &files->items[i], &len);
            if (!body) {
                continue;
            }
            char *trimmed = rstrip_copy(a, body, strlen(body));
            if (over_max_bytes(o, buf.len + strlen(trimmed) + 32)) {
                used = i;
                break;
            }
            if (!first) {
                buf_append_char(&buf, ',');
            }
            first = 0;
            buf_append_str(&buf, "{\"path\":");
            buf_append_json_str(&buf, files->items[i].rel);
            buf_append_str(&buf, ",\"content\":");
            buf_append_json_str(&buf, trimmed);
            buf_append_char(&buf, '}');
        }
        buf_append_str(&buf, "]}");
        buf_null_terminate(&buf);
        (void)used;
        return buf.data ? buf.data : arena_strdup(a, "{}");
    }

    if (o->mode == MODE_CXML) {
        buf_append_str(&buf, "<documents>\n");
        for (size_t i = 0; i < files->len; i++) {
            size_t len = 0;
            char *body = file_body(a, o, &files->items[i], &len);
            if (!body) {
                continue;
            }
            char *trimmed = rstrip_copy(a, body, strlen(body));
            if (over_max_bytes(o, buf.len + strlen(trimmed) + 64)) {
                break;
            }
            buf_append_fmt(&buf, "<document index=\"%zu\">\n<source>", i + 1);
            buf_append_xml(&buf, files->items[i].rel);
            buf_append_str(&buf, "</source>\n<document_content>\n");
            buf_append_xml(&buf, trimmed);
            buf_append_str(&buf, "\n</document_content>\n</document>\n");
        }
        buf_append_str(&buf, "</documents>\n");
        buf_null_terminate(&buf);
        return buf.data ? buf.data : arena_strdup(a, "");
    }

    if (o->mode == MODE_LINES) {
        StrList lines;
        collect_plain_lines(a, files, o, &lines);
        if (o->include_header) {
            buf_append_str(&buf, "# synced dump\n");
            append_stats(&buf, files, "# ");
            buf_append_char(&buf, '\n');
        }
        for (size_t i = 0; i < lines.len; i++) {
            if (over_max_bytes(o, buf.len + strlen(lines.items[i]) + 1)) {
                break;
            }
            buf_append_str(&buf, lines.items[i]);
            buf_append_char(&buf, '\n');
        }
        buf_null_terminate(&buf);
        return buf.data ? buf.data : arena_strdup(a, "");
    }

    if (o->include_header && o->mode != MODE_CONCAT) {
        buf_append_str(&buf, "# synced dump\n");
        append_stats(&buf, files, "# ");
        if (o->include_tree && files->len) {
            buf_append_str(&buf, "#\n# tree:\n");
            build_tree(&buf, files, "# ");
        } else if (files->len) {
            buf_append_str(&buf, "#\n");
            for (size_t i = 0; i < files->len; i++) {
                buf_append_fmt(&buf, "#   %s\n", files->items[i].rel);
            }
        }
        buf_append_char(&buf, '\n');
    }

    if (o->mode == MODE_MARKDOWN) {
        for (size_t i = 0; i < files->len; i++) {
            size_t len = 0;
            char *body = file_body(a, o, &files->items[i], &len);
            if (!body) {
                continue;
            }
            char *trimmed = rstrip_copy(a, body, strlen(body));
            if (over_max_bytes(o, buf.len + strlen(trimmed) + 64)) {
                break;
            }
            buf_append_fmt(&buf, "## %s\n\n```%s\n", files->items[i].rel, fence_lang(files->items[i].rel));
            append_body(&buf, o, trimmed);
            buf_append_str(&buf, "```\n\n");
        }
        buf_null_terminate(&buf);
        return buf.data ? buf.data : arena_strdup(a, "");
    }

    if (o->mode == MODE_CONCAT) {
        for (size_t i = 0; i < files->len; i++) {
            size_t len = 0;
            char *body = file_body(a, o, &files->items[i], &len);
            if (!body) {
                continue;
            }
            char *trimmed = rstrip_copy(a, body, strlen(body));
            if (over_max_bytes(o, buf.len + strlen(trimmed) + 32)) {
                break;
            }
            buf_append_fmt(&buf, "=== %s ===\n", files->items[i].rel);
            append_body(&buf, o, trimmed);
            if (i + 1 < files->len) {
                buf_append_char(&buf, '\n');
            }
        }
        buf_null_terminate(&buf);
        while (buf.len && buf.data[buf.len - 1] == '\n') {
            buf.len--;
        }
        if (buf.len) {
            buf_append_char(&buf, '\n');
        }
        return buf.data ? buf.data : arena_strdup(a, "");
    }

    for (size_t i = 0; i < files->len; i++) {
        size_t len = 0;
        char *body = file_body(a, o, &files->items[i], &len);
        if (!body) {
            continue;
        }
        char *trimmed = rstrip_copy(a, body, strlen(body));
        if (over_max_bytes(o, buf.len + strlen(trimmed) + 120)) {
            fprintf(stderr, "warning: --max-bytes reached, stopping at %s\n", files->items[i].rel);
            break;
        }
        buf_append_fmt(&buf,
                       "================================================================================\nFILE: "
                       "%s\n================================================================================\n",
                       files->items[i].rel);
        append_body(&buf, o, trimmed);
        if (i + 1 < files->len) {
            buf_append_char(&buf, '\n');
        }
    }
    buf_null_terminate(&buf);
    while (buf.len && (buf.data[buf.len - 1] == '\n' || buf.data[buf.len - 1] == '\r')) {
        buf.len--;
    }
    if (buf.len) {
        buf_append_char(&buf, '\n');
    }
    return buf.data ? buf.data : arena_strdup(a, "");
}

static void split_out_name(Arena *a, const char *path, char **dir, char **stem, char **ext) {
    char *abs = arena_strdup(a, path);
    char *slash = strrchr(abs, '/');
    if (slash) {
        *slash = 0;
        *dir = abs;
        abs = slash + 1;
    } else {
        *dir = arena_strdup(a, ".");
    }
    char *dot = strrchr(abs, '.');
    if (dot && dot != abs) {
        *ext = arena_strdup(a, dot);
        *dot = 0;
        *stem = abs;
    } else {
        *stem = abs;
        *ext = arena_strdup(a, "");
    }
}

static int write_one_path(const char *path, const char *data, size_t len, int append) {
    if (is_stdout_path(path)) {
        fwrite(data, 1, len, stdout);
        if (len && data[len - 1] != '\n') {
            fputc('\n', stdout);
        }
        return 1;
    }
    if (is_clip_path(path)) {
        if (!uvs_clipboard_set(data)) {
            fprintf(stderr, "warning: could not write clipboard\n");
            return 0;
        }
        return 1;
    }
    if (append) {
        return fs_append_all(path, data, len);
    }
    return fs_write_all(path, data, len);
}

static int write_chunks(Arena *a, const OutputSpec *o, const char *path, const char *content) {
    size_t len = strlen(content);
    if (o->chunk_bytes <= 0 || is_special_out(path) || o->append || (int64_t)len <= o->chunk_bytes) {
        int ok = write_one_path(path, content, len, o->append);
        if (ok && o->copy && !is_clip_path(path)) {
            if (!uvs_clipboard_set(content)) {
                fprintf(stderr, "warning: could not write clipboard\n");
            }
        }
        return ok;
    }
    char *dir = NULL;
    char *stem = NULL;
    char *ext = NULL;
    split_out_name(a, path, &dir, &stem, &ext);
    size_t start = 0;
    int part = 1;
    int ok = 1;
    while (start < len) {
        size_t end = start + (size_t)o->chunk_bytes;
        if (end > len) {
            end = len;
        }
        if (end < len) {
            const char *cut = content + end;
            while (cut > content + start && *cut != '\n') {
                cut--;
            }
            if (cut > content + start) {
                end = (size_t)(cut - content) + 1;
            }
        }
        char *chunk_path = path_join(a, dir, arena_sprintf(a, "%s-%d%s", stem, part, ext));
        if (!write_one_path(chunk_path, content + start, end - start, 0)) {
            ok = 0;
        }
        start = end;
        part++;
    }
    if (ok && o->copy) {
        if (!uvs_clipboard_set(content)) {
            fprintf(stderr, "warning: could not write clipboard\n");
        }
    }
    return ok;
}

static int write_output_spec(Arena *a, OutputSpec *o, const FileList *files) {
    char *content = build_output(a, files, o);
    int ok = 1;
    for (size_t i = 0; i < o->out_paths.len; i++) {
        if (!write_chunks(a, o, o->out_paths.items[i], content)) {
            ok = 0;
        }
    }
    return ok ? 1 : -1;
}

static int sync_once(Arena *a, Config *cfg, FileList *prev, int force, size_t *nfiles, size_t *nlines) {
    size_t total_files = 0;
    size_t total_lines = 0;
    int any_change = 0;
    for (size_t i = 0; i < cfg->noutputs; i++) {
        FileList files;
        discover(a, cfg, &cfg->outputs[i], &files);
        total_files += files.len;
        if (cfg->list_only || cfg->dry_run) {
            continue;
        }
        if (!force && prev && i == 0 && !uvs_filelist_changed(prev, &files)) {
            continue;
        }
        any_change = 1;
        char *content = build_output(a, &files, &cfg->outputs[i]);
        total_lines += count_newlines(content);
        if (write_output_spec(a, &cfg->outputs[i], &files) < 0) {
            return -1;
        }
        if (prev && i == 0) {
            *prev = files;
        }
    }
    *nfiles = total_files;
    *nlines = total_lines;
    if (cfg->list_only || cfg->dry_run) {
        return 1;
    }
    return any_change || force ? 1 : 0;
}

static void clone_config(Arena *dst, Config *out, const Config *src) {
    cfg_init(dst, out);
    strlist_copy(dst, &out->roots, &src->roots);
    strlist_copy(dst, &out->ignore_files, &src->ignore_files);
    out->file_list = src->file_list ? arena_strdup(dst, src->file_list) : NULL;
    out->interval = src->interval;
    out->debounce = src->debounce;
    out->once = src->once;
    out->use_gitignore = src->use_gitignore;
    out->allow_secrets = src->allow_secrets;
    out->hidden = src->hidden;
    out->follow_symlinks = src->follow_symlinks;
    out->dry_run = src->dry_run;
    out->list_only = src->list_only;
    out->poll = src->poll;
    out->diff = src->diff;
    out->diff_git = src->diff_git ? arena_strdup(dst, src->diff_git) : NULL;
    out->newer_than_ns = src->newer_than_ns;
    out->older_than_ns = src->older_than_ns;
    out->noutputs = 0;
    for (size_t i = 0; i < src->noutputs; i++) {
        OutputSpec *o = cfg_add_output(out);
        output_copy(dst, o, &src->outputs[i]);
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

    for (size_t i = 0; i < cfg.roots.len; i++) {
        if (!fs_exists(cfg.roots.items[i])) {
            fprintf(stdout, "warning: not found yet (will watch for it): %s\n", cfg.roots.items[i]);
        }
    }

    if (cfg.once) {
        size_t nfiles = 0;
        size_t nlines = 0;
        if (sync_once(a, &cfg, NULL, 1, &nfiles, &nlines) < 0) {
            arena_free(a);
            return 1;
        }
        if (!cfg.list_only && !cfg.dry_run) {
            int uses_stdout = 0;
            for (size_t i = 0; i < cfg.noutputs; i++) {
                for (size_t j = 0; j < cfg.outputs[i].out_paths.len; j++) {
                    if (is_stdout_path(cfg.outputs[i].out_paths.items[j])) {
                        uses_stdout = 1;
                    }
                }
            }
            fprintf(uses_stdout ? stderr : stdout, "wrote %zu lines from %zu file(s)\n", nlines, nfiles);
        }
        arena_free(a);
        return 0;
    }

    signal(SIGINT, on_sigint);
#ifdef SIGTERM
    signal(SIGTERM, on_sigint);
#endif

    FileList prev;
    uvs_filelist_init(&prev, a);
    size_t nfiles = 0;
    size_t nlines = 0;
    if (sync_once(a, &cfg, &prev, 1, &nfiles, &nlines) < 0) {
        arena_free(a);
        return 1;
    }
    printf("syncing %zu file(s)\n", nfiles);
    printf("mode=%s interval=%gs debounce=%gs (Ctrl+C to stop)\n", mode_name(cfg.outputs[0].mode), cfg.interval,
           cfg.debounce);
    printf("initial write: %zu lines\n", nlines);
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
        Config tick_cfg;
        clone_config(tick, &tick_cfg, &cfg);
        FileList current;
        uvs_filelist_init(&current, tick);
        discover(tick, &tick_cfg, &tick_cfg.outputs[0], &current);
        int changed = uvs_filelist_changed(&prev, &current);
        if (changed) {
            for (size_t i = 0; i < tick_cfg.noutputs; i++) {
                FileList files;
                if (i == 0) {
                    files = current;
                } else {
                    discover(tick, &tick_cfg, &tick_cfg.outputs[i], &files);
                }
                if (write_output_spec(tick, &tick_cfg.outputs[i], &files) < 0) {
                    arena_free(tick);
                    arena_free(a);
                    return 1;
                }
                if (i == 0) {
                    nfiles = files.len;
                    nlines = count_newlines(build_output(tick, &files, &tick_cfg.outputs[i]));
                }
            }
            printf("updated: %zu files, %zu lines\n", nfiles, nlines);
            fflush(stdout);
            arena_reset(a);
            parse_args(a, &cfg, argc, argv);
            FileList stored;
            uvs_filelist_init(&stored, a);
            for (size_t i = 0; i < current.len; i++) {
                FileInfo info = current.items[i];
                info.abs = arena_strdup(a, current.items[i].abs);
                info.rel = arena_strdup(a, current.items[i].rel);
                uvs_filelist_push(&stored, info);
            }
            prev = stored;
        }
        arena_free(tick);
    }
    printf("\nstopped\n");
    arena_free(a);
    return 0;
}
