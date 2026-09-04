#include "JpegDecoder.hpp"

#include "MemoryMath.hpp"
#include "PathUtils.hpp"
#include "PerformanceClock.hpp"

#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <limits>

extern "C" {
#include <jpeglib.h>
}

namespace {

struct JpegErrorManager {
    jpeg_error_mgr base;
    std::jmp_buf jump;
    char message[JMSG_LENGTH_MAX];
};

extern "C" void jpegErrorExit(j_common_ptr common) {
    JpegErrorManager* error = reinterpret_cast<JpegErrorManager*>(common->err);
    (*common->err->format_message)(common, error->message);
    std::longjmp(error->jump, 1);
}

extern "C" void jpegEmitMessage(j_common_ptr common, int messageLevel) {
    if (messageLevel < 0) jpegErrorExit(common);
}

[[noreturn]] void jpegFail(JpegErrorManager& error, const char* message) {
    std::strncpy(error.message, message, sizeof(error.message) - 1u);
    error.message[sizeof(error.message) - 1u] = '\0';
    std::longjmp(error.jump, 1);
}

bool validScale(const ImageDecodeRequest& request) {
    if (request.representation == ImageRepresentation::Region) return false;
    if (request.representation == ImageRepresentation::FullDetail) {
        return request.scaleDenominator == 1;
    }
    return request.scaleDenominator == 1 || request.scaleDenominator == 2 ||
           request.scaleDenominator == 4 || request.scaleDenominator == 8;
}

bool validSource(const PageData& page) {
    return !page.filesystemPath.empty() || !page.bytes.empty();
}

void setSource(jpeg_decompress_struct& decoder, const PageData& page, std::FILE* file) {
    if (file) {
        jpeg_stdio_src(&decoder, file);
    } else {
        // CBZ entries are already bounded by the reader budget. libjpeg's
        // in-memory API uses unsigned long even on 64-bit Windows.
        jpeg_mem_src(&decoder, page.bytes.data(),
                     static_cast<unsigned long>(page.bytes.size()));
    }
}

bool supportedColorSpace(const jpeg_decompress_struct& decoder) {
    return decoder.data_precision == 8 &&
           (decoder.jpeg_color_space == JCS_GRAYSCALE ||
            decoder.jpeg_color_space == JCS_RGB ||
            decoder.jpeg_color_space == JCS_YCbCr);
}

std::size_t coefficientEstimate(const jpeg_decompress_struct& decoder) {
    if (!decoder.progressive_mode || !decoder.comp_info) return 0;
    std::size_t total = 0;
    for (int index = 0; index < decoder.num_components; ++index) {
        const jpeg_component_info& component = decoder.comp_info[index];
        const std::size_t blocks = MemoryMath::saturatedMultiply(
            static_cast<std::size_t>(component.width_in_blocks),
            static_cast<std::size_t>(component.height_in_blocks));
        const std::size_t coefficients = MemoryMath::saturatedMultiply(blocks, DCTSIZE2);
        total = MemoryMath::saturatedAdd(total,
            MemoryMath::saturatedMultiply(coefficients, sizeof(JCOEF)));
    }
    return total;
}

void populateInfo(jpeg_decompress_struct& decoder, const PageData& page,
                  const ImageDecodeRequest& request, ImageInfo& info) {
    decoder.scale_num = 1;
    decoder.scale_denom = request.scaleDenominator;
    const bool grayscale = decoder.jpeg_color_space == JCS_GRAYSCALE;
    decoder.out_color_space = grayscale ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_calc_output_dimensions(&decoder);

    info = {};
    info.format = ImageFormat::Jpeg;
    info.representation = request.representation;
    info.sourceWidth = static_cast<std::uint32_t>(decoder.image_width);
    info.sourceHeight = static_cast<std::uint32_t>(decoder.image_height);
    info.outputWidth = static_cast<std::uint32_t>(decoder.output_width);
    info.outputHeight = static_cast<std::uint32_t>(decoder.output_height);
    info.components = decoder.num_components;
    info.grayscale = grayscale;
    info.progressive = decoder.progressive_mode != 0;
    info.scaleDenominator = request.scaleDenominator;
    info.encodedResidentBytes = page.bytes.size();
    info.finalRgb565Bytes = MemoryMath::rgb565Bytes(info.outputWidth, info.outputHeight);
    const std::size_t outputComponents = grayscale ? 1u : 3u;
    info.scanlineWorkspaceBytes = MemoryMath::saturatedMultiply(
        static_cast<std::size_t>(info.outputWidth), outputComponents);

    // libjpeg owns opaque MCU/controller allocations. This is a conservative estimate,
    // not a claimed allocator measurement. Progressive coefficient storage is included.
    const std::size_t rowAllowance = MemoryMath::saturatedMultiply(
        info.scanlineWorkspaceBytes, 16u);
    info.estimatedLibraryOverheadBytes = MemoryMath::saturatedAdd(
        128u * 1024u, MemoryMath::saturatedAdd(rowAllowance, coefficientEstimate(decoder)));
    info.trackedTransientBytes = MemoryMath::saturatedAdd(
        info.encodedResidentBytes,
        MemoryMath::saturatedAdd(info.finalRgb565Bytes, info.scanlineWorkspaceBytes));
    info.predictedTransientBytes = MemoryMath::saturatedAdd(
        info.trackedTransientBytes, info.estimatedLibraryOverheadBytes);
}

void configureError(jpeg_decompress_struct& decoder, JpegErrorManager& error) {
    decoder.err = jpeg_std_error(&error.base);
    error.base.error_exit = jpegErrorExit;
    error.base.emit_message = jpegEmitMessage;
    error.message[0] = '\0';
}

} // namespace

bool JpegDecoder::matches(const PageData& page) const {
    const std::string extension = PathUtils::extensionLower(
        page.logicalName.empty() ? page.filesystemPath : page.logicalName);
    if (extension == ".jpg" || extension == ".jpeg") return true;
    return page.bytes.size() >= 2 && page.bytes[0] == 0xff && page.bytes[1] == 0xd8;
}

bool JpegDecoder::probe(const PageData& page, const ImageDecodeRequest& request,
                        ImageInfo& info, std::string& error,
                        std::uint64_t* probeMicros) const {
    const std::uint64_t started = PerformanceClock::nowMicros();
    info = {};
    info.format = ImageFormat::Jpeg;
    error.clear();
    if (!validSource(page)) {
        error = "JPEG source is empty.";
        return false;
    }
    if (!validScale(request)) {
        error = request.representation == ImageRepresentation::Region
            ? "JPEG region decode is not implemented."
            : "Unsupported JPEG decode scale.";
        return false;
    }

    std::FILE* volatile file = nullptr;
    if (!page.filesystemPath.empty()) {
        file = std::fopen(page.filesystemPath.c_str(), "rb");
        if (!file) {
            error = "Could not open JPEG file.";
            return false;
        }
    }

    jpeg_decompress_struct decoder {};
    JpegErrorManager jpegError {};
    volatile bool created = false;
    configureError(decoder, jpegError);
    if (setjmp(jpegError.jump)) {
        if (created) jpeg_destroy_decompress(&decoder);
        if (file) std::fclose(file);
        error = jpegError.message[0] ? jpegError.message : "JPEG header probe failed.";
        if (probeMicros) *probeMicros = PerformanceClock::elapsedMicros(started);
        return false;
    }

    jpeg_create_decompress(&decoder);
    created = true;
    setSource(decoder, page, file);
    if (jpeg_read_header(&decoder, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&decoder);
        if (file) std::fclose(file);
        error = "JPEG header is incomplete.";
        if (probeMicros) *probeMicros = PerformanceClock::elapsedMicros(started);
        return false;
    }
    if (!supportedColorSpace(decoder)) {
        jpeg_destroy_decompress(&decoder);
        if (file) std::fclose(file);
        error = "JPEG precision or color space is unsupported.";
        if (probeMicros) *probeMicros = PerformanceClock::elapsedMicros(started);
        return false;
    }

    populateInfo(decoder, page, request, info);
    jpeg_destroy_decompress(&decoder);
    if (file) std::fclose(file);
    if (probeMicros) *probeMicros = PerformanceClock::elapsedMicros(started);
    return info.outputWidth > 0 && info.outputHeight > 0 &&
           info.finalRgb565Bytes != std::numeric_limits<std::size_t>::max();
}

std::uint16_t JpegDecoder::packRgb565(std::uint8_t red, std::uint8_t green,
                                      std::uint8_t blue) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(red >> 3) << 11) |
        (static_cast<std::uint16_t>(green >> 2) << 5) |
        static_cast<std::uint16_t>(blue >> 3));
}

ImageDecodeResult JpegDecoder::decodeRgb565(const PageData& page,
                                             const ImageDecodeRequest& request,
                                             const Rgb565Target& target) const {
    ImageDecodeResult result;
    const std::uint64_t totalStarted = PerformanceClock::nowMicros();
    if (!probe(page, request, result.info, result.error,
               &result.timing.headerProbeMicros)) {
        result.timing.totalMicros = PerformanceClock::elapsedMicros(totalStarted);
        return result;
    }
    if (!target.pixels || target.width != result.info.outputWidth ||
        target.height != result.info.outputHeight ||
        target.pitchBytes < MemoryMath::rgb565Pitch(target.width)) {
        result.error = "RGB565 target does not match probed JPEG dimensions.";
        result.timing.totalMicros = PerformanceClock::elapsedMicros(totalStarted);
        return result;
    }

    // libjpeg reports fatal decode errors through longjmp. Keep the owned file
    // pointer defined across that boundary so the error path can close it.
    std::FILE* volatile file = nullptr;
    if (!page.filesystemPath.empty()) {
        file = std::fopen(page.filesystemPath.c_str(), "rb");
        if (!file) {
            result.error = "Could not reopen JPEG file for decode.";
            result.timing.totalMicros = PerformanceClock::elapsedMicros(totalStarted);
            return result;
        }
    }

    jpeg_decompress_struct decoder {};
    JpegErrorManager jpegError {};
    volatile bool created = false;
    configureError(decoder, jpegError);
    if (setjmp(jpegError.jump)) {
        if (created) jpeg_destroy_decompress(&decoder);
        if (file) std::fclose(file);
        result.timing.jpegDecodeMicros = 0;
        result.timing.rgb565OutputMicros = 0;
        result.error = jpegError.message[0] ? jpegError.message : "JPEG decode failed.";
        result.timing.totalMicros = PerformanceClock::elapsedMicros(totalStarted);
        return result;
    }

    jpeg_create_decompress(&decoder);
    created = true;
    setSource(decoder, page, file);
    if (jpeg_read_header(&decoder, TRUE) != JPEG_HEADER_OK) {
        jpegFail(jpegError, "JPEG header changed or became incomplete before decode.");
    }
    if (!supportedColorSpace(decoder)) {
        jpegFail(jpegError, "JPEG precision or color space is unsupported.");
    }
    decoder.scale_num = 1;
    decoder.scale_denom = request.scaleDenominator;
    decoder.out_color_space = result.info.grayscale ? JCS_GRAYSCALE : JCS_RGB;
    if (request.maxLibraryMemoryBytes > 0) {
        decoder.mem->max_memory_to_use = static_cast<long>(std::min(
            request.maxLibraryMemoryBytes,
            static_cast<std::size_t>(std::numeric_limits<long>::max())));
    }

    const std::uint64_t decodeStarted = PerformanceClock::nowMicros();
    jpeg_start_decompress(&decoder);
    if (decoder.output_width != target.width || decoder.output_height != target.height) {
        jpegFail(jpegError, "JPEG output dimensions changed after probing.");
    }

    const std::size_t components = static_cast<std::size_t>(decoder.output_components);
    const std::size_t rowBytes = MemoryMath::saturatedMultiply(target.width, components);
    JSAMPARRAY scanlines = (*decoder.mem->alloc_sarray)(
        reinterpret_cast<j_common_ptr>(&decoder), JPOOL_IMAGE,
        static_cast<JDIMENSION>(rowBytes), 1);

    while (decoder.output_scanline < decoder.output_height) {
#ifndef NDEBUG
        const std::uint64_t readStarted = PerformanceClock::nowMicros();
#endif
        if (jpeg_read_scanlines(&decoder, scanlines, 1) != 1) {
            jpegFail(jpegError, "JPEG decoder returned no scanline.");
        }
#ifndef NDEBUG
        result.timing.jpegDecodeMicros += PerformanceClock::elapsedMicros(readStarted);
        const std::uint64_t convertStarted = PerformanceClock::nowMicros();
#endif
        const std::size_t outputRow = static_cast<std::size_t>(decoder.output_scanline - 1);
        std::uint16_t* destination = reinterpret_cast<std::uint16_t*>(
            static_cast<unsigned char*>(target.pixels) + outputRow * target.pitchBytes);
        if (result.info.grayscale) {
            for (std::size_t x = 0; x < target.width; ++x) {
                const std::uint8_t value = scanlines[0][x];
                destination[x] = packRgb565(value, value, value);
            }
        } else {
            for (std::size_t x = 0; x < target.width; ++x) {
                const std::size_t offset = x * 3u;
                destination[x] = packRgb565(
                    scanlines[0][offset], scanlines[0][offset + 1], scanlines[0][offset + 2]);
            }
        }
#ifndef NDEBUG
        result.timing.rgb565OutputMicros += PerformanceClock::elapsedMicros(convertStarted);
#endif
    }

    jpeg_finish_decompress(&decoder);
    jpeg_destroy_decompress(&decoder);
    if (file) std::fclose(file);
    result.success = true;
    const std::uint64_t combined = PerformanceClock::elapsedMicros(decodeStarted);
#ifdef NDEBUG
    result.timing.jpegDecodeMicros = combined;
#else
    const std::uint64_t measured = result.timing.jpegDecodeMicros +
                                   result.timing.rgb565OutputMicros;
    if (combined > measured) result.timing.jpegDecodeMicros += combined - measured;
#endif
    result.timing.totalMicros = PerformanceClock::elapsedMicros(totalStarted);
    return result;
}
