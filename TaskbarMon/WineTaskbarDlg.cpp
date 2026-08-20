#include "stdafx.h"
#include "WineTaskbarDlg.h"

bool CWineTaskbarDlg::InitTaskbarWnd()
{
    // Wine does not expose a stable, documented Explorer taskbar hierarchy.
    // Deliberately select the floating path instead of reparenting into the
    // desktop window or guessing a task-list container.
    return false;
}

bool CWineTaskbarDlg::IsTaskbarLayoutValid() const
{
    return false;
}

bool CWineTaskbarDlg::AdjustTaskbarWndPos(bool force_adjust)
{
    (void)force_adjust;
    const int screen_width = ::GetSystemMetrics(SM_CXSCREEN);
    const int screen_height = ::GetSystemMetrics(SM_CYSCREEN);
    if (screen_width <= 0 || screen_height <= 0 || m_window_width <= 0 || m_window_height <= 0)
    {
        SetTaskbarError(ERROR_NOT_SUPPORTED);
        return false;
    }

    CRect rect{};
    const int x = theApp.m_taskbar_data.tbar_wnd_on_left ? 0 : max(0, screen_width - m_window_width);
    const int y = max(0, screen_height - m_window_height);
    rect.SetRect(x, y, x + m_window_width, y + m_window_height);
    m_rect = rect;
    return MoveWindow(rect, false);
}

bool CWineTaskbarDlg::ResetTaskbarPos()
{
    // Floating mode never mutates Wine's taskbar windows.
    return true;
}

void CWineTaskbarDlg::CheckTaskbarOnTopOrBottom()
{
    m_taskbar_on_top_or_bottom = true;
}

HWND CWineTaskbarDlg::GetParentHwnd()
{
    return nullptr;
}
