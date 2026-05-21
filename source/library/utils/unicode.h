// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#ifdef _WIN32
#    include <string>


namespace Unicode
{
std::wstring convert(const std::string& file);
std::string convert(const wchar_t* file);
} // namespace Unicode
#endif
