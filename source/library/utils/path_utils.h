// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include <string>
#include <vector>


class PathUtils
{
public:

    // Returns the current absolute path dependent on your platform.
    static std::string AbsPath(const std::string& path);

    // Makes directories. If the target platform is not writable, it will return false.
    static bool MakeDirectories(const std::string& dir, std::string* error = nullptr);

    // Returns the dir path by removing suffix filename.
    static std::string GetDirName(const std::string& path);

    // Returns the ext name of path without ".".
    static std::string GetExtension(const std::string& path);

    static std::string GetFileName(const std::string& path, bool includeExt = false);

    static std::string JoinPaths(const std::string& basePath, const std::string& subPath);
    static std::string JoinPaths(const std::vector<std::string>& paths);

    // Creates temp folder on your local platform.
    static bool CreateTempFolder(std::string& tempFolder);

    static bool ComputeRelativePath(const std::string& inPath, const std::string& inRelativeToPath, std::string& resultPath);

    // Checkes if path is existed.
    static bool IsPathExisted(const std::string& filePath);

    // Checkes if path is absolute.
    static bool IsAbsolutePath(const std::string& path);

    static std::string NormalizePath(const std::string& path);

    // Checks if two paths are equal.
    static bool Equal(const std::string& p1, const std::string& p2);

    // Gets the module path on your platform.
    static std::string GetModulePath();

    // Gets the mime type of the path.
    static std::string ToMimeType(const std::string& path);

    // Mime type to extension name (without dot).
    static std::string MimeToExt(const std::string& mimeType);

    // Rename path.
    static bool Rename(const std::string& from, const std::string& to);

    // Remove path.
    static bool RemoveAll(const std::string& path);

    static bool IsOmniversePath(const std::string& path);

    static std::string GetUrlScheme(const std::string& url);
};
