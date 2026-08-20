#include "stdafx.h"
#include "Test.h"
#include "TaskbarMon.h"
#include "MessageDlg.h"

CTest::CTest()
{
}

CTest::~CTest()
{
}

void CTest::Test()
{
}

void CTest::TestCommand()
{
    CMessageDlg dlg;
    dlg.SetWindowTitle(_T("System Info"));
    dlg.SetInfoText(_T("System Information for TaskbarMon."));
    dlg.SetMessageText(theApp.GetSystemInfoString().GetString());
    dlg.SetStandarnMessageIcon(CMessageDlg::SI_INFORMATION);
    dlg.DoModal();
}
