#include "stdafx.h"
#include "ClassicalTaskbarDlg.h"

namespace
{
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

bool GetWindowRectInParentClientPixels(HWND hwnd, HWND parent, CRect& parent_client_rect)
{
    if (hwnd == nullptr || parent == nullptr || !::IsWindow(hwnd) || !::IsWindow(parent))
        return false;

    CRect screen_rect;
    if (!::GetWindowRect(hwnd, &screen_rect) || screen_rect.Width() <= 0 || screen_rect.Height() <= 0)
        return false;

    CPoint top_left{screen_rect.left, screen_rect.top};
    CPoint bottom_right{screen_rect.right, screen_rect.bottom};
    if (!::ScreenToClient(parent, &top_left) || !::ScreenToClient(parent, &bottom_right))
        return false;

    parent_client_rect.SetRect(top_left.x, top_left.y, bottom_right.x, bottom_right.y);
    return parent_client_rect.Width() > 0 && parent_client_rect.Height() > 0;
}

bool IsContainedIn(const CRect& outer, const CRect& inner)
{
    return inner.left >= outer.left && inner.top >= outer.top
        && inner.right <= outer.right && inner.bottom <= outer.bottom;
}
}

bool CClassicalTaskbarDlg::InitTaskbarWnd()
{
    m_hBar = nullptr;
    m_hMin = nullptr;
    m_taskbar_container_state = {};
    m_task_list_state = {};
    m_last_applied_task_list_rect.SetRectEmpty();
    m_has_last_applied_task_list_rect = false;

    if (!IsTaskbarWindow(m_hTaskbar))
        return false;

    m_hBar = ::FindWindowEx(m_hTaskbar, nullptr, L"ReBarWindow32", nullptr);
    if (m_hBar == nullptr)
        m_hBar = ::FindWindowEx(m_hTaskbar, nullptr, L"WorkerW", nullptr);
    if (m_hBar == nullptr || ::GetParent(m_hBar) != m_hTaskbar
        || (!IsWindowClass(m_hBar, L"ReBarWindow32") && !IsWindowClass(m_hBar, L"WorkerW")))
    {
        return false;
    }

    m_hMin = ::FindWindowEx(m_hBar, nullptr, L"MSTaskSwWClass", nullptr);
    if (m_hMin == nullptr)
        m_hMin = ::FindWindowEx(m_hBar, nullptr, L"MSTaskListWClass", nullptr);
    if (m_hMin == nullptr || ::GetParent(m_hMin) != m_hBar
        || (!IsWindowClass(m_hMin, L"MSTaskSwWClass") && !IsWindowClass(m_hMin, L"MSTaskListWClass")))
    {
        return false;
    }

    if (!CaptureWindowState(m_taskbar_container_state, m_hBar)
        || !CaptureWindowState(m_task_list_state, m_hMin))
        return false;

    return IsTaskbarLayoutValid();
}

bool CClassicalTaskbarDlg::IsTaskbarLayoutValid() const
{
    if (!IsCapturedTaskbarLayoutIdentityValid())
        return false;
    if (::GetWindowLongPtr(m_hMin, GWL_STYLE) != m_task_list_state.style
        || ::GetWindowLongPtr(m_hMin, GWL_EXSTYLE) != m_task_list_state.ex_style)
    {
        return false;
    }

    CRect taskbar_rect;
    CRect container_rect;
    CRect task_list_rect;
    CRect container_client_rect;
    if (!::GetWindowRect(m_hTaskbar, &taskbar_rect) || !::GetWindowRect(m_hBar, &container_rect)
        || !::GetWindowRect(m_hMin, &task_list_rect)
        || !GetClientRectInScreenPixels(m_hBar, container_client_rect))
    {
        return false;
    }

    return taskbar_rect.Width() >= 48 && taskbar_rect.Height() >= 16
        && container_rect.Width() >= 48 && container_rect.Height() >= 16
        && task_list_rect.Width() >= 32 && task_list_rect.Height() >= 16
        && IsContainedIn(taskbar_rect, container_rect)
        && IsContainedIn(container_client_rect, task_list_rect);
}

bool CClassicalTaskbarDlg::AdjustTaskbarWndPos(bool force_adjust)
{
    (void)force_adjust;
    if (!IsTaskbarLayoutValid() || !IsWindowSizeWithinSafeLimits())
    {
        SetTaskbarError(ERROR_NOT_SUPPORTED);
        return false;
    }

    CRect container_client_rect;
    if (!GetClientRectInScreenPixels(m_hBar, container_client_rect))
    {
        SetTaskbarError(::GetLastError());
        return false;
    }

    RECT raw_client_rect{};
    if (!::GetClientRect(m_hBar, &raw_client_rect))
    {
        SetTaskbarError(::GetLastError());
        return false;
    }
    const CRect container_client{raw_client_rect};
    if (!m_task_list_state.has_parent_client_rect)
    {
        SetTaskbarError(ERROR_INVALID_DATA);
        return false;
    }
    const CRect original_task_list{m_task_list_state.parent_client_rect};
    if (!IsContainedIn(container_client, original_task_list))
    {
        SetTaskbarError(ERROR_NOT_SUPPORTED);
        return false;
    }

    const int minimum_remaining_task_list_pixels = max(32, DPI(32));
    CRect resized_task_list;
    CRect hosted_window;
    if (m_taskbar_on_top_or_bottom)
    {
        const int remaining_width = original_task_list.Width() - m_window_width;
        if (remaining_width < minimum_remaining_task_list_pixels || m_window_height > container_client.Height())
        {
            SetTaskbarError(ERROR_NOT_ENOUGH_MEMORY);
            return false;
        }

        const int task_list_x = theApp.m_taskbar_data.tbar_wnd_on_left
            ? original_task_list.left + m_window_width
            : original_task_list.left;
        resized_task_list.SetRect(task_list_x,
                                  original_task_list.top,
                                  task_list_x + remaining_width,
                                  original_task_list.bottom);
        const int hosted_x = theApp.m_taskbar_data.tbar_wnd_on_left
            ? original_task_list.left
            : resized_task_list.right;
        const int hosted_y = max(0, (container_client.Height() - m_window_height) / 2);
        hosted_window.SetRect(hosted_x, hosted_y, hosted_x + m_window_width, hosted_y + m_window_height);
    }
    else
    {
        const int remaining_height = original_task_list.Height() - m_window_height;
        if (remaining_height < minimum_remaining_task_list_pixels || m_window_width > original_task_list.Width())
        {
            SetTaskbarError(ERROR_NOT_ENOUGH_MEMORY);
            return false;
        }

        const int task_list_y = theApp.m_taskbar_data.tbar_wnd_on_left
            ? original_task_list.top + m_window_height
            : original_task_list.top;
        resized_task_list.SetRect(original_task_list.left,
                                  task_list_y,
                                  original_task_list.right,
                                  task_list_y + remaining_height);
        const int hosted_y = theApp.m_taskbar_data.tbar_wnd_on_left
            ? original_task_list.top
            : resized_task_list.bottom;
        const int hosted_x = original_task_list.left + (original_task_list.Width() - m_window_width) / 2;
        hosted_window.SetRect(hosted_x, hosted_y, hosted_x + m_window_width, hosted_y + m_window_height);
    }

    if (!IsContainedIn(container_client, resized_task_list) || !IsContainedIn(container_client, hosted_window))
    {
        SetTaskbarError(ERROR_NOT_SUPPORTED);
        return false;
    }

    if (!::SetWindowPos(m_hMin,
                        nullptr,
                        resized_task_list.left,
                        resized_task_list.top,
                        resized_task_list.Width(),
                        resized_task_list.Height(),
                        SWP_NOACTIVATE | SWP_NOZORDER))
    {
        SetTaskbarError(::GetLastError());
        return false;
    }
    if (!VerifyWindowClientRect(m_hMin, m_hBar, resized_task_list)
        || ::GetWindowLongPtr(m_hMin, GWL_STYLE) != m_task_list_state.style
        || ::GetWindowLongPtr(m_hMin, GWL_EXSTYLE) != m_task_list_state.ex_style)
    {
        SetTaskbarError(ERROR_INVALID_DATA);
        return false;
    }

    // Record only a geometry that was verified after our SetWindowPos call.
    // ResetTaskbarPos uses this to distinguish our own live allocation from
    // an unrelated Explorer reflow before writing the captured state back.
    m_last_applied_task_list_rect = resized_task_list;
    m_has_last_applied_task_list_rect = true;

    m_rect = hosted_window;
    if (!MoveWindow(m_rect))
    {
        SetTaskbarError(::GetLastError());
        return false;
    }

    return true;
}

bool CClassicalTaskbarDlg::ResetTaskbarPos()
{
    if (!m_taskbar_container_state.captured || !m_task_list_state.captured)
    {
        SetTaskbarError(ERROR_INVALID_DATA);
        m_restoration_pending = true;
        return false;
    }

    // A destroyed task-list HWND cannot retain our resize. Likewise, once the
    // captured Explorer root is gone, its old subtree cannot be restored and
    // must never be replayed into a replacement hierarchy.
    if (m_task_list_state.hwnd == nullptr || !::IsWindow(m_task_list_state.hwnd)
        || !IsTaskbarWindow(m_hTaskbar))
    {
        return true;
    }

    // The captured task-list window still exists. A changed parent, class,
    // process, style, or extended style is not proof that our geometry was
    // discarded; reporting success here could leave a live Explorer child
    // resized. Keep the owner alive for a verified retry instead.
    if (!IsCapturedTaskbarLayoutIdentityValid()
        || ::GetWindowLongPtr(m_hMin, GWL_STYLE) != m_task_list_state.style
        || ::GetWindowLongPtr(m_hMin, GWL_EXSTYLE) != m_task_list_state.ex_style)
    {
        SetTaskbarError(ERROR_INVALID_DATA);
        m_restoration_pending = true;
        return false;
    }

    RECT raw_client_rect{};
    CRect current_task_list_rect;
    if (!::GetClientRect(m_hBar, &raw_client_rect)
        || !m_task_list_state.has_parent_client_rect
        || !GetWindowRectInParentClientPixels(m_hMin, m_hBar, current_task_list_rect))
    {
        SetTaskbarError(::GetLastError());
        m_restoration_pending = true;
        return false;
    }

    const CRect current_container_client{raw_client_rect};
    // A taskbar move alone preserves parent-client coordinates. A resize,
    // orientation switch, or reflow may not. Never write a stale rect outside
    // the current container and then claim that detach was restored.
    if (!IsContainedIn(current_container_client, m_task_list_state.parent_client_rect)
        || !IsContainedIn(current_container_client, current_task_list_rect))
    {
        SetTaskbarError(ERROR_NOT_SUPPORTED);
        m_restoration_pending = true;
        return false;
    }

    const bool task_list_at_original_rect =
        current_task_list_rect.EqualRect(&m_task_list_state.parent_client_rect) != FALSE;
    const bool task_list_at_last_verified_hosted_rect = m_has_last_applied_task_list_rect
        && current_task_list_rect.EqualRect(&m_last_applied_task_list_rect) != FALSE;
    if (!task_list_at_original_rect && !task_list_at_last_verified_hosted_rect)
    {
        SetTaskbarError(ERROR_INVALID_DATA);
        m_restoration_pending = true;
        return false;
    }

    if (!RestoreWindowState(m_task_list_state))
    {
        SetTaskbarError(::GetLastError());
        m_restoration_pending = true;
        return false;
    }
    return true;
}

bool CClassicalTaskbarDlg::IsCapturedTaskbarLayoutIdentityValid() const
{
    return IsTaskbarWindow(m_hTaskbar)
        && m_taskbar_container_state.captured
        && m_task_list_state.captured
        && m_taskbar_container_state.hwnd == m_hBar
        && m_taskbar_container_state.parent == m_hTaskbar
        && m_task_list_state.hwnd == m_hMin
        && m_task_list_state.parent == m_hBar
        && VerifyWindowIdentity(m_taskbar_container_state)
        && VerifyWindowIdentity(m_task_list_state)
        && ::GetParent(m_hBar) == m_hTaskbar
        && ::GetParent(m_hMin) == m_hBar;
}

HWND CClassicalTaskbarDlg::GetParentHwnd()
{
    return m_hBar;
}

void CClassicalTaskbarDlg::CheckTaskbarOnTopOrBottom()
{
    CRect rect;
    if (IsTaskbarWindow(m_hTaskbar) && ::GetWindowRect(m_hTaskbar, &rect)
        && rect.Width() > 0 && rect.Height() > 0)
    {
        m_taskbar_on_top_or_bottom = rect.Width() >= rect.Height();
    }
    else
    {
        m_taskbar_on_top_or_bottom = true;
    }
}
