// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "../../common/tiny_gltf_include.h"
#include "../../pxr_includes.h"

namespace omni
{
namespace assetconverter
{
namespace exporter
{
namespace gltf
{

using JointIndices4UShort = std::array<uint16_t, 4>;
using Color4UShort = JointIndices4UShort;

template <typename T>
struct TypeTraits
{
    const static size_t Components = 0;
    const static int GltfType = 0;
    const static int GltfComponentType = 0;
};

template <>
struct TypeTraits<JointIndices4UShort>
{
    const static size_t Components = 4;
    const static int GltfType = TINYGLTF_TYPE_VEC4;
    const static int GltfComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
};

template <>
struct TypeTraits<unsigned short>
{
    const static size_t Components = 1;
    const static int GltfType = TINYGLTF_TYPE_SCALAR;
    const static int GltfComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
};

template <>
struct TypeTraits<short>
{
    const static size_t Components = 1;
    const static int GltfType = TINYGLTF_TYPE_SCALAR;
    const static int GltfComponentType = TINYGLTF_COMPONENT_TYPE_SHORT;
};

template <>
struct TypeTraits<unsigned int>
{
    const static size_t Components = 1;
    const static int GltfType = TINYGLTF_TYPE_SCALAR;
    const static int GltfComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
};

template <>
struct TypeTraits<int>
{
    const static size_t Components = 1;
    const static int GltfType = TINYGLTF_TYPE_SCALAR;
    const static int GltfComponentType = TINYGLTF_COMPONENT_TYPE_INT;
};

template <>
struct TypeTraits<float>
{
    const static size_t Components = 1;
    const static int GltfType = TINYGLTF_TYPE_SCALAR;
    const static int GltfComponentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
};

template <>
struct TypeTraits<double>
{
    const static size_t Components = 1;
    const static int GltfType = TINYGLTF_TYPE_SCALAR;
    const static int GltfComponentType = TINYGLTF_COMPONENT_TYPE_DOUBLE;
};

template <>
struct TypeTraits<PXR_NS::GfVec2f>
{
    const static size_t Components = 2;
    const static int GltfType = TINYGLTF_TYPE_VEC2;
    const static int GltfComponentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
};

template <>
struct TypeTraits<PXR_NS::GfVec2d>
{
    const static size_t Components = 2;
    const static int GltfType = TINYGLTF_TYPE_VEC2;
    const static int GltfComponentType = TINYGLTF_COMPONENT_TYPE_DOUBLE;
};

template <>
struct TypeTraits<PXR_NS::GfVec3f>
{
    const static size_t Components = 3;
    const static int GltfType = TINYGLTF_TYPE_VEC3;
    const static int GltfComponentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
};

template <>
struct TypeTraits<PXR_NS::GfVec3d>
{
    const static size_t Components = 3;
    const static int GltfType = TINYGLTF_TYPE_VEC3;
    const static int GltfComponentType = TINYGLTF_COMPONENT_TYPE_DOUBLE;
};

template <>
struct TypeTraits<PXR_NS::GfVec4f>
{
    const static size_t Components = 4;
    const static int GltfType = TINYGLTF_TYPE_VEC4;
    const static int GltfComponentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
};

template <>
struct TypeTraits<PXR_NS::GfVec4d>
{
    const static size_t Components = 4;
    const static int GltfType = TINYGLTF_TYPE_VEC4;
    const static int GltfComponentType = TINYGLTF_COMPONENT_TYPE_DOUBLE;
};

template <>
struct TypeTraits<PXR_NS::GfQuatf>
{
    const static size_t Components = 4;
    const static int GltfType = TINYGLTF_TYPE_VEC4;
    const static int GltfComponentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
};

template <>
struct TypeTraits<PXR_NS::GfQuatd>
{
    const static size_t Components = 4;
    const static int GltfType = TINYGLTF_TYPE_VEC4;
    const static int GltfComponentType = TINYGLTF_COMPONENT_TYPE_DOUBLE;
};

template <>
struct TypeTraits<PXR_NS::GfMatrix4f>
{
    const static size_t Components = 16;
    const static int GltfType = TINYGLTF_TYPE_MAT4;
    const static int GltfComponentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
};

template <>
struct TypeTraits<PXR_NS::GfMatrix4d>
{
    const static size_t Components = 16;
    const static int GltfType = TINYGLTF_TYPE_MAT4;
    const static int GltfComponentType = TINYGLTF_COMPONENT_TYPE_DOUBLE;
};

} // namespace gltf
} // namespace exporter
} // namespace assetconverter
} // namespace omni
