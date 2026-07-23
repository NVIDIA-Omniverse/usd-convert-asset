// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "gltf_importer.h"

#include "../../common/common.h"
#include "../../utils/utils.h"
#include "gltf_loader_util.h"

#include <sha1.hpp>
#include <stb_image.h>

using namespace omni::assetconverter::importer::gltf;

#include <algorithm>
#include <cmath>
#include <numeric>

#define DEFAULT_FPS 24.0f

static bool InRange(size_t value, size_t low, size_t high)
{
    return (value >= low && value < high);
}

static size_t GetGltfComponentSize(int componentType)
{
    switch (componentType)
    {
        case TINYGLTF_COMPONENT_TYPE_BYTE:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return 1;
        case TINYGLTF_COMPONENT_TYPE_SHORT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            return 2;
        case TINYGLTF_COMPONENT_TYPE_INT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        case TINYGLTF_COMPONENT_TYPE_FLOAT:
            return 4;
        case TINYGLTF_COMPONENT_TYPE_DOUBLE:
            return 8;
        default:
            return 0;
    }
}

static size_t GetGltfTypeComponentCount(int type)
{
    switch (type)
    {
        case TINYGLTF_TYPE_SCALAR:
            return 1;
        case TINYGLTF_TYPE_VEC2:
            return 2;
        case TINYGLTF_TYPE_VEC3:
            return 3;
        case TINYGLTF_TYPE_VEC4:
        case TINYGLTF_TYPE_MAT2:
            return 4;
        case TINYGLTF_TYPE_MAT3:
            return 9;
        case TINYGLTF_TYPE_MAT4:
            return 16;
        default:
            return 0;
    }
}

static bool ValidateGltfAccessorExtents(const tinygltf::Model& model, std::string& error)
{
    for (size_t accessorIndex = 0; accessorIndex < model.accessors.size(); ++accessorIndex)
    {
        const auto& accessor = model.accessors[accessorIndex];

        // An omitted bufferView is valid for sparse accessors. Existing import
        // paths handle unsupported sparse/Draco data without dereferencing it.
        if (accessor.bufferView < 0)
        {
            continue;
        }

        if (accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
        {
            error = "glTF accessor " + std::to_string(accessorIndex) + " references an invalid bufferView.";
            return false;
        }

        const auto& bufferView = model.bufferViews[accessor.bufferView];
        if (bufferView.buffer < 0 || bufferView.buffer >= static_cast<int>(model.buffers.size()))
        {
            error = "glTF accessor " + std::to_string(accessorIndex) + " references an invalid buffer.";
            return false;
        }

        const auto& buffer = model.buffers[bufferView.buffer];
        if (bufferView.byteOffset > buffer.data.size() || bufferView.byteLength > buffer.data.size() - bufferView.byteOffset)
        {
            error = "glTF bufferView for accessor " + std::to_string(accessorIndex) + " exceeds its buffer.";
            return false;
        }

        if (accessor.byteOffset > bufferView.byteLength)
        {
            error = "glTF accessor " + std::to_string(accessorIndex) + " starts outside its bufferView.";
            return false;
        }

        const int byteStride = accessor.ByteStride(bufferView);
        const size_t componentSize = GetGltfComponentSize(accessor.componentType);
        const size_t componentCount = GetGltfTypeComponentCount(accessor.type);
        if (byteStride <= 0 || componentSize == 0 || componentCount == 0)
        {
            error = "glTF accessor " + std::to_string(accessorIndex) + " has an invalid element layout.";
            return false;
        }

        const size_t elementSize = componentSize * componentCount;
        const size_t available = bufferView.byteLength - accessor.byteOffset;
        if (accessor.count > 0 && (elementSize > available || accessor.count - 1 > (available - elementSize) / static_cast<size_t>(byteStride)))
        {
            error = "glTF accessor " + std::to_string(accessorIndex) + " exceeds its bufferView.";
            return false;
        }
    }

    return true;
}

static TextureWrapMode ToTextureWrapMode(int wrapMode)
{
    if (wrapMode == TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE)
    {
        return TextureWrapMode::CLAMP;
    }
    else if (wrapMode == TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT)
    {
        return TextureWrapMode::MIRROR;
    }
    else
    {
        return TextureWrapMode::REPEAT;
    }
}

template <typename ResultValueType>
static bool GetArrayValues(const tinygltf::Value& valueObject, std::vector<ResultValueType>& result, int expectedSize)
{
    if (!valueObject.IsArray())
    {
        return false;
    }

    const auto& arrayObject = valueObject.Get<tinygltf::Value::Array>();
    if (expectedSize != arrayObject.size())
    {
        return false;
    }

    for (size_t i = 0; i < arrayObject.size(); i++)
    {
        ResultValueType value = arrayObject[i].Get<ResultValueType>();
        result.push_back(value);
    }

    return true;
}

static GLTFOpacityMode FromAlphaModeString(const std::string& alphaModeString)
{
    if (alphaModeString != "OPAQUE")
    {
        if (alphaModeString == "BLEND")
        {
            return GLTFOpacityMode::GLTF_BLEND;
        }
        else
        {
            return GLTFOpacityMode::GLTF_MASK;
        }
    }

    return GLTFOpacityMode::GLTF_OPAQUE;
}

static inline unsigned char FromHex(unsigned char ch)
{
    if (ch <= '9' && ch >= '0')
    {
        ch -= '0';
    }
    else if (ch <= 'f' && ch >= 'a')
    {
        ch -= 'a' - 10;
    }
    else if (ch <= 'F' && ch >= 'A')
    {
        ch -= 'A' - 10;
    }
    else
    {
        ch = 0;
    }
    return ch;
}

static const std::string UrlDecode(const std::string& str)
{
    using namespace std;
    string result;
    string::size_type i;
    for (i = 0; i < str.size(); ++i)
    {
        if (str[i] == '+')
        {
            result += ' ';
        }
        else if (str[i] == '%' && str.size() > i + 2)
        {
            const unsigned char ch1 = FromHex(static_cast<unsigned char>(str[i + 1]));
            const unsigned char ch2 = FromHex(static_cast<unsigned char>(str[i + 2]));
            const unsigned char ch = static_cast<unsigned char>((ch1 << 4) | ch2);
            result += static_cast<char>(ch);
            i += 2;
        }
        else
        {
            result += str[i];
        }
    }

    return result;
}

static bool GetTextureInfo(const tinygltf::Value& valueObject, const std::string& key, tinygltf::TextureInfo& textureInfo)
{
    const auto& textureInfoObject = valueObject.Get(key);
    if (!textureInfoObject.IsObject())
    {
        return false;
    }

    const auto& indexObject = textureInfoObject.Get("index");
    if (indexObject.IsInt()) // MUST
    {
        textureInfo.index = indexObject.GetNumberAsInt();
    }
    else
    {
        return false;
    }

    const auto& texCoordObject = textureInfoObject.Get("texCoord");
    if (texCoordObject.IsInt())
    {
        textureInfo.texCoord = texCoordObject.GetNumberAsInt();
    }

    const auto& extensionsObject = textureInfoObject.Get("extensions");
    if (extensionsObject.IsObject())
    {
        for (const std::string& key : extensionsObject.Keys())
        {
            textureInfo.extensions.insert({ key, extensionsObject.Get(key) });
        }
    }

    textureInfo.extras = textureInfoObject.Get("extras");

    return true;
}

static bool GetUVTransformFromExtension(const tinygltf::ExtensionMap& extensionMap, UVTransform& transform)
{
    auto iter = extensionMap.find("KHR_texture_transform");
    if (iter != extensionMap.end())
    {
        const auto& offsetObject = iter->second.Get("offset");
        std::vector<double> offset;
        if (GetArrayValues(offsetObject, offset, 2))
        {
            transform.translation = PXR_NS::GfVec2f(offset[0], offset[1]);
        }

        const auto& scaleObject = iter->second.Get("scale");
        std::vector<double> scale;
        if (GetArrayValues(scaleObject, scale, 2))
        {
            transform.scale = PXR_NS::GfVec2f(scale[0], scale[1]);
        }

        const auto& rotationObject = iter->second.Get("rotation");
        const float rotationRadians = rotationObject.GetNumberAsDouble();
        transform.rotation = PXR_NS::GfVec3f(PXR_NS::GfRadiansToDegrees(rotationRadians));

        // Change coordinates is requred to map UV transformations into the space of converter ones.
        // For glTF, the rotation is around at top left (0, 1) in converter space,
        // while converter's rotation is around origin (0, 0). It needs to correct the translation to match the original
        // transform.
        float rotation = -rotationRadians;
        const float rcos(cos(rotation));
        const float rsin(sin(rotation));
        const float orignalTx = transform.translation[0];
        const float orignalTy = transform.translation[1];
        const float sx = transform.scale[0];
        const float sy = transform.scale[1];
        const float tx = orignalTx - sy * rsin;
        const float ty = 1 - sy * rcos - orignalTy;
        transform.translation = PXR_NS::GfVec2f(tx, ty);

        return true;
    }

    return false;
}

static PXR_NS::VtIntArray PopulateIntegerBufferData(tinygltf::Model& model, size_t bufferId)
{
    if (bufferId == -1)
    {
        return {};
    }

    std::unique_ptr<IntArrayBase> intArrayPtr = nullptr;
    const auto& accessor = model.accessors[bufferId];

    // Check if bufferView is valid (can be -1 for Draco compressed meshes)
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
    {
        return {};
    }

    const auto& bufferView = model.bufferViews[accessor.bufferView];

    // Check if buffer is valid
    if (bufferView.buffer < 0 || bufferView.buffer >= static_cast<int>(model.buffers.size()))
    {
        return {};
    }

    const auto& buffer = model.buffers[bufferView.buffer];

    // Check if buffer data is available
    if (buffer.data.empty())
    {
        return {};
    }

    const auto dataAddress = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
    const int byteStride = accessor.ByteStride(bufferView);
    const size_t count = accessor.count;
    if (accessor.type != TINYGLTF_TYPE_SCALAR)
    {
        return {};
    }

    switch (accessor.componentType)
    {
        case TINYGLTF_COMPONENT_TYPE_BYTE:
            intArrayPtr = std::unique_ptr<IntArray<char>>(new IntArray<char>(ArrayAdapter<char>(dataAddress, count, byteStride)));
            break;

        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            intArrayPtr = std::unique_ptr<IntArray<unsigned char>>(
                new IntArray<unsigned char>(ArrayAdapter<unsigned char>(dataAddress, count, byteStride))
            );
            break;

        case TINYGLTF_COMPONENT_TYPE_SHORT:
            intArrayPtr = std::unique_ptr<IntArray<short>>(new IntArray<short>(ArrayAdapter<short>(dataAddress, count, byteStride)));
            break;

        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            intArrayPtr = std::unique_ptr<IntArray<unsigned short>>(
                new IntArray<unsigned short>(ArrayAdapter<unsigned short>(dataAddress, count, byteStride))
            );
            break;

        case TINYGLTF_COMPONENT_TYPE_INT:
            intArrayPtr = std::unique_ptr<IntArray<int>>(new IntArray<int>(ArrayAdapter<int>(dataAddress, count, byteStride)));
            break;

        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            intArrayPtr = std::unique_ptr<IntArray<unsigned int>>(
                new IntArray<unsigned int>(ArrayAdapter<unsigned int>(dataAddress, count, byteStride))
            );
            break;
        default:
            break;
    }

    if (!intArrayPtr)
    {
        return {};
    }

    PXR_NS::VtIntArray intArray;
    const auto& values = *intArrayPtr;
    for (size_t i = 0; i < values.size(); i++)
    {
        intArray.push_back(values[i]);
    }

    return intArray;
}

static PXR_NS::VtVec4iArray PopulateVec4iBufferData(tinygltf::Model& model, size_t bufferId)
{
    const auto& accessor = model.accessors[bufferId];

    // Check if bufferView is valid (can be -1 for Draco compressed meshes)
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
    {
        return {};
    }

    const auto& bufferView = model.bufferViews[accessor.bufferView];

    // Check if buffer is valid
    if (bufferView.buffer < 0 || bufferView.buffer >= static_cast<int>(model.buffers.size()))
    {
        return {};
    }

    const auto& buffer = model.buffers[bufferView.buffer];

    // Check if buffer data is available
    if (buffer.data.empty())
    {
        return {};
    }

    const auto dataAddress = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
    const int byteStride = accessor.ByteStride(bufferView);
    const size_t count = accessor.count;
    if (accessor.type != TINYGLTF_TYPE_VEC4)
    {
        return {};
    }

    PXR_NS::VtVec4iArray values;
    switch (accessor.componentType)
    {
        case TINYGLTF_COMPONENT_TYPE_BYTE:
        {
            ArrayAdapter<std::array<char, 4>> adapter(dataAddress, count, byteStride);
            for (size_t i = 0; i < adapter.elemCount; i++)
            {
                std::array<char, 4> value = adapter[i];
                values.push_back(PXR_NS::GfVec4i(value[0], value[1], value[2], value[3]));
            }
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        {
            ArrayAdapter<std::array<unsigned char, 4>> adapter(dataAddress, count, byteStride);
            for (size_t i = 0; i < adapter.elemCount; i++)
            {
                std::array<unsigned char, 4> value = adapter[i];
                values.push_back(PXR_NS::GfVec4i(value[0], value[1], value[2], value[3]));
            }
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_SHORT:
        {
            ArrayAdapter<std::array<short, 4>> adapter(dataAddress, count, byteStride);
            for (size_t i = 0; i < adapter.elemCount; i++)
            {
                std::array<short, 4> value = adapter[i];
                values.push_back(PXR_NS::GfVec4i(value[0], value[1], value[2], value[3]));
            }
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        {
            ArrayAdapter<std::array<unsigned short, 4>> adapter(dataAddress, count, byteStride);
            for (size_t i = 0; i < adapter.elemCount; i++)
            {
                std::array<unsigned short, 4> value = adapter[i];
                values.push_back(PXR_NS::GfVec4i(value[0], value[1], value[2], value[3]));
            }
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_INT:
        {
            ArrayAdapter<std::array<int, 4>> adapter(dataAddress, count, byteStride);
            for (size_t i = 0; i < adapter.elemCount; i++)
            {
                std::array<int, 4> value = adapter[i];
                values.push_back(PXR_NS::GfVec4i(value[0], value[1], value[2], value[3]));
            }
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        {
            ArrayAdapter<std::array<unsigned int, 4>> adapter(dataAddress, count, byteStride);
            for (size_t i = 0; i < adapter.elemCount; i++)
            {
                std::array<unsigned int, 4> value = adapter[i];
                values.push_back(PXR_NS::GfVec4i(value[0], value[1], value[2], value[3]));
            }
            break;
        }
        default:
            break;
    }

    return values;
}

static PXR_NS::VtIntArray PopulateFaceVertexIndices(tinygltf::Model& model, tinygltf::Primitive& primitive)
{
    const auto& indices = PopulateIntegerBufferData(model, primitive.indices);
    if (indices.size() < 2)
    {
        return {};
    }

    PXR_NS::VtIntArray faceIndices;
    if (primitive.mode == TINYGLTF_MODE_TRIANGLE_FAN)
    {
        for (size_t i = 2; i < indices.size(); i++)
        {
            faceIndices.push_back(indices[0]);
            faceIndices.push_back(indices[i - 1]);
            faceIndices.push_back(indices[i]);
        }
    }
    else if (primitive.mode == TINYGLTF_MODE_TRIANGLE_STRIP)
    {
        for (size_t i = 2; i < indices.size(); i++)
        {
            faceIndices.push_back(indices[i - 2]);
            faceIndices.push_back(indices[i - 1]);
            faceIndices.push_back(indices[i]);
        }
    }
    else if (primitive.mode == TINYGLTF_MODE_TRIANGLES)
    {
        for (size_t i = 0; i < indices.size(); i++)
        {
            faceIndices.push_back(indices[i]);
        }
    }

    return faceIndices;
}

template <typename ValueType, typename DoubleType>
static ValueType GetIntepolatedValue(
    const PXR_NS::VtDoubleArray& times,
    const PXR_NS::VtArray<DoubleType>& values,
    double currentTime,
    const std::string& interpolationType,
    const std::function<ValueType(const DoubleType&, const DoubleType&, double)>& interpolator
)
{
    if (currentTime <= times.front())
    {
        return ConstructFrom<DoubleType, ValueType>(values[0]);
    }

    // Find the first time that's greater than current time.
    auto upper = std::upper_bound(times.begin(), times.end(), currentTime);
    size_t position = std::distance(times.begin(), upper) - 1;
    if (position == times.size() - 1)
    {
        return ConstructFrom<DoubleType, ValueType>(values[position]);
    }

    size_t nextPosition = position + 1;
    if (StringUtils::ToLower(interpolationType) == "step")
    {
        return ConstructFrom<DoubleType, ValueType>(values[position]);
    }
    else // Linear by default
    {
        double deltaTime = times[nextPosition] - times[position];
        double factor = (currentTime - times[position]) / deltaTime;

        auto start = values[position];
        auto end = values[nextPosition];
        return interpolator(start, end, factor);
    }
}

template <typename ValueType, typename Traits = TinygltfTypeTraits<ValueType>>
static PXR_NS::VtArray<ValueType> PopulateNumberBufferData(const tinygltf::Model& model, int bufferIndex, bool cubicSpline = false, double scale = 1.0f)
{
    const auto attribAccessor = model.accessors[bufferIndex];

    // Check if bufferView is valid (can be -1 for Draco compressed meshes)
    if (attribAccessor.bufferView < 0 || attribAccessor.bufferView >= static_cast<int>(model.bufferViews.size()))
    {
        return {};
    }

    const auto& bufferView = model.bufferViews[attribAccessor.bufferView];

    // Check if buffer is valid
    if (bufferView.buffer < 0 || bufferView.buffer >= static_cast<int>(model.buffers.size()))
    {
        return {};
    }

    const auto& buffer = model.buffers[bufferView.buffer];

    // Check if buffer data is available
    if (buffer.data.empty())
    {
        return {};
    }

    const auto dataPtr = buffer.data.data() + bufferView.byteOffset + attribAccessor.byteOffset;
    const int byte_stride = attribAccessor.ByteStride(bufferView);
    const size_t count = attribAccessor.count;

    using FloatType = typename Traits::FloatType;
    using DoubleType = typename Traits::DoubleType;
    if (attribAccessor.type != Traits::value)
    {
        return {};
    }

    PXR_NS::VtArray<ValueType> attributeValues;
    if (attribAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
    {
        ArrayAdapter<FloatType> adapter(dataPtr, count, byte_stride);
        // Algorithm is referred from Assimp to handle cubiespline interpolation.
        size_t ii = cubicSpline && adapter.elemCount > 1 ? 1 : 0;
        size_t step = cubicSpline && adapter.elemCount > 1 ? 3 : 1;
        for (size_t i = 0; i < adapter.elemCount; i++)
        {
            attributeValues.push_back(ValueType(adapter[ii] * scale));
            ii += step;
        }
    }
    else if (attribAccessor.componentType == TINYGLTF_COMPONENT_TYPE_DOUBLE)
    {
        ArrayAdapter<DoubleType> adapter(dataPtr, count, byte_stride);
        size_t ii = cubicSpline && adapter.elemCount > 1 ? 1 : 0;
        size_t step = cubicSpline && adapter.elemCount > 1 ? 3 : 1;
        for (size_t i = 0; i < adapter.elemCount; i++)
        {
            attributeValues.push_back(ValueType(adapter[ii] * scale));
            ii += step;
        }
    }

    return attributeValues;
}

static Transform GetNodeLocalTransform(const tinygltf::Model& model, int nodeIndex, double scale = 1.0)
{
    Transform transform;

    const auto& node = model.nodes[nodeIndex];
    if (node.matrix.size() == 16)
    {
        PXR_NS::GfMatrix4d matrix(
            node.matrix[0],
            node.matrix[1],
            node.matrix[2],
            node.matrix[3],
            node.matrix[4],
            node.matrix[5],
            node.matrix[6],
            node.matrix[7],
            node.matrix[8],
            node.matrix[9],
            node.matrix[10],
            node.matrix[11],
            node.matrix[12],
            node.matrix[13],
            node.matrix[14],
            node.matrix[15]
        );
        matrix.SetTranslateOnly(matrix.ExtractTranslation() * scale);
        transform.SetMatrix(matrix);
    }
    else
    {
        TranslateQuatScaleTransform tqs;
        if (node.translation.size() >= 3)
        {
            tqs.t = PXR_NS::GfVec3d(node.translation[0], node.translation[1], node.translation[2]) * scale;
        }

        if (node.scale.size() >= 3)
        {
            tqs.s = PXR_NS::GfVec3d(node.scale[0], node.scale[1], node.scale[2]);
        }

        if (node.rotation.size() >= 4)
        {
            tqs.q = PXR_NS::GfQuatd(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
        }
        transform.SetTQS(tqs);
    }

    return transform;
}

template <typename ValueType, typename Traits = TinygltfTypeTraits<ValueType>, typename ValueContainer = PXR_NS::VtArray<ValueType>>
static ValueContainer PopulateNumberAttribute(
    tinygltf::Model& model,
    tinygltf::Primitive& primitive,
    const std::string& attributeName,
    const PXR_NS::VtIntArray& indices,
    double scale = 1.0f
)
{
    int attributeType = Traits::value;
    using FloatValueType = typename Traits::FloatType;
    using DoubleValueType = typename Traits::DoubleType;

    auto iter = primitive.attributes.find(attributeName);
    if (iter == primitive.attributes.end())
    {
        return {};
    }

    bool faceVaryingAttribute = attributeName == "NORMAL" || attributeName.rfind("TEXCOORD", 0) == 0 || attributeName.rfind("COLOR", 0) == 0;
    const auto attribAccessor = model.accessors[iter->second];

    // Check if bufferView is valid (can be -1 for Draco compressed meshes)
    if (attribAccessor.bufferView < 0 || attribAccessor.bufferView >= static_cast<int>(model.bufferViews.size()))
    {
        // Log error for debugging
        std::string errorMsg = "Invalid bufferView index (" + std::to_string(attribAccessor.bufferView) + ") for attribute '" + attributeName + "'. ";
        errorMsg += "This may indicate Draco compression (KHR_draco_mesh_compression) which is not currently supported.";
        // Note: You may need to access a logging mechanism here
        // For now, returning empty to prevent crash
        return {};
    }

    const auto& bufferView = model.bufferViews[attribAccessor.bufferView];

    // Additional safety check for buffer index
    if (bufferView.buffer < 0 || bufferView.buffer >= static_cast<int>(model.buffers.size()))
    {
        return {};
    }

    const auto& buffer = model.buffers[bufferView.buffer];

    // Check if buffer data is available
    if (buffer.data.empty())
    {
        return {};
    }

    const auto dataPtr = buffer.data.data() + bufferView.byteOffset + attribAccessor.byteOffset;
    const int byte_stride = attribAccessor.ByteStride(bufferView);
    const size_t count = attribAccessor.count;

    if (attribAccessor.type != attributeType)
    {
        return {};
    }

    ValueContainer attributeValues;
    if (attribAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
    {
        ArrayAdapter<FloatValueType> adapter(dataPtr, count, byte_stride);
        if (faceVaryingAttribute)
        {
            for (size_t i = 0; i < indices.size() / 3; i++)
            {
                // Gets triangle indices
                auto f0 = indices[3 * i + 0];
                auto f1 = indices[3 * i + 1];
                auto f2 = indices[3 * i + 2];

                // check for index out of bound
                if (f0 >= adapter.elemCount || f1 >= adapter.elemCount || f2 >= adapter.elemCount)
                {
                    return {};
                }

                attributeValues.push_back(ValueType(adapter[f0] * scale));
                attributeValues.push_back(ValueType(adapter[f1] * scale));
                attributeValues.push_back(ValueType(adapter[f2] * scale));
            }
        }
        else
        {
            for (size_t i = 0; i < adapter.elemCount; ++i)
            {
                attributeValues.push_back(ValueType(adapter[i] * scale));
            }
        }
    }
    else if (attribAccessor.componentType == TINYGLTF_COMPONENT_TYPE_DOUBLE)
    {
        ArrayAdapter<DoubleValueType> adapter(dataPtr, count, byte_stride);
        if (faceVaryingAttribute)
        {
            for (size_t i = 0; i < indices.size() / 3; i++)
            {
                // Gets triangle indices
                auto f0 = indices[3 * i + 0];
                auto f1 = indices[3 * i + 1];
                auto f2 = indices[3 * i + 2];

                // check for index out of bound
                if (f0 >= adapter.elemCount || f1 >= adapter.elemCount || f2 >= adapter.elemCount)
                {
                    return {};
                }

                attributeValues.push_back(ValueType(adapter[f0] * scale));
                attributeValues.push_back(ValueType(adapter[f1] * scale));
                attributeValues.push_back(ValueType(adapter[f2] * scale));
            }
        }
        else
        {
            for (size_t i = 0; i < adapter.elemCount; ++i)
            {
                attributeValues.push_back(ValueType(adapter[i] * scale));
            }
        }
    }

    return attributeValues;
}

template <typename ValueType, typename DoubleType>
PXR_NS::VtArray<ValueType> PopulateTimesamplesFromAnimationSampler(
    const tinygltf::Model& model,
    const tinygltf::AnimationSampler& sampler,
    double frameDeltaTime,
    const std::function<ValueType(const DoubleType&, const DoubleType&, double)>& interpolator,
    double scale = 1.0
)
{
    PXR_NS::VtArray<double> keyframeTimes = PopulateNumberBufferData<double>(model, sampler.input);
    PXR_NS::VtArray<DoubleType> keyframeValues = PopulateNumberBufferData<DoubleType>(model, sampler.output, sampler.interpolation == "CUBICSPLINE");

    if (keyframeTimes.size() == 0)
    {
        return {};
    }

    PXR_NS::VtArray<ValueType> timesamples;
    double durationInSeconds = keyframeTimes.back();
    for (double currentTime = 0.0; currentTime < durationInSeconds + frameDeltaTime; currentTime += frameDeltaTime)
    {
        auto interpolatedValue = GetIntepolatedValue<ValueType, DoubleType>(
            keyframeTimes,
            keyframeValues,
            currentTime,
            sampler.interpolation,
            interpolator
        );
        timesamples.push_back(interpolatedValue * scale);
    }

    return timesamples;
}
template <typename ValueType, size_t valueSize>
void PopulateMeshColor(
    const unsigned char* dataPtr,
    const size_t count,
    const size_t byte_stride,
    const PXR_NS::VtIntArray& faceVertexIndices,
    const float factor,
    PXR_NS::VtArray<PXR_NS::GfVec3f>& meshColor
)
{
    ArrayAdapter<std::array<ValueType, valueSize>> adapter(dataPtr, count, byte_stride);
    for (size_t i = 0; i < faceVertexIndices.size() / 3; i++)
    {
        // Gets triangle indices
        auto f0 = faceVertexIndices[3 * i + 0];
        auto f1 = faceVertexIndices[3 * i + 1];
        auto f2 = faceVertexIndices[3 * i + 2];

        // Make sure adapter array index will not go out of bounds
        f0 = std::min(size_t(f0), count - 1);
        f1 = std::min(size_t(f1), count - 1);
        f2 = std::min(size_t(f2), count - 1);
        meshColor.push_back(PXR_NS::GfVec3f(adapter[f0][0] / factor, adapter[f0][1] / factor, adapter[f0][2] / factor));
        meshColor.push_back(PXR_NS::GfVec3f(adapter[f1][0] / factor, adapter[f1][1] / factor, adapter[f1][2] / factor));
        meshColor.push_back(PXR_NS::GfVec3f(adapter[f2][0] / factor, adapter[f2][1] / factor, adapter[f2][2] / factor));
    }
}

std::string GltfImporter::ComputeHash(const OmniFutureThreadContextPtr& context)
{
    mThreadContext = context;
    SHA1 sha1;
    const std::string importAssetPath = mThreadContext->converterContext.GetImportAssetPath();

    Log("Starting to import asset with GLTF importer.");

    OmniConverterBlobPtr fileBlob = mThreadContext->converterContext.ReadFile(importAssetPath);
    if (!fileBlob)
    {
        mModelLoadError = "Failed to read asset " + importAssetPath + ".";
        Log(mModelLoadError);
        mModelLoadStatus = OmniConverterStatus::FILE_READ_ERROR;
        mModelLoadedSuccessfully = false;
        mModelLoaded = true;
        return {};
    }

    if (!fileBlob->buffer || fileBlob->size == 0)
    {
        mModelLoadError = "Asset " + importAssetPath + " is empty.";
        Log(mModelLoadError);
        mModelLoadStatus = OmniConverterStatus::INCOMPLETE_IMPORT_FORMAT;
        mModelLoadedSuccessfully = false;
        mModelLoaded = true;
        return {};
    }

    if (context->converterContext.IsCachingEnabled())
    {
        imemstream memstream((const char*)fileBlob->buffer, fileBlob->size);
        sha1.update(memstream);
    }

    struct FileCallbacksContext
    {
        SHA1* sha1;
        OmniConverterContext* converterContext;
        GltfImporter* importer;
    } fileCallbacksContext = { &sha1, &mThreadContext->converterContext, this };

    static tinygltf::FileExistsFunction fileExists = [](const std::string& filename, void* userData)
    {
        FileCallbacksContext* context = (FileCallbacksContext*)userData;
        return context->converterContext->IsPathExisted(filename.c_str());
    };

    static tinygltf::ExpandFilePathFunction expandFilePath = [](const std::string& filePath, void* userData)
    {
        FileCallbacksContext* context = (FileCallbacksContext*)userData;
        if (PathUtils::IsAbsolutePath(filePath))
        {
            return filePath;
        }

        return PathUtils::JoinPaths(context->converterContext->GetImportAssetDir(), filePath);
    };

    static auto IsTextureFile = [](std::string path)
    {
        path = PathUtils::GetExtension(path);
        path = StringUtils::ToLower(path);
        return path == "png" || path == "jpg" || path == "bmp" || path == "dds" || path == "tif" || path == "exr" || path == "hdr" || path == "webp";
    };

    static tinygltf::ReadWholeFileFunction readWholeFile =
        [](std::vector<unsigned char>* data, std::string* error, const std::string& filePath, void* userData)
    {
        FileCallbacksContext* context = (FileCallbacksContext*)userData;

        bool isCachingEnabled = context->converterContext->IsCachingEnabled();
        bool isTexture = IsTextureFile(filePath);
        if (isCachingEnabled || !isTexture)
        {
            OmniConverterBlobPtr blob = context->converterContext->ReadFile(filePath.c_str());
            if (!blob)
            {
                *error = "File " + filePath + " is not existed.";
                return false;
            }

            if (isTexture)
            {

                data->push_back('c');
                context->importer->mExternalImageDatas.insert({ filePath, blob });
            }
            else
            {
                data->assign((unsigned char*)blob->buffer, (unsigned char*)blob->buffer + blob->size);
            }

            if (isCachingEnabled)
            {
                imemstream memstream((const char*)blob->buffer, blob->size);
                context->sha1->update(memstream);
            }
        }
        else
        {
            // WA: Don't load texture to reduce IO cost if it's not for caching since it's not necessary to obtain the
            // data for export, as textures will be directly copied, while tinygltf does not allow empty data.
            data->push_back('c');
        }

        return true;
    };


    static tinygltf::WriteWholeFileFunction writeWholeFile =
        [](std::string* error, const std::string& filePath, const std::vector<unsigned char>& data, void* userData)
    {
        // Importer does not need to write out anything.
        return false;
    };

    static tinygltf::LoadImageDataFunction loadImageDataFunction = [](tinygltf::Image* image,
                                                                      const int image_idx,
                                                                      std::string* err,
                                                                      std::string* warn,
                                                                      int req_width,
                                                                      int req_height,
                                                                      const unsigned char* bytes,
                                                                      int size,
                                                                      void* userData)
    {
        (void)warn;

        FileCallbacksContext* context = (FileCallbacksContext*)userData;
        image->image.clear(); // Don't save decompressed image data
        if (image->name.size() > 256)
        {
            image->name = "Image" + std::to_string(image_idx);
        }
        if (image->uri.empty() && image->bufferView == -1)
        {
            auto dataBlob = createOmniConverterBlob(new uint8_t[size], size);
            memcpy(dataBlob->buffer, bytes, size);
            context->importer->mEmbeddedImageDatas.insert({ image_idx, dataBlob });
        }

        return true;
    };

    tinygltf::FsCallbacks fsCallbacks;
    fsCallbacks.FileExists = fileExists;
    fsCallbacks.ExpandFilePath = expandFilePath;
    fsCallbacks.ReadWholeFile = readWholeFile;
    fsCallbacks.WriteWholeFile = writeWholeFile;
    fsCallbacks.user_data = &fileCallbacksContext;

    tinygltf::TinyGLTF loader;
    loader.SetFsCallbacks(fsCallbacks);
    loader.SetImageLoader(loadImageDataFunction, &fileCallbacksContext);

    std::string loaderError;
    std::string loaderWarn;
    const std::string& baseImportDir = mThreadContext->converterContext.GetImportAssetDir();

    mModelLoadedSuccessfully = true;
    if (mThreadContext->converterContext.GetImportAssetType() == AssetType::GLTF)
    {
        mModelLoadedSuccessfully = loader.LoadASCIIFromString(
            &mGltfModel,
            &loaderError,
            &loaderWarn,
            (const char*)fileBlob->buffer,
            fileBlob->size,
            baseImportDir
        );
    }
    else
    {
        mModelLoadedSuccessfully = loader.LoadBinaryFromMemory(
            &mGltfModel,
            &loaderError,
            &loaderWarn,
            (unsigned char*)fileBlob->buffer,
            fileBlob->size,
            baseImportDir
        );
    }

    mModelLoaded = true;

    if (mModelLoadedSuccessfully)
    {
        if (!ValidateGltfAccessorExtents(mGltfModel, mModelLoadError))
        {
            Log(mModelLoadError);
            mModelLoadStatus = OmniConverterStatus::INCOMPLETE_IMPORT_FORMAT;
            mModelLoadedSuccessfully = false;
            return {};
        }

        // Mixing flags also so that it will take flags into consideration.
        sha1.update(std::to_string(mThreadContext->converterContext.GetFlags()));

        return sha1.final();
    }
    else
    {
        mModelLoadError = "Asset " + importAssetPath + " cannot be loaded";
        if (!loaderError.empty())
        {
            mModelLoadError += ", error: " + loaderError + ", warn: " + loaderWarn + ".";
        }
        else
        {
            mModelLoadError += " due to unknown issue.";
        }

        Log(mModelLoadError);
    }

    return std::string();
}

StagePtr GltfImporter::ImportStage(const OmniFutureThreadContextPtr& context, OmniConverterStatus& status, std::string& detailedError)
{
    if (!mModelLoaded)
    {
        ComputeHash(context);
    }

    if (!mModelLoadedSuccessfully)
    {
        detailedError = mModelLoadError;
        status = mModelLoadStatus;
        return nullptr;
    }

    StagePtr stage = std::make_shared<Stage>();
    double scale = 1.0f;
    if (mThreadContext->converterContext.KeepAssetUnits() || mThreadContext->converterContext.UseMeterPerUnit())
    {
        stage->worldUnits = 1.0;
    }
    else
    {
        scale = 100.0f;
        stage->worldUnits = 0.01;
    }

    // override up-axis with user-defined value
    if (mThreadContext->converterContext.ConvertUpZ())
    {
        stage->yAxis = false;
    }

    if (mGltfModel.scenes.size() > 0)
    {
        PopulateStageAnimationInfomation(stage, mGltfModel, scale);
        PopulateAllSkeletonRoots(stage, mGltfModel, scale);
        PopulateAllMaterials(stage, mGltfModel);
        PopulateAllMeshes(stage, mGltfModel, scale);
        PopulateAllCameras(stage, mGltfModel, scale);
        PopulateAllLights(stage, mGltfModel, scale);
        PopulateSceneGraph(stage, mGltfModel, scale);
        FillInfluencedBones(stage, mGltfModel);
    }


    return stage;
}

void GltfImporter::Log(const std::string& message)
{
    mThreadContext->converterContext.Log(message.c_str());
}

void GltfImporter::PopulateAllMeshes(StagePtr& stage, tinygltf::Model& model, double scale)
{
    for (size_t i = 0; i < model.meshes.size(); i++)
    {
        const auto& mesh = model.meshes[i];
        MeshPtr stageMesh = std::make_shared<Mesh>();
        if (mesh.name.empty())
        {
            stageMesh->name = "Mesh" + std::to_string(mGlobalMeshIndex);
            mGlobalMeshIndex++;
        }
        else
        {
            stageMesh->name = mesh.name;
        }

        // Get the number of uv set. All primitives must have the same number of
        // uv set. If not, use the min number of them.
        size_t numUVSet = 0;
        for (size_t j = 0; j < mesh.primitives.size(); j++)
        {
            size_t total = 0;
            tinygltf::Primitive primitive = mesh.primitives[j];
            for (const auto& attribute : primitive.attributes)
            {
                if (attribute.first.rfind("TEXCOORD_", 0) == 0)
                {
                    total += 1;
                }
            }

            if (total > numUVSet)
            {
                numUVSet = total;
            }
        }

        stageMesh->uvs.resize(numUVSet);

        size_t numColors = 0;
        for (size_t j = 0; j < mesh.primitives.size(); j++)
        {
            size_t total = 0;
            tinygltf::Primitive primitive = mesh.primitives[j];
            for (const auto& attribute : primitive.attributes)
            {
                if (attribute.first.rfind("COLOR_", 0) == 0)
                {
                    total += 1;
                }
            }

            if (total > numColors)
            {
                numColors = total;
            }
        }

        stageMesh->colors.resize(numColors);

        bool hasValidData = false;
        MeshVertexInfluences meshVertexInfluences;
        for (size_t j = 0; j < mesh.primitives.size(); j++)
        {
            tinygltf::Primitive primitive = mesh.primitives[j];

            // Check for Draco compression extension
            auto dracoIter = primitive.extensions.find("KHR_draco_mesh_compression");
            if (dracoIter != primitive.extensions.end())
            {
                mThreadContext->converterContext.Log(
                    "WARNING: Mesh '" + stageMesh->name + "' primitive " + std::to_string(j) +
                    " uses Draco compression (KHR_draco_mesh_compression), which is not currently supported. Skipping this primitive."
                );
                continue;
            }

            if (primitive.mode != TINYGLTF_MODE_TRIANGLE_FAN && primitive.mode != TINYGLTF_MODE_TRIANGLE_STRIP &&
                primitive.mode != TINYGLTF_MODE_TRIANGLES)
            {
                mThreadContext->converterContext.Log(
                    "Skip mesh primitive(index=" + std::to_string(primitive.indices) + ") since only triangles are supported."
                );
                continue;
            }

            // Fill points
            size_t numExistingPoints = stageMesh->points.size();
            auto points = PopulateNumberAttribute<PXR_NS::GfVec3f>(model, primitive, "POSITION", {}, scale);
            if (points.empty())
            {
                mThreadContext->converterContext.Log(
                    "Skip invalid mesh primitive since its positions (index=" + std::to_string(primitive.indices) + ") are empty"
                );
                continue;
            }
            std::copy(points.begin(), points.end(), std::back_inserter<decltype(stageMesh->points)>(stageMesh->points));

            auto faceVertexIndices = PopulateFaceVertexIndices(model, primitive);
            if (faceVertexIndices.empty())
            {
                // Generates faces according to face mode.
                if (primitive.mode == TINYGLTF_MODE_TRIANGLES)
                {
                    const size_t numFaces = points.size() / 3;
                    if (numFaces * 3 != points.size())
                    {
                        Log("The number of vertices primitive " + std::to_string(j) + " of mesh " + mesh.name +
                            " was not compatible with the TRIANGLES mode. Some vertices were dropped.");
                    }
                    for (size_t faceIndex = 0; faceIndex < numFaces; faceIndex++)
                    {
                        size_t startIndex = 3 * faceIndex;
                        faceVertexIndices.push_back(startIndex);
                        faceVertexIndices.push_back(startIndex + 1);
                        faceVertexIndices.push_back(startIndex + 2);
                    }
                }
                else if (primitive.mode == TINYGLTF_MODE_TRIANGLE_STRIP)
                {
                    const size_t numFaces = points.size() - 2;
                    for (size_t faceIndex = 0; faceIndex < numFaces; faceIndex++)
                    {
                        if ((faceIndex + 1) % 2 == 0)
                        {
                            faceVertexIndices.push_back(faceIndex + 1);
                            faceVertexIndices.push_back(faceIndex);
                            faceVertexIndices.push_back(faceIndex + 2);
                        }
                        else
                        {
                            faceVertexIndices.push_back(faceIndex);
                            faceVertexIndices.push_back(faceIndex + 1);
                            faceVertexIndices.push_back(faceIndex + 2);
                        }
                    }
                }
                else if (primitive.mode == TINYGLTF_MODE_TRIANGLE_FAN)
                {
                    const size_t numFaces = points.size() - 2;
                    faceVertexIndices.push_back(0);
                    faceVertexIndices.push_back(1);
                    faceVertexIndices.push_back(2);
                    for (size_t faceIndex = 1; faceIndex < numFaces; faceIndex++)
                    {
                        faceVertexIndices.push_back(0);
                        faceVertexIndices.push_back(faceIndex + 1);
                        faceVertexIndices.push_back(faceIndex + 2);
                    }
                }
                else
                {
                    Log("Skip invalid mesh primitive since only triangles are supported currently.");
                    continue;
                }
            }

            size_t numExistingIndices = stageMesh->faceVertexIndices.size();
            // Fill face vertex indices
            std::transform(
                faceVertexIndices.begin(),
                faceVertexIndices.end(),
                std::back_inserter<decltype(stageMesh->faceVertexIndices)>(stageMesh->faceVertexIndices),
                [numExistingPoints](int index)
                {
                    return numExistingPoints + index;
                }
            );

            // Fill subset
            MeshGeomSubset subset;
            if (InRange(primitive.material, 0, stage->materials.size()))
            {
                subset.materialIndex = primitive.material;
                if (subset.materialIndex != INVALID_MATERIAL_INDEX)
                {
                    subset.name = stage->materials[subset.materialIndex]->name;
                }
            }

            subset.faceIndices.resize(faceVertexIndices.size() / 3);
            for (size_t faceIndex = 0; faceIndex < subset.faceIndices.size(); faceIndex++)
            {
                subset.faceIndices[faceIndex] = numExistingIndices / 3 + faceIndex;
            }
            stageMesh->meshSubsets.push_back(subset);

            // Fill normals
            auto normals = PopulateNumberAttribute<PXR_NS::GfVec3f>(model, primitive, "NORMAL", faceVertexIndices);
            if (!normals.empty())
            {
                std::copy(normals.begin(), normals.end(), std::back_inserter<decltype(stageMesh->normals)>(stageMesh->normals));
            }

            // Fill colors
            for (size_t colorIndex = 0; colorIndex < numColors; colorIndex++)
            {
                auto iter = primitive.attributes.find("COLOR_" + std::to_string(colorIndex));
                if (iter == primitive.attributes.end())
                {
                    continue;
                }

                const auto attribAccessor = model.accessors[iter->second];

                // Check if bufferView is valid (can be -1 for Draco compressed meshes)
                if (attribAccessor.bufferView < 0 || attribAccessor.bufferView >= static_cast<int>(model.bufferViews.size()))
                {
                    continue;
                }

                const auto& bufferView = model.bufferViews[attribAccessor.bufferView];

                // Check if buffer is valid
                if (bufferView.buffer < 0 || bufferView.buffer >= static_cast<int>(model.buffers.size()))
                {
                    continue;
                }

                const auto& buffer = model.buffers[bufferView.buffer];

                // Check if buffer data is available
                if (buffer.data.empty())
                {
                    continue;
                }

                const auto dataPtr = buffer.data.data() + bufferView.byteOffset + attribAccessor.byteOffset;
                const int byte_stride = attribAccessor.ByteStride(bufferView);
                const size_t count = attribAccessor.count;
                if (attribAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                {
                    stageMesh->colors[colorIndex].reserve(count);
                    PopulateMeshColor<unsigned short, 4>(dataPtr, count, byte_stride, faceVertexIndices, 65536.0, stageMesh->colors[colorIndex]);
                }
                // also should support mesh color with float value
                if (attribAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
                {
                    PopulateMeshColor<float, 4>(dataPtr, count, byte_stride, faceVertexIndices, 1.0, stageMesh->colors[colorIndex]);
                }

                if (attribAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                {
                    PopulateMeshColor<unsigned char, 4>(dataPtr, count, byte_stride, faceVertexIndices, 256.0, stageMesh->colors[colorIndex]);
                }
            }

            // Fill uvs
            for (size_t uvIndex = 0; uvIndex < numUVSet; uvIndex++)
            {
                auto uvs = PopulateNumberAttribute<PXR_NS::GfVec2f>(model, primitive, "TEXCOORD_" + std::to_string(uvIndex), faceVertexIndices);
                // Invert y direction to use lower-left as origin.
                std::transform(
                    uvs.begin(),
                    uvs.end(),
                    uvs.begin(),
                    [](const PXR_NS::GfVec2f& uv)
                    {
                        return PXR_NS::GfVec2f(uv[0], 1.0f - uv[1]);
                    }
                );

                if (uvs.empty())
                {
                    // Fill uv with (0, 0) if it has no uv set for this subset to make sure all faces will have uvs.
                    uvs = PXR_NS::VtVec2fArray(numExistingIndices, PXR_NS::GfVec2f(0.5f, 0.5f));
                }
                std::copy(uvs.begin(), uvs.end(), std::back_inserter<decltype(uvs)>(stageMesh->uvs[uvIndex]));
            }

            // Fill joints and weights
            auto iter = primitive.attributes.find("JOINTS_0");
            bool filled = false;
            if (iter != primitive.attributes.end())
            {
                auto joints = PopulateVec4iBufferData(model, iter->second);
                auto jointWeights = PopulateNumberAttribute<PXR_NS::GfVec4f>(model, primitive, "WEIGHTS_0", faceVertexIndices);
                if (!joints.empty() && !jointWeights.empty() && joints.size() == jointWeights.size() && joints.size() == points.size())
                {
                    filled = true;
                    hasValidData = true;
                    for (size_t jointIndex = 0; jointIndex < joints.size(); jointIndex++)
                    {
                        const auto& jointInfluence = joints[jointIndex];
                        const auto& jointWeight = jointWeights[jointIndex];
                        for (size_t k = 0; k < 4; k++)
                        {
                            meshVertexInfluences.jointInfluences.push_back(jointInfluence[k]);
                            meshVertexInfluences.jointWeights.push_back(jointWeight[k]);
                        }
                    }
                }
                else
                {
                    mThreadContext->converterContext.Log(
                        "Invalid joints and weights data found for primitive (index=" + std::to_string(j) + ") of mesh " + stageMesh->name + "."
                    );
                }
            }

            // If it's possible that part of the primitives have skinned, this is to fill trash data
            // to keep the same size as the mesh vertices.
            if (!filled)
            {
                for (size_t pointIndex = 0; pointIndex < points.size(); pointIndex++)
                {
                    meshVertexInfluences.jointInfluences.push_back(0);
                    meshVertexInfluences.jointInfluences.push_back(0);
                    meshVertexInfluences.jointInfluences.push_back(0);
                    meshVertexInfluences.jointInfluences.push_back(0);

                    meshVertexInfluences.jointWeights.push_back(0.0f);
                    meshVertexInfluences.jointWeights.push_back(0.0f);
                    meshVertexInfluences.jointWeights.push_back(0.0f);
                    meshVertexInfluences.jointWeights.push_back(0.0f);
                }
            }
        }

        if (hasValidData)
        {
            mMeshIndexToVetexInfluences.insert({ i, meshVertexInfluences });
        }

        stageMesh->faceVertexCounts = PXR_NS::VtIntArray(stageMesh->faceVertexIndices.size() / 3, 3);
        stage->meshes.push_back(stageMesh);
    }
}

void GltfImporter::PopulateAllMaterials(StagePtr& stage, tinygltf::Model& model)
{
    if (mThreadContext->converterContext.IgnoreMaterials())
    {
        return;
    }

    size_t inMemoryTextureIndex = 0;
    for (size_t i = 0; i < model.images.size(); i++)
    {
        TextureImagePtr image = std::make_shared<TextureImage>();
        auto& gltfImage = model.images[i];
        std::replace(gltfImage.name.begin(), gltfImage.name.end(), ':', '_');
        std::string textureFilePath = UrlDecode(gltfImage.uri);
        const std::string& importBasePath = mThreadContext->converterContext.GetImportAssetDir();
        if (textureFilePath.empty())
        {
            std::string format = PathUtils::MimeToExt(gltfImage.mimeType);
            if (format.empty())
            {
                format = PathUtils::ToMimeType(gltfImage.name);
            }

            if (format.empty())
            {
                continue;
            }

            if (gltfImage.name.empty())
            {
                const std::string& assetName = mThreadContext->converterContext.GetImportAssetFileName();
                textureFilePath = assetName + "_texture" + std::to_string(inMemoryTextureIndex) + "." + format;
                inMemoryTextureIndex++;
            }
            else
            {
                textureFilePath = gltfImage.name + "." + format;
            }
        }

        if (!textureFilePath.empty() && !PathUtils::IsAbsolutePath(textureFilePath))
        {
            textureFilePath = PathUtils::JoinPaths(importBasePath, textureFilePath);
        }

        image->originalPath = textureFilePath;
        image->realPath = textureFilePath;
        auto iter = mExternalImageDatas.find(textureFilePath);
        if (iter != mExternalImageDatas.end())
        {
            image->blob = iter->second;
        }
        else if (InRange(gltfImage.bufferView, 0, model.bufferViews.size()))
        {
            const tinygltf::BufferView& bufferView = model.bufferViews[size_t(gltfImage.bufferView)];

            // Additional safety check for buffer index
            if (InRange(bufferView.buffer, 0, model.buffers.size()))
            {
                const tinygltf::Buffer& buffer = model.buffers[size_t(bufferView.buffer)];

                // Check if buffer data is available
                if (!buffer.data.empty() && bufferView.byteLength > 0)
                {
                    const auto dataAddress = buffer.data.data() + bufferView.byteOffset;
                    uint8_t* data = new uint8_t[bufferView.byteLength];
                    std::memcpy(data, dataAddress, bufferView.byteLength);
                    image->blob = createOmniConverterBlob(data, bufferView.byteLength);
                }
            }
        }
        else
        {
            auto iter = mEmbeddedImageDatas.find(i);
            if (iter != mEmbeddedImageDatas.end())
            {
                image->blob = iter->second;
            }
        }
        stage->images.push_back(image);
    }

    auto GetImageIndex = [&stage, &model](int textureIndex)
    {
        if (InRange(textureIndex, 0, model.textures.size()))
        {
            const auto& texture = model.textures[textureIndex];
            int imageIndex = texture.source;

            // Check for EXT_texture_webp extension first
            auto webpIter = texture.extensions.find("EXT_texture_webp");
            if (webpIter != texture.extensions.end())
            {
                const auto& webpSourceObject = webpIter->second.Get("source");
                if (webpSourceObject.IsInt())
                {
                    imageIndex = webpSourceObject.GetNumberAsInt();
                }
            }

            if (InRange(imageIndex, 0, model.images.size()))
            {
                return imageIndex;
            }
        }

        return -1;
    };

    auto PopulateTextureFromTextureInfo =
        [&GetImageIndex,
         &model](const MaterialPtr& material, MaterialTextureType materialType, const tinygltf::TextureInfo& textureInfo, TextureOutputMode outputMode)
    {
        auto& textureReference = material->GetTextureReference(materialType);
        textureReference.imageIndex = GetImageIndex(textureInfo.index);
        textureReference.uvIndex = textureInfo.texCoord;
        textureReference.outputMode = outputMode;
        if (InRange(textureInfo.index, 0, model.textures.size()))
        {
            auto& gltfTexture = model.textures[textureInfo.index];
            if (InRange(gltfTexture.sampler, 0, model.samplers.size()))
            {
                auto& sampler = model.samplers[gltfTexture.sampler];
                textureReference.wrapS = ToTextureWrapMode(sampler.wrapS);
                textureReference.wrapT = ToTextureWrapMode(sampler.wrapT);
            }
        }
        GetUVTransformFromExtension(textureInfo.extensions, textureReference.transform);
    };

    auto PopulateTextureFromObject = [&PopulateTextureFromTextureInfo](
                                         const MaterialPtr& material,
                                         MaterialTextureType materialType,
                                         const tinygltf::Value& extensionInfo,
                                         const std::string& textureName,
                                         TextureOutputMode outputMode
                                     )
    {
        tinygltf::TextureInfo textureInfo;
        if (GetTextureInfo(extensionInfo, textureName, textureInfo))
        {
            PopulateTextureFromTextureInfo(material, materialType, textureInfo, outputMode);

            return true;
        }

        return false;
    };

    std::unordered_set<size_t> texturesHasAlpha;
    for (size_t i = 0; i < model.materials.size(); i++)
    {
        const auto& gltfMaterial = model.materials[i];
        MaterialPtr material = std::make_shared<Material>();
        if (gltfMaterial.name.empty())
        {
            material->name = "material" + std::to_string(i);
        }
        else
        {
            material->name = gltfMaterial.name;
        }

        material->emissiveColor = PXR_NS::GfVec3f(gltfMaterial.emissiveFactor[0], gltfMaterial.emissiveFactor[1], gltfMaterial.emissiveFactor[2]);
        if (material->emissiveColor != PXR_NS::GfVec3f(0.0f))
        {
            material->hasEmissiveColor = true;
            material->hasEmissiveStrength = true;
            // When we has a Emissive Color, should give it a empirical strength value
            // We should map strength value from gltf range(0 ~16) to Kit range(0 ~DEFAULT_EMISSIVE_INTENSITY)
            material->emissiveStrength = DEFAULT_EMISSIVE_INTENSITY / 16;
        }

        material->opacityMode = FromAlphaModeString(gltfMaterial.alphaMode);
        switch (material->opacityMode)
        {
            case GLTFOpacityMode::GLTF_OPAQUE:
                material->opacity = 1.0;
                material->hasOpacity = false;
                material->opacityThreshold = 0.0;
                break;
            case GLTFOpacityMode::GLTF_MASK:
                material->hasOpacity = true;
                material->opacityThreshold = gltfMaterial.alphaCutoff;
                break;
            case GLTFOpacityMode::GLTF_BLEND:
                material->hasOpacity = true;
                material->opacityThreshold = 0.0;
                if (gltfMaterial.doubleSided)
                {
                    // GLTF_BLEND and double sided means glass material in gltf
                    // it map to base_alpha=1(material->opacity = 1.0) and transmissionFactor=1 in usd
                    material->opacity = 1.0;
                    material->transmissionFactor = 1.0;
                }
                break;
            default:
                break;
        }

        PopulateTextureFromTextureInfo(material, MaterialTextureType::EMISSIVE, gltfMaterial.emissiveTexture, TextureOutputMode::RGB);

        auto& normal = material->GetTextureReference(MaterialTextureType::NORMAL);
        normal.imageIndex = GetImageIndex(gltfMaterial.normalTexture.index);
        normal.uvIndex = gltfMaterial.normalTexture.texCoord;
        GetUVTransformFromExtension(gltfMaterial.normalTexture.extensions, normal.transform);

        auto& occlusion = material->GetTextureReference(MaterialTextureType::OCCLUSION);
        occlusion.imageIndex = GetImageIndex(gltfMaterial.occlusionTexture.index);
        occlusion.uvIndex = gltfMaterial.occlusionTexture.texCoord;
        GetUVTransformFromExtension(gltfMaterial.occlusionTexture.extensions, occlusion.transform);

        auto iter = gltfMaterial.extensions.find("KHR_materials_pbrSpecularGlossiness");
        if (iter != gltfMaterial.extensions.end()) // Specular/glossy workflow
        {
            material->useSpecularGlossyWorkflow = true;
            const auto& diffuseFactorObject = iter->second.Get("diffuseFactor");
            std::vector<double> diffuseFactor;
            if (GetArrayValues(diffuseFactorObject, diffuseFactor, 4))
            {
                material->hasDiffuseColor = true;
                material->diffuseColor = PXR_NS::GfVec3f(diffuseFactor[0], diffuseFactor[1], diffuseFactor[2]);
            }

            const auto& specularFactorObject = iter->second.Get("specularFactor");
            std::vector<double> specularFactor;
            if (GetArrayValues(specularFactorObject, specularFactor, 3))
            {
                material->hasSpecularColor = true;
                material->specularColor = PXR_NS::GfVec3f(specularFactor[0], specularFactor[1], specularFactor[2]);
            }

            const auto& glossyFactorObject = iter->second.Get("glossinessFactor");
            if (glossyFactorObject.IsNumber())
            {
                material->hasGlossyFactor = true;
                material->glossyFactor = glossyFactorObject.GetNumberAsDouble();
            }

            PopulateTextureFromObject(material, MaterialTextureType::DIFFUSE, iter->second, "diffuseTexture", TextureOutputMode::RGB);
            PopulateTextureFromObject(material, MaterialTextureType::SPECULAR, iter->second, "specularGlossinessTexture", TextureOutputMode::RGB);
            if (PopulateTextureFromObject(material, MaterialTextureType::GLOSSY, iter->second, "specularGlossinessTexture", TextureOutputMode::ALPHA))
            {
                if (!material->hasGlossyFactor)
                {
                    material->glossyFactor = 1.0;
                }
            }
        }
        else
        {
            material->useSpecularGlossyWorkflow = false;
            const auto& metallicRoughness = gltfMaterial.pbrMetallicRoughness;
            material->diffuseColor = PXR_NS::GfVec3f(
                gltfMaterial.pbrMetallicRoughness.baseColorFactor[0],
                gltfMaterial.pbrMetallicRoughness.baseColorFactor[1],
                metallicRoughness.baseColorFactor[2]
            );
            material->hasDiffuseColor = true;

            material->metallicFactor = metallicRoughness.metallicFactor;
            material->hasMetallicFactor = true;
            material->roughnessFactor = metallicRoughness.roughnessFactor;
            material->hasRoughnessFactor = true;

            PopulateTextureFromTextureInfo(material, MaterialTextureType::DIFFUSE, metallicRoughness.baseColorTexture, TextureOutputMode::RGB);
            PopulateTextureFromTextureInfo(material, MaterialTextureType::METALLIC, metallicRoughness.metallicRoughnessTexture, TextureOutputMode::B);
            PopulateTextureFromTextureInfo(material, MaterialTextureType::ROUGHNESS, metallicRoughness.metallicRoughnessTexture, TextureOutputMode::G);

            auto iter = gltfMaterial.extensions.find("KHR_materials_clearcoat");
            if (iter != gltfMaterial.extensions.end())
            {
                const auto& clearcoatFactorObject = iter->second.Get("clearcoatFactor");
                if (clearcoatFactorObject.IsNumber())
                {
                    material->hasClearCoatFactor = true;
                    material->clearCoatFactor = clearcoatFactorObject.GetNumberAsDouble();
                }

                const auto& clearcoatRoughnessFactorObject = iter->second.Get("clearcoatRoughnessFactor");
                if (clearcoatRoughnessFactorObject.IsNumber())
                {
                    material->hasClearCoatRoughnessFactor = true;
                    material->clearCoatRoughnessFactor = clearcoatRoughnessFactorObject.GetNumberAsDouble();
                }

                PopulateTextureFromObject(material, MaterialTextureType::CLEARCOAT, iter->second, "clearcoatTexture", TextureOutputMode::AVERAGE);
                PopulateTextureFromObject(
                    material,
                    MaterialTextureType::CLEARCOAT_ROUGHNESS,
                    iter->second,
                    "clearcoatRoughnessTexture",
                    TextureOutputMode::AVERAGE
                );
                PopulateTextureFromObject(
                    material,
                    MaterialTextureType::CLEARCOAT_NORMAL,
                    iter->second,
                    "clearcoatNormalTexture",
                    TextureOutputMode::AVERAGE
                );
            }

            iter = gltfMaterial.extensions.find("KHR_materials_sheen");
            if (iter != gltfMaterial.extensions.end())
            {
                const auto& sheenColorFactorObject = iter->second.Get("sheenColorFactor");
                std::vector<double> sheenColor;
                if (GetArrayValues(sheenColorFactorObject, sheenColor, 3))
                {
                    material->hasSheenColor = true;
                    material->sheenColor = PXR_NS::GfVec3f(sheenColor[0], sheenColor[1], sheenColor[2]);
                }

                const auto& sheenRoughnessFactorObject = iter->second.Get("sheenRoughnessFactor");
                if (sheenRoughnessFactorObject.IsNumber())
                {
                    material->hasSheenRoughnessFactor = true;
                    material->sheenRoughnessFactor = sheenRoughnessFactorObject.GetNumberAsDouble();
                }

                PopulateTextureFromObject(material, MaterialTextureType::SHEEN, iter->second, "sheenColorTexture", TextureOutputMode::AVERAGE);
                PopulateTextureFromObject(
                    material,
                    MaterialTextureType::SHEEN_ROUGHNESS,
                    iter->second,
                    "sheenRoughnessTexture",
                    TextureOutputMode::AVERAGE
                );
            }

            iter = gltfMaterial.extensions.find("KHR_materials_transmission");
            if (iter != gltfMaterial.extensions.end())
            {
                const auto& transmissionFactorObject = iter->second.Get("transmissionFactor");
                if (transmissionFactorObject.IsNumber())
                {
                    material->hasTransmissionFactor = true;
                    material->transmissionFactor = transmissionFactorObject.GetNumberAsDouble();
                }

                PopulateTextureFromObject(
                    material,
                    MaterialTextureType::TRANSMISSION,
                    iter->second,
                    "transmissionTexture",
                    TextureOutputMode::AVERAGE
                );
            }

            iter = gltfMaterial.extensions.find("KHR_materials_specular");
            if (iter != gltfMaterial.extensions.end())
            {
                const auto& specularFactorObject = iter->second.Get("specularFactor");
                if (specularFactorObject.IsNumber())
                {
                    material->hasSpecularStrengthFactor = true;
                    material->specularStrength = specularFactorObject.GetNumberAsDouble();
                }

                const auto& specularColorFactorObject = iter->second.Get("specularColorFactor");
                std::vector<double> specularColorFactor;
                if (GetArrayValues(specularColorFactorObject, specularColorFactor, 3))
                {
                    material->hasSpecularColor = true;
                    material->specularColor = PXR_NS::GfVec3f(specularColorFactor[0], specularColorFactor[1], specularColorFactor[2]);
                }

                PopulateTextureFromObject(
                    material,
                    MaterialTextureType::SPECULAR_STRENGTH,
                    iter->second,
                    "specularTexture",
                    TextureOutputMode::AVERAGE
                );
                PopulateTextureFromObject(material, MaterialTextureType::SPECULAR, iter->second, "specularColorTexture", TextureOutputMode::RGB);
            }

            iter = gltfMaterial.extensions.find("KHR_materials_ior");
            if (iter != gltfMaterial.extensions.end())
            {
                const auto& specularFactorObject = iter->second.Get("ior");
                if (specularFactorObject.IsNumber())
                {
                    material->hasIor = true;
                    material->ior = specularFactorObject.GetNumberAsDouble();
                }
            }

            iter = gltfMaterial.extensions.find("KHR_materials_volume");
            if (iter != gltfMaterial.extensions.end())
            {
                const auto& thicknessFactorObject = iter->second.Get("thicknessFactor");
                if (thicknessFactorObject.IsNumber())
                {
                    double thicknessFactor = thicknessFactorObject.GetNumberAsDouble();
                    if (thicknessFactor != 0.0)
                    {
                        material->thinWalled = false;
                    }
                }

                const auto& attenuationDistanceObject = iter->second.Get("attenuationDistance");
                if (attenuationDistanceObject.IsNumber())
                {
                    material->hasAttenuationDistance = true;
                    material->attenuationDistance = attenuationDistanceObject.GetNumberAsDouble();
                }

                const auto& attenuationColorObject = iter->second.Get("attenuationColor");
                std::vector<double> attenuationColor;
                if (GetArrayValues(attenuationColorObject, attenuationColor, 3))
                {
                    material->hasAttenuationColor = true;
                    material->attenuationColor = PXR_NS::GfVec3f(attenuationColor[0], attenuationColor[1], attenuationColor[2]);
                }
            }

            iter = gltfMaterial.extensions.find("KHR_materials_emissive_strength");
            if (iter != gltfMaterial.extensions.end())
            {
                const auto& emissiveStrengthObject = iter->second.Get("emissiveStrength");
                if (emissiveStrengthObject.IsNumber())
                {
                    material->hasEmissiveStrength = true;
                    // We should map strength value from gltf range(0~16) to Kit range (0~DEFAULT_EMISSIVE_INTENSITY)
                    // TODO: the gltf range just based on experience, no official definition found
                    // see https://github.com/KhronosGroup/glTF-Sample-Models/tree/master/2.0/EmissiveStrengthTest
                    material->emissiveStrength = emissiveStrengthObject.GetNumberAsDouble() / 16 * DEFAULT_EMISSIVE_INTENSITY;
                }
            }

            iter = gltfMaterial.extensions.find("KHR_materials_iridescence");
            if (iter != gltfMaterial.extensions.end())
            {
                const auto& iridescenceFactorObject = iter->second.Get("iridescenceFactor");
                if (iridescenceFactorObject.IsNumber())
                {
                    material->hasIridescenceFactor = true;
                    material->iridescenceFactor = (float)iridescenceFactorObject.GetNumberAsDouble();
                }

                const auto& iridescenceIorObject = iter->second.Get("iridescenceIor");
                if (iridescenceIorObject.IsNumber())
                {
                    material->hasIridescenceIor = true;
                    material->iridescenceIor = (float)iridescenceIorObject.GetNumberAsDouble();
                }

                const auto& iridescenceThicknessMinimumObject = iter->second.Get("iridescenceThicknessMinimum");
                if (iridescenceThicknessMinimumObject.IsNumber())
                {
                    material->hasIridescenceThicknessMinimum = true;
                    material->iridescenceThicknessMinimum = (float)iridescenceThicknessMinimumObject.GetNumberAsDouble();
                }

                const auto& iridescenceThicknessMaximumObject = iter->second.Get("iridescenceThicknessMaximum");
                if (iridescenceThicknessMaximumObject.IsNumber())
                {
                    material->hasIridescenceThicknessMaximum = true;
                    material->iridescenceThicknessMaximum = (float)iridescenceThicknessMaximumObject.GetNumberAsDouble();
                }

                PopulateTextureFromObject(material, MaterialTextureType::IRIDESCENCE, iter->second, "iridescenceTexture", TextureOutputMode::R);
                PopulateTextureFromObject(
                    material,
                    MaterialTextureType::IRIDESCENCE_THICKNESS,
                    iter->second,
                    "iridescenceThicknessTexture",
                    TextureOutputMode::G
                );
            }

            iter = gltfMaterial.extensions.find("KHR_materials_anisotropy");
            if (iter != gltfMaterial.extensions.end())
            {
                const auto& anisotropyStrengthObject = iter->second.Get("anisotropyStrength");
                if (anisotropyStrengthObject.IsNumber())
                {
                    material->hasAnisotropyStrength = true;
                    material->anisotropyStrength = (float)anisotropyStrengthObject.GetNumberAsDouble();
                }

                const auto& anisotropyRotationObject = iter->second.Get("anisotropyRotation");
                if (anisotropyRotationObject.IsNumber())
                {
                    material->hasAnisotropyRotation = true;
                    material->anisotropyRotation = (float)(anisotropyRotationObject.GetNumberAsDouble() / (M_PI * 2.0));
                    material->anisotropyRotation = std::fmod(material->anisotropyRotation, 1.0f);
                    if (material->anisotropyRotation < 0.0f)
                    {
                        material->anisotropyRotation += 1.0f;
                    }
                }

                PopulateTextureFromObject(material, MaterialTextureType::ANISOTROPY, iter->second, "anisotropyTexture", TextureOutputMode::RGB);
            }
        }


        auto& diffuse = material->GetTextureReference(MaterialTextureType::DIFFUSE);
        if (diffuse.IsValid() && (texturesHasAlpha.count(diffuse.imageIndex) != 0 || HasAlphaChannel(mThreadContext, stage, diffuse.imageIndex)))
        {
            texturesHasAlpha.insert(diffuse.imageIndex);
            auto& opacity = material->GetTextureReference(MaterialTextureType::OPACITY);
            opacity = diffuse;
            opacity.outputMode = TextureOutputMode::ALPHA;
        }

        stage->materials.push_back(material);
    }
}

void GltfImporter::PopulateAllCameras(StagePtr& stage, tinygltf::Model& model, double scale)
{
    if (mThreadContext->converterContext.IgnoreCameras())
    {
        return;
    }

    for (size_t i = 0; i < model.cameras.size(); i++)
    {
        const auto& gltfCamera = model.cameras[i];
        CameraPtr camera = std::make_shared<Camera>();
        if (gltfCamera.name.empty())
        {
            camera->name = "Camera" + std::to_string(mGlobalCameraIndex);
            mGlobalCameraIndex++;
        }
        else
        {
            camera->name = gltfCamera.name;
        }

        if (gltfCamera.type == "orthographic")
        {
            const auto& ortho = gltfCamera.orthographic;
            float aspect = ortho.ymag == 0.0 ? 1.0f : ortho.xmag / ortho.ymag;
            PXR_NS::GfCamera gfCamera;
            gfCamera.SetOrthographicFromAspectRatioAndSize(aspect, ortho.xmag * 10 * scale, PXR_NS::GfCamera::FOVHorizontal);
            camera->projectionType = PXR_NS::GfCamera::Orthographic;
            camera->horizonalAperture = gfCamera.GetHorizontalAperture();
            camera->verticallAperture = gfCamera.GetVerticalAperture();
            camera->focalLength = gfCamera.GetFocalLength();
            camera->clippingNear = ortho.znear * scale;
            camera->clippingFar = ortho.zfar * scale;
        }
        else
        {
            const auto& perspective = gltfCamera.perspective;
            PXR_NS::GfCamera gfCamera;
            double horizontalFOV = 2 * atan(std::tan(perspective.yfov / 2) * ((perspective.aspectRatio == 0.0) ? 1.0 : perspective.aspectRatio));
            gfCamera.SetPerspectiveFromAspectRatioAndFieldOfView(
                perspective.aspectRatio,
                PXR_NS::GfRadiansToDegrees(horizontalFOV),
                PXR_NS::GfCamera::FOVHorizontal
            );
            camera->focalLength = gfCamera.GetFocalLength();
            camera->horizonalAperture = gfCamera.GetHorizontalAperture();
            camera->verticallAperture = gfCamera.GetVerticalAperture();
            camera->clippingNear = perspective.znear * scale;
            camera->clippingFar = perspective.zfar * scale;
        }

        stage->cameras.push_back(camera);
    }
}

void GltfImporter::PopulateAllLights(StagePtr& stage, tinygltf::Model& model, double scale)
{
    const double PI = 3.14159265358979323846;
    const double UNIT_PER_SQUARE_METRE = scale * scale;
    const double AREA = 1;

    if (mThreadContext->converterContext.IgnoreLights())
    {
        return;
    }

    for (size_t i = 0; i < model.lights.size(); i++)
    {
        const auto& gltfLight = model.lights[i];
        LightPtr light = std::make_shared<Light>();
        if (gltfLight.name.empty())
        {
            light->name = "Light" + std::to_string(mGlobalLightIndex);
            mGlobalLightIndex++;
        }
        else
        {
            light->name = gltfLight.name;
        }
        if (gltfLight.color.size() == 3)
        {
            light->color = PXR_NS::GfVec3f(gltfLight.color[0], gltfLight.color[1], gltfLight.color[2]);
        }
        light->intensity = gltfLight.intensity;
        light->innerAngle = gltfLight.spot.innerConeAngle;
        light->outAngle = gltfLight.spot.outerConeAngle;
        light->type = LightType::POINT;
        // gltf light unit is cm^2 from m^2 of USD
        if (gltfLight.type == "directional")
        {
            light->type = LightType::DISTANT;
            light->intensity = gltfLight.intensity / (16.0 * PI * UNIT_PER_SQUARE_METRE);
        }
        else
        {
            if (gltfLight.type == "point")
            {
                light->type = LightType::POINT;
            }
            if (gltfLight.type == "spot")
            {
                light->type = LightType::SPHERE;
            }
            light->intensity = gltfLight.intensity / (AREA * PI * UNIT_PER_SQUARE_METRE);
        }


        stage->lights.push_back(light);
    }
}


void GltfImporter::PopulateSceneGraph(StagePtr& stage, tinygltf::Model& model, double scale)
{
    int defaultScene = GetDefaultScene(model);
    const tinygltf::Scene& scene = model.scenes[defaultScene];
    stage->rootNode = std::make_shared<StageNode>("");

    mAllStageNodes.resize(model.nodes.size());
    mVisitedNodes.clear();
    if (scene.nodes.size() > 1 || scene.nodes.empty())
    {
        stage->rootNode->name = "World";
        std::unordered_map<std::string, size_t> siblingNodeNames;
        for (size_t i = 0; i < scene.nodes.size(); ++i)
        {
            const size_t nodeId = scene.nodes[i];
            bool underSkeleton = IsSkeletonRoot(nodeId);
            if (InRange(nodeId, 0, model.nodes.size()))
            {
                StageNodePtr child = std::make_shared<StageNode>("");
                child->parent = stage->rootNode;
                child->isBoneNode = underSkeleton;
                PopulateSceneNode(stage, child, model, nodeId, underSkeleton, scale);
                stage->rootNode->children.push_back(child);
                auto iter = siblingNodeNames.find(child->name);
                if (iter == siblingNodeNames.end())
                {
                    siblingNodeNames.insert({ child->name, 1 });
                }
                else
                {
                    child->name += std::to_string(iter->second);
                    iter->second += 1;
                }
            }
        }
    }
    else
    {
        auto& rootGltfNode = model.nodes[scene.nodes[0]];
        bool underSkeleton = IsSkeletonRoot(scene.nodes[0]);
        stage->rootNode->isBoneNode = underSkeleton;
        PopulateSceneNode(stage, stage->rootNode, model, scene.nodes[0], underSkeleton, scale);
    }
}

void GltfImporter::PopulateSceneNode(
    StagePtr& stage,
    StageNodePtr& stageNode,
    tinygltf::Model& model,
    size_t nodeIndex,
    bool underSkeleton,
    double scale
)
{
    mAllStageNodes[nodeIndex] = stageNode;
    const auto& node = model.nodes[nodeIndex];
    if (node.name.empty())
    {
        stageNode->name = "node" + std::to_string(mGlobalNodeIndex);
        mGlobalNodeIndex++;
    }
    else
    {
        stageNode->name = node.name;
    }

    stageNode->localTransform = GetNodeLocalTransform(model, nodeIndex, scale);
    const auto& parentNode = stageNode->parent.lock();
    if (parentNode)
    {
        stageNode->worldTransformMatrix = stageNode->localTransform.GetMatrix() * parentNode->worldTransformMatrix;
    }
    else
    {
        stageNode->worldTransformMatrix = stageNode->localTransform.GetMatrix();
    }

    stageNode->transformAnimationTracks = PopulateNodeAnimation(stage, model, nodeIndex, scale);


    auto iter = node.extensions.find("KHR_lights_punctual");
    if (iter != node.extensions.end())
    {
        const auto& lightIndexObject = iter->second.Get("light");
        if (lightIndexObject.IsNumber())
        {
            auto lightIndex = lightIndexObject.GetNumberAsInt();
            if (InRange(lightIndex, 0, stage->lights.size()))
            {
                stageNode->lights.push_back(lightIndexObject.GetNumberAsInt());
            }
        }
    }

    if (InRange(node.camera, 0, stage->cameras.size()))
    {
        const auto camera = stage->cameras[node.camera];
        camera->position = stageNode->worldTransformMatrix.Transform(camera->position);
        camera->up = stageNode->worldTransformMatrix.TransformDir(camera->up);
        camera->lookAt = stageNode->worldTransformMatrix.Transform(camera->lookAt);
        stageNode->cameras.push_back(node.camera);
    }

    if (InRange(node.mesh, 0, stage->meshes.size()))
    {
        if (InRange(node.skin, 0, model.skins.size()))
        {
            auto iter = mMeshIndexToVetexInfluences.find(node.mesh);
            if (iter != mMeshIndexToVetexInfluences.end() && !HasSkinnedMeshInSkeleton(stage, node.skin, node.mesh))
            {
                auto skinMesh = std::make_shared<SkinMesh>(node.mesh);
                skinMesh->numBoneInfluencesPerVertex = 4;
                skinMesh->jointInfluences = iter->second.jointInfluences;
                skinMesh->jointWeights = iter->second.jointWeights;
                stage->skinMeshes.push_back(skinMesh);
                mMeshSkinIndex.push_back(node.skin);
            }
        }
        else
        {
            stageNode->staticMeshInstances.push_back(node.mesh);
        }
    }

    bool isRootBone = IsSkeletonRoot(nodeIndex);
    if (underSkeleton || isRootBone)
    {
        stageNode->isBoneNode = true;
        auto iter = mJointBindMatrices.find(nodeIndex);
        if (iter != mJointBindMatrices.end())
        {
            stageNode->bindTransform = iter->second;
        }
        else if (!stageNode->IsRootBone())
        {
            stageNode->bindTransform = stageNode->localTransform.GetMatrix() * parentNode->bindTransform;
        }
        else
        {
            stageNode->bindTransform = stageNode->localTransform.GetMatrix();
        }

        stageNode->restTransform = stageNode->localTransform.GetMatrix();

        underSkeleton = true;
    }

    std::unordered_map<std::string, size_t> siblingNodeNames;
    for (size_t i = 0; i < node.children.size(); i++)
    {
        if (InRange(node.children[i], 0, model.nodes.size()))
        {
            StageNodePtr child = std::make_shared<StageNode>("");
            child->parent = stageNode;
            // skip visited nodes in case the model contains recursive nodes
            if (mVisitedNodes.find(node.children[i]) != mVisitedNodes.end())
            {
                continue;
            }
            mVisitedNodes.insert(node.children[i]);
            PopulateSceneNode(stage, child, model, node.children[i], underSkeleton, scale);
            stageNode->children.push_back(child);
            auto iter = siblingNodeNames.find(child->name);
            if (iter == siblingNodeNames.end())
            {
                siblingNodeNames.insert({ child->name, 1 });
            }
            else
            {
                child->name += std::to_string(iter->second);
                iter->second += 1;
            }
        }
    }
}

void GltfImporter::PopulateAllSkeletonRoots(StagePtr& stage, tinygltf::Model& model, double scale)
{
    if (model.skins.empty())
    {
        return;
    }

    int defaultScene = GetDefaultScene(model);
    const tinygltf::Scene& scene = model.scenes[defaultScene];
    std::unordered_map<size_t, size_t> nodeParents;
    for (size_t i = 0; i < scene.nodes.size(); ++i)
    {
        const size_t nodeId = scene.nodes[i];
        if (InRange(nodeId, 0, model.nodes.size()))
        {
            PopulateNodeParents(model, nodeId, nodeParents);
        }
    }

    mAllSkeletonRoots.resize(model.skins.size(), -1);
    std::set<size_t> allJointNodes;
    for (size_t i = 0; i < model.skins.size(); i++)
    {
        const auto& skin = model.skins[i];
        for (size_t j = 0; j < skin.joints.size(); j++)
        {
            allJointNodes.insert(skin.joints[j]);
        }
    }

    auto rootBone = FindCommonRootBone(model, nodeParents, allJointNodes);
    for (size_t i = 0; i < model.skins.size(); i++)
    {
        // It's possible that joints in the same skeleton does not have a common root.
        mAllSkeletonRoots[i] = rootBone;
    }
}

void GltfImporter::PopulateStageAnimationInfomation(StagePtr& stage, tinygltf::Model& model, double scale)
{
    AnimationTrack animationTrack;
    animationTrack.fps = DEFAULT_FPS;
    for (size_t i = 0; i < model.animations.size(); i++)
    {
        // TODO: To support other fps number.
        const auto& animation = model.animations[i];
        if (animation.name.empty())
        {
            animationTrack.name = "AnimationTrack" + std::to_string(i);
        }
        else
        {
            animationTrack.name = animation.name;
        }

        std::unordered_map<size_t, NodeAnimationSampler> nodeAnimationSamplers;
        double maxDurationInSeconds = 0.0;
        for (size_t j = 0; j < animation.channels.size(); j++)
        {
            const auto& channel = animation.channels[j];
            const int nodeIndex = channel.target_node;
            const int samplerIndex = channel.sampler;
            if (nodeIndex >= 0 && nodeIndex < model.nodes.size() && samplerIndex >= 0 && samplerIndex < animation.samplers.size())
            {
                auto& nodeAnimationSampler = nodeAnimationSamplers[nodeIndex];
                if (channel.target_path == "translation")
                {
                    nodeAnimationSampler.position = samplerIndex;
                }
                else if (channel.target_path == "rotation")
                {
                    nodeAnimationSampler.rotation = samplerIndex;
                }
                else if (channel.target_path == "scale")
                {
                    nodeAnimationSampler.scale = samplerIndex;
                }
            }
        }
        mNodeAnimationSamplerTracks.push_back(nodeAnimationSamplers);
        stage->animationTracks.push_back(animationTrack);
    }

    for (size_t i = 0; i < model.skins.size(); i++)
    {
        const auto& skin = model.skins[i];
        if (skin.inverseBindMatrices == -1)
        {
            continue;
        }
        auto inverseBindMatrices = PopulateNumberBufferData<PXR_NS::GfMatrix4d>(model, skin.inverseBindMatrices);
        if (inverseBindMatrices.empty())
        {
            continue;
        }

        for (size_t j = 0; j < skin.joints.size(); j++)
        {
            auto bindTransform = inverseBindMatrices[j].GetInverse();
            bindTransform.SetTranslateOnly(bindTransform.ExtractTranslation() * scale);
            mJointBindMatrices.insert({ skin.joints[j], bindTransform });
        }
    }
}

TransformAnimationTracks GltfImporter::PopulateNodeAnimation(const StagePtr& stage, tinygltf::Model& model, size_t nodeIndex, double scale)
{
    if (mThreadContext->converterContext.IgnoreAnimations())
    {
        return {};
    }

    auto InterpolateVec3d = [](const PXR_NS::GfVec3d& start, const PXR_NS::GfVec3d& end, double factor)
    {
        return (start + factor * (end - start));
    };

    static auto InterpolateQuatd = [](const PXR_NS::GfVec4d& start, const PXR_NS::GfVec4d& end, double factor)
    {
        PXR_NS::GfQuatd startQuat(start[3], start[0], start[1], start[2]);
        PXR_NS::GfQuatd endQuat(end[3], end[0], end[1], end[2]);
        auto interpolatedQuat = PXR_NS::GfSlerp(startQuat, endQuat, factor);

        return interpolatedQuat;
    };

    TransformAnimationTracks transformAnimationTracks(mNodeAnimationSamplerTracks.size());
    for (size_t i = 0; i < mNodeAnimationSamplerTracks.size(); i++)
    {
        auto& animationTrack = stage->animationTracks[i];
        const auto& animation = model.animations[i];
        const auto& nodeAnimationSamplerTrack = mNodeAnimationSamplerTracks[i];
        auto iter = nodeAnimationSamplerTrack.find(nodeIndex);
        if (iter == nodeAnimationSamplerTrack.end())
        {
            continue;
        }

        const auto& nodeAnimationSampler = iter->second;
        PXR_NS::VtVec3dArray translations;
        PXR_NS::VtVec3dArray scales;
        PXR_NS::VtQuatdArray orients;
        double timeDelta = 1 / animationTrack.fps;
        if (nodeAnimationSampler.position != -1)
        {
            const auto& positionSampler = animation.samplers[nodeAnimationSampler.position];
            translations = PopulateTimesamplesFromAnimationSampler<PXR_NS::GfVec3d, PXR_NS::GfVec3d>(
                model,
                positionSampler,
                timeDelta,
                InterpolateVec3d,
                scale
            );
        }

        if (nodeAnimationSampler.rotation != -1)
        {
            const auto& rotationSampler = animation.samplers[nodeAnimationSampler.rotation];
            orients = PopulateTimesamplesFromAnimationSampler<PXR_NS::GfQuatd, PXR_NS::GfVec4d>(model, rotationSampler, timeDelta, InterpolateQuatd);
        }

        if (nodeAnimationSampler.scale != -1)
        {
            const auto& scaleSampler = animation.samplers[nodeAnimationSampler.scale];
            scales = PopulateTimesamplesFromAnimationSampler<PXR_NS::GfVec3d, PXR_NS::GfVec3d>(model, scaleSampler, timeDelta, InterpolateVec3d);
        }

        // Update global information here to calculate the longest track.
        size_t keyFrames = std::max(translations.size(), scales.size());
        keyFrames = std::max(keyFrames, orients.size());
        animationTrack.keyFrames = std::max(keyFrames, animationTrack.keyFrames);
        stage->maxKeyFrames = std::max(stage->maxKeyFrames, keyFrames);
        stage->mutiplier = animationTrack.fps / 24.0;
        transformAnimationTracks[i] = TransformTimesamples(translations, orients, scales);
    }

    return transformAnimationTracks;
}

void GltfImporter::PopulateNodeParents(tinygltf::Model& model, size_t currentNodeIndex, std::unordered_map<size_t, size_t>& nodeParents)
{
    const auto& node = model.nodes[currentNodeIndex];
    for (size_t i = 0; i < node.children.size(); i++)
    {
        if (InRange(node.children[i], 0, model.nodes.size()))
        {
            nodeParents.insert({ node.children[i], currentNodeIndex });
            PopulateNodeParents(model, node.children[i], nodeParents);
        }
    }
}

size_t GltfImporter::FindCommonRootBone(
    tinygltf::Model& model,
    const std::unordered_map<size_t, size_t>& nodeParents,
    const std::set<size_t>& allJointNodes
)
{
    if (allJointNodes.size() == 1)
    {
        return *allJointNodes.begin();
    }

    std::vector<size_t> nodeAndParents;
    auto GetNodeAndAllParents = [&nodeParents](size_t node)
    {
        std::vector<size_t> parents; // From topmost to bottom.
        parents.push_back(node);
        auto parent = node;
        auto iter = nodeParents.find(node);
        while (iter != nodeParents.end())
        {
            parent = iter->second;
            parents.push_back(parent);
            iter = nodeParents.find(parent);
        }

        std::reverse(parents.begin(), parents.end());

        return parents;
    };

    size_t commonRoot = *allJointNodes.begin();
    auto parents = GetNodeAndAllParents(commonRoot);
    size_t commonIndex = parents.size() - 1;
    for (auto iter = std::next(allJointNodes.begin()); iter != allJointNodes.end(); iter++)
    {
        auto nodeParents = GetNodeAndAllParents(*iter);
        // That means they are not from the same root tree.
        if (parents[0] != nodeParents[0])
        {
            continue;
        }

        size_t maxIndex = std::min(commonIndex, nodeParents.size() - 1);
        for (size_t i = 0; i <= maxIndex; i++)
        {
            if (parents[i] != nodeParents[i])
            {
                commonIndex = i - 1;
                break;
            }
        }
    }

    return parents[commonIndex];
}

int GltfImporter::GetDefaultScene(tinygltf::Model& model)
{
    int defaultScene = model.defaultScene;
    if (!InRange(defaultScene, 0, model.scenes.size()))
    {
        defaultScene = 0;
    }

    return defaultScene;
}

bool GltfImporter::IsSkeletonRoot(size_t nodeIndex)
{
    auto iter = std::find(mAllSkeletonRoots.begin(), mAllSkeletonRoots.end(), nodeIndex);

    return iter != mAllSkeletonRoots.end();
}

bool GltfImporter::HasSkinnedMeshInSkeleton(const StagePtr& stage, size_t skinIndex, size_t meshIndex)
{
    for (size_t i = 0; i < stage->skinMeshes.size(); i++)
    {
        const auto& skinMesh = stage->skinMeshes[i];
        if (mMeshSkinIndex[i] == skinIndex && skinMesh->meshIndex == meshIndex)
        {
            return true;
        }
    }

    return false;
}

void GltfImporter::FillInfluencedBones(const StagePtr& stage, tinygltf::Model& model)
{
    // Post-processing skin mesh to fill all influenced bones.
    for (size_t i = 0; i < stage->skinMeshes.size(); i++)
    {
        const auto& skinMesh = stage->skinMeshes[i];
        size_t skinIndex = mMeshSkinIndex[i];
        size_t skinRoot = mAllSkeletonRoots[skinIndex];
        skinMesh->skeletonRoot = mAllStageNodes[skinRoot];
        const auto& skin = model.skins[skinIndex];
        for (size_t nodeIndex : skin.joints)
        {
            skinMesh->influencedBoneNodes.push_back(mAllStageNodes[nodeIndex]);
        }
    }
}
