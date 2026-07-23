// SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "name_utils.h"

#include "../pxr_includes.h"

#include <algorithm>
#include <sstream>


namespace
{

std::string GetCacheKey(const std::string& name)
{
    return PXR_NS::TfStringToLower(name);
}

bool IsNameUsed(const NameUtils::NameCache& cache, const std::string& name)
{
    return cache.find(GetCacheKey(name)) != cache.end();
}

bool IsFutureName(const std::vector<std::string>& names, size_t nameIndex, const std::string& name)
{
    return std::find(names.begin() + nameIndex + 1, names.end(), name) != names.end();
}

bool NeedsDisplayName(const std::string& originalName)
{
    const std::string validIdentifier = NameUtils::MakeValidIdentifier(originalName);
    return validIdentifier.empty() || validIdentifier[0] == '_';
}

NameUtils::NameInfo MakeNameInfo(const std::string& originalName, const std::string& validName)
{
    NameUtils::NameInfo info;
    info.name = validName;
    if (NeedsDisplayName(originalName))
    {
        info.displayName = originalName;
    }
    return info;
}

} // namespace

std::string NameUtils::MakeValidIdentifier(const std::string& name)
{
// see https://openusd.org/dev/api/_usd__page__u_t_f_8.html
#if PXR_MINOR_VERSION >= 24
    if (name.empty())
    {
        return "_";
    }

    constexpr PXR_NS::TfUtf8CodePoint cp_underscore = PXR_NS::TfUtf8CodePointFromAscii('_');

    bool firstCp = true;
    std::stringstream stream;
    for (auto cp : PXR_NS::TfUtf8CodePointView{ name })
    {
        const bool cpAllowed = firstCp ? (cp == cp_underscore || PXR_NS::TfIsUtf8CodePointXidStart(cp)) : PXR_NS::TfIsUtf8CodePointXidContinue(cp);
        if (!cpAllowed)
        {
            stream << '_';
        }
        else
        {
            stream << cp;
        }

        firstCp = false;
    }
    return stream.str();
#else
    return PXR_NS::TfMakeValidIdentifier(name);
#endif
}

std::string NameUtils::MakeValidUSDIdentifier(const std::string& name, const std::string& prefix)
{
    auto validName = MakeValidIdentifier(name);
    if (validName.empty() || validName[0] == '_')
    {
        validName = prefix + validName;
    }

    return validName;
}

NameUtils::NameInfo NameUtils::GetValidName(const std::string& name, const std::string& prefix, NameCache& cache)
{
    return GetValidNames({ name }, prefix, cache).front();
}

std::vector<NameUtils::NameInfo> NameUtils::GetValidNames(const std::vector<std::string>& names, const std::string& prefix, NameCache& cache)
{
    std::vector<NameInfo> result;
    result.reserve(names.size());

    for (size_t nameIndex = 0; nameIndex < names.size(); ++nameIndex)
    {
        const std::string& originalName = names[nameIndex];
        const std::string validName = MakeValidUSDIdentifier(originalName, prefix);

        std::string uniqueName = validName;
        while (true)
        {
            if (!IsNameUsed(cache, uniqueName) && (uniqueName == validName || !IsFutureName(names, nameIndex, uniqueName)))
            {
                cache.emplace(GetCacheKey(uniqueName), 0);
                result.push_back(MakeNameInfo(originalName, uniqueName));
                break;
            }

            size_t& suffix = cache[GetCacheKey(validName)];
            ++suffix;
            uniqueName = MakeValidUSDIdentifier(PXR_NS::TfStringPrintf("%s_%zu", originalName.c_str(), suffix), prefix);
        }
    }

    return result;
}
