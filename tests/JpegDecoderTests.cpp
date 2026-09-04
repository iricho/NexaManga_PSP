#include "ImageProbe.hpp"
#include "JpegDecoder.hpp"
#include "MemoryMath.hpp"
#include "PageCache.hpp"
#include "PageSource.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <jpeglib.h>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::vector<std::uint8_t> makeJpeg(std::size_t width, std::size_t height,
                                   bool grayscale, bool progressive = false) {
    jpeg_compress_struct compressor {};
    jpeg_error_mgr error {};
    compressor.err = jpeg_std_error(&error);
    jpeg_create_compress(&compressor);
    unsigned char* output = nullptr;
    unsigned long outputSize = 0;
    jpeg_mem_dest(&compressor, &output, &outputSize);
    compressor.image_width = static_cast<JDIMENSION>(width);
    compressor.image_height = static_cast<JDIMENSION>(height);
    compressor.input_components = grayscale ? 1 : 3;
    compressor.in_color_space = grayscale ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_set_defaults(&compressor);
    jpeg_set_quality(&compressor, 96, TRUE);
    if (progressive) jpeg_simple_progression(&compressor);
    jpeg_start_compress(&compressor, TRUE);

    std::vector<JSAMPLE> row(width * static_cast<std::size_t>(compressor.input_components));
    while (compressor.next_scanline < compressor.image_height) {
        const std::size_t y = compressor.next_scanline;
        for (std::size_t x = 0; x < width; ++x) {
            if (grayscale) {
                row[x] = static_cast<JSAMPLE>((x * 17u + y * 11u + 37u) & 0xffu);
            } else {
                row[x * 3u] = static_cast<JSAMPLE>((x * 31u + 23u) & 0xffu);
                row[x * 3u + 1u] = static_cast<JSAMPLE>((y * 29u + 61u) & 0xffu);
                row[x * 3u + 2u] = static_cast<JSAMPLE>(((x + y) * 13u + 97u) & 0xffu);
            }
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

PageData memoryPage(std::vector<std::uint8_t> bytes, const std::string& name = "page.jpg") {
    PageData page;
    page.logicalName = name;
    page.identifier = name;
    page.bytes = std::move(bytes);
    return page;
}

struct DecodedPage {
    ImageInfo info;
    std::vector<std::uint8_t> pixels;
};

std::shared_ptr<DecodedPage> decode(const PageData& page,
                                    ImageDecodeRequest request = {}) {
    JpegDecoder decoder;
    ImageInfo info;
    std::string error;
    if (!decoder.probe(page, request, info, error)) return {};
    auto decoded = std::make_shared<DecodedPage>();
    decoded->info = info;
    const std::size_t pitch = MemoryMath::rgb565Pitch(info.outputWidth);
    decoded->pixels.assign(MemoryMath::saturatedMultiply(pitch, info.outputHeight), 0xcd);
    const ImageDecodeResult result = decoder.decodeRgb565(page, request, {
        decoded->pixels.data(), pitch, info.outputWidth, info.outputHeight
    });
    return result.success ? decoded : std::shared_ptr<DecodedPage>();
}

void write16(std::ofstream& output, std::uint16_t value) {
    output.put(static_cast<char>(value & 0xff));
    output.put(static_cast<char>((value >> 8) & 0xff));
}

void write32(std::ofstream& output, std::uint32_t value) {
    write16(output, static_cast<std::uint16_t>(value & 0xffff));
    write16(output, static_cast<std::uint16_t>((value >> 16) & 0xffff));
}

std::uint32_t crc32(const std::vector<std::uint8_t>& data) {
    std::uint32_t crc = 0xffffffffu;
    for (const std::uint8_t byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

void writeStoredZip(const fs::path& path, const std::string& name,
                    const std::vector<std::uint8_t>& data) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    const std::uint32_t checksum = crc32(data);
    write32(output, 0x04034b50u);
    write16(output, 20); write16(output, 0); write16(output, 0);
    write16(output, 0); write16(output, 0); write32(output, checksum);
    write32(output, static_cast<std::uint32_t>(data.size()));
    write32(output, static_cast<std::uint32_t>(data.size()));
    write16(output, static_cast<std::uint16_t>(name.size())); write16(output, 0);
    output.write(name.data(), static_cast<std::streamsize>(name.size()));
    output.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
    const std::uint32_t centralOffset = static_cast<std::uint32_t>(output.tellp());
    write32(output, 0x02014b50u);
    write16(output, 20); write16(output, 20); write16(output, 0); write16(output, 0);
    write16(output, 0); write16(output, 0); write32(output, checksum);
    write32(output, static_cast<std::uint32_t>(data.size()));
    write32(output, static_cast<std::uint32_t>(data.size()));
    write16(output, static_cast<std::uint16_t>(name.size()));
    write16(output, 0); write16(output, 0); write16(output, 0); write16(output, 0);
    write32(output, 0); write32(output, 0);
    output.write(name.data(), static_cast<std::streamsize>(name.size()));
    const std::uint32_t centralSize = static_cast<std::uint32_t>(output.tellp()) - centralOffset;
    write32(output, 0x06054b50u);
    write16(output, 0); write16(output, 0); write16(output, 1); write16(output, 1);
    write32(output, centralSize); write32(output, centralOffset); write16(output, 0);
}

void testProbeAndDecode() {
    JpegDecoder decoder;
    ImageDecodeRequest request;
    ImageInfo info;
    std::string error;
    const PageData grayscale = memoryPage(makeJpeg(13, 7, true), "gray.jpg");
    expect(decoder.probe(grayscale, request, info, error), "grayscale JPEG header probes");
    expect(info.sourceWidth == 13 && info.sourceHeight == 7 && info.grayscale &&
           info.components == 1, "probe exposes grayscale dimensions and components");
    expect(info.finalRgb565Bytes == MemoryMath::rgb565Bytes(13, 7) &&
           info.scanlineWorkspaceBytes == 13, "grayscale memory estimate is row-accurate");
    const std::shared_ptr<DecodedPage> gray = decode(grayscale);
    expect(gray && gray->pixels.size() == MemoryMath::rgb565Bytes(13, 7),
           "odd-width grayscale JPEG decodes directly to aligned RGB565");
    if (gray) {
        const std::size_t pitch = MemoryMath::rgb565Pitch(13);
        expect(gray->pixels[pitch - 1] == 0xcd, "odd-width destination padding is untouched");
    }

    const PageData rgb = memoryPage(makeJpeg(17, 9, false), "rgb.jpeg");
    expect(decoder.probe(rgb, request, info, error) && !info.grayscale && info.components == 3,
           "RGB JPEG probe exposes three components");
    expect(decode(rgb) != nullptr, "RGB JPEG scanlines decode to RGB565");
    expect(JpegDecoder::packRgb565(255, 0, 0) == 0xf800 &&
           JpegDecoder::packRgb565(0, 255, 0) == 0x07e0 &&
           JpegDecoder::packRgb565(0, 0, 255) == 0x001f &&
           JpegDecoder::packRgb565(255, 255, 255) == 0xffff,
           "RGB565 bit conversion is exact");

    request.representation = ImageRepresentation::ScaledPreview;
    request.scaleDenominator = 2;
    expect(decoder.probe(rgb, request, info, error) && info.outputWidth == 9 &&
           info.outputHeight == 5 && info.sourceWidth == 17,
           "native 1/2 DCT preview keeps authoritative source dimensions separate");
    expect(decode(rgb, request) != nullptr, "scaled preview decodes through the same interface");
    for (const unsigned int denominator : {4u, 8u}) {
        request.scaleDenominator = denominator;
        expect(decoder.probe(rgb, request, info, error) && decode(rgb, request) != nullptr,
               "native reduced DCT preview probes and decodes");
    }
    request.representation = ImageRepresentation::Region;
    expect(!decoder.probe(rgb, request, info, error), "future region representation fails explicitly");
}

void testLargeMetadataAndFailures() {
    JpegDecoder decoder;
    ImageDecodeRequest request;
    ImageInfo info;
    std::string error;
    const PageData large = memoryPage(makeJpeg(2000, 3000, false, true), "large.jpg");
    expect(decoder.probe(large, request, info, error) && info.sourceWidth == 2000 &&
           info.sourceHeight == 3000 && info.progressive,
           "large progressive JPEG metadata probes without a full pixel allocation");
    expect(info.finalRgb565Bytes == 12000000u &&
           info.predictedTransientBytes > info.trackedTransientBytes,
           "large-page RGB565 and opaque-library estimates are separated");

    const PageData invalid = memoryPage({1, 2, 3, 4, 5}, "invalid.jpg");
    expect(!decoder.probe(invalid, request, info, error) && !error.empty(),
           "invalid JPEG is rejected without fatal process exit");
    std::vector<std::uint8_t> corruptBytes = makeJpeg(32, 24, false);
    for (std::size_t index = 0; index + 1 < corruptBytes.size(); ++index) {
        if (corruptBytes[index] == 0xffu && corruptBytes[index + 1] == 0xc0u) {
            corruptBytes[index + 1] = 0x00u;
            break;
        }
    }
    const PageData corrupt = memoryPage(corruptBytes, "corrupt.jpg");
    expect(!decoder.probe(corrupt, request, info, error), "corrupt JPEG is contained");
    std::vector<std::uint8_t> truncatedBytes = makeJpeg(64, 48, false);
    truncatedBytes.resize(truncatedBytes.size() / 2u);
    const PageData truncated = memoryPage(truncatedBytes, "truncated.jpg");
    expect(!decode(truncated), "truncated JPEG warning is treated as a safe failure");
    expect(decode(memoryPage(makeJpeg(3, 2, true), "after-failure.jpg")) != nullptr,
           "decoder cleanup permits a successful decode after failures");
}

void testFolderAndCbz(const fs::path& root) {
    const std::vector<std::uint8_t> jpeg = makeJpeg(19, 11, false);
    const fs::path folder = root / "folder";
    fs::create_directories(folder);
    const fs::path file = folder / "001.jpg";
    { std::ofstream output(file, std::ios::binary); output.write(
        reinterpret_cast<const char*>(jpeg.data()), static_cast<std::streamsize>(jpeg.size())); }
    PageData folderPage;
    folderPage.logicalName = "001.jpg";
    folderPage.identifier = file.generic_string();
    folderPage.filesystemPath = file.generic_string();
    expect(folderPage.bytes.empty() && decode(folderPage) != nullptr,
           "folder JPEG streams from FILE without an encoded-byte vector");

    const fs::path archive = root / "pages.cbz";
    writeStoredZip(archive, "nested/001.jpg", jpeg);
    const Chapter chapter = CbzPageSource::inspect(archive.generic_string());
    CbzPageSource source(chapter);
    PageData cbzPage;
    expect(source.available() && source.readPage(0, cbzPage) &&
           cbzPage.filesystemPath.empty() && cbzPage.bytes == jpeg,
           "CBZ source returns only the selected compressed JPEG bytes");
    expect(decode(cbzPage) != nullptr, "CBZ JPEG decodes directly from memory");
}

void testBudgetAndTransactionalCache() {
    const PageData first = memoryPage(makeJpeg(31, 21, false), "first.jpg");
    const PageData second = memoryPage(makeJpeg(33, 23, true), "second.jpg");
    const std::shared_ptr<DecodedPage> current = decode(first);
    const std::shared_ptr<DecodedPage> neighbor = decode(second);
    expect(current && neighbor, "cache fixtures decode through direct JPEG path");
    if (!current || !neighbor) return;

    const std::size_t fixed = 1000;
    const std::size_t firstBytes = current->pixels.size();
    const std::size_t secondBytes = neighbor->pixels.size();
    PageCache<DecodedPage> cache(fixed + firstBytes + secondBytes, fixed);
    expect(cache.insert(1, current, firstBytes, static_cast<std::size_t>(-1), 4, false),
           "direct-decoded current page enters bounded cache");
    expect(cache.insert(2, neighbor, secondBytes, 1, 4, true),
           "direct-decoded N+1 preload enters cache when feasible");

    std::shared_ptr<DecodedPage> displayed = cache.peek(1);
    const PageData bad = memoryPage({0xff, 0xd8, 0x00}, "bad-next.jpg");
    const std::shared_ptr<DecodedPage> replacement = decode(bad);
    if (replacement) displayed = replacement;
    expect(displayed == current && cache.peek(1) == current,
           "failed transactional replacement leaves current page and cache valid");

    ImageInfo oversized;
    std::string error;
    const PageData large = memoryPage(makeJpeg(2000, 3000, false), "oversized.jpg");
    expect(ImageProbe::probe(large, {}, oversized, error), "oversized candidate probes first");
    const std::size_t available = cache.budgetBytes() - cache.totalTrackedBytes();
    expect(oversized.predictedTransientBytes > available,
           "oversized preload is refused by estimate before decode allocation");
    expect(!cache.insert(0, neighbor, cache.budgetBytes(), 1, 4, true) &&
           cache.peek(1) == current, "cache refusal preserves stable current page");
}

} // namespace

int main() {
    const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
        ("mangapsp_jpeg_" + std::to_string(nonce));
    fs::create_directories(root);

    testProbeAndDecode();
    testLargeMetadataAndFailures();
    testFolderAndCbz(root);
    testBudgetAndTransactionalCache();

    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    expect(!cleanupError, "JPEG fixture cleanup");
    if (failures) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All direct JPEG decoder tests passed\n";
    return 0;
}
