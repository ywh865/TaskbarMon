#include "stdafx.h"
#include "DrawCommon.h"
#include "DrawCommonHelper.h"

CDrawCommon::CDrawCommon()
{
}

CDrawCommon::~CDrawCommon()
{
}

void CDrawCommon::Create(CDC* pDC, CWnd* pMainWnd)
{
    m_pDC = pDC;
    m_pMainWnd = pMainWnd;
    if (m_pDC == nullptr || m_pDC->GetSafeHdc() == NULL)
        return;
    if (pMainWnd != nullptr)
        m_pfont = m_pMainWnd->GetFont();
    m_gdi_plus_drawer.Create(pDC);
}

void CDrawCommon::SetFont(CFont* pfont)
{
    if (m_pDC == nullptr || m_pDC->GetSafeHdc() == NULL || pfont == nullptr
        || pfont->GetSafeHandle() == NULL)
        return;
    m_pfont = pfont;
    if (m_pfont != nullptr && m_pfont->GetSafeHandle() != NULL)
        m_pDC->SelectObject(m_pfont);
}

void CDrawCommon::SetDC(CDC* pDC)
{
    m_pDC = pDC;
    if (m_pDC == nullptr || m_pDC->GetSafeHdc() == NULL)
        return;
    m_gdi_plus_drawer.Create(pDC);
}

void CDrawCommon::DrawWindowText(CRect rect, LPCTSTR lpszString, COLORREF color, Alignment align, bool draw_back_ground, bool multi_line, BYTE alpha)
{
    if (m_pDC == nullptr || m_pDC->GetSafeHdc() == NULL || lpszString == nullptr)
        return;
    m_pDC->SetTextColor(color);
    if (!draw_back_ground)
        m_pDC->SetBkMode(TRANSPARENT);
    if (m_pfont != nullptr && m_pfont->GetSafeHandle() != NULL)
        m_pDC->SelectObject(m_pfont);
    CSize text_size = m_pDC->GetTextExtent(lpszString);

    auto format = DrawCommonHelper::ProccessTextFormat(rect, text_size, align, multi_line);

    if (draw_back_ground)
        m_pDC->FillSolidRect(rect, m_back_color);
    m_pDC->DrawText(lpszString, rect, format);
}

void CDrawCommon::SetDrawRect(CRect rect)
{
    if (m_pDC == nullptr || m_pDC->GetSafeHdc() == NULL)
        return;
    CRgn rgn;
    rgn.CreateRectRgnIndirect(rect);
    m_pDC->SelectClipRgn(&rgn);
}

void CDrawCommon::SetDrawRect(CDC* pDC, CRect rect)
{
    if (pDC == nullptr || pDC->GetSafeHdc() == NULL)
        return;
    CRgn rgn;
    rgn.CreateRectRgnIndirect(rect);
    pDC->SelectClipRgn(&rgn);
}

void CDrawCommon::DrawBitmap(CBitmap& bitmap, CPoint start_point, CSize size, StretchMode stretch_mode)
{
    if (m_pDC == nullptr || m_pDC->GetSafeHdc() == NULL || bitmap.GetSafeHandle() == NULL
        || size.cx < 0 || size.cy < 0)
        return;

    CDC memDC;

    //获取图像实际大小
    BITMAP bm{};
    if (::GetObject(bitmap.GetSafeHandle(), static_cast<int>(sizeof(bm)), &bm) != static_cast<int>(sizeof(bm))
        || bm.bmWidth <= 0 || bm.bmHeight <= 0)
        return;

    if (!memDC.CreateCompatibleDC(m_pDC) || memDC.SelectObject(&bitmap) == nullptr)
    {
        memDC.DeleteDC();
        return;
    }
    // 以下两行避免图片失真
    m_pDC->SetStretchBltMode(HALFTONE);
    m_pDC->SetBrushOrg(0, 0);

    DrawCommonHelper::ImageDrawAreaConvert(CSize(bm.bmWidth, bm.bmHeight), start_point, size, stretch_mode);

    m_pDC->StretchBlt(start_point.x, start_point.y, size.cx, size.cy, &memDC, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    memDC.DeleteDC();
}

void CDrawCommon::DrawBitmap(UINT bitmap_id, CPoint start_point, CSize size, StretchMode stretch_mode)
{
    CBitmap bitmap;
    bitmap.LoadBitmap(bitmap_id);
    DrawBitmap(bitmap, start_point, size, stretch_mode);
}

void CDrawCommon::DrawBitmap(HBITMAP hbitmap, CPoint start_point, CSize size, StretchMode stretch_mode, BYTE)
{
    if (hbitmap == NULL)
        return;

    CBitmap bitmap;
    if (!bitmap.Attach(hbitmap))
        return;
    DrawBitmap(bitmap, start_point, size, stretch_mode);
    bitmap.Detach();
}

void CDrawCommon::DrawIcon(HICON hIcon, CPoint start_point, CSize size)
{
    if (m_pDC == nullptr || m_pDC->GetSafeHdc() == NULL || hIcon == NULL
        || size.cx < 0 || size.cy < 0)
        return;
    if (size.cx == 0 || size.cy == 0)
        ::DrawIconEx(m_pDC->GetSafeHdc(), start_point.x, start_point.y, hIcon, 0, 0, 0, NULL, DI_NORMAL | DI_DEFAULTSIZE);
    else
        ::DrawIconEx(m_pDC->GetSafeHdc(), start_point.x, start_point.y, hIcon, size.cx, size.cy, 0, NULL, DI_NORMAL);
}

void CDrawCommon::BitmapStretch(CImage* pImage, CImage* ResultImage, CSize size)
{
    if (pImage == nullptr || ResultImage == nullptr || !pImage->IsDIBSection() || size.cx <= 0 || size.cy <= 0)
        return;

    // CImage owns the temporary DCs returned here; every successful GetDC is
    // paired with ReleaseDC, including all failure paths.
    HDC image_hdc = pImage->GetDC();
    if (image_hdc == NULL)
        return;
    CDC* pImageDC1 = CDC::FromHandle(image_hdc);
    CBitmap* bitmap1 = pImageDC1 == nullptr ? nullptr : pImageDC1->GetCurrentBitmap();
    BITMAP bmpInfo{};
    if (bitmap1 == nullptr || bitmap1->GetBitmap(&bmpInfo) == 0)
    {
        pImage->ReleaseDC();
        return;
    }

    if (FAILED(ResultImage->Create(size.cx, size.cy, bmpInfo.bmBitsPixel)))
    {
        pImage->ReleaseDC();
        return;
    }

    HDC result_hdc = ResultImage->GetDC();
    if (result_hdc == NULL)
    {
        pImage->ReleaseDC();
        return;
    }
    CDC* ResultImageDC = CDC::FromHandle(result_hdc);
    if (ResultImageDC == nullptr)
    {
        ResultImage->ReleaseDC();
        pImage->ReleaseDC();
        return;
    }

    // 當 Destination 比較小的時候, 會根據 Destination DC 上的 Stretch Blt mode 決定是否要保留被刪除點的資訊
    ResultImageDC->SetStretchBltMode(HALFTONE);        // 使用最高品質的方式
    ::SetBrushOrgEx(ResultImageDC->m_hDC, 0, 0, NULL); // 調整 Brush 的起點

    // 把 pImage 畫到 ResultImage 上面
    StretchBlt(*ResultImageDC, 0, 0, size.cx, size.cy, *pImageDC1, 0, 0, pImage->GetWidth(), pImage->GetHeight(), SRCCOPY);
    // pImage->Draw(*ResultImageDC,0,0,StretchWidth,StretchHeight,0,0,pImage->GetWidth(),pImage->GetHeight());

    ResultImage->ReleaseDC();
    pImage->ReleaseDC();
}

void CDrawCommon::FillRect(CRect rect, COLORREF color, BYTE alpha)
{
    if (m_pDC != nullptr && m_pDC->GetSafeHdc() != NULL)
        m_pDC->FillSolidRect(rect, color);
}

void CDrawCommon::FillRectWithBackColor(CRect rect)
{
    if (m_pDC != nullptr && m_pDC->GetSafeHdc() != NULL)
        m_pDC->FillSolidRect(rect, m_back_color);
}

void CDrawCommon::DrawRectOutLine(CRect rect, COLORREF color, int width, bool dot_line, BYTE alpha, int radius)
{
    if (m_pDC == nullptr || m_pDC->GetSafeHdc() == NULL || width <= 0)
        return;
    if (radius > 0)
    {
        //半径大于0时绘制圆角矩形，使用GDI+绘制
        m_gdi_plus_drawer.DrawRectOutLine(rect, color, width, dot_line, alpha, radius);
    }
    else
    {
        CPen aPen, * pOldPen;
        if (!aPen.CreatePen((dot_line ? PS_DOT : PS_SOLID), width, color))
            return;
        pOldPen = m_pDC->SelectObject(&aPen);
        if (pOldPen == nullptr)
        {
            aPen.DeleteObject();
            return;
        }
        CBrush* pOldBrush{ dynamic_cast<CBrush*>(m_pDC->SelectStockObject(NULL_BRUSH)) };
        if (pOldBrush == nullptr)
        {
            m_pDC->SelectObject(pOldPen);
            aPen.DeleteObject();
            return;
        }

        rect.DeflateRect(width / 2, width / 2);
        m_pDC->Rectangle(rect);
        m_pDC->SelectObject(pOldPen);
        m_pDC->SelectObject(pOldBrush); // Restore the old brush
        aPen.DeleteObject();
    }
}

void CDrawCommon::GetRegionFromImage(CRgn& rgn, CBitmap& cBitmap, int threshold)
{
    rgn.DeleteObject();
    if (cBitmap.GetSafeHandle() == NULL)
        return;

    CDC memDC;

    if (!memDC.CreateCompatibleDC(NULL))
        return;
    CBitmap* pOldMemBmp = memDC.SelectObject(&cBitmap);
    if (pOldMemBmp == nullptr)
        return;

    //创建总的窗体区域，初始region为0
    rgn.CreateRectRgn(0, 0, 0, 0);

    BITMAP bit{};
    if (cBitmap.GetBitmap(&bit) == 0 || bit.bmWidth <= 0 || bit.bmHeight <= 0)
        return;
    int y;
    for (y = 0; y < bit.bmHeight; y++)
    {
        CRgn rgnTemp; //保存临时region
        int iX = 0;
        do
        {
            //跳过透明色找到下一个非透明色的点.
            while (iX < bit.bmWidth && GetColorBritness(memDC.GetPixel(iX, y)) <= threshold)
                iX++;
            int iLeftX = iX; //记住这个起始点

            //寻找下个透明色的点
            while (iX < bit.bmWidth && GetColorBritness(memDC.GetPixel(iX, y)) > threshold)
                ++iX;

            //创建一个包含起点与重点间高为1像素的临时“region”
            rgnTemp.CreateRectRgn(iLeftX, y, iX, y + 1);
            rgn.CombineRgn(&rgn, &rgnTemp, RGN_OR);

            //删除临时"region",否则下次创建时和出错
            rgnTemp.DeleteObject();
        } while (iX < bit.bmWidth);
    }
    memDC.DeleteDC();
}

int CDrawCommon::GetColorBritness(COLORREF color)
{
    return (GetRValue(color) + GetGValue(color) + GetBValue(color)) / 3;
}

void CDrawCommon::DrawLine(CPoint start_point, CPoint end_point, COLORREF color, BYTE alpha)
{
    if (m_pDC == nullptr || m_pDC->GetSafeHdc() == NULL)
        return;
    CPen aPen, *pOldPen;
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

int CDrawCommon::GetTextWidth(LPCTSTR lpszString)
{
    if (m_pDC == nullptr || m_pDC->GetSafeHdc() == NULL || lpszString == nullptr)
        return 0;
    return m_pDC->GetTextExtent(lpszString).cx;
}

void CDrawCommon::GetTextExtent(const wchar_t* lpszString, int& w, int& h)
{
    w = 0;
    h = 0;
    if (m_pDC == nullptr || m_pDC->GetSafeHdc() == NULL || lpszString == nullptr)
        return;
    CSize size = m_pDC->GetTextExtent(lpszString);
    w = size.cx;
    h = size.cy;
}
