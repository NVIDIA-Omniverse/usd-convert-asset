// SPDX-FileCopyrightText: Copyright (c) 2021-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "file_format.h"

#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>
#if PXR_MINOR_VERSION < 25 || (PXR_MINOR_VERSION == 25 && PXR_PATCH_VERSION < 11)
#    include <pxr/usd/usd/usdaFileFormat.h>
#else
#    include <pxr/usd/sdf/usdaFileFormat.h>
#endif
#include <pxr/usd/usdGeom/metrics.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <usd_convert_asset.h>
#include <utility>
#include <vector>

#ifdef _WIN32
#    include <process.h>
#else
#    include <unistd.h>
#endif

PXR_NAMESPACE_OPEN_SCOPE

// Helper macro for file format token namespace change in OpenUSD 25.11
#if PXR_MINOR_VERSION >= 25 && PXR_PATCH_VERSION >= 11
#    define USDA_FILE_FORMAT_TOKENS SdfUsdaFileFormatTokens
#else
#    define USDA_FILE_FORMAT_TOKENS UsdUsdaFileFormatTokens
#endif

using std::string;

namespace
{
namespace fs = std::filesystem;
const std::string kTemporaryDirectoryKey = "omni:assetConverter:temporaryDirectory";

std::int64_t CurrentProcessId()
{
#ifdef _WIN32
    return static_cast<std::int64_t>(_getpid());
#else
    return static_cast<std::int64_t>(getpid());
#endif
}

bool RemoveTemporaryDirectory(const std::string& path)
{
    if (path.empty())
    {
        return true;
    }

    std::error_code error;
    fs::remove_all(fs::u8path(path), error);
    return !error;
}

bool AssetReferencesDirectory(const SdfAssetPath& asset, const SdfLayer& layer, const std::string& directory)
{
    const auto& assetPath = asset.GetAssetPath();
    const auto absolutePath = layer.ComputeAbsolutePath(assetPath);
    auto path = fs::u8path(absolutePath.empty() ? assetPath : absolutePath).lexically_normal().generic_u8string();
#ifdef _WIN32
    path = TfStringToLower(path);
#endif
    return path.compare(0, directory.size(), directory) == 0;
}

bool ValueReferencesDirectory(const VtValue& value, const SdfLayer& layer, const std::string& directory)
{
    if (value.IsHolding<SdfAssetPath>())
    {
        return AssetReferencesDirectory(value.UncheckedGet<SdfAssetPath>(), layer, directory);
    }
    if (value.IsHolding<VtArray<SdfAssetPath>>())
    {
        for (const auto& asset : value.UncheckedGet<VtArray<SdfAssetPath>>())
        {
            if (AssetReferencesDirectory(asset, layer, directory))
            {
                return true;
            }
        }
    }
    else if (value.IsHolding<VtDictionary>())
    {
        for (const auto& field : value.UncheckedGet<VtDictionary>())
        {
            if (ValueReferencesDirectory(field.second, layer, directory))
            {
                return true;
            }
        }
    }
    else if (value.IsHolding<SdfTimeSampleMap>())
    {
        for (const auto& sample : value.UncheckedGet<SdfTimeSampleMap>())
        {
            if (ValueReferencesDirectory(sample.second, layer, directory))
            {
                return true;
            }
        }
    }
    return false;
}

bool IsTemporaryDirectoryReferenced(const std::string& path)
{
    if (path.empty())
    {
        return false;
    }
    auto directoryPrefix = fs::u8path(path).lexically_normal().generic_u8string() + "/";
#ifdef _WIN32
    directoryPrefix = TfStringToLower(directoryPrefix);
#endif
    for (const auto& layer : SdfLayer::GetLoadedLayers())
    {
        if (!layer)
        {
            continue;
        }
        const auto customData = layer->GetCustomLayerData();
        const auto directory = customData.find(kTemporaryDirectoryKey);
        if (directory != customData.end() && directory->second.IsHolding<std::string>() && directory->second.UncheckedGet<std::string>() == path)
        {
            return true;
        }
        // Prim copies retain authored asset paths without layer metadata.
        // Inspect fields directly so this cannot reopen a layer during Read.
        bool referenced = false;
        layer->Traverse(
            SdfPath::AbsoluteRootPath(),
            [&](const SdfPath& specPath)
            {
                if (!referenced)
                {
                    for (const auto& field : layer->ListFields(specPath))
                    {
                        if (ValueReferencesDirectory(layer->GetField(specPath, field), *layer, directoryPrefix))
                        {
                            referenced = true;
                            break;
                        }
                    }
                }
            }
        );
        if (referenced)
        {
            return true;
        }
    }
    return false;
}

void SetTemporaryDirectoryReference(SdfLayer& layer, const std::string& path)
{
    auto customData = layer.GetCustomLayerData();
    customData[kTemporaryDirectoryKey] = VtValue(path);
    layer.SetCustomLayerData(customData);
}

class TemporaryDirectory
{
public:

    explicit TemporaryDirectory(std::string path) : mPath(std::move(path))
    {
    }

    ~TemporaryDirectory()
    {
        RemoveTemporaryDirectory(mPath);
    }

    std::string Release()
    {
        std::string path;
        path.swap(mPath);
        return path;
    }

private:

    std::string mPath;
};

// Layer content edits can replace SdfData while retaining texture paths. Keep
// directories until reload, a later read observes layer expiry, or process exit.
class TemporaryDirectoryRegistry
{
public:

    ~TemporaryDirectoryRegistry()
    {
        const auto processId = CurrentProcessId();
        for (const auto& entry : mEntries)
        {
            if (entry.ownerProcessId == processId)
            {
                RemoveTemporaryDirectory(entry.path);
            }
        }
    }

    void RemoveExpiredLayers()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        const auto processId = CurrentProcessId();
        auto entry = mEntries.begin();
        while (entry != mEntries.end())
        {
            if (entry->ownerProcessId == processId && !entry->layer)
            {
                if (!IsTemporaryDirectoryReferenced(entry->path) && RemoveTemporaryDirectory(entry->path))
                {
                    entry = mEntries.erase(entry);
                }
                else
                {
                    ++entry;
                }
            }
            else
            {
                ++entry;
            }
        }
    }

    std::shared_ptr<std::mutex> GetReadMutex(SdfLayer& layer)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto entry = mReadLocks.begin();
        while (entry != mReadLocks.end())
        {
            if (!entry->layer)
            {
                entry = mReadLocks.erase(entry);
            }
            else if (get_pointer(entry->layer) == &layer)
            {
                return entry->mutex;
            }
            else
            {
                ++entry;
            }
        }

        auto mutex = std::make_shared<std::mutex>();
        mReadLocks.emplace_back(SdfLayerHandle(&layer), mutex);
        return mutex;
    }

    void Track(SdfLayer& layer, const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        const auto processId = CurrentProcessId();
        for (size_t index = 0; index < mEntries.size(); ++index)
        {
            auto& entry = mEntries[index];
            if (get_pointer(entry.layer) == &layer)
            {
                const bool ownsOldPath = entry.ownerProcessId == processId;
                const std::string oldPath = std::move(entry.path);
                entry.path = path;
                entry.ownerProcessId = processId;
                if (ownsOldPath && (IsTemporaryDirectoryReferenced(oldPath) || !RemoveTemporaryDirectory(oldPath)))
                {
                    mEntries.emplace_back(SdfLayerHandle(), oldPath);
                }
                return;
            }
        }
        mEntries.emplace_back(SdfLayerHandle(&layer), path);
    }

private:

    struct Entry
    {
        Entry(const SdfLayerHandle& layerHandle, const std::string& directory)
            : layer(layerHandle), path(directory), ownerProcessId(CurrentProcessId())
        {
        }

        SdfLayerHandle layer;
        std::string path;
        std::int64_t ownerProcessId;
    };

    struct ReadLockEntry
    {
        ReadLockEntry(const SdfLayerHandle& layerHandle, std::shared_ptr<std::mutex> readMutex) : layer(layerHandle), mutex(std::move(readMutex))
        {
        }

        SdfLayerHandle layer;
        std::shared_ptr<std::mutex> mutex;
    };

    std::mutex mMutex;
    std::vector<Entry> mEntries;
    std::vector<ReadLockEntry> mReadLocks;
};

TemporaryDirectoryRegistry& GetTemporaryDirectoryRegistry()
{
    static TemporaryDirectoryRegistry registry;
    return registry;
}

std::string GetTemporaryOutputDirectory(OmniConverterFuture* future)
{
    const size_t size = omniConverterGetFutureTemporaryOutputDirectory(future, nullptr, 0);
    if (size == 0)
    {
        return std::string();
    }

    std::vector<char> path(size);
    if (omniConverterGetFutureTemporaryOutputDirectory(future, path.data(), path.size()) != size)
    {
        return std::string();
    }
    return std::string(path.data());
}
} // namespace

TF_DEFINE_PUBLIC_TOKENS(OmniAssetFileFormatTokens, OMNIASSET_FILE_FORMAT_TOKENS);

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(OmniAssetFileFormat, SdfFileFormat);
}

static bool stringToBool(std::string str)
{
    str = PXR_NS::TfStringToLower(str);
    return str == "1" || str == "true";
}

OmniAssetFileFormat::OmniAssetFileFormat()
    : SdfFileFormat(
          OmniAssetFileFormatTokens->Id,
          OmniAssetFileFormatTokens->Version,
          OmniAssetFileFormatTokens->Target,
          { "fbx", "FBX", "obj", "OBJ", "gltf", "glb", "md5", "lxo" }
      ),
      _usda(SdfFileFormat::FindById(USDA_FILE_FORMAT_TOKENS->Id))
{
}

OmniAssetFileFormat::~OmniAssetFileFormat()
{
}

bool OmniAssetFileFormat::CanRead(const string& filePath) const
{
    // XXX: Add more verification of file header magic
    auto extension = TfGetExtension(filePath);
    if (extension.empty())
    {
        return false;
    }

    return extension == this->GetFormatId();
}

bool OmniAssetFileFormat::Read(SdfLayer* layer, const string& resolvedPath, bool metadataOnly) const
{

    auto& registry = GetTemporaryDirectoryRegistry();
    const auto readMutex = registry.GetReadMutex(*layer);
    std::lock_guard<std::mutex> readLock(*readMutex);
    registry.RemoveExpiredLayers();
    auto tempLayer = PXR_NS::SdfLayer::CreateAnonymous();
    int flags = OMNI_CONVERTER_FLAGS_CREATE_WORLD_AS_DEFAULT_PRIM;
    std::string layerPath;
    PXR_NS::SdfLayer::FileFormatArguments arguments;
    PXR_NS::SdfLayer::SplitIdentifier(layer->GetIdentifier(), &layerPath, &arguments);
    auto iter = arguments.find("USE_METER_PER_UNIT");
    if (iter != arguments.end() && stringToBool(iter->second))
    {
        flags |= OMNI_CONVERTER_FLAGS_USE_METER_PER_UNIT;
    }
    else
    {
        iter = arguments.find("USE_CENTIMETER_PER_UNIT");
        if (iter == arguments.end() || !stringToBool(iter->second))
        {
            flags |= OMNI_CONVERTER_FLAGS_KEEP_WORLD_UNITS;
        }
    }

    const std::string& assetPath = layer->GetRepositoryPath().empty() ? resolvedPath : layer->GetRepositoryPath();
    auto future = omniConverterCreateAsset(assetPath.c_str(), tempLayer->GetIdentifier().c_str(), flags);
    OmniConverterStatus status = OmniConverterStatus::OK;
    while (true)
    {
        status = omniConverterCheckFutureStatus(future);
        if (status != OmniConverterStatus::IN_PROGRESS)
        {
            break;
        }
    }

    TemporaryDirectory temporaryDirectory(GetTemporaryOutputDirectory(future));
    omniConverterReleaseFuture(future);

    if (status == OmniConverterStatus::OK)
    {
        layer->TransferContent(tempLayer);
        const std::string path = temporaryDirectory.Release();
        SetTemporaryDirectoryReference(*layer, path);
        registry.Track(*layer, path);

        return true;
    }

    return false;
}

bool OmniAssetFileFormat::ReadFromString(SdfLayer* layer, const std::string& str) const
{
    // XXX: For now, defer to the usda file format for this. May need to
    //      revisit this as the alembic reader gets fully fleshed out.
    return _usda->ReadFromString(layer, str);
}

bool OmniAssetFileFormat::WriteToString(const SdfLayer& layer, std::string* str, const std::string& comment) const
{
    // XXX: For now, defer to the usda file format for this.  We don't support
    // writing Usd content as other formats at this moment.
    return SdfFileFormat::FindById(USDA_FILE_FORMAT_TOKENS->Id)->WriteToString(layer, str, comment);
}

bool OmniAssetFileFormat::WriteToStream(const SdfSpecHandle& spec, std::ostream& out, size_t indent) const
{
    // XXX: For now, defer to the usda file format for this.  We don't support
    // writing Usd content as other formats at this moment.
    return SdfFileFormat::FindById(USDA_FILE_FORMAT_TOKENS->Id)->WriteToStream(spec, out, indent);
}

PXR_NAMESPACE_CLOSE_SCOPE
