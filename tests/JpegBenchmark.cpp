#include "JpegDecoder.hpp"
#include "MemoryMath.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <vector>

#include <jpeglib.h>

namespace {

std::vector<std::uint8_t> makeJpeg(std::size_t width, std::size_t height) {
    jpeg_compress_struct compressor {};
    jpeg_error_mgr error {};
    compressor.err = jpeg_std_error(&error);
    jpeg_create_compress(&compressor);
    unsigned char* output = nullptr;
    unsigned long outputSize = 0;
    jpeg_mem_dest(&compressor, &output, &outputSize);
    compressor.image_width = static_cast<JDIMENSION>(width);
    compressor.image_height = static_cast<JDIMENSION>(height);
    compressor.input_components = 3;
    compressor.in_color_space = JCS_RGB;
    jpeg_set_defaults(&compressor);
    jpeg_set_quality(&compressor, 90, TRUE);
    jpeg_start_compress(&compressor, TRUE);
    std::vector<JSAMPLE> row(width * 3u);
    while (compressor.next_scanline < compressor.image_height) {
        const std::size_t y = compressor.next_scanline;
        for (std::size_t x = 0; x < width; ++x) {
            row[x * 3u] = static_cast<JSAMPLE>((x + y) & 0xffu);
            row[x * 3u + 1u] = static_cast<JSAMPLE>((x / 3u + y / 2u) & 0xffu);
            row[x * 3u + 2u] = static_cast<JSAMPLE>((x / 7u + y) & 0xffu);
        }
        JSAMPROW rows[1] = {row.data()};
        jpeg_write_scanlines(&compressor, rows, 1);
    }
    jpeg_finish_compress(&compressor);
    std::vector<std::uint8_t> bytes(output, output + outputSize);
    std::free(output);
    jpeg_destroy_compress(&compressor);
    return bytes;
}

void run(std::size_t width, std::size_t height) {
    PageData page;
    page.logicalName = "benchmark.jpg";
    page.identifier = page.logicalName;
    page.bytes = makeJpeg(width, height);
    JpegDecoder decoder;
    ImageInfo info;
    std::string error;
    if (!decoder.probe(page, {}, info, error)) {
        std::cerr << width << 'x' << height << " probe failed: " << error << '\n';
        return;
    }
    const std::size_t pitch = MemoryMath::rgb565Pitch(width);
    std::vector<std::uint8_t> pixels(MemoryMath::rgb565Bytes(width, height));
    const auto started = std::chrono::steady_clock::now();
    const ImageDecodeResult result = decoder.decodeRgb565(page, {}, {
        pixels.data(), pitch, static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height)
    });
    const double milliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    if (!result.success) {
        std::cerr << width << 'x' << height << " decode failed: " << result.error << '\n';
        return;
    }
    const std::size_t oldRgb24Plus565 = width * height * 3u + info.finalRgb565Bytes;
    std::cout << width << 'x' << height
              << " encoded_KiB=" << page.bytes.size() / 1024u
              << " final_MiB=" << std::fixed << std::setprecision(2)
              << static_cast<double>(info.finalRgb565Bytes) / (1024.0 * 1024.0)
              << " direct_tracked_MiB="
              << static_cast<double>(info.trackedTransientBytes) / (1024.0 * 1024.0)
              << " direct_predicted_MiB="
              << static_cast<double>(info.predictedTransientBytes) / (1024.0 * 1024.0)
              << " old_RGB24_plus_565_MiB="
              << static_cast<double>(oldRgb24Plus565 + page.bytes.size()) / (1024.0 * 1024.0)
              << " decode_ms=" << milliseconds << '\n';
}

} // namespace

int main() {
    std::cout << "NexaManga PSP direct JPEG development benchmark\n";
    std::cout << "Old column is a byte-model comparison, not measured SDL_image RSS.\n";
    run(1200, 1800);
    run(1600, 2400);
    run(2000, 3000);
    return 0;
}
