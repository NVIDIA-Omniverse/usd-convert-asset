// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "path_utils.h"

#include "../pxr_includes.h"
#include "unicode.h"

#include <usd_convert_asset.h>

#ifdef _WIN32
#    include <bcrypt.h>
#    include <windows.h>
#    pragma comment(lib, "bcrypt.lib")
#else
#    include <errno.h>
#    include <fcntl.h>
#    include <sys/stat.h>
#    include <unistd.h>
#endif

#include <array>
#include <cstdint>
#ifndef _WIN32
#    include <dlfcn.h>
#endif

#include <ctype.h>
#include <experimental/filesystem>
#include <iomanip>
#include <sstream>

namespace fs = std::experimental::filesystem;

namespace
{
constexpr size_t kTempFolderRandomByteCount = 16;
constexpr size_t kTempFolderMaxTries = 100;

bool FillRandomBytes(uint8_t* data, size_t size)
{
#ifdef _WIN32
    return BCryptGenRandom(nullptr, data, static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    int fd = open(
        "/dev/urandom",
        O_RDONLY
#    ifdef O_CLOEXEC
            | O_CLOEXEC
#    endif
    );
    if (fd < 0)
    {
        return false;
    }

#    ifndef O_CLOEXEC
    fcntl(fd, F_SETFD, FD_CLOEXEC);
#    endif

    size_t bytesRead = 0;
    while (bytesRead < size)
    {
        ssize_t result = read(fd, data + bytesRead, size - bytesRead);
        if (result > 0)
        {
            bytesRead += static_cast<size_t>(result);
        }
        else if (result == -1 && errno == EINTR)
        {
            continue;
        }
        else
        {
            close(fd);
            return false;
        }
    }

    close(fd);
    return true;
#endif
}

bool CreatePrivateTempDirectory(const fs::path& path, std::error_code& ec)
{
#ifdef _WIN32
    // The random directory name is created atomically beneath the user's temp root.
    return fs::create_directory(path, ec);
#else
    ec.clear();
    if (mkdir(path.string().c_str(), S_IRWXU) == 0)
    {
        return true;
    }

    ec = std::error_code(errno, std::generic_category());
    return false;
#endif
}

std::string GenerateTempFolderName()
{
    std::array<uint8_t, kTempFolderRandomByteCount> bytes;
    if (!FillRandomBytes(bytes.data(), bytes.size()))
    {
        return "";
    }

    std::stringstream ss;
    ss << "omniverse_asset_converter_";
    for (uint8_t byte : bytes)
    {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }

    return ss.str();
}
} // namespace


// Return std::string::npos if there is no scheme present
// Otherwise returns the index of the colon
static size_t ParseScheme(const std::string& uri)
{
    // The scheme is defined as ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )
    // That is, it must start with alpha, followed by any number of alpha, digit, plus, minus, or dot
    // It then ends with a colon
    if (uri.empty())
    {
        return std::string::npos;
    }
    if (!isalpha(uri[0]))
    {
        return std::string::npos;
    }
    for (size_t i = 1; i < uri.length(); i++)
    {
        if (uri[i] == ':')
        {
            return i;
        }
        else if (isalnum(uri[i]))
        {
            continue;
        }
        else if (uri[i] == '.' || uri[i] == '+' || uri[i] == '-')
        {
            continue;
        }
        else
        {
            break;
        }
    }

    return std::string::npos;
}

// returns tuple of (root, path components, filename)
// For relative paths, assume the current working directory
//
// Examples, assuming our working directory is D:\folder :
// splitPathOrUri("C:/path/to/foo.usd") -> ("C", ["path", "to"], "foo.usd")
// splitPathOrUri("C:/path/") -> ("C", ["path"], ".")
// splitPathOrUri("stuff/foo.usd") -> ("D", ["folder", "stuff"], "foo.usd")
// splitPathOrUri("") -> ("D", ["folder"], ".")
static std::tuple<std::string, std::vector<std::string>, std::string> SplitPathOrUri(const std::string& pathOrUri)
{
    size_t schemePos = ParseScheme(pathOrUri);
    std::string tempPath = pathOrUri;
    std::string root;
    if (schemePos != std::string::npos)
    {
        root = pathOrUri.substr(0, schemePos);
        root = PXR_NS::TfStringToLower(root);
        tempPath = pathOrUri.substr(schemePos + 1);
    }

    fs::path path(tempPath);

    // Get and remove filename and root, if they exist
    std::string filename = path.filename().string();
    path.remove_filename();

    // Collect folders
    std::vector<std::string> folders;
    for (const auto& folder : path)
    {
        std::string folderString = folder.string();
        folderString = PXR_NS::TfStringTrim(folderString, " \\/\n\t\r");
        if (folderString.size() > 0)
        {
            folders.push_back(folder.string());
        }
    }

    return std::make_tuple(root, folders, filename);
}

std::string PathUtils::AbsPath(const std::string& path)
{
    if (PXR_NS::SdfLayer::IsAnonymousLayerIdentifier(path))
    {
        return path;
    }

    std::string absolutePath = path;
    if (!IsAbsolutePath(path))
    {
        absolutePath = PXR_NS::TfAbsPath(path);
    }

    return NormalizePath(absolutePath);
}

bool PathUtils::MakeDirectories(const std::string& dir, std::string* error)
{
    if (PXR_NS::SdfLayer::IsAnonymousLayerIdentifier(dir))
    {
        return true;
    }

    // URL other than local path
    size_t schemePos = ParseScheme(dir);
    if (schemePos != 1 && schemePos != std::string::npos)
    {
        return true;
    }

    std::error_code ec;
    return fs::create_directories(fs::u8path(dir), ec);
}

std::string PathUtils::GetDirName(const std::string& path)
{
    if (PXR_NS::SdfLayer::IsAnonymousLayerIdentifier(path))
    {
        return "";
    }

    const std::string& dir = PXR_NS::TfGetPathName(path);
    return NormalizePath(dir);
}

std::string PathUtils::GetExtension(const std::string& path)
{
    return PXR_NS::TfGetExtension(path);
}

std::string PathUtils::GetFileName(const std::string& path, bool includeExt)
{
    if (PXR_NS::SdfLayer::IsAnonymousLayerIdentifier(path))
    {
        return PXR_NS::SdfLayer::GetDisplayNameFromIdentifier(path);
    }

    std::string fileName = PXR_NS::TfGetBaseName(path);
    if (!includeExt)
    {
        fileName = PXR_NS::TfStringGetBeforeSuffix(fileName);
    }

    return NormalizePath(fileName);
}

std::string PathUtils::JoinPaths(const std::string& basePath, const std::string& subPath)
{
    if (PXR_NS::SdfLayer::IsAnonymousLayerIdentifier(basePath) || PXR_NS::SdfLayer::IsAnonymousLayerIdentifier(subPath))
    {
        return subPath;
    }

    if (basePath.empty() || IsAbsolutePath(subPath))
    {
        return subPath;
    }

    std::string base = NormalizePath(basePath);
    ;
    if (basePath.back() != '/')
    {
        base += "/";
    }

    return NormalizePath(base + subPath);
}

std::string PathUtils::JoinPaths(const std::vector<std::string>& paths)
{
    if (paths.empty())
    {
        return "";
    }

    std::string joinedPath = paths[0];
    for (size_t i = 1; i < paths.size(); i++)
    {
        joinedPath = PathUtils::JoinPaths(joinedPath, paths[i]);
    }

    return joinedPath;
}

bool PathUtils::CreateTempFolder(std::string& tempFolder)
{
    std::error_code ec;
    // Use the OS temp root only as a parent; the child directory is created with
    // an unpredictable CSPRNG-backed name.
    auto tempDir = fs::temp_directory_path(ec);
    if (ec)
    {
        return false;
    }

    for (size_t i = 0; i < kTempFolderMaxTries; i++)
    {
        const std::string folderName = GenerateTempFolderName();
        if (folderName.empty())
        {
            return false;
        }

        const fs::path path = tempDir / folderName;
        if (CreatePrivateTempDirectory(path, ec))
        {
            // Create successfully
            tempFolder = path.string();
            return true;
        }
    }

    return false;
}

bool PathUtils::ComputeRelativePath(const std::string& inPath, const std::string& inRelativeToPath, std::string& resultPath)
{
    if (PXR_NS::SdfLayer::IsAnonymousLayerIdentifier(inPath) || PXR_NS::SdfLayer::IsAnonymousLayerIdentifier(inRelativeToPath))
    {
        resultPath = inPath;
        return false;
    }

    resultPath = inPath;
    std::string path = PathUtils::NormalizePath(inPath);
    std::string relativePath = PathUtils::NormalizePath(inRelativeToPath);

    std::string pathPrefix;
    std::vector<std::string> pathParts;
    std::string pathFileName;
    std::tie(pathPrefix, pathParts, pathFileName) = SplitPathOrUri(inPath);

    std::string relativeToPathPrefix;
    std::vector<std::string> relativeToPathParts;
    std::tie(relativeToPathPrefix, relativeToPathParts, std::ignore) = SplitPathOrUri(inRelativeToPath);

    // check if two paths are not on the same disk or domain
    if (pathPrefix.length() != relativeToPathPrefix.length())
    {
        return false;
    }
    for (auto c1 = pathPrefix.begin(), c2 = relativeToPathPrefix.begin(); c1 != pathPrefix.end(); ++c1, ++c2)
    {
        if (tolower(*c1) != tolower(*c2))
        {
            return false;
        }
    }

    // Form relative path
    size_t i = 0;
    size_t j = 0;
    while (i < pathParts.size() && j < relativeToPathParts.size())
    {
        if (pathParts[i] != relativeToPathParts[j])
        {
            break;
        }

        i++;
        j++;
    }

    if (i == pathParts.size() && j == relativeToPathParts.size())
    {
        resultPath = ".";
    }
    else if (i == pathParts.size())
    {
        std::string relativePart = ".";
        size_t leftParts = relativeToPathParts.size() - j;
        for (size_t k = 0; k < leftParts; k++)
        {
            relativePart += "/..";
        }
        resultPath = relativePart;
    }
    else if (j == relativeToPathParts.size())
    {
        std::string relativePart = ".";
        for (; i < pathParts.size(); i++)
        {
            relativePart += "/" + pathParts[i];
        }
        resultPath = relativePart;
    }
    else
    {
        std::string relativePart = ".";
        size_t leftParts = relativeToPathParts.size() - j;
        for (size_t k = 0; k < leftParts; k++)
        {
            relativePart += "/..";
        }
        for (; i < pathParts.size(); i++)
        {
            relativePart += "/" + pathParts[i];
        }
        resultPath = relativePart;
    }
    resultPath += "/" + pathFileName;

    return true;
}

bool PathUtils::IsPathExisted(const std::string& filePath)
{
    if (PXR_NS::SdfLayer::IsAnonymousLayerIdentifier(filePath))
    {
        auto layer = PXR_NS::SdfLayer::Find(filePath);
        if (!layer)
        {
            return false;
        }
        else
        {
            return true;
        }
    }

    return !PXR_NS::ArGetResolver().Resolve(filePath).empty();
}

bool PathUtils::IsAbsolutePath(const std::string& path)
{
    if (PXR_NS::SdfLayer::IsAnonymousLayerIdentifier(path))
    {
        return true;
    }

    // Simply returns if filesystem interface treats it as absolute path.
    bool isLocalAbsolute = fs::path(path).is_absolute();
    if (isLocalAbsolute)
    {
        return true;
    }

    // If it's url, like "http://xx"
    size_t pos = ParseScheme(path);
    if (pos != std::string::npos)
    {
        return true;
    }

    return false;
}

std::string PathUtils::NormalizePath(const std::string& path)
{
    // For URL, skips the normalization.
    size_t schemePos = ParseScheme(path);
    if (schemePos != 1 && schemePos != std::string::npos)
    {
        return path;
    }

    bool hasTrailingSlash = path.back() == '\\' || path.back() == '/';
    std::string normalizedPath = PXR_NS::TfNormPath(path);
    if (hasTrailingSlash)
    {
        normalizedPath += "/";
    }

    return normalizedPath;
}

bool PathUtils::Equal(const std::string& p1, const std::string& p2)
{
    if (PXR_NS::SdfLayer::IsAnonymousLayerIdentifier(p1) || PXR_NS::SdfLayer::IsAnonymousLayerIdentifier(p2))
    {
        return p1 == p2;
    }
    else
    {
        return PathUtils::NormalizePath(p1) == PathUtils::NormalizePath(p2);
    }
}

std::string PathUtils::GetModulePath()
{
#if defined(_WIN32)
    TCHAR szDLLFile[MAX_PATH + 1];
    HMODULE hModule;

    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPTSTR)GetModulePath, &hModule);
    GetModuleFileName(hModule, szDLLFile, MAX_PATH);
    auto fullPath = fs::path(szDLLFile);
#else

    Dl_info dlInfo;
    dladdr((const void*)omniConverterCreateUSD, &dlInfo);
    auto fullPath = fs::path(dlInfo.dli_fname);
#endif
    auto base = fullPath.parent_path().string();
    std::replace(base.begin(), base.end(), '\\', '/');
    if (base.length() > 0)
    {
        return base;
    }

    return ".";
}

std::string PathUtils::ToMimeType(const std::string& path)
{
    const std::string& ext = PathUtils::GetExtension(path);
    if (ext == "jpg" || ext == "jpeg")
    {
        return "image/jpeg";
    }
    else if (ext == "png")
    {
        return "image/png";
    }
    else if (ext == "bmp")
    {
        return "image/bmp";
    }
    else if (ext == "gif")
    {
        return "image/gif";
    }

    return "";
}

std::string PathUtils::MimeToExt(const std::string& mimeType)
{
    if (mimeType == "image/jpeg")
    {
        return "jpg";
    }
    else if (mimeType == "image/png")
    {
        return "png";
    }
    else if (mimeType == "image/bmp")
    {
        return "bmp";
    }
    else if (mimeType == "image/gif")
    {
        return "gif";
    }
    else if (mimeType == "image/webp")
    {
        return "webp";
    }

    return "";
}

bool PathUtils::Rename(const std::string& from, const std::string& to)
{
    fs::path f(from);
    fs::path t(to);
    std::error_code ec;

    fs::rename(f, t, ec);

    if (ec)
    {
        return false;
    }
    else
    {
        return true;
    }
}

bool PathUtils::RemoveAll(const std::string& path)
{
    fs::path p(path);
    std::error_code ec;
    fs::remove_all(p, ec);

    if (ec)
    {
        return false;
    }
    else
    {
        return true;
    }
}

bool PathUtils::IsOmniversePath(const std::string& path)
{
    return PXR_NS::TfStringStartsWith(path, "omniverse://");
}

std::string PathUtils::GetUrlScheme(const std::string& url)
{
    size_t schemePos = ParseScheme(url);
    std::string scheme;
    if (schemePos != std::string::npos)
    {
        scheme = url.substr(0, schemePos);
        scheme = PXR_NS::TfStringToLower(scheme);
    }

    return scheme;
}
