// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "image_utils.h"
#include "math_utils.h"
#include "name_utils.h"
#include "path_utils.h"
#include "stage_utils.h"
#include "string_utils.h"


struct membuf : std::streambuf
{
    membuf(char const* base, size_t size)
    {
        char* p(const_cast<char*>(base));
        this->setg(p, p, p + size);
    }
};

struct imemstream : virtual membuf, std::istream
{
    imemstream(char const* base, size_t size) : membuf(base, size), std::istream(static_cast<std::streambuf*>(this))
    {
    }
};
