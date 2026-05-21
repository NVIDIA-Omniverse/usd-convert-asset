// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "../../pxr_includes.h"

#include <string>
#include <vector>

// The following utils are refered from
// https://raw.githubusercontent.com/syoyo/tinygltf/master/examples/raytrace/gltf-loader.h.

namespace omni
{
namespace assetconverter
{
namespace importer
{
namespace gltf
{

template <typename ParamType, typename ReturnType>
ReturnType ConstructFrom(const ParamType& value)
{
    return ReturnType(value);
}

template <>
PXR_NS::GfQuatd ConstructFrom<PXR_NS::GfVec4d, PXR_NS::GfQuatd>(const PXR_NS::GfVec4d& value)
{
    // Gltf has the 3rd component as real
    return PXR_NS::GfQuatd(value[3], value[0], value[1], value[2]);
}

template <>
PXR_NS::GfVec4d ConstructFrom<PXR_NS::GfQuatd, PXR_NS::GfVec4d>(const PXR_NS::GfQuatd& value)
{
    auto im = value.GetImaginary();
    return PXR_NS::GfVec4d(im[0], im[1], im[2], value.GetReal());
}

template <typename ValueType>
struct TinygltfTypeTraits
{
    const static int value = TINYGLTF_TYPE_SCALAR;
    typedef float FloatType;
    typedef double DoubleType;
};

template <>
struct TinygltfTypeTraits<float>
{
    const static int value = TINYGLTF_TYPE_SCALAR;
    typedef float FloatType;
    typedef double DoubleType;
};

template <>
struct TinygltfTypeTraits<double>
{
    const static int value = TINYGLTF_TYPE_SCALAR;
    typedef float FloatType;
    typedef double DoubleType;
};

template <>
struct TinygltfTypeTraits<PXR_NS::GfVec2f>
{
    const static int value = TINYGLTF_TYPE_VEC2;
    typedef PXR_NS::GfVec2f FloatType;
    typedef PXR_NS::GfVec2d DoubleType;
};

template <>
struct TinygltfTypeTraits<PXR_NS::GfVec2d>
{
    const static int value = TINYGLTF_TYPE_VEC2;
    typedef PXR_NS::GfVec2f FloatType;
    typedef PXR_NS::GfVec2d DoubleType;
};

template <>
struct TinygltfTypeTraits<PXR_NS::GfVec3f>
{
    const static int value = TINYGLTF_TYPE_VEC3;
    typedef PXR_NS::GfVec3f FloatType;
    typedef PXR_NS::GfVec3d DoubleType;
};

template <>
struct TinygltfTypeTraits<PXR_NS::GfVec3d>
{
    const static int value = TINYGLTF_TYPE_VEC3;
    typedef PXR_NS::GfVec3f FloatType;
    typedef PXR_NS::GfVec3d DoubleType;
};

template <>
struct TinygltfTypeTraits<PXR_NS::GfVec4f>
{
    const static int value = TINYGLTF_TYPE_VEC4;
    typedef PXR_NS::GfVec4f FloatType;
    typedef PXR_NS::GfVec4d DoubleType;
};

template <>
struct TinygltfTypeTraits<PXR_NS::GfVec4d>
{
    const static int value = TINYGLTF_TYPE_VEC4;
    typedef PXR_NS::GfVec4f FloatType;
    typedef PXR_NS::GfVec4d DoubleType;
};

template <>
struct TinygltfTypeTraits<PXR_NS::GfQuatf>
{
    const static int value = TINYGLTF_TYPE_VEC4;
    typedef PXR_NS::GfVec4f FloatType;
    typedef PXR_NS::GfVec4d DoubleType;
};

template <>
struct TinygltfTypeTraits<PXR_NS::GfQuatd>
{
    const static int value = TINYGLTF_TYPE_VEC4;
    typedef PXR_NS::GfVec4f FloatType;
    typedef PXR_NS::GfVec4d DoubleType;
};

template <>
struct TinygltfTypeTraits<PXR_NS::GfMatrix4f>
{
    const static int value = TINYGLTF_TYPE_MAT4;
    typedef PXR_NS::GfMatrix4f FloatType;
    typedef PXR_NS::GfMatrix4d DoubleType;
};

template <>
struct TinygltfTypeTraits<PXR_NS::GfMatrix4d>
{
    const static int value = TINYGLTF_TYPE_MAT4;
    typedef PXR_NS::GfMatrix4f FloatType;
    typedef PXR_NS::GfMatrix4d DoubleType;
};

template <typename ValueType, int>
struct UsdArrayTypeTraits
{
    typename PXR_NS::VtArray<ValueType> type;
};

template <>
struct UsdArrayTypeTraits<float, TINYGLTF_TYPE_VEC3>
{
    typename PXR_NS::VtArray<PXR_NS::GfVec3f> UsdArrayType;
};

template <>
struct UsdArrayTypeTraits<double, TINYGLTF_TYPE_VEC3>
{
    typename PXR_NS::VtArray<PXR_NS::GfVec3d> UsdArrayType;
};

template <>
struct UsdArrayTypeTraits<float, TINYGLTF_TYPE_VEC4>
{
    typename PXR_NS::VtArray<PXR_NS::GfVec4f> UsdArrayType;
};

template <>
struct UsdArrayTypeTraits<double, TINYGLTF_TYPE_VEC4>
{
    typename PXR_NS::VtArray<PXR_NS::GfVec4d> UsdArrayType;
};

/// Adapts an array of bytes to an array of T. Will advace of byte_stride each
/// elements.
template <typename T>
struct ArrayAdapter
{
    /// Pointer to the bytes
    const unsigned char* dataPtr;
    /// Number of elements in the array
    const size_t elemCount;
    /// Stride in bytes between two elements
    const size_t stride;

    /// Construct an array adapter.
    /// \param ptr Pointer to the start of the data, with offset applied
    /// \param count Number of elements in the array
    /// \param byte_stride Stride betweens elements in the array
    ArrayAdapter(const unsigned char* ptr, size_t count, size_t byte_stride) : dataPtr(ptr), elemCount(count), stride(byte_stride)
    {
    }

    /// Returns a *copy* of a single element. Can't be used to modify it.
    T operator[](size_t pos) const
    {
        return *(reinterpret_cast<const T*>(dataPtr + pos * stride));
    }
};

/// Interface of any adapted array that returns ingeger data
struct IntArrayBase
{
    virtual ~IntArrayBase() = default;
    virtual unsigned int operator[](size_t) const = 0;
    virtual size_t size() const = 0;
};

/// Interface of any adapted array that returns float data
struct FloatArrayBase
{
    virtual ~FloatArrayBase() = default;
    virtual float operator[](size_t) const = 0;
    virtual size_t size() const = 0;
};

/// An array that loads interger types, returns them as int
template <class T>
struct IntArray : public IntArrayBase
{
    ArrayAdapter<T> adapter;

    IntArray(const ArrayAdapter<T>& a) : adapter(a)
    {
    }
    unsigned int operator[](size_t position) const override
    {
        return static_cast<unsigned int>(adapter[position]);
    }

    size_t size() const override
    {
        return adapter.elemCount;
    }
};

template <class T>
struct FloatArray : public FloatArrayBase
{
    ArrayAdapter<T> adapter;

    FloatArray(const ArrayAdapter<T>& a) : adapter(a)
    {
    }
    float operator[](size_t position) const override
    {
        return static_cast<float>(adapter[position]);
    }

    size_t size() const override
    {
        return adapter.elemCount;
    }
};

struct Vector4IntArray
{
    ArrayAdapter<PXR_NS::GfVec4i> adapter;
    Vector4IntArray(const ArrayAdapter<PXR_NS::GfVec4i>& a) : adapter(a)
    {
    }

    PXR_NS::GfVec4i operator[](size_t position) const
    {
        return adapter[position];
    }
    size_t size() const
    {
        return adapter.elemCount;
    }
};

struct Vector2fArray
{
    ArrayAdapter<PXR_NS::GfVec2f> adapter;
    Vector2fArray(const ArrayAdapter<PXR_NS::GfVec2f>& a) : adapter(a)
    {
    }

    PXR_NS::GfVec2f operator[](size_t position) const
    {
        return adapter[position];
    }
    size_t size() const
    {
        return adapter.elemCount;
    }
};

struct Vector3fArray
{
    ArrayAdapter<PXR_NS::GfVec3f> adapter;
    Vector3fArray(const ArrayAdapter<PXR_NS::GfVec3f>& a) : adapter(a)
    {
    }

    PXR_NS::GfVec3f operator[](size_t position) const
    {
        return adapter[position];
    }
    size_t size() const
    {
        return adapter.elemCount;
    }
};

struct Vector4fArray
{
    ArrayAdapter<PXR_NS::GfVec4f> adapter;
    Vector4fArray(const ArrayAdapter<PXR_NS::GfVec4f>& a) : adapter(a)
    {
    }

    PXR_NS::GfVec4f operator[](size_t position) const
    {
        return adapter[position];
    }
    size_t size() const
    {
        return adapter.elemCount;
    }
};

struct Vector2dArray
{
    ArrayAdapter<PXR_NS::GfVec2d> adapter;
    Vector2dArray(const ArrayAdapter<PXR_NS::GfVec2d>& a) : adapter(a)
    {
    }

    PXR_NS::GfVec2d operator[](size_t position) const
    {
        return adapter[position];
    }
    size_t size() const
    {
        return adapter.elemCount;
    }
};

struct Vector3dArray
{
    ArrayAdapter<PXR_NS::GfVec3d> adapter;
    Vector3dArray(const ArrayAdapter<PXR_NS::GfVec3d>& a) : adapter(a)
    {
    }

    PXR_NS::GfVec3d operator[](size_t position) const
    {
        return adapter[position];
    }
    size_t size() const
    {
        return adapter.elemCount;
    }
};

struct Vector4dArray
{
    ArrayAdapter<PXR_NS::GfVec4d> adapter;
    Vector4dArray(const ArrayAdapter<PXR_NS::GfVec4d>& a) : adapter(a)
    {
    }

    PXR_NS::GfVec4d operator[](size_t position) const
    {
        return adapter[position];
    }
    size_t size() const
    {
        return adapter.elemCount;
    }
};

} // namespace gltf
} // namespace importer
} // namespace assetconverter
} // namespace omni
