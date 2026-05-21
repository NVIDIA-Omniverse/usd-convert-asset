// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "../usd_convert_asset_internal.h"

#include <fbxsdk.h>
#include <vector>

class CustomFBXReadStream : public FbxStream
{
public:

    CustomFBXReadStream(FbxManager* pSdkManager, const OmniConverterBlobPtr& dataBlob);

    virtual FbxStream::EState GetState();

    virtual bool Open(void* pStreamData);

    virtual bool Close()
    {
        return true;
    }

    virtual bool Flush()
    {
        return true;
    }

    virtual size_t Write(const void* pData, FbxUInt64 pSize)
    {
        return 0;
    }

    virtual size_t Read(void* pData, FbxUInt64 pSize) const override;

    virtual int GetReaderID() const
    {
        return mReaderID;
    }

    virtual int GetWriterID() const
    {
        return mWriterID;
    }

    virtual void Seek(const FbxInt64& pOffset, const FbxFile::ESeekPos& pSeekPos);

    virtual FbxInt64 GetPosition() const
    {
        return mCurrentPosition;
    }

    virtual void SetPosition(FbxInt64 pPosition)
    {
        if (mData)
        {
            mCurrentPosition = pPosition;
        }
    }

    virtual int GetError() const
    {
        return 0;
    }

    size_t GetLength() const
    {
        return mCurrentPosition;
    }

    virtual void ClearError()
    {
    }

private:

    OmniConverterBlobPtr mData;
    mutable FbxInt64 mCurrentPosition = 0;
    int mReaderID = -1;
    int mWriterID = -1;
};

class CustomFBXWriteStream : public FbxStream
{
public:

    CustomFBXWriteStream(FbxManager* pSdkManager, const OmniConverterBlobPtr& dataBlob);

    virtual FbxStream::EState GetState();

    virtual bool Open(void* pStreamData);

    virtual bool Close()
    {
        return true;
    }

    virtual bool Flush()
    {
        return true;
    }

    virtual size_t Write(const void* pData, FbxUInt64 pSize);

    virtual size_t Read(void* pData, FbxUInt64 pSize) const;

    virtual int GetReaderID() const
    {
        return mReaderID;
    }

    virtual int GetWriterID() const
    {
        return mWriterID;
    }

    virtual void Seek(const fbxsdk::FbxInt64& pOffset, const fbxsdk::FbxFile::ESeekPos& pSeekPos);

    virtual FbxInt64 GetPosition() const
    {
        return mCurrentPosition;
    }

    virtual void SetPosition(FbxInt64 pPosition)
    {
        if (mData)
        {
            mCurrentPosition = pPosition;
        }
    }

    virtual int GetError() const
    {
        return 0;
    }

    size_t GetLength() const
    {
        return mData ? mData->size : 0;
    }

    virtual void ClearError()
    {
    }

private:

    OmniConverterBlobPtr mData;
    FbxUInt64 mDataCapacity = 0;
    int mReaderID = -1;
    int mWriterID = -1;
    mutable FbxInt64 mCurrentPosition = 0;
};
