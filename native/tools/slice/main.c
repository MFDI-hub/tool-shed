#include "uvs.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *file;
    StrList names;
    StrList line_specs;
    const char *out_path;
    Arena *arena;
} Config;

static void usage(FILE *fp) {
    fprintf(fp,
            "Usage: slice FILE [--name NAME] [--lines A-B[,C-D]] [--out PATH]\n"
            "\n"
            "Emit one symbol body or line range. Not a file dump and not a skeleton.\n"
            "\n"
            "  --name NAME     Signature whose name matches as a word (repeatable)\n"
            "  --lines RANGES  1-based inclusive ranges, comma-separated\n"
            "  --out PATH      File or - for stdout (default: -)\n"
            "  -h, --help\n");
}

static void cfg_init(Arena *a, Config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->arena = a;
    strlist_init(&cfg->names, a);
    strlist_init(&cfg->line_specs, a);
    cfg->out_path = "-";
}

static void parse_args(Arena *a, Config *cfg, int argc, char **argv) {
    cfg_init(a, cfg);
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            exit(0);
        }
        if (uvs_flag_eq(arg, "--name")) {
            strlist_push(&cfg->names, uvs_consume_value(&i, argc, argv, "--name"));
            continue;
        }
        if (uvs_flag_eq(arg, "--lines")) {
            uvs_parse_csv(a, &cfg->line_specs, uvs_consume_value(&i, argc, argv, "--lines"));
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
        if (cfg->file) {
            fprintf(stderr, "error: extra path %s\n", arg);
            exit(2);
        }
        cfg->file = arg;
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

static int line_indent(const char *s) {
    int n = 0;
    while (*s == ' ' || *s == '\t') {
        n += (*s == '\t') ? 4 : 1;
        s++;
    }
    return n;
}

static const char *lstrip(const char *s) {
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

static int fold_eq(const char *a, const char *b) {
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

static const char *file_ext(const char *path) {
    const char *base = path_basename(path);
    const char *dot = strrchr(base, '.');
    if (!dot || !dot[1] || dot == base) {
        return "";
    }
    return dot + 1;
}

static int is_indent_lang(const char *path) {
    const char *ext = file_ext(path);
    return fold_eq(ext, "py") || fold_eq(ext, "pyi") || fold_eq(ext, "pyw") || fold_eq(ext, "rb");
}

static int ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

static int has_word(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    if (!n) {
        return 0;
    }
    for (const char *p = hay; *p; p++) {
        if (strncmp(p, needle, n) == 0) {
            int left = p == hay || !ident_char(p[-1]);
            int right = !ident_char(p[n]);
            if (left && right) {
                return 1;
            }
        }
    }
    return 0;
}

typedef struct {
    const char *text;
    size_t off;
    size_t len;
    int indent;
} SrcLine;

static void split_lines(Arena *a, const char *src, size_t n, SrcLine **out, size_t *nout) {
    size_t cap = 0;
    size_t len = 0;
    SrcLine *items = NULL;
    const char *p = src;
    const char *end = src + n;
    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        size_t ln = eol ? (size_t)(eol - p) : (size_t)(end - p);
        if (len + 1 > cap) {
            size_t ncap = cap ? cap * 2 : 32;
            SrcLine *ni = arena_alloc(a, ncap * sizeof(*ni));
            if (items) {
                memcpy(ni, items, len * sizeof(*ni));
            }
            items = ni;
            cap = ncap;
        }
        char *line = arena_strndup(a, p, ln);
        if (ln && line[ln - 1] == '\r') {
            line[ln - 1] = 0;
            ln--;
        }
        items[len].text = line;
        items[len].off = (size_t)(p - src);
        items[len].len = ln;
        items[len].indent = line_indent(line);
        len++;
        p = eol ? eol + 1 : end;
    }
    *out = items;
    *nout = len;
}

static void skip_body(const char *s, size_t n, size_t *i) {
    int inner = 1;
    int in_s = 0, in_c = 0, in_sl = 0, in_ml = 0;
    while (*i < n && inner > 0) {
        char ch = s[*i];
        char next = (*i + 1 < n) ? s[*i + 1] : 0;
        if (in_sl) {
            if (ch == '\n') {
                in_sl = 0;
            }
            (*i)++;
            continue;
        }
        if (in_ml) {
            if (ch == '*' && next == '/') {
                in_ml = 0;
                *i += 2;
                continue;
            }
            (*i)++;
            continue;
        }
        if (in_s) {
            if (ch == '\\' && *i + 1 < n) {
                *i += 2;
                continue;
            }
            if (ch == '"') {
                in_s = 0;
            }
            (*i)++;
            continue;
        }
        if (in_c) {
            if (ch == '\\' && *i + 1 < n) {
                *i += 2;
                continue;
            }
            if (ch == '\'') {
                in_c = 0;
            }
            (*i)++;
            continue;
        }
        if (ch == '/' && next == '/') {
            in_sl = 1;
            *i += 2;
            continue;
        }
        if (ch == '/' && next == '*') {
            in_ml = 1;
            *i += 2;
            continue;
        }
        if (ch == '"') {
            in_s = 1;
            (*i)++;
            continue;
        }
        if (ch == '\'') {
            in_c = 1;
            (*i)++;
            continue;
        }
        if (ch == '{') {
            inner++;
        } else if (ch == '}') {
            inner--;
        }
        (*i)++;
    }
}

static int emit_indent_name(Buf *out, SrcLine *lines, size_t nlines, const char *name, Arena *a) {
    for (size_t i = 0; i < nlines; i++) {
        const char *stripped = lstrip(lines[i].text);
        char *sig = uvs_sig_text(a, stripped);
        if (!sig || !has_word(sig, name)) {
            continue;
        }
        int ind = lines[i].indent;
        buf_append_str(out, lines[i].text);
        buf_append_char(out, '\n');
        for (size_t j = i + 1; j < nlines; j++) {
            const char *st = lstrip(lines[j].text);
            if (st[0] && lines[j].indent <= ind) {
                break;
            }
            buf_append_str(out, lines[j].text);
            buf_append_char(out, '\n');
        }
        return 1;
    }
    return 0;
}

static int emit_brace_name(Buf *out, const char *src, size_t n, SrcLine *lines, size_t nlines, const char *name,
                           Arena *a) {
    for (size_t i = 0; i < nlines; i++) {
        const char *stripped = lstrip(lines[i].text);
        char *sig = uvs_sig_text(a, stripped);
        if (!sig || !has_word(sig, name)) {
            continue;
        }
        size_t start = lines[i].off;
        size_t brace = start;
        while (brace < n && src[brace] != '{') {
            brace++;
        }
        if (brace >= n) {
            buf_append_str(out, lines[i].text);
            buf_append_char(out, '\n');
            return 1;
        }
        size_t end = brace + 1;
        skip_body(src, n, &end);
        buf_append(out, src + start, end - start);
        if (end > start && src[end - 1] != '\n') {
            buf_append_char(out, '\n');
        }
        return 1;
    }
    return 0;
}

static int parse_range(const char *spec, size_t *lo, size_t *hi) {
    char *dash = strchr(spec, '-');
    if (!dash) {
        unsigned long v = strtoul(spec, NULL, 10);
        if (!v) {
            return 0;
        }
        *lo = (size_t)v;
        *hi = (size_t)v;
        return 1;
    }
    unsigned long a = strtoul(spec, NULL, 10);
    unsigned long b = strtoul(dash + 1, NULL, 10);
    if (!a || !b || b < a) {
        return 0;
    }
    *lo = (size_t)a;
    *hi = (size_t)b;
    return 1;
}

static void emit_lines(Buf *out, SrcLine *lines, size_t nlines, size_t lo, size_t hi) {
    if (lo < 1) {
        lo = 1;
    }
    for (size_t i = lo; i <= hi && i <= nlines; i++) {
        buf_append_str(out, lines[i - 1].text);
        buf_append_char(out, '\n');
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
    if (!cfg.file) {
        fprintf(stderr, "error: FILE is required\n");
        usage(stderr);
        arena_free(a);
        return 2;
    }
    if (cfg.names.len == 0 && cfg.line_specs.len == 0) {
        fprintf(stderr, "error: need --name or --lines\n");
        arena_free(a);
        return 2;
    }
    char *abs = path_absolute(a, cfg.file);
    size_t n = 0;
    char *src = fs_read_all(a, abs, &n);
    if (!src) {
        fprintf(stderr, "error: could not read %s\n", cfg.file);
        arena_free(a);
        return 1;
    }
    SrcLine *lines = NULL;
    size_t nlines = 0;
    split_lines(a, src, n, &lines, &nlines);

    Buf body;
    buf_init(&body, a);
    int indent_lang = is_indent_lang(cfg.file);
    int missing = 0;
    int parts = 0;
    for (size_t i = 0; i < cfg.names.len; i++) {
        if (parts) {
            buf_append_char(&body, '\n');
        }
        int ok = indent_lang ? emit_indent_name(&body, lines, nlines, cfg.names.items[i], a)
                             : emit_brace_name(&body, src, n, lines, nlines, cfg.names.items[i], a);
        if (!ok) {
            fprintf(stderr, "error: name not found: %s\n", cfg.names.items[i]);
            missing = 1;
        } else {
            parts++;
        }
    }
    for (size_t i = 0; i < cfg.line_specs.len; i++) {
        size_t lo = 0, hi = 0;
        if (!parse_range(cfg.line_specs.items[i], &lo, &hi)) {
            fprintf(stderr, "error: bad --lines range %s\n", cfg.line_specs.items[i]);
            arena_free(a);
            return 2;
        }
        if (parts) {
            buf_append_char(&body, '\n');
        }
        emit_lines(&body, lines, nlines, lo, hi);
        parts++;
    }

    Buf out;
    buf_init(&out, a);
    buf_append_str(&out, "# slice\n");
    buf_append_fmt(&out, "# file: %s\n\n", uvs_rel_slash(a, cfg.file));
    buf_append(&out, body.data ? body.data : "", body.len);
    buf_null_terminate(&out);
    int ok = write_out(cfg.out_path, out.data ? out.data : "", out.len);
    arena_free(a);
    if (missing) {
        return 1;
    }
    return ok ? 0 : 1;
}
