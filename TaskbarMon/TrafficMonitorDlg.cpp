
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



// CTrafficMonitorDlg 对话框

//静态成员初始化
unsigned int CTrafficMonitorDlg::m_WM_TASKBARCREATED{ ::RegisterWindowMessage(_T("TaskbarCreated")) };  //注册任务栏建立的消息

CTrafficMonitorDlg::CTrafficMonitorDlg(CWnd* pParent /*=NULL*/)
    : CDialog(IDD_TRAFFICMONITOR_DIALOG, pParent)
    , m_monitor_service(GetMonitorConfig())
{
    m_desktop_dc = ::GetDC(NULL);
    m_monitor_service.SetHardwareProvider(&m_hardware_provider);
    m_monitor_service.on_history_save = [this]() { SaveHistoryTraffic(); };
}

CTrafficMonitorDlg::~CTrafficMonitorDlg()
{
    if (m_tBarDlg != nullptr)
    {
        delete m_tBarDlg;
        m_tBarDlg = nullptr;
    }

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
        const auto& connections = m_monitor_service.Connections();
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
    int item_count = static_cast<int>(m_monitor_service.Connections().size()) + 1;
    if (theApp.m_cfg_data.m_select_all)
        pMenu->CheckMenuRadioItem(0, item_count, 1, MF_BYPOSITION | MF_CHECKED);
    else if (theApp.m_cfg_data.m_auto_select)       //m_auto_select为true时为自动选择，选中菜单的第1项
        pMenu->CheckMenuRadioItem(0, item_count, 0, MF_BYPOSITION | MF_CHECKED);
    else        //m_auto_select为false时非自动选择，根据m_connection_selected的值选择对应的项
        pMenu->CheckMenuRadioItem(0, item_count, m_monitor_service.SelectedIndex() + 2, MF_BYPOSITION | MF_CHECKED);

    //没有设置为“选择全部”时，将当前选择项设置为默认菜单项（加粗显示）
    if (!theApp.m_cfg_data.m_select_all)
        pMenu->SetDefaultItem(m_monitor_service.SelectedIndex() + 2, TRUE);
    else
        pMenu->SetDefaultItem(-1, TRUE);
}

void CTrafficMonitorDlg::CloseTaskBarWnd()
{
    if (m_tBarDlg != nullptr)
    {
        if (IsTaskbarWndValid())
            m_tBarDlg->OnCancel();
        delete m_tBarDlg;
        m_tBarDlg = nullptr;
        theApp.m_taskbar_data.update_layered_window_error_code = 0;
    }
}

void CTrafficMonitorDlg::OpenTaskBarWnd()
{
    // 强制初始化theApp.m_is_windows11_taskbar的值
    theApp.CheckWindows11Taskbar();
    if (theApp.m_win_version.IsWine())
        m_tBarDlg = new CWineTaskbarDlg();
    else if (theApp.IsWindows11Taskbar())
        m_tBarDlg = new CWin11TaskbarDlg();
    else
        m_tBarDlg = new CClassicalTaskbarDlg();

    CSupportedRenderEnums supported_render_enums{};
    CTaskBarDlg::DisableRenderFeatureIfNecessary(supported_render_enums);
    auto render_type = supported_render_enums.GetAutoFitEnum();
    // WS_EX_LAYERED 和 WS_EX_NOREDIRECTIONBITMAP 可以共存，见微软示例代码
    // https://github.com/microsoft/Windows-classic-samples/blob/7cbd99ac1d2b4a0beffbaba29ea63d024ceff700/Samples/DynamicDPI/cpp/SampleDesktopWindow.cpp#L179
    // 但是WS_EX_NOREDIRECTIONBITMAP似乎会导致UpdateLayeredWindowIndirect失败
    switch (render_type)
    {
        using namespace DrawCommonHelper;
    case RenderType::D2D1_WITH_DCOMPOSITION:
        m_tBarDlg->Create(IDD_TASK_BAR_DIALOG_NOREDIRECTIONBITMAP, this);
        break;
    // 包括RenderType::D2D1在内的其他值
    default:
        m_tBarDlg->Create(IDD_TASK_BAR_DIALOG, this);
        break;
    }
    m_tBarDlg->ShowWindow(SW_SHOW);
    //m_tBarDlg->ShowInfo();
    //IniTaskBarConnectionMenu();
}

void CTrafficMonitorDlg::AddNotifyIcon()
{
    if (theApp.m_cfg_data.m_show_task_bar_wnd)
        CloseTaskBarWnd();
    //添加通知栏图标
    ::Shell_NotifyIcon(NIM_ADD, &m_ntIcon);
    if (theApp.m_cfg_data.m_show_task_bar_wnd)
        OpenTaskBarWnd();
}

void CTrafficMonitorDlg::DeleteNotifyIcon()
{
    if (theApp.m_cfg_data.m_show_task_bar_wnd)
        CloseTaskBarWnd();
    //删除通知栏图标
    ::Shell_NotifyIcon(NIM_DELETE, &m_ntIcon);
    if (theApp.m_cfg_data.m_show_task_bar_wnd)
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
    m_monitor_service.HistoryFile().SaveTodayOnly();
}

void CTrafficMonitorDlg::SaveHistoryTrafficFull()
{
    // 完整保存，用于程序退出时确保所有数据都保存
    m_monitor_service.HistoryFile().Save();
}

void CTrafficMonitorDlg::LoadHistoryTraffic()
{
    auto& history_traffic = m_monitor_service.HistoryFile();
    history_traffic.Load();
    CHistoryTrafficFile backup_file(theApp.m_history_traffic_path + L".bak");
    backup_file.LoadSize();     //读取备份文件中流量记录的数量
    
    // 如果备份文件中流量记录的数量大于当前的数量，尝试从备份文件中恢复
    if (backup_file.Size() > history_traffic.Size())
    {
        size_t size_before = history_traffic.Size();
        backup_file.Load();     //加载备份文件（会清理"未来"的记录）
        size_t backup_size_after_load = backup_file.Size();  //加载后实际的记录数（可能因为清理"未来"记录而减少）
        
        // 加载后，如果备份文件的记录数仍然大于当前文件，才进行恢复
        if (backup_size_after_load > history_traffic.Size())
        {
            history_traffic.Merge(backup_file, true);
            size_t size_after = history_traffic.Size();
            size_t recovered_count = size_after - size_before;  //实际恢复的记录数
            
            // 只有当实际恢复了记录时才记录日志
            if (recovered_count > 0)
            {
                CString log_info = CCommon::LoadTextFormat(IDS_HISTORY_TRAFFIC_LOST_ERROR_LOG, { size_before, recovered_count });
                CCommon::WriteLog(log_info, theApp.m_log_path.c_str());
            }
        }
    }

    theApp.m_today_up_traffic = history_traffic.GetTodayUpTraffic();
    theApp.m_today_down_traffic = history_traffic.GetTodayDownTraffic();
}

void CTrafficMonitorDlg::BackupHistoryTrafficFile()
{
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

    //打开了D2D渲染后自动开启“背景透明”并关闭“根据任务栏颜色自动设置背景色”
    if (d2d_turned_on)
    {
        theApp.m_taskbar_data.SetTaskabrTransparent(true);
        theApp.m_taskbar_data.auto_set_background_color = false;
    }

    //CTaskBarDlg::SaveConfig();
    if (IsTaskbarWndValid())
    {
        m_tBarDlg->ApplySettings();
        //如果更改了任务栏窗口字体或显示的文本，则任务栏窗口可能要变化，于是关闭再打开任务栏窗口
        if (taskbar_changed)
        {
            CloseTaskBarWnd();
            OpenTaskBarWnd();
        }
        else
        {
            m_tBarDlg->WidthChanged();
        }
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
        //如果关闭了硬件监控，则析构硬件监控类
        if (theApp.m_general_data.hardware_monitor_item == 0)
        {
            CSingleLock sync(&theApp.m_minitor_lib_critical, TRUE);
            theApp.m_pMonitor.reset();
        }
        else if (theApp.m_pMonitor != nullptr)
        {
            theApp.UpdateOpenHardwareMonitorEnableState();
        }
        else if (IsTemperatureNeeded())
        {
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
    m_monitor_service.ApplyConfig(GetMonitorConfig());

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

// CHardwareDataProvider 实现（封装 OpenHardwareMonitor 访问）
bool CTrafficMonitorDlg::CHardwareDataProvider::IsAvailable() const
{
    CTrafficMonitorDlg* pMainWnd = CTrafficMonitorDlg::Instance();
    if (pMainWnd == nullptr)
        return false;
    return pMainWnd->IsTemperatureNeeded() && theApp.m_pMonitor != nullptr;
}

void CTrafficMonitorDlg::CHardwareDataProvider::Acquire()
{
    if (theApp.m_pMonitor == nullptr)
        return;
    CSingleLock sync(&theApp.m_minitor_lib_critical, TRUE);
    __try
    {
        theApp.m_pMonitor->GetHardwareInfo();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        CString error_info = CCommon::LoadText(IDS_HARDWARE_INFO_ACQUIRE_FAILED_ERROR);
        AfxMessageBox(error_info, MB_ICONERROR | MB_OK);
    }
}

float CTrafficMonitorDlg::CHardwareDataProvider::CpuTemperature() const
{
    return (theApp.m_pMonitor != nullptr) ? theApp.m_pMonitor->CpuTemperature() : -1;
}

float CTrafficMonitorDlg::CHardwareDataProvider::CpuFreq() const
{
    return (theApp.m_pMonitor != nullptr) ? theApp.m_pMonitor->CpuFreq() : -1;
}

float CTrafficMonitorDlg::CHardwareDataProvider::GpuTemperature() const
{
    return (theApp.m_pMonitor != nullptr) ? theApp.m_pMonitor->GpuTemperature() : -1;
}

float CTrafficMonitorDlg::CHardwareDataProvider::HddTemperature() const
{
    return (theApp.m_pMonitor != nullptr) ? theApp.m_pMonitor->HDDTemperature() : -1;
}

float CTrafficMonitorDlg::CHardwareDataProvider::MainboardTemperature() const
{
    return (theApp.m_pMonitor != nullptr) ? theApp.m_pMonitor->MainboardTemperature() : -1;
}

int CTrafficMonitorDlg::CHardwareDataProvider::GpuUsage() const
{
    return (theApp.m_pMonitor != nullptr) ? theApp.m_pMonitor->GpuUsage() : -1;
}

int CTrafficMonitorDlg::CHardwareDataProvider::HddUsage() const
{
    return (theApp.m_pMonitor != nullptr) ? theApp.m_pMonitor->HddUsage() : -1;
}

std::map<std::wstring, float> CTrafficMonitorDlg::CHardwareDataProvider::AllCpuTemperature() const
{
    if (theApp.m_pMonitor == nullptr)
        return {};
    return theApp.m_pMonitor->AllCpuTemperature();
}

std::map<std::wstring, float> CTrafficMonitorDlg::CHardwareDataProvider::AllHddTemperature() const
{
    if (theApp.m_pMonitor == nullptr)
        return {};
    return theApp.m_pMonitor->AllHDDTemperature();
}

std::map<std::wstring, int> CTrafficMonitorDlg::CHardwareDataProvider::AllHddUsage() const
{
    if (theApp.m_pMonitor == nullptr)
        return {};
    return theApp.m_pMonitor->AllHDDUsage();
}

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
    AfxBeginThread(MonitorThreadCallback, (LPVOID)this);

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



void CTrafficMonitorDlg::DoMonitorAcquisition()
{
    //委托统一采样引擎执行一次完整采样
    m_monitor_service.Sample();

    //将快照数据同步到 App 全局成员（供任务栏窗口/托盘等 UI 读取）
    const MonitorSnapshot& snapshot = m_monitor_service.Snapshot();
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

    //发送监控信息更新消息
    SendMessage(WM_MONITOR_INFO_UPDATED);
}

UINT CTrafficMonitorDlg::MonitorThreadCallback(LPVOID dwUser)
{
    CTrafficMonitorDlg* pThis = (CTrafficMonitorDlg*)dwUser;
    while (true)
    {
        //获取一次监控数据
        if (pThis->m_monitor_data_required)
        {
            pThis->DoMonitorAcquisition();
            //获取到监控数据后重置flag
            pThis->m_monitor_data_required = false;
        }
        else
        {
            Sleep(10);
        }

        // 检查退出标志
        if (pThis->m_is_thread_exit)
        {
            // 触发事件，通知主线程工作线程已退出
            pThis->m_threadExitEvent.SetEvent();
            return 0;
        }
    }

    return 0;
}


void CTrafficMonitorDlg::ExitMonitorThread()
{
    // 通知线程退出
    m_is_thread_exit = true;

    // 等待线程退出
    ::WaitForSingleObject(m_threadExitEvent.m_hObject, 1000);
}


void CTrafficMonitorDlg::OnTimer(UINT_PTR nIDEvent)
{
    // TODO: 在此添加消息处理程序代码和/或调用默认值
    if (nIDEvent == MONITOR_TIMER)
    {
        //通知线程获取监控数据
        m_monitor_data_required = true;
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
        if (!theApp.m_win_version.IsWine() && IsTaskbarWndValid() && m_timer_cnt % 10 == 1)
        {
            if (m_tBarDlg->GetCannotInsertToTaskBar() && m_insert_to_taskbar_cnt < MAX_INSERT_TO_TASKBAR_CNT)
            {
                CloseTaskBarWnd();
                OpenTaskBarWnd();
                m_insert_to_taskbar_cnt++;
                if (m_tBarDlg->GetCannotInsertToTaskBar() && m_insert_to_taskbar_cnt >= WARN_INSERT_TO_TASKBAR_CNT)
                {
                    //写入错误日志
                    CString info = CCommon::LoadText(IDS_CONNOT_INSERT_TO_TASKBAR_ERROR_LOG);
                    info.Replace(_T("<%cnt%>"), CCommon::IntToString(m_insert_to_taskbar_cnt));
                    info.Replace(_T("<%error_code%>"), CCommon::IntToString(m_tBarDlg->GetErrorCode()));
                    CCommon::WriteLog(info, theApp.m_log_path.c_str());
                    if (m_cannot_insert_to_task_bar_warning)      //确保提示信息只弹出一次
                    {
                        //弹出错误信息
                        m_cannot_insert_to_task_bar_warning = false;
                        MessageBox(CCommon::LoadText(IDS_CONNOT_INSERT_TO_TASKBAR, CCommon::IntToString(m_tBarDlg->GetErrorCode())), NULL, MB_ICONWARNING);
                    }
                }
            }
            if (!m_tBarDlg->GetCannotInsertToTaskBar())
            {
                m_insert_to_taskbar_cnt = 0;
            }
        }

        //检查是否需要弹出鼠标提示
        //setting_data: 消息提示的设置数据
        //value: 当前的值
        //last_value: 传递一个static或可以在此lambda表达式调用结束后继续存在的变量，用于保存上一次的值
        //notify_time: 传递一个static或可以在此lambda表达式调用结束后继续存在的变量，用于记录上次弹出提示的时间（定时器触发次数）
        //tip_str: 要提示的消息
        auto checkNotifyTip = [&](GeneralSettingData::NotifyTipSettings setting_data, int value, int& last_value, int& notify_time, LPCTSTR tip_str)
        {
            if (setting_data.enable)
            {
                if (last_value < setting_data.tip_value && value >= setting_data.tip_value && (m_timer_cnt - notify_time > static_cast<unsigned int>(theApp.m_notify_interval)))
                {
                    ShowNotifyTip(CCommon::LoadText(_T("TrafficMonitor "), IDS_NOTIFY, _T("")), tip_str);
                    notify_time = m_timer_cnt;
                }
                last_value = value;
            }
        };

        //检查是否要弹出内存使用率超出提示
        CString info;
        info.Format(CCommon::LoadText(IDS_MEMORY_UDAGE_EXCEED, _T(" %d%%!")), theApp.m_memory_usage);
        static int last_memory_usage;
        static int memory_usage_notify_time{ -theApp.m_notify_interval };       //记录上次弹出提示时的时间
        checkNotifyTip(theApp.m_general_data.memory_usage_tip, theApp.m_memory_usage, last_memory_usage, memory_usage_notify_time, info.GetString());

        //检查是否要弹出CPU温度使用率超出提示
        info.Format(CCommon::LoadText(IDS_CPU_TEMPERATURE_EXCEED, _T(" %d°C!")), static_cast<int>(theApp.m_cpu_temperature));
        static int last_cpu_temp;
        static int cpu_temp_notify_time{ -theApp.m_notify_interval };       //记录上次弹出提示时的时间
        checkNotifyTip(theApp.m_general_data.cpu_temp_tip, theApp.m_cpu_temperature, last_cpu_temp, cpu_temp_notify_time, info.GetString());

        //检查是否要弹出显卡温度使用率超出提示
        info.Format(CCommon::LoadText(IDS_GPU_TEMPERATURE_EXCEED, _T(" %d°C!")), static_cast<int>(theApp.m_gpu_temperature));
        static int last_gpu_temp;
        static int gpu_temp_notify_time{ -theApp.m_notify_interval };       //记录上次弹出提示时的时间
        checkNotifyTip(theApp.m_general_data.gpu_temp_tip, theApp.m_gpu_temperature, last_gpu_temp, gpu_temp_notify_time, info.GetString());

        //检查是否要弹出硬盘温度使用率超出提示
        info.Format(CCommon::LoadText(IDS_HDD_TEMPERATURE_EXCEED, _T(" %d°C!")), static_cast<int>(theApp.m_hdd_temperature));
        static int last_hdd_temp;
        static int hdd_temp_notify_time{ -theApp.m_notify_interval };       //记录上次弹出提示时的时间
        checkNotifyTip(theApp.m_general_data.hdd_temp_tip, theApp.m_hdd_temperature, last_hdd_temp, hdd_temp_notify_time, info.GetString());

        //检查是否要弹出主板温度使用率超出提示
        info.Format(CCommon::LoadText(IDS_MBD_TEMPERATURE_EXCEED, _T(" %d°C!")), static_cast<int>(theApp.m_main_board_temperature));
        static int last_main_board_temp;
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
            && !m_is_foreground_fullscreen && theApp.m_taskbar_data.disable_d2d)
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
                SetTimer(RESTART_TASKBAR_TIMER, 500, [](HWND, UINT, UINT_PTR, DWORD) {
                    theApp.m_pMainWnd->SendMessage(WM_REOPEN_TASKBAR_WND);
                    ::KillTimer(theApp.m_pMainWnd->GetSafeHwnd(), RESTART_TASKBAR_TIMER);
                });
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
            m_tBarDlg->Invalidate(FALSE);
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
    // TODO: 在此添加命令处理程序代码
    //弹出“连接详情”对话框
    CNetworkInfoDlg aDlg(m_monitor_service.Connections(), m_monitor_service.IfTable()->table, m_monitor_service.SelectedIndex());
    aDlg.m_start_time = m_start_time;
    aDlg.DoModal();
    if (m_tBarDlg != nullptr)
        m_tBarDlg->m_tool_tips.SetWindowPos(&wndTopMost, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);  //重新设置任务栏窗口的提示信息置顶
}












void CTrafficMonitorDlg::OnClose()
{
    // TODO: 在此添加消息处理程序代码和/或调用默认值
    theApp.m_cannot_save_config_warning = true;
    theApp.m_cannot_save_global_config_warning = true;
    theApp.SaveConfig();    //退出前保存设置到ini文件
    theApp.SaveGlobalConfig();
    SaveHistoryTrafficFull();  // 退出时使用完整保存，确保所有数据都保存
    BackupHistoryTrafficFile();

    if (IsTaskbarWndValid())
        m_tBarDlg->OnCancel();

    //确保在退出前关闭所有窗口
    for (const auto& item : CBaseDialog::AllUniqueHandels())
    {
        ::SendMessage(item.second, WM_COMMAND, IDCANCEL, 0);
    }

    CDialog::OnClose();
}


BOOL CTrafficMonitorDlg::OnCommand(WPARAM wParam, LPARAM lParam)
{
    // TODO: 在此添加专用代码和/或调用基类
    UINT uMsg = LOWORD(wParam);
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
    if (uMsg > ID_SELECT_ALL_CONNECTION && uMsg <= ID_SELECT_ALL_CONNECTION + m_monitor_service.Connections().size()) //选择了一个网络连接
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
    CDialog::OnDestroy();

    //程序退出时删除通知栏图标
    ::Shell_NotifyIcon(NIM_DELETE, &m_ntIcon);

    // 停止监控线程
    ExitMonitorThread();
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
    // TODO: 在此添加命令处理程序代码
    if (m_tBarDlg != nullptr)
    {
        CloseTaskBarWnd();
    }
    if (!theApp.m_cfg_data.m_show_task_bar_wnd)
    {
        theApp.m_cfg_data.m_show_task_bar_wnd = true;
        OpenTaskBarWnd();
    }
    else
    {
        theApp.m_cfg_data.m_show_task_bar_wnd = false;
        //关闭任务栏窗口后，如果没有显示通知区图标，且没有显示主窗口或设置了鼠标穿透，则将通知区图标显示出来
        if (!theApp.m_general_data.show_notify_icon && theApp.IsForceShowNotifyIcon())
        {
            AddNotifyIcon();
            theApp.m_general_data.show_notify_icon = true;
        }
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
    if (m_tBarDlg != nullptr)
    {
        CloseTaskBarWnd();
        if (theApp.m_general_data.show_notify_icon)
        {
            //重新添加通知栏图标
            ::Shell_NotifyIcon(NIM_ADD, &m_ntIcon);
        }
        OpenTaskBarWnd();
    }
    else
    {
        if (theApp.m_general_data.show_notify_icon)
            ::Shell_NotifyIcon(NIM_ADD, &m_ntIcon);
    }
    return LRESULT();
}







void CTrafficMonitorDlg::OnTrafficHistory()
{
    // TODO: 在此添加命令处理程序代码
    CHistoryTrafficDlg historyDlg(m_monitor_service.HistoryFile().GetTraffics());
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

    // TODO:  在此添加专用的查询结束会话代码
    theApp.SaveConfig();
    theApp.SaveGlobalConfig();
    SaveHistoryTrafficFull();  // 系统关机时使用完整保存，确保所有数据都保存
    BackupHistoryTrafficFile();

    if (theApp.m_debug_log)
    {
        CCommon::WriteLog(_T("TrafficMonitor进程已被终止，设置已保存。"), (theApp.m_config_dir + L".\\debug.log").c_str());
    }

    return TRUE;
}




afx_msg LRESULT CTrafficMonitorDlg::OnDpichanged(WPARAM wParam, LPARAM lParam)
{
    static int dpi;
    static CTrafficMonitorDlg* pThis;
    dpi = LOWORD(wParam);
    pThis = this;

    //由于窗口在不同DPI的显示器上移动时，会短时间内触发多次DPI更改消息，因此这里在收到消息后延迟一段时间后再处理
    KillTimer(DPI_CHANGE_TIMER);
    SetTimer(DPI_CHANGE_TIMER, 500, [](HWND, UINT, UINT_PTR, DWORD) {
        //根据窗口的位置获取DPI
        CRect rect;
        pThis->GetWindowRect(rect);
        UINT dpi_x, dpi_y;
        if (theApp.DPIFromRect(rect, &dpi_x, &dpi_y))   //获取成功，则使用根据窗口位置得到的dpi
            dpi = dpi_x;
        TRACE("Dpi changed: %d\n", dpi);

        theApp.SetDPI(dpi);
        //当系统版本小于Windows 8.1时使用原来的行为
        if (pThis->IsTaskbarWndValid() && !theApp.m_win_version.IsWindows8Point1OrLater())
        {
            //为任务栏窗口重新指定DPI
            pThis->m_tBarDlg->SetDPI(dpi);
            //根据新的DPI重新设置任务栏窗口字体
            pThis->m_tBarDlg->SetTextFont();
        }

        pThis->KillTimer(DPI_CHANGE_TIMER);
    });

    return 0;
}


afx_msg LRESULT CTrafficMonitorDlg::OnTaskbarWndClosed(WPARAM wParam, LPARAM lParam)
{
    theApp.m_cfg_data.m_show_task_bar_wnd = false;
    //关闭任务栏窗口后，如果没有显示通知区图标，且没有显示主窗口或设置了鼠标穿透，则将通知区图标显示出来
    if (!theApp.m_general_data.show_notify_icon && theApp.IsForceShowNotifyIcon())
    {
        AddNotifyIcon();
        theApp.m_general_data.show_notify_icon = true;
    }
    return 0;
}



afx_msg LRESULT CTrafficMonitorDlg::OnMonitorInfoUpdated(WPARAM wParam, LPARAM lParam)
{
    //更新任务栏窗口鼠标提示
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
    CloseTaskBarWnd();
    OpenTaskBarWnd();
    return 0;
}


void CTrafficMonitorDlg::OnOpenTaskManager()
{
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
    // 系统从休眠恢复
    if (nPowerEvent == PBT_APMRESUMESUSPEND)
    {
        //延迟一段时间后重新初始化网络连接
        KillTimer(INIT_CONNECT_TIMER);
        static CTrafficMonitorDlg* pThis = this;
        static int check_times = 0;
        SetTimer(INIT_CONNECT_TIMER, 10000, [](HWND, UINT, UINT_PTR, DWORD) {
            pThis->m_monitor_service.InitConnections();

            //重新初始化连接后，如果需要延迟自动选择，则设置延迟定时器
            if (pThis->m_monitor_service.ConsumeDelayedAutoSelectPending())
                pThis->SetTimer(DELAY_TIMER, 5000, NULL);
            check_times++;

            //写入日志
            CString info = CCommon::LoadTextFormat(IDS_RESTORE_FROM_SLEEP_LOG, {pThis->m_monitor_service.RestartCount() });
            CCommon::WriteLog(info, theApp.m_log_path.c_str());

            //如果连接为空，定时器继续运行，每隔一段时间重新初始化连接
            if (pThis->m_monitor_service.Connections().size() == 0)
            {
                //超过20次，结束定时器
                if (check_times >= 20)
                    pThis->KillTimer(INIT_CONNECT_TIMER);
            }
            //成功获取到连接，结束定时器
            else
            {
                pThis->KillTimer(INIT_CONNECT_TIMER);
                check_times = 0;
            }
        });
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
