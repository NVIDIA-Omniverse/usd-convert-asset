// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#include "../usd_convert_asset_internal.h"

#include <cstdint>


struct ImageHeader
{
    int width = 0;
    int height = 0;
    int channels = 0;
    int bitsPerChannel = 8;
};

struct Image
{
    ImageHeader info;
    OmniConverterBlobPtr decompressedData;
};

class ImageUtils
{
public:

    // Checks if the image has 4 channels.
    static bool HasAlphaChannel(const uint8_t* compressedImageData, size_t size);

    static bool LoadImageInfoFromMemory(const uint8_t* compressedImageData, size_t size, ImageHeader& header);

    static bool LoadImageFromMemory(const uint8_t* compressedImageData, size_t size, Image& image);

#if 0
    static Image CreateDecompressedImage(int width, int height, int bitsPerChannel, int channels);

    /*
     * For each pixel, the value will be scaled as value = min(scale * value + offset * PIXEL_CHANNEL_MAX_VALUE,
     * PIXEL_CHANNEL_MAX_VALUE) * colorTint
     */
    static OmniConverterBlobPtr ScaleImageAsPNG(const uint8_t* compressedImageData, size_t size, double scale = 1.0, double offset = 0.0);
#endif

    // Extracts specific channel from source image and replace the target channel.
    // Both source and target image must be compressed data.
    // If any of source image and target cannot be loaded, it will return nullptr.
    // If the image width and height are different, it will return nullptr.
    // If targetImageChannel == -1, it will append the source channel to target image.
    // If targetImageChannel >= total channels of target image, it will return nullptr.
    // If sourceImageChannel == -1, it will average all channels of source image to mix them together.
    // If sourceImageChannel >= total channels of source image, it will return nullptr.
    // The scaleSource and offsetSource can be used to scale the source channel, like:
    // <final source value> = min(scaleSource * <source value> + offsetSource * PIXEL_CHANNEL_MAX_VALUE,
    // PIXEL_CHANNEL_MAX_VALUE) Return value is the compressed image data.
    static OmniConverterBlobPtr ExtractSourceChannelAndSetTargetChannelAsPNG(
        const uint8_t* targetImageData,
        size_t targetImageSize,
        int targetImageChannel,
        const uint8_t* sourceImageData,
        size_t sourceImageSize,
        int sourceImageChannel,
        double scaleSource = 1.0,
        double offsetSource = 0.0
    );

    // The same as ExtractSourceChannelAndSetTargetChannelAsPNG, but it will work for decompressed
    // image data and do in-place change. Also, targetImageChannel must be less than target image channels.
    static bool ExtractSourceChannelAndSetTargetChannel(
        Image& targetImage,
        int targetImageChannel,
        const uint8_t* sourceImageData,
        size_t sourceImageSize,
        int sourceImageChannel,
        double scaleSource = 1.0,
        double offsetSource = 0.0
    );

    // Extracts channels for two images to fill two channels of a 3 channels image.
    // Both images must be compressed data.
    // If any of images cannot be loaded, it will return nullptr.
    // If the two images' width and height are different, it will return nullptr.
    // If firstImageChannel == -1, it will average all channels of first image to mix them together.
    // If firstImageChannel >= total channels of first image, it will return nullptr.
    // If secondImageChannel == -1, it will average all channels of second image to mix them together.
    // If secondImageChannel >= total channels of second image, it will return nullptr.
    // firstTargetChannel and secondTargetChannel must be in range [0, 3).
    // The scaleFirst/Second and offsetFirst/Second can be used to scale the channel, like:
    // <final value> = min(scale * <value> + offset * PIXEL_CHANNEL_MAX_VALUE, PIXEL_CHANNEL_MAX_VALUE)
    // Return value is the compressed image data that has 3 channels.
    static OmniConverterBlobPtr MergeTwoImageChannelsAsPNG(
        const uint8_t* firstImageData,
        size_t firstImageSize,
        int firstImageChannel,
        int firstTargetChannel,
        const uint8_t* secondImageData,
        size_t secondImageSize,
        int secondImageChannel,
        int secondTargetChannel,
        double scaleFirst = 1.0,
        double offsetFirst = 0.0,
        double scaleSecond = 1.0,
        double offsetSecond = 0.0
    );
};
