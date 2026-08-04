#include "stdafx.h"
#include "IDrawCommon.h"

void IDrawCommon::DrawLine(CPoint start_point, int height, COLORREF color, BYTE alpha)
{
    if (height > 0)
    {
        CPoint end_point = start_point;
        end_point.y -= height;
        DrawLine(start_point, end_point, color, alpha);
    }
}
