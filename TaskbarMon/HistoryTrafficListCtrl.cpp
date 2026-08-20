#include "stdafx.h"
#include "HistoryTrafficListCtrl.h"
#include "DrawCommonHelper.h"

#include <cmath>
#include <limits>

IMPLEMENT_DYNAMIC(CHistoryTrafficListCtrl, CListCtrl)

CHistoryTrafficListCtrl::CHistoryTrafficListCtrl()
{
}


CHistoryTrafficListCtrl::~CHistoryTrafficListCtrl()
{
}
void CHistoryTrafficListCtrl::SetDrawItemRangeData(int item, double range, COLORREF color)
{
	if (item < 0) return;
	if (item >= static_cast<int>(m_item_rage_data.size()))
		m_item_rage_data.resize(item + 1);
	m_item_rage_data[item].data_value = range;
	m_item_rage_data[item].color = color;
}

void CHistoryTrafficListCtrl::SetDrawItemRangInLogScale(bool log_scale)
{
	m_use_log_scale = log_scale;
	Invalidate();
}

BEGIN_MESSAGE_MAP(CHistoryTrafficListCtrl, CListCtrl)
	ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, &CHistoryTrafficListCtrl::OnNMCustomdraw)
END_MESSAGE_MAP()


void CHistoryTrafficListCtrl::OnNMCustomdraw(NMHDR *pNMHDR, LRESULT *pResult)
{
	if (m_draw_item_range)
	{
		*pResult = CDRF_DODEFAULT;
		LPNMLVCUSTOMDRAW lplvdr = reinterpret_cast<LPNMLVCUSTOMDRAW>(pNMHDR);
		NMCUSTOMDRAW& nmcd = lplvdr->nmcd;
		switch (lplvdr->nmcd.dwDrawStage)	//判断状态   
		{
		case CDDS_PREPAINT:
			*pResult = CDRF_NOTIFYITEMDRAW;
			break;
		case CDDS_ITEMPREPAINT:			//如果为画ITEM之前就要进行颜色的改变
			{
			const DWORD_PTR item_spec = nmcd.dwItemSpec;
			if (item_spec <= static_cast<DWORD_PTR>((std::numeric_limits<int>::max)()) &&
				item_spec < m_item_rage_data.size())
			{
				const int item_index = static_cast<int>(item_spec);
				const size_t item_offset = static_cast<size_t>(item_index);
				double range = m_item_rage_data[item_offset].data_value;
				CDC* pDC = CDC::FromHandle(nmcd.hdc);		//获取绘图DC
				CRect item_rect, draw_rect;
				GetSubItemRect(item_index,m_draw_item_range_row, LVIR_BOUNDS, item_rect);	//获取绘图单元格的矩形区域
				CDrawCommon::SetDrawRect(pDC, item_rect);		//设置绘图区域为当前列
                //使用双缓冲绘图
                {
                    CDrawDoubleBuffer draw_double_buffer(pDC, item_rect);
                    auto* mem_dc = draw_double_buffer.GetMemDC();
                    if (mem_dc == nullptr)
                        break;
                    //填充背景
                    draw_rect = item_rect;
                    draw_rect.MoveToXY(0, 0);
                    mem_dc->FillSolidRect(draw_rect, GetSysColor(COLOR_WINDOW));
                    if (draw_rect.Height() > 2 * m_margin)
                    {
                        draw_rect.top += m_margin;
                        draw_rect.bottom -= m_margin;
                    }
					int width;
					if (!std::isfinite(range) || range < 0)
						range = 0;
					if (m_use_log_scale)	//使用对数比例（y=ln(x+1)）
					{
						range = std::log(range + 1);
						if (!std::isfinite(range))
							range = 0;
					}
					if (range > 1000)
						range = 1000;
					const auto width_value = range * draw_rect.Width()
						/ (m_use_log_scale ? std::log(1000 + 1) : 1000.0);
					if (!std::isfinite(width_value) || width_value <= 0)
						width = 0;
					else if (width_value >= draw_rect.Width())
						width = draw_rect.Width();
					else
						width = static_cast<int>(width_value);
					draw_rect.right = draw_rect.left + width;
                    mem_dc->FillSolidRect(draw_rect, m_item_rage_data[item_offset].color);
                }

				//当前列绘制完成后将绘图区域设置为左边的区域，防止当前列的区域被覆盖
				CRect rect1{ item_rect };
				rect1.left = 0;
				rect1.right = item_rect.left;
				CDrawCommon::SetDrawRect(pDC, rect1);
			}
			}
			*pResult = CDRF_DODEFAULT;
			break;
		}
	}
}
