// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "../../library/importers/importer.h"

#include <pxr/usd/sdf/layer.h>

#include <experimental/filesystem>
#include <fstream>

namespace TestUtils
{
inline OmniConverterCallbacks FileCallbacks()
{
    namespace fs = std::experimental::filesystem;
    OmniConverterCallbacks callbacks;
    callbacks.pathExistsCallback = [](const char* path)
    {
        return fs::exists(path);
    };
    callbacks.makeDirsCallback = [](const char* path)
    {
        return fs::create_directories(path) || fs::is_directory(path);
    };
    callbacks.layerWriteCallback = [](const char* path, const char* identifier)
    {
        auto layer = PXR_NS::SdfLayer::Find(identifier);
        return layer && layer->Export(path);
    };
    callbacks.readCallback = [](const char* path, OmniConverterBlob* blob)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            return false;
        }
        blob->size = static_cast<size_t>(stream.tellg());
        blob->buffer = new uint8_t[blob->size];
        blob->deleter = gBlobDefaultDataDeleter;
        stream.seekg(0);
        stream.read(static_cast<char*>(blob->buffer), blob->size);
        return !stream.fail();
    };
    return callbacks;
}

inline bool WriteBinaryFile(const char* path, OmniConverterBlob* blob)
{
    std::ofstream stream(path, std::ios::binary);
    stream.write(static_cast<const char*>(blob->buffer), blob->size);
    return !stream.fail();
}

inline MeshPtr MakeTriangle()
{
    auto mesh = std::make_shared<Mesh>();
    mesh->name = "triangle";
    mesh->points = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
    mesh->faceVertexCounts = { 3 };
    mesh->faceVertexIndices = { 0, 1, 2 };
    return mesh;
}
} // namespace TestUtils
