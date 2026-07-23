# 1.0.0
* usd-convert-asset initial release
* Fixed heap corruption crash when converting a zero-byte or unreadable input.
* Fixed null-pointer dereference on file-read-failure path in the OBJ importer.
* Fix CLI printing for Unicode paths on legacy Windows consoles
* Added an import-time guard that warns if a different OpenUSD (`pxr`) build was imported before the converter's bundled OpenUSD, and suggestions to prevent ABI and singleton conflicts.
* Added CLI fail-fast validation that rejects unsupported input formats and unsupported output formats before the native runtime is loaded.
* Reject Gaussian Splat PLY (3DGS attribute signature) with a fail-fast error; splat attributes would be discarded by the Assimp path.
* Fixed crashes caused by out-of-range glTF accessor counts.
* Reject ASCII PLY files whose header element counts do not match the body.
* Fixed Windows CTRL_BREAK / Ctrl+C interrupt during conversion exiting with ACCESS_VIOLATION.
* Skill guardrail: do not add optional conversion flags (e.g. `--embed-textures`, `--ignore-materials`) unless the user requests them.
