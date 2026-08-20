#include "stdafx.h"
#include "Win11TaskbarDlg.h"
#include "WindowsSettingHelper.h"

namespace
{
bool IsContainedIn(const CRect& outer, const CRect& inner)
{
    return inner.left >= outer.left && inner.top >= outer.top
        && inner.right <= outer.right && inner.bottom <= outer.bottom;
}

bool GetClientRectInScreenPixels(HWND hwnd, CRect& screen_rect)
{
    RECT client_rect{};
    if (hwnd == nullptr || !::IsWindow(hwnd) || !::GetClientRect(hwnd, &client_rect))
        return false;

    CPoint top_left{client_rect.left, client_rect.top};
    if (!::ClientToScreen(hwnd, &top_left))
        return false;

    screen_rect.SetRect(top_left.x,
                        top_left.y,
                        top_left.x + (client_rect.right - client_rect.left),
                        top_left.y + (client_rect.bottom - client_rect.top));
    return screen_rect.Width() > 0 && screen_rect.Height() > 0;
}
}

bool CWin11TaskbarDlg::InitTaskbarWnd()
{
    m_hNotify = nullptr;
    m_hStart = nullptr;

    // Secondary-taskbar and non-horizontal hierarchies have historically
    // varied across Windows releases.  Use the safe floating path until a
    // concrete layout has been verified rather than guessing a host window.
    if (m_is_secondary_display || !IsWindowClass(m_hTaskbar, L"Shell_TrayWnd"))
        return false;

    m_hNotify = ::FindWindowEx(m_hTaskbar, nullptr, L"TrayNotifyWnd", nullptr);
    m_hStart = ::FindWindowEx(m_hTaskbar, nullptr, L"Start", nullptr);
    return IsTaskbarLayoutValid();
}

bool CWin11TaskbarDlg::IsTaskbarLayoutValid() const
{
    if (m_is_secondary_display || !IsWindowClass(m_hTaskbar, L"Shell_TrayWnd")
        || m_hNotify == nullptr || m_hStart == nullptr || !::IsWindow(m_hNotify) || !::IsWindow(m_hStart)
        || ::GetParent(m_hNotify) != m_hTaskbar || ::GetParent(m_hStart) != m_hTaskbar
        || !IsWindowClass(m_hNotify, L"TrayNotifyWnd") || !IsWindowClass(m_hStart, L"Start"))
    {
        return false;
    }

    CRect taskbar_rect;
    CRect taskbar_client_rect;
    CRect notify_rect;
    CRect start_rect;
    if (!::GetWindowRect(m_hTaskbar, &taskbar_rect) || !GetClientRectInScreenPixels(m_hTaskbar, taskbar_client_rect)
        || !::GetWindowRect(m_hNotify, &notify_rect) || !::GetWindowRect(m_hStart, &start_rect))
    {
        return false;
    }

    return taskbar_rect.Width() >= 64 && taskbar_rect.Height() >= 16
        && taskbar_rect.Width() >= taskbar_rect.Height()
        && IsContainedIn(taskbar_client_rect, notify_rect)
        && IsContainedIn(taskbar_client_rect, start_rect);
}

bool CWin11TaskbarDlg::AdjustTaskbarWndPos(bool force_adjust)
{
    (void)force_adjust;
    if (!IsTaskbarLayoutValid() || !IsWindowSizeWithinSafeLimits())
    {
        SetTaskbarError(ERROR_NOT_SUPPORTED);
        return false;
    }

    CRect notify_screen_rect;
    CRect start_screen_rect;
    RECT raw_client_rect{};
    if (!::GetWindowRect(m_hNotify, &notify_screen_rect) || !::GetWindowRect(m_hStart, &start_screen_rect)
        || !::GetClientRect(m_hTaskbar, &raw_client_rect))
    {
        SetTaskbarError(::GetLastError());
        return false;
    }
    const CRect taskbar_client{raw_client_rect};

    CPoint notify_position{notify_screen_rect.left, notify_screen_rect.top};
    CPoint start_position{start_screen_rect.left, start_screen_rect.top};
    if (!::ScreenToClient(m_hTaskbar, &notify_position) || !::ScreenToClient(m_hTaskbar, &start_position))
    {
        SetTaskbarError(::GetLastError());
        return false;
    }

    const CRect notify_rect{notify_position.x,
                            notify_position.y,
                            notify_position.x + notify_screen_rect.Width(),
                            notify_position.y + notify_screen_rect.Height()};
    const CRect start_rect{start_position.x,
                           start_position.y,
                           start_position.x + start_screen_rect.Width(),
                           start_position.y + start_screen_rect.Height()};
    if (!IsContainedIn(taskbar_client, notify_rect) || !IsContainedIn(taskbar_client, start_rect))
    {
        SetTaskbarError(ERROR_INVALID_DATA);
        return false;
    }

    int hosted_x{};
    if (!theApp.m_taskbar_data.tbar_wnd_on_left || !CWindowsSettingHelper::IsTaskbarCenterAlign())
    {
        hosted_x = notify_rect.left - m_window_width;
        if (theApp.m_taskbar_data.avoid_overlap_with_widgets
            && CWindowsSettingHelper::IsTaskbarWidgetsBtnShown()
            && !CWindowsSettingHelper::IsTaskbarCenterAlign())
        {
            hosted_x -= DPI(theApp.m_taskbar_data.taskbar_left_space_win11);
        }
    }
    else if (theApp.m_taskbar_data.tbar_wnd_snap)
    {
        hosted_x = start_rect.left - m_window_width;
    }
    else
    {
        hosted_x = DPI(2);
        if (CWindowsSettingHelper::IsTaskbarWidgetsBtnShown())
            hosted_x += DPI(theApp.m_taskbar_data.taskbar_left_space_win11);
    }

    hosted_x += DPI(theApp.m_taskbar_data.window_offset_left);
    const int hosted_y = (taskbar_client.Height() - m_window_height) / 2
        + DPI(theApp.m_taskbar_data.window_offset_top);
    const CRect hosted_window{hosted_x, hosted_y, hosted_x + m_window_width, hosted_y + m_window_height};
    if (!IsContainedIn(taskbar_client, hosted_window))
    {
        SetTaskbarError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }

    // Windows 11 overlay mode intentionally changes only our child window.
    // It never moves or resizes Start, TrayNotifyWnd, or a task-list window.
    m_rect = hosted_window;
    if (!MoveWindow(m_rect))
    {
        SetTaskbarError(::GetLastError());
        return false;
    }
    return true;
}

bool CWin11TaskbarDlg::ResetTaskbarPos()
{
    // Overlay mode does not mutate Explorer windows.  CloseAndRestore restores
    // our own captured parent/style/position after this returns.
    return true;
}

HWND CWin11TaskbarDlg::GetParentHwnd()
{
    return m_hTaskbar;
}

void CWin11TaskbarDlg::CheckTaskbarOnTopOrBottom()
{
    CRect taskbar_rect;
    if (::GetWindowRect(m_hTaskbar, &taskbar_rect) && taskbar_rect.Width() > 0 && taskbar_rect.Height() > 0)
        m_taskbar_on_top_or_bottom = taskbar_rect.Width() >= taskbar_rect.Height();
    else
        m_taskbar_on_top_or_bottom = true;
}
