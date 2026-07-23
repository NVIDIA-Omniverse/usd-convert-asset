// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "usd_convert_asset_internal.h"

#include "utils/utils.h"

#include <set>


const static std::unordered_map<std::string, AssetType> gExtensionAssetMap = {
    { "fbx", AssetType::FBX },   { "obj", AssetType::OBJ },   { "gltf", AssetType::GLTF }, { "glb", AssetType::GLB }, { "usd", AssetType::USDC },
    { "usdc", AssetType::USDC }, { "usda", AssetType::USDA }, { "usdz", AssetType::USDZ }, { "bvh", AssetType::BVH }, { "stl", AssetType::STL },
    { "ply", AssetType::PLY },   { "lxo", AssetType::LXO },   { "md5", AssetType::MD5 },
};

static AssetType FileFormatToAssetType(const PXR_NS::SdfFileFormatConstPtr& fileFormat)
{
    auto formatId = fileFormat->GetFormatId();
#if PXR_MINOR_VERSION >= 25 && PXR_PATCH_VERSION >= 11
    if (formatId == PXR_NS::SdfUsdcFileFormatTokens->Id || formatId == PXR_NS::SdfUsdFileFormatTokens->Id)
    {
        return AssetType::USDC;
    }
    else if (formatId == PXR_NS::SdfUsdaFileFormatTokens->Id)
    {
        return AssetType::USDA;
    }
    else if (formatId == PXR_NS::SdfUsdzFileFormatTokens->Id)
    {
        return AssetType::USDZ;
    }
#else
    if (formatId == PXR_NS::UsdUsdcFileFormatTokens->Id || formatId == PXR_NS::UsdUsdFileFormatTokens->Id)
    {
        return AssetType::USDC;
    }
    else if (formatId == PXR_NS::UsdUsdaFileFormatTokens->Id || formatId == PXR_NS::SdfTextFileFormatTokens->Id)
    {
        return AssetType::USDA;
    }
    else if (formatId == PXR_NS::UsdUsdzFileFormatTokens->Id)
    {
        return AssetType::USDZ;
    }
#endif

    return AssetType::USDC;
};

static void blobDeleter(OmniConverterBlob* blob)
{
    if (blob && blob->buffer && blob->deleter)
    {
        blob->deleter(blob->buffer);
    }

    delete blob;
}

OmniConverterBlobPtr createOmniConverterBlob(uint8_t* data, size_t size, OmniConverterBlobDataDeleter dataDeleter)
{
    auto blob = new OmniConverterBlob;
    blob->buffer = data;
    blob->size = size;
    blob->deleter = dataDeleter;

    return OmniConverterBlobPtr(blob, blobDeleter);
}

OmniConverterContext::OmniConverterContext(
    const std::string& importAssetPath,
    const std::string& outputAssetpath,
    OmniConverterCallbacks callbacks,
    const std::string& cacheFolder,
    int32_t flags,
    bool populateMaterialsOnly
)
    : mImportAssetPath(importAssetPath),
      mOutputAssetPath(outputAssetpath),
      mCallbacks(callbacks),
      mCacheFolder(cacheFolder),
      mFlags(flags),
      mPopulateMaterialsOnly(populateMaterialsOnly)
{
    static auto IsInteger = [](const std::string& s)
    {
        if (s.empty() || ((!isdigit(s[0])) && (s[0] != '-') && (s[0] != '+')))
        {
            return false;
        }

        char* p;
        strtol(s.c_str(), &p, 10);

        return (*p == 0);
    };

    if (IsInteger(importAssetPath))
    {
        mCachedStage = PXR_NS::UsdUtilsStageCache::Get().Find(PXR_NS::UsdStageCache::Id::FromString(importAssetPath));
        if (mCachedStage)
        {
            mImportAssetPath = mCachedStage->GetRootLayer()->GetIdentifier();
            std::replace(mImportAssetPath.begin(), mImportAssetPath.end(), '\\', '/');
            mImportAssetType = FileFormatToAssetType(mCachedStage->GetRootLayer()->GetFileFormat());
        }
    }
    else
    {
        mImportAssetPath = PathUtils::AbsPath(mImportAssetPath);
        mImportAssetType = GetAssetTypeInternal(mImportAssetPath);
    }
    mOutputAssetType = GetAssetTypeInternal(mOutputAssetPath);
    mOutputAssetPath = PathUtils::AbsPath(mOutputAssetPath);
    if (mCacheFolder.size() > 0)
    {
        mCacheFolder = PathUtils::AbsPath(mCacheFolder);
        if (mCacheFolder.size() > 0 && mCacheFolder.back() != '/')
        {
            mCacheFolder += '/';
        }

        // Disables caching since cache folder is not writable.
        if (!IsPathExisted(mCacheFolder) && !MakeDirectories(mCacheFolder))
        {
            std::string().swap(mCacheFolder);
        }
    }
}

bool OmniConverterContext::IsInMemoryImport() const
{
    return PXR_NS::SdfLayer::IsAnonymousLayerIdentifier(mImportAssetPath);
}

bool OmniConverterContext::IsInMemoryOutput() const
{
    return PXR_NS::SdfLayer::IsAnonymousLayerIdentifier(mOutputAssetPath);
}

std::string OmniConverterContext::GetImportAssetPath() const
{
    return mImportAssetPath;
}

std::string OmniConverterContext::GetImportAssetDir() const
{
    return PathUtils::GetDirName(mImportAssetPath);
}

std::string OmniConverterContext::GetImportAssetFileName() const
{
    return PathUtils::GetFileName(mImportAssetPath);
}

std::string OmniConverterContext::GetOutputAssetPath() const
{
    return mOutputAssetPath;
}

std::string OmniConverterContext::GetOutputAssetDir() const
{
    return PathUtils::GetDirName(mOutputAssetPath);
}

std::string OmniConverterContext::GetOutputAssetFileName() const
{
    return PathUtils::GetFileName(mOutputAssetPath);
}

bool OmniConverterContext::HasMaterialLoader() const
{
    return mCallbacks.materialLoader != nullptr;
}

bool OmniConverterContext::PopulateMaterialsOnly() const
{
    return mPopulateMaterialsOnly;
}

// By default, it will search path specified by texturePath.
// If it's not existed, it will search other files that has the same name, but with different extensions.
bool OmniConverterContext::FilterTexturePath(const std::string& texturePath, std::string& filteredPath)
{
    static std::vector<std::string> supportedExtensions = { "png", "jpg", "bmp", "dds", "tif", "tga", "exr", "hdr" };
    if (IsPathExisted(texturePath))
    {
        filteredPath = texturePath;
        return true;
    }

    auto basePath = PathUtils::GetDirName(texturePath);
    auto fileNameWithoutExtension = PathUtils::GetFileName(texturePath, false);
    auto fileName = PathUtils::GetFileName(texturePath, true);

    // If texture is not existed, trying to check if textures with same name but different extensions are exsited.
    for (auto extension : supportedExtensions)
    {
        auto fileNameWithExtension = fileName + extension;
        auto fileWithExtension = PathUtils::JoinPaths(basePath, fileNameWithExtension);
        if (IsPathExisted(fileWithExtension))
        {
            filteredPath = fileWithExtension;
            return true;
        }

        fileNameWithExtension = fileNameWithoutExtension + extension;
        fileWithExtension = PathUtils::JoinPaths(basePath, fileNameWithExtension);
        if (IsPathExisted(fileWithExtension))
        {
            filteredPath = fileWithExtension;
            return true;
        }
    }

    return false;
}

bool OmniConverterContext::LoadMaterial(OmniConverterFuture* future, OmniConverterMaterialDescription* material) const
{
    if (!mCallbacks.materialLoader)
    {
        return false;
    }

    return mCallbacks.materialLoader(future, material);
}

bool OmniConverterContext::IsPathExisted(const std::string& path) const
{
    if (!mCallbacks.pathExistsCallback)
    {
        return false;
    }

    return mCallbacks.pathExistsCallback(path.c_str());
}

bool OmniConverterContext::WriteUsdLayer(const std::string& outputPath, const std::string& layerIdentifier) const
{
    if (!mCallbacks.layerWriteCallback)
    {
        return false;
    }

    return mCallbacks.layerWriteCallback(outputPath.c_str(), layerIdentifier.c_str());
}

bool OmniConverterContext::MakeDirectories(const std::string& directory) const
{
    if (!mCallbacks.makeDirsCallback)
    {
        return false;
    }

    return mCallbacks.makeDirsCallback(directory.c_str());
}

OmniConverterBlobPtr OmniConverterContext::ReadFile(const std::string& path) const
{
    if (!mCallbacks.readCallback)
    {
        return nullptr;
    }

    OmniConverterBlob blob{};
    bool success = mCallbacks.readCallback(path.c_str(), &blob);
    if (!success)
    {
        return nullptr;
    }

    return createOmniConverterBlob((uint8_t*)blob.buffer, blob.size, blob.deleter);
}

bool OmniConverterContext::CopyFile(const std::string& targetPath, const std::string& sourcePath) const
{
    if (mCallbacks.fileCopyCallback)
    {
        return mCallbacks.fileCopyCallback(targetPath.c_str(), sourcePath.c_str());
    }

    auto fileBlob = ReadFile(sourcePath);
    if (!fileBlob)
    {
        return false;
    }

    return WriteBinary(targetPath, fileBlob.get());
}

bool OmniConverterContext::WriteBinary(const std::string& path, OmniConverterBlob* blob) const
{
    if (!mCallbacks.binaryWriteCallback)
    {
        return false;
    }

    auto basePath = PathUtils::GetDirName(path);
    if (!IsPathExisted(basePath) && !MakeDirectories(basePath))
    {
        Log("Couldn't create directory: " + basePath);
        return false;
    }

    if (!mCallbacks.binaryWriteCallback(path.c_str(), blob))
    {
        Log("Couldn't write path: " + path);
        return false;
    }

    return true;
}

void OmniConverterContext::ReportProgress(OmniConverterFuture* future, uint32_t progress, uint32_t total) const
{
    if (mCallbacks.progressCallback)
    {
        mCallbacks.progressCallback(future, progress, total);
    }
}

void OmniConverterContext::Log(const std::string& message) const
{
    if (mCallbacks.logCallback)
    {
        mCallbacks.logCallback(message.c_str());
    }
}


bool OmniConverterContext::UseMeterPerUnit() const
{
    return mFlags & OMNI_CONVERTER_FLAGS_USE_METER_PER_UNIT;
}

bool OmniConverterContext::KeepAllMaterials() const
{
    return mFlags & OMNI_CONVERTER_FLAGS_KEEP_ALL_MATERIALS;
}

bool OmniConverterContext::IsImportAssetGltfOrGlb() const
{
    return mImportAssetType == AssetType::GLTF || mImportAssetType == AssetType::GLB;
}

bool OmniConverterContext::IsImportAssetUsdcOrUsdaOrUsdz() const
{
    return mImportAssetType == AssetType::USDC || mImportAssetType == AssetType::USDA || mImportAssetType == AssetType::USDZ;
}

bool OmniConverterContext::IsSupportedImportAsset() const
{
    if (IsInMemoryImport() || mCachedStage)
    {
        return true;
    }

    const std::string extension = StringUtils::ToLower(PathUtils::GetExtension(mImportAssetPath));
    return gExtensionAssetMap.find(extension) != gExtensionAssetMap.end();
}

std::string OmniConverterContext::GetSupportedImportFormatsForError()
{
    std::set<std::string> extensions;
    for (const auto& entry : gExtensionAssetMap)
    {
        extensions.insert(entry.first);
    }

    std::string formatted;
    for (const auto& extension : extensions)
    {
        if (!formatted.empty())
        {
            formatted += ", ";
        }

        formatted += ".";
        formatted += extension;
    }

    return formatted;
}

bool OmniConverterContext::IsOutputAssetGltfOrGlb() const
{
    return mOutputAssetType == AssetType::GLTF || mOutputAssetType == AssetType::GLB;
}

bool OmniConverterContext::IsOutputAssetUsdcOrUsdaOrUsdz() const
{
    return mOutputAssetType == AssetType::USDC || mOutputAssetType == AssetType::USDA || mOutputAssetType == AssetType::USDZ;
}

AssetType OmniConverterContext::GetImportAssetType() const
{
    return mImportAssetType;
}

AssetType OmniConverterContext::GetOutputAssetType() const
{
    return mOutputAssetType;
}

AssetType OmniConverterContext::GetAssetTypeInternal(const std::string& assetPath) const
{
    if (PXR_NS::SdfLayer::IsAnonymousLayerIdentifier(assetPath))
    {
        auto anonymousLayer = PXR_NS::SdfLayer::Find(assetPath);
        if (!anonymousLayer)
        {
            return AssetType::USDC;
        }

        return FileFormatToAssetType(anonymousLayer->GetFileFormat());
    }

    const std::string& extension = StringUtils::ToLower(PathUtils::GetExtension(assetPath));
    auto iter = gExtensionAssetMap.find(extension);
    if (iter != gExtensionAssetMap.end())
    {
        return iter->second;
    }

    return AssetType::OTHER;
}
