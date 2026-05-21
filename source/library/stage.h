// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "pxr_includes.h"
#include "thirdparty/tiny_gltf.h"
#include "usd_convert_asset_internal.h"

#include <memory>
#include <vector>

const static size_t INVALID_MATERIAL_INDEX = SIZE_MAX;
// This is empirical value for Omniverse Kit and Create.
const static float DEFAULT_EMISSIVE_INTENSITY = 10000.0f;

// Texture coordinate has lower left corner as its origin, and top-right as (1, 1)
struct UVTransform
{
    PXR_NS::GfVec2f translation = PXR_NS::GfVec2f(0.0f, 0.0f);
    PXR_NS::GfVec2f scale = PXR_NS::GfVec2f(1.0f, 1.0f);
    PXR_NS::GfVec3f rotation = PXR_NS::GfVec3f(0.0f, 0.0f, 0.0f); // In degrees
};

static inline bool operator==(const UVTransform& left, const UVTransform& right)
{
    return left.translation == right.translation && left.scale == right.scale && left.rotation == right.rotation;
}

static inline bool operator!=(const UVTransform& left, const UVTransform& right)
{
    return !(left == right);
}

enum class TextureWrapMode
{
    REPEAT,
    CLAMP,
    MIRROR
};

struct TextureImage
{
    std::string originalPath; // The original path string in the asset. It may not point to the correct disk path.
    std::string realPath; // The real absolute path of texture on the disk. If blob is not null, that
                          // means the texture is in memory, then this path will be empty.
    OmniConverterBlobPtr blob; // When it's not null, it means the texture is in-memory, otherwise, it will point to
                               // texture specified by realPath.
};

using TextureImagePtr = std::shared_ptr<TextureImage>;

static inline bool operator==(const TextureImage& left, const TextureImage& right)
{
    return left.realPath == right.realPath && left.blob == right.blob;
}

static inline bool operator!=(const TextureImage& left, const TextureImage& right)
{
    return !(left == right);
}

enum class TextureOutputMode
{
    AVERAGE, // Average all channels.
    ALPHA, // Alpha channel only.
    R, // Red channel only.
    G, // Green channel only.
    B, // Blue channel only.
    RGB, // RGB channels.
    RGBA // RGBA channels.
};

enum class TextureColorSpace
{
    AUTO, // Auto checking, this is supported by some material systems, like MDL, UsdPreviewSurface.
    SRGB,
    RAW
};

enum class MaterialTextureType
{
    START = 0,
    DIFFUSE = START,
    EMISSIVE,
    OPACITY,
    NORMAL,

    SPECULAR,
    GLOSSY,

    OCCLUSION,
    ROUGHNESS,
    METALLIC,

    CLEARCOAT,
    CLEARCOAT_ROUGHNESS,
    CLEARCOAT_NORMAL,

    TRANSMISSION,
    SHEEN,
    SHEEN_ROUGHNESS,

    SPECULAR_STRENGTH,

    IRIDESCENCE,
    IRIDESCENCE_THICKNESS,

    ANISOTROPY,

    END
};
constexpr size_t NUM_TEXTURE_TYPES = (int)MaterialTextureType::END;

struct TextureReference
{
    TextureReference()
    {
    }
    TextureReference(TextureOutputMode mode, TextureColorSpace space = TextureColorSpace::AUTO) : outputMode(mode), colorSpace(space)
    {
    }

    bool IsValid() const
    {
        return imageIndex != -1;
    }

    bool SingleChannelOutput() const
    {
        return outputMode == TextureOutputMode::R || outputMode == TextureOutputMode::G || outputMode == TextureOutputMode::B ||
               outputMode == TextureOutputMode::ALPHA;
    }

    // Texture index in the textures array of stage. @See Stage::textures.
    size_t imageIndex = -1;

    // Transform applied to uv coordinates.
    UVTransform transform;

    // Wrap mode.
    TextureWrapMode wrapS = TextureWrapMode::REPEAT;
    TextureWrapMode wrapT = TextureWrapMode::REPEAT;

    // The sample value is textureValue * scale + bias.
    PXR_NS::GfVec4f scale = PXR_NS::GfVec4f(1.0f, 1.0f, 1.0f, 1.0f);
    PXR_NS::GfVec4f bias = PXR_NS::GfVec4f(0.0f, 0.0f, 0.0f, 0.0f);

    // The texture output mode to use.
    TextureOutputMode outputMode = TextureOutputMode::RGB;

    // Color space.
    TextureColorSpace colorSpace = TextureColorSpace::AUTO;

    // The uv set to be used.
    size_t uvIndex = 0;
};

static inline bool operator==(const TextureReference& left, const TextureReference& right)
{
    return left.bias == right.bias && left.colorSpace == right.colorSpace && left.outputMode == right.outputMode && left.scale == right.scale &&
           left.imageIndex == right.imageIndex && left.transform == right.transform && left.wrapS == right.wrapS && left.wrapT == right.wrapT &&
           left.uvIndex == right.uvIndex;
}

static inline bool operator!=(const TextureReference& left, const TextureReference& right)
{
    return !(left == right);
}

enum class GLTFOpacityMode
{
    GLTF_OPAQUE = 0,
    GLTF_MASK, // When it's mask mode, the alpha_cutoff will be 0.5 by default.
    GLTF_BLEND // When it's blend mode, the alpha_cutoff will be 0 by default.
};

/**
 * Material can be defined with customized material loader to map by feeding raw material
 * parameters from converter. If customized loader is not used, materials will be loaded
 * with fallback material model, which supports the following PBR workflow:
 * MetallicRoughness
 * KHR_materials_pbrSpecularGlossiness
 * KHR_materials_clearcoat
 * KHR_materials_ior
 * KHR_materials_sheen
 * KHR_materials_transmission
 * KHR_materials_specular
 * KHR_materials_volume
 * KHR_materials_emissive_strength
 * KHR_materials_iridescence
 * KHR_materials_anisotropy
 */
struct Material
{
    std::string name;

    // If fallback is true, it means to skip the material loader
    // to use fallback mapping.
    bool fallback = true;

    // Raw asset material properties.
    // This is used by populating materials only to fetch all material informations.
    std::vector<OmniConverterMaterialProperty> rawAssetProperties;

    // When material is loaded with material loader provided by client,
    // it will return the following to be mapped into USD.
    std::string materialType; // It's used to identify material type for different assets.
                              // For FBX, it's FbxSurfaceMaterial.

    // If it's builtin material from Core Material Library.
    bool builtIn = false;

    // The MDL file path.
    std::string materialPath;

    // The entrypoint.
    std::string entryIdentifier;

    // Input properties of MDL module.
    std::vector<OmniConverterMaterialProperty> inputProperties;

    // Fallback mapping. Explicitly parsing materials from asset instead of loading it from material loader.
    // The following properties are mapped for UsdPreviewSurface or when material loader is not provided.

    // Use specular glossy or metallic roughness workflow
    bool useSpecularGlossyWorkflow = false;
    bool isOmniGlass = false;

    bool hasDiffuseColor = true;
    PXR_NS::GfVec3f diffuseColor = PXR_NS::GfVec3f(1.0f); // Base color

    bool hasEmissiveColor = false;
    PXR_NS::GfVec3f emissiveColor = PXR_NS::GfVec3f(0.0f, 0.0f, 0.0f);

    bool hasEmissiveStrength = false;
    float emissiveStrength = 1.0f;

    GLTFOpacityMode opacityMode = GLTFOpacityMode::GLTF_OPAQUE; // Alpha mode for glTF
    bool hasOpacity = false;
    float opacity = 1.0f;
    float opacityThreshold = 0.0f;

    // For specular glossy workflow.
    bool hasSpecularColor = false;
    PXR_NS::GfVec3f specularColor = PXR_NS::GfVec3f(1.0f);

    bool hasSpecularStrengthFactor = false;
    float specularStrength = 1.0;

    bool hasGlossyFactor = false;
    float glossyFactor = 0.0f;

    // Separate roughness texture instead of ORM.
    bool hasRoughnessFactor = false;
    float roughnessFactor = 0.0f;

    // Separate metallic texture instead of ORM.
    float hasMetallicFactor = false;
    float metallicFactor = 0.0f;

    bool hasOcclusionFactor = false;
    float occlusionFactor = 0.0f;

    bool hasClearCoatFactor = false;
    float clearCoatFactor = 0.0f;

    bool hasClearCoatRoughnessFactor = false;
    float clearCoatRoughnessFactor = 0.0f;

    bool hasTransmissionFactor = false;
    float transmissionFactor = 0.0f;

    bool hasSheenColor = false;
    PXR_NS::GfVec3f sheenColor = PXR_NS::GfVec3f(0.0f);

    bool hasSheenRoughnessFactor = false;
    float sheenRoughnessFactor = 0.0f;

    bool hasIor = false;
    float ior = 1.5f;

    bool thinWalled = true;

    bool hasAttenuationDistance = false;
    float attenuationDistance = std::numeric_limits<float>::max();

    bool hasAttenuationColor = false;
    PXR_NS::GfVec3f attenuationColor = PXR_NS::GfVec3f(1.0f);

    bool hasIridescenceFactor = false;
    float iridescenceFactor = 0.0f;

    bool hasIridescenceIor = false;
    float iridescenceIor = 1.3f;

    bool hasIridescenceThicknessMinimum = false;
    float iridescenceThicknessMinimum = 100.0f;

    bool hasIridescenceThicknessMaximum = false;
    float iridescenceThicknessMaximum = 400.0f;

    bool hasAnisotropyStrength = false;
    float anisotropyStrength = 0.0f;

    bool hasAnisotropyRotation = false;
    float anisotropyRotation = 0.0f;


    TextureReference textures[NUM_TEXTURE_TYPES] = {
        TextureReference(TextureOutputMode::RGB), // Diffuse
        TextureReference(TextureOutputMode::RGB), // Emissive
        TextureReference(TextureOutputMode::AVERAGE), // Opacity
        TextureReference(TextureOutputMode::RGB, TextureColorSpace::RAW), // Normal

        TextureReference(TextureOutputMode::RGB, TextureColorSpace::SRGB), // Specular
        TextureReference(TextureOutputMode::AVERAGE, TextureColorSpace::RAW), // Glossy

        TextureReference(TextureOutputMode::AVERAGE, TextureColorSpace::RAW), // Occlusion
        TextureReference(TextureOutputMode::AVERAGE, TextureColorSpace::RAW), // Roughness
        TextureReference(TextureOutputMode::AVERAGE, TextureColorSpace::RAW), // Metallic

        TextureReference(TextureOutputMode::AVERAGE, TextureColorSpace::RAW), // ClearCoat
        TextureReference(TextureOutputMode::AVERAGE, TextureColorSpace::RAW), // ClearCoat Roughness
        TextureReference(TextureOutputMode::RGB, TextureColorSpace::RAW), // ClearCoat Normal

        TextureReference(TextureOutputMode::R, TextureColorSpace::RAW), // Transmission

        TextureReference(TextureOutputMode::RGB), // Sheen
        TextureReference(TextureOutputMode::ALPHA, TextureColorSpace::RAW), // Sheen Roughness

        TextureReference(TextureOutputMode::AVERAGE, TextureColorSpace::RAW), // Specular Strength

        TextureReference(TextureOutputMode::R, TextureColorSpace::RAW), // Iridescence
        TextureReference(TextureOutputMode::G, TextureColorSpace::RAW), // Iridescence Thickness

        TextureReference(TextureOutputMode::RGB, TextureColorSpace::RAW) // Anisotropy (RG=direction, B=strength)
    };

    TextureReference& GetTextureReference(MaterialTextureType type)
    {
        return textures[(int)type];
    }

    const TextureReference& GetTextureReference(MaterialTextureType type) const
    {
        return textures[(int)type];
    }

    bool HasTextureReference(MaterialTextureType type) const
    {
        return textures[(int)type].IsValid();
    }

    void SetTextureReference(MaterialTextureType type, const TextureReference& reference)
    {
        textures[(int)type] = reference;
    }

    void ClearTextureReference(MaterialTextureType type)
    {
        textures[(int)type].imageIndex = -1;
    }

    size_t GetValidTexturesCount() const
    {
        size_t validTextureCount = 0;
        for (size_t i = (size_t)MaterialTextureType::START; i < (size_t)MaterialTextureType::END; i++)
        {
            if (HasTextureReference((MaterialTextureType)i))
            {
                validTextureCount += 1;
            }
        }

        return validTextureCount;
    }

    bool UseORMMap() const
    {
        auto metallic = GetTextureReference(MaterialTextureType::METALLIC);
        auto roughness = GetTextureReference(MaterialTextureType::ROUGHNESS);
        if (metallic.IsValid() && roughness.IsValid() && metallic.imageIndex == roughness.imageIndex)
        {
            return true;
        }

        return false;
    }

    bool UseSpecuarGlossyMap() const
    {
        auto specular = GetTextureReference(MaterialTextureType::SPECULAR);
        auto glossy = GetTextureReference(MaterialTextureType::GLOSSY);
        if (specular.IsValid() && glossy.IsValid() && specular.imageIndex == glossy.imageIndex)
        {
            return true;
        }

        return false;
    }
};
using MaterialPtr = std::shared_ptr<Material>;

static inline bool operator==(const Material& left, const Material& right)
{
    bool equal = left.name == right.name && left.clearCoatFactor == right.clearCoatFactor &&
                 left.clearCoatRoughnessFactor == right.clearCoatRoughnessFactor && left.diffuseColor == right.diffuseColor &&
                 left.emissiveColor == right.emissiveColor && left.entryIdentifier == right.entryIdentifier && left.fallback == right.fallback &&
                 left.glossyFactor == right.glossyFactor && left.metallicFactor == right.metallicFactor &&
                 left.occlusionFactor == right.occlusionFactor && left.opacity == right.opacity && left.opacityMode == right.opacityMode &&
                 left.opacityThreshold == right.opacityThreshold && left.roughnessFactor == right.roughnessFactor &&
                 left.specularColor == right.specularColor && left.transmissionFactor == right.transmissionFactor &&
                 left.sheenColor == right.sheenColor && left.sheenRoughnessFactor == right.sheenRoughnessFactor &&
                 left.specularStrength == right.specularStrength;
    if (equal)
    {
        for (size_t i = (size_t)MaterialTextureType::START; i < (size_t)MaterialTextureType::END; i++)
        {
            if (left.GetTextureReference((MaterialTextureType)i) != right.GetTextureReference((MaterialTextureType)i))
            {
                equal = false;
                break;
            }
        }
    }

    return equal;
}

static inline bool operator!=(const Material& left, const Material& right)
{
    return !(left == right);
}

static PXR_NS::GfVec3d ZERO_VEC_3D = PXR_NS::GfVec3d(0.0, 0.0, 0.0);
static PXR_NS::GfVec3d ONE_VEC_3D = PXR_NS::GfVec3d(1.0, 1.0, 1.0);

struct TranslateEulerScaleTransform
{
    TranslateEulerScaleTransform() = default;
    TranslateEulerScaleTransform(const PXR_NS::GfVec3d& translation, const PXR_NS::GfVec3d& rotation, const PXR_NS::GfVec3d& scaling)
        : t(translation), r(rotation), s(scaling)
    {
    }

    PXR_NS::GfVec3d t = ZERO_VEC_3D;
    PXR_NS::GfVec3d r = ZERO_VEC_3D; // Euler angles in XYZ order.
    PXR_NS::GfVec3d s = ONE_VEC_3D;

    bool operator==(const TranslateEulerScaleTransform& other) const
    {
        return t == other.t && r == other.r && s == other.s;
    }

    bool operator!=(const TranslateEulerScaleTransform& other) const
    {
        return !this->operator==(other);
    }
};

struct TranslateQuatScaleTransform
{
    TranslateQuatScaleTransform() = default;
    TranslateQuatScaleTransform(const PXR_NS::GfVec3d& translation, const PXR_NS::GfQuatd& rotation, const PXR_NS::GfVec3d& scaling)
        : t(translation), q(rotation), s(scaling)
    {
    }

    PXR_NS::GfVec3d t = ZERO_VEC_3D;
    PXR_NS::GfQuatd q = PXR_NS::GfQuatd(1.0, 0.0, 0.0, 0.0);
    PXR_NS::GfVec3d s = ONE_VEC_3D;

    bool operator==(const TranslateQuatScaleTransform& other) const
    {
        return t == other.t && q == other.q && s == other.s;
    }

    bool operator!=(const TranslateQuatScaleTransform& other) const
    {
        return !this->operator==(other);
    }
};

/*
 * Transform is defined with the local transform frame plus a pivot.
 * It can be accessed in 3 mode:
 * 1. Matrix.
 * 2. Translation-RotationXYZ (in Euler Angles)-Scale.
 * 3. Translation-Orient (in Quatenion)-Scale.
 */
struct Transform
{
    Transform() = default;

    Transform(const PXR_NS::GfVec3d& t, const PXR_NS::GfVec3d& e, const PXR_NS::GfVec3d& s)
    {
        SetTES(TranslateEulerScaleTransform(t, e, s));
    }

    Transform(const TranslateEulerScaleTransform& tes)
    {
        SetTES(tes);
    }

    Transform(const PXR_NS::GfVec3d& t, const PXR_NS::GfQuatd& q, const PXR_NS::GfVec3d& s)
    {
        SetTQS(TranslateQuatScaleTransform(t, q, s));
    }

    Transform(const TranslateQuatScaleTransform& tqs)
    {
        SetTQS(tqs);
    }

    Transform(const PXR_NS::GfMatrix4d& matrix)
    {
        SetMatrix(matrix);
    }

    void SetPivot(const PXR_NS::GfVec3d& pivot);

    PXR_NS::GfVec3d GetPivot() const;

    void SetTES(const TranslateEulerScaleTransform& tes);

    TranslateEulerScaleTransform GetTES() const;

    void SetTQS(const TranslateQuatScaleTransform& tqs);

    TranslateQuatScaleTransform GetTQS() const;

    void SetMatrix(const PXR_NS::GfMatrix4d& matrix);

    PXR_NS::GfMatrix4d GetMatrix() const;

    PXR_NS::GfVec3d GetTranslate() const;

    PXR_NS::GfVec3d GetScale() const;

    void SetScale(const PXR_NS::GfVec3d& scale);

    PXR_NS::GfVec3d GetRotationXYZ() const;

    bool IsIdentity() const;

    bool operator==(const Transform& other) const;

    bool operator!=(const Transform& other) const
    {
        return !this->operator==(other);
    }

private:

    mutable PXR_NS::GfMatrix4d mMatrix = PXR_NS::GfMatrix4d(1.0);
    mutable bool mMatrixIsDirty = false;
    mutable TranslateEulerScaleTransform mTES;
    mutable bool mTESIsDirty = false;
    mutable TranslateQuatScaleTransform mTQS;
    mutable bool mTQSIsDirty = false;
    mutable PXR_NS::GfVec3d mPivot = ZERO_VEC_3D;
};

struct TransformTimesamples
{
public:

    TransformTimesamples()
    {
    }

    // <t, r, s> must have the same size, or part of them are empty.
    TransformTimesamples(const PXR_NS::VtVec3dArray& t, const PXR_NS::VtVec3dArray& r, const PXR_NS::VtVec3dArray& s)
        : mTranslations(t), mScales(s), mRotationXYZ(r)
    {
    }

    // <t, q, s> must have the same size, or part of them are empty.
    TransformTimesamples(const PXR_NS::VtVec3dArray& t, const PXR_NS::VtQuatdArray& q, const PXR_NS::VtVec3dArray& s)
        : mTranslations(t), mScales(s), mOrients(q)
    {
    }

    bool Empty() const;

    size_t Size() const
    {
        // <t, r, s> or <t, q, s> are saved with equal size or part of them are empty.
        if (mTranslations.size() > 0)
        {
            return mTranslations.size();
        }
        else if (mScales.size() > 0)
        {
            return mScales.size();
        }
        else if (mRotationXYZ.size() > 0)
        {
            return mRotationXYZ.size();
        }
        else
        {
            return mOrients.size();
        }
    }

    void SetTranslationSamples(const PXR_NS::VtVec3dArray& samples)
    {
        mTranslations = samples;
    }

    const PXR_NS::VtVec3dArray& GetTranslationSamples() const
    {
        return mTranslations;
    }

    void SetScaleSamples(const PXR_NS::VtVec3dArray& samples)
    {
        mScales = samples;
    }

    const PXR_NS::VtVec3dArray& GetScaleSamples() const
    {
        return mScales;
    }

    const PXR_NS::VtVec3dArray& GetRotationXYZSamples() const;

    const PXR_NS::VtQuatdArray& GetOrientSamples() const;

private:

    PXR_NS::VtVec3dArray mTranslations;
    mutable PXR_NS::VtVec3dArray mRotationXYZ;
    mutable PXR_NS::VtQuatdArray mOrients;
    PXR_NS::VtVec3dArray mScales;
};
using TransformAnimationTracks = std::vector<TransformTimesamples>;

struct StageNode
{
    StageNode(const std::string& nodeName = "", bool tes = false) : name(nodeName), useTES(tes)
    {
    }

    std::string name;

    // Use Euler angles for rotation if it's true. Quatenion otherwise.
    bool useTES = false;
    Transform localTransform;

    // The size of the following tracks must be the same as Stage::animationTracks,
    // or it's empty that means it has no keyed samples. If one of the track is empty,
    // it means it has no keyed samples for this track.
    // It's possible that the current track has less samples than the AnimationTrack::keyFrames.
    TransformAnimationTracks transformAnimationTracks;

    // Cached world transform matrix in stage tree
    PXR_NS::GfMatrix4d worldTransformMatrix = PXR_NS::GfMatrix4d(1.0);

    std::vector<size_t> staticMeshInstances; // Indices of mesh instances to global mesh array in the stage.
    std::vector<size_t> cameras; // Indices of camera instances to global camera array in the stage.
    std::vector<size_t> lights; // Indices of light instances to global light array in the stage.
    std::vector<size_t> curveInstances; // Indices of curve instances to global curve array in the stage.

    std::weak_ptr<StageNode> parent;
    std::vector<std::shared_ptr<StageNode>> children;

    // If this node is a bone node.
    bool isBoneNode = false;
    bool useOrderForAnimation = false;
    PXR_NS::GfMatrix4d bindTransform = PXR_NS::GfMatrix4d(1.0);
    PXR_NS::GfMatrix4d restTransform = PXR_NS::GfMatrix4d(1.0);
    PXR_NS::GfMatrix4d orderTransform = PXR_NS::GfMatrix4d(1.0);

    bool IsRootBone() const
    {
        if (!isBoneNode)
        {
            return false;
        }
        auto parentNode = parent.lock();
        while (parentNode)
        {
            if (parentNode->isBoneNode)
            {
                return false;
            }
            parentNode = parentNode->parent.lock();
        }
        return isBoneNode;
    }

    PXR_NS::GfMatrix4d ComputeLocalToWorldTransform(size_t animTrackIndex, size_t frameIndex);
};
using StageNodePtr = std::shared_ptr<StageNode>;

struct MeshGeomSubset
{
    std::string name;
    PXR_NS::VtArray<int> faceIndices; // Face indices of mesh.
    size_t materialIndex = INVALID_MATERIAL_INDEX; // Index of global material array in Stage.
};

using PointCacheFrame = PXR_NS::VtArray<PXR_NS::GfVec3f>; // One frame of all points
using PointCacheTimesamples = PXR_NS::VtArray<PointCacheFrame>; // All frames

using NormalCacheFrame = PXR_NS::VtArray<PXR_NS::GfVec3f>; // One frame of all normals
using NormalCacheTimesamples = PXR_NS::VtArray<NormalCacheFrame>; // All frames

struct Mesh
{
    std::string name; // Must not be empty.
    PXR_NS::VtArray<PXR_NS::GfVec3f> points; // Point must not be empty.
    PointCacheTimesamples pointCacheTimesamples;
    NormalCacheTimesamples normalCacheTimesamples;
    double timeSampleStart = 0.0;
    double timeSampleEnd = 0.0;
    PXR_NS::VtArray<int> faceVertexCounts; // Face vertices must not be empty.
    PXR_NS::VtArray<int> faceVertexIndices; // Face vertex mapping. Must not be empty.
    PXR_NS::VtArray<PXR_NS::GfVec3f> normals; // Face varying normals
    PXR_NS::VtArray<PXR_NS::VtArray<PXR_NS::GfVec2f>> uvs; // Face varying uvs
    PXR_NS::VtArray<PXR_NS::VtIntArray> uvIndices; // Face uv mapping.
    PXR_NS::VtArray<PXR_NS::VtArray<PXR_NS::GfVec3f>> colors; // Face varying colors
    std::vector<MeshGeomSubset> meshSubsets; // If it has only one mesh subset, it's whole mesh.
    bool hasFaceVaryingNormals = false;
    bool hasFaceVaryingUVs = false;
    bool hasFaceVaryingColors = false;
};
using MeshPtr = std::shared_ptr<Mesh>;

// A mesh can be skinned to multiple meshes.
struct SkinMesh
{
    SkinMesh(size_t index) : meshIndex(index)
    {
    }

    size_t meshIndex; // Indices of mesh instances to global mesh array in Stage.

    StageNodePtr skeletonRoot = nullptr; // Skeleton root of the mesh skinned to.
    std::vector<StageNodePtr> influencedBoneNodes; // All influenced bone nodes of the skeleton.
    PXR_NS::VtFloatArray jointWeights; // Influenced weights of each joint to each vertex.
    PXR_NS::VtIntArray jointInfluences; // Influenced joint indices (indexed with influencedBoneNodes) of each vertex.
    size_t numBoneInfluencesPerVertex = 0; // Number of joints influence each vertex.
    PXR_NS::GfMatrix4d geomBindTransform = PXR_NS::GfMatrix4d(1.0); // World transform relative to skeleton space this
                                                                    // skinned mesh attaches to.
};
using SkinMeshPtr = std::shared_ptr<SkinMesh>;

enum class CurveType
{
    Linear,
    Cubic,
};

enum class CurveBasis
{
    Bezier,
    Bspline,
    CatmullRom,
};

enum class CurveWrap
{
    NonPeriodic,
    Periodic,
    Pinned,
};

struct Curve
{
    std::string name;
    CurveType type;
    CurveBasis basis;
    CurveWrap wrap;
    PXR_NS::VtIntArray vertexCounts;
    PXR_NS::VtArray<PXR_NS::GfVec3f> points;
    PXR_NS::VtArray<PXR_NS::GfVec2f> uvs;
    PXR_NS::VtArray<float> width;
    uint32_t subdivPerSegment;
};
using CurvePtr = std::shared_ptr<Curve>;

enum class LightType
{
    POINT,
    SPHERE,
    DISTANT,
    RECT
};

struct Light
{
    std::string name;
    LightType type;
    PXR_NS::GfVec3f color;
    float intensity;
    float outAngle;
    float innerAngle;
};
using LightPtr = std::shared_ptr<Light>;

struct Camera
{
    std::string name;

    // View matrix params in world space
    PXR_NS::GfVec3d lookAt = PXR_NS::GfVec3d(0.0, 0.0, -1.0); // In world units
    std::vector<PXR_NS::VtVec3dArray> lookAtAnimations;
    PXR_NS::GfVec3d position = PXR_NS::GfVec3d(0.0, 0.0, 0.0); // In world units
    std::vector<PXR_NS::VtVec3dArray> positionAnimations;
    PXR_NS::GfVec3d up = PXR_NS::GfVec3d(0.0, 1.0, 0.0);
    std::vector<PXR_NS::VtVec3dArray> upAnimations;

    PXR_NS::GfCamera::Projection projectionType = PXR_NS::GfCamera::Perspective;
    float clippingNear = 1.0f; // In world units
    float clippingFar = 10000.0f; // In world units
    float horizonalAperture = 0.0f; // In tenth of world units
    float verticallAperture = 0.0f; // In tenth of world units
    float focalLength = 0.0f; // In tenth of world units
    float focusDistance = 400.0f; // In world units
};
using CameraPtr = std::shared_ptr<Camera>;

struct AnimationTrack
{
    std::string name;
    size_t keyFrames = 0;
    double fps = 24.0;
};

struct Stage
{
    bool yAxis = true;
    double worldUnits = 0.01; // CM by default
    StageNodePtr rootNode;
    std::vector<MeshPtr> meshes;
    std::vector<SkinMeshPtr> skinMeshes;
    std::vector<CurvePtr> curves;
    std::vector<CameraPtr> cameras;
    std::vector<LightPtr> lights;
    std::vector<MaterialPtr> materials;
    std::vector<AnimationTrack> animationTracks;
    std::vector<TextureImagePtr> images;
    size_t maxKeyFrames = 0;
    size_t defaultBoundCamera = -1;
    double startTime = 0.0;
    double endTime = 0.0;
    double mutiplier = 1.0;


    bool isExportFromBlender = false;
};
using StagePtr = std::shared_ptr<Stage>;
