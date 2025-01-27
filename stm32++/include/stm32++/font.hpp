#ifndef FONT_HPP
#define FONT_HPP
#include <stdint.h>

struct Font
{
    const uint8_t width;
    const uint8_t height;
    const uint8_t count;
    const uint8_t* widths;
    const uint8_t* data;
    Font(uint8_t aWidth, uint8_t aHeight, uint8_t aCount, const uint8_t* aWidths, const void* aData)
    :width(aWidth), height(aHeight), count(aCount), widths(aWidths), data((uint8_t*)aData)
    {}
    bool isMono() const { return widths == nullptr; }
    const uint8_t* getCharData(uint8_t pos) const
    {
        if (!widths)
            return nullptr;
        uint32_t ofs = 0;
        for (int ch = 0; ch < pos; ch++)
            ofs+=widths[ch];
        return data+ofs;
    }
};

#endif // FONT_HPP
