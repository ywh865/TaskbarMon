#pragma once
#include "TaskBarDlg.h"
class CWin11TaskbarDlg :
    public CTaskBarDlg
{
public:

private:
    // 通过 CTaskBarDlg 继承
    bool InitTaskbarWnd() override;
    bool IsTaskbarLayoutValid() const override;
    virtual bool AdjustTaskbarWndPos(bool force_adjust) override;
    bool ResetTaskbarPos() override;
    virtual HWND GetParentHwnd() override;

    HWND m_hNotify{};   //任务栏通知区域的句柄（只读定位，不调整其尺寸）
    HWND m_hStart{};    //开始按钮的句柄（只读定位，不调整其尺寸）

    // 通过 CTaskBarDlg 继承
    void CheckTaskbarOnTopOrBottom() override;

};

