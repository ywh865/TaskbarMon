#include "stdafx.h"
#include "DrawCommonHelper.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>

namespace
{
    // These helpers only operate on UI-sized bitmaps.  Keep hostile or corrupt
    // HBITMAP metadata from causing an unbounded allocation.
    constexpr std::size_t MAX_BITMAP_PIXEL_BUFFER_BYTES = 64U * 1024U * 1024U;
    constexpr std::size_t MAX_BITMAP_ALPHA_POINTS = 1024U * 1024U;

    bool GetBitmapPixelBufferSize(HBITMAP hBitmap, int& width, int& height, std::size_t& pixel_count) noexcept
    {
        width = 0;
        height = 0;
        pixel_count = 0;

        if (hBitmap == NULL)
            return false;

        BITMAP bm{};
        if (GetObject(hBitmap, static_cast<int>(sizeof(bm)), &bm) != static_cast<int>(sizeof(bm))
            || bm.bmWidth <= 0 || bm.bmHeight <= 0)
        {
            return false;
        }

        const auto bitmap_width = static_cast<std::size_t>(bm.bmWidth);
        const auto bitmap_height = static_cast<std::size_t>(bm.bmHeight);
        if (bitmap_height > (std::numeric_limits<std::size_t>::max)() / bitmap_width)
            return false;

        const auto candidate_pixel_count = bitmap_width * bitmap_height;
        if (candidate_pixel_count > MAX_BITMAP_PIXEL_BUFFER_BYTES / sizeof(RGBQUAD))
            return false;

        width = bm.bmWidth;
        height = bm.bmHeight;
        pixel_count = candidate_pixel_count;
        return true;
    }

    bool TryScaleDimension(LONG source_dimension, LONG target_dimension, LONG divisor, LONG& scaled_dimension) noexcept
    {
        if (source_dimension <= 0 || target_dimension <= 0 || divisor <= 0)
            return false;

        // int × int fits in int64_t, then the result must still fit the Win32
        // coordinate type before it is stored in CSize.
        const std::int64_t scaled = static_cast<std::int64_t>(source_dimension)
            * static_cast<std::int64_t>(target_dimension) / divisor;
        if (scaled <= 0 || scaled > (std::numeric_limits<LONG>::max)())
            return false;

        scaled_dimension = static_cast<LONG>(scaled);
        return true;
    }

    void AddSaturatingOffset(LONG& coordinate, LONG offset) noexcept
    {
        const std::int64_t adjusted = static_cast<std::int64_t>(coordinate) + offset;
        if (adjusted < (std::numeric_limits<LONG>::min)())
            coordinate = (std::numeric_limits<LONG>::min)();
        else if (adjusted > (std::numeric_limits<LONG>::max)())
            coordinate = (std::numeric_limits<LONG>::max)();
        else
            coordinate = static_cast<LONG>(adjusted);
    }
}

UINT DrawCommonHelper::ProccessTextFormat(CRect rect, CSize text_length, IDrawCommon::Alignment align, bool multi_line) noexcept
{
    UINT result; // CDC::DrawText()函数的文本格式
    if (multi_line)
        result = DT_EDITCONTROL | DT_WORDBREAK | DT_NOPREFIX;
    else
        result = DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;

    if (text_length.cx > rect.Width()) //如果文本宽度超过了矩形区域的宽度，设置了居中时左对齐
    {
        if (align == IDrawCommon::Alignment::RIGHT)
            result |= DT_RIGHT;
    }
    else
    {
        switch (align)
        {
        case IDrawCommon::Alignment::RIGHT:
            result |= DT_RIGHT;
            break;
        case IDrawCommon::Alignment::CENTER:
            result |= DT_CENTER;
            break;
        }
    }
    return result;
}

void DrawCommonHelper::ImageDrawAreaConvert(CSize image_size, CPoint& start_point, CSize& size, IDrawCommon::StretchMode stretch_mode)
{
    // Do not divide by zero or pass invalid dimensions into GDI/GDI+. A zero
    // requested size keeps the historical "use image size" behavior; an
    // invalid image cannot be drawn safely.
    if (image_size.cx <= 0 || image_size.cy <= 0)
    {
        size = CSize(0, 0);
        return;
    }

    if (size.cx <= 0 || size.cy <= 0)       //如果指定的size为0，则使用位图的实际大小绘制
    {
        size = CSize(image_size.cx, image_size.cy);
    }
    else
    {
        if (stretch_mode == IDrawCommon::StretchMode::FILL)
        {
            const std::int64_t image_ratio_numerator = static_cast<std::int64_t>(image_size.cx) * size.cy;
            const std::int64_t draw_ratio_numerator = static_cast<std::int64_t>(size.cx) * image_size.cy;
            if (image_ratio_numerator > draw_ratio_numerator)     //如果图像的宽高比大于绘制区域的宽高比，则需要裁剪两边的图像
            {
                LONG image_width;        //按比例缩放后的宽度
                if (!TryScaleDimension(image_size.cx, size.cy, image_size.cy, image_width))
                {
                    size = image_size;
                    return;
                }
                AddSaturatingOffset(start_point.x, -((image_width - size.cx) / 2));
                size.cx = image_width;
            }
            else
            {
                LONG image_height;       //按比例缩放后的高度
                if (!TryScaleDimension(image_size.cy, size.cx, image_size.cx, image_height))
                {
                    size = image_size;
                    return;
                }
                AddSaturatingOffset(start_point.y, -((image_height - size.cy) / 2));
                size.cy = image_height;
            }
        }
        else if (stretch_mode == IDrawCommon::StretchMode::FIT)
        {
            CSize draw_size = image_size;
            const std::int64_t image_ratio_numerator = static_cast<std::int64_t>(image_size.cx) * size.cy;
            const std::int64_t draw_ratio_numerator = static_cast<std::int64_t>(size.cx) * image_size.cy;
            if (image_ratio_numerator > draw_ratio_numerator)     //如果图像的宽高比大于绘制区域的宽高比
            {
                if (!TryScaleDimension(draw_size.cy, size.cx, draw_size.cx, draw_size.cy))
                {
                    size = image_size;
                    return;
                }
                draw_size.cx = size.cx;
                AddSaturatingOffset(start_point.y, (size.cy - draw_size.cy) / 2);
            }
            else
            {
                if (!TryScaleDimension(draw_size.cx, size.cy, draw_size.cy, draw_size.cx))
                {
                    size = image_size;
                    return;
                }
                draw_size.cy = size.cy;
                AddSaturatingOffset(start_point.x, (size.cx - draw_size.cx) / 2);
            }
            size = draw_size;
        }
    }
}

void DrawCommonHelper::GetBitmapAlphaPixel(HBITMAP hBitmap, std::set<Point>& points)
{
    points.clear();
    int width{};
    int height{};
    std::size_t pixel_count{};
    if (!GetBitmapPixelBufferSize(hBitmap, width, height, pixel_count))
        return;

    // 获取位图的像素数据
    BITMAPINFO bmpInfo = { 0 };
    bmpInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmpInfo.bmiHeader.biWidth = width;
    bmpInfo.bmiHeader.biHeight = -height; // top-down DIB
    bmpInfo.bmiHeader.biPlanes = 1;
    bmpInfo.bmiHeader.biBitCount = 32;
    bmpInfo.bmiHeader.biCompression = BI_RGB;

    HDC hdc = CreateCompatibleDC(NULL);
    if (hdc == NULL)
        return;

    // 分配内存存储位图像素
    std::unique_ptr<RGBQUAD[]> pPixels(new (std::nothrow) RGBQUAD[pixel_count]);
    if (pPixels == nullptr)
    {
        DeleteDC(hdc);
        return;
    }

    if (GetDIBits(hdc, hBitmap, 0, height, pPixels.get(), &bmpInfo, DIB_RGB_COLORS) != height)
    {
        DeleteDC(hdc);
        return;
    }

    // 遍历所有像素点
    try
    {
        std::set<Point> transparent_points;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                    + static_cast<std::size_t>(x);
                //添加alpha值为0的像素点
                if (pPixels[index].rgbReserved == 0)
                {
                    if (transparent_points.size() >= MAX_BITMAP_ALPHA_POINTS)
                    {
                        DeleteDC(hdc);
                        return;
                    }
                    transparent_points.emplace(x, y);
                }
            }
        }

        points.swap(transparent_points);
    }
    catch (const std::bad_alloc&)
    {
        points.clear();
    }
    DeleteDC(hdc);
}

void DrawCommonHelper::FixBitmapTextAlpha(HBITMAP hBitmap, BYTE alpha, const std::set<Point>& alpha_points)
{
    int width{};
    int height{};
    std::size_t pixel_count{};
    if (!GetBitmapPixelBufferSize(hBitmap, width, height, pixel_count))
        return;

    // 获取位图的像素数据
    BITMAPINFO bmpInfo = { 0 };
    bmpInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmpInfo.bmiHeader.biWidth = width;
    bmpInfo.bmiHeader.biHeight = -height; // top-down DIB
    bmpInfo.bmiHeader.biPlanes = 1;
    bmpInfo.bmiHeader.biBitCount = 32;
    bmpInfo.bmiHeader.biCompression = BI_RGB;

    HDC hdc = CreateCompatibleDC(NULL);
    if (hdc == NULL)
        return;

    // 分配内存存储位图像素
    std::unique_ptr<RGBQUAD[]> pPixels(new (std::nothrow) RGBQUAD[pixel_count]);
    if (pPixels == nullptr)
    {
        DeleteDC(hdc);
        return;
    }

    if (GetDIBits(hdc, hBitmap, 0, height, pPixels.get(), &bmpInfo, DIB_RGB_COLORS) != height)
    {
        DeleteDC(hdc);
        return;
    }

    // 遍历所有像素
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                + static_cast<std::size_t>(x);
            //如果检测到alpha值为0，但是却不在alpha_points里，将其修正为正确的alpha值
            if (pPixels[index].rgbReserved == 0 && !alpha_points.contains(Point(x, y)))
                pPixels[index].rgbReserved = alpha; // 设置Alpha通道
        }
    }

    // 将修改后的像素数据写回位图
    if (SetDIBits(hdc, hBitmap, 0, height, pPixels.get(), &bmpInfo, DIB_RGB_COLORS) != height)
    {
        DeleteDC(hdc);
        return;
    }

    DeleteDC(hdc);
}
