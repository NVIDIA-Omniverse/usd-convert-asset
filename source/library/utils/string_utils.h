// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include <string>


class StringUtils
{
public:

    static std::string ToLower(const std::string& str);
    static void ReplaceAll(std::string& str, const std::string& from, const std::string& to);
};
