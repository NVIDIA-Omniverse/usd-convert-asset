// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "../../library/exporters/assimp/assimp_exporter.h"
#include "test_utils.h"

#include <cstring>
#include <doctest/doctest.h>


TEST_CASE("Assimp propagates disk and embedded texture write failures")
{
    for (const std::string extension : { "obj", "fbx" })
    {
        for (bool embedded : { false, true })
        {
            for (bool reject : { true, false })
            {
                CAPTURE(extension);
                CAPTURE(embedded);
                CAPTURE(reject);
                auto stage = std::make_shared<Stage>();
                stage->rootNode = std::make_shared<StageNode>("Root");
                stage->rootNode->staticMeshInstances = { 0 };
                stage->meshes = { TestUtils::MakeTriangle() };
                auto texture = std::make_shared<TextureImage>();
                texture->originalPath = "checker.png";
                texture->realPath = "checker.png";
                if (embedded)
                {
                    texture->blob = createOmniConverterBlob(new uint8_t[4]{ 1, 2, 3, 4 }, 4);
                }
                stage->images = { texture };
                OmniConverterCallbacks callbacks;
                callbacks.pathExistsCallback = [](const char*)
                {
                    return true;
                };
                callbacks.fileCopyCallback = [](const char*, const char*)
                {
                    return true;
                };
                callbacks.binaryWriteCallback = [](const char*, OmniConverterBlob*)
                {
                    return true;
                };
                if (reject)
                {
                    callbacks.fileCopyCallback = [](const char*, const char*)
                    {
                        return false;
                    };
                    callbacks.binaryWriteCallback = [](const char* path, OmniConverterBlob*)
                    {
                        return std::string(path).find("checker.png") == std::string::npos;
                    };
                }
                auto context = std::make_shared<OmniConverterFuture::FutureThreadContext>();
                context->converterContext = OmniConverterContext("input.usda", "output/out." + extension, callbacks, "output", 0);
                AssimpExporter exporter(context);
                std::string error;
                auto status = exporter.Export(stage, error);
                CHECK_MESSAGE(status == (reject ? OmniConverterStatus::FILE_WRITE_ERROR : OmniConverterStatus::OK), error);
                if (reject)
                {
                    CHECK(error.find("checker.png") != std::string::npos);
                    if (embedded)
                    {
                        CHECK(texture->blob != nullptr);
                    }
                }
            }
        }
    }
}
TEST_CASE("Temporary output directory API validates its output buffer")
{
    OmniConverterCallbacks callbacks;
    OmniConverterContext context("input.obj", "output.usda", callbacks, ".", 0);
    OmniConverterFuture future(context);

    CHECK(omniConverterGetFutureTemporaryOutputDirectory(nullptr, nullptr, 0) == 0);
    CHECK(omniConverterGetFutureTemporaryOutputDirectory(&future, nullptr, 0) == 0);

    const std::string directory = "temporary/output";
    future.SetTemporaryOutputDirectory(directory);
    const size_t requiredSize = directory.size() + 1;
    CHECK(omniConverterGetFutureTemporaryOutputDirectory(&future, nullptr, 0) == requiredSize);

    char shortBuffer[] = { 'x', '\0' };
    CHECK(omniConverterGetFutureTemporaryOutputDirectory(&future, shortBuffer, sizeof(shortBuffer)) == requiredSize);
    CHECK(shortBuffer[0] == 'x');

    std::vector<char> output(requiredSize);
    CHECK(omniConverterGetFutureTemporaryOutputDirectory(&future, output.data(), output.size()) == requiredSize);
    CHECK(std::string(output.data()) == directory);
}
