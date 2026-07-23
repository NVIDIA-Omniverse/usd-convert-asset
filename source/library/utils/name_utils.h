// SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>


class NameUtils
{
public:

    struct NameInfo
    {
        std::string name;
        std::string displayName;
    };

    using NameCache = std::unordered_map<std::string, size_t>;

    static std::string MakeValidIdentifier(const std::string& name);
    static std::string MakeValidUSDIdentifier(const std::string& name, const std::string& prefix);
    static NameInfo GetValidName(const std::string& name, const std::string& prefix, NameCache& cache);
    static std::vector<NameInfo> GetValidNames(const std::vector<std::string>& names, const std::string& prefix, NameCache& cache);
};
