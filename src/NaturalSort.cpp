#include "NaturalSort.hpp"

namespace {

unsigned char asciiLower(unsigned char value) {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<unsigned char>(value + ('a' - 'A'));
    }
    return value;
}

bool isAsciiDigit(unsigned char value) {
    return value >= '0' && value <= '9';
}

} // namespace

namespace NaturalSort {

int compare(const std::string& left, const std::string& right) {
    std::size_t i = 0;
    std::size_t j = 0;

    while (i < left.size() && j < right.size()) {
        const unsigned char a = static_cast<unsigned char>(left[i]);
        const unsigned char b = static_cast<unsigned char>(right[j]);

        if (isAsciiDigit(a) && isAsciiDigit(b)) {
            const std::size_t rawStartA = i;
            const std::size_t rawStartB = j;

            while (i < left.size() && left[i] == '0') ++i;
            while (j < right.size() && right[j] == '0') ++j;

            std::size_t significantEndA = i;
            std::size_t significantEndB = j;
            while (significantEndA < left.size() &&
                   isAsciiDigit(static_cast<unsigned char>(left[significantEndA]))) {
                ++significantEndA;
            }
            while (significantEndB < right.size() &&
                   isAsciiDigit(static_cast<unsigned char>(right[significantEndB]))) {
                ++significantEndB;
            }

            const std::size_t significantLengthA = significantEndA - i;
            const std::size_t significantLengthB = significantEndB - j;
            if (significantLengthA != significantLengthB) {
                return significantLengthA < significantLengthB ? -1 : 1;
            }

            for (std::size_t offset = 0; offset < significantLengthA; ++offset) {
                if (left[i + offset] != right[j + offset]) {
                    return left[i + offset] < right[j + offset] ? -1 : 1;
                }
            }

            std::size_t rawEndA = significantEndA;
            std::size_t rawEndB = significantEndB;
            while (rawEndA < left.size() &&
                   isAsciiDigit(static_cast<unsigned char>(left[rawEndA]))) {
                ++rawEndA;
            }
            while (rawEndB < right.size() &&
                   isAsciiDigit(static_cast<unsigned char>(right[rawEndB]))) {
                ++rawEndB;
            }

            const std::size_t rawLengthA = rawEndA - rawStartA;
            const std::size_t rawLengthB = rawEndB - rawStartB;
            if (rawLengthA != rawLengthB) return rawLengthA < rawLengthB ? -1 : 1;

            i = rawEndA;
            j = rawEndB;
            continue;
        }

        const unsigned char foldedA = asciiLower(a);
        const unsigned char foldedB = asciiLower(b);
        if (foldedA != foldedB) return foldedA < foldedB ? -1 : 1;

        ++i;
        ++j;
    }

    if (i != left.size() || j != right.size()) return i == left.size() ? -1 : 1;
    if (left == right) return 0;
    return left < right ? -1 : 1;
}

} // namespace NaturalSort
