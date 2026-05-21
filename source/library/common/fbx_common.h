// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include <fbxsdk.h>
#include <memory>
#include <mutex>

extern std::mutex gFbxManagerLock;

template <typename T>
static void gFbxTypeDeleter(T* type)
{
    if (type)
    {
        type->Destroy();
    }
}

template <typename... Ts>
static std::shared_ptr<fbxsdk::FbxManager> FbxManagerCreator(Ts... args)
{
    auto type = fbxsdk::FbxManager::Create(args...);
    return std::shared_ptr<fbxsdk::FbxManager>(type, gFbxTypeDeleter<fbxsdk::FbxManager>);
}

template <typename... Ts>
static std::shared_ptr<fbxsdk::FbxIOSettings> FbxSettingsCreator(Ts... args)
{
    auto type = fbxsdk::FbxIOSettings::Create(args...);
    return std::shared_ptr<fbxsdk::FbxIOSettings>(type, gFbxTypeDeleter<fbxsdk::FbxIOSettings>);
}

template <typename... Ts>
static std::shared_ptr<fbxsdk::FbxScene> FbxSceneCreator(Ts... args)
{
    auto type = fbxsdk::FbxScene::Create(args...);
    return std::shared_ptr<fbxsdk::FbxScene>(type, gFbxTypeDeleter<fbxsdk::FbxScene>);
}

template <typename... Ts>
static std::shared_ptr<fbxsdk::FbxImporter> FbxImporterCreator(Ts... args)
{
    auto type = fbxsdk::FbxImporter::Create(args...);
    return std::shared_ptr<fbxsdk::FbxImporter>(type, gFbxTypeDeleter<fbxsdk::FbxImporter>);
}

template <typename... Ts>
static std::shared_ptr<fbxsdk::FbxExporter> FbxExporterCreator(Ts... args)
{
    auto type = fbxsdk::FbxExporter::Create(args...);
    return std::shared_ptr<fbxsdk::FbxExporter>(type, gFbxTypeDeleter<fbxsdk::FbxExporter>);
}
