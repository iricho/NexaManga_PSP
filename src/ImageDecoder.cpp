#include "ImageDecoder.hpp"

const char* imageFormatName(ImageFormat format) {
    switch (format) {
        case ImageFormat::Jpeg: return "JPEG";
        case ImageFormat::Png: return "PNG";
        default: return "unknown";
    }
}
