#pragma once

#include "PageSource.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

enum class ImageFormat {
    Unknown,
    Jpeg,
    Png
};

enum class ImageRepresentation {
    FullDetail,
    ScaledPreview,
    Region
};

struct ImageDecodeRequest {
    ImageRepresentation representation = ImageRepresentation::FullDetail;
    unsigned int scaleDenominator = 1;
    std::size_t maxLibraryMemoryBytes = 0;
    std::uint32_t regionX = 0;
    std::uint32_t regionY = 0;
    std::uint32_t regionWidth = 0;
    std::uint32_t regionHeight = 0;
};

struct ImageInfo {
    ImageFormat format = ImageFormat::Unknown;
    ImageRepresentation representation = ImageRepresentation::FullDetail;
    std::uint32_t sourceWidth = 0;
    std::uint32_t sourceHeight = 0;
    std::uint32_t outputWidth = 0;
    std::uint32_t outputHeight = 0;
    int components = 0;
    bool grayscale = false;
    bool progressive = false;
    unsigned int scaleDenominator = 1;

    std::size_t encodedResidentBytes = 0;
    std::size_t finalRgb565Bytes = 0;
    std::size_t fallbackDecodedBytes = 0;
    std::size_t scanlineWorkspaceBytes = 0;
    std::size_t estimatedLibraryOverheadBytes = 0;
    std::size_t trackedTransientBytes = 0;
    std::size_t predictedTransientBytes = 0;
};

struct DecodeTiming {
    std::uint64_t sourceReadMicros = 0;
    std::uint64_t headerProbeMicros = 0;
    std::uint64_t jpegDecodeMicros = 0;
    std::uint64_t fallbackDecodeMicros = 0;
    std::uint64_t rgb565OutputMicros = 0;
    std::uint64_t totalMicros = 0;
};

struct Rgb565Target {
    void* pixels = nullptr;
    std::size_t pitchBytes = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct ImageDecodeResult {
    bool success = false;
    ImageInfo info;
    DecodeTiming timing;
    std::string error;
};

class ImageDecoder {
public:
    virtual ~ImageDecoder() = default;

    virtual ImageFormat format() const = 0;
    virtual bool matches(const PageData& page) const = 0;
    virtual bool probe(const PageData& page, const ImageDecodeRequest& request,
                       ImageInfo& info, std::string& error,
                       std::uint64_t* probeMicros = nullptr) const = 0;
    virtual ImageDecodeResult decodeRgb565(const PageData& page,
                                            const ImageDecodeRequest& request,
                                            const Rgb565Target& target) const = 0;
    virtual bool supportsRegionDecode() const { return false; }
};

const char* imageFormatName(ImageFormat format);
