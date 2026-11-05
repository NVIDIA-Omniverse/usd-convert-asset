// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#include "converter_future.h"

#include "exporters/exporter.h"
#include "importers/importer.h"
#include "pxr_includes.h"
#include "utils/utils.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>


OmniConverterFuture::~OmniConverterFuture()
{
    Stop();
    Wait();
}

void OmniConverterFuture::Start()
{
    mContext->SetStatus(OmniConverterStatus::IN_PROGRESS);
    mContext->mThisFuture = this;
    mThread = std::thread(OmniConverterFuture::Convert, mContext);
}

void OmniConverterFuture::Stop()
{
    mContext->Exit();
    auto status = GetStatus();
    if (status == OmniConverterStatus::IN_PROGRESS)
    {
        mContext->SetStatus(OmniConverterStatus::CANCELLED);
    }
}

void OmniConverterFuture::Wait()
{
    if (!mThread.joinable())
    {
        return;
    }

    if (mThread.get_id() == std::this_thread::get_id())
    {
        mThread.detach();
        return;
    }

    mThread.join();
}

void OmniConverterFuture::Release()
{
    if (mThread.joinable() && mThread.get_id() == std::this_thread::get_id())
    {
        Stop();
        auto worker = std::make_shared<std::thread>(std::move(mThread));
        std::thread(
            [this, worker]()
            {
                worker->join();
                delete this;
            }
        ).detach();
        return;
    }

    delete this;
}

OmniConverterStatus OmniConverterFuture::GetStatus() const
{
    return mContext->GetStatus();
}

const std::string& OmniConverterFuture::GetDetailedError() const
{
    std::lock_guard<std::mutex> lock(mDetailedErrorSnapshotsMutex);
    auto detailedError = mContext->GetDetailedError();
    if (mDetailedErrorSnapshots.empty() || *mDetailedErrorSnapshots.back() != detailedError)
    {
        mDetailedErrorSnapshots.push_back(std::make_unique<std::string>(std::move(detailedError)));
    }
    return *mDetailedErrorSnapshots.back();
}

void OmniConverterFuture::SetCallbackError(const std::string& error)
{
    mContext->SetCallbackError(error);
}

void OmniConverterFuture::SetTemporaryOutputDirectory(const std::string& path)
{
    mContext->SetTemporaryOutputDirectory(path);
}

std::string OmniConverterFuture::GetTemporaryOutputDirectory() const
{
    return mContext->GetTemporaryOutputDirectory();
}

void OmniConverterFuture::Convert(std::shared_ptr<OmniConverterFuture::FutureThreadContext> context)
{
    OmniConverterStatus status = OmniConverterStatus::OK;
    std::string detailedError;
    const auto applyCallbackError = [&]()
    {
        const std::string callbackError = context->GetCallbackError();
        if (!callbackError.empty() && status != OmniConverterStatus::CANCELLED && !context->IsExited())
        {
            detailedError = callbackError;
            status = OmniConverterStatus::UNKNOWN;
        }
    };

    if (context->IsExited())
    {
        context->converterContext.Log("Asset convert cancelled...");
        context->SetStatus(OmniConverterStatus::CANCELLED);
        return;
    }

    bool fileNotExisted = false;
    const auto& importAssetPath = context->converterContext.GetImportAssetPath();
    const auto& outputAssetPath = context->converterContext.GetOutputAssetPath();
    if (context->converterContext.IsInMemoryImport() && context->converterContext.IsInMemoryOutput())
    {
        context->converterContext.Log("Asset convert failed as both input and output paths are anonymous.");
        context->SetStatus(OmniConverterStatus::UNSUPPORTED_EXPORT_FORMAT);
        return;
    }

    if (context->converterContext.IsInMemoryImport())
    {
        auto layer = PXR_NS::SdfLayer::Find(importAssetPath);
        if (!layer)
        {
            fileNotExisted = true;
        }
    }
    else if (!context->converterContext.IsPathExisted(importAssetPath))
    {
        fileNotExisted = true;
    }

    if (fileNotExisted)
    {
        context->converterContext.Log("Asset convert failed as " + importAssetPath + " cannot be found.");
        context->SetStatus(OmniConverterStatus::FILE_NOT_EXISTED);
        return;
    }

    if (!context->converterContext.IsSupportedImportAsset())
    {
        const std::string extension = StringUtils::ToLower(PathUtils::GetExtension(importAssetPath));
        detailedError = "Unsupported import format";
        if (!extension.empty())
        {
            detailedError += ": ." + extension;
        }
        detailedError += ". Supported formats: " + OmniConverterContext::GetSupportedImportFormatsForError();
        context->converterContext.Log(detailedError);
        context->SetDetailedError(detailedError);
        context->SetStatus(OmniConverterStatus::UNSUPPORTED_IMPORT_FORMAT);
        return;
    }

    if (!context->converterContext.ValidateOutputPath(detailedError))
    {
        context->converterContext.Log(detailedError);
        context->SetDetailedError(detailedError);
        context->SetStatus(OmniConverterStatus::FILE_WRITE_ERROR);
        return;
    }

    // Output can be anonymous layer too so all imports will be in-memory.
    std::string baseOutPath = PathUtils::GetDirName(outputAssetPath);
    if (context->converterContext.IsInMemoryOutput())
    {
        auto layer = PXR_NS::SdfLayer::Find(outputAssetPath);
        if (!layer)
        {
            detailedError = "Asset convert failed as anonymous layer " + outputAssetPath + " cannot be found.";
            context->converterContext.Log(detailedError);
            context->SetStatus(OmniConverterStatus::FILE_NOT_EXISTED);
            return;
        }
    }
    else if (!context->converterContext.IsPathExisted(baseOutPath) && !context->converterContext.MakeDirectories(baseOutPath))
    {
        detailedError = "Asset convert failed as dir " + baseOutPath + " cannot be created.";
        context->converterContext.Log(detailedError);
        context->SetDetailedError(detailedError);
        context->SetStatus(OmniConverterStatus::DIRECTORY_CREATE_FAILED);
        return;
    }

    if (!context->converterContext.CheckOutputReplaceable(detailedError))
    {
        context->converterContext.Log(detailedError);
        context->SetDetailedError(detailedError);
        context->SetStatus(OmniConverterStatus::FILE_WRITE_ERROR);
        return;
    }

    auto importer = CreateImporter(context->converterContext);
    auto startImportTime = std::chrono::system_clock::now();

    // Checks caching
    bool cached = false;
    if (context->converterContext.IsCachingEnabled())
    {
        context->converterContext.Log("Caching is enabled, using cache store: " + context->converterContext.GetCacheFolder());
        const std::string& sha1Hash = importer->ComputeHash(context);
        context->converterContext.SetImportAssetDigest(sha1Hash);
        if (!sha1Hash.empty())
        {
            context->converterContext.Log("Asset hash: " + sha1Hash);
            const std::string& cacheItemPath = PathUtils::JoinPaths(
                { context->converterContext.GetCacheFolder(), sha1Hash, CACHED_USD_MAIN_FILE_NAME }
            );
            if (PathUtils::IsPathExisted(cacheItemPath))
            {
                context->converterContext.Log("Asset caching is found: " + PathUtils::GetDirName(cacheItemPath));

                auto endImportTime = std::chrono::system_clock::now();
                auto importCost = std::chrono::duration_cast<std::chrono::seconds>(endImportTime - startImportTime).count();
                context->converterContext.Log("Costs " + std::to_string(importCost) + "s to import asset.");
                importer.reset();

                auto outputLayer = PXR_NS::SdfLayer::Find(outputAssetPath);
                auto cachedLayer = PXR_NS::SdfLayer::FindOrOpen(cacheItemPath);
                if (!cachedLayer)
                {
                    context->converterContext.Log("ERROR: Cached layer " + cacheItemPath + " is broken. Removing...");
                    if (!PathUtils::RemoveAll(cacheItemPath))
                    {
                        context->converterContext.Log("ERROR: Failed to remove cached path " + cacheItemPath + ".");
                    }
                }
                else
                {
                    cached = true;
                    auto outputStage = PXR_NS::UsdStage::Open(outputLayer);
                    auto rootPrim = outputStage->DefinePrim(PXR_NS::SdfPath::AbsoluteRootPath().AppendElementString("Root"));
#if !defined(_WIN32)
                    if (PathUtils::IsOmniversePath(importAssetPath))
                    {
                        // Omni client needs local path to be prefixed with scheme, otherwise
                        // when the local path is referenced into a non-local stage. The resolver
                        // will concat the path as relative path.
                        rootPrim.GetReferences().AddReference("file:" + cacheItemPath);
                    }
                    else
#endif
                    {
                        rootPrim.GetReferences().AddReference(cacheItemPath);
                    }
                    PXR_NS::UsdUtilsCopyLayerMetadata(cachedLayer, outputLayer);
                    outputStage->SetDefaultPrim(rootPrim);
                }
            }
        }
    }

    if (!cached)
    {
        auto importedStage = importer->ImportStage(context, status, detailedError);
        applyCallbackError();
        auto endImportTime = std::chrono::system_clock::now();
        auto importCost = std::chrono::duration_cast<std::chrono::seconds>(endImportTime - startImportTime).count();
        context->converterContext.Log("Costs " + std::to_string(importCost) + "s for importer to import asset.");

        importer.reset();

        if (importedStage && status == OmniConverterStatus::OK)
        {
            if (!StageUtils::CleanupMeshes(importedStage, detailedError))
            {
                context->converterContext.Log("ERROR: " + detailedError);
                status = OmniConverterStatus::INCOMPLETE_IMPORT_FORMAT;
            }

            if (status == OmniConverterStatus::OK && context->converterContext.SmoothNormals())
            {
                for (const auto& mesh : importedStage->meshes)
                {
                    if (!StageUtils::GenerateSmoothNormals(mesh))
                    {
                        detailedError = "Failed to generate smooth normals";
                        context->converterContext.Log("ERROR: " + detailedError);
                        status = OmniConverterStatus::INCOMPLETE_IMPORT_FORMAT;
                        break;
                    }
                }
            }

            if (status == OmniConverterStatus::OK && context->converterContext.MergeAllMeshes())
            {
                StageUtils::MergeMeshes(importedStage, "mesh");
                if (!StageUtils::CleanupMeshes(importedStage, detailedError))
                {
                    context->converterContext.Log("ERROR: " + detailedError);
                    status = OmniConverterStatus::INCOMPLETE_IMPORT_FORMAT;
                }
            }

            if (status == OmniConverterStatus::OK && importedStage->hasSkippedCorruptGeometry)
            {
                const auto hasMeshPoints = [&importedStage](size_t index)
                {
                    return index < importedStage->meshes.size() && !importedStage->meshes[index]->points.empty();
                };
                bool hasGeometry = false;
                StageUtils::TraverseStageTree(
                    importedStage->rootNode,
                    [&](const StageNodePtr& node)
                    {
                        hasGeometry = hasGeometry || std::any_of(node->staticMeshInstances.begin(), node->staticMeshInstances.end(), hasMeshPoints);
                        return !hasGeometry;
                    }
                );
                hasGeometry = hasGeometry || std::any_of(
                                                 importedStage->skinMeshes.begin(),
                                                 importedStage->skinMeshes.end(),
                                                 [&hasMeshPoints](const SkinMeshPtr& skinMesh)
                                                 {
                                                     return hasMeshPoints(skinMesh->meshIndex);
                                                 }
                                             );
                if (!hasGeometry)
                {
                    detailedError = "Asset " + importAssetPath + " contains corrupt geometry; no valid geometry remains after cleanup.";
                    context->converterContext.Log("ERROR: " + detailedError);
                    status = OmniConverterStatus::INCOMPLETE_IMPORT_FORMAT;
                }
            }

            // Creates root node always
            if (!importedStage->rootNode)
            {
                importedStage->rootNode = std::make_shared<StageNode>("Root");
            }

            if (status == OmniConverterStatus::OK)
            {
                auto exporter = CreateExporter(context);
                auto startExportTime = std::chrono::system_clock::now();
                status = exporter->Export(importedStage, detailedError);
                auto endExportTime = std::chrono::system_clock::now();
                auto exportCost = std::chrono::duration_cast<std::chrono::seconds>(endExportTime - startExportTime).count();
                context->converterContext.Log("Costs " + std::to_string(exportCost) + "s for exporter to export asset.");
            }
        }
    }

    context->converterContext.clear();
    context->FinishProgress();
    applyCallbackError();
    context->SetDetailedError(detailedError);
    context->SetStatus(status);
}

void OmniConverterFuture::FutureThreadContext::SetStatus(OmniConverterStatus status)
{
    std::lock_guard<std::mutex> exitLock(mExitMutex);
    std::lock_guard<std::mutex> lock(mStatusMutex);
    if (mExited && status != OmniConverterStatus::CANCELLED)
    {
        return;
    }
    mCurrentStatus = status;
}

OmniConverterStatus OmniConverterFuture::FutureThreadContext::GetStatus()
{
    std::lock_guard<std::mutex> lock(mStatusMutex);
    return mCurrentStatus;
}

void OmniConverterFuture::FutureThreadContext::SetDetailedError(const std::string& error)
{
    std::lock_guard<std::mutex> lock(mStatusMutex);
    mDetailedError = error;
}

std::string OmniConverterFuture::FutureThreadContext::GetDetailedError()
{
    std::lock_guard<std::mutex> lock(mStatusMutex);
    return mDetailedError;
}

void OmniConverterFuture::FutureThreadContext::SetCallbackError(const std::string& error)
{
    std::lock_guard<std::mutex> lock(mStatusMutex);
    if (mCallbackError.empty())
    {
        mCallbackError = error;
    }
}

std::string OmniConverterFuture::FutureThreadContext::GetCallbackError()
{
    std::lock_guard<std::mutex> lock(mStatusMutex);
    return mCallbackError;
}

void OmniConverterFuture::FutureThreadContext::SetTemporaryOutputDirectory(const std::string& path)
{
    std::lock_guard<std::mutex> lock(mStatusMutex);
    mTemporaryOutputDirectory = path;
}

std::string OmniConverterFuture::FutureThreadContext::GetTemporaryOutputDirectory()
{
    std::lock_guard<std::mutex> lock(mStatusMutex);
    return mTemporaryOutputDirectory;
}

bool OmniConverterFuture::FutureThreadContext::MaterialLoader(OmniConverterMaterialDescription* material)
{
    OmniConverterFuture* future = nullptr;
    {
        std::lock_guard<std::mutex> lock(mExitMutex);
        if (mExited)
        {
            return false;
        }
        future = mThisFuture;
    }

    return converterContext.LoadMaterial(future, material);
}

void OmniConverterFuture::FutureThreadContext::StartProgress(uint32_t totalSteps)
{
    OmniConverterFuture* future = nullptr;
    {
        std::lock_guard<std::mutex> lock(mExitMutex);
        if (mExited)
        {
            return;
        }
        mTotalSteps = totalSteps;
        future = mThisFuture;
    }

    converterContext.ReportProgress(future, 0, totalSteps);
}

void OmniConverterFuture::FutureThreadContext::IncrementProgress(uint32_t steps)
{
    OmniConverterFuture* future = nullptr;
    uint32_t currentProgress = 0;
    uint32_t totalSteps = 0;
    {
        std::lock_guard<std::mutex> lock(mExitMutex);
        if (mExited)
        {
            return;
        }
        mCurrentProgress += steps;
        if (mCurrentProgress > mTotalSteps)
        {
            mCurrentProgress = mTotalSteps;
        }
        future = mThisFuture;
        currentProgress = mCurrentProgress;
        totalSteps = mTotalSteps;
    }

    converterContext.ReportProgress(future, currentProgress, totalSteps);
}

void OmniConverterFuture::FutureThreadContext::FinishProgress()
{
    OmniConverterFuture* future = nullptr;
    uint32_t totalSteps = 0;
    {
        std::lock_guard<std::mutex> lock(mExitMutex);
        if (mExited)
        {
            return;
        }
        future = mThisFuture;
        totalSteps = mTotalSteps;
    }

    converterContext.ReportProgress(future, totalSteps, totalSteps);
}

bool OmniConverterFuture::FutureThreadContext::IsExited()
{
    std::lock_guard<std::mutex> lock(mExitMutex);
    return mExited;
}

void OmniConverterFuture::FutureThreadContext::Exit()
{
    std::lock_guard<std::mutex> lock(mExitMutex);
    mExited = true;
    mThisFuture = nullptr;
}
