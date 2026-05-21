// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#ifdef _WIN32
#    include "unicode.h"

#    include <cassert>


namespace
{
typedef uint32_t CodePoint;
const CodePoint InvalidCodePoint(-1);

const unsigned char First[256][2] = {
    // 00-7F
    { 0x00, 1 },
    { 0x01, 1 },
    { 0x02, 1 },
    { 0x03, 1 },
    { 0x04, 1 },
    { 0x05, 1 },
    { 0x06, 1 },
    { 0x07, 1 },
    { 0x08, 1 },
    { 0x09, 1 },
    { 0x0A, 1 },
    { 0x0B, 1 },
    { 0x0C, 1 },
    { 0x0D, 1 },
    { 0x0E, 1 },
    { 0x0F, 1 },
    { 0x10, 1 },
    { 0x11, 1 },
    { 0x12, 1 },
    { 0x13, 1 },
    { 0x14, 1 },
    { 0x15, 1 },
    { 0x16, 1 },
    { 0x17, 1 },
    { 0x18, 1 },
    { 0x19, 1 },
    { 0x1A, 1 },
    { 0x1B, 1 },
    { 0x1C, 1 },
    { 0x1D, 1 },
    { 0x1E, 1 },
    { 0x1F, 1 },
    { 0x20, 1 },
    { 0x21, 1 },
    { 0x22, 1 },
    { 0x23, 1 },
    { 0x24, 1 },
    { 0x25, 1 },
    { 0x26, 1 },
    { 0x27, 1 },
    { 0x28, 1 },
    { 0x29, 1 },
    { 0x2A, 1 },
    { 0x2B, 1 },
    { 0x2C, 1 },
    { 0x2D, 1 },
    { 0x2E, 1 },
    { 0x2F, 1 },
    { 0x30, 1 },
    { 0x31, 1 },
    { 0x32, 1 },
    { 0x33, 1 },
    { 0x34, 1 },
    { 0x35, 1 },
    { 0x36, 1 },
    { 0x37, 1 },
    { 0x38, 1 },
    { 0x39, 1 },
    { 0x3A, 1 },
    { 0x3B, 1 },
    { 0x3C, 1 },
    { 0x3D, 1 },
    { 0x3E, 1 },
    { 0x3F, 1 },
    { 0x40, 1 },
    { 0x41, 1 },
    { 0x42, 1 },
    { 0x43, 1 },
    { 0x44, 1 },
    { 0x45, 1 },
    { 0x46, 1 },
    { 0x47, 1 },
    { 0x48, 1 },
    { 0x49, 1 },
    { 0x4A, 1 },
    { 0x4B, 1 },
    { 0x4C, 1 },
    { 0x4D, 1 },
    { 0x4E, 1 },
    { 0x4F, 1 },
    { 0x50, 1 },
    { 0x51, 1 },
    { 0x52, 1 },
    { 0x53, 1 },
    { 0x54, 1 },
    { 0x55, 1 },
    { 0x56, 1 },
    { 0x57, 1 },
    { 0x58, 1 },
    { 0x59, 1 },
    { 0x5A, 1 },
    { 0x5B, 1 },
    { 0x5C, 1 },
    { 0x5D, 1 },
    { 0x5E, 1 },
    { 0x5F, 1 },
    { 0x60, 1 },
    { 0x61, 1 },
    { 0x62, 1 },
    { 0x63, 1 },
    { 0x64, 1 },
    { 0x65, 1 },
    { 0x66, 1 },
    { 0x67, 1 },
    { 0x68, 1 },
    { 0x69, 1 },
    { 0x6A, 1 },
    { 0x6B, 1 },
    { 0x6C, 1 },
    { 0x6D, 1 },
    { 0x6E, 1 },
    { 0x6F, 1 },
    { 0x70, 1 },
    { 0x71, 1 },
    { 0x72, 1 },
    { 0x73, 1 },
    { 0x74, 1 },
    { 0x75, 1 },
    { 0x76, 1 },
    { 0x77, 1 },
    { 0x78, 1 },
    { 0x79, 1 },
    { 0x7A, 1 },
    { 0x7B, 1 },
    { 0x7C, 1 },
    { 0x7D, 1 },
    { 0x7E, 1 },
    { 0x7F, 1 },

    // 80-BF
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },

    // C0, C1, C2-DF
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x02, 2 },
    { 0x03, 2 },
    { 0x04, 2 },
    { 0x05, 2 },
    { 0x06, 2 },
    { 0x07, 2 },
    { 0x08, 2 },
    { 0x09, 2 },
    { 0x0A, 2 },
    { 0x0B, 2 },
    { 0x0C, 2 },
    { 0x0D, 2 },
    { 0x0E, 2 },
    { 0x0F, 2 },
    { 0x10, 2 },
    { 0x11, 2 },
    { 0x12, 2 },
    { 0x13, 2 },
    { 0x14, 2 },
    { 0x15, 2 },
    { 0x16, 2 },
    { 0x17, 2 },
    { 0x18, 2 },
    { 0x19, 2 },
    { 0x1A, 2 },
    { 0x1B, 2 },
    { 0x1C, 2 },
    { 0x1D, 2 },
    { 0x1E, 2 },
    { 0x1F, 2 },

    // E0 - EF
    { 0x00, 3 },
    { 0x01, 3 },
    { 0x02, 3 },
    { 0x03, 3 },
    { 0x04, 3 },
    { 0x05, 3 },
    { 0x06, 3 },
    { 0x07, 3 },
    { 0x08, 3 },
    { 0x09, 3 },
    { 0x0A, 3 },
    { 0x0B, 3 },
    { 0x0C, 3 },
    { 0x0D, 3 },
    { 0x0E, 3 },
    { 0x0F, 3 },

    // F0-F7
    { 0x00, 4 },
    { 0x01, 4 },
    { 0x02, 4 },
    { 0x03, 4 },
    { 0x04, 4 },
    { 0x05, 4 },
    { 0x06, 4 },
    { 0x07, 4 },

    // F8-FF
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
    { 0x00, 0 },
};

CodePoint from(const char* data, size_t size)
{
    if (!size)
    {
        return InvalidCodePoint;
    }

    auto& first = First[static_cast<unsigned char>(*data)];
    auto left = first[1];
    if (!size || size < left)
    {
        return InvalidCodePoint;
    }
    CodePoint cp = first[0];
    while (--left)
    {
        cp = (cp << 6) | (static_cast<unsigned char>(*++data) & 0x7Fu);
    }
    return cp;
}

CodePoint from(const wchar_t*& data)
{
    CodePoint cp = *data;
    data++;
    if (cp < 0xD800u || cp >= 0xE000u)
    {
        return cp;
    }
    if (cp < 0xDC00u)
    {
        CodePoint cp2 = *data;
        data++;
        if (cp2 < 0xDC00u || cp2 >= 0xE000u)
        {
            return InvalidCodePoint;
        }
        return (cp << 10) + cp2 - 0x35FDC00lu;
    }
    return InvalidCodePoint;
}

void append(CodePoint cp, std::string& str)
{
    assert(cp != InvalidCodePoint);
    if (cp <= 0x7F)
    {
        str.push_back(static_cast<char>(cp & 0x7Fu));
    }
    else if (cp <= 0x7FF)
    {
        unsigned char buf[] = { static_cast<unsigned char>(0xC0u | (cp >> 6)), static_cast<unsigned char>(0x80u | (cp & 0x3Fu)) };
        str.append(reinterpret_cast<const char*>(buf), sizeof(buf));
    }
    else if (cp <= 0xFFFF)
    {
        unsigned char buf[] = { static_cast<unsigned char>(0xE0u | (cp >> 12)),
                                static_cast<unsigned char>(0x80u | ((cp >> 6) & 0x3Fu)),
                                static_cast<unsigned char>(0x80u | (cp & 0x3Fu)) };
        str.append(reinterpret_cast<const char*>(buf), sizeof(buf));
    }
    else
    {
        unsigned char buf[] = { static_cast<unsigned char>(0xF0u | (cp >> 18)),
                                static_cast<unsigned char>(0x80u | ((cp >> 12) & 0x3Fu)),
                                static_cast<unsigned char>(0x80u | ((cp >> 6) & 0x3Fu)),
                                static_cast<unsigned char>(0x80u | (cp & 0x3Fu)) };
        str.append(reinterpret_cast<const char*>(buf), sizeof(buf));
    }
}

void append(CodePoint cp, std::wstring& str)
{
    assert(cp != InvalidCodePoint);
    if (cp < 0x10000lu)
    {
        str.push_back(static_cast<wchar_t>(cp));
    }
    else
    {
        wchar_t buf[] = { static_cast<wchar_t>((cp >> 10) + 0xD7C0u), static_cast<wchar_t>((cp & 0x3FFu) + 0xDC00u) };
        str.append(buf, sizeof(buf) / sizeof(*buf));
    }
}
} // namespace

namespace Unicode
{
std::wstring convert(const std::string& file)
{
    auto data = file.data();
    auto size = file.size();
    std::wstring result;
    result.reserve(size * 2);
    while (size)
    {
        const auto cp = from(data, size);
        if (cp == InvalidCodePoint)
        {
            return {};
        }
        const auto inc = First[static_cast<unsigned char>(*data)][1];
        data += inc;
        size -= inc;
        append(cp == '/' ? '\\' : cp, result);
    }
    return result;
}

std::string convert(const wchar_t* data)
{
    std::string str;
    str.reserve(512);
    while (*data)
    {
        const auto cp = from(data);
        assert(cp != InvalidCodePoint);
        append(cp == '\\' ? '/' : cp, str);
    }
    return str;
}
} // namespace Unicode
#endif
