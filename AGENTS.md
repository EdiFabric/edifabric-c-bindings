# AGENTS.md: ediFabric Native C and C++ bindings

Guidance for AI coding assistants (Codex, Cursor, GitHub Copilot, Claude Code and others) that work in this
repository, or that write C and C++ code using these bindings. README.md has the full API reference.

## What this is

C and C++ bindings for **ediFabric Native**, a native library that converts **X12 and HIPAA 5010** EDI to JSON
and back, validates transaction sets up to HIPAA SNIP level 4, and generates 997, 999 and TA1 acknowledgments,
inside the calling process. EDI files never leave the machine.

Native supports X12 and HIPAA only. For EDIFACT, HL7, NCPDP, VDA or flat files, use ediFabric .NET or ediFabric
Cloud: https://www.edifabric.com/products.html.

## Install

Download the library from https://www.edifabric.com/docs/edifabric-native/download.html and add `edifabric_x12.h`, `edifabric_x12.c` and `c-abi-edifabric_x12_tools.h` to your project

There is no package: the library is loaded at runtime from the path you pass or `EDIFABRIC_X12_LIB`.

## Call order

Do these once per process, in this order, before any parse, build, split or merge:

```c
const char *serial = getenv("EDIFABRIC_SERIAL"); /* or the demo serial bd96a836feca45cb91c86ee65d281f52 */
ef_parse_result result;

if (ef_load_library(NULL) != 0) return 1;
ef_ensure_token(serial, 3600);
ef_set_map("{\"default\":\"YOUR_SERIAL\",\"maps\":{}}");

if (ef_parse(edi_text, EF_PARSE_JSON_VALIDATE, NULL, &result) == 0) {
    /* result.output.data, result.output.length */
    ef_free(result.output.data);
}
```

1. Load the library: `ef_load_library(NULL)`.
2. License the process: `ef_ensure_token(serial, 3600)`. Use it on every plan: Community, Developer, Enterprise and Perpetual.
   It caches a token on the machine and refreshes it when it expires within the given seconds. Don't write new code
   with `ef_set_serial`: it still works, but checks the serial online on every operation.
3. Set the model map: `ef_set_map`. `{"default": serial, "maps": {}}` downloads each transaction's model
   from the online spec library; map `message:version` keys to local model files to work offline. Parsing or
   splitting before this fails with error 615.
4. Then call `ef_parse`, `ef_build`, or split and merge. On failure the binding returns a non-zero `int`; free every output buffer with `ef_free`.

## Parse modes

| Mode | Name | Output |
|---|---|---|
| 1 | `EF_PARSE_JSON` | Transaction-set JSON |
| 2 | `EF_PARSE_JSON_VALIDATE` | JSON, then a validation report from `offset` |
| 3 | `EF_PARSE_JSON_VALIDATE_ACK` | JSON, validation, and a 999, 997 or TA1 acknowledgment |

## Serial keys and errors

- The demo serial `bd96a836feca45cb91c86ee65d281f52` needs no account and has a small daily quota. Use it in examples and tests.
- Read the user's serial from `EDIFABRIC_SERIAL` or their configuration. Never commit a customer's serial.
- **639**: the daily quota of the demo serial or the Community plan is used up. Don't retry in a loop. Tell the user
  to sign up free for Community or upgrade at https://www.edifabric.com/pricing.html.
- **628 / 635**: the process isn't licensed. Call the ensure-token step first.
- **615**: set the model map before parsing or splitting.
- **640**: the license server can't be reached and no valid token is cached. Check network, firewall and proxy.
- The full error table is in README.md.

## Models and JSON

- The JSON property names come from each transaction's model (an OpenEDI JSON schema). Don't guess property names:
  parse a sample file, or read the model in the EDI spec library: https://www.edifabric.com/specs/index.html.
- A trading partner's changes go in a custom model built in the EDI Spec Builder: https://www.edifabric.com/spec-builder/index.html.

## Threading

The library keeps process-wide state (model map, license, the active split reader and merge writer). Parse and
build calls are independent, but don't interleave one split or merge sequence with another from a different thread.

## Working in this repository

- Run the walkthrough of every entry point: `cmake -S . -B build && cmake --build build && ./build/example_all_functions --serial bd96a836feca45cb91c86ee65d281f52`. From a clone, put the native library in the
  repository root first (https://www.edifabric.com/docs/edifabric-native/download.html).
- `c-abi-edifabric_x12_tools.h` mirrors the native library's C ABI. Don't edit it by hand; change the bindings to match it.
- Keep README.md's API reference and error table in step with the code.

## More

- Docs as markdown: https://www.edifabric.com/docs/edifabric-native/introduction.html.md and https://www.edifabric.com/docs/edifabric-native/licensing.html.md
- Index of all docs for LLMs: https://www.edifabric.com/llms.txt
- MCP server, for assistants that should run these operations as tools: https://github.com/EdiFabric/edifabric-mcp
