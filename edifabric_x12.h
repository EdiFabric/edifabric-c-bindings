/*
 * Convenience C wrappers for ediFabric Native X12
 * (see c-abi-edifabric_x12_tools.h).
 *
 * Loads the platform shared library (edifabric-x12-tools.dll / .so / .dylib)
 * and handles grow-and-retry. All payloads are UTF-8. Native return codes:
 *   0 = success
 *   1 = InsufficientCapacity (retried internally by these wrappers)
 *   anything else = error; use ef_get_error / ef_check.
 *
 * Call ef_load_library() once (or let the first wrapper call load it).
 * Free every ef_buffer / heap string with ef_free().
 */

#ifndef EDIFABRIC_X12_H
#define EDIFABRIC_X12_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EF_SUCCESS = 0,
    EF_INSUFFICIENT_CAPACITY = 1
};

typedef enum {
    EF_PARSE_JSON = 1,
    EF_PARSE_JSON_VALIDATE = 2,
    EF_PARSE_JSON_VALIDATE_ACK = 3
} ef_parse_mode;

typedef enum {
    EF_LOG_TRACE = 0,
    EF_LOG_DEBUG = 1,
    EF_LOG_INFORMATION = 2,
    EF_LOG_WARNING = 3,
    EF_LOG_ERROR = 4,
    EF_LOG_ALL = 5
} ef_log_level;

typedef enum {
    EF_ERR_INSUFFICIENT_CAPACITY = 1,
    EF_ERR_UNKNOWN = 501,
    EF_ERR_NO_CONNECTION = 502,
    EF_ERR_INVALID_MAP = 503,
    EF_ERR_INCORRECT_INPUT = 611,
    EF_ERR_LOGGER_INIT = 612,
    EF_ERR_MAP_DESERIALIZE = 613,
    EF_ERR_INCORRECT_CAPACITY = 614,
    EF_ERR_MAP_NOT_SET = 615,
    EF_ERR_INCORRECT_MODE = 616,
    EF_ERR_NO_JSON = 617,
    EF_ERR_VALIDATION_UNAVAILABLE = 618,
    EF_ERR_VALIDATION_SERIALIZE = 619,
    EF_ERR_INCORRECT_TOKEN = 620,
    EF_ERR_CONFIG_DESERIALIZE = 621,
    EF_ERR_SPLIT_SEGMENT_ID_MISSING = 622,
    EF_ERR_SPLIT_NOT_STARTED = 623,
    EF_ERR_NO_RESULT = 624,
    EF_ERR_RESULT_SIZE_MISMATCH = 625,
    EF_ERR_MERGE_NOT_STARTED = 626,
    EF_ERR_INCORRECT_OUTPUT_POINTER = 627,
    EF_ERR_INCORRECT_SERIAL = 628,
    EF_ERR_LICENSE_NOT_INSTALLED = 629,
    EF_ERR_APP_VERSION_EXCEEDED = 630,
    EF_ERR_TOKEN_EXPIRED = 631,
    EF_ERR_TOKEN_MISSING = 632,
    EF_ERR_MAX_LICENSES_EXCEEDED = 633,
    EF_ERR_LICENSE_SNAPSHOT_MISSING = 634,
    EF_ERR_LICENSE_NOT_SET = 635,
    EF_ERR_RATE_EXCEEDED = 636,
    EF_ERR_INVALID_JSON = 637,
    EF_ERR_INCORRECT_LICENSE = 638
} ef_error_code;

/* Heap buffer returned by parse / build / get_token / get_result helpers. */
typedef struct {
    unsigned char *data; /* UTF-8 bytes; free with ef_free */
    int length;
} ef_buffer;

typedef struct {
    ef_buffer output;
    int offset; /* 0 in mode 1; start of validation/ack JSON in modes 2 and 3 */
} ef_parse_result;

typedef struct {
    int size;
    int offset;
    int is_last; /* non-zero when this is the final split step */
} ef_split_step;

/*
 * Load the native library.
 * path may be the library file, its folder, or NULL to search
 * EDIFABRIC_X12_LIB, then this process directory, then the cwd.
 * Returns 0 on success, -1 on failure (see ef_last_load_error).
 */
int ef_load_library(const char *path);

/* Path of the library that was loaded, or NULL before a successful load. */
const char *ef_library_path(void);

/* Last dynamic-load failure message, or NULL. */
const char *ef_last_load_error(void);

/* Release a buffer allocated by these wrappers (safe with NULL). */
void ef_free(void *pointer);

/* Message for an error code. Caller must ef_free() the returned string. */
char *ef_get_error(int error_code);

/*
 * Free a pointer returned by the raw get_error export.
 * Prefer ef_get_error(), which already frees the native string.
 */
void ef_free_error(char *pointer);

/* Raw ABI get_error — caller owns the pointer and must ef_free_error it. */
char *ef_raw_get_error(int error_code);

/* ---------------------------------------------------------------------------
 * Lifecycle / logging
 * --------------------------------------------------------------------------- */

int ef_init_logger(const char *path, ef_log_level min_level);
int ef_shutdown_logger(void);
int ef_clear_cache(void);

/* ---------------------------------------------------------------------------
 * Licensing
 * --------------------------------------------------------------------------- */

/* Cache a token for runtime authorization; refresh it if it expires within `seconds`. */
int ef_ensure_token(const char *serial, int seconds);
int ef_get_app_version(int *app_version);
int ef_get_token(const char *serial, ef_buffer *out);
int ef_validate_token(const char *token);
int ef_set_token(const char *token);
int ef_get_token_expiration(int64_t *expiration_utc);
int ef_set_serial(const char *serial);

/* ---------------------------------------------------------------------------
 * Model map
 * --------------------------------------------------------------------------- */

int ef_set_map(const char *map_json);
int ef_set_map_bytes(const unsigned char *map_json, int length);

/* ---------------------------------------------------------------------------
 * Parse / split / build / merge
 * --------------------------------------------------------------------------- */

int ef_parse(const char *edi, ef_parse_mode mode, const char *config, ef_parse_result *out);
int ef_parse_bytes(const unsigned char *edi, int edi_length,
                   ef_parse_mode mode,
                   const unsigned char *config, int config_length,
                   ef_parse_result *out);

int ef_start_split(const char *edi, ef_parse_mode mode, const char *config);
int ef_start_split_bytes(const unsigned char *edi, int edi_length,
                         ef_parse_mode mode,
                         const unsigned char *config, int config_length);
int ef_split(ef_split_step *step);

int ef_build(const char *json_text, const char *postfix, ef_buffer *out);
int ef_build_bytes(const unsigned char *json_text, int json_length,
                   const char *postfix, ef_buffer *out);

int ef_start_merge(const char *json_text);
int ef_start_merge_bytes(const unsigned char *json_text, int json_length);
int ef_merge(int *result_size);

int ef_get_result(int size, ef_buffer *out);

#ifdef __cplusplus
}
#endif

#endif /* EDIFABRIC_X12_H */
