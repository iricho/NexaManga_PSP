#include "ImageProbe.hpp"

#include "JpegDecoder.hpp"
#include "MemoryMath.hpp"
#include "PerformanceClock.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

constexpr unsigned char PngSignature[8] = {137, 80, 78, 71, 13, 10, 26, 10};

std::uint32_t readBigEndian32(const unsigned char* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) |
           static_cast<std::uint32_t>(bytes[3]);
}

bool readPngHeader(const PageData& page, std::array<unsigned char, 24>& header) {
    if (!page.filesystemPath.empty()) {
        std::FILE* file = std::fopen(page.filesystemPath.c_str(), "rb");
        if (!file) return false;
        const std::size_t read = std::fread(header.data(), 1, header.size(), file);
        std::fclose(file);
        return read == header.size();
    }
    if (page.bytes.size() < header.size()) return false;
    std::memcpy(header.data(), page.bytes.data(), header.size());
    return true;
}

bool probePng(const PageData& page, const ImageDecodeRequest& request,
              ImageInfo& info, std::string& error) {
    if (request.representation != ImageRepresentation::FullDetail ||
        request.scaleDenominator != 1) {
        error = "PNG scaled/region probing is not implemented.";
        return false;
    }
    std::array<unsigned char, 24> header {};
    if (!readPngHeader(page, header) ||
        std::memcmp(header.data(), PngSignature, sizeof(PngSignature)) != 0 ||
        std::memcmp(header.data() + 12, "IHDR", 4) != 0) {
        error = "PNG header is invalid or truncated.";
        return false;
    }
    const std::uint32_t width = readBigEndian32(header.data() + 16);
    const std::uint32_t height = readBigEndian32(header.data() + 20);
    if (width == 0 || height == 0) {
        error = "PNG dimensions are invalid.";
        return false;
    }

    info = {};
    info.format = ImageFormat::Png;
    info.sourceWidth = width;
    info.sourceHeight = height;
    info.outputWidth = width;
    info.outputHeight = height;
    info.components = 4;
    info.encodedResidentBytes = page.bytes.size();
    info.finalRgb565Bytes = MemoryMath::rgb565Bytes(width, height);
    info.fallbackDecodedBytes = MemoryMath::saturatedMultiply(
        MemoryMath::saturatedMultiply(width, height), 4u);
    info.scanlineWorkspaceBytes = MemoryMath::saturatedMultiply(width, 4u);
    info.estimatedLibraryOverheadBytes = MemoryMath::saturatedAdd(
        256u * 1024u, MemoryMath::saturatedMultiply(info.scanlineWorkspaceBytes, 8u));
    info.trackedTransientBytes = MemoryMath::saturatedAdd(
        info.encodedResidentBytes,
        MemoryMath::saturatedAdd(info.fallbackDecodedBytes, info.finalRgb565Bytes));
    info.predictedTransientBytes = MemoryMath::saturatedAdd(
        info.trackedTransientBytes, info.estimatedLibraryOverheadBytes);
    if (info.predictedTransientBytes == std::numeric_limits<std::size_t>::max()) {
        error = "PNG memory estimate overflowed.";
        return false;
    }
    return true;
}

} // namespace

namespace ImageProbe {

bool probe(const PageData& page, const ImageDecodeRequest& request,
           ImageInfo& info, std::string& error, std::uint64_t* probeMicros) {
    const std::uint64_t started = PerformanceClock::nowMicros();
    JpegDecoder jpeg;
    bool success = false;
    if (jpeg.matches(page)) {
        success = jpeg.probe(page, request, info, error, nullptr);
    } else {
        success = probePng(page, request, info, error);
    }
    if (probeMicros) *probeMicros = PerformanceClock::elapsedMicros(started);
    return success;
}

} // namespace ImageProbe
