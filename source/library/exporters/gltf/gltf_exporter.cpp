// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "gltf_exporter.h"

#include "../../common/common.h"
#include "../../common/cubicSpline.h"
#include "../../common/curveTessellation.h"
#include "../../common/tiny_gltf_include.h"
#include "../../utils/utils.h"
#include "usd_type_traits.h"

#include <cassert>
#include <numeric>

using namespace omni::assetconverter::exporter::gltf;


#define ENSURE_STATUS_OK(status)                                                                                                                     \
    {                                                                                                                                                \
        auto res = status;                                                                                                                           \
        if (res != OmniConverterStatus::OK)                                                                                                          \
        {                                                                                                                                            \
            return res;                                                                                                                              \
        }                                                                                                                                            \
    }

const static std::string MDL_MODULE_DIR_NAME = "materials";
const static std::string TEXTURE_DIR_NAME = "textures";

static std::vector<double> GfVec3fToVector3(const PXR_NS::GfVec3f& vec3f)
{
    return { vec3f[0], vec3f[1], vec3f[2] };
}

static std::vector<double> GfVec3fToVector4(const PXR_NS::GfVec3f& vec3f)
{
    return { vec3f[0], vec3f[1], vec3f[2], 1.0 };
}

static tinygltf::Value GfVec2fToValueArray2(const PXR_NS::GfVec2f& vec2f)
{
    tinygltf::Value::Array valueArray = { tinygltf::Value(vec2f[0]), tinygltf::Value(vec2f[1]) };

    return tinygltf::Value(valueArray);
}

static tinygltf::Value GfVec3fToValueArray3(const PXR_NS::GfVec3f& vec3f)
{
    tinygltf::Value::Array valueArray = { tinygltf::Value(vec3f[0]), tinygltf::Value(vec3f[1]), tinygltf::Value(vec3f[2]) };

    return tinygltf::Value(valueArray);
}

static tinygltf::Value GfVec3fToValueArray4(const PXR_NS::GfVec3f& vec3f)
{
    tinygltf::Value::Array valueArray = { tinygltf::Value(vec3f[0]), tinygltf::Value(vec3f[1]), tinygltf::Value(vec3f[2]), tinygltf::Value(1.0f) };

    return tinygltf::Value(valueArray);
}

static tinygltf::Value GltfTextureInfoToValue(const tinygltf::TextureInfo& textureInfo)
{
    tinygltf::Value::Object textureInfoObject;
    textureInfoObject.insert({ "index", tinygltf::Value(textureInfo.index) });
    textureInfoObject.insert({ "texCoord", tinygltf::Value(textureInfo.texCoord) });

    return tinygltf::Value(textureInfoObject);
}

static int ToTinygltfTextureWrapMode(TextureWrapMode wrapMode)
{
    if (wrapMode == TextureWrapMode::CLAMP)
    {
        return TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE;
    }
    else if (wrapMode == TextureWrapMode::MIRROR)
    {
        return TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT;
    }
    else
    {
        return TINYGLTF_TEXTURE_WRAP_REPEAT;
    }
}

static void SetGltfNodeTransform(tinygltf::Node& node, const Transform& transform, double translationScale)
{
    const auto& tqs = transform.GetTQS();
    node.translation = { tqs.t[0] * translationScale, tqs.t[1] * translationScale, tqs.t[2] * translationScale };
    const auto& im = tqs.q.GetImaginary();
    node.rotation = { im[0], im[1], im[2], tqs.q.GetReal() };
    node.scale = { tqs.s[0], tqs.s[1], tqs.s[2] };
}

// All model data will be in the same buffer for space contiguous.
static size_t CreateBufferView(tinygltf::Model& model, void* data, size_t size, int target = TINYGLTF_TARGET_ARRAY_BUFFER)
{
    // the null data pointer will cause crash, so need check it here
    if (!data || (size == 0))
    {
        return 0;
    }
    size_t offset;
    // Align offset with 4 bytes
    size_t allocateSize = (size + 3) - (size + 3) % 4;
    if (model.buffers.size() > 0)
    {
        auto& buffer = model.buffers[0];
        offset = buffer.data.size();
        buffer.data.resize(offset + allocateSize);
        std::memcpy(&buffer.data[offset], data, size);
    }
    else
    {
        offset = 0;
        tinygltf::Buffer buffer;
        buffer.name = "Model Data";
        buffer.data.resize(allocateSize);
        std::memcpy(&buffer.data[0], data, size);
        model.buffers.push_back(buffer);
    }

    tinygltf::BufferView bufferView;
    bufferView.byteOffset = offset;
    bufferView.byteLength = size;
    bufferView.target = target;
    bufferView.buffer = model.buffers.size() - 1;
    model.bufferViews.push_back(bufferView);

    return model.bufferViews.size() - 1;
}

template <typename ValueType, typename Traits = TypeTraits<ValueType>>
static size_t CreateAccessor(
    tinygltf::Model& model,
    const PXR_NS::VtArray<ValueType>& values,
    const std::string& name,
    bool normalized = false,
    int target = 0,
    const std::vector<double>& minValues = {},
    const std::vector<double>& maxValues = {}
)
{
    tinygltf::Accessor accessor;
    size_t size = values.size() * sizeof(ValueType);
    target = Traits::GltfType == TINYGLTF_TYPE_SCALAR ? TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER : TINYGLTF_TARGET_ARRAY_BUFFER;
    accessor.bufferView = CreateBufferView(model, (void*)values.data(), size, target);
    accessor.componentType = Traits::GltfComponentType;
    accessor.count = values.size();
    accessor.type = Traits::GltfType;
    accessor.byteOffset = 0;
    accessor.normalized = normalized;
    accessor.name = name + " Accessor";
    accessor.minValues = minValues;
    accessor.maxValues = maxValues;
    model.accessors.push_back(accessor);

    return model.accessors.size() - 1;
}

namespace
{
template <typename ValueType>
void CreateAnimationChannel(
    tinygltf::Model& model,
    tinygltf::Animation& animationTrack,
    size_t nodeIndex,
    const std::string& channelPath,
    const PXR_NS::VtArray<ValueType>& values,
    double frameStep,
    size_t frameCount,
    double startTime = 0.0
)
{
    if (values.empty())
    {
        return;
    }

    tinygltf::AnimationChannel animationChannel;
    animationChannel.target_node = nodeIndex;
    animationChannel.target_path = channelPath;

    tinygltf::AnimationSampler animationSampler;
    animationSampler.interpolation = "LINEAR";

    PXR_NS::VtFloatArray times(frameCount);
    double start = startTime;
    std::generate(
        times.begin(),
        times.end(),
        [&frameStep, &start]()
        {
            double value = start;
            start += frameStep;

            return (float)value;
        }
    );

    animationSampler.input = CreateAccessor(model, times, "Animation Frame Times", false, 0, { 0.0 }, { times.back() });

    animationSampler.output = CreateAccessor(model, values, "Animation Frame Values");

    animationTrack.samplers.push_back(animationSampler);
    animationChannel.sampler = animationTrack.samplers.size() - 1;

    animationTrack.channels.push_back(animationChannel);
}
} // namespace

OmniConverterStatus GltfExporter::Export(const StagePtr& stage, std::string& detailedError)
{
    auto status = OmniConverterStatus::OK;
    const std::string& assetOutputPath = mExportContext->converterContext.GetOutputAssetPath();
    const std::string& basePath = mExportContext->converterContext.GetOutputAssetDir();
    const std::string& fileName = mExportContext->converterContext.GetOutputAssetFileName();

    mMdlModulesExportRoot = PathUtils::JoinPaths(basePath, MDL_MODULE_DIR_NAME);
    mTexturesExportRoot = PathUtils::JoinPaths(basePath, TEXTURE_DIR_NAME);

    Log("Starting to export asset with GLTF exporter.");

    uint32_t totalSteps = GetTotalExportSteps(stage);
    mExportContext->StartProgress(totalSteps);

    static tinygltf::FileExistsFunction fileExists = [](const std::string& filename, void* userData)
    {
        OmniConverterContext* context = (OmniConverterContext*)userData;
        return context->IsPathExisted(filename.c_str());
    };

    static tinygltf::ExpandFilePathFunction expandFilePath = [](const std::string& filePath, void* userData)
    {
        OmniConverterContext* context = (OmniConverterContext*)userData;
        if (PathUtils::IsAbsolutePath(filePath))
        {
            return filePath;
        }

        return PathUtils::JoinPaths(context->GetOutputAssetDir(), filePath);
    };

    static tinygltf::ReadWholeFileFunction readWholeFile =
        [](std::vector<unsigned char>* data, std::string* error, const std::string& filePath, void* userData)
    {
        return false;
    };

    bool export_to_bin = mExportContext->converterContext.ExportEmbeddedGltf();
    static tinygltf::WriteWholeFileFunction writeWholeFile =
        [](std::string* error, const std::string& filePath, const std::vector<unsigned char>& data, void* userData)
    {
        return true;
    };
    static tinygltf::WriteImageDataFunction writeImageDataFunction = [](const std::string* basepath,
                                                                        const std::string* filename,
                                                                        const tinygltf::Image* image,
                                                                        bool embedImages,
                                                                        const tinygltf::FsCallbacks* fs_cb,
                                                                        const tinygltf::URICallbacks* uri_cb,
                                                                        std::string* out_uri,
                                                                        void* user_data)
    {
        if (image != nullptr && out_uri != nullptr && !image->uri.empty())
        {
            *out_uri = image->uri;
        }
        return true;
    };

    if (export_to_bin)
    {
        writeWholeFile = [](std::string* error, const std::string& filePath, const std::vector<unsigned char>& data, void* userData)
        {
            OmniConverterContext* context = (OmniConverterContext*)userData;

            OmniConverterBlob dataBlob = { (void*)data.data(), data.size(), nullptr };
            return context->WriteBinary(filePath, &dataBlob);
        };
    }

    tinygltf::FsCallbacks fsCallbacks;
    fsCallbacks.FileExists = fileExists;
    fsCallbacks.ExpandFilePath = expandFilePath;
    fsCallbacks.ReadWholeFile = readWholeFile;
    fsCallbacks.WriteWholeFile = writeWholeFile;
    fsCallbacks.user_data = &mExportContext->converterContext;

    double scale = stage->worldUnits;

    tinygltf::Model model;
    // Fill animation tracks
    for (const auto& animationTrack : stage->animationTracks)
    {
        tinygltf::Animation animation;
        animation.name = animationTrack.name;
        model.animations.push_back(animation);
    }

    tinygltf::TinyGLTF loader;
    loader.SetFsCallbacks(fsCallbacks);
    if (!export_to_bin)
    {
        loader.SetImageWriter(writeImageDataFunction, nullptr);
    }

    ENSURE_STATUS_OK(ExportTextures(model, stage, detailedError));


    if (!stage->yAxis)
    {
        PXR_NS::GfMatrix4d zUpToYupMatrix(1.0, 0.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0);
        const auto& rootNodeMatrix = stage->rootNode->localTransform.GetMatrix() * zUpToYupMatrix;
        stage->rootNode->localTransform.SetMatrix(rootNodeMatrix);
        stage->rootNode->worldTransformMatrix = rootNodeMatrix;
    }

    PreprocessAllNodes(stage);
    PXR_NS::VtMatrix4fArray inverseBindMatrices;
    std::vector<std::vector<LinearSweptSphere>> lssStrands;
    PopulateStageNodeTree(model, stage, stage->rootNode, -1, -1, inverseBindMatrices, scale, lssStrands);

    // remove null animations, that cause some gltf viewer's error.
    model.animations.erase(
        std::remove_if(
            model.animations.begin(),
            model.animations.end(),
            [](const tinygltf::Animation& o)
            {
                return o.channels.size() == 0;
            }
        ),
        model.animations.end()
    );

    // Populate props after stage tree traverse to find all bones to fill skin mesh.
    PopulatePropsAndMaterials(model, stage, scale, lssStrands);
    if (mExportContext->IsExited())
    {
        return OmniConverterStatus::CANCELLED;
    }


    for (const std::string& extensionUsed : mExtensionsUsed)
    {
        model.extensionsUsed.push_back(extensionUsed);
        model.extensionsRequired.push_back(extensionUsed);
    }

    tinygltf::Asset assetSource;
    assetSource.generator = "Exported from Omniverse GLTF Converter.";
    model.asset = assetSource;

    bool success = false;
    bool writeBinary = mExportContext->converterContext.GetOutputAssetType() == AssetType::GLB;

    if (export_to_bin)
    {
        success = loader.WriteGltfSceneToFile(&model, assetOutputPath, false, false, !writeBinary, writeBinary);
    }
    else
    {
        std::ostringstream writeStream;
        success = loader.WriteGltfSceneToStream(&model, writeStream, !writeBinary, writeBinary);
        if (success)
        {
            const auto& gltfData = writeStream.str();
            OmniConverterBlob dataBlob = { (void*)gltfData.data(), gltfData.size(), nullptr };
            success = mExportContext->converterContext.WriteBinary(assetOutputPath, &dataBlob);
        }
    }
    if (!success)
    {
        Log("Failed to export " + assetOutputPath);
        return OmniConverterStatus::FILE_WRITE_ERROR;
    }

    return OmniConverterStatus::OK;
}

OmniConverterStatus GltfExporter::ExportTextures(tinygltf::Model& model, const StagePtr& stage, std::string& detailedError)
{
    Log("Starting to export textures...");
    static auto ClearInMemoryTexture = [](TextureImagePtr& texture)
    {
        if (texture)
        {
            texture->blob = nullptr;
        }
    };

    auto ChangeTextureExtensionToPNG = [&stage](size_t imageIndex)
    {
        if (imageIndex != -1)
        {
            TextureImagePtr texture = stage->images[imageIndex];
            std::string basePath = PathUtils::GetDirName(texture->realPath);
            std::string filename = PathUtils::GetFileName(texture->realPath);
            texture->realPath = PathUtils::JoinPaths(basePath, filename + ".png");

            basePath = PathUtils::GetDirName(texture->originalPath);
            filename = PathUtils::GetFileName(texture->originalPath);
            texture->originalPath = PathUtils::JoinPaths(basePath, filename + ".png");
        }
    };

    for (size_t i = 0; i < stage->materials.size(); i++)
    {
        if (mExportContext->IsExited())
        {
            return OmniConverterStatus::CANCELLED;
        }

        mExportContext->IncrementProgress();

        auto& material = stage->materials[i];

        auto& diffuse = material->GetTextureReference(MaterialTextureType::DIFFUSE);
        auto& opacity = material->GetTextureReference(MaterialTextureType::OPACITY);
        if (diffuse.IsValid() && opacity.IsValid())
        {
            const auto& diffuseBlob = ReadTextureData(stage, diffuse.imageIndex);
            if (diffuseBlob)
            {
                ImageHeader diffuseImageInfo;
                bool success = ImageUtils::LoadImageInfoFromMemory((uint8_t*)diffuseBlob->buffer, diffuseBlob->size, diffuseImageInfo);
                if (success && diffuseImageInfo.channels == 3)
                {
                    const auto& opacityBlob = ReadTextureData(stage, opacity.imageIndex);
                    if (opacityBlob)
                    {
                        ImageHeader opacityImageInfo;
                        ImageUtils::LoadImageInfoFromMemory((uint8_t*)opacityBlob->buffer, opacityBlob->size, opacityImageInfo);
                        int alphaChannel = opacity.outputMode != TextureOutputMode::ALPHA ? -1 : opacityImageInfo.channels - 1;
                        const uint8_t* diffuseData = (uint8_t*)diffuseBlob->buffer;
                        size_t diffuseSize = diffuseBlob->size;
                        const uint8_t* alphaData = (uint8_t*)opacityBlob->buffer;
                        size_t alphaSize = opacityBlob->size;
                        auto diffuseWithAlphaBlob = ImageUtils::ExtractSourceChannelAndSetTargetChannelAsPNG(
                            diffuseData,
                            diffuseSize,
                            -1,
                            alphaData,
                            alphaSize,
                            alphaChannel
                        );
                        if (diffuseWithAlphaBlob)
                        {
                            ChangeTextureExtensionToPNG(diffuse.imageIndex);
                            stage->images[diffuse.imageIndex]->blob = diffuseWithAlphaBlob;
                            opacity.imageIndex = -1;
                        }
                    }
                }
            }
        }

        // glTF only supports ORM map instead of separate metallic/roughness map.
        auto& metallic = material->GetTextureReference(MaterialTextureType::METALLIC);
        auto& roughness = material->GetTextureReference(MaterialTextureType::ROUGHNESS);
        if (metallic.IsValid() && roughness.IsValid() && !material->UseORMMap())
        {
            OmniConverterBlobPtr metallicBlob = ReadTextureData(stage, metallic.imageIndex);
            OmniConverterBlobPtr roughnessBlob = ReadTextureData(stage, roughness.imageIndex);
            int firstChannel = metallic.outputMode == TextureOutputMode::AVERAGE ? -1 : 0;
            int secondChannel = roughness.outputMode == TextureOutputMode::AVERAGE ? -1 : 0;
            if (metallicBlob && roughnessBlob)
            {
                auto metallicRoughnessBlob = ImageUtils::MergeTwoImageChannelsAsPNG(
                    (uint8_t*)metallicBlob->buffer,
                    metallicBlob->size,
                    firstChannel,
                    2,
                    (uint8_t*)roughnessBlob->buffer,
                    roughnessBlob->size,
                    secondChannel,
                    1,
                    1.0,
                    metallic.bias[0],
                    1.0,
                    roughness.bias[0]
                );
                if (metallicRoughnessBlob)
                {
                    ChangeTextureExtensionToPNG(metallic.imageIndex);
                    stage->images[metallic.imageIndex]->blob = metallicRoughnessBlob;
                    roughness.imageIndex = -1;
                }
            }
        }
        else
        {
            if (roughness.IsValid() && !material->UseORMMap())
            {
                OmniConverterBlobPtr roughnessBlob = ReadTextureData(stage, roughness.imageIndex);
                ChangeTextureExtensionToPNG(roughness.imageIndex);
                stage->images[roughness.imageIndex]->blob = roughnessBlob;
            }
        }

        auto& specular = material->GetTextureReference(MaterialTextureType::SPECULAR);
        auto& glossy = material->GetTextureReference(MaterialTextureType::GLOSSY);
        if (specular.IsValid() && glossy.IsValid() && !material->UseSpecuarGlossyMap())
        {
            OmniConverterBlobPtr specularBlob = ReadTextureData(stage, specular.imageIndex);
            OmniConverterBlobPtr glossyBlob = ReadTextureData(stage, glossy.imageIndex);
            if (glossyBlob && specularBlob)
            {
                ImageHeader specularImageInfo;
                bool success = ImageUtils::LoadImageInfoFromMemory((uint8_t*)specularBlob->buffer, specularBlob->size, specularImageInfo);
                if (success && specularImageInfo.channels == 3)
                {
                    const auto& opacityBlob = ReadTextureData(stage, opacity.imageIndex);
                    if (opacityBlob)
                    {
                        ImageHeader glossyImageInfo;
                        ImageUtils::LoadImageInfoFromMemory((uint8_t*)glossyBlob->buffer, glossyBlob->size, glossyImageInfo);
                        int alphaChannel = glossy.outputMode != TextureOutputMode::ALPHA ? -1 : glossyImageInfo.channels - 1;
                        const uint8_t* specularData = (uint8_t*)specularBlob->buffer;
                        size_t specularSize = specularBlob->size;
                        const uint8_t* gossyData = (uint8_t*)glossyBlob->buffer;
                        size_t glossySize = glossyBlob->size;

                        bool invert = PXR_NS::GfVec4f(1.0f) == glossy.bias && PXR_NS::GfVec4f(-1.0f) == glossy.scale;
                        int scale = invert ? -1 : 1;
                        int offset = invert ? 255 : 0;
                        auto specularGlossyBlob = ImageUtils::ExtractSourceChannelAndSetTargetChannelAsPNG(
                            specularData,
                            specularSize,
                            -1,
                            gossyData,
                            glossySize,
                            alphaChannel,
                            scale,
                            offset
                        );
                        if (specularGlossyBlob)
                        {
                            ChangeTextureExtensionToPNG(specular.imageIndex);
                            stage->images[specular.imageIndex]->blob = specularGlossyBlob;
                            glossy.imageIndex = -1;
                        }
                    }
                }
            }
        }
    }

    if (!mExportContext->converterContext.EmbeddingTextures())
    {
        const std::string& exportAssetPath = mExportContext->converterContext.GetOutputAssetPath();
        for (size_t i = 0; i < stage->images.size(); i++)
        {
            if (mExportContext->IsExited())
            {
                return OmniConverterStatus::CANCELLED;
            }

            TextureImagePtr texture = stage->images[i];
            const std::string& imageUrl = UploadTexture(texture, detailedError);
            ClearInMemoryTexture(texture);
            mExportContext->IncrementProgress();

            tinygltf::Image image;
            std::string relativePath;
            PathUtils::ComputeRelativePath(imageUrl, exportAssetPath, relativePath);
            image.uri = relativePath;
            image.mimeType = PathUtils::ToMimeType(relativePath);
            image.name = PathUtils::GetFileName(relativePath);
            model.images.push_back(image);
        }
    }
    else // Embedded into assets
    {
        for (size_t i = 0; i < stage->images.size(); i++)
        {
            if (mExportContext->IsExited())
            {
                return OmniConverterStatus::CANCELLED;
            }

            TextureImagePtr texture = stage->images[i];
            tinygltf::Image image;
            image.name = PathUtils::GetFileName(texture->originalPath);
            image.mimeType = PathUtils::ToMimeType(texture->originalPath);
            if (texture->blob)
            {
                image.bufferView = CreateBufferView(model, texture->blob->buffer, texture->blob->size);
            }
            else
            {
                auto blob = mExportContext->converterContext.ReadFile(texture->realPath);
                if (blob)
                {
                    image.bufferView = CreateBufferView(model, blob->buffer, blob->size);
                }
                else // Invalid file
                {
                    // Create invalid bufferview still as placeholder to avoid
                    // messing image index.
                    image.bufferView = -1;
                }
            }
            model.images.push_back(image);
            mExportContext->IncrementProgress();
        }
    }

    return OmniConverterStatus::OK;
}


std::string GltfExporter::UploadTexture(const TextureImagePtr& texture, std::string& detailedError)
{
    if (texture->blob)
    {
        const std::string& filename = PathUtils::GetFileName(texture->realPath, true);
        const std::string& targetPath = PathUtils::JoinPaths(mTexturesExportRoot, filename);
        mUploadedFiles[texture->originalPath] = targetPath;
        UploadContent(targetPath, texture->blob.get());
        return targetPath;
    }
    else
    {
        UploadFileInternal(texture->realPath, mTexturesExportRoot, detailedError);
        return mUploadedFiles[texture->realPath];
    }
}

OmniConverterStatus GltfExporter::UploadFileInternal(const std::string& filePath, const std::string& targetDir, std::string& detailedError)
{
    auto iter = mUploadedFiles.find(filePath);
    if (iter == mUploadedFiles.end())
    {
        const std::string& fileName = PathUtils::GetFileName(filePath, true);
        const std::string& outFilePath = PathUtils::JoinPaths(targetDir, fileName);
        mUploadedFiles[filePath] = outFilePath;
        if (!mExportContext->converterContext.CopyFile(outFilePath, filePath))
        {
            detailedError = "Failed to copy file " + filePath + " to " + outFilePath + ".";
            Log(detailedError);
            return OmniConverterStatus::CANCELLED;
        }
    }

    return OmniConverterStatus::OK;
}

void GltfExporter::PreprocessAllNodes(const StagePtr& stage)
{
    StageUtils::TraverseStageTree(
        stage->rootNode,
        [&stage, this](const StageNodePtr& stageNode)
        {
            bool includeMeshes = stageNode->staticMeshInstances.size() > 0;
            bool includeCurves = stageNode->curveInstances.size() > 0;
            auto& stageNodeInfo = mStageNodeInfos[stageNode];
            stageNodeInfo.hasProps = stageNode->cameras.size() > 0 || includeMeshes || includeCurves || stageNode->lights.size() > 0;
            stageNodeInfo.hasSkeleton = stageNode->isBoneNode;

            return true;
        },
        [&stage, this](const StageNodePtr& stageNode)
        {
            bool hasProps = false;
            bool hasSkeleton = false;
            for (const auto& child : stageNode->children)
            {
                const auto& childNodeInfo = mStageNodeInfos[child];
                hasProps = hasProps || childNodeInfo.hasProps;
                hasSkeleton = hasSkeleton || childNodeInfo.hasSkeleton;
            }

            auto& stageNodeInfo = mStageNodeInfos[stageNode];
            stageNodeInfo.hasProps = stageNodeInfo.hasProps || hasProps;
            stageNodeInfo.hasSkeleton = stageNodeInfo.hasSkeleton || hasSkeleton;
        }
    );
}


template <typename T>
struct ValuesAndRange
{
    PXR_NS::VtArray<T> values;
    T minValue = T(std::numeric_limits<typename T::ScalarType>::max());
    T maxValue = T(std::numeric_limits<typename T::ScalarType>::min());

    std::vector<double> MinValue()
    {
        std::vector<double> result;
        result.reserve(T::dimension);
        for (uint32_t i = 0; i < T::dimension; i++)
        {
            result.push_back(static_cast<double>(minValue[i]));
        }
        return result;
    }

    std::vector<double> MaxValue()
    {
        std::vector<double> result;
        result.reserve(T::dimension);
        for (uint32_t i = 0; i < T::dimension; i++)
        {
            result.push_back(static_cast<double>(maxValue[i]));
        }
        return result;
    }

    void AddValue(const T& v)
    {
        values.push_back(v);
        for (uint32_t i = 0; i < T::dimension; i++)
        {
            minValue[i] = std::min(minValue[i], v[i]);
            maxValue[i] = std::max(maxValue[i], v[i]);
        }
    }
};

template <typename T>
struct ValuesAndRangeScalar
{
    PXR_NS::VtArray<T> values;
    T minValue = std::numeric_limits<T>::max();
    T maxValue = std::numeric_limits<T>::min();

    void AddValue(const T& v)
    {
        values.push_back(v);

        minValue = std::min(minValue, v);
        maxValue = std::max(maxValue, v);
    }

    std::vector<double> MinValue()
    {
        return { static_cast<double>(minValue) };
    }

    std::vector<double> MaxValue()
    {
        return { static_cast<double>(maxValue) };
    }
};

tinygltf::Mesh GltfExporter::ToTinygltfMesh(tinygltf::Model& model, const StagePtr& stage, const MeshPtr& mesh, double scale)
{
    tinygltf::Mesh tinygltfMesh;
    tinygltfMesh.name = mesh->name;

    std::vector<size_t> faceVertexIndexEnd;
    std::partial_sum(mesh->faceVertexCounts.begin(), mesh->faceVertexCounts.end(), std::back_inserter(faceVertexIndexEnd));

    tinygltf::Buffer buffer;
    std::vector<tinygltf::BufferView> bufferViews;
    std::vector<tinygltf::Accessor> accessors;

    bool hasValidCacheAnimation = false;

    for (const auto& geomsubset : mesh->meshSubsets)
    {
        ValuesAndRange<PXR_NS::GfVec3f> subsetPoints;
        ValuesAndRange<PXR_NS::GfVec3f> subsetNormals;

        std::vector<ValuesAndRange<PXR_NS::GfVec3f>> subsetColors(mesh->colors.size());
        std::vector<ValuesAndRange<PXR_NS::GfVec2f>> subsetUVs(mesh->uvs.size());
        ValuesAndRangeScalar<unsigned int> subsetFaceVertexIndices;

        PXR_NS::VtIntArray jointIndices;
        PXR_NS::VtFloatArray jointWeights;
        PXR_NS::VtIntArray influencedJointIndices;

        SkinMeshPtr foundSkinMesh;
        for (const auto& skinMesh : stage->skinMeshes)
        {
            if (stage->meshes[skinMesh->meshIndex] == mesh)
            {
                foundSkinMesh = skinMesh;
            }
        }

        if (foundSkinMesh)
        {
            for (const auto& influencedBone : foundSkinMesh->influencedBoneNodes)
            {
                auto iter = mAllJointIndices.find(influencedBone);
                if (iter != mAllJointIndices.end())
                {
                    influencedJointIndices.push_back(iter->second);
                }
                else
                {
                    influencedJointIndices.push_back(0);
                }
            }
        }

        typedef std::unordered_map<size_t, size_t> VertexMap;
        VertexMap vertexRemap;
        std::vector<size_t> pointRemap;
        auto AddPoint = [&](size_t faceVertexIndex)
        {
            size_t pointIndexInMesh = static_cast<size_t>(mesh->faceVertexIndices[faceVertexIndex]);
            size_t newIndex = pointRemap.size();

            bool hasFaceVaryingData = mesh->hasFaceVaryingNormals || mesh->hasFaceVaryingUVs || mesh->hasFaceVaryingColors;
            if (!hasFaceVaryingData)
            {
                // We can de-dupe and exit early
                auto insertResult = vertexRemap.insert(VertexMap::value_type(pointIndexInMesh, newIndex));
                if (!insertResult.second)
                {
                    return insertResult.first->second;
                }
            }

            pointRemap.push_back(pointIndexInMesh);
            subsetPoints.AddValue(mesh->points[pointIndexInMesh] * scale);
            if (mesh->normals.size() > 0)
            {
                size_t normalIndex = mesh->hasFaceVaryingNormals ? faceVertexIndex : pointIndexInMesh;
                // Normals are face varying, it can be directy indexed.
                subsetNormals.AddValue(mesh->normals[normalIndex].GetNormalized());
            }

            for (size_t i = 0; i < mesh->uvs.size(); i++)
            {
                if (mesh->uvs[i].size() > 0)
                {
                    size_t uvIndex = mesh->hasFaceVaryingUVs ? faceVertexIndex : pointIndexInMesh;

                    // UVs are face varying, it can be directy indexed.
                    PXR_NS::GfVec2f uv = mesh->uvs[i][uvIndex];
                    subsetUVs[i].AddValue({ uv[0], 1.0f - uv[1] });
                }
            }

            // Fill color
            for (size_t i = 0; i < mesh->colors.size(); i++)
            {
                if (mesh->colors[i].size() > 0)
                {
                    size_t colorIndex = mesh->hasFaceVaryingColors ? faceVertexIndex : pointIndexInMesh;
                    subsetColors[i].AddValue(mesh->colors[i][colorIndex]);
                }
            }

            if (foundSkinMesh)
            {
                size_t maxInfluences = foundSkinMesh->numBoneInfluencesPerVertex;
                size_t numInfluences = std::min((size_t)4, maxInfluences);
                for (size_t i = 0; i < numInfluences; i++)
                {
                    size_t currentIndex = pointIndexInMesh * maxInfluences + i;
                    size_t influencedJoint = foundSkinMesh->jointInfluences[currentIndex];
                    float influencedWeight = foundSkinMesh->jointWeights[currentIndex];
                    if (PXR_NS::GfIsClose(influencedWeight, 0.0f, 1e-06))
                    {
                        jointIndices.push_back(0);
                    }
                    else
                    {
                        jointIndices.push_back(influencedJointIndices[influencedJoint]);
                    }
                    jointWeights.push_back(influencedWeight);
                }

                for (size_t i = numInfluences; i < 4; i++)
                {
                    jointIndices.push_back(0);
                    jointWeights.push_back(0.0f);
                }
            }

            return newIndex;
        };

        std::vector<size_t> outputFaceIndices;
        for (size_t i = 0; i < geomsubset.faceIndices.size(); i++)
        {
            size_t faceIndex = geomsubset.faceIndices[i];
            size_t faceVerticesCount = mesh->faceVertexCounts[faceIndex];
            size_t endIndex = faceVertexIndexEnd[faceIndex];
            size_t startIndex = endIndex - faceVerticesCount;

            // Skip lines or values
            if (faceVerticesCount < 3)
            {
                continue;
            }

            outputFaceIndices.clear();
            outputFaceIndices.resize(faceVerticesCount);

            // Add each new face point only once
            for (size_t j = 0; j < faceVerticesCount; j++)
            {
                outputFaceIndices[j] = AddPoint(startIndex + j);
            }

            // Splitting quads into triangles if faceVerticesCount > 3
            size_t i0 = outputFaceIndices[0];
            for (size_t j = 1; j < faceVerticesCount - 1; j++)
            {
                size_t i1 = outputFaceIndices[j];
                size_t i2 = outputFaceIndices[j + 1];
                subsetFaceVertexIndices.AddValue(i0);
                subsetFaceVertexIndices.AddValue(i1);
                subsetFaceVertexIndices.AddValue(i2);
            }
        }

        tinygltf::Primitive subset;
        subset.mode = TINYGLTF_MODE_TRIANGLES;
        subset.material = geomsubset.materialIndex;
        subset.indices = CreateAccessor<unsigned int>(
            model,
            subsetFaceVertexIndices.values,
            "Face Vertex Indices",
            false,
            TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER,
            subsetFaceVertexIndices.MinValue(),
            subsetFaceVertexIndices.MaxValue()
        );

        // Fill Points
        // Calculate bounds
        auto DeltaPoints = [](const PXR_NS::VtArray<PXR_NS::GfVec3f>& values,
                              const PXR_NS::VtArray<PXR_NS::GfVec3f>& baseValues,
                              float scale,
                              const std::vector<size_t>* optRemap = nullptr)
        {
            ValuesAndRange<PXR_NS::GfVec3f> result;
            if (optRemap)
            {
                assert(optRemap->size() == baseValues.size());
                for (size_t index = 0; index < optRemap->size(); index++)
                {
                    size_t remappedIndex = (*optRemap)[index];
                    auto point = values[remappedIndex] * scale;
                    point = point - baseValues[index];
                    result.AddValue(point);
                }
            }
            else
            {
                assert(values.size() == baseValues.size());
                for (size_t index = 0; index < values.size(); index++)
                {
                    auto point = values[index] * scale;
                    point = point - baseValues[index];
                    result.AddValue(point);
                }
            }
            return result;
        };

        auto basePoseSubpointsAccessor =
            CreateAccessor(model, subsetPoints.values, "Face Points", false, 0, subsetPoints.MinValue(), subsetPoints.MaxValue());
        subset.attributes.insert({ "POSITION", basePoseSubpointsAccessor });

        // Add animation frame as morph target deltas off basePoseSubpoints
        if (!mExportContext->converterContext.IgnoreAnimations() && mesh->pointCacheTimesamples.size() > 0)
        {
            bool isAnimationValid = true;

            bool hasNormalTimeSamples = mesh->pointCacheTimesamples.size() == mesh->normalCacheTimesamples.size() && subsetNormals.values.size();
            for (uint32_t frameIndex = 0; frameIndex < mesh->pointCacheTimesamples.size(); frameIndex++)
            {
                const auto& srcKeyframePoints = mesh->pointCacheTimesamples[frameIndex];
                auto keyframeSubPoints = DeltaPoints(srcKeyframePoints, subsetPoints.values, scale, &pointRemap);
                if (keyframeSubPoints.values.size() != subsetPoints.values.size())
                {
                    // Encountered a bad keyframe, ignore animation
                    Log("WARNING: Encountered point vertex cache animation with different number of vertices than base pose. Ignoring animation");
                    isAnimationValid = false;
                    break;
                }

                auto keyframePointsAccessor = CreateAccessor(
                    model,
                    keyframeSubPoints.values,
                    "Face Points Frame",
                    false,
                    0,
                    keyframeSubPoints.MinValue(),
                    keyframeSubPoints.MaxValue()
                );
                std::map<std::string, int> target = { { "POSITION", keyframePointsAccessor } };

                if (hasNormalTimeSamples)
                {
                    const auto& srcKeyframeNormals = mesh->normalCacheTimesamples[frameIndex];
                    auto keyframeSubNormals = DeltaPoints(
                        srcKeyframeNormals,
                        subsetNormals.values,
                        1.0f,
                        mesh->hasFaceVaryingNormals ? nullptr : &pointRemap
                    );
                    if (keyframeSubNormals.values.size() != subsetNormals.values.size())
                    {
                        // Encountered a bad keyframe, ignore animation
                        Log("WARNING: Encountered normal cache animation with different number of vertices than base pose. Ignoring animation");
                        isAnimationValid = false;
                        break;
                    }

                    auto keyframeNormalsAccessor = CreateAccessor(
                        model,
                        keyframeSubNormals.values,
                        "Normals Frame",
                        false,
                        0,
                        keyframeSubNormals.MinValue(),
                        keyframeSubNormals.MaxValue()
                    );
                    target.insert({ "NORMAL", keyframeNormalsAccessor });
                }

                subset.targets.emplace_back(target);
            }

            if (!isAnimationValid)
            {
                subset.targets.clear();
            }

            hasValidCacheAnimation = hasValidCacheAnimation || isAnimationValid;
        }

        // Fill Normals
        if (subsetNormals.values.size() > 0)
        {
            auto normalsAccessor =
                CreateAccessor(model, subsetNormals.values, "Face Normals", false, 0, subsetNormals.MinValue(), subsetNormals.MaxValue());
            subset.attributes.insert({ "NORMAL", normalsAccessor });
        }

        // Fill UVs
        for (size_t i = 0; i < subsetUVs.size(); i++)
        {
            if (subsetUVs[i].values.size() > 0)
            {
                auto uvsAccessor = CreateAccessor(
                    model,
                    subsetUVs[i].values,
                    "Face UV" + std::to_string(i),
                    false,
                    0,
                    subsetUVs[i].MinValue(),
                    subsetUVs[i].MaxValue()
                );
                subset.attributes.insert({ "TEXCOORD_" + std::to_string(i), uvsAccessor });
            }
        }

        // Fill color
        auto ToColor4UShort = [](const PXR_NS::GfVec3f& v)
        {
            Color4UShort color;
            color[0] = uint16_t(double(v[0]) * 65535);
            color[1] = uint16_t(double(v[1]) * 65535);
            color[2] = uint16_t(double(v[2]) * 65535);
            return color;
        };

        PXR_NS::VtArray<Color4UShort> uShortColors;
        for (size_t i = 0; i < subsetColors.size(); i++)
        {
            uShortColors.clear();
            for (size_t j = 0; j < subsetColors[i].values.size(); j++)
            {
                uShortColors.push_back(ToColor4UShort(subsetColors[i].values[j]));
            }

            if (uShortColors.size() > 0)
            {
                Color4UShort minValue = ToColor4UShort(subsetColors[i].minValue);
                Color4UShort maxValue = ToColor4UShort(subsetColors[i].maxValue);

                auto colorsAccessor = CreateAccessor(
                    model,
                    uShortColors,
                    "Face COLOR" + std::to_string(i),
                    true,
                    0,
                    { (double)minValue[0], (double)minValue[1], (double)minValue[2] },
                    { (double)maxValue[0], (double)maxValue[1], (double)maxValue[2] }
                );
                subset.attributes.insert({ "COLOR_" + std::to_string(i), colorsAccessor });
            }
        }

        // Fill influenced joints and weights if it's skinned mesh.
        if (jointIndices.size() > 0 && jointWeights.size() > 0)
        {
            PXR_NS::VtArray<JointIndices4UShort> jointsVecIndices;
            PXR_NS::VtVec4fArray jointVecWeights;
            for (size_t i = 0; i < jointIndices.size(); i += 4)
            {
                JointIndices4UShort indices;
                for (size_t j = 0; j < 4; j++)
                {
                    if (PXR_NS::GfIsClose(jointWeights[i + j], 0.0f, 1e-6))
                    {
                        indices[j] = 0;
                    }
                    else
                    {
                        indices[j] = jointIndices[i + j];
                    }
                }

                jointsVecIndices.push_back(
                    { (uint16_t)jointIndices[i], (uint16_t)jointIndices[i + 1], (uint16_t)jointIndices[i + 2], (uint16_t)jointIndices[i + 3] }
                );
                jointVecWeights.push_back({ jointWeights[i], jointWeights[i + 1], jointWeights[i + 2], jointWeights[i + 3] });
            }

            auto jointIndicesAccessor = CreateAccessor(model, jointsVecIndices, "Joints");
            subset.attributes.insert({ "JOINTS_0", jointIndicesAccessor });

            auto jointWeightsAccessor = CreateAccessor(model, jointVecWeights, "Joint Weights");
            subset.attributes.insert({ "WEIGHTS_0", jointWeightsAccessor });
        }

        tinygltfMesh.primitives.push_back(subset);
    }

    if (hasValidCacheAnimation && mesh->pointCacheTimesamples.size() > 0)
    {
        // weights are the same for all submeshes, only add it if we have a submesh that has animations.
        tinygltfMesh.weights.resize(mesh->pointCacheTimesamples.size(), 0.0);
    }

    return tinygltfMesh;
}

tinygltf::Mesh GltfExporter::ToTinygltfMesh(
    tinygltf::Model& model,
    const StagePtr& stage,
    const CurvePtr& curve,
    double scale,
    const std::vector<std::vector<LinearSweptSphere>>& lssStrands
)
{
    tinygltf::Mesh tinygltfMesh;
    tinygltfMesh.name = curve->name;

    int counter = 0;
    for (const auto& lssStrand : lssStrands)
    {
        for (const auto& lss : lssStrand)
        {
            if (lss.points.size() > 3)
            {
                ValuesAndRange<PXR_NS::GfVec3f> subsetPoints;
                ValuesAndRangeScalar<float> subsetRadius;
                ValuesAndRangeScalar<unsigned int> subsetFaceVertexIndices;
                ValuesAndRange<PXR_NS::GfVec2f> subsetUVs;

                for (int i = 0; i < lss.points.size(); ++i)
                {
                    subsetFaceVertexIndices.AddValue(i);
                    subsetPoints.AddValue(lss.points[i]);
                    subsetRadius.AddValue(lss.radius[i]);
                    subsetUVs.AddValue(lss.texCoords[i]);
                }

                tinygltf::Primitive subset;
                subset.mode = TINYGLTF_MODE_LINE_STRIP;
                subset.indices = CreateAccessor<unsigned int>(
                    model,
                    subsetFaceVertexIndices.values,
                    "Curve Segments Indices",
                    false,
                    TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER,
                    subsetFaceVertexIndices.MinValue(),
                    subsetFaceVertexIndices.MaxValue()
                );

                auto basePoseSubpointsAccessor =
                    CreateAccessor(model, subsetPoints.values, "Curve Segments Points", false, 0, subsetPoints.MinValue(), subsetPoints.MaxValue());
                subset.attributes.insert({ "POSITION", basePoseSubpointsAccessor });

                // Fill Radius
                if (subsetRadius.values.size() > 0)
                {
                    auto radiusAccessor =
                        CreateAccessor(model, subsetRadius.values, "Curve Radius", false, 0, subsetRadius.MinValue(), subsetRadius.MaxValue());
                    subset.attributes.insert({ "RADIUS", radiusAccessor });
                }

                // Fill UVs
                if (subsetUVs.values.size() > 0)
                {
                    auto uvsAccessor = CreateAccessor(model, subsetUVs.values, "Curve UV", false, 0, subsetUVs.MinValue(), subsetUVs.MaxValue());
                    subset.attributes.insert({ "TEXCOORD_" + std::to_string(0), uvsAccessor });
                }

                tinygltfMesh.primitives.push_back(subset);
            }
            else
            {
                Log("Invalid cubic spline control points at curve index[" + std::to_string(counter) +
                    "], the size of cubic spline must be larger than 3.");
            }

            ++counter;
        }
    }

    return tinygltfMesh;
}

tinygltf::Camera GltfExporter::ToTinygltfCamera(tinygltf::Model& model, const CameraPtr& camera, double scale)
{
    tinygltf::Camera tinygltfCamera;
    tinygltfCamera.name = camera->name;
    if (camera->projectionType == PXR_NS::GfCamera::Perspective)
    {
        tinygltfCamera.type = "perspective";
        PXR_NS::GfCamera gfCamera;
        gfCamera.SetFocalLength(camera->focalLength * scale);
        gfCamera.SetHorizontalAperture(camera->horizonalAperture * scale);
        gfCamera.SetVerticalAperture(camera->verticallAperture * scale);
        gfCamera.SetFocusDistance(camera->focusDistance);
        tinygltfCamera.perspective.yfov = PXR_NS::GfDegreesToRadians(gfCamera.GetFieldOfView(PXR_NS::GfCamera::FOVVertical));
        tinygltfCamera.perspective.znear = camera->clippingNear * scale;
        tinygltfCamera.perspective.zfar = camera->clippingFar * scale;
        tinygltfCamera.perspective.aspectRatio = camera->verticallAperture > 0.0 ? camera->horizonalAperture / camera->verticallAperture : 1.0;
    }
    else
    {
        tinygltfCamera.type = "orthographic";
        tinygltfCamera.orthographic.znear = camera->clippingNear * scale;
        tinygltfCamera.orthographic.zfar = camera->clippingFar * scale;
        tinygltfCamera.orthographic.xmag = 1.0;
        if (camera->horizonalAperture != 0.0)
        {
            tinygltfCamera.orthographic.ymag = camera->verticallAperture / camera->horizonalAperture;
        }
        else
        {
            tinygltfCamera.orthographic.ymag = 1.0;
        }
    }

    return tinygltfCamera;
}

tinygltf::Light GltfExporter::ToTinygltfLight(const LightPtr& light, double scale)
{
    const double PI = 3.14159265358979323846;
    const double UNIT_PER_SQUARE_METRE = scale * scale;
    const double AREA = 1;
    tinygltf::Light tinygltfLight;
    tinygltfLight.name = light->name;
    tinygltfLight.color = GfVec3fToVector3(light->color);
    // gltf light unit is cm^2 from m^2 of USD
    if (light->type == LightType::DISTANT)
    {
        tinygltfLight.intensity = (double)light->intensity * (16.0 * PI * UNIT_PER_SQUARE_METRE);
    }
    else
    {
        tinygltfLight.intensity = (double)light->intensity * (PI * AREA * UNIT_PER_SQUARE_METRE);
    }

    switch (light->type)
    {
        case LightType::POINT:
        {
            tinygltfLight.type = "point";
            break;
        }
        case LightType::SPHERE:
        {
            tinygltfLight.type = "spot";
            if (light->outAngle > 0)
            {
                tinygltfLight.spot.innerConeAngle = light->innerAngle;
                tinygltfLight.spot.outerConeAngle = light->outAngle;
            }

            break;
        }
        case LightType::DISTANT:
        {
            tinygltfLight.type = "directional";
            break;
        }
        case LightType::RECT:
        default:
            tinygltfLight.type = "point";
            break;
    }

    return tinygltfLight;
}


tinygltf::Material GltfExporter::ToTinygltfMaterial(tinygltf::Model& model, const MaterialPtr& material)
{
    tinygltf::Material tinygltfMaterial;
    tinygltfMaterial.name = material->name;
    switch (material->opacityMode)
    {
        case GLTFOpacityMode::GLTF_BLEND:
            tinygltfMaterial.alphaMode = "BLEND";
            if (material->hasOpacity)
            {
                tinygltfMaterial.doubleSided = true;
            }
            break;
        case GLTFOpacityMode::GLTF_MASK:
            tinygltfMaterial.alphaMode = "MASK";
            tinygltfMaterial.alphaCutoff = material->opacityThreshold;
            break;
        default:
            tinygltfMaterial.alphaMode = "OPAQUE";
            break;
    }

    // set default value for omni glass
    if (material->isOmniGlass)
    {
        tinygltfMaterial.alphaMode = "BLEND";
        tinygltfMaterial.doubleSided = true;
        tinygltfMaterial.pbrMetallicRoughness.metallicFactor = 0;
        tinygltfMaterial.pbrMetallicRoughness.roughnessFactor = 0.2;
    }

    auto ToTinyGltfNormalTextureInfo = [&model, this](const TextureReference& textureReference)
    {
        tinygltf::NormalTextureInfo textureInfo;
        textureInfo.scale = 1.0;
        textureInfo.texCoord = textureReference.uvIndex;
        tinygltf::Texture texture;
        tinygltf::Sampler sampler;
        sampler.wrapS = ToTinygltfTextureWrapMode(textureReference.wrapS);
        sampler.wrapT = ToTinygltfTextureWrapMode(textureReference.wrapT);
        model.samplers.push_back(sampler);
        texture.sampler = model.samplers.size() - 1;
        texture.source = textureReference.imageIndex;
        model.textures.push_back(texture);
        textureInfo.index = model.textures.size() - 1;

        tinygltf::Value extensionValue;
        if (FillTextureTransformExtensionValue(model, textureReference, extensionValue))
        {
            mExtensionsUsed.insert("KHR_texture_transform");
            textureInfo.extensions.insert({ "KHR_texture_transform", extensionValue });
        }

        return textureInfo;
    };

    auto ToTinyGltfOcclusionTextureInfo = [&model, this](const TextureReference& textureReference, float occlusionFactor)
    {
        tinygltf::OcclusionTextureInfo textureInfo;
        textureInfo.strength = occlusionFactor;
        textureInfo.texCoord = textureReference.uvIndex;
        tinygltf::Texture texture;
        tinygltf::Sampler sampler;
        sampler.wrapS = ToTinygltfTextureWrapMode(textureReference.wrapS);
        sampler.wrapT = ToTinygltfTextureWrapMode(textureReference.wrapT);
        model.samplers.push_back(sampler);
        texture.sampler = model.samplers.size() - 1;
        texture.source = textureReference.imageIndex;
        model.textures.push_back(texture);
        textureInfo.index = model.textures.size() - 1;

        tinygltf::Value extensionValue;
        if (FillTextureTransformExtensionValue(model, textureReference, extensionValue))
        {
            mExtensionsUsed.insert("KHR_texture_transform");
            textureInfo.extensions.insert({ "KHR_texture_transform", extensionValue });
        }

        return textureInfo;
    };

    tinygltfMaterial.emissiveFactor = GfVec3fToVector3(material->emissiveColor);
    const auto& emissiveTexture = material->GetTextureReference(MaterialTextureType::EMISSIVE);
    if (emissiveTexture.IsValid())
    {
        tinygltfMaterial.emissiveTexture = ToTinyGltfTextureInfo(model, emissiveTexture);
    }

    const auto& normalTexture = material->GetTextureReference(MaterialTextureType::NORMAL);
    if (normalTexture.IsValid())
    {
        tinygltfMaterial.normalTexture = ToTinyGltfNormalTextureInfo(normalTexture);
    }

    const auto& occlusionTexture = material->GetTextureReference(MaterialTextureType::OCCLUSION);
    if (occlusionTexture.IsValid())
    {
        float occlusionFactor = 1.0f;
        if (material->hasOcclusionFactor)
        {
            occlusionFactor = material->occlusionFactor;
        }
        tinygltfMaterial.occlusionTexture = ToTinyGltfOcclusionTextureInfo(occlusionTexture, occlusionFactor);
    }

    if (material->useSpecularGlossyWorkflow)
    {
        tinygltf::Value extensionValue;
        FillSpecularRoughnessExtensionValue(model, material, extensionValue);
        mExtensionsUsed.insert("KHR_materials_pbrSpecularGlossiness");
        tinygltfMaterial.extensions.insert({ "KHR_materials_pbrSpecularGlossiness", extensionValue });
    }
    else
    {
        const auto& diffuseTexture = material->GetTextureReference(MaterialTextureType::DIFFUSE);
        if (diffuseTexture.IsValid())
        {
            tinygltfMaterial.pbrMetallicRoughness.baseColorTexture = ToTinyGltfTextureInfo(model, diffuseTexture);
        }
        tinygltfMaterial.pbrMetallicRoughness.baseColorFactor = GfVec3fToVector4(material->diffuseColor);

        if (material->hasMetallicFactor)
        {
            tinygltfMaterial.pbrMetallicRoughness.metallicFactor = material->metallicFactor;
        }

        if (material->hasRoughnessFactor)
        {
            tinygltfMaterial.pbrMetallicRoughness.roughnessFactor = material->roughnessFactor;
        }

        const auto metallicTexture = material->GetTextureReference(MaterialTextureType::METALLIC);
        if (metallicTexture.IsValid())
        {
            tinygltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture = ToTinyGltfTextureInfo(model, metallicTexture);
        }
        else
        {
            auto& roughness = material->GetTextureReference(MaterialTextureType::ROUGHNESS);
            if (roughness.IsValid())
            {
                tinygltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture = ToTinyGltfTextureInfo(model, roughness);
                tinygltfMaterial.pbrMetallicRoughness.metallicFactor = material->roughnessFactor;
            }
        }

        tinygltf::Value extensionValue;
        if (FillClearCoatExtensionValue(model, material, extensionValue))
        {
            mExtensionsUsed.insert("KHR_materials_clearcoat");
            tinygltfMaterial.extensions.insert({ "KHR_materials_clearcoat", extensionValue });
        }

        if (FillSheenExtensionValue(model, material, extensionValue))
        {
            mExtensionsUsed.insert("KHR_materials_sheen");
            tinygltfMaterial.extensions.insert({ "KHR_materials_sheen", extensionValue });
        }

        if (FillTransmissionExtensionValue(model, material, extensionValue))
        {
            mExtensionsUsed.insert("KHR_materials_transmission");
            tinygltfMaterial.extensions.insert({ "KHR_materials_transmission", extensionValue });
        }

        if (FillSpecularExtensionValue(model, material, extensionValue))
        {
            mExtensionsUsed.insert("KHR_materials_specular");
            tinygltfMaterial.extensions.insert({ "KHR_materials_specular", extensionValue });
        }

        if (FillIorExtensionValue(model, material, extensionValue))
        {
            mExtensionsUsed.insert("KHR_materials_ior");
            tinygltfMaterial.extensions.insert({ "KHR_materials_ior", extensionValue });
        }

        if (FillVolumeExtensionValue(model, material, extensionValue))
        {
            mExtensionsUsed.insert("KHR_materials_volume");
            tinygltfMaterial.extensions.insert({ "KHR_materials_volume", extensionValue });
        }

        if (FillEmissiveStrengthExtensionValue(model, material, extensionValue))
        {
            mExtensionsUsed.insert("KHR_materials_emissive_strength");
            tinygltfMaterial.extensions.insert({ "KHR_materials_emissive_strength", extensionValue });
        }

        if (FillIridescenceExtensionValue(model, material, extensionValue))
        {
            mExtensionsUsed.insert("KHR_materials_iridescence");
            tinygltfMaterial.extensions.insert({ "KHR_materials_iridescence", extensionValue });
        }

        if (FillAnisotropyExtensionValue(model, material, extensionValue))
        {
            mExtensionsUsed.insert("KHR_materials_anisotropy");
            tinygltfMaterial.extensions.insert({ "KHR_materials_anisotropy", extensionValue });
        }

    }

    return tinygltfMaterial;
}

void GltfExporter::CreateTransformAnimationChannels(
    tinygltf::Model& model,
    tinygltf::Animation& animationTrack,
    size_t nodeIndex,
    const TransformTimesamples& transformTimesamples,
    double frameStep,
    double scale
)
{
    const auto& translations = transformTimesamples.GetTranslationSamples();
    const auto& scales = transformTimesamples.GetScaleSamples();
    const auto& orients = transformTimesamples.GetOrientSamples();

    // FIXME: GLTF can only support float samples?
    PXR_NS::VtVec3fArray floatTranslations(translations.size());
    std::transform(
        translations.begin(),
        translations.end(),
        floatTranslations.begin(),
        [scale](const PXR_NS::GfVec3d& value)
        {
            return PXR_NS::GfVec3f(value) * scale;
        }
    );

    PXR_NS::VtVec3fArray floatScales(scales.size());
    std::transform(
        scales.begin(),
        scales.end(),
        floatScales.begin(),
        [](const PXR_NS::GfVec3d& value)
        {
            return PXR_NS::GfVec3f(value);
        }
    );

    PXR_NS::VtVec4fArray floatRotations(orients.size());
    std::transform(
        orients.begin(),
        orients.end(),
        floatRotations.begin(),
        [](const PXR_NS::GfQuatd& value)
        {
            const auto& im = value.GetImaginary();
            return PXR_NS::GfVec4f(im[0], im[1], im[2], value.GetReal());
        }
    );

    CreateAnimationChannel(model, animationTrack, nodeIndex, "translation", floatTranslations, frameStep, floatTranslations.size());
    CreateAnimationChannel(model, animationTrack, nodeIndex, "scale", floatScales, frameStep, floatScales.size());
    CreateAnimationChannel(model, animationTrack, nodeIndex, "rotation", floatRotations, frameStep, floatRotations.size());
}

void GltfExporter::CreatePointCacheAnimationChannel(tinygltf::Model& model, size_t nodeIndex, size_t meshIndex, const StagePtr& stage)
{
    // assume there is a root animation track
    // alternative is we have to create one
    if (model.animations.size() == 0 || stage->animationTracks.size() == 0)
    {
        return;
    }

    const MeshPtr& srcMesh = stage->meshes[meshIndex];

    size_t numFrames = srcMesh->pointCacheTimesamples.size();
    if (numFrames == 0)
    {
        return;
    }

    double frameStep = 1.0 / stage->animationTracks.front().fps;

    // Most inefficient part of adapting vertex animation into morph target.
    // Each frame is a morph target, so weights has length of numFrames^2
    // only 1 morph target is active per key frame.
    const size_t numMorphTargets = numFrames;

    // Could be a sparse accessor over a undefined all zero bufferview,
    // this would reduce the number of elements to numFrames but it's unclear how many gltf importers support sparse
    // accessor.
    PXR_NS::VtFloatArray floatWeights(numFrames * numMorphTargets, 0.0f);
    for (size_t frameIndex = 0; frameIndex < numFrames; frameIndex++)
    {
        // Set 1.0 per each frame's corresponding morph target
        floatWeights[frameIndex * numMorphTargets + frameIndex] = 1.0f;
    }

    double outputStartTime = srcMesh->timeSampleStart - stage->startTime;
    CreateAnimationChannel(model, model.animations.front(), nodeIndex, "weights", floatWeights, frameStep, numFrames, outputStartTime);
}

void GltfExporter::CreateNodeAnimation(
    tinygltf::Model& model,
    size_t nodeIndex,
    const StagePtr& stage,
    const TransformAnimationTracks& transformAnimationTracks,
    double scale
)
{
    const size_t numAnimationTracks = transformAnimationTracks.size();
    for (size_t i = 0; i < numAnimationTracks; i++)
    {
        // numAnimationTracks must be less or equal than number of stage->animationTracks.
        if (i >= stage->animationTracks.size())
        {
            break;
        }

        const auto& stageAnimTrack = stage->animationTracks[i];
        auto& gltfAnimationTrack = model.animations[i];
        double timeStep = 1.0 / stageAnimTrack.fps;
        const auto& transformTimesamples = transformAnimationTracks[i];
        CreateTransformAnimationChannels(model, gltfAnimationTrack, nodeIndex, transformTimesamples, timeStep, scale);
    }
}

bool GltfExporter::FillClearCoatExtensionValue(tinygltf::Model& model, const MaterialPtr& material, tinygltf::Value& extensionValue)
{
    tinygltf::Value::Object extensionObject;
    bool extensionUsed = false;
    if (material->hasClearCoatFactor && material->clearCoatFactor > 0.0f)
    {
        extensionUsed = true;
        extensionObject.insert({ "clearcoatFactor", tinygltf::Value(material->clearCoatFactor) });
    }

    if (material->hasClearCoatRoughnessFactor && material->clearCoatRoughnessFactor > 0.0f)
    {
        extensionUsed = true;
        extensionObject.insert({ "clearcoatRoughnessFactor", tinygltf::Value(material->clearCoatRoughnessFactor) });
    }

    auto clearCoatTexture = material->GetTextureReference(MaterialTextureType::CLEARCOAT);
    if (clearCoatTexture.IsValid())
    {
        extensionUsed = true;
        extensionObject.insert({ "clearcoatTexture", GltfTextureInfoToValue(ToTinyGltfTextureInfo(model, clearCoatTexture)) });
    }

    auto clearCoatNormalTexture = material->GetTextureReference(MaterialTextureType::CLEARCOAT_NORMAL);
    if (clearCoatNormalTexture.IsValid())
    {
        extensionUsed = true;
        extensionObject.insert({ "clearcoatNormalTexture", GltfTextureInfoToValue(ToTinyGltfTextureInfo(model, clearCoatNormalTexture)) });
    }

    auto clearCoatRoughnessTexture = material->GetTextureReference(MaterialTextureType::CLEARCOAT_ROUGHNESS);
    if (clearCoatRoughnessTexture.IsValid())
    {
        extensionUsed = true;
        extensionObject.insert({ "clearcoatRoughnessTexture", GltfTextureInfoToValue(ToTinyGltfTextureInfo(model, clearCoatRoughnessTexture)) });
    }

    extensionValue = tinygltf::Value(extensionObject);

    return extensionUsed;
}

bool GltfExporter::FillSpecularRoughnessExtensionValue(tinygltf::Model& model, const MaterialPtr& material, tinygltf::Value& extensionValue)
{
    tinygltf::Value::Object extensionObject;
    if (material->hasDiffuseColor)
    {
        extensionObject.insert({ "diffuseFactor", GfVec3fToValueArray4(material->diffuseColor) });
    }

    if (material->hasSpecularColor)
    {
        extensionObject.insert({ "specularFactor", GfVec3fToValueArray3(material->specularColor) });
    }

    if (material->hasGlossyFactor)
    {
        extensionObject.insert({ "glossinessFactor", tinygltf::Value(material->glossyFactor) });
    }

    auto diffuseTexture = material->GetTextureReference(MaterialTextureType::DIFFUSE);
    if (diffuseTexture.IsValid())
    {
        extensionObject.insert({ "diffuseTexture", GltfTextureInfoToValue(ToTinyGltfTextureInfo(model, diffuseTexture)) });
    }

    auto specularGlossyTexture = material->GetTextureReference(MaterialTextureType::GLOSSY);
    if (specularGlossyTexture.IsValid())
    {
        extensionObject.insert({ "specularGlossinessTexture", GltfTextureInfoToValue(ToTinyGltfTextureInfo(model, specularGlossyTexture)) });
    }

    extensionValue = tinygltf::Value(extensionObject);

    return true;
}

bool GltfExporter::FillSheenExtensionValue(tinygltf::Model& model, const MaterialPtr& material, tinygltf::Value& extensionValue)
{
    tinygltf::Value::Object extensionObject;
    bool extensionUsed = false;
    if (material->hasSheenColor)
    {
        extensionUsed = true;
        extensionObject.insert({ "sheenColorFactor", GfVec3fToValueArray3(material->sheenColor) });
    }

    if (material->hasSheenRoughnessFactor && material->sheenRoughnessFactor > 0.0f)
    {
        extensionUsed = true;
        extensionObject.insert({ "sheenRoughnessFactor", tinygltf::Value(material->sheenRoughnessFactor) });
    }

    auto sheenColorTexture = material->GetTextureReference(MaterialTextureType::SHEEN);
    if (sheenColorTexture.IsValid())
    {
        extensionUsed = true;
        extensionObject.insert({ "sheenColorTexture", GltfTextureInfoToValue(ToTinyGltfTextureInfo(model, sheenColorTexture)) });
    }

    auto sheenRoughnessTexture = material->GetTextureReference(MaterialTextureType::SHEEN_ROUGHNESS);
    if (sheenRoughnessTexture.IsValid())
    {
        extensionUsed = true;
        extensionObject.insert({ "sheenRoughnessTexture", GltfTextureInfoToValue(ToTinyGltfTextureInfo(model, sheenRoughnessTexture)) });
    }

    extensionValue = tinygltf::Value(extensionObject);

    return extensionUsed;
}

bool GltfExporter::FillTransmissionExtensionValue(tinygltf::Model& model, const MaterialPtr& material, tinygltf::Value& extensionValue)
{
    tinygltf::Value::Object extensionObject;
    bool extensionUsed = false;
    if (material->hasTransmissionFactor && material->transmissionFactor > 0.0f)
    {
        extensionUsed = true;
        extensionObject.insert({ "transmissionFactor", tinygltf::Value(material->transmissionFactor) });
    }

    auto transmissionTexture = material->GetTextureReference(MaterialTextureType::TRANSMISSION);
    if (transmissionTexture.IsValid())
    {
        extensionUsed = true;
        extensionObject.insert({ "transmissionTexture", GltfTextureInfoToValue(ToTinyGltfTextureInfo(model, transmissionTexture)) });
    }

    extensionValue = tinygltf::Value(extensionObject);

    return extensionUsed;
}

bool GltfExporter::FillSpecularExtensionValue(tinygltf::Model& model, const MaterialPtr& material, tinygltf::Value& extensionValue)
{
    tinygltf::Value::Object extensionObject;
    bool extensionUsed = false;
    if (material->hasSpecularStrengthFactor)
    {
        extensionUsed = true;
        extensionObject.insert({ "specularFactor", tinygltf::Value(material->specularStrength) });
    }

    if (material->hasSpecularColor)
    {
        extensionUsed = true;
        extensionObject.insert({ "specularColorFactor", GfVec3fToValueArray3(material->specularColor) });
    }

    auto specularTexture = material->GetTextureReference(MaterialTextureType::SPECULAR_STRENGTH);
    if (specularTexture.IsValid())
    {
        extensionUsed = true;
        extensionObject.insert({ "specularTexture", GltfTextureInfoToValue(ToTinyGltfTextureInfo(model, specularTexture)) });
    }

    auto specularColorTexture = material->GetTextureReference(MaterialTextureType::SPECULAR);
    if (specularColorTexture.IsValid())
    {
        extensionUsed = true;
        extensionObject.insert({ "specularColorTexture", GltfTextureInfoToValue(ToTinyGltfTextureInfo(model, specularColorTexture)) });
    }

    extensionValue = tinygltf::Value(extensionObject);

    return extensionUsed;
}

bool GltfExporter::FillIorExtensionValue(tinygltf::Model& model, const MaterialPtr& material, tinygltf::Value& extensionValue)
{
    tinygltf::Value::Object extensionObject;
    bool extensionUsed = false;
    if (material->hasIor)
    {
        extensionUsed = true;
        extensionObject.insert({ "ior", tinygltf::Value(material->ior) });
    }

    extensionValue = tinygltf::Value(extensionObject);

    return extensionUsed;
}

bool GltfExporter::FillVolumeExtensionValue(tinygltf::Model& model, const MaterialPtr& material, tinygltf::Value& extensionValue)
{
    tinygltf::Value::Object extensionObject;
    bool extensionUsed = false;
    if (!material->thinWalled)
    {
        extensionUsed = true;
        extensionObject.insert({ "thicknessFactor", tinygltf::Value(1.0) });
    }

    if (material->hasAttenuationDistance)
    {
        extensionUsed = true;
        extensionObject.insert({ "attenuationDistance", tinygltf::Value(material->attenuationDistance) });
    }

    if (material->hasAttenuationColor)
    {
        extensionUsed = true;
        extensionObject.insert({ "attenuationColor", GfVec3fToValueArray3(material->attenuationColor) });
    }

    extensionValue = tinygltf::Value(extensionObject);

    return extensionUsed;
}

bool GltfExporter::FillEmissiveStrengthExtensionValue(tinygltf::Model& model, const MaterialPtr& material, tinygltf::Value& extensionValue)
{
    tinygltf::Value::Object extensionObject;
    bool extensionUsed = false;
    if (material->hasEmissiveStrength)
    {
        extensionUsed = true;
        extensionObject.insert({ "emissiveStrength", tinygltf::Value(material->emissiveStrength) });
    }

    extensionValue = tinygltf::Value(extensionObject);

    return extensionUsed;
}

bool GltfExporter::FillIridescenceExtensionValue(tinygltf::Model& model, const MaterialPtr& material, tinygltf::Value& extensionValue)
{
    tinygltf::Value::Object extensionObject;
    bool extensionUsed = false;
    if (material->hasIridescenceFactor)
    {
        extensionUsed = true;
        extensionObject.insert({ "iridescenceFactor", tinygltf::Value(material->iridescenceFactor) });
    }

    if (material->hasIridescenceIor)
    {
        extensionUsed = true;
        extensionObject.insert({ "iridescenceIor", tinygltf::Value(material->iridescenceIor) });
    }

    if (material->hasIridescenceThicknessMinimum)
    {
        extensionUsed = true;
        extensionObject.insert({ "iridescenceThicknessMinimum", tinygltf::Value(material->iridescenceThicknessMinimum) });
    }

    if (material->hasIridescenceThicknessMaximum)
    {
        extensionUsed = true;
        extensionObject.insert({ "iridescenceThicknessMaximum", tinygltf::Value(material->iridescenceThicknessMaximum) });
    }

    auto iridescenceTexture = material->GetTextureReference(MaterialTextureType::IRIDESCENCE);
    if (iridescenceTexture.IsValid())
    {
        extensionObject.insert({ "iridescenceTexture", GltfTextureInfoToValue(ToTinyGltfTextureInfo(model, iridescenceTexture)) });
        extensionUsed = true;
    }

    auto iridescenceThicknessTexture = material->GetTextureReference(MaterialTextureType::IRIDESCENCE_THICKNESS);
    if (iridescenceThicknessTexture.IsValid())
    {
        extensionObject.insert({ "iridescenceThicknessTexture", GltfTextureInfoToValue(ToTinyGltfTextureInfo(model, iridescenceThicknessTexture)) });
        extensionUsed = true;
    }

    extensionValue = tinygltf::Value(extensionObject);

    return extensionUsed;
}

bool GltfExporter::FillAnisotropyExtensionValue(tinygltf::Model& model, const MaterialPtr& material, tinygltf::Value& extensionValue)
{
    tinygltf::Value::Object extensionObject;
    bool extensionUsed = false;
    if (material->hasAnisotropyStrength)
    {
        extensionUsed = true;
        extensionObject.insert({ "anisotropyStrength", tinygltf::Value(material->anisotropyStrength) });
    }

    if (material->hasAnisotropyRotation)
    {
        extensionUsed = true;
        extensionObject.insert({ "anisotropyRotation", tinygltf::Value(material->anisotropyRotation * M_PI * 2.0f) });
    }

    auto anisotropyTexture = material->GetTextureReference(MaterialTextureType::ANISOTROPY);
    if (anisotropyTexture.IsValid())
    {
        extensionObject.insert({ "anisotropyTexture", GltfTextureInfoToValue(ToTinyGltfTextureInfo(model, anisotropyTexture)) });
        extensionUsed = true;
    }

    extensionValue = tinygltf::Value(extensionObject);

    return extensionUsed;
}


bool GltfExporter::FillTextureTransformExtensionValue(
    tinygltf::Model& model,
    const TextureReference& textureReference,
    tinygltf::Value& extensionValue
)
{
    // It needs to transform from coordinate system of converter into glTF.
    float rotation = (float)PXR_NS::GfDegreesToRadians(textureReference.transform.rotation[2]);
    const float rcos(cos(rotation));
    const float rsin(sin(rotation));
    const float originalTx = textureReference.transform.translation[0];
    const float originalTy = textureReference.transform.translation[1];
    const float sx = textureReference.transform.scale[0];
    const float sy = textureReference.transform.scale[1];
    const float tx = originalTx - sy * rsin;
    const float ty = 1 - sy * rcos - originalTy;

    tinygltf::Value::Object extensionObject;
    bool extensionUsed = false;
    if (!PXR_NS::GfIsClose(textureReference.transform.scale, PXR_NS::GfVec2f(1.0f), 1e-6))
    {
        extensionUsed = true;
        extensionObject.insert({ "scale", GfVec2fToValueArray2(textureReference.transform.scale) });
    }

    if (!PXR_NS::GfIsClose(textureReference.transform.translation, PXR_NS::GfVec2f(0.0f), 1e-6))
    {
        extensionUsed = true;
        extensionObject.insert({ "offset", GfVec2fToValueArray2(PXR_NS::GfVec2f(tx, ty)) });
    }

    if (!PXR_NS::GfIsClose(textureReference.transform.rotation[2], 0.0f, 1e-6))
    {
        extensionUsed = true;
        extensionObject.insert({ "rotation", tinygltf::Value(PXR_NS::GfDegreesToRadians(textureReference.transform.rotation[2])) });
    }

    extensionValue = tinygltf::Value(extensionObject);

    return extensionUsed;
}

void GltfExporter::PopulatePropsAndMaterials(
    tinygltf::Model& model,
    const StagePtr& stage,
    double scale,
    const std::vector<std::vector<LinearSweptSphere>>& lssStrands
)
{
    // Fill materials to model
    for (const auto& material : stage->materials)
    {
        model.materials.push_back(ToTinygltfMaterial(model, material));
    }

    // Fill meshes to model
    for (const auto& mesh : stage->meshes)
    {
        mExportContext->IncrementProgress();
        model.meshes.push_back(ToTinygltfMesh(model, stage, mesh, scale));
    }

    // Fill curves to model
    for (const auto& curve : stage->curves)
    {
        mExportContext->IncrementProgress();
        model.meshes.push_back(ToTinygltfMesh(model, stage, curve, scale, lssStrands));
    }

    // Fill cameras to model
    for (const auto& camera : stage->cameras)
    {
        model.cameras.push_back(ToTinygltfCamera(model, camera, scale));
    }

    // Fill lights to model
    if (stage->lights.size() > 0)
    {
        mExtensionsUsed.insert("KHR_lights_punctual");
        for (const auto& light : stage->lights)
        {
            model.lights.push_back(ToTinygltfLight(light, scale));
        }
    }
}

void GltfExporter::PopulateStageNodeTree(
    tinygltf::Model& model,
    const StagePtr& stage,
    const StageNodePtr& currentNode,
    size_t parentNodeIndex,
    size_t currentSkin,
    PXR_NS::VtMatrix4fArray& skinInverseBindMatrices,
    double scale,
    std::vector<std::vector<LinearSweptSphere>>& lssStrands
)
{
    const auto& stageNodeInfo = mStageNodeInfos[currentNode];
    if (!stageNodeInfo.hasProps && !stageNodeInfo.hasSkeleton)
    {
        return;
    }

    size_t childIndex = model.nodes.size();
    model.nodes.push_back({});
    if (parentNodeIndex != -1)
    {
        model.nodes[parentNodeIndex].children.push_back(childIndex);
    }
    else
    {
        tinygltf::Scene defaultScene;
        defaultScene.name = "DefaultScene";
        defaultScene.nodes.push_back(childIndex);
        model.scenes.push_back(defaultScene);
        model.defaultScene = model.scenes.size() - 1;
    }

    model.nodes[childIndex].name = currentNode->name;
    SetGltfNodeTransform(model.nodes[childIndex], currentNode->localTransform, scale);
    CreateNodeAnimation(model, childIndex, stage, currentNode->transformAnimationTracks, scale);

    std::unordered_map<std::string, size_t> uniqueNameCount;
    if (currentNode->staticMeshInstances.size() == 1)
    {
        auto meshIndex = currentNode->staticMeshInstances[0];
        model.nodes[childIndex].mesh = currentNode->staticMeshInstances[0];

        CreatePointCacheAnimationChannel(model, childIndex, currentNode->staticMeshInstances[0], stage);
    }
    else
    {
        // glTF only permits one mesh per node
        for (size_t i = 0; i < currentNode->staticMeshInstances.size(); i++)
        {
            size_t meshIndex = currentNode->staticMeshInstances[i];
            if (meshIndex < 0 || meshIndex >= stage->meshes.size())
            {
                continue;
            }

            tinygltf::Node meshNode;
            const auto& mesh = stage->meshes[meshIndex];
            auto iter = uniqueNameCount.find(mesh->name);
            if (iter == uniqueNameCount.end())
            {
                meshNode.name = mesh->name;
                uniqueNameCount.insert({ mesh->name, 0 });
            }
            else
            {
                meshNode.name = mesh->name + "_" + std::to_string(iter->second);
                iter->second += 1;
            }

            meshNode.mesh = currentNode->staticMeshInstances[i];
            model.nodes.push_back(meshNode);
            size_t nodeIndex = model.nodes.size() - 1;
            model.nodes[childIndex].children.push_back(nodeIndex);

            CreatePointCacheAnimationChannel(model, nodeIndex, currentNode->staticMeshInstances[i], stage);
        }
    }

    // Curve
    if (stage->curves.size() > 0)
    {
        lssStrands.reserve(stage->curves.size());
        for (uint32_t curveIndex = 0; curveIndex < stage->curves.size(); ++curveIndex)
        {
            const auto& curve = stage->curves[curveIndex];

            lssStrands.push_back(std::vector<LinearSweptSphere>());
            lssStrands[curveIndex].reserve(curve->vertexCounts.size());

            uint32_t ptr = 0;
            for (uint32_t strandIndex = 0; strandIndex < curve->vertexCounts.size(); ++strandIndex)
            {
                LinearSweptSphere lss = {};

                const uint32_t vertexCount = curve->vertexCounts[strandIndex];
                CubicSpline<PXR_NS::GfVec3f> splinePoints;
                if (vertexCount > 3)
                {
                    if (curve->wrap == CurveWrap::Pinned)
                    {
                        splinePoints.setupPinnedControlPoints(curve->points.data() + ptr, vertexCount);
                    }
                    else
                    {
                        splinePoints.setup(curve->points.data() + ptr, vertexCount);
                    }
                }
                else
                {
                    continue;
                }

                const CubicSpline<float> splineWidth(curve->width.data() ? curve->width.data() + ptr : nullptr, vertexCount);
                const CubicSpline<PXR_NS::GfVec2f> splineUv(curve->uvs.data() ? curve->uvs.data() + ptr : nullptr, vertexCount);

                lss.degree = 1;

                for (uint32_t strandSegmentIndex = 0; strandSegmentIndex < vertexCount - 1; ++strandSegmentIndex)
                {
                    for (uint32_t k = 0; k < curve->subdivPerSegment; k++)
                    {
                        const float t = (float)k / (float)curve->subdivPerSegment;

                        lss.indices.push_back((uint32_t)lss.points.size());

                        if (vertexCount > 3)
                        {
                            if (curve->wrap == CurveWrap::Pinned)
                            {
                                lss.points.push_back(splinePoints.interpolatePinned(strandSegmentIndex, t));
                            }
                            else
                            {
                                lss.points.push_back(splinePoints.interpolate(strandSegmentIndex, t));
                            }
                        }

                        if (curve->width.data())
                        {
                            lss.radius.push_back(splineWidth.interpolate(strandSegmentIndex, t));
                        }
                        else
                        {
                            lss.radius.push_back(1.0f);
                        }

                        if (curve->uvs.data())
                        {
                            lss.texCoords.push_back(splineUv.interpolate(strandSegmentIndex, t));
                        }
                        else
                        {
                            lss.texCoords.push_back(PXR_NS::GfVec2f(0.0f, 0.0f));
                        }
                    }
                }

                if (curve->wrap == CurveWrap::Pinned)
                {
                    lss.points.push_back(splinePoints.interpolatePinned(vertexCount - 2, 1.0f));
                    lss.radius.push_back(1.0f);
                    lss.texCoords.push_back(PXR_NS::GfVec2f(0.0f, 0.0f));
                }
                else
                {
                    lss.indices.push_back((uint32_t)lss.points.size());
                    lss.points.push_back(splinePoints.interpolate(vertexCount - 2, 1.0f));
                    lss.radius.push_back(1.0f);
                    lss.texCoords.push_back(PXR_NS::GfVec2f(0.0f, 0.0f));
                }
                lssStrands[curveIndex].push_back(lss);

                ptr += vertexCount;
            }
        }

        for (size_t i = 0; i < currentNode->curveInstances.size(); i++)
        {
            size_t curveIndex = currentNode->curveInstances[i];
            if (curveIndex < 0 || curveIndex >= stage->curves.size())
            {
                continue;
            }

            tinygltf::Node meshNode;
            const auto& curve = stage->curves[curveIndex];
            auto iter = uniqueNameCount.find(curve->name);
            if (iter == uniqueNameCount.end())
            {
                meshNode.name = curve->name;
                uniqueNameCount.insert({ curve->name, 0 });
            }
            else
            {
                meshNode.name = curve->name + "_" + std::to_string(iter->second);
                iter->second += 1;
            }

            meshNode.mesh = currentNode->curveInstances[i];
            model.nodes.push_back(meshNode);
            size_t nodeIndex = model.nodes.size() - 1;
            model.nodes[childIndex].children.push_back(nodeIndex);
        }
    }

    if (currentNode->lights.size() == 1)
    {
        mExtensionsUsed.insert("KHR_lights_punctual");
        size_t lightIndex = currentNode->lights[0];
        tinygltf::Value::Object extensionObject;
        extensionObject.insert({ "light", tinygltf::Value((int)lightIndex) });
        model.nodes[childIndex].extensions.insert({ "KHR_lights_punctual", tinygltf::Value(extensionObject) });
    }
    else
    {
        for (size_t i = 0; i < currentNode->lights.size(); i++)
        {
            size_t lightIndex = currentNode->lights[i];
            if (lightIndex < 0 || lightIndex >= stage->lights.size())
            {
                continue;
            }
            tinygltf::Node lightNode;
            const auto& light = stage->lights[lightIndex];
            auto iter = uniqueNameCount.find(light->name);
            if (iter == uniqueNameCount.end())
            {
                lightNode.name = light->name;
                uniqueNameCount.insert({ light->name, 0 });
            }
            else
            {
                lightNode.name = light->name + "_" + std::to_string(iter->second);
                iter->second += 1;
            }

            mExtensionsUsed.insert("KHR_lights_punctual");
            tinygltf::Value::Object extensionObject;
            extensionObject.insert({ "light", tinygltf::Value((int)lightIndex) });
            lightNode.extensions.insert({ "KHR_lights_punctual", tinygltf::Value(extensionObject) });

            model.nodes.push_back(lightNode);
            model.nodes[childIndex].children.push_back(model.nodes.size() - 1);
        }
    }
    if (currentNode->cameras.size() == 1)
    {
        model.nodes[childIndex].camera = currentNode->cameras[0];
    }
    else
    {
        // glTF only permits one camera per node
        for (size_t i = 0; i < currentNode->cameras.size(); i++)
        {
            size_t cameraIndex = currentNode->cameras[i];
            if (cameraIndex < 0 || cameraIndex >= stage->cameras.size())
            {
                continue;
            }

            tinygltf::Node cameraNode;
            const auto& camera = stage->cameras[cameraIndex];
            auto iter = uniqueNameCount.find(camera->name);
            if (iter == uniqueNameCount.end())
            {
                cameraNode.name = camera->name;
                uniqueNameCount.insert({ camera->name, 0 });
            }
            else
            {
                cameraNode.name = camera->name + "_" + std::to_string(iter->second);
                iter->second += 1;
            }

            cameraNode.camera = currentNode->cameras[i];
            model.nodes.push_back(cameraNode);
            model.nodes[childIndex].children.push_back(model.nodes.size() - 1);
        }
    }

    if (currentNode->isBoneNode)
    {
        if (currentNode->IsRootBone())
        {
            tinygltf::Skin skin;
            // Next node will be the skeleton root.
            skin.name = currentNode->name;
            skin.skeleton = model.nodes.size();
            size_t skinIndex = model.skins.size();
            currentSkin = skinIndex;
            model.skins.push_back(skin);
            skinInverseBindMatrices.clear();
        }

        size_t jointIndex = model.skins[currentSkin].joints.size();
        model.skins[currentSkin].joints.push_back(childIndex);
        mAllJointIndices.insert({ currentNode, jointIndex });

        auto bindMatrix = currentNode->bindTransform;
        bindMatrix.SetTranslateOnly(bindMatrix.ExtractTranslation() * scale);
        skinInverseBindMatrices.push_back(PXR_NS::GfMatrix4f(bindMatrix.GetInverse()));

        std::unordered_set<size_t> influencedMeshes;
        for (const auto& skinMesh : stage->skinMeshes)
        {
            if (skinMesh->skeletonRoot == currentNode)
            {
                influencedMeshes.insert(skinMesh->meshIndex);
            }
        }

        // Creates all skin mesh nodes.
        for (const size_t influencedSkinMesh : influencedMeshes)
        {
            tinygltf::Node skinMeshNode;
            skinMeshNode.name = stage->meshes[influencedSkinMesh]->name;
            skinMeshNode.skin = currentSkin;
            skinMeshNode.mesh = influencedSkinMesh;
            model.nodes[childIndex].children.push_back(model.nodes.size());
            model.nodes.push_back(skinMeshNode);
        }
    }

    for (size_t i = 0; i < currentNode->children.size(); i++)
    {
        PopulateStageNodeTree(model, stage, currentNode->children[i], childIndex, currentSkin, skinInverseBindMatrices, scale, lssStrands);
    }

    if (currentNode->IsRootBone())
    {
        model.skins[currentSkin].inverseBindMatrices = CreateAccessor(model, skinInverseBindMatrices, "Inverse Bind Matrices");
        skinInverseBindMatrices.clear();
    }
}

tinygltf::TextureInfo GltfExporter::ToTinyGltfTextureInfo(tinygltf::Model& model, const TextureReference& textureReference)
{
    tinygltf::TextureInfo textureInfo;
    textureInfo.texCoord = textureReference.uvIndex;
    tinygltf::Texture texture;
    tinygltf::Sampler sampler;
    sampler.wrapS = ToTinygltfTextureWrapMode(textureReference.wrapS);
    sampler.wrapT = ToTinygltfTextureWrapMode(textureReference.wrapT);
    model.samplers.push_back(sampler);
    texture.sampler = model.samplers.size() - 1;
    texture.source = textureReference.imageIndex;
    model.textures.push_back(texture);
    textureInfo.index = model.textures.size() - 1;

    tinygltf::Value extensionValue;
    if (FillTextureTransformExtensionValue(model, textureReference, extensionValue))
    {
        mExtensionsUsed.insert("KHR_texture_transform");
        textureInfo.extensions.insert({ "KHR_texture_transform", extensionValue });
    }

    return textureInfo;
}

OmniConverterBlobPtr GltfExporter::ReadTextureData(const StagePtr& stage, size_t imageIndex)
{
    if (imageIndex == -1 || imageIndex >= stage->images.size())
    {
        return nullptr;
    }

    auto texture = stage->images[imageIndex];
    if (!texture->blob)
    {
        return texture->blob;
    }

    texture->blob = mExportContext->converterContext.ReadFile(texture->realPath);
    return texture->blob;
};
