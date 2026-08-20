#pragma once
#include "TaskBarDlg.h"
class CClassicalTaskbarDlg :
    public CTaskBarDlg
{
public:

private:
    // 通过 CTaskBarDlg 继承
    bool AdjustTaskbarWndPos(bool force_adjust) override;
    bool InitTaskbarWnd() override;
    bool IsTaskbarLayoutValid() const override;
    bool ResetTaskbarPos() override;
    virtual HWND GetParentHwnd() override;

private:
    HWND m_hBar{};                 //已验证的任务栏容器
    HWND m_hMin{};                 //已验证的任务列表窗口
    WindowState m_taskbar_container_state;
    WindowState m_task_list_state; //附加前的完整任务列表状态
    CRect m_last_applied_task_list_rect;
    bool m_has_last_applied_task_list_rect{};
    bool IsCapturedTaskbarLayoutIdentityValid() const;

    // 通过 CTaskBarDlg 继承
    void CheckTaskbarOnTopOrBottom() override;
};

