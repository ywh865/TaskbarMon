#include "stdafx.h"
#include "PictureStatic.h"


CPictureStatic::CPictureStatic()
{
}


CPictureStatic::~CPictureStatic()
{
	m_memDC.DeleteDC();
	m_bitmap.DeleteObject();
}

void CPictureStatic::SetPicture(UINT pic_id)
{
	m_memDC.DeleteDC();
	m_bitmap.DeleteObject();
	ZeroMemory(&m_bm, sizeof(m_bm));

	if (!m_bitmap.LoadBitmap(pic_id))
	{
		Invalidate();
		return;
	}

	if (::GetObject(m_bitmap.GetSafeHandle(), static_cast<int>(sizeof(m_bm)), &m_bm) != static_cast<int>(sizeof(m_bm)))
	{
		m_bitmap.DeleteObject();
		Invalidate();
		return;
	}

	CDC* window_dc = GetDC();
	if (window_dc == nullptr)
	{
		m_bitmap.DeleteObject();
		Invalidate();
		return;
	}

	const BOOL created = m_memDC.CreateCompatibleDC(window_dc);
	ReleaseDC(window_dc);
	if (!created || m_memDC.SelectObject(&m_bitmap) == nullptr)
	{
		m_memDC.DeleteDC();
		m_bitmap.DeleteObject();
		Invalidate();
		return;
	}

	GetClientRect(m_rect);
	Invalidate();
}

void CPictureStatic::SetPicture(HBITMAP hBitmap)
{
	m_memDC.DeleteDC();
	m_bitmap.DeleteObject();
	ZeroMemory(&m_bm, sizeof(m_bm));

	if (hBitmap == nullptr)
	{
		Invalidate();
		return;
	}

	// Ownership of hBitmap transfers to this control.
	if (!m_bitmap.Attach(hBitmap))
	{
		::DeleteObject(hBitmap);
		Invalidate();
		return;
	}

	if (::GetObject(m_bitmap.GetSafeHandle(), static_cast<int>(sizeof(m_bm)), &m_bm) != static_cast<int>(sizeof(m_bm)))
	{
		m_bitmap.DeleteObject();
		Invalidate();
		return;
	}

	CDC* window_dc = GetDC();
	if (window_dc == nullptr)
	{
		m_bitmap.DeleteObject();
		Invalidate();
		return;
	}

	const BOOL created = m_memDC.CreateCompatibleDC(window_dc);
	ReleaseDC(window_dc);
	if (!created || m_memDC.SelectObject(&m_bitmap) == nullptr)
	{
		m_memDC.DeleteDC();
		m_bitmap.DeleteObject();
		Invalidate();
		return;
	}

	GetClientRect(m_rect);
	Invalidate();
}

BEGIN_MESSAGE_MAP(CPictureStatic, CStatic)
	ON_WM_PAINT()
END_MESSAGE_MAP()


void CPictureStatic::OnPaint()
{
	CPaintDC dc(this); // device context for painting
					   // TODO: 在此处添加消息处理程序代码
					   // 不为绘图消息调用 CStatic::OnPaint()
	if (m_bitmap.m_hObject != NULL)
	{
		// 以下两行避免图片失真
		dc.SetStretchBltMode(HALFTONE);
		dc.SetBrushOrg(0, 0);
		//绘制将内存DC中的图像
		dc.StretchBlt(0, 0, m_rect.Width(), m_rect.Height(), &m_memDC, 0, 0, m_bm.bmWidth, m_bm.bmHeight, SRCCOPY);

		//向父窗口发送重绘的消息
		CWnd* pParent{ GetParent() };
		if (pParent != nullptr)
			pParent->SendMessage(WM_CONTROL_REPAINT, (WPARAM)this, LPARAM(&dc));
	}
}
