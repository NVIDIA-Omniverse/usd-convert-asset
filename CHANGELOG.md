# 2.0.0
* Keep wheel license notices in the package metadata directory so installing or removing another package cannot overwrite them.
* Update the backend converter to 9.2.11.
* Preserve unbound meshes inside USD skeleton groups and accept rigid skin bindings; keep static bind-pose geometry when exporting through Assimp.
* Preserve glTF transparency, texture-coordinate selection, emission strength, and refraction settings in USD Preview Surface materials, and improve the limited MDL color fallback.
* Preserve animation speed, timeline length, and final skeleton poses, including smooth movement between sparse animation keys when exporting to FBX.
* Preserve mirrored face orientation, two-sided USD surfaces, and the association between generated normals and face corners.
* Preserve default transforms during USD-to-GLB conversion when transform operations have no authored values.
* Report Python material-loader exceptions as conversion failures instead of silently using fallback materials.
* Clean up file-format plugin temporary directories when no layers reference them, while keeping textures needed by edited or copied layers.
* Limit asynchronous future cleanup to one worker to reduce memory use and thread count during concurrent Python conversions.
* Require Python 3.12.4 or later so Windows staging directories receive restricted access controls.
* Fix Windows CLI staging failures for long output filenames and deep folders; preserve USDZ root-layer names and rollback across filesystems.
* Reject Windows device output names before CLI filesystem checks to prevent hangs.
* Breaking material change from 1.1: USD output uses USD Preview Surface instead of MDL. Full MDL shader evaluation and glTF MDL extensions are not supported; a base-color fallback is not an equivalent rendering of the original material.
* Check output write access before starting CLI conversion, while retaining final save checks and rollback protection.
* Report an error instead of hanging when a USDZ output folder cannot be written to.
* Preserve existing output files when a CLI conversion is cancelled, and restore replaced files if the final save fails.
* Skip glTF primitives and affected OBJ or PLY geometry containing non-finite vertex values, warn about skipped elements, and preserve valid geometry. Fail when no valid mesh geometry or supported PLY point-cloud data remains.
* UsdPreviewSurface inputs connected to a texture now use the types defined by the UsdPreviewSurface specification: `inputs:diffuseColor`, `inputs:emissiveColor`, and `inputs:specularColor` use `color3f`, and `inputs:normal` uses `normal3f`. These inputs previously used `float3`; their values are unchanged. Scripts that create or compare these inputs as `float3` must use the new types.
* Add diagnostic guidance when a Windows DLL load failure occurs with a possible OpenUSD conflict.
* Generate smooth normals for meshes without authored normals across all importers when the smooth-normal option is enabled.
* Fix a crash when exporting models without materials to FBX through Assimp.
* Preserve a custom MDL material's base color as the FBX diffuse-color fallback.
* Use the fallback diffuse color when material-loader output has no FBX diffuse-color property.
* Reject conflicting FBX Y-up and Z-up settings in the Python API.
* Report FBX output-write failures instead of returning success, and perform the final write outside the global FBX SDK lock.
* Added an explicit warning when a source material references a texture that cannot be resolved or copied.
* Generate angle-weighted smooth normals without corrupting indexed mesh normals.
* Fixed mesh merging after topology cleanup by ignoring cleared meshes and removing their stale instances.
* Preserve authored normal directions during USD export.
* Fixed conversion cancellation cleanup to wait for worker completion and remove temporary files safely.
* Prevent converter future release races, callback deadlocks, and event-loop blocking during asynchronous cleanup.
* Linux and Windows wheels use libxml2 2.13.8 and 2.15.4, respectively, for FBX SDK compatibility.
* Prevent Windows wheels from shipping non-runtime MSVC build artifacts (`.pdb`, `.lib`, `.exp`, and `.ilk`).
* Stop bundling the Python runtime library in the usd-convert-asset wheel; use the host Python runtime instead.
* Prevent `--debug` from failing when stderr does not provide a file descriptor.
* Reject Windows input and output paths of 260 characters or more with a clear error before native conversion.
* Keep temporary conversion and USDZ packaging paths short enough on Windows and Linux without consuming the final output path's available space.
* Preserve read-only output files and sidecars instead of replacing them, and retain existing file permissions when replacing writable files.
* Report USDZ packaging failures without incorrectly blaming materials, and include the underlying exception when available.
* Clarify that `OmniAssetConverter.shutdown()` cancels unfinished conversions and waits for their workers before releasing callbacks. Await conversions before shutdown to let them finish normally.
* Fixed the Windows interrupt exit code and used an immediate CLI process exit to prevent a late `CTRL_BREAK` shutdown race.
* Clarified that usd-convert-asset authors USD Preview Surface materials for all supported USD output formats.
* Breaking change: Removed the `--preview-surface` and `--import-gltf-mdl-extension` CLI options.
* Removed the `preview_surface`, `embed_mdl_in_usd`, and `import_gltf_mdl_extension` Python arguments.
* Removed the `OmniAssetConverter.set_neuraylib_callback` Python method.
* Clarified that `--single-props-file` stores mesh data in the exported scene, disables USD scene instancing, does not merge meshes, and places copied texture sidecars under `materials/textures/`.
* Added a stderr deprecation note for the hidden `--single-mesh` alias that names `--single-props-file` as its replacement.
* Emit OpenUSD conflict warnings independently of the host's logging level.
* Skill guardrail: reject unsupported flags and capabilities instead of substituting workarounds.
* Skill guardrail: require exit-code and output-file checks before reporting conversion success or confirmed effects.
* Skill guardrail: redirect Gaussian Splat `.ply` conversion requests to `usd-convert-gsplat` and name the tool explicitly.
* Skill guardrail: treat the Y-up and Z-up flags for one conversion scope as mutually exclusive.
* Merge eligible static meshes even when other meshes are animated, skinned, or instanced.
* Report a file-write error when an exported texture cannot be copied or written through Assimp.
* Preserve OBJ corner normals, texture coordinates, and colors when point and face-corner orders differ.
* Update to FBX SDK 2020.3.11.
* Reject PLY files with invalid vertex references instead of crashing.
* Report incomplete USD skeleton data instead of crashing.
* Clean up converter callbacks when Python exits to prevent shutdown crashes.
* Report unsupported or invalid glTF animation data instead of crashing.
* Preserve the final animation values when sampling beyond the last keyframe through Assimp.

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
