/*
 * Walkthrough of every function in the ediFabric Native X12 C ABI.
 *
 * Each section calls one group of entry points and prints the result, so the
 * output doubles as a reference for what each call returns.
 *
 * Usage:
 *   ./example_all_functions
 *   ./example_all_functions --serial YOUR_SERIAL
 *   ./example_all_functions --lib /path/to/edifabric-x12-tools.dll
 */

#ifdef _MSC_VER
#  define _CRT_SECURE_NO_WARNINGS
#endif

#include "edifabric_x12.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <limits.h>
#  include <unistd.h>
#endif

/* Free plan serial published in the ediFabric Native documentation. */
static const char *DEFAULT_SERIAL = "bd96a836feca45cb91c86ee65d281f52";

static char g_root[1024];
static char *g_sample_edi;
static char *g_sample_edi_invalid;

/* Validation levels: 0 none; 1 syntax; 2 limits/codes; 3 balancing; 4 inter-segment.
 * Levels match HIPAA SNIP validation levels. */
static const char *PARSE_CONFIG =
    "{"
    "\"validate\":{"
    "\"regex\":null,\"date_format\":null,\"time_format\":null,"
    "\"skip_seq_count\":false,\"skip_hl_seq\":false,"
    "\"snip_level\":4,\"max_errors\":100"
    "},"
    "\"ack\":{"
    "\"supress_ta1\":false,\"ak901p\":false,"
    "\"gen_for_valid\":true,\"gen997\":false"
    "}"
    "}";

static const char *SPLIT_CONFIG =
    "{"
    "\"validate\":{"
    "\"regex\":null,\"date_format\":null,\"time_format\":null,"
    "\"skip_seq_count\":false,\"skip_hl_seq\":false,"
    "\"snip_level\":4,\"max_errors\":100"
    "},"
    "\"ack\":{"
    "\"supress_ta1\":false,\"ak901p\":false,"
    "\"gen_for_valid\":true,\"gen997\":false"
    "},"
    "\"split\":{\"segment_id\":\"LX\",\"segment_depth\":6,\"loop_id\":\"2400\"}"
    "}";

/* ---------------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------------- */

static void section(const char *title)
{
    printf("\n");
    printf("======================================================================\n");
    printf("%s\n", title);
    printf("======================================================================\n");
}

static void preview(const char *text, int length, int limit)
{
    int n = length;
    if (n < 0)
        n = text ? (int)strlen(text) : 0;
    if (n > limit) {
        fwrite(text, 1, (size_t)limit, stdout);
        printf(" ...");
    } else if (text && n > 0) {
        fwrite(text, 1, (size_t)n, stdout);
    }
}

static void die_rc(int rc, const char *api)
{
    char *message = ef_get_error(rc);
    fprintf(stderr, "\n%s: error %d: %s\n", api, rc, message ? message : "(unknown)");
    ef_free(message);
}

static int check(int rc, const char *api)
{
    if (rc == EF_SUCCESS)
        return 0;
    die_rc(rc, api);
    return -1;
}

static char *read_file(const char *path, int *out_length)
{
    FILE *fp;
    long size;
    char *data;

    fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    data = (char *)malloc((size_t)size + 1);
    if (!data) {
        fclose(fp);
        return NULL;
    }
    if (fread(data, 1, (size_t)size, fp) != (size_t)size) {
        free(data);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    data[size] = '\0';
    if (out_length)
        *out_length = (int)size;
    return data;
}

static void join_root(char *out, size_t out_size, const char *relative)
{
#ifdef _WIN32
    snprintf(out, out_size, "%s\\%s", g_root, relative);
#else
    snprintf(out, out_size, "%s/%s", g_root, relative);
#endif
}

static void resolve_root(const char *argv0)
{
    char path[1024];
    char *slash;

    g_root[0] = '\0';

#ifdef _WIN32
    if (GetModuleFileNameA(NULL, path, (DWORD)sizeof path) > 0) {
        slash = strrchr(path, '\\');
        if (!slash)
            slash = strrchr(path, '/');
        if (slash) {
            *slash = '\0';
            /* Prefer repo root when running from a build/ subfolder. */
            {
                char probe[1024];
                snprintf(probe, sizeof probe, "%s\\edi\\837p.txt", path);
                if (GetFileAttributesA(probe) != INVALID_FILE_ATTRIBUTES) {
                    snprintf(g_root, sizeof g_root, "%s", path);
                    return;
                }
                snprintf(probe, sizeof probe, "%s\\..\\edi\\837p.txt", path);
                if (GetFileAttributesA(probe) != INVALID_FILE_ATTRIBUTES) {
                    snprintf(g_root, sizeof g_root, "%s\\..", path);
                    return;
                }
            }
            snprintf(g_root, sizeof g_root, "%s", path);
            return;
        }
    }
#else
    (void)argv0;
    if (getcwd(path, sizeof path)) {
        char probe[1024];
        snprintf(probe, sizeof probe, "%s/edi/837p.txt", path);
        if (access(probe, R_OK) == 0) {
            snprintf(g_root, sizeof g_root, "%s", path);
            return;
        }
    }
#endif

    snprintf(g_root, sizeof g_root, ".");
}

static char *json_escape(const char *input)
{
    size_t n = 0;
    size_t i;
    char *out;
    size_t o = 0;

    for (i = 0; input[i]; ++i)
        n += (input[i] == '\\' || input[i] == '"') ? 2 : 1;
    out = (char *)malloc(n + 1);
    if (!out)
        return NULL;
    for (i = 0; input[i]; ++i) {
        if (input[i] == '\\' || input[i] == '"')
            out[o++] = '\\';
        out[o++] = input[i];
    }
    out[o] = '\0';
    return out;
}

/* Build a map JSON with absolute model locations under map/. */
static char *build_local_map_json(void)
{
    char map_dir[1024];
    char *escaped;
    char *json;
    size_t cap;

    join_root(map_dir, sizeof map_dir, "map");
    escaped = json_escape(map_dir);
    if (!escaped)
        return NULL;

    cap = strlen(escaped) * 2 + 512;
    json = (char *)malloc(cap);
    if (!json) {
        free(escaped);
        return NULL;
    }

    snprintf(json, cap,
             "{"
             "\"default\":\"\","
             "\"maps\":{"
             "\"837:005010X222A1\":{\"type\":1,\"name\":\"model837P.json\",\"location\":\"%s\"},"
             "\"834:005010X220A1\":{\"type\":1,\"name\":\"model834.json\",\"location\":\"%s\"}"
             "}"
             "}",
             escaped, escaped);
    free(escaped);
    return json;
}

static char *build_online_map_json(const char *serial)
{
    char *escaped;
    char *json;
    size_t cap;

    escaped = json_escape(serial);
    if (!escaped)
        return NULL;
    cap = strlen(escaped) + 64;
    json = (char *)malloc(cap);
    if (!json) {
        free(escaped);
        return NULL;
    }
    snprintf(json, cap, "{\"default\":\"%s\",\"maps\":{}}", escaped);
    free(escaped);
    return json;
}

/* ---------------------------------------------------------------------------
 * Demos
 * --------------------------------------------------------------------------- */

static void demo_errors(void)
{
    int codes[] = {
        EF_ERR_INSUFFICIENT_CAPACITY,
        EF_ERR_MAP_NOT_SET,
        EF_ERR_LICENSE_NOT_SET,
    };
    size_t i;

    section("Error messages: get_error, free_error");

    for (i = 0; i < sizeof codes / sizeof codes[0]; ++i) {
        char *message = ef_get_error(codes[i]);
        printf("  get_error(%3d) -> %s\n", codes[i], message ? message : "(null)");
        ef_free(message);
    }

    {
        char *pointer = ef_raw_get_error(EF_ERR_TOKEN_EXPIRED);
        printf("  raw get_error(631) -> %s\n", pointer ? pointer : "(null)");
        ef_free_error(pointer);
        printf("  free_error(pointer) released the string\n");
    }
}

static int demo_logging(const char *log_path)
{
    int version = 0;

    section("Lifecycle: init_logger, get_app_version");

    if (check(ef_init_logger(log_path, EF_LOG_TRACE), "init_logger") != 0)
        return -1;
    printf("  init_logger -> logging to %s\n", log_path);

    if (check(ef_get_app_version(&version), "get_app_version") != 0)
        return -1;
    printf("  get_app_version -> %d\n", version);
    return 0;
}

static int demo_set_online_map(const char *serial)
{
    char *map_json;

    section("Model map: set_map online, default is the serial key");

    map_json = build_online_map_json(serial);
    if (!map_json) {
        fprintf(stderr, "out of memory building online map\n");
        return -1;
    }
    if (check(ef_set_map(map_json), "set_map") != 0) {
        free(map_json);
        return -1;
    }
    printf("  set_map -> default=%s, local maps=(none)\n", serial);
    free(map_json);
    return 0;
}

static int demo_set_local_map(void)
{
    char *map_json;

    section("Model map: set_map local, default is blank");

    map_json = build_local_map_json();
    if (!map_json) {
        fprintf(stderr, "out of memory building local map\n");
        return -1;
    }
    if (check(ef_set_map(map_json), "set_map") != 0) {
        free(map_json);
        return -1;
    }
    printf("  set_map <- map/map.json (locations rewritten to absolute map/)\n");
    printf("  default='', local maps=837:005010X222A1, 834:005010X220A1\n");
    free(map_json);
    return 0;
}

static int demo_parse(ef_buffer *transactions_out)
{
    ef_parse_result result;

    section("Parse: parse (mode 1, JSON only)");

    memset(&result, 0, sizeof result);
    if (check(ef_parse(g_sample_edi, EF_PARSE_JSON, NULL, &result), "parse") != 0)
        return -1;

    printf("  %d bytes of JSON, offset=%d\n", result.output.length, result.offset);
    printf("  ");
    preview((const char *)result.output.data, result.output.length, 400);
    printf("\n");

    *transactions_out = result.output; /* caller owns */
    return 0;
}

static int demo_parse_validate(void)
{
    ef_parse_result result;

    section("Parse: parse (mode 2, JSON + validation report)");

    memset(&result, 0, sizeof result);
    if (check(ef_parse(g_sample_edi_invalid, EF_PARSE_JSON_VALIDATE, PARSE_CONFIG, &result), "parse") != 0)
        return -1;

    printf("  %d bytes total, validation starts at offset %d\n", result.output.length, result.offset);
    printf("  validation -> ");
    if (result.offset >= 0 && result.offset <= result.output.length) {
        preview((const char *)result.output.data + result.offset,
                result.output.length - result.offset, 400);
    }
    printf("\n");
    ef_free(result.output.data);
    return 0;
}

static int demo_parse_ack(const char *edi, const char *label)
{
    ef_parse_result result;

    section(label);

    memset(&result, 0, sizeof result);
    if (check(ef_parse(edi, EF_PARSE_JSON_VALIDATE_ACK, PARSE_CONFIG, &result), "parse") != 0)
        return -1;

    printf("  %d bytes total, report starts at offset %d\n", result.output.length, result.offset);
    printf("  report -> ");
    if (result.offset >= 0 && result.offset <= result.output.length) {
        preview((const char *)result.output.data + result.offset,
                result.output.length - result.offset, 600);
    }
    printf("\n");
    ef_free(result.output.data);
    return 0;
}

static int demo_split(void)
{
    ef_split_step step;
    int step_no = 0;

    section("Split: start_split, split, get_result");

    if (check(ef_start_split(g_sample_edi, EF_PARSE_JSON, SPLIT_CONFIG), "start_split") != 0)
        return -1;

    for (;;) {
        ef_buffer payload;
        memset(&step, 0, sizeof step);
        if (check(ef_split(&step), "split") != 0)
            return -1;
        ++step_no;
        printf("  step %d: size=%d offset=%d last=%d\n", step_no, step.size, step.offset, step.is_last);

        memset(&payload, 0, sizeof payload);
        if (step.size > 0) {
            if (check(ef_get_result(step.size, &payload), "get_result") != 0)
                return -1;
            printf("    ");
            preview((const char *)payload.data, payload.length, 160);
            printf("\n");
            ef_free(payload.data);
        }
        if (step.is_last)
            break;
    }
    return 0;
}

static int demo_build(const ef_buffer *transactions)
{
    ef_buffer edi;

    section("Build: build");

    memset(&edi, 0, sizeof edi);
    if (check(ef_build_bytes(transactions->data, transactions->length, "\r\n", &edi), "build") != 0)
        return -1;

    printf("  %d bytes of X12\n", edi.length);
    {
        int i;
        printf("  ");
        for (i = 0; i < edi.length; ++i) {
            unsigned char c = edi.data[i];
            if (c == '\r')
                continue;
            if (c == '\n') {
                printf("\n  ");
                continue;
            }
            fputc(c, stdout);
        }
        printf("\n");
    }
    ef_free(edi.data);
    return 0;
}

static int demo_merge(const ef_buffer *transactions)
{
    int count = 0;

    section("Merge: start_merge, merge, get_result");

    if (check(ef_start_merge_bytes(transactions->data, transactions->length), "start_merge") != 0)
        return -1;

    for (;;) {
        int size = 0;
        ef_buffer segment;
        if (check(ef_merge(&size), "merge") != 0)
            return -1;
        if (size == 0)
            break;
        memset(&segment, 0, sizeof segment);
        if (check(ef_get_result(size, &segment), "get_result") != 0)
            return -1;
        ++count;
        printf("  segment %2d: ", count);
        fwrite(segment.data, 1, (size_t)segment.length, stdout);
        printf("\n");
        ef_free(segment.data);
    }
    printf("  merge produced %d segments\n", count);
    return 0;
}

static int demo_teardown(void)
{
    section("Teardown: clear_cache, shutdown_logger");

    if (check(ef_clear_cache(), "clear_cache") != 0)
        return -1;
    printf("  clear_cache -> map, license, stream state and results reset\n");

    if (check(ef_shutdown_logger(), "shutdown_logger") != 0)
        return -1;
    printf("  shutdown_logger -> logger stopped\n");
    return 0;
}

/* ---------------------------------------------------------------------------
 * CLI
 * --------------------------------------------------------------------------- */

typedef struct {
    const char *serial;
    const char *lib;
} options;

static void print_usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s [--serial SERIAL] [--lib PATH]\n"
            "  --serial   license serial (default: EDIFABRIC_SERIAL or the free plan serial)\n"
            "  --lib      path to edifabric-x12-tools.dll/.so/.dylib or its folder\n",
            argv0);
}

static int parse_args(int argc, char **argv, options *out)
{
    int i;
    out->serial = NULL;
    out->lib = NULL;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--serial") == 0 && i + 1 < argc) {
            out->serial = argv[++i];
        } else if (strcmp(argv[i], "--lib") == 0 && i + 1 < argc) {
            out->lib = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 1;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    options opts;
    const char *serial;
    const char *env_serial;
    char edi_path[1024];
    char invalid_path[1024];
    char log_path[1024];
    ef_buffer transactions;
    int exit_code = 0;
    int arg_rc;

    arg_rc = parse_args(argc, argv, &opts);
    if (arg_rc != 0)
        return arg_rc > 0 ? 0 : 1;

    resolve_root(argv[0]);

    env_serial = getenv("EDIFABRIC_SERIAL");
    serial = opts.serial ? opts.serial : (env_serial && env_serial[0] ? env_serial : DEFAULT_SERIAL);

    join_root(edi_path, sizeof edi_path, "edi/837p.txt");
    join_root(invalid_path, sizeof invalid_path, "edi/837p_error.txt");
    join_root(log_path, sizeof log_path, "edifabric.log");

    g_sample_edi = read_file(edi_path, NULL);
    g_sample_edi_invalid = read_file(invalid_path, NULL);
    if (!g_sample_edi || !g_sample_edi_invalid) {
        fprintf(stderr, "Failed to read sample EDI under %s (tried %s)\n", g_root, edi_path);
        free(g_sample_edi);
        free(g_sample_edi_invalid);
        return 1;
    }

    section("Load: ef_load_library");
    if (ef_load_library(opts.lib) != 0) {
        fprintf(stderr, "  %s\n", ef_last_load_error() ? ef_last_load_error() : "load failed");
        free(g_sample_edi);
        free(g_sample_edi_invalid);
        return 1;
    }
    printf("  loaded %s\n", ef_library_path() ? ef_library_path() : "(unknown)");

    demo_errors();

    memset(&transactions, 0, sizeof transactions);

    if (demo_logging(log_path) != 0) {
        exit_code = 1;
        goto done;
    }

    /* Free and developer licenses authenticate with set_serial only. */
    section("Licensing: set_serial");
    if (check(ef_set_serial(serial), "set_serial") != 0) {
        exit_code = 1;
        goto done;
    }
    printf("  set_serial -> ok\n");

    if (demo_set_local_map() != 0 ||
        demo_parse(&transactions) != 0 ||
        demo_set_online_map(serial) != 0 ||
        demo_parse_validate() != 0 ||
        demo_parse_ack(g_sample_edi, "Parse: parse (mode 3, JSON + validation + acknowledgment)") != 0 ||
        demo_parse_ack(g_sample_edi_invalid,
                       "Parse: parse (mode 3, JSON + validation + acknowledgment) [invalid sample]") != 0 ||
        demo_split() != 0 ||
        demo_build(&transactions) != 0 ||
        demo_merge(&transactions) != 0) {
        fprintf(stderr,
                "Check that the serial is valid and that map/map.json resolves the "
                "transaction set to a local model file under map/.\n");
        exit_code = 1;
        goto done;
    }

    if (demo_teardown() != 0)
        exit_code = 1;

done:
    /* Always release the log file handle (Windows). */
    ef_shutdown_logger();

    ef_free(transactions.data);
    free(g_sample_edi);
    free(g_sample_edi_invalid);

    if (exit_code != 0)
        return exit_code;

    section("Finished");
    printf("Every entry point in c-abi-edifabric_x12_tools.h was called.\n");
    return 0;
}
