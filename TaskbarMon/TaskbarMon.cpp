
// TaskbarMon.cpp : 定义应用程序的类行为。
//

#include "stdafx.h"
#include "TaskbarMon.h"
#include <memory>
#include "TrafficMonitorDlg.h"
#include "core/ConfigStore.h"
#include "crashtool.h"
#include "UpdateHelper.h"
#include "Test.h"
#include "WIC.h"
#include "auto_start_helper.h"
#include "AppAlreadyRuningDlg.h"
#include "WindowsSettingHelper.h"
#include "SettingsHelper.h"
#ifndef DISABLE_WINDOWS_WEB_EXPERIENCE_DETECTOR
#include "winrt/base.h"
#endif
#include <gdiplus.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CTaskbarMonApp

BEGIN_MESSAGE_MAP(CTaskbarMonApp, CWinApp)
    //ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
    ON_COMMAND(ID_HELP, &CTaskbarMonApp::OnHelp)
    ON_COMMAND(ID_FREQUENTY_ASKED_QUESTIONS, &CTaskbarMonApp::OnFrequentyAskedQuestions)
    ON_COMMAND(ID_UPDATE_LOG, &CTaskbarMonApp::OnUpdateLog)
END_MESSAGE_MAP()


CTaskbarMonApp* CTaskbarMonApp::self = NULL;

namespace
{
#ifndef WITHOUT_TEMPERATURE
    struct OpenHardwareMonitorInitializationRequest
    {
        uint64_t generation{};
        unsigned int enabled_items{};
    };
#endif

    bool ReadRegistryStringValue(CRegKey& key, LPCTSTR value_name, wstring& value)
    {
        value.clear();

        // CRegKey reports this count in TCHARs. Keep a small, explicit limit
        // so the extra sentinel below cannot overflow or allocate unbounded
        // memory when the registry value is malformed.
        constexpr ULONG kMaxRegistryStringCharacters = 32768;
        ULONG character_count{};
        const LONG size_result = key.QueryStringValue(value_name, nullptr, &character_count);
        if (size_result != ERROR_SUCCESS && size_result != ERROR_MORE_DATA)
            return false;
        if (character_count == 0 || character_count > kMaxRegistryStringCharacters)
            return false;

        const ULONG queried_character_count = character_count;
        const ULONG buffer_character_count = queried_character_count + 1;
        vector<wchar_t> buffer(static_cast<size_t>(buffer_character_count), L'\0');

        character_count = buffer_character_count;
        if (key.QueryStringValue(value_name, buffer.data(), &character_count) != ERROR_SUCCESS)
            return false;

        // The value can change between the sizing query and the read. Do not
        // consume a value larger than the validated initial bound. The final
        // zero-initialized element is only a sentinel; it is never read as
        // registry data.
        if (character_count == 0 || character_count > queried_character_count)
            return false;

        size_t string_length{};
        const size_t returned_character_count = static_cast<size_t>(character_count);
        while (string_length < returned_character_count && buffer[string_length] != L'\0')
            ++string_length;
        value.assign(buffer.data(), string_length);
        return true;
    }

    wstring StripSurroundingQuotes(wstring value)
    {
        if (value.size() >= 2 && value.front() == L'\"' && value.back() == L'\"')
            return value.substr(1, value.size() - 2);
        return value;
    }

    bool SameWindowsPath(const wstring& left, const wstring& right)
    {
        return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
    }

    bool GetCurrentModulePath(wstring& path)
    {
        std::vector<wchar_t> buffer(MAX_PATH);
        while (buffer.size() <= 32768)
        {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
                return false;
            if (length < buffer.size())
            {
                path.assign(buffer.data(), length);
                return true;
            }
            buffer.resize(buffer.size() * 2);
        }
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
}


// CTaskbarMonApp 构造
CTaskbarMonApp::CTaskbarMonApp()
{
    self = this;
    // 支持重新启动管理器
    //m_dwRestartManagerSupportFlags = AFX_RESTART_MANAGER_SUPPORT_RESTART;

    // TODO: 在此处添加构造代码，
    // 将所有重要的初始化放置在 InitInstance 中
    CRASHREPORT::StartCrashReport();
#ifndef DISABLE_WINDOWS_WEB_EXPERIENCE_DETECTOR
    if (m_win_version.IsWindows11OrLater())
        winrt::init_apartment();
#endif

    CheckWindows11Taskbar();
    m_theme_color = CCommon::GetWindowsThemeColor();
}

void CTaskbarMonApp::LoadLanguageConfig()
{
    ConfigStore store{ m_config_path };
    store.LoadLanguage(m_general_data.language);
}

void CTaskbarMonApp::LoadConfig()
{
    ConfigStore store{ m_config_path };
    ConfigStore::EnvironmentDefaults defaults;
    defaults.default_font_name = m_str_table.GetLanguageInfo().default_font_name;
    defaults.system_dir = m_system_dir;
    defaults.default_notify_icon = m_cfg_data.m_dft_notify_icon;
    defaults.update_source_default = (m_str_table.IsSimplifiedChinese() ? 1 : 0);
    defaults.is_windows7_or_8 = (m_win_version.IsWindows7() || m_win_version.IsWindows8Or8point1());
    defaults.is_windows8_or_later = m_win_version.IsWindows8OrLater();
    defaults.is_windows10_or_later = m_win_version.IsWindows10OrLater();
    defaults.is_windows7 = m_win_version.IsWindows7();
    defaults.default_light_theme = CWindowsSettingHelper::IsWindows10LightTheme();
    defaults.d2d_supported = CTaskBarDlgDrawCommonSupport::CheckSupport();

    store.Load(m_general_data, m_taskbar_data, m_cfg_data, defaults);
    ConfigStore::Validate(m_general_data, m_taskbar_data);

    //Windows10颜色模式设置
    if (defaults.default_light_theme)
        CCommon::SetColorMode(ColorMode::Light);
    else
        CCommon::SetColorMode(ColorMode::Default);

    if (m_cfg_data.m_notify_icon_auto_adapt)
        AutoSelectNotifyIcon();
}


void CTaskbarMonApp::SaveConfig()
{
    ConfigStore store{ m_config_path };
    ConfigStore::OtherSettings other;
    other.no_multistart_warning = m_no_multistart_warning;
    other.exit_when_start_by_restart_manager = m_exit_when_start_by_restart_manager;
    other.debug_log = m_debug_log;
    other.notify_interval = m_notify_interval;
    other.taksbar_transparent_color_enable = m_taksbar_transparent_color_enable;
    other.last_light_mode = m_last_light_mode;
    other.show_dot_net_notinstalled_tip = m_show_dot_net_notinstalled_tip;

    //检查是否保存成功
    if (!store.Save(m_general_data, m_taskbar_data, m_cfg_data, other, VERSION))
    {
        if (m_cannot_save_config_warning)
        {
            CString info = CCommon::LoadText(IDS_CONNOT_SAVE_CONFIG_WARNING);
            info.Replace(_T("<%file_path%>"), m_config_path.c_str());
            AfxMessageBox(info, MB_ICONWARNING);
        }
        m_cannot_save_config_warning = false;
        return;
    }
}


void CTaskbarMonApp::LoadGlobalConfig()
{
    bool portable_mode_default{ false };
    wstring global_cfg_path{ m_module_dir + L"global_cfg.ini" };
    if (!CCommon::FileExist(global_cfg_path.c_str()))       //如果global_cfg.ini不存在，则根据AppData/Roaming/TaskbarMon目录下是否存在config.ini来判断配置文件的保存位置
    {
        portable_mode_default = !CCommon::FileExist((m_appdata_dir + L"config.ini").c_str());
    }

    CIniHelper ini{ global_cfg_path };
    m_general_data.portable_mode = ini.GetBool(L"config", L"portable_mode", portable_mode_default);

    //执行一次保存操作，以检查当前目录是否有写入权限
    m_module_dir_writable = ini.Save();

    if (m_module_dir.find(CCommon::GetTemplateDir()) != wstring::npos)      //如果当前路径是在Temp目录下，则强制将数据保存到Appdata
    {
        m_module_dir_writable = false;
    }

    if (!m_module_dir_writable)              //如果当前目录没有写入权限，则设置配置保存到AppData目录
    {
        m_general_data.portable_mode = false;
    }
}

void CTaskbarMonApp::SaveGlobalConfig()
{
    CIniHelper ini{ m_module_dir + L"global_cfg.ini" };
    ini.WriteBool(L"config", L"portable_mode", m_general_data.portable_mode);

    //检查是否保存成功
    if (!ini.Save())
    {
        //if (m_cannot_save_global_config_warning)
        //{
        //    CString info;
        //    info.LoadString(IDS_CONNOT_SAVE_CONFIG_WARNING);
        //    info.Replace(_T("<%file_path%>"), m_module_dir.c_str());
        //    AfxMessageBox(info, MB_ICONWARNING);
        //}
        //m_cannot_save_global_config_warning = false;
        //return;
    }
}

int CTaskbarMonApp::DPI(int pixel)
{
    return m_dpi * pixel / 96;
}

void CTaskbarMonApp::DPI(CRect& rect)
{
    rect.left = DPI(rect.left);
    rect.right = DPI(rect.right);
    rect.top = DPI(rect.top);
    rect.bottom = DPI(rect.bottom);
}

void CTaskbarMonApp::DPIFromWindow(CWnd* pWnd)
{
    CWindowDC dc(pWnd);
    HDC hDC = dc.GetSafeHdc();
    if (hDC != nullptr)
    {
        const int dpi = GetDeviceCaps(hDC, LOGPIXELSY);
        if (dpi > 0)
            m_dpi = dpi;
    }
}

void CTaskbarMonApp::CheckUpdate(bool message)
{
    if (m_checking_update)      //如果还在检查更新，则直接返回
        return;
    CFlagLocker update_locker(m_checking_update);
    CWaitCursor wait_cursor;

    wstring version;        //程序版本
    wstring link;           //下载链接
    wstring contents_zh_cn; //更新内容（简体中文）
    wstring contents_en;    //更新内容（English）
    wstring contents_zh_tw; //更新内容（繁体中文）
    CUpdateHelper update_helper;
    update_helper.SetUpdateSource(static_cast<CUpdateHelper::UpdateSource>(m_general_data.update_source));
    if (!update_helper.CheckForUpdate())
    {
        if (message)
            AfxMessageBox(CCommon::LoadText(IDS_CHECK_UPDATE_FAILD), MB_OK | MB_ICONWARNING);
        return;
    }
    version = update_helper.GetVersion();
#ifdef _M_X64
    link = update_helper.GetLink64();
#elif defined _M_ARM64EC
    link = update_helper.GetLinkArm64ec();
#else
    link = update_helper.GetLink();
#endif
    contents_zh_cn = update_helper.GetContentsZhCn();
    contents_en = update_helper.GetContentsEn();
    contents_zh_tw = update_helper.GetContentsZhTw();
    if (version.empty() || link.empty())
    {
        if (message)
        {
            CString info = CCommon::LoadText(IDS_CHECK_UPDATE_ERROR);
            info += _T("\r\nrow_data=");
            info += std::to_wstring(update_helper.IsRowData()).c_str();

            AfxMessageBox(info, MB_OK | MB_ICONWARNING);
        }
        return;
    }
    if (version > VERSION)      //如果服务器上的版本大于本地版本
    {
        CString info;
        //根据语言设置选择对应语言版本的更新内容
        wstring language_tag = m_str_table.GetLanguageInfo().bcp_47;
        wstring contents_lan;
        if (language_tag == L"zh-CN")
            contents_lan = contents_zh_cn;
        else if (language_tag == L"zh-TW")
            contents_lan = contents_zh_tw;
        else
            contents_lan = contents_en;
        if (contents_lan.empty())
            info = CCommon::LoadTextFormat(IDS_UPDATE_AVLIABLE, { version });
        else
            info = CCommon::LoadTextFormat(IDS_UPDATE_AVLIABLE2, { version, contents_lan });

        if (AfxMessageBox(info, MB_YESNO | MB_ICONQUESTION) == IDYES)
        {
            ShellExecute(NULL, _T("open"), link.c_str(), NULL, NULL, SW_SHOW);      //转到下载链接
        }
    }
    else
    {
        if (message)
            AfxMessageBox(CCommon::LoadText(IDS_ALREADY_UPDATED), MB_OK | MB_ICONINFORMATION);
    }
}

void CTaskbarMonApp::CheckUpdateInThread(bool message)
{
    // Update checks intentionally fail closed until this project owns an
    // authenticated release manifest.  Do this on the UI thread so a manual
    // check never opens an MFC message box from a worker thread.
    CUpdateHelper update_helper;
    if (!update_helper.IsUpdateCheckSupported())
    {
        if (message)
            AfxMessageBox(CCommon::LoadText(IDS_CHECK_UPDATE_FAILD), MB_OK | MB_ICONWARNING);
        return;
    }

    // A future project-owned update channel must provide a worker-to-UI
    // result callback. Do not run the legacy implementation on a worker: it
    // displays MFC dialogs and would violate UI-thread affinity.
    if (message)
        AfxMessageBox(CCommon::LoadText(IDS_CHECK_UPDATE_FAILD), MB_OK | MB_ICONWARNING);
}

UINT CTaskbarMonApp::InitOpenHardwareMonitorLibThreadFunc(LPVOID lpParam)
{
#ifndef WITHOUT_TEMPERATURE
    std::unique_ptr<OpenHardwareMonitorInitializationRequest> request(
        static_cast<OpenHardwareMonitorInitializationRequest*>(lpParam));
    if (request == nullptr || request->enabled_items == 0)
        return 0;

    // Initialization can block in the managed hardware library. Keep it out
    // of the application lock, then publish only if the UI has not changed the
    // requested hardware configuration in the meantime.
    const auto monitor = OpenHardwareMonitorApi::CreateInstance();
    if (monitor == nullptr)
    {
        ::OutputDebugStringW(L"TaskbarMon: OpenHardwareMonitor initialization failed.\n");
        return 0;
    }

    CSingleLock sync(&theApp.m_minitor_lib_critical, TRUE);
    if (request->generation != theApp.m_hardware_monitor_generation.load(std::memory_order_acquire))
        return 0;

    monitor->SetCpuEnable((request->enabled_items & HI_CPU) != 0);
    monitor->SetGpuEnable((request->enabled_items & HI_GPU) != 0);
    monitor->SetHddEnable((request->enabled_items & HI_HDD) != 0);
    monitor->SetMainboardEnable((request->enabled_items & HI_MBD) != 0);
    theApp.m_pMonitor = monitor;
#endif
    return 0;
}

bool  CTaskbarMonApp::SetAutoRun(bool auto_run, bool task_scheduler)
{
    if (task_scheduler)
    {
        //使用任务计划的方式设置开机自启动
        return SetAutoRunByTaskScheduler(auto_run);
    }
    else
    {
        //使用添加注册表项的方式实现开机自启动
        return SetAutoRunByRegistry(auto_run);
    }
}

bool CTaskbarMonApp::GetAutoRun(wstring* auto_run_path, bool task_scheduler)
{
    if (auto_run_path != nullptr)
        auto_run_path->clear();

    if (task_scheduler)
    {
        //使用任务计划的方式实现开机自启动
        return is_auto_start_task_active_for_this_user(auto_run_path);
    }
    else
    {
        //使用添加注册表项的方式实现开机自启动
        CRegKey key;
        if (key.Open(HKEY_CURRENT_USER, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Run")) != ERROR_SUCCESS)
        {
            //打开注册表“Software\\Microsoft\\Windows\\CurrentVersion\\Run”失败，则返回false
            return false;
        }
        wstring registered_path;
        if (!ReadRegistryStringValue(key, APP_NAME, registered_path))
            return false;

        const wstring executable_path = StripSurroundingQuotes(registered_path);
        if (auto_run_path != nullptr)
            *auto_run_path = executable_path;

        // A value with our display name may belong to another installation or
        // have been modified. It is not an active TaskbarMon autorun entry
        // unless it resolves to this executable.
        return SameWindowsPath(executable_path, m_module_path);
    }
}

bool CTaskbarMonApp::SetAutoRunByRegistry(bool auto_run)
{
    CRegKey key;
    //打开注册表项
    if (key.Open(HKEY_CURRENT_USER, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Run")) != ERROR_SUCCESS)
    {
        AfxMessageBox(CCommon::LoadText(IDS_AUTORUN_FAILED_NO_KEY), MB_OK | MB_ICONWARNING);
        return false;
    }
    if (auto_run)       //写入注册表项
    {
        //通过注册表设置开机自启动项时删除计划任务中的自启动项
        if (!SetAutoRunByTaskScheduler(false))
            return false;

        if (key.SetStringValue(APP_NAME, m_module_path_reg.c_str()) != ERROR_SUCCESS)
        {
            AfxMessageBox(CCommon::LoadText(IDS_AUTORUN_FAILED_NO_ACCESS), MB_OK | MB_ICONWARNING);
            return false;
        }
    }
    else        //删除注册表项
    {
        // Only remove an entry that points at this executable. Never delete
        // a same-named entry created by another installation or application.
        wstring registered_path;
        if (!ReadRegistryStringValue(key, APP_NAME, registered_path))
            return true;
        if (!SameWindowsPath(StripSurroundingQuotes(registered_path), m_module_path))
            return true;
        if (key.DeleteValue(APP_NAME) != ERROR_SUCCESS)
        {
            AfxMessageBox(CCommon::LoadText(IDS_AUTORUN_DELETE_FAILED), MB_OK | MB_ICONWARNING);
            return false;
        }
    }
    return true;
}

bool CTaskbarMonApp::SetAutoRunByTaskScheduler(bool auto_run)
{
    if (!auto_run)
        return delete_auto_start_task_for_this_user();

    // Create and verify the scheduled task before disabling the registry
    // fallback. This avoids losing a working autorun entry on scheduler
    // failure, and the helper refuses to replace an unverified same-name
    // task.
    if (!create_auto_start_task_for_this_user())
        return false;

    if (SetAutoRunByRegistry(false))
        return true;

    // Keep the two mechanisms mutually exclusive. The helper deletes only a
    // task whose full definition still matches this executable and user.
    delete_auto_start_task_for_this_user();
    return false;
}

CString CTaskbarMonApp::GetSystemInfoString()
{
    CString info;
    info += _T("System Info:\r\n");

    CString strTmp;
    strTmp.Format(_T("Windows Version: %d.%d build %d"), m_win_version.GetMajorVersion(),
        m_win_version.GetMinorVersion(), m_win_version.GetBuildNumber());
    info += strTmp;

    if (m_win_version.IsWine())
        info += _T(" (Wine)");

    info += _T("\r\n");

    strTmp.Format(_T("DPI: %d"), m_dpi);
    info += strTmp;
    info += _T("\r\n");

    info += _T("Version: ");
    info += VERSION;
    info += _T(" ");
#ifdef _M_X64
    info += _T("x64");
#elif defined _M_ARM64EC
    info += _T("Arm64EC");
#else
    info += _T("x86");
#endif

#ifdef WITHOUT_TEMPERATURE
    info += _T(" (Lite)");
#endif

    info += _T("\r\nLast compiled date: ");
    info += CCommon::GetLastCompileTime();

    return info;
}


void CTaskbarMonApp::InitMenuResourse()
{
    //载入菜单
    CCommon::LoadMenuResource(m_main_menu, IDR_MENU1);
    CCommon::LoadMenuResource(m_taskbar_menu, IDR_TASK_BAR_MENU);

    //为菜单项添加图标
    auto addIconsForMainWindowMenu = [&](const CMenu& menu)
    {
        CMenuIcon::AddIconToMenuItem(menu.GetSubMenu(0)->GetSafeHmenu(), 0, TRUE, GetMenuIcon(IDI_CONNECTION));
        CMenuIcon::AddIconToMenuItem(menu.GetSubMenu(0)->GetSafeHmenu(), 5, TRUE, GetMenuIcon(IDI_FUNCTION));
        CMenuIcon::AddIconToMenuItem(menu.GetSafeHmenu(), ID_NETWORK_INFO, FALSE, GetMenuIcon(IDI_INFO));
        CMenuIcon::AddIconToMenuItem(menu.GetSafeHmenu(), ID_SHOW_TASK_BAR_WND, FALSE, GetMenuIcon(IDI_TASKBAR_WINDOW));
        CMenuIcon::AddIconToMenuItem(menu.GetSafeHmenu(), ID_CHANGE_NOTIFY_ICON, FALSE, GetMenuIcon(IDI_NOTIFY));
        CMenuIcon::AddIconToMenuItem(menu.GetSafeHmenu(), ID_TRAFFIC_HISTORY, FALSE, GetMenuIcon(IDI_STATISTICS));
        CMenuIcon::AddIconToMenuItem(menu.GetSafeHmenu(), ID_OPTIONS, FALSE, GetMenuIcon(IDI_SETTINGS));
        CMenuIcon::AddIconToMenuItem(menu.GetSubMenu(0)->GetSafeHmenu(), 9, TRUE, GetMenuIcon(IDI_HELP));
        CMenuIcon::AddIconToMenuItem(menu.GetSafeHmenu(), ID_HELP, FALSE, GetMenuIcon(IDI_HELP));
        CMenuIcon::AddIconToMenuItem(menu.GetSafeHmenu(), ID_APP_ABOUT, FALSE, GetMenuIcon(IDR_MAINFRAME));
        CMenuIcon::AddIconToMenuItem(menu.GetSafeHmenu(), ID_APP_EXIT, FALSE, GetMenuIcon(IDI_EXIT));
    };
    //托盘右键菜单
    addIconsForMainWindowMenu(m_main_menu);

    //任务栏窗口右键菜单
    auto addIconsForTaksbarWindowMenu = [&](const CMenu& menu)
    {
        CMenuIcon::AddIconToMenuItem(menu.GetSubMenu(0)->GetSafeHmenu(), 0, TRUE, GetMenuIcon(IDI_CONNECTION));
        CMenuIcon::AddIconToMenuItem(menu.GetSafeHmenu(), ID_NETWORK_INFO, FALSE, GetMenuIcon(IDI_INFO));
        CMenuIcon::AddIconToMenuItem(menu.GetSafeHmenu(), ID_TRAFFIC_HISTORY, FALSE, GetMenuIcon(IDI_STATISTICS));
        CMenuIcon::AddIconToMenuItem(menu.GetSafeHmenu(), ID_DISPLAY_SETTINGS, FALSE, GetMenuIcon(IDI_ITEM));
        CMenuIcon::AddIconToMenuItem(menu.GetSafeHmenu(), ID_SHOW_TASK_BAR_WND, FALSE, GetMenuIcon(IDI_CLOSE));
        CMenuIcon::AddIconToMenuItem(menu.GetSafeHmenu(), ID_OPEN_TASK_MANAGER, FALSE, GetMenuIcon(IDI_TASK_MANAGER));
        CMenuIcon::AddIconToMenuItem(menu.GetSafeHmenu(), ID_OPTIONS2, FALSE, GetMenuIcon(IDI_SETTINGS));
        CMenuIcon::AddIconToMenuItem(menu.GetSubMenu(0)->GetSafeHmenu(), 11, TRUE, GetMenuIcon(IDI_HELP));
        CMenuIcon::AddIconToMenuItem(menu.GetSafeHmenu(), ID_HELP, FALSE, GetMenuIcon(IDI_HELP));
        CMenuIcon::AddIconToMenuItem(menu.GetSafeHmenu(), ID_APP_ABOUT, FALSE, GetMenuIcon(IDR_MAINFRAME));
        CMenuIcon::AddIconToMenuItem(menu.GetSafeHmenu(), ID_APP_EXIT, FALSE, GetMenuIcon(IDI_EXIT));
    };
    addIconsForTaksbarWindowMenu(m_taskbar_menu);

#ifdef _DEBUG
    m_main_menu.GetSubMenu(0)->AppendMenu(MF_BYCOMMAND, ID_CMD_TEST, _T("Test Command"));
#endif
}

HICON CTaskbarMonApp::GetMenuIcon(UINT id)
{
    auto iter = m_menu_icons.find(id);
    if (iter != m_menu_icons.end())
    {
        return iter->second;
    }
    else
    {
        HICON hIcon = CCommon::LoadIconResource(id, DPI(16));
        m_menu_icons[id] = hIcon;
        return hIcon;
    }
}

void CTaskbarMonApp::AutoSelectNotifyIcon()
{
    if (m_win_version.GetMajorVersion() >= 10)
    {
        bool light_mode = CWindowsSettingHelper::IsWindows10LightTheme();
        if (light_mode)     //浅色模式下，如果图标是白色，则改成黑色
        {
            if (m_cfg_data.m_notify_icon_selected == 0)
                m_cfg_data.m_notify_icon_selected = 4;
            if (m_cfg_data.m_notify_icon_selected == 1)
                m_cfg_data.m_notify_icon_selected = 5;
        }
        else     //深色模式下，如果图标是黑色，则改成白色
        {
            if (m_cfg_data.m_notify_icon_selected == 4)
                m_cfg_data.m_notify_icon_selected = 0;
            if (m_cfg_data.m_notify_icon_selected == 5)
                m_cfg_data.m_notify_icon_selected = 1;
        }
    }
}

// 唯一的一个 CTaskbarMonApp 对象

CTaskbarMonApp theApp;


// CTaskbarMonApp 初始化

BOOL CTaskbarMonApp::InitInstance()
{
    //替换掉对话框程序的默认类名
    WNDCLASS wc;
    ::GetClassInfo(AfxGetInstanceHandle(), _T("#32770"), &wc);       //MFC默认的所有对话框的类名为#32770
    wc.lpszClassName = APP_CLASS_NAME;      //将对话框的类名修改为新类名
    AfxRegisterClass(&wc);

    //设置配置文件的路径
    if (!GetCurrentModulePath(m_module_path))
    {
        ::MessageBoxW(nullptr,
            L"TaskbarMon could not determine its executable path safely. The application will not start.",
            L"TaskbarMon",
            MB_OK | MB_ICONERROR);
        return FALSE;
    }
    if (m_module_path.find(L' ') != wstring::npos)
    {
        //如果路径中有空格，则在程序路径前后添加双引号
        m_module_path_reg = L'\"';
        m_module_path_reg += m_module_path;
        m_module_path_reg += L'\"';
    }
    else
    {
        m_module_path_reg = m_module_path;
    }
    const size_t module_directory_separator = m_module_path.find_last_of(L"\\/");
    if (module_directory_separator == wstring::npos)
    {
        ::MessageBoxW(nullptr,
            L"TaskbarMon could not determine its executable directory safely. The application will not start.",
            L"TaskbarMon",
            MB_OK | MB_ICONERROR);
        return FALSE;
    }
    m_module_dir = m_module_path.substr(0, module_directory_separator + 1);
    m_system_dir = CCommon::GetSystemDir();
    if (!CCommon::GetAppDataConfigDir(m_appdata_dir))
    {
        ::MessageBoxW(nullptr,
            L"TaskbarMon could not access its per-user configuration directory. The application will not start.",
            L"TaskbarMon",
            MB_OK | MB_ICONERROR);
        return FALSE;
    }
    if (!CCommon::MigrateLegacyAppDataConfig(m_appdata_dir))
    {
        ::MessageBoxW(nullptr,
            L"TaskbarMon could not fully migrate legacy TrafficMonitor files. The original files were left unchanged.",
            L"TaskbarMon",
            MB_OK | MB_ICONWARNING);
    }

    LoadGlobalConfig();

#ifdef _DEBUG
    m_config_dir = L".\\";
#else
    if (m_general_data.portable_mode)
        m_config_dir = m_module_dir;
    else
        m_config_dir = m_appdata_dir;
#endif
    //AppData里面的程序配置文件路径
    m_config_path = m_config_dir + L"config.ini";
    m_history_traffic_path = m_config_dir + L"history_traffic.dat";
    m_log_path = m_config_dir + L"error.log";

    //#ifndef _DEBUG
    //  //原来的、程序所在目录下的配置文件的路径
    //  wstring config_path_old = m_module_dir + L"config.ini";
    //  wstring history_traffic_path_old = m_module_dir + L"history_traffic.dat";
    //  wstring log_path_old = m_module_dir + L"error.log";
    //  //如果程序所在目录下含有配置文件，则将其移动到AppData对应的目录下面
    //  CCommon::MoveAFile(config_path_old.c_str(), m_config_path.c_str());
    //  CCommon::MoveAFile(history_traffic_path_old.c_str(), m_history_traffic_path.c_str());
    //  CCommon::MoveAFile(log_path_old.c_str(), m_log_path.c_str());
    //#endif // !_DEBUG

    LoadLanguageConfig();

    //初始化界面语言
    CCommon::SetThreadLanguage(m_general_data.language.language_id);

    //初始化字符串资源
    m_str_table.Init();

    //检查是否已有实例正在运行
    ConfigStore::OtherSettings other_settings;
    {
        ConfigStore store{ m_config_path };
        ConfigStore::EnvironmentDefaults defaults;
        defaults.is_windows7 = m_win_version.IsWindows7();
        defaults.default_light_theme = CWindowsSettingHelper::IsWindows10LightTheme();
        store.LoadOther(other_settings, defaults);
    }
    m_no_multistart_warning = other_settings.no_multistart_warning;
    m_exit_when_start_by_restart_manager = other_settings.exit_when_start_by_restart_manager;
    m_debug_log = other_settings.debug_log;
    m_notify_interval = other_settings.notify_interval;
    m_taksbar_transparent_color_enable = other_settings.taksbar_transparent_color_enable;
    m_last_light_mode = other_settings.last_light_mode;
    m_show_dot_net_notinstalled_tip = other_settings.show_dot_net_notinstalled_tip;
    LPCTSTR mutex_name{};
#ifdef _DEBUG
    mutex_name = _T("Local\\TaskbarMon-e8Ahk24HP6JC8hDy");
#else
    mutex_name = _T("Local\\TaskbarMon-1419J3XLKL1w8OZc");
#endif
    m_instance_mutex = ::CreateMutex(nullptr, FALSE, mutex_name);
    const DWORD mutex_error = GetLastError();
    if (m_instance_mutex == nullptr)
    {
        ::MessageBoxW(nullptr,
            L"TaskbarMon could not create its single-instance guard. The application will not start.",
            L"TaskbarMon",
            MB_OK | MB_ICONERROR);
        return FALSE;
    }
    if (mutex_error == ERROR_ALREADY_EXISTS)
    {
        ::CloseHandle(m_instance_mutex);
        m_instance_mutex = nullptr;

        //char buff[128];
        //string cmd_line_str{ CCommon::UnicodeToStr(cmd_line.c_str()) };
        //sprintf_s(buff, "when_start=%d, m_no_multistart_warning=%d, cmd_line=%s", when_start, m_no_multistart_warning, cmd_line_str.c_str());
        //CCommon::WriteLog(buff, _T(".\\start.log"));
        if (!m_no_multistart_warning)
        {
            //查找已存在 TaskbarMon 进程的主窗口的句柄
            HWND exist_handel = ::FindWindow(APP_CLASS_NAME, NULL);
            if (exist_handel != NULL)
            {
                //弹出“TaskbarMon 已在运行”对话框
                CAppAlreadyRuningDlg dlg(exist_handel);
                dlg.DoModal();
            }
            else
            {
                AfxMessageBox(CCommon::LoadText(IDS_AN_INSTANCE_RUNNING));
            }
        }
        return FALSE;
    }

    //初始化GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok)
    {
        m_gdiplusToken = 0;
        ::MessageBoxW(nullptr, L"TaskbarMon could not initialize GDI+.", L"TaskbarMon",
                      MB_OK | MB_ICONERROR);
        return FALSE;
    }

    //从ini文件载入设置
    LoadConfig();

    m_taskbar_default_style.LoadConfig();

    //SaveConfig();

    // 如果一个运行在 Windows XP 上的应用程序清单指定要
    // 使用 ComCtl32.dll 版本 6 或更高版本来启用可视化方式，
    //则需要 InitCommonControlsEx()。  否则，将无法创建窗口。
    INITCOMMONCONTROLSEX InitCtrls;
    InitCtrls.dwSize = sizeof(InitCtrls);
    // 将它设置为包括所有要在应用程序中使用的
    // 公共控件类。
    InitCtrls.dwICC = ICC_WIN95_CLASSES;
    if (!InitCommonControlsEx(&InitCtrls))
    {
        ::MessageBoxW(nullptr, L"TaskbarMon could not initialize Windows common controls.",
                      L"TaskbarMon", MB_OK | MB_ICONERROR);
        return FALSE;
    }

    if (!CWinApp::InitInstance())
        return FALSE;


    AfxEnableControlContainer();

    // 创建 shell 管理器，以防对话框包含
    // 任何 shell 树视图控件或 shell 列表视图控件。
    CShellManager* pShellManager = new CShellManager;

    // 激活“Windows Native”视觉管理器，以便在 MFC 控件中启用主题
    CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerWindows));

    // 标准初始化
    // 如果未使用这些功能并希望减小
    // 最终可执行文件的大小，则应移除下列
    // 不需要的特定初始化例程
    // 更改用于存储设置的注册表项
    // TODO: 应适当修改该字符串，
    // 例如修改为公司或组织名
    //SetRegistryKey(_T("应用程序向导生成的本地应用程序"));        //暂不使用注册表保存数据

    //启动时检查更新
    if (m_general_data.check_update_when_start)
    {
        CheckUpdateInThread(false);
    }

#ifndef WITHOUT_TEMPERATURE
    //检测是否安装.net framework 4.5
    if (!CWindowsSettingHelper::IsDotNetFramework4Point5Installed())
    {
        if (theApp.m_show_dot_net_notinstalled_tip)
        {
            if (AfxMessageBox(CCommon::LoadText(IDS_DOTNET_NOT_INSTALLED_TIP), MB_OKCANCEL | MB_ICONWARNING) == IDCANCEL)       //点击“取消”不再提示
            {
                theApp.m_show_dot_net_notinstalled_tip = false;
                SaveConfig();
            }
        }
    }
    else
    {
        //如果没有开启任何一项的硬件监控，则不初始化OpenHardwareMonitor
        if (theApp.m_general_data.IsHardwareEnable(HI_CPU) || theApp.m_general_data.IsHardwareEnable(HI_GPU)
            || theApp.m_general_data.IsHardwareEnable(HI_HDD) || theApp.m_general_data.IsHardwareEnable(HI_MBD))
        {
            //启动初始化OpenHardwareMonitor的线程。由于OpenHardwareMonitor初始化需要一定的时间，为了防止启动时程序卡顿，将其放到后台线程中处理
            InitOpenHardwareLibInThread();
        }
    }
#endif

    //执行测试代码
#ifdef _DEBUG
    CTest::Test();
#endif

    CTrafficMonitorDlg dlg;
    m_pMainWnd = &dlg;
    INT_PTR nResponse = dlg.DoModal();
    if (nResponse == IDOK)
    {
        // TODO: 在此放置处理何时用
        //  “确定”来关闭对话框的代码
    }
    else if (nResponse == IDCANCEL)
    {
        // TODO: 在此放置处理何时用
        //  “取消”来关闭对话框的代码
    }
    else if (nResponse == -1)
    {
        TRACE(traceAppMsg, 0, "警告: 对话框创建失败，应用程序将意外终止。\n");
        TRACE(traceAppMsg, 0, "警告: 如果您在对话框上使用 MFC 控件，则无法 #define _AFX_NO_MFC_CONTROLS_IN_DIALOGS。\n");
    }

    // 删除上面创建的 shell 管理器。
    if (pShellManager != NULL)
    {
        delete pShellManager;
    }

#ifndef _AFXDLL
    ControlBarCleanUp();
#endif

    // 由于对话框已关闭，所以将返回 FALSE 以便退出应用程序，
    //  而不是启动应用程序的消息泵。
    return FALSE;
}

void CTaskbarMonApp::InvalidateOpenHardwareInitialization()
{
#ifndef WITHOUT_TEMPERATURE
    m_hardware_monitor_generation.fetch_add(1, std::memory_order_acq_rel);
#endif
}

void CTaskbarMonApp::InitOpenHardwareLibInThread()
{
#ifndef WITHOUT_TEMPERATURE
    auto request = std::make_unique<OpenHardwareMonitorInitializationRequest>();
    request->generation = m_hardware_monitor_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
    request->enabled_items = m_general_data.hardware_monitor_item;
    if (request->enabled_items == 0)
        return;

    if (AfxBeginThread(InitOpenHardwareMonitorLibThreadFunc, request.get()) == nullptr)
    {
        ::OutputDebugStringW(L"TaskbarMon: unable to start OpenHardwareMonitor initialization worker.\n");
        return;
    }
    request.release();
#endif
}


void CTaskbarMonApp::UpdateOpenHardwareMonitorEnableState()
{
#ifndef WITHOUT_TEMPERATURE
    const unsigned int enabled_items = m_general_data.hardware_monitor_item;
    CSingleLock sync(&m_minitor_lib_critical, TRUE);
    if (m_pMonitor != nullptr)
    {
        m_pMonitor->SetCpuEnable((enabled_items & HI_CPU) != 0);
        m_pMonitor->SetGpuEnable((enabled_items & HI_GPU) != 0);
        m_pMonitor->SetHddEnable((enabled_items & HI_HDD) != 0);
        m_pMonitor->SetMainboardEnable((enabled_items & HI_MBD) != 0);
    }
#endif
}

//void CTaskbarMonApp::UpdateTaskbarWndMenu()
//{
//    //获取“显示设置”子菜单
//    CMenu* pMenu = m_taskbar_menu.GetSubMenu(0)->GetSubMenu(5);
//    ASSERT(pMenu != nullptr);
//    if (pMenu != nullptr)
//    {
//        //将ID_SHOW_MEMORY_USAGE后面的所有菜单项删除
//        if (pMenu->GetMenuItemCount() > 4)
//        {
//            int start_pos = CCommon::GetMenuItemPosition(pMenu, ID_SHOW_MEMORY_USAGE) + 1;
//            while (pMenu->GetMenuItemCount() > start_pos)
//            {
//                pMenu->DeleteMenu(start_pos, MF_BYPOSITION);
//            }
//        }
//
//        //添加温度相关菜单项
//#ifndef WITHOUT_TEMPERATURE
//        if (m_general_data.IsHardwareEnable(HI_GPU))
//            pMenu->AppendMenu(MF_STRING | MF_ENABLED, ID_SHOW_GPU, CCommon::LoadText(IDS_GPU_USAGE));
//        if (m_general_data.IsHardwareEnable(HI_CPU))
//            pMenu->AppendMenu(MF_STRING | MF_ENABLED, ID_SHOW_CPU_TEMPERATURE, CCommon::LoadText(IDS_CPU_TEMPERATURE));
//        if (m_general_data.IsHardwareEnable(HI_GPU))
//            pMenu->AppendMenu(MF_STRING | MF_ENABLED, ID_SHOW_GPU_TEMPERATURE, CCommon::LoadText(IDS_GPU_TEMPERATURE));
//        if (m_general_data.IsHardwareEnable(HI_HDD))
//            pMenu->AppendMenu(MF_STRING | MF_ENABLED, ID_SHOW_HDD_TEMPERATURE, CCommon::LoadText(IDS_HDD_TEMPERATURE));
//        if (m_general_data.IsHardwareEnable(HI_MBD))
//            pMenu->AppendMenu(MF_STRING | MF_ENABLED, ID_SHOW_MAIN_BOARD_TEMPERATURE, CCommon::LoadText(IDS_MAINBOARD_TEMPERATURE));
//        if (m_general_data.IsHardwareEnable(HI_HDD))
//            pMenu->AppendMenu(MF_STRING | MF_ENABLED, ID_SHOW_HDD, CCommon::LoadText(IDS_HDD_USAGE));
//#endif
//
//        pMenu->AppendMenu(MF_STRING | MF_ENABLED, ID_SHOW_TOTAL_SPEED, CCommon::LoadText(IDS_TOTAL_NET_SPEED));
//
//        //添加插件菜单项
//        if (!m_plugins.GetPluginItems().empty())
//            pMenu->AppendMenu(MF_SEPARATOR);
//        int plugin_index = 0;
//        for (const auto& plugin_item : m_plugins.GetPluginItems())
//        {
//            pMenu->AppendMenu(MF_STRING | MF_ENABLED, ID_SHOW_PLUGIN_ITEM_START + plugin_index, plugin_item->GetItemName());
//            plugin_index++;
//        }
//    }
//}

bool CTaskbarMonApp::IsForceShowNotifyIcon()
{
    return !m_cfg_data.m_show_task_bar_wnd;    //如果没有显示任务栏窗口，则强制显示通知区图标，否则无法呼出右键菜单
}

void CTaskbarMonApp::CheckWindows11Taskbar()
{
    // 在“Shell_TrayWnd”的子窗口找到类名为“Windows.UI.Composition.DesktopWindowContentBridge”的窗口则认为是Windows11的任务栏
    if (m_win_version.IsWindows11OrLater())
    {
        HWND hTaskbar = ::FindWindow(L"Shell_TrayWnd", NULL);
        m_is_windows11_taskbar = (::FindWindowExW(hTaskbar, 0, L"Windows.UI.Composition.DesktopWindowContentBridge", NULL) != NULL);
    }
    else
    {
        m_is_windows11_taskbar = false;
    }
}

bool CTaskbarMonApp::DPIFromRect(const RECT& rect, UINT* out_dpi_x, UINT* out_dpi_y)
{
    HMONITOR h_current_monitor = ::MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    HRESULT hr = m_dll_functions.GetDpiForMonitor(h_current_monitor, MDT_EFFECTIVE_DPI, out_dpi_x, out_dpi_y);
    return hr == S_OK;
}

unsigned int CTaskbarMonApp::GetThemeColor() const
{
    return m_theme_color;
}

void CTaskbarMonApp::SetThemeColor(COLORREF color)
{
    m_theme_color = color;
}

void CTaskbarMonApp::OnHelp()
{
    // Do not send TaskbarMon users to an upstream project's mutable docs.
    // The fork's repository is the only currently supported help endpoint.
    ShellExecuteW(nullptr, L"open", L"https://github.com/ywh865/TaskbarMon", nullptr, nullptr, SW_SHOW);
}


void CTaskbarMonApp::OnFrequentyAskedQuestions()
{
    ShellExecuteW(nullptr, L"open", L"https://github.com/ywh865/TaskbarMon", nullptr, nullptr, SW_SHOW);
}


void CTaskbarMonApp::OnUpdateLog()
{
    ShellExecuteW(nullptr, L"open", L"https://github.com/ywh865/TaskbarMon", nullptr, nullptr, SW_SHOW);
}


int CTaskbarMonApp::ExitInstance()
{
    InvalidateOpenHardwareInitialization();
#ifndef WITHOUT_TEMPERATURE
    {
        CSingleLock sync(&m_minitor_lib_critical, TRUE);
        m_pMonitor.reset();
    }
#endif

    // 释放GDI+
    if (m_gdiplusToken != 0)
    {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusToken = 0;
    }

    if (m_instance_mutex != nullptr)
    {
        ::CloseHandle(m_instance_mutex);
        m_instance_mutex = nullptr;
    }

    return CWinApp::ExitInstance();
}


// end of file
