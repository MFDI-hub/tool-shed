#include "uvs.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t tail;
    size_t max_bytes;
    char **cmd;
} Config;

static void usage(FILE *fp) {
    fprintf(fp,
            "Usage: quiet_exec [options] -- command [args...]\n"
            "\n"
            "Run a command, print a shrunk stdout+stderr for an AI agent, exit with\n"
            "the command status. Not watch_run (no watch) and not a repo dump.\n"
            "\n"
            "  --tail N        Keep this many non-priority lines (default: 80)\n"
            "  --max-bytes N   Truncate kept output\n"
            "  -h, --help\n");
}

static size_t parse_size(const char *s) {
    return (size_t)strtoull(s, NULL, 10);
}

static void parse_args(Config *cfg, int argc, char **argv) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->tail = 80;
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
        if (uvs_flag_eq(arg, "--tail")) {
            cfg->tail = parse_size(uvs_consume_value(&i, argc, argv, "--tail"));
            continue;
        }
        if (uvs_flag_eq(arg, "--max-bytes")) {
            cfg->max_bytes = parse_size(uvs_consume_value(&i, argc, argv, "--max-bytes"));
            continue;
        }
        fprintf(stderr, "error: unknown option %s\n", arg);
        exit(2);
    }
    if (cmd_at < 0 || cmd_at >= argc) {
        fprintf(stderr, "error: missing command (put it after --)\n");
        usage(stderr);
        exit(2);
    }
    cfg->cmd = argv + cmd_at;
}

static int ci_has(const char *s, const char *needle) {
    size_t n = strlen(needle);
    for (const char *p = s; *p; p++) {
        size_t i = 0;
        while (i < n) {
            unsigned char a = (unsigned char)p[i];
            unsigned char b = (unsigned char)needle[i];
            if (a >= 'A' && a <= 'Z') {
                a = (unsigned char)(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = (unsigned char)(b - 'A' + 'a');
            }
            if (a != b) {
                break;
            }
            i++;
        }
        if (i == n) {
            return 1;
        }
    }
    return 0;
}

static int is_keep(const char *s) {
    return ci_has(s, "fail") || ci_has(s, "error") || ci_has(s, "fatal") || ci_has(s, "traceback") ||
           ci_has(s, "assert") || ci_has(s, "panic") || strncmp(s, "E ", 2) == 0 || strncmp(s, "===", 3) == 0 ||
           strncmp(s, "--- FAIL", 8) == 0;
}

static int is_drop(const char *s) {
    if (!s[0]) {
        return 1;
    }
    if (ci_has(s, "PASSED") || ci_has(s, "SKIPPED")) {
        return 1;
    }
    if (strcmp(s, "ok") == 0 || strcmp(s, "OK") == 0) {
        return 1;
    }
    int only_dots = 1;
    for (const char *p = s; *p; p++) {
        if (*p != '.' && *p != 'F' && *p != 's' && *p != 'x' && *p != ' ') {
            only_dots = 0;
            break;
        }
    }
    if (only_dots && strchr(s, '.')) {
        return 1;
    }
    return 0;
}

static const char *lstrip(const char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r') {
        s++;
    }
    return s;
}

int main(int argc, char **argv) {
    char **utf8_argv = uvs_cmdline_argv(&argc);
    if (utf8_argv) {
        argv = utf8_argv;
    }
    Config cfg;
    parse_args(&cfg, argc, argv);
    Arena *a = arena_new();
    char *raw = NULL;
    size_t raw_len = 0;
    int rc = uvs_run_capture_argv(a, cfg.cmd, &raw, &raw_len);
    if (!raw) {
        raw = arena_strdup(a, "");
    }

    typedef struct {
        const char *text;
        int keep;
    } Line;
    Line *lines = NULL;
    size_t nlines = 0;
    size_t cap = 0;
    const char *p = raw;
    const char *end = raw + raw_len;
    const char *prev = NULL;
    size_t dup = 0;
    while (p < end || (p == end && raw_len && end > raw && end[-1] != '\n' && p == end)) {
        if (p >= end) {
            break;
        }
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        size_t ln = eol ? (size_t)(eol - p) : (size_t)(end - p);
        char *line = arena_strndup(a, p, ln);
        if (ln && line[ln - 1] == '\r') {
            line[ln - 1] = 0;
        }
        p = eol ? eol + 1 : end;
        const char *stripped = lstrip(line);
        if (is_drop(stripped) && !is_keep(stripped)) {
            continue;
        }
        if (prev && strcmp(prev, stripped) == 0) {
            dup++;
            continue;
        }
        if (dup >= 2 && prev) {
            if (nlines + 1 > cap) {
                size_t ncap = cap ? cap * 2 : 32;
                Line *nl = arena_alloc(a, ncap * sizeof(*nl));
                if (lines) {
                    memcpy(nl, lines, nlines * sizeof(*nl));
                }
                lines = nl;
                cap = ncap;
            }
            lines[nlines].text = arena_sprintf(a, "... (%zu more)", dup);
            lines[nlines].keep = 0;
            nlines++;
        }
        dup = 0;
        prev = stripped;
        if (nlines + 1 > cap) {
            size_t ncap = cap ? cap * 2 : 32;
            Line *nl = arena_alloc(a, ncap * sizeof(*nl));
            if (lines) {
                memcpy(nl, lines, nlines * sizeof(*nl));
            }
            lines = nl;
            cap = ncap;
        }
        lines[nlines].text = stripped;
        lines[nlines].keep = is_keep(stripped);
        nlines++;
    }
    if (dup >= 2 && prev) {
        if (nlines + 1 > cap) {
            size_t ncap = cap ? cap * 2 : 32;
            Line *nl = arena_alloc(a, ncap * sizeof(*nl));
            if (lines) {
                memcpy(nl, lines, nlines * sizeof(*nl));
            }
            lines = nl;
            cap = ncap;
        }
        lines[nlines].text = arena_sprintf(a, "... (%zu more)", dup);
        lines[nlines].keep = 0;
        nlines++;
    }

    size_t rest = 0;
    for (size_t i = 0; i < nlines; i++) {
        if (!lines[i].keep) {
            rest++;
        }
    }
    size_t skip_rest = 0;
    if (cfg.tail && rest > cfg.tail) {
        skip_rest = rest - cfg.tail;
    }
    size_t seen_rest = 0;
    Buf out;
    buf_init(&out, a);
    for (size_t i = 0; i < nlines; i++) {
        if (!lines[i].keep) {
            if (seen_rest < skip_rest) {
                seen_rest++;
                continue;
            }
            seen_rest++;
        }
        buf_append_str(&out, lines[i].text);
        buf_append_char(&out, '\n');
        if (cfg.max_bytes && out.len > cfg.max_bytes) {
            out.len = cfg.max_bytes;
            break;
        }
    }
    buf_null_terminate(&out);
    fwrite(out.data ? out.data : "", 1, out.len, stdout);
    arena_free(a);
    return rc;
}
