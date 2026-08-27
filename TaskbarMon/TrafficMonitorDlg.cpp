
// TrafficMonitorDlg.cpp : 实现文件
//

#include "stdafx.h"
#include "TaskbarMon.h"
#include "TrafficMonitorDlg.h"
#include "afxdialogex.h"
#include "Test.h"
#include "SetItemOrderDlg.h"
#include "WindowsSettingHelper.h"
#include "WIC.h"
#include "SupportedRenderEnums.h"
#include "ClassicalTaskbarDlg.h"
#include "Win11TaskbarDlg.h"
#include "WineTaskbarDlg.h"
#include "TaskbarHelper.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
    constexpr UINT_PTR kTaskbarRestoreRetryTimer = 0xD101;
    constexpr UINT_PTR kCloseRetryTimer = 0xD102;
    constexpr UINT kRetryIntervalMs = 500;
    constexpr DWORD kMonitorThreadExitTimeoutMs = 2000;
}


// CTrafficMonitorDlg 对话框

//静态成员初始化
unsigned int CTrafficMonitorDlg::m_WM_TASKBARCREATED{ ::RegisterWindowMessage(_T("TaskbarCreated")) };  //注册任务栏建立的消息

CTrafficMonitorDlg::CTrafficMonitorDlg(CWnd* pParent /*=NULL*/)
    : CDialog(IDD_TRAFFICMONITOR_DIALOG, pParent)
    , m_monitor_service(GetMonitorConfig())
{
    m_desktop_dc = ::GetDC(NULL);
#ifndef WITHOUT_TEMPERATURE
    m_monitor_service.SetHardwareProvider(&m_hardware_provider);
#endif
    m_monitor_service.on_history_save = [this]() { SaveHistoryTraffic(); };
}

CTrafficMonitorDlg::~CTrafficMonitorDlg()
{
    // OnClose normally joins the worker before MFC starts destroying this
    // object. Keep the same invariant for all unexpected teardown paths.
    ExitMonitorThread(INFINITE);

    // Explorer recovery is only safe while the owner HWND and its retry
    // timers still exist. PrepareForDestroy() must have completed it before
    // this destructor is reached; trying (and failing) here used to discard
    // the only object that could verify the restoration.
    ASSERT(m_tBarDlg == nullptr);

    ::ReleaseDC(NULL, m_desktop_dc);

    // Destroy notify area icon handles loaded via LoadImage in OnInitDialog
    // LoadImage creates a copy of the icon resource; each must be freed with DestroyIcon
    for (int i = 0; i < MAX_NOTIFY_ICON; i++)
    {
        if (theApp.m_notify_icons[i] != NULL)
        {
            ::DestroyIcon(theApp.m_notify_icons[i]);
            theApp.m_notify_icons[i] = NULL;
        }
    }
}

MonitorService::Config CTrafficMonitorDlg::GetMonitorConfig() const
{
    MonitorService::Config config;
    config.monitor_time_span = theApp.m_general_data.monitor_time_span;
    config.select_all = theApp.m_cfg_data.m_select_all;
    config.auto_select = theApp.m_cfg_data.m_auto_select;
    config.show_all_interface = theApp.m_general_data.show_all_interface;
    config.connection_name = theApp.m_cfg_data.m_connection_name;
    config.connections_hide = theApp.m_general_data.connections_hide.data();
    config.cpu_usage_by_time = (theApp.m_general_data.cpu_usage_acquire_method == GeneralSettingData::CA_CPU_TIME);
    config.hardware_monitor_item = theApp.m_general_data.hardware_monitor_item;
    config.hard_disk_name = theApp.m_general_data.hard_disk_name;
    config.cpu_core_name = theApp.m_general_data.cpu_core_name;
    config.history_traffic_path = theApp.m_history_traffic_path;
    config.log_path = theApp.m_log_path;
    config.config_dir = theApp.m_config_dir;
    config.debug_log = theApp.m_debug_log;
    return config;
}

CTaskBarDlg* CTrafficMonitorDlg::GetTaskbarWindow() const
{
    if (IsTaskbarWndValid())
        return m_tBarDlg;
    else
        return nullptr;
}

CTrafficMonitorDlg* CTrafficMonitorDlg::Instance()
{
    return dynamic_cast<CTrafficMonitorDlg*>(theApp.m_pMainWnd);
}

void CTrafficMonitorDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CTrafficMonitorDlg, CDialog)
    ON_WM_TIMER()
    ON_COMMAND(ID_NETWORK_INFO, &CTrafficMonitorDlg::OnNetworkInfo)
    ON_WM_CLOSE()
    ON_MESSAGE(MY_WM_NOTIFYICON, &CTrafficMonitorDlg::OnNotifyIcon)
    ON_COMMAND(ID_SHOW_NOTIFY_ICON, &CTrafficMonitorDlg::OnShowNotifyIcon)
    ON_WM_DESTROY()
    ON_COMMAND(ID_SHOW_TASK_BAR_WND, &CTrafficMonitorDlg::OnShowTaskBarWnd)
    ON_COMMAND(ID_APP_EXIT, &CTrafficMonitorDlg::OnClose)
    ON_COMMAND(ID_APP_ABOUT, &CTrafficMonitorDlg::OnAppAbout)
    ON_COMMAND(ID_SHOW_CPU_MEMORY2, &CTrafficMonitorDlg::OnShowCpuMemory2)
    ON_REGISTERED_MESSAGE(m_WM_TASKBARCREATED, &CTrafficMonitorDlg::OnTaskBarCreated)
    ON_COMMAND(ID_TRAFFIC_HISTORY, &CTrafficMonitorDlg::OnTrafficHistory)
    ON_COMMAND(ID_OPTIONS2, &CTrafficMonitorDlg::OnOptions2)
    ON_MESSAGE(WM_EXITMENULOOP, &CTrafficMonitorDlg::OnExitmenuloop)
    ON_COMMAND(ID_CHANGE_NOTIFY_ICON, &CTrafficMonitorDlg::OnChangeNotifyIcon)
    ON_COMMAND(ID_CHECK_UPDATE, &CTrafficMonitorDlg::OnCheckUpdate)
    ON_MESSAGE(WM_TASKBAR_MENU_POPED_UP, &CTrafficMonitorDlg::OnTaskbarMenuPopedUp)
    ON_COMMAND(ID_SHOW_NET_SPEED, &CTrafficMonitorDlg::OnShowNetSpeed)
    ON_WM_QUERYENDSESSION()
    ON_MESSAGE(WM_ENDSESSION, &CTrafficMonitorDlg::OnEndSessionMessage)
    ON_MESSAGE(WM_DPICHANGED, &CTrafficMonitorDlg::OnDpichanged)
    ON_MESSAGE(WM_TASKBAR_WND_CLOSED, &CTrafficMonitorDlg::OnTaskbarWndClosed)
    ON_MESSAGE(WM_MONITOR_INFO_UPDATED, &CTrafficMonitorDlg::OnMonitorInfoUpdated)
    ON_MESSAGE(WM_DISPLAYCHANGE, &CTrafficMonitorDlg::OnDisplaychange)
    ON_MESSAGE(WM_REOPEN_TASKBAR_WND, &CTrafficMonitorDlg::OnReopenTaksbarWnd)
    ON_COMMAND(ID_OPEN_TASK_MANAGER, &CTrafficMonitorDlg::OnOpenTaskManager)
    ON_MESSAGE(WM_SETTINGS_APPLIED, &CTrafficMonitorDlg::OnSettingsApplied)
    ON_COMMAND(ID_DISPLAY_SETTINGS, &CTrafficMonitorDlg::OnDisplaySettings)
    ON_COMMAND(ID_REFRESH_CONNECTION_LIST, &CTrafficMonitorDlg::OnRefreshConnectionList)
    ON_MESSAGE(WM_TABLET_QUERYSYSTEMGESTURESTATUS, &CTrafficMonitorDlg::OnTabletQuerysystemgesturestatus)
    ON_WM_POWERBROADCAST()
    ON_WM_DWMCOLORIZATIONCOLORCHANGED()
END_MESSAGE_MAP()









void CTrafficMonitorDlg::GetScreenSize()
{
    m_screen_size.cx = GetSystemMetrics(SM_CXSCREEN);
    m_screen_size.cy = GetSystemMetrics(SM_CYSCREEN);

    //::SystemParametersInfo(SPI_GETWORKAREA, 0, &m_screen_rect, 0);   // 获得工作区大小

    //获取所有屏幕工作区的大小
    m_last_screen_rects = m_screen_rects;
    m_screen_rects.clear();
    Monitors monitors;
    for (auto& a : monitors.monitorinfos)
    {
        m_screen_rects.push_back(a.rcWork);
    }
}






void CTrafficMonitorDlg::IniConnectionMenu(CMenu* pMenu)
{
    ASSERT(pMenu != nullptr);
    if (pMenu != nullptr)
    {
        //先将ID_SELECT_ALL_CONNECTION后面的所有菜单项删除
        int start_pos = CCommon::GetMenuItemPosition(pMenu, ID_SELECT_ALL_CONNECTION) + 1;
        while (pMenu->GetMenuItemCount() > start_pos)
        {
            pMenu->DeleteMenu(start_pos, MF_BYPOSITION);
        }

        CString connection_descr;
        const auto connections = m_monitor_service.ConnectionsSnapshot();
        for (size_t i{}; i < connections.size(); i++)
        {
            connection_descr = CCommon::StrToUnicode(connections[i].description.c_str()).c_str();
            pMenu->AppendMenu(MF_STRING | MF_ENABLED, ID_SELECT_ALL_CONNECTION + i + 1, connection_descr);
        }

        //添加“刷新网络列表”命令
        pMenu->AppendMenu(MF_SEPARATOR);
        pMenu->AppendMenu(MF_STRING | MF_ENABLED, ID_REFRESH_CONNECTION_LIST, CCommon::LoadText(IDS_REFRESH_CONNECTION_LIST));
    }
}

void CTrafficMonitorDlg::IniTaskBarConnectionMenu()
{
    //向“选择网络连接”子菜单项添加项目
    IniConnectionMenu(theApp.m_taskbar_menu.GetSubMenu(0)->GetSubMenu(0));
}

void CTrafficMonitorDlg::SetConnectionMenuState(CMenu* pMenu)
{
    const auto network_state = m_monitor_service.GetNetworkStateSnapshot();
    const int item_count = static_cast<int>(network_state.connections.size()) + 1;
    const int selected_index = network_state.selected_index;
    if (theApp.m_cfg_data.m_select_all)
        pMenu->CheckMenuRadioItem(0, item_count, 1, MF_BYPOSITION | MF_CHECKED);
    else if (theApp.m_cfg_data.m_auto_select)
        pMenu->CheckMenuRadioItem(0, item_count, 0, MF_BYPOSITION | MF_CHECKED);
    else
        pMenu->CheckMenuRadioItem(0, item_count, selected_index + 2, MF_BYPOSITION | MF_CHECKED);

    if (!theApp.m_cfg_data.m_select_all)
        pMenu->SetDefaultItem(selected_index + 2, TRUE);
    else
        pMenu->SetDefaultItem(-1, TRUE);
}

bool CTrafficMonitorDlg::CloseTaskBarWnd()
{
    if (m_tBarDlg == nullptr)
    {
        KillTimer(kTaskbarRestoreRetryTimer);
        return true;
    }

    // CloseAndRestore verifies every captured Explorer mutation. A failure
    // leaves the dialog alive so the next retry can restore the same state.
    if (!m_tBarDlg->CloseAndRestore())
    {
        ScheduleTaskbarRestoreRetry();
        return false;
    }

    if (IsTaskbarWndValid())
        m_tBarDlg->OnCancel();
    delete m_tBarDlg;
    m_tBarDlg = nullptr;
    theApp.m_taskbar_data.update_layered_window_error_code = 0;
    KillTimer(kTaskbarRestoreRetryTimer);
    return true;
}

void CTrafficMonitorDlg::OpenTaskBarWnd()
{
    // A pending process close may only restore/tear down existing state; it
    // must never create a new Explorer attachment.
    if (m_closeInProgress)
        return;

    // Never replace an instance that may still be responsible for restoring
    // Explorer state.
    if (m_tBarDlg != nullptr)
        return;

    // Refresh the cached Windows 11 taskbar detection before selecting a host.
    theApp.CheckWindows11Taskbar();
    if (theApp.m_win_version.IsWine())
        m_tBarDlg = new CWineTaskbarDlg();
    else if (theApp.IsWindows11Taskbar())
        m_tBarDlg = new CWin11TaskbarDlg();
    else
        m_tBarDlg = new CClassicalTaskbarDlg();

    CSupportedRenderEnums supported_render_enums{};
    CTaskBarDlg::DisableRenderFeatureIfNecessary(supported_render_enums);
    const auto render_type = supported_render_enums.GetAutoFitEnum();
    BOOL created{};
    switch (render_type)
    {
        using namespace DrawCommonHelper;
    case RenderType::D2D1_WITH_DCOMPOSITION:
        created = m_tBarDlg->Create(IDD_TASK_BAR_DIALOG_NOREDIRECTIONBITMAP, this);
        break;
    default:
        created = m_tBarDlg->Create(IDD_TASK_BAR_DIALOG, this);
        break;
    }

    if (!created)
    {
        // Create can fail after the dialog has already captured taskbar state.
        // Preserve it for retry if that state cannot yet be verified restored.
        if (!m_tBarDlg->CloseAndRestore())
        {
            ScheduleTaskbarRestoreRetry();
            return;
        }
        if (IsTaskbarWndValid())
            m_tBarDlg->OnCancel();
        delete m_tBarDlg;
        m_tBarDlg = nullptr;
        return;
    }

    m_tBarDlg->ShowWindow(SW_SHOW);
}

void CTrafficMonitorDlg::ScheduleTaskbarRestoreRetry()
{
    if (GetSafeHwnd() != NULL)
        SetTimer(kTaskbarRestoreRetryTimer, kRetryIntervalMs, NULL);
}

void CTrafficMonitorDlg::ScheduleCloseRetry()
{
    if (GetSafeHwnd() != NULL)
        SetTimer(kCloseRetryTimer, kRetryIntervalMs, NULL);
}

void CTrafficMonitorDlg::AddNotifyIcon()
{
    const bool reopen_taskbar = theApp.m_cfg_data.m_show_task_bar_wnd;
    if (reopen_taskbar && !CloseTaskBarWnd())
        return;

    // Add the notification-area icon.
    ::Shell_NotifyIcon(NIM_ADD, &m_ntIcon);
    if (reopen_taskbar)
        OpenTaskBarWnd();
}

void CTrafficMonitorDlg::DeleteNotifyIcon()
{
    const bool reopen_taskbar = theApp.m_cfg_data.m_show_task_bar_wnd;
    if (reopen_taskbar && !CloseTaskBarWnd())
        return;

    // Remove the notification-area icon.
    ::Shell_NotifyIcon(NIM_DELETE, &m_ntIcon);
    if (reopen_taskbar)
        OpenTaskBarWnd();
}

void CTrafficMonitorDlg::ShowNotifyTip(const wchar_t* title, const wchar_t* message)
{
    //要显示通知区提示，必须先将通知区图标显示出来
    if (!theApp.m_general_data.show_notify_icon)
    {
        //添加通知栏图标
        AddNotifyIcon();
    }
    //显示通知提示
    m_ntIcon.uFlags |= NIF_INFO;
    //wcscpy_s(m_ntIcon.szInfo, message ? message : _T(""));
    //wcscpy_s(m_ntIcon.szInfoTitle, title ? title : _T(""));
    CCommon::WStringCopy(m_ntIcon.szInfo, 256, message);
    CCommon::WStringCopy(m_ntIcon.szInfoTitle, 64, title);
    ::Shell_NotifyIcon(NIM_MODIFY, &m_ntIcon);
    m_ntIcon.uFlags &= ~NIF_INFO;

    //如果不显示通知区域图标，则在弹出通知的一段时间后删除通知区图标
    if (!theApp.m_general_data.show_notify_icon)
    {
        //延迟一定时间后删除通知区图标
        KillTimer(DELETE_NOTIFY_ICON_TIMER);
        SetTimer(DELETE_NOTIFY_ICON_TIMER, 8000, NULL);
    }
}

void CTrafficMonitorDlg::UpdateNotifyIconTip()
{
    CString strTip;         //鼠标指向图标时显示的提示
#ifdef _DEBUG
    strTip = CCommon::LoadText(IDS_TRAFFICMONITOR, _T(" (Debug)"));
#else
    strTip = CCommon::LoadText(IDS_TRAFFICMONITOR);
#endif

    CString in_speed = CCommon::DataSizeToString(theApp.m_in_speed);
    CString out_speed = CCommon::DataSizeToString(theApp.m_out_speed);

    strTip += CCommon::StringFormat(_T("\r\n<%1%>: <%2%>/s"), { CCommon::LoadText(IDS_UPLOAD), out_speed });
    strTip += CCommon::StringFormat(_T("\r\n<%1%>: <%2%>/s"), { CCommon::LoadText(IDS_DOWNLOAD), in_speed });
    strTip += CCommon::StringFormat(_T("\r\nCPU: <%1%> %"), { theApp.m_cpu_usage });
    strTip += CCommon::StringFormat(_T("\r\n<%1%>: <%2%> %"), { CCommon::LoadText(IDS_MEMORY), theApp.m_memory_usage });
    if (IsTemperatureNeeded())
    {
        if (theApp.m_general_data.IsHardwareEnable(HI_GPU) && theApp.m_gpu_usage >= 0)
            strTip += CCommon::StringFormat(_T("\r\n<%1%>: <%2%> %"), { CCommon::LoadText(IDS_GPU_USAGE), theApp.m_gpu_usage });
        if (theApp.m_general_data.IsHardwareEnable(HI_CPU) && theApp.m_cpu_temperature > 0)
            strTip += CCommon::StringFormat(_T("\r\n<%1%>: <%2%> °C"), { CCommon::LoadText(IDS_CPU_TEMPERATURE), static_cast<int>(theApp.m_cpu_temperature) });
        if (theApp.m_general_data.IsHardwareEnable(HI_GPU) && theApp.m_gpu_temperature > 0)
            strTip += CCommon::StringFormat(_T("\r\n<%1%>: <%2%> °C"), { CCommon::LoadText(IDS_GPU_TEMPERATURE), static_cast<int>(theApp.m_gpu_temperature) });
        if (theApp.m_general_data.IsHardwareEnable(HI_HDD) && theApp.m_hdd_temperature > 0)
            strTip += CCommon::StringFormat(_T("\r\n<%1%>: <%2%> °C"), { CCommon::LoadText(IDS_HDD_TEMPERATURE), static_cast<int>(theApp.m_hdd_temperature) });
        if (theApp.m_general_data.IsHardwareEnable(HI_MBD) && theApp.m_main_board_temperature > 0)
            strTip += CCommon::StringFormat(_T("\r\n<%1%>: <%2%> °C"), { CCommon::LoadText(IDS_MAINBOARD_TEMPERATURE), static_cast<int>(theApp.m_main_board_temperature) });
        if (theApp.m_general_data.IsHardwareEnable(HI_HDD) && theApp.m_hdd_usage >= 0)
            strTip += CCommon::StringFormat(_T("\r\n<%1%>: <%2%> %"), { CCommon::LoadText(IDS_HDD_USAGE), theApp.m_hdd_usage });
    }

    CCommon::WStringCopy(m_ntIcon.szTip, 128, strTip);
    ::Shell_NotifyIcon(NIM_MODIFY, &m_ntIcon);

}

void CTrafficMonitorDlg::SaveHistoryTraffic()
{
    // 使用增量保存，只更新第一行和今天的记录，减少I/O操作
    m_monitor_service.SaveHistoryTraffic(true);
}

bool CTrafficMonitorDlg::SaveHistoryTrafficFull()
{
    return m_monitor_service.SaveHistoryTraffic(false);
}

void CTrafficMonitorDlg::LoadHistoryTraffic()
{
    auto& history_traffic = m_monitor_service.HistoryFile();
    history_traffic.Load();
    CHistoryTrafficFile backup_file(theApp.m_history_traffic_path + L".bak");
    backup_file.Load();

    const size_t size_before = history_traffic.Size();
    if (backup_file.IsLoadValid() && history_traffic.IsLoadFailed())
    {
        if (history_traffic.RecoverFromBackup(backup_file))
        {
            const size_t size_after = history_traffic.Size();
            const size_t recovered_count = size_after > size_before ? size_after - size_before : 0;
            if (recovered_count > 0)
            {
                CString log_info = CCommon::LoadTextFormat(IDS_HISTORY_TRAFFIC_LOST_ERROR_LOG, { size_before, recovered_count });
                CCommon::WriteLog(log_info, theApp.m_log_path.c_str());
            }
        }
    }
    else if (backup_file.IsLoadValid())
    {
        history_traffic.Merge(backup_file, true);
        const size_t size_after = history_traffic.Size();
        const size_t recovered_count = size_after > size_before ? size_after - size_before : 0;
        if (recovered_count > 0)
        {
            CString log_info = CCommon::LoadTextFormat(IDS_HISTORY_TRAFFIC_LOST_ERROR_LOG, { size_before, recovered_count });
            CCommon::WriteLog(log_info, theApp.m_log_path.c_str());
        }
    }
    // The service owns the sampling baseline. Seed it only after recovery has
    // merged the final history file and before the monitor worker starts.
    m_monitor_service.InitializeTodayTrafficFromHistory();
    const MonitorSnapshot snapshot = m_monitor_service.Snapshot();
    theApp.m_today_up_traffic = snapshot.today_up_traffic;
    theApp.m_today_down_traffic = snapshot.today_down_traffic;
}

void CTrafficMonitorDlg::BackupHistoryTrafficFile(bool history_save_succeeded)
{
    if (!history_save_succeeded)
        return;
    // 确保文件已保存到磁盘
    wstring latest_file_path = theApp.m_history_traffic_path;
    wstring backup_file_path = latest_file_path + L".bak";
    
    // 检查当前文件是否存在
    if (!CCommon::FileExist(latest_file_path.c_str()))
    {
        return; // 当前文件不存在，无需备份
    }
    
    // 直接备份当前文件（当前文件是最新的，包含最新的数据）
    // 备份文件可能包含"未来"的记录，但恢复时会自动清理，所以总是备份当前文件即可
    CopyFile(latest_file_path.c_str(), backup_file_path.c_str(), FALSE);
}

void CTrafficMonitorDlg::_OnOptions(int tab, CWnd* pParent)
{
    COptionsDlg optionsDlg(tab, pParent);

    //将选项设置数据传递给选项设置对话框
    if (COptionsDlg::GetUniqueHandel(OPTION_DLG_NAME) == NULL)     //确保此时选项设置对话框已经关闭
    {
        optionsDlg.m_tab1_dlg.m_data = theApp.m_taskbar_data;
        optionsDlg.m_tab2_dlg.m_data = theApp.m_general_data;
    }

    if (optionsDlg.DoModal() == IDOK)
    {
        ApplySettings(optionsDlg);
    }
}

void CTrafficMonitorDlg::ApplySettings(COptionsDlg& optionsDlg)
{
    bool is_hardware_monitor_item_changed = (optionsDlg.m_tab2_dlg.m_data.hardware_monitor_item != theApp.m_general_data.hardware_monitor_item);
    bool is_show_notify_icon_changed = (optionsDlg.m_tab2_dlg.m_data.show_notify_icon != theApp.m_general_data.show_notify_icon);
    bool is_connections_hide_changed = (optionsDlg.m_tab2_dlg.m_data.connections_hide.data() != theApp.m_general_data.connections_hide.data());
    bool d2d_turned_on = (theApp.m_taskbar_data.disable_d2d && !optionsDlg.m_tab1_dlg.m_data.disable_d2d);
    //需要重新关闭再打开任务栏窗口的情况
    bool taskbar_changed = (theApp.m_taskbar_data.show_taskbar_wnd_in_secondary_display != optionsDlg.m_tab1_dlg.m_data.show_taskbar_wnd_in_secondary_display
        || theApp.m_taskbar_data.secondary_display_index != optionsDlg.m_tab1_dlg.m_data.secondary_display_index
        || theApp.m_taskbar_data.disable_d2d != optionsDlg.m_tab1_dlg.m_data.disable_d2d
        || theApp.m_taskbar_data.IsTaskbarTransparent() != optionsDlg.m_tab1_dlg.m_data.IsTaskbarTransparent()
        || theApp.m_taskbar_data.auto_set_background_color != optionsDlg.m_tab1_dlg.m_data.auto_set_background_color
        );

    theApp.m_taskbar_data = optionsDlg.m_tab1_dlg.m_data;
    theApp.m_general_data = optionsDlg.m_tab2_dlg.m_data;

    CGeneralSettingsDlg::CheckTaskbarDisplayItem();

    // The connection rebuild below consults MonitorService::Config. Publish
    // the new options first so visibility/hidden-interface changes are applied
    // to this rebuild rather than being deferred until a later refresh.
    m_monitor_service.ApplyConfig(GetMonitorConfig());

    //打开了D2D渲染后自动开启“背景透明”并关闭“根据任务栏颜色自动设置背景色”
    if (d2d_turned_on)
    {
        theApp.m_taskbar_data.SetTaskabrTransparent(true);
        theApp.m_taskbar_data.auto_set_background_color = false;
    }

    //CTaskBarDlg::SaveConfig();
    if (IsTaskbarWndValid() && !m_tBarDlg->IsRestorationPending())
    {
        m_tBarDlg->ApplySettings();
        if (taskbar_changed)
        {
            if (CloseTaskBarWnd())
                OpenTaskBarWnd();
        }
        else if (IsTaskbarWndValid())
        {
            m_tBarDlg->WidthChanged();
        }

        if (IsTaskbarWndValid() && !m_tBarDlg->IsRestorationPending())
            m_tBarDlg->ApplyWindowTransparentColor();
    }

    if (optionsDlg.m_tab2_dlg.IsAutoRunModified())
    {
        if (!theApp.SetAutoRun(theApp.m_general_data.auto_run, theApp.m_general_data.auto_run_by_task_scheduler))
            MessageBox(CCommon::LoadText(IDS_SET_AUTO_RUN_FAILED_WARNING), NULL, MB_ICONWARNING | MB_OK);
    }

    if (optionsDlg.m_tab2_dlg.IsShowAllInterfaceModified() || is_connections_hide_changed)
    {
        m_monitor_service.InitConnections();

        //重新初始化连接后，如果需要延迟自动选择，则设置延迟定时器
        if (m_monitor_service.ConsumeDelayedAutoSelectPending())
            SetTimer(DELAY_TIMER, 5000, NULL);
    }

    if (optionsDlg.m_tab2_dlg.IsMonitorTimeSpanModified())      //如果监控时间间隔改变了，则重设定时器
    {
        KillTimer(MONITOR_TIMER);
        SetTimer(MONITOR_TIMER, theApp.m_general_data.monitor_time_span, NULL);
    }

#ifndef WITHOUT_TEMPERATURE
    if (is_hardware_monitor_item_changed)
    {
        theApp.InvalidateOpenHardwareInitialization();
        //如果关闭了硬件监控，则析构硬件监控类
        if (theApp.m_general_data.hardware_monitor_item == 0)
        {
            CSingleLock sync(&theApp.m_minitor_lib_critical, TRUE);
            theApp.m_pMonitor.reset();
        }
        else
        {
            bool monitor_initialized = false;
            {
                CSingleLock sync(&theApp.m_minitor_lib_critical, TRUE);
                monitor_initialized = theApp.m_pMonitor != nullptr;
            }
            if (monitor_initialized)
                theApp.UpdateOpenHardwareMonitorEnableState();
            else if (IsTemperatureNeeded())
                theApp.InitOpenHardwareLibInThread();
        }
    }
#endif

    if (is_show_notify_icon_changed)
    {
        if (theApp.IsForceShowNotifyIcon())
            theApp.m_general_data.show_notify_icon = true;
        if (theApp.m_general_data.show_notify_icon)
            AddNotifyIcon();
        else
            DeleteNotifyIcon();
    }

    //同步采样引擎配置
    theApp.SaveConfig();
    theApp.SaveGlobalConfig();
}





bool CTrafficMonitorDlg::IsTaskbarWndValid() const
{
    return m_tBarDlg != nullptr && ::IsWindow(m_tBarDlg->GetSafeHwnd());
}

void CTrafficMonitorDlg::TaskbarShowHideItem(DisplayItem type)
{
    if (IsTaskbarWndValid())
    {
        bool show = (theApp.m_taskbar_data.display_item.Contains(type));
        if (show)
        {
            theApp.m_taskbar_data.display_item.Remove(type);
        }
        else
        {
            theApp.m_taskbar_data.display_item.Add(type);
        }
        //CloseTaskBarWnd();
        //OpenTaskBarWnd();
        m_tBarDlg->WidthChanged();
    }
}



bool CTrafficMonitorDlg::IsTemperatureNeeded() const
{
    //判断是否需要从OpenHardwareMonitor获取信息。
    ////只有主窗口和任务栏窗口中CPU温度、显卡利用率、显卡温度、硬盘温度和主板温度中至少有一个要显示，才返回true
    //bool needed = false;
    //if (theApp.m_cfg_data.m_show_task_bar_wnd && IsTaskbarWndValid())
    //{
    //    needed |= m_tBarDlg->IsShowCpuTemperature();
    //    needed |= m_tBarDlg->IsShowGpu();
    //    needed |= m_tBarDlg->IsShowGpuTemperature();
    //    needed |= m_tBarDlg->IsShowHddTemperature();
    //    needed |= m_tBarDlg->IsShowMainboardTemperature();
    //    needed |= (::IsWindow(m_tBarDlg->m_tool_tips.GetSafeHwnd()) && m_tBarDlg->m_tool_tips.IsWindowVisible());
    //}

    //if (!theApp.m_cfg_data.m_hide_main_window)
    //{
    //    const CSkinFile::Layout& skin_layout{ theApp.m_cfg_data.m_show_more_info ? m_skin.GetLayoutInfo().layout_l : m_skin.GetLayoutInfo().layout_s }; //当前的皮肤布局
    //    needed |= skin_layout.GetItem(TDI_CPU_TEMP).show;
    //    needed |= skin_layout.GetItem(TDI_GPU_USAGE).show;
    //    needed |= skin_layout.GetItem(TDI_GPU_TEMP).show;
    //    needed |= skin_layout.GetItem(TDI_HDD_TEMP).show;
    //    needed |= skin_layout.GetItem(TDI_MAIN_BOARD_TEMP).show;
    //    needed |= (::IsWindow(m_tool_tips.GetSafeHwnd()) && m_tool_tips.IsWindowVisible());
    //}
    //return needed;

    return theApp.m_general_data.IsHardwareEnable(HI_CPU) || theApp.m_general_data.IsHardwareEnable(HI_GPU)
        || theApp.m_general_data.IsHardwareEnable(HI_HDD) || theApp.m_general_data.IsHardwareEnable(HI_MBD);
}

#ifndef WITHOUT_TEMPERATURE
// CHardwareDataProvider 实现（封装 OpenHardwareMonitor 访问）
bool CTrafficMonitorDlg::CHardwareDataProvider::IsAvailable() const
{
    CSingleLock sync(&theApp.m_minitor_lib_critical, TRUE);
    return theApp.m_pMonitor != nullptr;
}

void CTrafficMonitorDlg::CHardwareDataProvider::Acquire()
{
    HardwareSnapshot snapshot;
    bool acquired = false;
    {
        CSingleLock sync(&theApp.m_minitor_lib_critical, TRUE);
        const auto monitor = theApp.m_pMonitor;
        if (monitor != nullptr)
        {
            auto acquire_hardware_info = [&]() {
                __try
                {
                    monitor->GetHardwareInfo();
                    snapshot.cpu_temperature = monitor->CpuTemperature();
                    snapshot.cpu_freq = monitor->CpuFreq();
                    snapshot.gpu_temperature = monitor->GpuTemperature();
                    snapshot.hdd_temperature = monitor->HDDTemperature();
                    snapshot.mainboard_temperature = monitor->MainboardTemperature();
                    snapshot.gpu_usage = monitor->GpuUsage();
                    snapshot.all_cpu_temperature = monitor->AllCpuTemperature();
                    snapshot.all_hdd_temperature = monitor->AllHDDTemperature();
                    snapshot.all_hdd_usage = monitor->AllHDDUsage();
                    acquired = true;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    // Do not show UI while the sampling worker owns the monitor
                    // lock: shutdown and settings changes can both be waiting on
                    // the UI thread. The default snapshot marks this sample as
                    // unavailable and the next timer tick may retry safely.
                }
            };
            acquire_hardware_info();
        }
    }

    m_snapshot = std::move(snapshot);
    if (!acquired)
    {
        if (!m_hardware_acquisition_failed)
            ::OutputDebugStringW(L"TaskbarMon: hardware monitor acquisition failed.\n");
        m_hardware_acquisition_failed = true;
    }
    else
    {
        m_hardware_acquisition_failed = false;
    }
}

float CTrafficMonitorDlg::CHardwareDataProvider::CpuTemperature() const
{
    return m_snapshot.cpu_temperature;
}

float CTrafficMonitorDlg::CHardwareDataProvider::CpuFreq() const
{
    return m_snapshot.cpu_freq;
}

float CTrafficMonitorDlg::CHardwareDataProvider::GpuTemperature() const
{
    return m_snapshot.gpu_temperature;
}

float CTrafficMonitorDlg::CHardwareDataProvider::HddTemperature() const
{
    return m_snapshot.hdd_temperature;
}

float CTrafficMonitorDlg::CHardwareDataProvider::MainboardTemperature() const
{
    return m_snapshot.mainboard_temperature;
}

int CTrafficMonitorDlg::CHardwareDataProvider::GpuUsage() const
{
    return m_snapshot.gpu_usage;
}


std::map<std::wstring, float> CTrafficMonitorDlg::CHardwareDataProvider::AllCpuTemperature() const
{
    return m_snapshot.all_cpu_temperature;
}

std::map<std::wstring, float> CTrafficMonitorDlg::CHardwareDataProvider::AllHddTemperature() const
{
    return m_snapshot.all_hdd_temperature;
}

std::map<std::wstring, float> CTrafficMonitorDlg::CHardwareDataProvider::AllHddUsage() const
{
    return m_snapshot.all_hdd_usage;
}

#endif // !WITHOUT_TEMPERATURE

// CTrafficMonitorDlg 消息处理程序

BOOL CTrafficMonitorDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    // TODO: 在此添加额外的初始化代码
    SetWindowText(APP_NAME);
    //设置隐藏任务栏图标
    ModifyStyleEx(WS_EX_APPWINDOW, WS_EX_TOOLWINDOW);

    theApp.DPIFromWindow(this);
    //获取屏幕大小
    GetScreenSize();
    m_last_screen_rects = m_screen_rects;

    //初始化菜单
    theApp.InitMenuResourse();

    m_monitor_service.InitConnections();    //初始化连接

    //初始化连接后，如果需要延迟自动选择，则设置延迟定时器
    if (m_monitor_service.ConsumeDelayedAutoSelectPending())
        SetTimer(DELAY_TIMER, 5000, NULL);

    //载入通知区图标
    theApp.m_notify_icons[0] = (HICON)LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDI_NOFITY_ICON), IMAGE_ICON, theApp.DPI(16), theApp.DPI(16), LR_DEFAULTCOLOR | LR_CREATEDIBSECTION);
    theApp.m_notify_icons[1] = (HICON)LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDI_NOFITY_ICON2), IMAGE_ICON, theApp.DPI(16), theApp.DPI(16), LR_DEFAULTCOLOR | LR_CREATEDIBSECTION);
    theApp.m_notify_icons[2] = (HICON)LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDI_NOFITY_ICON3), IMAGE_ICON, theApp.DPI(16), theApp.DPI(16), LR_DEFAULTCOLOR | LR_CREATEDIBSECTION);
    theApp.m_notify_icons[3] = (HICON)LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDR_MAINFRAME), IMAGE_ICON, theApp.DPI(16), theApp.DPI(16), LR_DEFAULTCOLOR | LR_CREATEDIBSECTION);
    theApp.m_notify_icons[4] = (HICON)LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDI_NOFITY_ICON4), IMAGE_ICON, theApp.DPI(16), theApp.DPI(16), LR_DEFAULTCOLOR | LR_CREATEDIBSECTION);
    theApp.m_notify_icons[5] = (HICON)LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDI_NOTIFY_ICON5), IMAGE_ICON, theApp.DPI(16), theApp.DPI(16), LR_DEFAULTCOLOR | LR_CREATEDIBSECTION);

    //设置通知区域图标
    m_ntIcon.cbSize = sizeof(NOTIFYICONDATA);   //该结构体变量的大小
    if (theApp.m_cfg_data.m_notify_icon_selected < 0 || theApp.m_cfg_data.m_notify_icon_selected >= MAX_NOTIFY_ICON)
        theApp.m_cfg_data.m_notify_icon_selected = 0;
    m_ntIcon.hIcon = theApp.m_notify_icons[theApp.m_cfg_data.m_notify_icon_selected];       //设置图标
    m_ntIcon.hWnd = this->m_hWnd;               //接收托盘图标通知消息的窗口句柄
    CString atip;           //鼠标指向图标时显示的提示
#ifdef _DEBUG
    atip = CCommon::LoadText(IDS_TRAFFICMONITOR, _T(" (Debug)"));
#else
    atip = CCommon::LoadText(IDS_TRAFFICMONITOR);
#endif
    //wcscpy_s(m_ntIcon.szTip, 128, strTip);
    CCommon::WStringCopy(m_ntIcon.szTip, 128, atip.GetString());
    m_ntIcon.uCallbackMessage = MY_WM_NOTIFYICON;   //应用程序定义的消息ID号
    m_ntIcon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; //图标的属性：设置成员uCallbackMessage、hIcon、szTip有效
    if (theApp.m_general_data.show_notify_icon)
        ::Shell_NotifyIcon(NIM_ADD, &m_ntIcon); //在系统通知区域增加这个图标

    //载入流量历史记录
    LoadHistoryTraffic();

    //设置1000毫秒触发的定时器
    SetTimer(MAIN_TIMER, 1000, NULL);

    SetTimer(MONITOR_TIMER, theApp.m_general_data.monitor_time_span, NULL);
    if (!StartMonitorThread())
    {
        KillTimer(MONITOR_TIMER);
        MessageBox(_T("Unable to start the monitor worker. Monitoring has been disabled."), APP_NAME, MB_ICONERROR | MB_OK);
    }

    //获取启动时的时间
    GetLocalTime(&m_start_time);

    SetTimer(TASKBAR_TIMER, 100, NULL);

    //宿主窗口永远隐藏，只显示任务栏窗口和通知区图标
    ShowWindow(SW_HIDE);

    return TRUE;  // 除非将焦点设置到控件，否则返回 TRUE
}


//当用户拖动最小化窗口时系统调用此函数取得光标
//显示。

//计算指定秒数的时间内Monitor定时器会触发的次数



bool CTrafficMonitorDlg::StartMonitorThread()
{
    if (m_monitorThread != nullptr)
        return true;

    m_is_thread_exit.store(false);
    m_monitor_data_required.store(false);
    m_monitorWakeEvent.ResetEvent();
    m_threadExitEvent.ResetEvent();
    m_monitorNotifyWindow = GetSafeHwnd();
    {
        std::lock_guard<std::mutex> lock(m_pendingMonitorSnapshotMutex);
        m_hasPendingMonitorSnapshot = false;
        m_pendingMonitorRevision = 0;
    }

    CWinThread* monitor_thread = AfxBeginThread(
        MonitorThreadCallback,
        this,
        THREAD_PRIORITY_NORMAL,
        0,
        CREATE_SUSPENDED);
    if (monitor_thread == nullptr)
    {
        m_monitorNotifyWindow = NULL;
        return false;
    }

    monitor_thread->m_bAutoDelete = FALSE;
    m_monitorThread = monitor_thread;
    if (m_monitorThread->ResumeThread() == static_cast<DWORD>(-1))
    {
        delete m_monitorThread;
        m_monitorThread = nullptr;
        m_monitorNotifyWindow = NULL;
        return false;
    }
    return true;
}

void CTrafficMonitorDlg::RequestMonitorAcquisition()
{
    if (m_monitorThread == nullptr || m_is_thread_exit.load())
        return;

    m_monitor_data_required.store(true);
    m_monitorWakeEvent.SetEvent();
}

void CTrafficMonitorDlg::DoMonitorAcquisition()
{
    m_monitor_service.Sample();

    // The worker publishes a complete immutable copy. Only the UI thread
    // consumes it and updates legacy application state.
    const MonitorSnapshot snapshot = m_monitor_service.Snapshot();
    const uint64_t revision = m_monitor_service.Revision();
    {
        std::lock_guard<std::mutex> lock(m_pendingMonitorSnapshotMutex);
        m_pendingMonitorSnapshot = snapshot;
        m_pendingMonitorRevision = revision;
        m_hasPendingMonitorSnapshot = true;
    }

    if (m_monitorNotifyWindow != NULL)
        ::PostMessage(m_monitorNotifyWindow, WM_MONITOR_INFO_UPDATED, 0, 0);
}

UINT CTrafficMonitorDlg::MonitorThreadCallback(LPVOID dwUser)
{
    CTrafficMonitorDlg* pThis = static_cast<CTrafficMonitorDlg*>(dwUser);
    try
    {
        while (!pThis->m_is_thread_exit.load())
        {
            const DWORD wait_result = ::WaitForSingleObject(pThis->m_monitorWakeEvent.m_hObject, INFINITE);
            if (pThis->m_is_thread_exit.load())
                break;
            if (wait_result != WAIT_OBJECT_0)
                break;

            if (pThis->m_monitor_data_required.exchange(false))
                pThis->DoMonitorAcquisition();
        }
    }
    catch (...)
    {
        // A failed sample must still release the main window from the worker
        // lifetime contract during shutdown.
        pThis->m_is_thread_exit.store(true);
    }

    pThis->m_threadExitEvent.SetEvent();
    return 0;
}

bool CTrafficMonitorDlg::ExitMonitorThread(DWORD timeout_ms)
{
    CWinThread* monitor_thread = m_monitorThread;
    if (monitor_thread == nullptr)
        return true;

    m_is_thread_exit.store(true);
    m_monitor_data_required.store(false);
    m_monitorWakeEvent.SetEvent();

    if (monitor_thread->m_hThread == NULL ||
        ::WaitForSingleObject(monitor_thread->m_hThread, timeout_ms) != WAIT_OBJECT_0)
    {
        return false;
    }

    delete monitor_thread;
    m_monitorThread = nullptr;
    m_monitorNotifyWindow = NULL;
    return true;
}

void CTrafficMonitorDlg::OnTimer(UINT_PTR nIDEvent)
{
    // TODO: 在此添加消息处理程序代码和/或调用默认值
    if (nIDEvent == kCloseRetryTimer)
    {
        KillTimer(kCloseRetryTimer);
        if (m_closeInProgress)
        {
            // Permit the retry to re-enter OnClose while retaining the close
            // intent, so taskbar recovery cannot trigger a reopen in between.
            m_closeInProgress = false;
            OnClose();
        }
        return;
    }

    if (nIDEvent == kTaskbarRestoreRetryTimer)
    {
        if (m_tBarDlg == nullptr || !m_tBarDlg->IsRestorationPending())
        {
            KillTimer(kTaskbarRestoreRetryTimer);
        }
        else if (CloseTaskBarWnd())
        {
            if (theApp.m_cfg_data.m_show_task_bar_wnd && !m_closeInProgress)
                OpenTaskBarWnd();
        }
        return;
    }

    if (nIDEvent == DPI_CHANGE_TIMER)
    {
        CRect rect;
        GetWindowRect(rect);
        UINT dpi_x{};
        UINT dpi_y{};
        if (theApp.DPIFromRect(rect, &dpi_x, &dpi_y))
            m_pending_dpi = dpi_x;

        if (m_pending_dpi != 0)
        {
            TRACE("Dpi changed: %u\n", m_pending_dpi);
            theApp.SetDPI(m_pending_dpi);
            if (IsTaskbarWndValid() && !theApp.m_win_version.IsWindows8Point1OrLater())
            {
                m_tBarDlg->SetDPI(m_pending_dpi);
                m_tBarDlg->SetTextFont();
            }
        }
        KillTimer(DPI_CHANGE_TIMER);
        return;
    }

    if (nIDEvent == RESTART_TASKBAR_TIMER)
    {
        KillTimer(RESTART_TASKBAR_TIMER);
        if (!m_closeInProgress)
            PostMessage(WM_REOPEN_TASKBAR_WND, 0, 0);
        return;
    }

    if (nIDEvent == INIT_CONNECT_TIMER)
    {
        m_monitor_service.InitConnections();
        if (m_monitor_service.ConsumeDelayedAutoSelectPending())
            SetTimer(DELAY_TIMER, 5000, NULL);

        ++m_resume_connection_check_times;
        CString info = CCommon::LoadTextFormat(IDS_RESTORE_FROM_SLEEP_LOG,
            { m_monitor_service.RestartCount() });
        CCommon::WriteLog(info, theApp.m_log_path.c_str());

        if (m_monitor_service.ConnectionsSnapshot().empty())
        {
            if (m_resume_connection_check_times >= 20)
            {
                KillTimer(INIT_CONNECT_TIMER);
                m_resume_connection_check_times = 0;
            }
        }
        else
        {
            KillTimer(INIT_CONNECT_TIMER);
            m_resume_connection_check_times = 0;
        }
        return;
    }
    if (nIDEvent == MONITOR_TIMER)
    {
        RequestMonitorAcquisition();
    }

    if (nIDEvent == MAIN_TIMER)
    {
        m_timer_cnt++;
        if (m_first_start)      //这个if语句在程序启动后1秒执行
        {
            //打开任务栏窗口
            if (theApp.m_cfg_data.m_show_task_bar_wnd && m_tBarDlg == nullptr)
                OpenTaskBarWnd();

            m_first_start = false;
        }

        //每隔1秒钟就判断一下前台窗口是否全屏
        CRect rect;
        GetWindowRect(rect);
        HMONITOR h_current_monitor = ::MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
        m_is_foreground_fullscreen = CCommon::IsForegroundFullscreen(h_current_monitor);

        if (!m_menu_popuped)
        {
            if (m_timer_cnt % 30 == 26)     //每隔30秒钟保存一次设置（兼容原主窗口位置保存周期）
            {
                theApp.SaveConfig();
            }
        }

        if (m_timer_cnt % 2 == 1)       //每隔2秒钟获取一次屏幕区域
        {
            GetScreenSize();
        }

        //每隔10秒钟检测一次是否可以嵌入任务栏
        // Retry a failed attach by replacing the bar only after its previous
        // instance has verified a complete restore.
        if (!theApp.m_win_version.IsWine() && IsTaskbarWndValid() && m_timer_cnt % 10 == 1)
        {
            if (m_tBarDlg->GetCannotInsertToTaskBar() && m_insert_to_taskbar_cnt < MAX_INSERT_TO_TASKBAR_CNT)
            {
                if (CloseTaskBarWnd())
                {
                    OpenTaskBarWnd();
                    ++m_insert_to_taskbar_cnt;
                    if (IsTaskbarWndValid() && m_tBarDlg->GetCannotInsertToTaskBar() &&
                        m_insert_to_taskbar_cnt >= WARN_INSERT_TO_TASKBAR_CNT)
                    {
                        CString info = CCommon::LoadText(IDS_CONNOT_INSERT_TO_TASKBAR_ERROR_LOG);
                        info.Replace(_T("<%cnt%>"), CCommon::IntToString(m_insert_to_taskbar_cnt));
                        info.Replace(_T("<%error_code%>"), CCommon::IntToString(m_tBarDlg->GetErrorCode()));
                        CCommon::WriteLog(info, theApp.m_log_path.c_str());
                        if (m_cannot_insert_to_task_bar_warning)
                        {
                            m_cannot_insert_to_task_bar_warning = false;
                            MessageBox(CCommon::LoadText(IDS_CONNOT_INSERT_TO_TASKBAR,
                                CCommon::IntToString(m_tBarDlg->GetErrorCode())), NULL, MB_ICONWARNING);
                        }
                    }
                }
            }
            if (IsTaskbarWndValid() && !m_tBarDlg->GetCannotInsertToTaskBar())
                m_insert_to_taskbar_cnt = 0;
        }

        // Notification threshold helper.
        auto checkNotifyTip = [&]<typename T>(GeneralSettingData::NotifyTipSettings setting_data, T value, T& last_value, int& notify_time, LPCTSTR tip_str)
        {
            const T threshold = static_cast<T>(setting_data.tip_value);
            if (setting_data.enable)
            {
                if (last_value < threshold && value >= threshold && (m_timer_cnt - notify_time > static_cast<unsigned int>(theApp.m_notify_interval)))
                {
                    ShowNotifyTip(CCommon::LoadText(_T("TrafficMonitor "), IDS_NOTIFY, _T("")), tip_str);
                    notify_time = m_timer_cnt;
                }
                last_value = value;
            }
        };

        //检查是否要弹出内存使用率超出提示
        CString info;
        info = CCommon::LoadText(IDS_MEMORY_UDAGE_EXCEED) + CCommon::StringFormat(_T(" <%1%>%!"), { theApp.m_memory_usage });
        static int last_memory_usage;
        static int memory_usage_notify_time{ -theApp.m_notify_interval };       //记录上次弹出提示时的时间
        checkNotifyTip(theApp.m_general_data.memory_usage_tip, theApp.m_memory_usage, last_memory_usage, memory_usage_notify_time, info.GetString());

        //检查是否要弹出CPU温度使用率超出提示
        info = CCommon::LoadText(IDS_CPU_TEMPERATURE_EXCEED) + CCommon::StringFormat(_T(" <%1%>\x2103!"), { static_cast<int>(theApp.m_cpu_temperature) });
        static float last_cpu_temp;
        static int cpu_temp_notify_time{ -theApp.m_notify_interval };       //记录上次弹出提示时的时间
        checkNotifyTip(theApp.m_general_data.cpu_temp_tip, theApp.m_cpu_temperature, last_cpu_temp, cpu_temp_notify_time, info.GetString());

        //检查是否要弹出显卡温度使用率超出提示
        info = CCommon::LoadText(IDS_GPU_TEMPERATURE_EXCEED) + CCommon::StringFormat(_T(" <%1%>\x2103!"), { static_cast<int>(theApp.m_gpu_temperature) });
        static float last_gpu_temp;
        static int gpu_temp_notify_time{ -theApp.m_notify_interval };       //记录上次弹出提示时的时间
        checkNotifyTip(theApp.m_general_data.gpu_temp_tip, theApp.m_gpu_temperature, last_gpu_temp, gpu_temp_notify_time, info.GetString());

        //检查是否要弹出硬盘温度使用率超出提示
        info = CCommon::LoadText(IDS_HDD_TEMPERATURE_EXCEED) + CCommon::StringFormat(_T(" <%1%>\x2103!"), { static_cast<int>(theApp.m_hdd_temperature) });
        static float last_hdd_temp;
        static int hdd_temp_notify_time{ -theApp.m_notify_interval };       //记录上次弹出提示时的时间
        checkNotifyTip(theApp.m_general_data.hdd_temp_tip, theApp.m_hdd_temperature, last_hdd_temp, hdd_temp_notify_time, info.GetString());

        //检查是否要弹出主板温度使用率超出提示
        info = CCommon::LoadText(IDS_MBD_TEMPERATURE_EXCEED) + CCommon::StringFormat(_T(" <%1%>\x2103!"), { static_cast<int>(theApp.m_main_board_temperature) });
        static float last_main_board_temp;
        static int main_board_temp_notify_time{ -theApp.m_notify_interval };        //记录上次弹出提示时的时间
        checkNotifyTip(theApp.m_general_data.mainboard_temp_tip, theApp.m_main_board_temperature, last_main_board_temp, main_board_temp_notify_time, info.GetString());


        //检查是否要弹出流量使用超出提示
        if (theApp.m_general_data.traffic_tip_enable)
        {
            static __int64 last_today_traffic;
            __int64 traffic_tip_value;
            if (theApp.m_general_data.traffic_tip_unit == 0)
                traffic_tip_value = static_cast<__int64>(theApp.m_general_data.traffic_tip_value) * 1024 * 1024;
            else
                traffic_tip_value = static_cast<__int64>(theApp.m_general_data.traffic_tip_value) * 1024 * 1024 * 1024;

            __int64 today_traffic = theApp.m_today_up_traffic + theApp.m_today_down_traffic;
            if (last_today_traffic < traffic_tip_value && today_traffic >= traffic_tip_value)
            {
                CString info = CCommon::LoadText(IDS_TODAY_TRAFFIC_EXCEED, CCommon::DataSizeToString(today_traffic));
                ShowNotifyTip(CCommon::LoadText(_T("TrafficMonitor "), IDS_NOTIFY, _T("")), info.GetString());
            }
            last_today_traffic = today_traffic;
        }

        CWindowsSettingHelper::CheckWindows10LightTheme();        //每隔1秒钟检查一下当前系统是否为白色主题

        //根据当前Win10颜色模式自动切换任务栏颜色
        bool light_mode = CWindowsSettingHelper::IsWindows10LightTheme();
        if (theApp.m_last_light_mode != light_mode)
        {
            theApp.m_last_light_mode = light_mode;
            bool restart_taskbar_dlg{ false };
            if (theApp.m_taskbar_data.auto_adapt_light_theme)
            {
                int style_index = CWindowsSettingHelper::IsWindows10LightTheme() ? theApp.m_taskbar_data.light_default_style : theApp.m_taskbar_data.dark_default_style;
                theApp.m_taskbar_default_style.ApplyDefaultStyle(style_index, theApp.m_taskbar_data);
                theApp.SaveConfig();
                restart_taskbar_dlg = true;
            }
            bool is_taskbar_transparent{ theApp.m_taskbar_data.IsTaskbarTransparent()};
            if (!is_taskbar_transparent)
            {
                theApp.m_taskbar_data.SetTaskabrTransparent(false);
                restart_taskbar_dlg = true;
            }
            if (restart_taskbar_dlg && IsTaskbarWndValid())
            {
                m_tBarDlg->ApplyWindowTransparentColor();
                //CloseTaskBarWnd();
                //OpenTaskBarWnd();

                //写入调试日志
                if (theApp.m_debug_log)
                {
                    CString log_str;
                    log_str += _T("检测到 Windows10 深浅色变化。\n");
                    log_str += _T("IsWindows10LightTheme: ");
                    log_str += std::to_wstring(light_mode).c_str();
                    log_str += _T("\n");
                    log_str += _T("auto_adapt_light_theme: ");
                    log_str += std::to_wstring(theApp.m_taskbar_data.auto_adapt_light_theme).c_str();
                    log_str += _T("\n");
                    log_str += _T("is_taskbar_transparent: ");
                    log_str += std::to_wstring(is_taskbar_transparent).c_str();
                    log_str += _T("\n");
                    log_str += _T("taskbar_back_color: ");
                    log_str += std::to_wstring(theApp.m_taskbar_data.back_color).c_str();
                    log_str += _T("\n");
                    log_str += _T("taskbar_transparent_color: ");
                    log_str += std::to_wstring(theApp.m_taskbar_data.transparent_color).c_str();
                    log_str += _T("\n");
                    log_str += _T("taskbar_text_colors: ");
                    for (const auto& item : theApp.m_taskbar_data.text_colors)
                    {
                        log_str += std::to_wstring(item.second.label).c_str();
                        log_str += _T('|');
                        log_str += std::to_wstring(item.second.value).c_str();
                        log_str += _T(", ");
                    }
                    log_str += _T("\n");
                    CCommon::WriteLog(log_str, (theApp.m_config_dir + L".\\debug.log").c_str());
                }
            }

            //根据当前Win10颜色模式自动切换通知区图标
            if (theApp.m_cfg_data.m_notify_icon_auto_adapt)
            {
                int notify_icon_selected = theApp.m_cfg_data.m_notify_icon_selected;
                theApp.AutoSelectNotifyIcon();
                if (notify_icon_selected != theApp.m_cfg_data.m_notify_icon_selected)
                {
                    m_ntIcon.hIcon = theApp.m_notify_icons[theApp.m_cfg_data.m_notify_icon_selected];
                    if (theApp.m_general_data.show_notify_icon)
                    {
                        DeleteNotifyIcon();
                        AddNotifyIcon();
                    }
                }
            }
        }

        //根据任务栏颜色自动设置任务栏窗口背景色
        if (theApp.m_taskbar_data.auto_set_background_color && theApp.m_win_version.IsWindows8OrLater()
            && IsTaskbarWndValid() && theApp.m_taskbar_data.transparent_color != 0
            && !m_is_foreground_fullscreen && theApp.m_taskbar_data.disable_d2d
            && m_desktop_dc != nullptr)
        {
            CRect rect;
            ::GetWindowRect(m_tBarDlg->GetSafeHwnd(), rect);
            int pointx{ rect.left - 1 };
            if (theApp.m_taskbar_data.tbar_wnd_on_left && m_tBarDlg->IsTasksbarOnTopOrBottom())
                pointx = rect.right + 1;
            int pointy = rect.bottom;
            if (pointx < 0) pointx = 0;
            if (pointx >= m_screen_size.cx) pointx = m_screen_size.cx - 1;
            if (pointy < 0) pointy = 0;
            if (pointy >= m_screen_size.cy) pointy = m_screen_size.cy - 1;
            COLORREF color = ::GetPixel(m_desktop_dc, pointx, pointy);        //取任务栏窗口左侧1像素处的颜色作为背景色
            if (!CCommon::IsColorSimilar(color, theApp.m_taskbar_data.back_color) && (/*CWindowsSettingHelper::IsWindows10LightTheme() ||*/ color != 0))
            {
                bool is_taskbar_transparent{ theApp.m_taskbar_data.IsTaskbarTransparent()};
                theApp.m_taskbar_data.back_color = color;
                theApp.m_taskbar_data.SetTaskabrTransparent(is_taskbar_transparent);
                if (is_taskbar_transparent)
                    m_tBarDlg->ApplyWindowTransparentColor();
            }
        }

        //当检测到背景色和文字颜色都为黑色写入错误日志
        static bool erro_log_write{ false };
        if (theApp.m_taskbar_data.back_color == 0 && !theApp.m_taskbar_data.text_colors.empty() && theApp.m_taskbar_data.text_colors.begin()->second.label == 0)
        {
            if (!erro_log_write)
            {
                CString log_str;
                log_str.Format(_T("检查到背景色和文字颜色都为黑色。IsWindows10LightTheme: %d, 系统启动时间：%d/%.2d/%.2d %.2d:%.2d:%.2d"),
                    light_mode, m_start_time.wYear, m_start_time.wMonth, m_start_time.wDay, m_start_time.wHour, m_start_time.wMinute, m_start_time.wSecond);
                CCommon::WriteLog(log_str, theApp.m_log_path.c_str());
                erro_log_write = true;
            }
        }
        else
        {
            erro_log_write = false;
        }

        UpdateNotifyIconTip();


    }

    if (nIDEvent == DELAY_TIMER)
    {
        m_monitor_service.AutoSelect();
        KillTimer(DELAY_TIMER);
    }

    if (nIDEvent == TASKBAR_TIMER)
    {
        ++m_taskbar_timer_cnt;
        if (m_taskbar_timer_cnt % 5 == 0 && theApp.m_cfg_data.m_show_task_bar_wnd && theApp.m_taskbar_data.show_taskbar_wnd_in_secondary_display)
        {
            static int last_taskbar_num = 0;
            int taskbar_num = CTaskbarHelper::GetSecondaryTaskbarNum();
            //如果副显示器的任务栏数量发生变化，则重启任务栏窗口
            if (last_taskbar_num != taskbar_num)
            {
                last_taskbar_num = taskbar_num;
                //延迟一段时间后重启任务栏窗口
                KillTimer(RESTART_TASKBAR_TIMER);
                SetTimer(RESTART_TASKBAR_TIMER, 500, NULL);
            }
        }

        if (IsTaskbarWndValid())
        {
            //启动时就隐藏主窗体的情况下，无法收到dpichange消息，故需要手动检查
            //每次100ms*10执行一次屏幕DPI检查，并且尽可能少的检查操作系统版本
            if (m_taskbar_timer_cnt % 10 == 0 && theApp.m_win_version.IsWindows8Point1OrLater())
            {
                CTaskBarDlg::CheckWindowMonitorDPIAndHandle(*m_tBarDlg, [p_TaskBarDlg = m_tBarDlg](UINT new_dpi_x, UINT new_dpi_y)
                                                            {
                                                                // auto s_dpi = std::to_string(new_dpi_x);
                                                                // s_dpi += '\n';
                                                                // TRACE(s_dpi.c_str());
                                                                //考虑到任务栏窗口可能和主窗口不在同一个屏幕上，dpi可能不同
                                                                //设置DPI并刷新窗口
                                                                p_TaskBarDlg->SetDPI(new_dpi_x);
                                                                p_TaskBarDlg->SetTextFont();
                                                                p_TaskBarDlg->CalculateWindowSize(); });
            }

            m_tBarDlg->AdjustWindowPos();

            //R1+R5: 数据修订号变化时才重绘，并做最小间隔节流（合并绘制）
            //后台/全屏/远程会话时降低重绘频率以省电
            ULONGLONG min_interval = 150;
            if (m_is_foreground_fullscreen || (GetSystemMetrics(SM_REMOTESESSION) != 0))
                min_interval = 1000;
            ULONGLONG now = GetTickCount64();
            if (m_last_drawn_revision != theApp.m_monitor_revision && now - m_last_paint_time >= min_interval)
            {
                m_tBarDlg->Invalidate(FALSE);
                m_last_drawn_revision = theApp.m_monitor_revision;
                m_last_paint_time = now;
            }
        }
    }

    if (nIDEvent == DELETE_NOTIFY_ICON_TIMER)
    {
        DeleteNotifyIcon();
        KillTimer(DELETE_NOTIFY_ICON_TIMER);
    }

    CDialog::OnTimer(nIDEvent);
}






void CTrafficMonitorDlg::OnNetworkInfo()
{
    auto network_state = m_monitor_service.GetNetworkStateSnapshot();
    std::vector<NetWorkConection> valid_connections;
    valid_connections.reserve(network_state.connections.size());
    int selected_index = -1;
    for (size_t source_index = 0; source_index < network_state.connections.size(); ++source_index)
    {
        const NetWorkConection& connection = network_state.connections[source_index];
        if (connection.index < 0 ||
            static_cast<size_t>(connection.index) >= network_state.interface_rows.size())
        {
            continue;
        }

        if (static_cast<int>(source_index) == network_state.selected_index)
            selected_index = static_cast<int>(valid_connections.size());
        valid_connections.push_back(connection);
    }
    if (selected_index < 0 && !valid_connections.empty())
        selected_index = 0;

    CNetworkInfoDlg aDlg(&m_monitor_service,
        valid_connections,
        network_state.interface_rows,
        selected_index);
    aDlg.m_start_time = m_start_time;
    aDlg.DoModal();
    if (IsTaskbarWndValid())
        m_tBarDlg->m_tool_tips.SetWindowPos(&wndTopMost, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
}

bool CTrafficMonitorDlg::PrepareForDestroy()
{
    if (m_destroyWindowAuthorized)
        return true;
    if (m_closeInProgress)
        return false;

    m_closeInProgress = true;
    KillTimer(MAIN_TIMER);
    KillTimer(MONITOR_TIMER);
    KillTimer(TASKBAR_TIMER);
    KillTimer(DELAY_TIMER);
    KillTimer(DELETE_NOTIFY_ICON_TIMER);
    KillTimer(DPI_CHANGE_TIMER);
    KillTimer(RESTART_TASKBAR_TIMER);
    KillTimer(INIT_CONNECT_TIMER);

    // Explorer must be restored before any path can destroy this main window.
    if (!CloseTaskBarWnd())
    {
        ScheduleCloseRetry();
        return false;
    }

    // The worker can still be using this object and its history file. Do not
    // continue to MFC destruction unless it has actually terminated.
    if (!ExitMonitorThread(kMonitorThreadExitTimeoutMs))
    {
        ScheduleCloseRetry();
        return false;
    }

    KillTimer(kCloseRetryTimer);
    theApp.m_cannot_save_config_warning = true;
    theApp.m_cannot_save_global_config_warning = true;
    BackupHistoryTrafficFile(SaveHistoryTrafficFull());
    theApp.SaveConfig();
    theApp.SaveGlobalConfig();

    for (const auto& item : CBaseDialog::AllUniqueHandels())
    {
        ::SendMessage(item.second, WM_COMMAND, IDCANCEL, 0);
    }

    m_destroyWindowAuthorized = true;
    return true;
}

void CTrafficMonitorDlg::OnClose()
{
    if (PrepareForDestroy())
        CDialog::OnClose();
}

BOOL CTrafficMonitorDlg::DestroyWindow()
{
    // MFC modeless destruction can bypass WM_CLOSE. Do not let it tear down
    // the host until the same preflight has restored Explorer and joined the worker.
    if (!m_destroyWindowAuthorized && !PrepareForDestroy())
        return FALSE;
    return CDialog::DestroyWindow();
}


BOOL CTrafficMonitorDlg::OnCommand(WPARAM wParam, LPARAM lParam)
{
    // TODO: 在此添加专用代码和/或调用基类
    UINT uMsg = LOWORD(wParam);
    if (uMsg == IDOK || uMsg == IDCANCEL || uMsg == ID_APP_EXIT)
    {
        OnClose();
        return TRUE;
    }
    if (uMsg == ID_SELECT_ALL_CONNECTION)
    {
        m_monitor_service.SetSelectAll(true);
        theApp.m_cfg_data.m_select_all = true;
        theApp.m_cfg_data.m_auto_select = false;
    }
    //选择了“选择网络连接”子菜单中项目时的处理
    if (uMsg == ID_SELETE_CONNECTION)   //选择了“自动选择”菜单项
    {
        m_monitor_service.AutoSelect();
        theApp.m_cfg_data.m_auto_select = true;
        theApp.m_cfg_data.m_select_all = false;
        theApp.SaveConfig();
    }
    if (uMsg > ID_SELECT_ALL_CONNECTION && uMsg <= ID_SELECT_ALL_CONNECTION + m_monitor_service.ConnectionsSnapshot().size()) //选择了一个网络连接
    {
        int connection_index = uMsg - ID_SELECT_ALL_CONNECTION - 1;
        m_monitor_service.SelectConnection(connection_index);
        theApp.m_cfg_data.m_connection_name = m_monitor_service.SelectedConnectionName();
        theApp.m_cfg_data.m_auto_select = false;
        theApp.m_cfg_data.m_select_all = false;
        theApp.SaveConfig();
    }
#ifdef DEBUG
    if (uMsg == ID_CMD_TEST)
    {
        CTest::TestCommand();
        //ShowNotifyTip(CCommon::LoadText(_T("TrafficMonitor "), IDS_NOTIFY, _T("")), _T("测试通知"));

    }
#endif // DEBUG

    return CDialog::OnCommand(wParam, lParam);
    }




BOOL CTrafficMonitorDlg::PreTranslateMessage(MSG* pMsg)
{
    // TODO: 在此添加专用代码和/或调用基类
    //屏蔽按回车键和ESC键退出
    if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE) return TRUE;
    if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN) return TRUE;

    return CDialog::PreTranslateMessage(pMsg);
}







afx_msg LRESULT CTrafficMonitorDlg::OnNotifyIcon(WPARAM wParam, LPARAM lParam)
{
    bool dialog_exist{ false };
    HWND handle{};
    if (lParam == WM_LBUTTONDOWN || lParam == WM_RBUTTONUP || lParam == WM_LBUTTONDBLCLK)
    {
        for (const auto& item : CBaseDialog::AllUniqueHandels())
        {
            if (IsWindow(item.second))
            {
                dialog_exist = true;
                handle = item.second;
                break;
            }
        }
    }

    if (lParam == WM_RBUTTONUP)
    {
        if (dialog_exist)       //有打开的对话框时，点击通知区图标后将焦点设置到对话框
        {
            ::SetForegroundWindow(handle);
        }
        else
        {
            //在通知区点击右键弹出右键菜单
            if (IsTaskbarWndValid())        //如果显示了任务栏窗口，则在右击了通知区图标后将焦点设置到任务栏窗口
                m_tBarDlg->SetForegroundWindow();
            CPoint point1;  //定义一个用于确定光标位置的位置
            GetCursorPos(&point1);  //获取当前光标的位置，以便使得菜单可以跟随光标
            theApp.m_main_menu.GetSubMenu(0)->SetDefaultItem(-1);       //设置没有默认菜单项
            theApp.m_main_menu.GetSubMenu(0)->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point1.x, point1.y, this); //在指定位置显示弹出菜单
        }
    }
    return 0;
}


void CTrafficMonitorDlg::OnChangeNotifyIcon()
{
    // TODO: 在此添加命令处理程序代码
    CIconSelectDlg dlg(theApp.m_cfg_data.m_notify_icon_selected);
    dlg.SetAutoAdaptNotifyIcon(theApp.m_cfg_data.m_notify_icon_auto_adapt);
    if (dlg.DoModal() == IDOK)
    {
        theApp.m_cfg_data.m_notify_icon_selected = dlg.GetIconSelected();
        theApp.m_cfg_data.m_notify_icon_auto_adapt = dlg.AutoAdaptNotifyIcon();
        m_ntIcon.hIcon = theApp.m_notify_icons[theApp.m_cfg_data.m_notify_icon_selected];
        if (theApp.m_cfg_data.m_notify_icon_auto_adapt)
            theApp.AutoSelectNotifyIcon();
        if (theApp.m_general_data.show_notify_icon)
        {
            DeleteNotifyIcon();
            AddNotifyIcon();
        }
        theApp.SaveConfig();
    }
}

void CTrafficMonitorDlg::OnShowNotifyIcon()
{
    // TODO: 在此添加命令处理程序代码
    if (theApp.m_general_data.show_notify_icon)
    {
        DeleteNotifyIcon();
        theApp.m_general_data.show_notify_icon = false;
    }
    else
    {
        AddNotifyIcon();
        theApp.m_general_data.show_notify_icon = true;
    }
    theApp.SaveConfig();
}


void CTrafficMonitorDlg::OnDestroy()
{
    // WM_DESTROY is already irreversible: recovery and retry must have
    // completed in PrepareForDestroy(), before MFC begins native teardown.
    // Do not pretend a failed restoration can be retried after timers and the
    // owner HWND are gone.
    ASSERT(m_tBarDlg == nullptr);

    // OnClose normally joined the worker. This is intentionally idempotent so
    // an unexpected destruction path cannot leave the worker with a dangling
    // CTrafficMonitorDlg pointer.
    KillTimer(MAIN_TIMER);
    KillTimer(MONITOR_TIMER);
    KillTimer(TASKBAR_TIMER);
    KillTimer(DELAY_TIMER);
    KillTimer(DELETE_NOTIFY_ICON_TIMER);
    KillTimer(DPI_CHANGE_TIMER);
    KillTimer(RESTART_TASKBAR_TIMER);
    KillTimer(INIT_CONNECT_TIMER);
    KillTimer(kCloseRetryTimer);
    KillTimer(kTaskbarRestoreRetryTimer);
    ExitMonitorThread(INFINITE);

    ::Shell_NotifyIcon(NIM_DELETE, &m_ntIcon);
    CDialog::OnDestroy();
}




//任务栏窗口切换显示CPU和内存利用率时的处理
void CTrafficMonitorDlg::OnShowCpuMemory2()
{
    // TODO: 在此添加命令处理程序代码
    if (IsTaskbarWndValid())
    {
        bool show_cpu_memory = (theApp.m_taskbar_data.display_item.Contains(TDI_CPU) || theApp.m_taskbar_data.display_item.Contains(TDI_MEMORY));
        if (show_cpu_memory)
        {
            theApp.m_taskbar_data.display_item.Remove(TDI_CPU);
            theApp.m_taskbar_data.display_item.Remove(TDI_MEMORY);
        }
        else
        {
            theApp.m_taskbar_data.display_item.Add(TDI_CPU);
            theApp.m_taskbar_data.display_item.Add(TDI_MEMORY);
        }
        //theApp.m_cfg_data.m_tbar_show_cpu_memory = !theApp.m_cfg_data.m_tbar_show_cpu_memory;
        //切换显示CPU和内存利用率时，删除任务栏窗口，再重新显示
        //CloseTaskBarWnd();
        //OpenTaskBarWnd();
        m_tBarDlg->WidthChanged();
    }
}




void CTrafficMonitorDlg::OnShowTaskBarWnd()
{
    // Persist the requested state before attempting restoration. If Explorer
    // needs retries, the retry handler then knows whether to reopen or hide.
    const bool show_taskbar = !theApp.m_cfg_data.m_show_task_bar_wnd;
    theApp.m_cfg_data.m_show_task_bar_wnd = show_taskbar;

    if (m_tBarDlg != nullptr && !CloseTaskBarWnd())
    {
        theApp.SaveConfig();
        return;
    }

    if (show_taskbar)
    {
        OpenTaskBarWnd();
    }
    else if (!theApp.m_general_data.show_notify_icon && theApp.IsForceShowNotifyIcon())
    {
        AddNotifyIcon();
        theApp.m_general_data.show_notify_icon = true;
    }
    theApp.SaveConfig();
}
void CTrafficMonitorDlg::OnAppAbout()
{
    // TODO: 在此添加命令处理程序代码
    //弹出“关于”对话框
    CAboutDlg aDlg;
    aDlg.DoModal();
}


//当资源管理器重启时会触发此消息
LRESULT CTrafficMonitorDlg::OnTaskBarCreated(WPARAM wParam, LPARAM lParam)
{
    if (m_tBarDlg != nullptr && !CloseTaskBarWnd())
        return 0;

    if (theApp.m_general_data.show_notify_icon)
        ::Shell_NotifyIcon(NIM_ADD, &m_ntIcon);
    if (theApp.m_cfg_data.m_show_task_bar_wnd)
        OpenTaskBarWnd();
    return 0;
}

void CTrafficMonitorDlg::OnTrafficHistory()
{
    auto history_traffics = m_monitor_service.HistoryTrafficSnapshot();
    CHistoryTrafficDlg historyDlg(history_traffics);
    historyDlg.DoModal();
}








//通过任务栏窗口的右键菜单打开“选项”对话框
void CTrafficMonitorDlg::OnOptions2()
{
    CWnd* pParent = this;
    if (IsTaskbarWndValid())
        pParent = m_tBarDlg;
    _OnOptions(1, pParent);
}


afx_msg LRESULT CTrafficMonitorDlg::OnExitmenuloop(WPARAM wParam, LPARAM lParam)
{
    m_menu_popuped = false;
    return 0;
}






void CTrafficMonitorDlg::OnCheckUpdate()
{
    // TODO: 在此添加命令处理程序代码
    theApp.CheckUpdateInThread(true);
}


afx_msg LRESULT CTrafficMonitorDlg::OnTaskbarMenuPopedUp(WPARAM wParam, LPARAM lParam)
{
    //设置“选择连接”子菜单项中各单选项的选择状态
    SetConnectionMenuState(theApp.m_taskbar_menu.GetSubMenu(0)->GetSubMenu(0));
    return 0;
}


//任务栏窗口切换显示网速时的处理
void CTrafficMonitorDlg::OnShowNetSpeed()
{
    // TODO: 在此添加命令处理程序代码
    if (IsTaskbarWndValid())
    {
        bool show_net_speed = (theApp.m_taskbar_data.display_item.Contains(TDI_UP) || theApp.m_taskbar_data.display_item.Contains(TDI_DOWN));
        if (show_net_speed)
        {
            theApp.m_taskbar_data.display_item.Remove(TDI_UP);
            theApp.m_taskbar_data.display_item.Remove(TDI_DOWN);
        }
        else
        {
            theApp.m_taskbar_data.display_item.Add(TDI_UP);
            theApp.m_taskbar_data.display_item.Add(TDI_DOWN);
        }
        //CloseTaskBarWnd();
        //OpenTaskBarWnd();
        m_tBarDlg->WidthChanged();
    }
}


BOOL CTrafficMonitorDlg::OnQueryEndSession()
{
    if (!CDialog::OnQueryEndSession())
        return FALSE;

    // QueryEndSession can still be vetoed by another application. Only the
    // reversible Explorer restore happens here; worker/file teardown waits
    // for WM_ENDSESSION(TRUE).
    m_destroyWindowAuthorized = false;
    m_sessionEndApproved = false;
    m_closeInProgress = true;
    if (!CloseTaskBarWnd())
    {
        m_closeInProgress = false;
        return FALSE;
    }

    m_sessionEndApproved = true;
    return TRUE;
}

afx_msg LRESULT CTrafficMonitorDlg::OnEndSessionMessage(WPARAM wParam, LPARAM lParam)
{
    if (wParam != FALSE)
    {
        if (m_sessionEndApproved)
        {
            KillTimer(MAIN_TIMER);
            KillTimer(MONITOR_TIMER);
            KillTimer(TASKBAR_TIMER);
            KillTimer(DELAY_TIMER);
            KillTimer(DELETE_NOTIFY_ICON_TIMER);
            KillTimer(DPI_CHANGE_TIMER);
            KillTimer(RESTART_TASKBAR_TIMER);
            KillTimer(INIT_CONNECT_TIMER);
            KillTimer(kCloseRetryTimer);
            KillTimer(kTaskbarRestoreRetryTimer);
            if (!ExitMonitorThread(INFINITE))
            {
                ASSERT(FALSE);
                return 0;
            }

            theApp.m_cannot_save_config_warning = true;
            theApp.m_cannot_save_global_config_warning = true;
            BackupHistoryTrafficFile(SaveHistoryTrafficFull());
            theApp.SaveConfig();
            theApp.SaveGlobalConfig();
            m_destroyWindowAuthorized = true;

            if (theApp.m_debug_log)
                CCommon::WriteLog(_T("TaskbarMon session end accepted after state restoration."),
                    (theApp.m_config_dir + L".\\debug.log").c_str());
        }
        return 0;
    }

    // Windows can cancel a logoff after every application has approved it.
    // QueryEndSession only closed the bar, so restoring the UI is sufficient;
    // the worker was deliberately left running.
    m_destroyWindowAuthorized = false;
    if (m_sessionEndApproved)
    {
        m_sessionEndApproved = false;
        m_closeInProgress = false;
        if (theApp.m_cfg_data.m_show_task_bar_wnd)
            OpenTaskBarWnd();
    }
    return 0;
}

afx_msg LRESULT CTrafficMonitorDlg::OnDpichanged(WPARAM wParam, LPARAM lParam)
{
    m_pending_dpi = LOWORD(wParam);
    KillTimer(DPI_CHANGE_TIMER);
    SetTimer(DPI_CHANGE_TIMER, 500, NULL);
    return 0;
}

afx_msg LRESULT CTrafficMonitorDlg::OnTaskbarWndClosed(WPARAM wParam, LPARAM lParam)
{
    theApp.m_cfg_data.m_show_task_bar_wnd = false;
    if (!theApp.m_general_data.show_notify_icon && theApp.IsForceShowNotifyIcon())
    {
        AddNotifyIcon();
        theApp.m_general_data.show_notify_icon = true;
    }

    // CTaskBarDlg sends this synchronously before it destroys its HWND. Defer
    // deletion until its OnClose stack has returned, then only clean up.
    PostMessage(WM_REOPEN_TASKBAR_WND, 0, 0);
    return 0;
}

afx_msg LRESULT CTrafficMonitorDlg::OnMonitorInfoUpdated(WPARAM wParam, LPARAM lParam)
{
    MonitorSnapshot snapshot;
    uint64_t revision{};
    {
        std::lock_guard<std::mutex> lock(m_pendingMonitorSnapshotMutex);
        if (!m_hasPendingMonitorSnapshot)
            return 0;
        snapshot = m_pendingMonitorSnapshot;
        revision = m_pendingMonitorRevision;
    }

    // Legacy consumers render from the UI-thread copy only; the worker never
    // writes these fields directly.
    theApp.m_in_speed = snapshot.in_speed;
    theApp.m_out_speed = snapshot.out_speed;
    theApp.m_cpu_usage = snapshot.cpu_usage;
    theApp.m_memory_usage = snapshot.memory_usage;
    theApp.m_used_memory = snapshot.used_memory;
    theApp.m_total_memory = snapshot.total_memory;
    theApp.m_cpu_temperature = snapshot.cpu_temperature;
    theApp.m_cpu_freq = snapshot.cpu_freq;
    theApp.m_gpu_temperature = snapshot.gpu_temperature;
    theApp.m_hdd_temperature = snapshot.hdd_temperature;
    theApp.m_main_board_temperature = snapshot.main_board_temperature;
    theApp.m_gpu_usage = snapshot.gpu_usage;
    theApp.m_hdd_usage = snapshot.hdd_usage;
    theApp.m_today_up_traffic = snapshot.today_up_traffic;
    theApp.m_today_down_traffic = snapshot.today_down_traffic;
    theApp.m_monitor_revision = revision;

    if (IsTaskbarWndValid())
        m_tBarDlg->UpdateToolTips();
    return 0;
}


LRESULT CTrafficMonitorDlg::OnDisplaychange(WPARAM wParam, LPARAM lParam)
{
    GetScreenSize();
    return 0;
}





LRESULT CTrafficMonitorDlg::OnReopenTaksbarWnd(WPARAM wParam, LPARAM lParam)
{
    if (CloseTaskBarWnd() && theApp.m_cfg_data.m_show_task_bar_wnd)
        OpenTaskBarWnd();
    return 0;
}


void CTrafficMonitorDlg::OnOpenTaskManager()
{
    if (!theApp.m_system_dir.empty())
        ShellExecuteW(NULL, _T("open"), (theApp.m_system_dir + L"\\Taskmgr.exe").c_str(), NULL, NULL, SW_NORMAL);       //打开任务管理器
}


afx_msg LRESULT CTrafficMonitorDlg::OnSettingsApplied(WPARAM wParam, LPARAM lParam)
{
    COptionsDlg* pOptionsDlg = (COptionsDlg*)wParam;
    if (pOptionsDlg != nullptr)
    {
        ApplySettings(*pOptionsDlg);
    }
    return 0;
}


void CTrafficMonitorDlg::OnDisplaySettings()
{
    // TODO: 在此添加命令处理程序代码
    CSetItemOrderDlg dlg;
    dlg.SetItemOrder(theApp.m_taskbar_data.item_order.GetItemOrderConst());
    dlg.SetDisplayItem(theApp.m_taskbar_data.display_item);
    dlg.SetPluginDisplayItem(theApp.m_taskbar_data.plugin_display_item);
    if (dlg.DoModal() == IDOK)
    {
        theApp.m_taskbar_data.item_order.SetOrder(dlg.GetItemOrder());
        theApp.m_taskbar_data.display_item = dlg.GetDisplayItem();
        theApp.m_taskbar_data.plugin_display_item = dlg.GetPluginDisplayItem();
        //CloseTaskBarWnd();
        //OpenTaskBarWnd();
        if (IsTaskbarWndValid())
            m_tBarDlg->WidthChanged();
    }
}




void CTrafficMonitorDlg::OnRefreshConnectionList()
{
    m_monitor_service.InitConnections();

    //重新初始化连接后，如果需要延迟自动选择，则设置延迟定时器
    if (m_monitor_service.ConsumeDelayedAutoSelectPending())
        SetTimer(DELAY_TIMER, 5000, NULL);

}


afx_msg LRESULT CTrafficMonitorDlg::OnTabletQuerysystemgesturestatus(WPARAM wParam, LPARAM lParam)
{
    return 0;
}




UINT CTrafficMonitorDlg::OnPowerBroadcast(UINT nPowerEvent, LPARAM nEventData)
{
    if (nPowerEvent == PBT_APMRESUMESUSPEND)
    {
        m_resume_connection_check_times = 0;
        KillTimer(INIT_CONNECT_TIMER);
        SetTimer(INIT_CONNECT_TIMER, 10000, NULL);
    }
    return CDialog::OnPowerBroadcast(nPowerEvent, nEventData);
}

void CTrafficMonitorDlg::OnColorizationColorChanged(DWORD dwColorizationColor, BOOL bOpacity)
{
    // 此功能要求 Windows Vista 或更高版本。
    // _WIN32_WINNT 符号必须 >= 0x0600。
    // TODO: 在此添加消息处理程序代码和/或调用默认值

    static DWORD last_color;
    if (last_color != dwColorizationColor)
    {
        last_color = dwColorizationColor;
        BYTE red = (dwColorizationColor >> 16) & 0xFF;
        BYTE green = (dwColorizationColor >> 8) & 0xFF;
        BYTE blue = dwColorizationColor & 0xFF;
        COLORREF theme_color = RGB(red, green, blue);
        theApp.SetThemeColor(theme_color);
    }
    CDialog::OnColorizationColorChanged(dwColorizationColor, bOpacity);
}
