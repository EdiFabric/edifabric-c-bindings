/*
 * Implementation of the ediFabric Native X12 convenience wrappers.
 * Dynamically loads edifabric-x12-tools and retries on InsufficientCapacity.
 */

#ifdef _MSC_VER
#  define _CRT_SECURE_NO_WARNINGS
#endif

#include "edifabric_x12.h"
#include "c-abi-edifabric_x12_tools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include <limits.h>
#  include <unistd.h>
#endif

/* ---------------------------------------------------------------------------
 * Function pointers into the shared library
 * --------------------------------------------------------------------------- */

typedef int (*fn_init_logger)(const unsigned char *, int, int);
typedef int (*fn_shutdown_logger)(void);
typedef int (*fn_clear_cache)(void);
typedef int (*fn_ensure_token)(const unsigned char *, int, int);
typedef int (*fn_get_app_version)(int *);
typedef int (*fn_get_token)(const unsigned char *, int, unsigned char *, int, int *);
typedef int (*fn_validate_token)(const unsigned char *, int);
typedef int (*fn_set_token)(const unsigned char *, int);
typedef int (*fn_get_token_expiration)(int64_t *);
typedef int (*fn_set_serial)(const unsigned char *, int);
typedef int (*fn_set_map)(const unsigned char *, int);
typedef int (*fn_parse)(const unsigned char *, int, int, const unsigned char *, int,
                        unsigned char *, int, int *, int *);
typedef int (*fn_start_split)(const unsigned char *, int, int, const unsigned char *, int);
typedef int (*fn_split)(int *, int *, unsigned char *);
typedef int (*fn_build)(const unsigned char *, int, const char *, unsigned char *, int, int *);
typedef int (*fn_start_merge)(const unsigned char *, int);
typedef int (*fn_merge)(int *);
typedef int (*fn_get_result)(unsigned char *, int);
typedef char *(*fn_get_error)(int);
typedef void (*fn_free_error)(char *);

typedef struct {
#ifdef _WIN32
    HMODULE handle;
#else
    void *handle;
#endif
    char path[1024];
    fn_init_logger init_logger;
    fn_shutdown_logger shutdown_logger;
    fn_clear_cache clear_cache;
    fn_ensure_token ensure_token;
    fn_get_app_version get_app_version;
    fn_get_token get_token;
    fn_validate_token validate_token;
    fn_set_token set_token;
    fn_get_token_expiration get_token_expiration;
    fn_set_serial set_serial;
    fn_set_map set_map;
    fn_parse parse;
    fn_start_split start_split;
    fn_split split;
    fn_build build;
    fn_start_merge start_merge;
    fn_merge merge;
    fn_get_result get_result;
    fn_get_error get_error;
    fn_free_error free_error; /* optional */
} ef_library;

static ef_library g_lib;
static char g_load_error[512];

static void set_load_error(const char *message)
{
    if (!message) {
        g_load_error[0] = '\0';
        return;
    }
    snprintf(g_load_error, sizeof g_load_error, "%s", message);
}

#ifdef _WIN32
static void *sym(HMODULE handle, const char *name)
{
    return (void *)GetProcAddress(handle, name);
}
#else
static void *sym(void *handle, const char *name)
{
    return dlsym(handle, name);
}
#endif

static const char *library_filename(void)
{
#ifdef _WIN32
    return "edifabric-x12-tools.dll";
#elif defined(__APPLE__)
    return "edifabric-x12-tools.dylib";
#else
    return "edifabric-x12-tools.so";
#endif
}

static int path_is_dir(const char *path)
{
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
    /* Treat missing trailing separator as "maybe a file"; caller also tries as-is. */
    size_t n = strlen(path);
    return n > 0 && (path[n - 1] == '/' || path[n - 1] == '\\');
#endif
}

static int try_load_file(const char *path)
{
#ifdef _WIN32
    HMODULE handle = LoadLibraryA(path);
    if (!handle) {
        DWORD err = GetLastError();
        snprintf(g_load_error, sizeof g_load_error, "LoadLibrary(%s) failed (%lu)", path, (unsigned long)err);
        return -1;
    }
#else
    void *handle = dlopen(path, RTLD_NOW);
    if (!handle) {
        snprintf(g_load_error, sizeof g_load_error, "%s", dlerror() ? dlerror() : "dlopen failed");
        return -1;
    }
#endif

    memset(&g_lib, 0, sizeof g_lib);
    g_lib.handle = handle;
    snprintf(g_lib.path, sizeof g_lib.path, "%s", path);

    /* Bind required exports. free_error is optional. */
    g_lib.init_logger = (fn_init_logger)sym(handle, "init_logger");
    g_lib.shutdown_logger = (fn_shutdown_logger)sym(handle, "shutdown_logger");
    g_lib.clear_cache = (fn_clear_cache)sym(handle, "clear_cache");
    g_lib.ensure_token = (fn_ensure_token)sym(handle, "ensure_token");
    g_lib.get_app_version = (fn_get_app_version)sym(handle, "get_app_version");
    g_lib.get_token = (fn_get_token)sym(handle, "get_token");
    g_lib.validate_token = (fn_validate_token)sym(handle, "validate_token");
    g_lib.set_token = (fn_set_token)sym(handle, "set_token");
    g_lib.get_token_expiration = (fn_get_token_expiration)sym(handle, "get_token_expiration");
    g_lib.set_serial = (fn_set_serial)sym(handle, "set_serial");
    g_lib.set_map = (fn_set_map)sym(handle, "set_map");
    g_lib.parse = (fn_parse)sym(handle, "parse");
    g_lib.start_split = (fn_start_split)sym(handle, "start_split");
    g_lib.split = (fn_split)sym(handle, "split");
    g_lib.build = (fn_build)sym(handle, "build");
    g_lib.start_merge = (fn_start_merge)sym(handle, "start_merge");
    g_lib.merge = (fn_merge)sym(handle, "merge");
    g_lib.get_result = (fn_get_result)sym(handle, "get_result");
    g_lib.get_error = (fn_get_error)sym(handle, "get_error");
    g_lib.free_error = (fn_free_error)sym(handle, "free_error");

    if (!g_lib.init_logger || !g_lib.shutdown_logger || !g_lib.clear_cache ||
        !g_lib.ensure_token || !g_lib.get_app_version || !g_lib.get_token ||
        !g_lib.validate_token || !g_lib.set_token || !g_lib.get_token_expiration ||
        !g_lib.set_serial || !g_lib.set_map || !g_lib.parse || !g_lib.start_split ||
        !g_lib.split || !g_lib.build || !g_lib.start_merge || !g_lib.merge ||
        !g_lib.get_result || !g_lib.get_error) {
        set_load_error("native library is missing one or more required exports");
#ifdef _WIN32
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        memset(&g_lib, 0, sizeof g_lib);
        return -1;
    }

    set_load_error(NULL);
    return 0;
}

static void join_path(char *out, size_t out_size, const char *dir, const char *name)
{
    size_t n = strlen(dir);
    int needs_sep = n > 0 && dir[n - 1] != '/' && dir[n - 1] != '\\';
#ifdef _WIN32
    snprintf(out, out_size, needs_sep ? "%s\\%s" : "%s%s", dir, name);
#else
    snprintf(out, out_size, needs_sep ? "%s/%s" : "%s%s", dir, name);
#endif
}

static int try_hint(const char *hint)
{
    char candidate[1024];
    if (!hint || !hint[0])
        return -1;

    if (path_is_dir(hint)) {
        join_path(candidate, sizeof candidate, hint, library_filename());
        if (try_load_file(candidate) == 0)
            return 0;
    }

    return try_load_file(hint);
}

static void exe_directory(char *out, size_t out_size)
{
    out[0] = '\0';
#ifdef _WIN32
    if (GetModuleFileNameA(NULL, out, (DWORD)out_size) == 0) {
        out[0] = '\0';
        return;
    }
    {
        char *slash = strrchr(out, '\\');
        char *slash2 = strrchr(out, '/');
        if (slash2 && (!slash || slash2 > slash))
            slash = slash2;
        if (slash)
            *slash = '\0';
    }
#else
    {
        ssize_t n = readlink("/proc/self/exe", out, out_size - 1);
        if (n < 0) {
            out[0] = '\0';
            return;
        }
        out[n] = '\0';
        {
            char *slash = strrchr(out, '/');
            if (slash)
                *slash = '\0';
        }
    }
#endif
}

int ef_load_library(const char *path)
{
    char candidate[1024];
    char exe_dir[1024];
    const char *env;

    if (g_lib.handle && path == NULL)
        return 0;

    if (g_lib.handle) {
#ifdef _WIN32
        FreeLibrary(g_lib.handle);
#else
        dlclose(g_lib.handle);
#endif
        memset(&g_lib, 0, sizeof g_lib);
    }

    if (try_hint(path) == 0)
        return 0;

    env = getenv("EDIFABRIC_X12_LIB");
    if (try_hint(env) == 0)
        return 0;

    exe_directory(exe_dir, sizeof exe_dir);
    if (exe_dir[0]) {
        join_path(candidate, sizeof candidate, exe_dir, library_filename());
        if (try_load_file(candidate) == 0)
            return 0;
    }

    join_path(candidate, sizeof candidate, ".", library_filename());
    if (try_load_file(candidate) == 0)
        return 0;

    if (!g_load_error[0]) {
        snprintf(g_load_error, sizeof g_load_error,
                 "Could not load %s (tried --lib / EDIFABRIC_X12_LIB / exe dir / cwd)",
                 library_filename());
    }
    return -1;
}

const char *ef_library_path(void)
{
    return g_lib.handle ? g_lib.path : NULL;
}

const char *ef_last_load_error(void)
{
    return g_load_error[0] ? g_load_error : NULL;
}

static ef_library *lib(void)
{
    if (!g_lib.handle && ef_load_library(NULL) != 0)
        return NULL;
    return &g_lib;
}

void ef_free(void *pointer)
{
    free(pointer);
}

void ef_free_error(char *pointer)
{
    ef_library *L;
    if (!pointer)
        return;
    L = lib();
    if (L && L->free_error)
        L->free_error(pointer);
    else
        free(pointer);
}

char *ef_raw_get_error(int error_code)
{
    ef_library *L = lib();
    if (!L)
        return NULL;
    return L->get_error(error_code);
}

char *ef_get_error(int error_code)
{
    char *native;
    char *copy;
    size_t n;

    native = ef_raw_get_error(error_code);
    if (!native) {
        copy = (char *)malloc(64);
        if (copy)
            snprintf(copy, 64, "Unknown error %d", error_code);
        return copy;
    }

    n = strlen(native);
    copy = (char *)malloc(n + 1);
    if (copy)
        memcpy(copy, native, n + 1);
    ef_free_error(native);
    return copy;
}

static int utf8_len(const char *s)
{
    return s ? (int)strlen(s) : 0;
}

static void buffer_clear(ef_buffer *buf)
{
    if (!buf)
        return;
    buf->data = NULL;
    buf->length = 0;
}

typedef int (*grow_call)(unsigned char *buffer, int capacity, int *out_length, void *ctx);

static int with_growth(int capacity, ef_buffer *out, void *ctx, grow_call call)
{
    unsigned char *buffer = NULL;
    int length = 0;
    int rc;

    if (!out)
        return EF_ERR_INCORRECT_OUTPUT_POINTER;
    buffer_clear(out);
    if (capacity < 1)
        capacity = 1;

    for (;;) {
        unsigned char *next = (unsigned char *)realloc(buffer, (size_t)capacity);
        if (!next) {
            free(buffer);
            return EF_ERR_INCORRECT_CAPACITY;
        }
        buffer = next;
        length = 0;
        rc = call(buffer, capacity, &length, ctx);
        if (rc == EF_INSUFFICIENT_CAPACITY) {
            capacity = length > capacity ? length : capacity * 2;
            continue;
        }
        if (rc != EF_SUCCESS) {
            free(buffer);
            return rc;
        }
        out->data = buffer;
        out->length = length;
        return EF_SUCCESS;
    }
}

/* ---------------------------------------------------------------------------
 * Lifecycle
 * --------------------------------------------------------------------------- */

int ef_init_logger(const char *path, ef_log_level min_level)
{
    ef_library *L = lib();
    if (!L)
        return -1;
    return L->init_logger((const unsigned char *)path, utf8_len(path), (int)min_level);
}

int ef_shutdown_logger(void)
{
    ef_library *L = lib();
    if (!L)
        return -1;
    return L->shutdown_logger();
}

int ef_clear_cache(void)
{
    ef_library *L = lib();
    if (!L)
        return -1;
    return L->clear_cache();
}

/* ---------------------------------------------------------------------------
 * Licensing
 * --------------------------------------------------------------------------- */

int ef_ensure_token(const char *serial, int seconds)
{
    ef_library *L = lib();
    if (!L)
        return -1;
    return L->ensure_token((const unsigned char *)serial, utf8_len(serial), seconds);
}

int ef_get_app_version(int *app_version)
{
    ef_library *L = lib();
    if (!L)
        return -1;
    return L->get_app_version(app_version);
}

typedef struct {
    const unsigned char *serial;
    int serial_len;
} get_token_ctx;

static int get_token_call(unsigned char *buffer, int capacity, int *out_length, void *ctx)
{
    get_token_ctx *c = (get_token_ctx *)ctx;
    return lib()->get_token(c->serial, c->serial_len, buffer, capacity, out_length);
}

int ef_get_token(const char *serial, ef_buffer *out)
{
    get_token_ctx ctx;
    if (!lib())
        return -1;
    ctx.serial = (const unsigned char *)serial;
    ctx.serial_len = utf8_len(serial);
    return with_growth(4096, out, &ctx, get_token_call);
}

int ef_validate_token(const char *token)
{
    ef_library *L = lib();
    if (!L)
        return -1;
    return L->validate_token((const unsigned char *)token, utf8_len(token));
}

int ef_set_token(const char *token)
{
    ef_library *L = lib();
    if (!L)
        return -1;
    return L->set_token((const unsigned char *)token, utf8_len(token));
}

int ef_get_token_expiration(int64_t *expiration_utc)
{
    ef_library *L = lib();
    if (!L)
        return -1;
    return L->get_token_expiration(expiration_utc);
}

int ef_set_serial(const char *serial)
{
    ef_library *L = lib();
    if (!L)
        return -1;
    return L->set_serial((const unsigned char *)serial, utf8_len(serial));
}

/* ---------------------------------------------------------------------------
 * Model map
 * --------------------------------------------------------------------------- */

int ef_set_map_bytes(const unsigned char *map_json, int length)
{
    ef_library *L = lib();
    if (!L)
        return -1;
    return L->set_map(map_json, length);
}

int ef_set_map(const char *map_json)
{
    return ef_set_map_bytes((const unsigned char *)map_json, utf8_len(map_json));
}

/* ---------------------------------------------------------------------------
 * Parse
 * --------------------------------------------------------------------------- */

typedef struct {
    const unsigned char *input;
    int input_len;
    int mode;
    const unsigned char *config;
    int config_len;
    int *offset;
} parse_ctx;

static int parse_call(unsigned char *buffer, int capacity, int *out_length, void *ctx)
{
    parse_ctx *c = (parse_ctx *)ctx;
    return lib()->parse(c->input, c->input_len, c->mode, c->config, c->config_len,
                        buffer, capacity, out_length, c->offset);
}

int ef_parse_bytes(const unsigned char *edi, int edi_length,
                   ef_parse_mode mode,
                   const unsigned char *config, int config_length,
                   ef_parse_result *out)
{
    parse_ctx ctx;
    int capacity;
    int rc;

    if (!out)
        return EF_ERR_INCORRECT_OUTPUT_POINTER;
    buffer_clear(&out->output);
    out->offset = 0;
    if (!lib())
        return -1;

    ctx.input = edi;
    ctx.input_len = edi_length;
    ctx.mode = (int)mode;
    ctx.config = config;
    ctx.config_len = config_length;
    ctx.offset = &out->offset;
    capacity = edi_length > 0 ? edi_length * 12 : 4096;
    if (capacity < 4096)
        capacity = 4096;
    rc = with_growth(capacity, &out->output, &ctx, parse_call);
    return rc;
}

int ef_parse(const char *edi, ef_parse_mode mode, const char *config, ef_parse_result *out)
{
    return ef_parse_bytes((const unsigned char *)edi, utf8_len(edi), mode,
                          (const unsigned char *)config, utf8_len(config), out);
}

/* ---------------------------------------------------------------------------
 * Split
 * --------------------------------------------------------------------------- */

int ef_start_split_bytes(const unsigned char *edi, int edi_length,
                         ef_parse_mode mode,
                         const unsigned char *config, int config_length)
{
    ef_library *L = lib();
    if (!L)
        return -1;
    return L->start_split(edi, edi_length, (int)mode, config, config_length);
}

int ef_start_split(const char *edi, ef_parse_mode mode, const char *config)
{
    return ef_start_split_bytes((const unsigned char *)edi, utf8_len(edi), mode,
                                (const unsigned char *)config, utf8_len(config));
}

int ef_split(ef_split_step *step)
{
    unsigned char last = 0;
    ef_library *L = lib();
    if (!L)
        return -1;
    if (!step)
        return EF_ERR_INCORRECT_OUTPUT_POINTER;
    step->size = 0;
    step->offset = 0;
    step->is_last = 0;
    {
        int rc = L->split(&step->size, &step->offset, &last);
        step->is_last = last ? 1 : 0;
        return rc;
    }
}

/* ---------------------------------------------------------------------------
 * Build
 * --------------------------------------------------------------------------- */

typedef struct {
    const unsigned char *input;
    int input_len;
    const char *postfix;
} build_ctx;

static int build_call(unsigned char *buffer, int capacity, int *out_length, void *ctx)
{
    build_ctx *c = (build_ctx *)ctx;
    return lib()->build(c->input, c->input_len, c->postfix, buffer, capacity, out_length);
}

int ef_build_bytes(const unsigned char *json_text, int json_length,
                   const char *postfix, ef_buffer *out)
{
    build_ctx ctx;
    int capacity;
    if (!lib())
        return -1;
    ctx.input = json_text;
    ctx.input_len = json_length;
    ctx.postfix = postfix;
    capacity = json_length > 0 ? json_length : 4096;
    if (capacity < 4096)
        capacity = 4096;
    return with_growth(capacity, out, &ctx, build_call);
}

int ef_build(const char *json_text, const char *postfix, ef_buffer *out)
{
    return ef_build_bytes((const unsigned char *)json_text, utf8_len(json_text), postfix, out);
}

/* ---------------------------------------------------------------------------
 * Merge
 * --------------------------------------------------------------------------- */

int ef_start_merge_bytes(const unsigned char *json_text, int json_length)
{
    ef_library *L = lib();
    if (!L)
        return -1;
    return L->start_merge(json_text, json_length);
}

int ef_start_merge(const char *json_text)
{
    return ef_start_merge_bytes((const unsigned char *)json_text, utf8_len(json_text));
}

int ef_merge(int *result_size)
{
    ef_library *L = lib();
    if (!L)
        return -1;
    if (!result_size)
        return EF_ERR_INCORRECT_OUTPUT_POINTER;
    *result_size = 0;
    return L->merge(result_size);
}

int ef_get_result(int size, ef_buffer *out)
{
    ef_library *L = lib();
    unsigned char *buffer;
    int rc;

    if (!L)
        return -1;
    if (!out)
        return EF_ERR_INCORRECT_OUTPUT_POINTER;
    buffer_clear(out);
    if (size < 0)
        return EF_ERR_INCORRECT_INPUT;
    if (size == 0)
        return EF_SUCCESS;

    buffer = (unsigned char *)malloc((size_t)size);
    if (!buffer)
        return EF_ERR_INCORRECT_CAPACITY;
    rc = L->get_result(buffer, size);
    if (rc != EF_SUCCESS) {
        free(buffer);
        return rc;
    }
    out->data = buffer;
    out->length = size;
    return EF_SUCCESS;
}
