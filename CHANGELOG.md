# 9.0.11
* Add `fbxsdk` repository dependency and document source installation steps for the agent skill

# 9.0.10
* Update agent skill metadata and structure for Skills as Docs compliance

# 9.0.9
* Update repository for OSRB compliance.

# 9.0.8
* Reset Python/native callbacks and loaders during shutdown/teardown
* Fixed Draco dependency packaging for linux-aarch64 builds
* Update `Third_Party_Notices.md` file

# 9.0.7
* Update repository for OSRB compliance.

# 9.0.6
* Add PACKAGE-LICENSES in PIP wheel package

# 9.0.5
* Update repository for PLC compliance.

# 9.0.4
* Fixed pip package runtime error on linux-aarch64

# 9.0.3
* Update to repo_usd 5.0.26 to pull public USD packages

# 9.0.2
* Replaced internal Packman dependencies with Conan OSS dependencies for GitHub source staging
* Fixed staged test runtime paths for Conan-built Draco, Assimp, and tinyxml2 DLLs

# 9.0.1
* Add Skill.md for `usd-convert-asset` Github
* Fix stage_for_github CI

# 9.0.0
* Setup Pip installable Python Wheel package
* Exclude non-redistributable features for public builds 
* Renamed to usd-convert-asset
* Remove unused `zlib` and `omni_core_materials` dependencies

# 8.0.1
* Use Monolithic OpenUSD-25.11 build from repo_usd

# 8.0.0
* Added flavor for Monolithic OpenUSD-25.11 build

# 7.1.2
* Added WebP texture format support for GLTF conversion.

# 7.1.1
* Updated iray-for-omniverse to v387700.1606 for OpenUSD-25.11 Python-3.12 flavor
* Set file or data URI reference when exporting gltf

# 7.1.0
* OpenUSD-25.11 Python-3.12 flavor
* Updated draco to v1.5.6
* Updated tiny_gltf to v2.9.7
* Updated stb_image to v2.28
* Updated stb_image_write to v1.16
* Updated json.hpp to v3.10.4

# 7.0.21
* Usd FbxSdk allocation routines to enforce 16-byte alignment if mimalloc is used

# 7.0.20
* Enable tinygltf loader on linux-aarch64

# 7.0.19
* Fix crash in USD to STL export when USD file contains no materials or when IgnoreMaterials() is enabled

# 7.0.18
* Fix out of bound access to materials for FBX importer

# 7.0.17
* Fix crash in Assimp exporter when STL export fails due to no valid triangular meshes
* Improve STL export error messages to include names of skipped meshes with non-triangular faces

# 7.0.16
* Improve STL export error handling and mesh skipping logic in Assimp exporter
* Fix incorrect FBX animation rotation

# 7.0.15
* Fix invalid texture mapping path for obj conversions

# 7.0.14
* Fix obj conversion on linux-aarch64

# 7.0.13
* Fix Collada unit scaling
* Update to libxml2 2.14.5

# 7.0.12
* Fix root transform for Z_UP Collada file

# 7.0.11
* Add unit test code coverage

# 7.0.10
* Fix CI scripts

# 7.0.9
* Support usd export UTF-8 characters

# 7.0.8
* Re-enable ARM64 jobs

# 7.0.7
* Normalize material paths in obj_importer.cpp
* Temporarily disable ARM64 jobs

# 7.0.6
* Update to libxml2 2.14.4

# 7.0.5
* Fixed regression where mesh points were not being set correctly during conversion

# 7.0.4
* Fix IRay deps for Kit 106.5 flavor

# 7.0.3
* Only create normals attribute for UsdGeomPoints if normals are nonzero

# 7.0.2
* Add Kit 106.5 build flavor

# 7.0.1

* Use the release version for assimp

# 7.0.0
* Add build flavors support

# 6.0.4
* Add Point Cloud Support

# 6.0.3
* Fix collisions of USD layer names on Windows
* Ensure prim type is set to 'Mesh' for prototype mesh prims

# 6.0.2
* Update to fbxsdk 2020.3.7

# 6.0.1
* Update conan-transition published assimp v5.4.3

# 6.0.0
* Update to USD 25.02 and Python 3.12

# 5.1.0
* Add linux-aarch64 support

# 5.0.2
* Update libxml2 to 2.14.1

# 5.0.1
* Update libxml2 to 2.13.6

# 5.0.0
* Switch to new ABI

# 4.0.2
* Add support for KHR_materials_iridescence and KHR_materials_anisotropy

# 4.0.1
* Fix crash when converting mesh without face or vertex data

# 4.0.0
* Update to USD 24 and Python 3.11

# 3.2.0

* Update to assimp v5.4.3
* Fix obj export

# 3.1.0

* Update to assimp v5.2.5

# 3.0.6

* Update dependencies of libxml2, omni_core_materials, and iray-for-omniverse

# 3.0.5

* update dependencies of assimp, zlib and fbxsdk

# 3.0.4

* Switch from std::unordered_set to std::set for joint nodes to set common root node

# 3.0.3

* Fix for calculating rest transform for non-root bones

# 3.0.2

* Calculate rest transform using local transform instead of bind transform

# 3.0.1

* Fix for black diffuse colors previously displaying as white

# 3.0.0

* Added support for unicode via displayName metadata in USD exporter.

# 2.0.0

* Reworked the asset converter service. Fixed the issue with the service not downloading all files needed for
converting FBX and OBJ.
