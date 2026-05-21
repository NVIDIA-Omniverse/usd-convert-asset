// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "../pxr_includes.h"

#include <vector>

// Utility Functions, override operations for vectors
namespace
{
float GfCompMult(const float f1, const float f2)
{
    return f1 * f2;
}

PXR_NS::GfVec2f operator/(const PXR_NS::GfVec2f& v1, const PXR_NS::GfVec2f& v2)
{
    return PXR_NS::GfVec2f(v1[0] / v2[0], v1[1] / v2[1]);
}

PXR_NS::GfVec3f operator/(const PXR_NS::GfVec3f& v1, const PXR_NS::GfVec3f& v2)
{
    return PXR_NS::GfVec3f(v1[0] / v2[0], v1[1] / v2[1], v1[2] / v2[2]);
}
} // namespace

template <typename T>
class CubicSpline
{
public:

    CubicSpline() = default;
    void clear()
    {
        m_coefficient.clear();
    }

    CubicSpline(const T* controlPoints, uint32_t pointCount)
    {
        if (controlPoints)
        {
            setup(controlPoints, pointCount);
        }
    }

    CubicSpline& setupPinnedControlPoints(const T* controlPoints, uint32_t pointCount)
    {
        if (pointCount < 4)
        {
            return *this;
        }

        const T p0 = GfCompMult(T(2.0f), controlPoints[0]) - controlPoints[1];
        const T pn = GfCompMult(T(2.0f), controlPoints[pointCount - 1]) - controlPoints[pointCount - 2];

        m_coefficient.resize(pointCount - 1);
        m_coefficient[0].a = p0;
        m_coefficient[0].b = controlPoints[0];
        m_coefficient[0].c = controlPoints[1];
        m_coefficient[0].d = controlPoints[2];

        for (uint32_t i = 0; i < pointCount - 2; ++i)
        {
            m_coefficient[i + 1].a = controlPoints[i];
            m_coefficient[i + 1].b = controlPoints[i + 1];
            m_coefficient[i + 1].c = controlPoints[i + 2];
            m_coefficient[i + 1].d = controlPoints[i + 3];
        }

        m_coefficient[pointCount - 2].a = controlPoints[pointCount - 3];
        m_coefficient[pointCount - 2].b = controlPoints[pointCount - 2];
        m_coefficient[pointCount - 2].c = controlPoints[pointCount - 1];
        m_coefficient[pointCount - 2].d = pn;

        return *this;
    }

    CubicSpline& setup(const T* controlPoints, uint32_t pointCount)
    {
        m_coefficient.clear();

        // The following code is based on the article from http://graphicsrunner.blogspot.co.uk/2008/05/camera-animation-part-ii.html
        static const T kHalf = T(0.5f);
        static const T kTwo = T(2.0f);
        static const T kThree = T(3.0f);

        auto gamma = [&](unsigned i) -> T&
        {
            return m_coefficient[i].a;
        };
        auto delta = [&](unsigned i) -> T&
        {
            return m_coefficient[i].c;
        };
        auto D = [&](unsigned i) -> T&
        {
            return m_coefficient[i].b;
        };

        m_coefficient.resize(pointCount);
        // Calculate Gamma =: m_coefficient.a
        gamma(0) = kHalf;
        for (uint32_t i = 1; i < pointCount - 1; i++)
        {
            gamma(i) = T(1.0f) / (T(4.0f) - gamma(i - 1));
        }
        gamma(pointCount - 1) = T(1.0f) / (T(2.0f) - gamma(pointCount - 2));

        // Calculate Delta := m_coefficient.c (b will be used straight for D)
        delta(0) = GfCompMult(GfCompMult(kThree, (controlPoints[1] - controlPoints[0])), gamma(0));

        for (uint32_t i = 1; i < pointCount; i++)
        {
            uint32_t index = (i == (pointCount - 1)) ? i : i + 1;
            delta(i) = GfCompMult(GfCompMult(kThree, (controlPoints[index] - controlPoints[i - 1]) - delta(i - 1)), gamma(i));
        }

        // Calculate D := m_coefficient.b
        D(pointCount - 1) = delta(pointCount - 1);

        for (int32_t i = int32_t(pointCount - 2); i >= 0; i--)
        {
            D(i) = delta(i) - GfCompMult(gamma(i), D(i + 1));
        }

        // Calculate the coefficients
        for (uint32_t i = 0; i < pointCount - 1; i++)
        {
            m_coefficient[i].a = controlPoints[i];
            // m_coefficient[i].b = D[i]; no-op
            m_coefficient[i].c = GfCompMult(kThree, (controlPoints[i + 1] - controlPoints[i])) - GfCompMult(kTwo, D(i)) - D(i + 1);
            m_coefficient[i].d = GfCompMult(kTwo, (controlPoints[i] - controlPoints[i + 1])) + D(i) + D(i + 1);
        }

        // Resize from cache size to the final size
        m_coefficient.resize(pointCount - 1);

        return *this;
    }

    T interpolate(uint32_t section, float point) const
    {
        const CubicCoeff& coeff = m_coefficient[section];
        T result = (((coeff.d * point) + coeff.c) * point + coeff.b) * point + coeff.a;
        return result;
    }

    T interpolatePinned(uint32_t section, float t) const
    {
        const CubicCoeff& coeff = m_coefficient[section];
        constexpr float rcp6 = 1.0f / 6.0f;
        const float t2 = t * t;
        const float t3 = t2 * t;

        T result = rcp6 *
                   ((-1.0f * coeff.a + 3.0f * coeff.b - 3.0f * coeff.c + coeff.d) * t3 + (3.0f * coeff.a - 6.0f * coeff.b + 3.0f * coeff.c) * t2 +
                    (-3.0f * coeff.a + 3.0f * coeff.c) * t + (coeff.a + 4.0f * coeff.b + coeff.c));
        return result;
    }

private:

    struct CubicCoeff
    {
        T a, b, c, d;
    };
    std::vector<CubicCoeff> m_coefficient;
};
