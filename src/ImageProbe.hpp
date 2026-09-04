#pragma once

#include "ImageDecoder.hpp"

namespace ImageProbe {

bool probe(const PageData& page, const ImageDecodeRequest& request,
           ImageInfo& info, std::string& error,
           std::uint64_t* probeMicros = nullptr);

} // namespace ImageProbe
