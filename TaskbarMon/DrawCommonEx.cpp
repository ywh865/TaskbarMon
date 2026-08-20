#include "stdafx.h"
#include "DrawCommonEx.h"
#include "DrawCommonHelper.h"

#include <cmath>
#include <limits>

namespace
{
    int RoundTextExtentToInt(Gdiplus::REAL value) noexcept
    {
        // GDI+ reports non-negative extents.  Retain the prior nearest-pixel
        // rounding while preventing an out-of-range float-to-int conversion.
        if (!(value > 0.0F))
            return 0;

        const auto maximum = static_cast<Gdiplus::REAL>((std::numeric_limits<int>::max)());
        if (value >= maximum - 0.5F)
            return (std::numeric_limits<int>::max)();

        return static_cast<int>(value + 0.5F);
    }

    int TruncateRealToInt(Gdiplus::REAL value) noexcept
    {
        // CRect previously received the normal C++ truncation-toward-zero
        // conversion.  Preserve that geometry behavior and make it bounded.
        if (std::isnan(value))
            return 0;

        const auto minimum = static_cast<Gdiplus::REAL>((std::numeric_limits<int>::min)());
        const auto maximum = static_cast<Gdiplus::REAL>((std::numeric_limits<int>::max)());
        if (value <= minimum)
            return (std::numeric_limits<int>::min)();
        if (value >= maximum)
            return (std::numeric_limits<int>::max)();

        return static_cast<int>(value);
    }
}

CDrawCommonEx::CDrawCommonEx(CDC* pDC)
{
    Create(pDC);
}

CDrawCommonEx::CDrawCommonEx()
{
}


CDrawCommonEx::~CDrawCommonEx()
{
    SAFE_DELETE(m_pGraphics);
}

void CDrawCommonEx::Create(CDC* pDC)
{
    ASSERT(pDC != nullptr);
    m_pDC = pDC;
    SAFE_DELETE(m_pGraphics);
    if (pDC == nullptr || pDC->GetSafeHdc() == NULL)
        return;
    m_pGraphics = new Gdiplus::Graphics(pDC->GetSafeHdc());
}

void CDrawCommonEx::SetFont(CFont * pFont)
{
    //将字体设置到CDC，绘图时从CDC创建GDI+字体
    if (m_pDC != nullptr && m_pDC->GetSafeHdc() != NULL && pFont != nullptr)
        m_pDC->SelectObject(pFont);
}

void CDrawCommonEx::DrawImage(Gdiplus::Image* pImage, CPoint start_point, CSize size, StretchMode stretch_mode)
{
    if (m_pGraphics == nullptr || pImage == nullptr || size.cx < 0 || size.cy < 0
        || pImage->GetWidth() > static_cast<UINT>((std::numeric_limits<int>::max)())
        || pImage->GetHeight() > static_cast<UINT>((std::numeric_limits<int>::max)()))
        return;

    m_pGraphics->SetInterpolationMode(Gdiplus::InterpolationMode::InterpolationModeHighQuality);
    DrawCommonHelper::ImageDrawAreaConvert(CSize(pImage->GetWidth(), pImage->GetHeight()), start_point, size, stretch_mode);
    m_pGraphics->DrawImage(pImage, INT(start_point.x), INT(start_point.y), INT(size.cx), INT(size.cy));
}

void CDrawCommonEx::SetBackColor(COLORREF back_color, BYTE alpha)
{
    m_back_color = CGdiPlusHelper::COLORREFToGdiplusColor(back_color, alpha);
}

void CDrawCommonEx::DrawWindowText(CRect rect, LPCTSTR lpszString, COLORREF color, Alignment align, bool draw_back_ground, bool multi_line, BYTE alpha)
{
    if (m_pGraphics == nullptr || m_pDC == nullptr || m_pDC->GetSafeHdc() == NULL || lpszString == nullptr)
        return;

    //矩形区域
    Gdiplus::RectF rect_gdiplus = CGdiPlusHelper::CRectToGdiplusRect(rect);

    //绘制背景
    if (draw_back_ground)
    {
        Gdiplus::SolidBrush brush(m_back_color);
        m_pGraphics->FillRectangle(&brush, rect_gdiplus);
    }
    //设置字体
    Gdiplus::Font font(m_pDC->GetSafeHdc());
    if (font.GetLastStatus() != Gdiplus::Ok)
        return;
    //设置文本颜色
    Gdiplus::SolidBrush brush(CGdiPlusHelper::COLORREFToGdiplusColor(color, alpha));
    //设置对齐方式
    Gdiplus::StringFormat format;
    Gdiplus::StringAlignment alignment = Gdiplus::StringAlignmentNear;
    if (align == Alignment::CENTER)
        alignment = Gdiplus::StringAlignmentCenter;
    else if (align == Alignment::RIGHT)
        alignment = Gdiplus::StringAlignmentFar;
    format.SetAlignment(alignment);    //水平对齐方式
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);    //垂直对齐方式
    UINT flags = Gdiplus::StringFormatFlagsNoFitBlackBox;
    if (!multi_line)
        flags |= Gdiplus::StringFormatFlagsNoWrap;      //不自动换行
    format.SetTrimming(Gdiplus::StringTrimmingNone);    //禁止文本截断
    format.SetFormatFlags(flags);

    //绘制文本
    m_pGraphics->DrawString(lpszString, -1, &font, rect_gdiplus, &format, &brush);
}

void CDrawCommonEx::SetDrawRect(CRect rect)
{
    if (m_pGraphics != nullptr)
        m_pGraphics->SetClip(CGdiPlusHelper::CRectToGdiplusRect(rect));
}

void CDrawCommonEx::FillRect(CRect rect, COLORREF color, BYTE alpha)
{
    if (m_pGraphics == nullptr)
        return;

    Gdiplus::RectF rect_gdiplus = CGdiPlusHelper::CRectToGdiplusRect(rect);
    Gdiplus::SolidBrush brush(CGdiPlusHelper::COLORREFToGdiplusColor(color, alpha));
    m_pGraphics->FillRectangle(&brush, rect_gdiplus);
}

static void CreateRoundRectPath(Gdiplus::GraphicsPath* pPath, const Gdiplus::RectF& rect, float radius)
{
    // 限制半径不超过矩形尺寸的一半
    float half_w = rect.Width / 2.0f;
    float half_h = rect.Height / 2.0f;
    if (radius > half_w) radius = half_w;
    if (radius > half_h) radius = half_h;

    // 如果半径为零或负值，直接添加普通矩形
    if (radius < 0.1f)
    {
        pPath->AddRectangle(rect);
        return;
    }

    float x = rect.X;
    float y = rect.Y;
    float width = rect.Width;
    float height = rect.Height;
    
    pPath->AddArc(x, y, radius * 2, radius * 2, 180, 90); // 左上角
    pPath->AddArc(x + width - radius * 2, y, radius * 2, radius * 2, 270, 90); // 右上角
    pPath->AddArc(x + width - radius * 2, y + height - radius * 2, radius * 2, radius * 2, 0, 90); // 右下角
    pPath->AddArc(x, y + height - radius * 2, radius * 2, radius * 2, 90, 90); // 左下角
    pPath->CloseFigure();

}

void CDrawCommonEx::DrawRectOutLine(CRect rect, COLORREF color, int width, bool dot_line, BYTE alpha, int radius)
{
    if (m_pGraphics == nullptr || width <= 0)
        return;

    // 转换为 GDI+ 矩形
    Gdiplus::RectF rect_gdiplus = CGdiPlusHelper::CRectToGdiplusRect(rect);

    // 创建画笔
    Gdiplus::Color gdi_color = CGdiPlusHelper::COLORREFToGdiplusColor(color, alpha);
    Gdiplus::Pen pen(gdi_color, (Gdiplus::REAL)width);

    // 设置虚线样式
    if (dot_line)
    {
        // 使用点划线
        pen.SetDashStyle(Gdiplus::DashStyleDash);
    }

    // 绘制
    auto gdiplus_states = m_pGraphics->Save();
    if (radius > 0)
    {
        // 使用路径绘制圆角矩形
        Gdiplus::GraphicsPath path;
        rect_gdiplus.Width -= width;
        rect_gdiplus.Height -= width;
        CreateRoundRectPath(&path, rect_gdiplus, (float)radius);
        m_pGraphics->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias); // 启用抗锯齿
        m_pGraphics->DrawPath(&pen, &path);
    }
    else
    {
        // 普通矩形
        m_pGraphics->DrawRectangle(&pen, rect_gdiplus);
    }
    m_pGraphics->Restore(gdiplus_states);
}

void CDrawCommonEx::DrawLine(CPoint start_point, CPoint end_point, COLORREF color, BYTE alpha)
{
    if (m_pDC == nullptr || m_pDC->GetSafeHdc() == NULL)
        return;

    //使用GDI画线
    CPen aPen, * pOldPen;
    if (!aPen.CreatePen(PS_SOLID, 1, color))
        return;
    pOldPen = m_pDC->SelectObject(&aPen);
    if (pOldPen == nullptr)
    {
        aPen.DeleteObject();
        return;
    }

    m_pDC->MoveTo(start_point);
    m_pDC->LineTo(end_point);
    m_pDC->SelectObject(pOldPen);
    aPen.DeleteObject();
}

void CDrawCommonEx::SetTextColor(const COLORREF color, BYTE alpha)
{
    m_text_color = CGdiPlusHelper::COLORREFToGdiplusColor(color, alpha);
}

CDC* CDrawCommonEx::GetDC()
{
    return m_pDC;
}

int CDrawCommonEx::GetTextWidth(LPCTSTR lpszString)
{
    int w{}, h{};
    GetTextExtent(lpszString, w, h);
    return w;
}

void CDrawCommonEx::GetTextExtent(const wchar_t* lpszString, int& w, int& h)
{
    w = 0;
    h = 0;
    if (m_pGraphics == nullptr || m_pDC == nullptr || m_pDC->GetSafeHdc() == NULL || lpszString == nullptr)
        return;

    Gdiplus::Font font(m_pDC->GetSafeHdc());
    if (font.GetLastStatus() != Gdiplus::Ok)
        return;
    Gdiplus::RectF textSize;
    m_pGraphics->MeasureString(lpszString, -1, &font, Gdiplus::PointF(0, 0), &textSize);
    w = RoundTextExtentToInt(textSize.Width);
    h = RoundTextExtentToInt(textSize.Height);
}

void CDrawCommonEx::DrawBitmap(HBITMAP hbitmap, CPoint start_point, CSize size, StretchMode stretch_mode, BYTE alpha)
{
    if (m_pGraphics == nullptr || hbitmap == NULL || size.cx < 0 || size.cy < 0)
        return;

    Gdiplus::Bitmap bm(hbitmap, NULL);
    if (bm.GetLastStatus() != Gdiplus::Ok)
        return;
    if (bm.GetWidth() > static_cast<UINT>((std::numeric_limits<int>::max)())
        || bm.GetHeight() > static_cast<UINT>((std::numeric_limits<int>::max)()))
        return;

    m_pGraphics->SetInterpolationMode(Gdiplus::InterpolationMode::InterpolationModeHighQuality);
    DrawCommonHelper::ImageDrawAreaConvert(CSize(bm.GetWidth(), bm.GetHeight()), start_point, size, stretch_mode);
    m_pGraphics->DrawImage(&bm, INT(start_point.x), INT(start_point.y), INT(size.cx), INT(size.cy));
}

void CDrawCommonEx::DrawIcon(HICON hIcon, CPoint start_point, CSize size)
{
    //直接使用GDI的方式绘制图标
    if (m_pDC == nullptr || m_pDC->GetSafeHdc() == NULL || hIcon == NULL
        || size.cx < 0 || size.cy < 0)
        return;
    if (size.cx == 0 || size.cy == 0)
        ::DrawIconEx(m_pDC->GetSafeHdc(), start_point.x, start_point.y, hIcon, 0, 0, 0, NULL, DI_NORMAL | DI_DEFAULTSIZE);
    else
        ::DrawIconEx(m_pDC->GetSafeHdc(), start_point.x, start_point.y, hIcon, size.cx, size.cy, 0, NULL, DI_NORMAL);
}


/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
Gdiplus::Color CGdiPlusHelper::COLORREFToGdiplusColor(COLORREF color, BYTE alpha /*= 255*/)
{
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

COLORREF CGdiPlusHelper::GdiplusColorToCOLORREF(Gdiplus::Color color)
{
    return RGB(color.GetR(), color.GetG(), color.GetB());
}

CRect CGdiPlusHelper::GdiplusRectToCRect(Gdiplus::RectF rect)
{
    return CRect(TruncateRealToInt(rect.GetLeft()),
        TruncateRealToInt(rect.GetTop()),
        TruncateRealToInt(rect.GetRight()),
        TruncateRealToInt(rect.GetBottom()));
}

Gdiplus::RectF CGdiPlusHelper::CRectToGdiplusRect(CRect rect)
{
    return Gdiplus::RectF(static_cast<Gdiplus::REAL>(rect.left),
        static_cast<Gdiplus::REAL>(rect.top),
        static_cast<Gdiplus::REAL>(rect.Width()),
        static_cast<Gdiplus::REAL>(rect.Height()));
}
