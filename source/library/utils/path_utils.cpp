// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "path_utils.h"

#include "../pxr_includes.h"
#include "string_utils.h"
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

#include <algorithm>
#include <array>
#include <cstdint>
#ifndef _WIN32
#    include <dlfcn.h>
#endif

#include <ctype.h>
#include <experimental/filesystem>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#    include <chrono>
#    include <thread>
#endif

namespace fs = std::experimental::filesystem;

namespace
{
constexpr size_t kTempFolderRandomByteCount = 16;
constexpr size_t kTempFolderMaxTries = 100;

#ifdef _WIN32
constexpr int kRenameMaxAttempts = 10;
constexpr auto kRenameRetryDelay = std::chrono::milliseconds(10);

bool IsReservedWindowsDeviceName(const fs::path& component)
{
    std::wstring name = component.native();

    // Windows resolves a device name before a file extension or an alternate data-stream suffix.
    // For example, "CON.txt" and "CON:stream" both refer to the CON device.
    const auto separator = name.find_first_of(L".:");
    if (separator != std::wstring::npos)
    {
        name.resize(separator);
    }

    // Win32 ignores trailing spaces and periods when it resolves a path component.
    while (!name.empty() && (name.back() == L' ' || name.back() == L'.'))
    {
        name.pop_back();
    }

    // Device-name matching is case-insensitive. Only ASCII letters occur in reserved names.
    for (auto& character : name)
    {
        if (character >= L'a' && character <= L'z')
        {
            character -= L'a' - L'A';
        }
    }

    // These names refer to DOS character devices in every Windows directory.
    if (name == L"CON" || name == L"CONIN$" || name == L"CONOUT$" || name == L"PRN" || name == L"AUX" || name == L"NUL")
    {
        return true;
    }

    // COM and LPT are reserved only with a recognized one-character port number.
    if (name.size() != 4 || (name.compare(0, 3, L"COM") != 0 && name.compare(0, 3, L"LPT") != 0))
    {
        return false;
    }
    const wchar_t suffix = name.back();

    // Windows also recognizes the ISO-8859-1 superscript forms for ports 1, 2, and 3.
    return (suffix >= L'1' && suffix <= L'9') || suffix == L'\u00b9' || suffix == L'\u00b2' || suffix == L'\u00b3';
}

std::wstring ToExtendedWindowsPath(const fs::path& path)
{
    std::wstring native = path.native();
    std::replace(native.begin(), native.end(), L'/', L'\\');
    if (native.rfind(L"\\\\?\\", 0) == 0)
    {
        return native;
    }

    native = fs::absolute(path).native();
    std::replace(native.begin(), native.end(), L'/', L'\\');
    if (native.rfind(L"\\\\", 0) == 0)
    {
        return L"\\\\?\\UNC\\" + native.substr(2);
    }
    return L"\\\\?\\" + native;
}
#endif

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
    const auto path = fs::u8path(dir);
    if (fs::create_directories(path, ec))
    {
        return true;
    }
    // Another conversion may have created the directory after the existence check.
    std::error_code statusError;
    if (fs::is_directory(path, statusError))
    {
        return true;
    }
    if (error)
    {
        *error = ec ? ec.message() : "Path is not a directory: " + dir;
    }
    return false;
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

std::string PathUtils::NormalizeAssetPathSeparators(const std::string& path)
{
    std::string normalizedPath = path;
    // Replace separator pairs first so each pair produces one forward slash.
    StringUtils::ReplaceAll(normalizedPath, "\\\\", "/");
    StringUtils::ReplaceAll(normalizedPath, "\\", "/");
    return normalizedPath;
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
#ifdef _WIN32
    try
    {
        // Replace an existing file without first deleting it or falling back to a copy.
        const auto source = ToExtendedWindowsPath(fs::u8path(from));
        const auto target = ToExtendedWindowsPath(fs::u8path(to));
        for (int attempt = 0; attempt < kRenameMaxAttempts; ++attempt)
        {
            if (MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING) != 0)
            {
                return true;
            }

            const DWORD error = GetLastError();
            if (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION)
            {
                return false;
            }
            if (attempt + 1 < kRenameMaxAttempts)
            {
                std::this_thread::sleep_for(kRenameRetryDelay);
            }
        }
        return false;
    }
    catch (const fs::filesystem_error&)
    {
        return false;
    }
#else
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
#endif
}

bool PathUtils::ValidateOutputPath(const std::string& path, std::string& error)
{
#ifdef _WIN32
    const auto requestedTarget = fs::u8path(path);
    for (const auto& component : requestedTarget)
    {
        if (IsReservedWindowsDeviceName(component))
        {
            error = "Cannot replace output " + path + ": path contains a reserved Windows device name";
            return false;
        }
    }
#else
    static_cast<void>(path);
    static_cast<void>(error);
#endif
    return true;
}

bool PathUtils::CheckOutputReplaceable(const std::string& path, std::string& error)
{
    const auto fail = [&](const std::string& reason)
    {
        error = "Cannot replace output " + path + ": " + reason;
        return false;
    };
    const auto requestedTarget = fs::u8path(path);
    if (!ValidateOutputPath(path, error))
    {
        return false;
    }
    try
    {
        const auto target = fs::absolute(requestedTarget);
        const auto parent = target.parent_path();
#ifdef _WIN32
        const auto nativeTarget = ToExtendedWindowsPath(target);
        const DWORD attributes = GetFileAttributesW(nativeTarget.c_str());
        const bool existing = attributes != INVALID_FILE_ATTRIBUTES;
        if (!existing)
        {
            const DWORD code = GetLastError();
            if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND)
            {
                return fail(std::error_code(code, std::system_category()).message());
            }
        }
        else if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 || (attributes & FILE_ATTRIBUTE_READONLY) != 0)
        {
            return fail("destination is not a writable regular file");
        }
#else
        const long configuredLimit = pathconf(parent.c_str(), _PC_PATH_MAX);
        const size_t limit = configuredLimit > 0 ? static_cast<size_t>(configuredLimit) : 4096;
        const size_t length = target.native().size();
        const size_t parentLength = parent.native().size();
        if (length >= limit)
        {
            return fail("path exceeds the filesystem limit");
        }

        const bool existing = fs::exists(target);
        if (existing)
        {
            const auto status = fs::status(target);
            const auto writePermissions = fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
            if (!fs::is_regular_file(status) || (status.permissions() & writePermissions) == fs::perms::none)
            {
                return fail("destination is not a writable regular file");
            }
        }
#endif

        // Open an existing target without truncation. Use a short sibling file
        // to verify that a new target can be created and replaced.
        const auto probe = [&](const fs::path& candidate, bool openExisting)
        {
#ifdef _WIN32
            const auto nativeCandidate = ToExtendedWindowsPath(candidate);
            HANDLE handle = CreateFileW(
                nativeCandidate.c_str(),
                GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                openExisting ? OPEN_EXISTING : CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL,
                nullptr
            );
            if (handle == INVALID_HANDLE_VALUE)
            {
                return fail(std::error_code(GetLastError(), std::system_category()).message());
            }
            DWORD written = 0;
            const bool success = openExisting || (WriteFile(handle, "0", 1, &written, nullptr) && written == 1);
            const bool closed = CloseHandle(handle) != 0;
            const bool removed = openExisting || DeleteFileW(nativeCandidate.c_str()) != 0;
#else
            const int descriptor = open(candidate.c_str(), O_WRONLY | O_CLOEXEC | (openExisting ? 0 : O_CREAT | O_EXCL), 0600);
            if (descriptor < 0)
            {
                return fail(std::error_code(errno, std::generic_category()).message());
            }
            const bool success = openExisting || write(descriptor, "0", 1) == 1;
            const bool closed = close(descriptor) == 0;
            const bool removed = openExisting || unlink(candidate.c_str()) == 0;
#endif
            if (!success || !closed || !removed)
            {
                return fail("could not write or clean up the output probe");
            }
            return true;
        };

        if (existing && !probe(target, true))
        {
            return false;
        }

        std::array<uint8_t, 6> bytes;
        if (!FillRandomBytes(bytes.data(), bytes.size()))
        {
            return fail("cannot generate a temporary filename");
        }
        std::ostringstream name;
        for (const auto byte : bytes)
        {
            name << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
        }
#ifdef _WIN32
        const auto candidate = parent / name.str();
#else
        const auto candidate = parent / name.str().substr(0, std::min<size_t>(12, limit - parentLength - 2));
#endif
        return probe(candidate, false);
    }
    catch (const fs::filesystem_error& exception)
    {
        return fail(exception.what());
    }
}

bool PathUtils::ReplaceOutputFile(const std::string& from, const std::string& to)
{
    std::string error;
    if (!CheckOutputReplaceable(to, error))
    {
        return false;
    }
    return Rename(from, to);
}

bool PathUtils::RemoveAll(const std::string& path)
{
    fs::path p = fs::u8path(path);
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
