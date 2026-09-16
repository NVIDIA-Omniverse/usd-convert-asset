# 1.1.0
* CLI: curated error when output-dir creation fails; reject Windows path components with trailing space/dot.
* Rename misleading `--single-mesh` option to `--single-props-file`; retain hidden legacy alias and clarify distinction from `--merge-all-meshes`.
* Ensure platform wheels use clean staging directories and identical Python payloads.
* Clarified that `--smooth-normals` only affects formats imported through Assimp.
* Skill guardrail: refuse unsupported input or output formats and offer supported formats unless a workaround is explicitly requested.
* Clarified that external contributions and pull requests are not accepted, removed CLA/DCO instructions, and limited repository-change skill guidance to NVIDIA maintainers.
* Added USDZ (`.usdz`) export support. The converter authors a temporary `.usdc` layer in an isolated staging directory, then packages it with OpenUSD. USD Preview Surface enables automatically for USDZ output when materials are exported.
* Added `SUPPRESS_PXR_CONFLICT_WARNING` environment variable to suppress the expected OpenUSD conflict warning.
* Explicitly set the subdivision scheme to `none` on exported meshes, including empty meshes.
* Reject negative and out-of-range face-vertex indices during USD import.
* Abort USD export when an external material or texture file copy fails.
* Export JPEG textures with the validator-compatible `.jpg` extension.
* Remove unreferenced mesh points and remap related vertex and skin data.
* Remove unreferenced indexed UV values.
* Skip empty meshes after mesh compaction.
* Author normalized, winding-consistent face-varying normals during USD mesh export.
* Preserve valid FBX NURBS curves as `UsdGeomNurbsCurves`, with explicit closed or periodic control points. Skip and log malformed curves.
* Report FBX NURBS curves rejected during USD export instead of silently losing them.
* Deduplicate repeated display-color and UV primvar values while preserving face-varying corner data.
* Remove zero-area faces.
* Support Unicode texture file paths on Windows.
* Skip bundled USD plugins on pxr conflict
* Normalize imported texture path separators before texture lookup and USD export.
* Preserve FBX conversion through Assimp on Linux aarch64.

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
* Skill guardrail: do not add optional conversion flags (e.g. `--ignore-materials`) unless the user requests them.
