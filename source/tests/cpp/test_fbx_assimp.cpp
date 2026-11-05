// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "../../library/exporters/assimp/assimp_exporter.h"
#include "../../library/importers/importer.h"
#include "../../library/utils/stage_utils.h"

#include <cstdlib>
#include <doctest/doctest.h>
#include <experimental/filesystem>
#include <fstream>

TEST_CASE("Assimp FBX exports unbound meshes and reports rejected writes")
{
    namespace fs = std::experimental::filesystem;
    const std::string root = std::getenv("REPO_ROOT_PATH");
    const std::string outputDir = root + "/_build/tests/cpp/assimp_fbx_unbound";
    fs::create_directories(outputDir);
    for (bool ignoreMaterials : { false, true })
    {
        // Exercise Assimp directly even on Windows, where FBX normally uses Autodesk.
        // Run the failing write between two successful exports to check recovery.
        for (bool rejectWrite : { false, true, false })
        {
            OmniConverterCallbacks callbacks;
            callbacks.pathExistsCallback = [](const char* path)
            {
                return fs::exists(path);
            };
            callbacks.binaryWriteCallback = [](const char* path, OmniConverterBlob* blob)
            {
                std::ofstream stream(path, std::ios::binary);
                stream.write(static_cast<const char*>(blob->buffer), blob->size);
                stream.close();
                return !stream.fail();
            };
            if (rejectWrite)
            {
                callbacks.binaryWriteCallback = [](const char*, OmniConverterBlob*)
                {
                    return false;
                };
            }
            auto context = std::make_shared<OmniConverterFuture::FutureThreadContext>();
            const std::string output = outputDir + "/unbound.fbx";
            context->converterContext = OmniConverterContext(
                root + "/data/tests/usda/Cube.usda",
                output,
                callbacks,
                outputDir,
                ignoreMaterials ? OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS : 0
            );
            auto importer = CreateImporter(context->converterContext);
            auto status = OmniConverterStatus::OK;
            std::string error;
            auto stage = importer->ImportStage(context, status, error);
            REQUIRE_MESSAGE(status == OmniConverterStatus::OK, error);
            REQUIRE(stage);
            REQUIRE(stage->materials.empty());
            REQUIRE(StageUtils::CleanupMeshes(stage, error));
            AssimpExporter exporter(context);
            status = exporter.Export(stage, error);
            if (rejectWrite)
            {
                CHECK(status == OmniConverterStatus::FILE_WRITE_ERROR);
                CHECK(error.find("Failed to write FBX file") != std::string::npos);
            }
            else
            {
                CHECK_MESSAGE(status == OmniConverterStatus::OK, error);
                CHECK(fs::file_size(output) > 0);
            }
        }
    }
    fs::remove_all(outputDir);
}
