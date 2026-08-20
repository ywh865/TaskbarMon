#pragma once
#include "TaskBarDlg.h"
class CWineTaskbarDlg :
    public CTaskBarDlg
{
private:
    // 通过 CTaskBarDlg 继承
    bool InitTaskbarWnd() override;
    bool IsTaskbarLayoutValid() const override;
    bool AdjustTaskbarWndPos(bool force_adjust) override;
    bool ResetTaskbarPos() override;
    void CheckTaskbarOnTopOrBottom() override;
    HWND GetParentHwnd() override;
};

