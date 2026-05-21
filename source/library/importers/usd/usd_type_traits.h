// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "../../pxr_includes.h"

namespace omni
{
namespace assetconverter
{
namespace importer
{
namespace usd
{

template <typename T>
struct TypeTraits
{
    typedef void FloatType;
    typedef void DoubleType;
    typedef void HalfType;
};

template <>
struct TypeTraits<PXR_NS::GfVec3f>
{
    typedef PXR_NS::GfVec3f FloatType;
    typedef PXR_NS::GfVec3d DoubleType;
    typedef PXR_NS::GfVec3h HalfType;
};

template <>
struct TypeTraits<PXR_NS::GfVec3d>
{
    typedef PXR_NS::GfVec3f FloatType;
    typedef PXR_NS::GfVec3d DoubleType;
    typedef PXR_NS::GfVec3h HalfType;
};

template <>
struct TypeTraits<PXR_NS::GfQuatf>
{
    typedef PXR_NS::GfQuatf FloatType;
    typedef PXR_NS::GfQuatd DoubleType;
    typedef PXR_NS::GfQuath HalfType;
};

template <>
struct TypeTraits<PXR_NS::GfQuatd>
{
    typedef PXR_NS::GfQuatf FloatType;
    typedef PXR_NS::GfQuatd DoubleType;
    typedef PXR_NS::GfQuath HalfType;
};

template <>
struct TypeTraits<PXR_NS::GfQuath>
{
    typedef PXR_NS::GfQuatf FloatType;
    typedef PXR_NS::GfQuatd DoubleType;
    typedef PXR_NS::GfQuath HalfType;
};

template <>
struct TypeTraits<PXR_NS::GfMatrix4f>
{
    typedef PXR_NS::GfMatrix4f FloatType;
    typedef PXR_NS::GfMatrix4d DoubleType;
    typedef PXR_NS::GfMatrix4f HalfType;
};

template <>
struct TypeTraits<PXR_NS::GfMatrix4d>
{
    typedef PXR_NS::GfMatrix4f FloatType;
    typedef PXR_NS::GfMatrix4d DoubleType;
    typedef PXR_NS::GfMatrix4f HalfType;
};

} // namespace usd
} // namespace importer
} // namespace assetconverter
} // namespace omni
