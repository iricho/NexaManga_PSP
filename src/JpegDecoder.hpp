#pragma once

#include "ImageDecoder.hpp"

class JpegDecoder final : public ImageDecoder {
public:
    ImageFormat format() const override { return ImageFormat::Jpeg; }
    bool matches(const PageData& page) const override;
    bool probe(const PageData& page, const ImageDecodeRequest& request,
               ImageInfo& info, std::string& error,
               std::uint64_t* probeMicros = nullptr) const override;
    ImageDecodeResult decodeRgb565(const PageData& page,
                                    const ImageDecodeRequest& request,
                                    const Rgb565Target& target) const override;

    static std::uint16_t packRgb565(std::uint8_t red, std::uint8_t green,
                                    std::uint8_t blue);
};
