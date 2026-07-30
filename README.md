# ediFabric Native X12 — C bindings

**ediFabric Native** is a self-contained, high-performance X12 EDI native shared library. It converts X12 EDI to JSON (and back),
validates transaction sets, and generates acknowledgments — callable from **any
language with a C foreign-function interface** (C, C++, Rust, Go, Python, Node.js,
Java/JNA, .NET, …). No .NET runtime, JVM, or other dependency is required on the
target machine.

C wrappers and examples for [ediFabric Native](https://www.edifabric.com/edifabric-native.html).

| File | Purpose |
| --- | --- |
| `c-abi-edifabric_x12_tools.h` | raw C ABI exported by the shared library |
| `edifabric_x12.h` / `edifabric_x12.c` | convenience wrappers (dynamic load + grow-and-retry) |
| `example_all_functions.c` | runnable walkthrough of every entry point |
| `CMakeLists.txt` / `Makefile` / `build.bat` | build the example |

## Requirements

- A C99 compiler (MSVC, Clang, or GCC)
- CMake 3.16+ (or Make on Linux/macOS)
- 64-bit Windows, Linux, or macOS
- The native library for your platform:

| Platform | File |
| --- | --- |
| Windows | `edifabric-x12-tools.dll` |
| Linux | `edifabric-x12-tools.so` |
| macOS | `edifabric-x12-tools.dylib` |

[Download **ediFabric Native** Library](https://support.edifabric.com/hc/en-us/articles/37289848931869-Download)

Plus your **model files** (per transaction set) and a **map file** that tells the
engine where to find them. See [Model map](#model-map) for details.

## Getting started

**Download the library** from [here](https://support.edifabric.com/hc/en-us/articles/37289848931869-Download).
Put the native library in the repository root, then build and run the walkthrough:

### CMake (Windows / Linux / macOS)

```bash
cmake -S . -B build
cmake --build build
./build/example_all_functions          # Windows: build\Debug\example_all_functions.exe
```

### Make (Linux / macOS)

```bash
make
./example_all_functions
```

### MSVC (Windows)

From an x64 Native Tools prompt, or by double-running the helper script:

```bat
build.bat
example_all_functions.exe
```

It authorizes with the free plan serial, loads the model map, and calls every
function in the ABI, printing what each one returns.

```
Parse: parse (mode 2, JSON + validation report)
  1754 bytes total, validation starts at offset 1708
  validation -> {"errors":[],"errors_count":0,"data_count":10}
```

Options:

```bash
./example_all_functions --serial YOUR_SERIAL   # use your own license
./example_all_functions --lib /opt/edifabric    # library file or folder
```

The library path is resolved from `--lib`, then `EDIFABRIC_X12_LIB`, then the
executable directory, then the working directory. The serial comes from
`--serial`, then `EDIFABRIC_SERIAL`, then the built-in free plan serial.

All strings and payloads cross the boundary as **UTF‑8 byte buffers**
(`pointer + length`). Every function returns `0` on success or a non-zero
[error code](#error-codes).

## Usage

Copy `edifabric_x12.h`, `edifabric_x12.c`, and `c-abi-edifabric_x12_tools.h` into
your project and link the example the same way (no import library required — the
wrappers load the shared library at runtime).

```c
#include "edifabric_x12.h"
#include <stdio.h>

int main(void)
{
    const char *serial = "your-serial";
    ef_parse_result result;

    if (ef_load_library(NULL) != 0) {
        fprintf(stderr, "%s\n", ef_last_load_error());
        return 1;
    }

    ef_set_serial(serial);   /* or ef_set_token(token) for Enterprise offline use */
    ef_set_map("{\"default\":\"your-serial\",\"maps\":{}}");

    if (ef_parse(edi_text, EF_PARSE_JSON, NULL, &result) != 0)
        return 1;

    fwrite(result.output.data, 1, (size_t)result.output.length, stdout);
    ef_free(result.output.data);
    return 0;
}
```

Read a file from a subfolder of the binary:

```c
/* see read_file() in example_all_functions.c */
char *edi = read_file("edi/837p.txt", NULL);
```

### Validation and acknowledgments

`ef_parse` fills an `ef_parse_result`. In modes 2 and 3, `offset` marks where the
validation and acknowledgment JSON starts inside `output`.

```c
ef_parse_result result;
const char *config =
    "{\"validate\":{\"snip_level\":2,\"max_errors\":0},"
    "\"ack\":{\"gen997\":false,\"supress_ta1\":false}}";

ef_parse(edi, EF_PARSE_JSON_VALIDATE_ACK, config, &result);
/* transactions = output[0 .. offset), report = output[offset .. length) */
ef_free(result.output.data);
```

| Mode | Constant | Output |
| --- | --- | --- |
| 1 | `EF_PARSE_JSON` | transaction-set JSON |
| 2 | `EF_PARSE_JSON_VALIDATE` | JSON plus a validation report |
| 3 | `EF_PARSE_JSON_VALIDATE_ACK` | JSON plus validation and a 999/997/TA1 acknowledgment |

### Streaming large interchanges

Drive `ef_start_split` / `ef_split` / `ef_get_result` to stream one transaction
set (or repeating loop) at a time. `segment_id` must be `ST` or the first
segment of a repeating loop.

```c
const char *config =
    "{\"split\":{\"segment_id\":\"LX\",\"segment_depth\":6,\"loop_id\":\"2400\"}}";

ef_start_split(edi, EF_PARSE_JSON, config);
for (;;) {
    ef_split_step step;
    ef_buffer payload;
    ef_split(&step);
    if (step.size > 0) {
        ef_get_result(step.size, &payload);
        /* handle payload */
        ef_free(payload.data);
    }
    if (step.is_last)
        break;
}
```

`ef_start_merge` / `ef_merge` / `ef_get_result` streams a full interchange JSON
document back out one segment at a time:

```c
ef_start_merge(transactions_json);
for (;;) {
    int size = 0;
    ef_buffer segment;
    ef_merge(&size);
    if (size == 0)
        break;
    ef_get_result(size, &segment);
    fwrite(segment.data, 1, (size_t)segment.length, out);
    fputs("\r\n", out);
    ef_free(segment.data);
}
```

### Building EDI

```c
ef_buffer edi;
ef_build(transactions_json, "\r\n", &edi);   /* postfix NULL for compact output */
ef_free(edi.data);
```

## API reference

Conventions used by every raw ABI function (and mirrored by the wrappers):

- Returns `int`: `0` = success, non-zero = [error code](#error-codes).
- Inputs are UTF‑8 `byte*` + `int length`.
- Output functions use **grow-and-retry**: if the buffer is too small the call
  returns `1` (`InsufficientCapacity`) and writes the required size into the
  length out-parameter; reallocate and call again.
- Exceptions never cross the boundary.

The wrappers in `edifabric_x12.h` retry `InsufficientCapacity` automatically and
return heap buffers you free with `ef_free()`.

| Group | Functions |
| --- | --- |
| Loading | `ef_load_library`, `ef_library_path`, `ef_last_load_error` |
| Lifecycle | `ef_init_logger`, `ef_shutdown_logger`, `ef_clear_cache` |
| Licensing | `ef_install_license`, `ef_get_app_version`, `ef_get_token`, `ef_validate_token`, `ef_set_token`, `ef_get_token_expiration`, `ef_set_serial` |
| Model map | `ef_set_map`, `ef_set_map_bytes` |
| Processing | `ef_parse`, `ef_start_split`, `ef_split`, `ef_build`, `ef_start_merge`, `ef_merge`, `ef_get_result` |
| Errors | `ef_get_error`, `ef_free_error`, `ef_raw_get_error`, `ef_free` |
| Constants | `ef_parse_mode`, `ef_log_level`, `ef_error_code` |

`ef_get_error` copies the message and frees the native string for you. Call
`ef_free_error` only if you invoke `ef_raw_get_error` yourself. Builds that do
not export `free_error` fall back to the C runtime `free`.

To call the shared library directly without the wrappers, include
`c-abi-edifabric_x12_tools.h` and link or `dlopen`/`LoadLibrary` the platform
binary yourself.

## Licensing

> [!NOTE]
> The examples are available with a free plan which can be used only with Serial model validation.
> You don't need to call `install_license` with the free plan, and the only licensing call must be `set_serial`.

The serial key for the free plan is:
```
bd96a836feca45cb91c86ee65d281f52
```

Two models are supported. Tokens are recommended for containers, air-gapped
machines, and high volume; serials are simplest when always online.

```c
/* Token: fetch once with internet access, cache it, set it at process start */
ef_buffer token;
ef_get_token(serial, &token);
ef_set_token((const char *)token.data);
ef_free(token.data);

int64_t ticks = 0;
ef_get_token_expiration(&ticks);   /* .NET UTC ticks, 0 when unset */

/* Serial: register the machine once, then authorize per process */
ef_install_license(serial);
ef_set_serial(serial);
```

## Model map

`ef_set_map` tells the engine where to find transaction-set models. Keys are
`message:version`. Set `default` to your serial to resolve unmapped transaction
sets through the online spec service, or leave it empty / `null` and map
everything locally.

```json
{
  "default": null,
  "maps": {
    "837:005010X222A1": { "type": 1, "name": "837P.json", "location": "/opt/models" },
    "850:005010":       { "type": 1, "name": "850.json",  "location": "/opt/models" }
  }
}
```

All X12 transactions, such as 837P, 834, 850, etc. are represented as proprietary JSON.
Download a standard model from [EdiNation Spec Library](https://edination.edifabric.com/edi-spec-library.html),
or a custom model from [EdiNation Spec Builder](https://edination.edifabric.com/edi-spec-builder.html).
Create/modify models in OpenEDI format, upload them in EdiNation Spec Builder and download them as JSON for use in ediFabric Native.

To download a model in either EdiNation Spec Library or EdiNation Spec Builder,
select the model first, then in the JSON view
select the Download button in the top right corner.

![Model Img](https://github.com/EdiFabric/edifabric-c-bindings/blob/main/model.png)

Choose to download as **ediFabric Native**.

## Configuration JSON

All JSON uses **`snake_case`** keys and is case-insensitive.

### ParseConfig (`parse`)

```json
{
  "validate": { "regex": null, "date_format": null, "time_format": null,
                "skip_seq_count": false, "skip_hl_seq": false,
                "snip_level": 0, "max_errors": 0 },
  "ack":      { "supress_ta1": false, "ak901p": false,
                "gen_for_valid": false, "gen997": false }
}
```

- `validate` — applied when `mode ≥ 2`; `snip_level` is `1`–`4`.
- `ack` — applied when `mode == 3`.

All sections are optional for `parse`.

### SplitConfig (`start_split`)

```json
{
  "split":    { "segment_id": "ST", "segment_depth": 0, "loop_id": null }
}
```

- `split` — required for `start_split`.

Splitting is possible for the following boundaries:

- Transaction - for files that contain batches of transactions.
- Repeating loop - for files that contain batches of loops, such as order lines, claims or benefit enrollments.

The splitter must be configured as follows:

- `segment_id`  — the name of the segment to split by. It must be either ST or the first segment in the repeatable loop (Mandatory).
- `segment_depth` — the depth of the segment in the model hierarchy (Mandatory).
- `loop_id` — the name of the loop for the segment specified in segment_id (Optional).

The values for the splitter can be found in EdiNation by loading a sample file. For example, if you want to split by loop 2000A in 837P, load an 837P file in EdiNation (or use the example one), click on the first segment in that loop, e.g., HL. `segment_id` is **CODE**,  `loop_id` is the last item in **PATH**, and `segment_depth` is **DEPTH**.

> [!NOTE]
> If a segment does not show a SPLITTER copy button, than splitting is not possible by that segment.

The easiest way to get the splitter configuration is to click on the copy button under SPLITTER that has the full splitter JSON pre-configured.

![Model Img](https://github.com/EdiFabric/edifabric-c-bindings/blob/main/splitter.png)

## Threading

The library holds process-global state (model map, active split reader, active
merge writer, last result, license) behind an internal lock. `parse` and `build`
are independent per call, but each split or merge sequence must run to
completion without another split or merge interleaving from a different thread.

## Error codes

`0` is success and `1` means the output buffer was too small. Library-level codes
are exposed as `ef_error_code`, and `ef_get_error(code)` returns the message.

| Code | Meaning |
| --- | --- |
| 501 | Unknown |
| 502 | No internet access to the authentication API |
| 503 | Local map file has invalid paths or file names |
| 611 | Incorrect or empty input |
| 612 | Logger initialization failed |
| 613 | Map JSON could not be deserialized |
| 614 | Negative output capacity |
| 615 | Model map not set, call `set_map` / `ef_set_map` first |
| 616 | Mode must be 1, 2, or 3 |
| 617 | No JSON produced |
| 618 | Validation result unavailable |
| 619 | Validation report serialization failed |
| 620 | Incorrect token |
| 621 | Config JSON could not be deserialized |
| 622 | Split `segment_id` missing or empty |
| 623 | `split` called before `start_split` |
| 624 | No result available for `get_result` |
| 625 | `get_result` buffer size mismatch |
| 626 | `merge` called before `start_merge` |
| 627 | Incorrect or null output pointer |
| 628 | Incorrect serial |
| 629 | License not installed, run `install_license` |
| 630 | Application maximum version exceeded |
| 631 | Token expired |
| 632 | Token missing |
| 633 | Maximum licenses exceeded |
| 634 | License snapshot not found |
| 635 | License not set, call `set_token` or `set_serial` |

## Troubleshooting

**`Could not load edifabric-x12-tools.dll`** — the library is not on any searched
path. Pass `--lib /path/to/library`, call `ef_load_library(...)`, or set
`EDIFABRIC_X12_LIB`.

**Error 615 on parse** — call `ef_set_map` before parsing or splitting.
`ef_clear_cache` resets the map, so reload it afterwards.

**Error 635 on parse** — authorize first with `ef_set_token` or `ef_set_serial`.

**Error 633 on install_license** — the plan's machine quota is used up. Switch to
token authorization or contact support.

**Windows: log file in use** — call `ef_shutdown_logger()` before deleting or
replacing the log path; the example does this in a `goto done` cleanup path.

## Links

- [Documentation](https://support.edifabric.com/hc/en-us/articles/37276016388125-Introduction)
- [Product page](https://www.edifabric.com/edifabric-native.html)
- Support: support@edifabric.com
