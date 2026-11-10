/*
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIBPEBBLE3D_ICON_COLOR_H
#define LIBPEBBLE3D_ICON_COLOR_H

#include <stddef.h>
#include <stdint.h>

/*
 * Select a representative Pebble-palette colour from straight (unpremultiplied)
 * numeric ARGB32 pixels. Width and height must be at most 32; the caller must
 * provide sufficient row storage for stride_pixels pixels per row.
 * stride_pixels is measured in uint32_t pixels.
 * Returns RGB24, or -1 when no representative colour can be selected.
 */
static inline int32_t libpebble3d_icon_color(const uint32_t *pixels,
                                             int width, int height,
                                             int stride_pixels)
{
    struct libpebble3d_icon_color_bin {
        int count;
        int red;
        int green;
        int blue;
        int saturation;
    } bins[4096] = {};
    int eligible = 0;

    if (!pixels || width <= 0 || height <= 0 || width > 32 || height > 32
            || stride_pixels < width)
        return -1;

    for (int y = 0; y < height; ++y) {
        const uint32_t *line = pixels + (size_t)y * (size_t)stride_pixels;
        for (int x = 0; x < width; ++x) {
            const uint32_t p = line[x];
            if (((p >> 24) & 0xff) < 96)
                continue;
            const int r = (p >> 16) & 0xff;
            const int g = (p >> 8) & 0xff;
            const int b = p & 0xff;
            const int hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
            const int lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
            const int saturation = hi ? (255 * (hi - lo)) / hi : 0;
            const int luminance = (299 * r + 587 * g + 114 * b) / 1000;
            if (luminance < 32 || luminance > 238 || saturation < 48)
                continue;
            const int index = ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
            ++bins[index].count;
            bins[index].red += r;
            bins[index].green += g;
            bins[index].blue += b;
            bins[index].saturation += saturation;
            ++eligible;
        }
    }
    if (eligible < 3)
        return -1;

    int best = -1;
    for (int i = 0; i < 4096; ++i) {
        if (bins[i].count < 2)
            continue;
        if (best < 0 || bins[i].count > bins[best].count
                || (bins[i].count == bins[best].count
                    && bins[i].saturation / bins[i].count
                       > bins[best].saturation / bins[best].count))
            best = i;
    }
    if (best < 0 || bins[best].count * 100 < eligible * 4)
        return -1;

    const int r = bins[best].red / bins[best].count;
    const int g = bins[best].green / bins[best].count;
    const int b = bins[best].blue / bins[best].count;
    const int pr = ((r + 42) / 85) * 85;
    const int pg = ((g + 42) / 85) * 85;
    const int pb = ((b + 42) / 85) * 85;
    const int cr = pr < 0 ? 0 : (pr > 255 ? 255 : pr);
    const int cg = pg < 0 ? 0 : (pg > 255 ? 255 : pg);
    const int cb = pb < 0 ? 0 : (pb > 255 ? 255 : pb);
    return (cr << 16) | (cg << 8) | cb;
}

#endif
