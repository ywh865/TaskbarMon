
// TrafficMonitorDlg.h : 头文件
//

#pragma once
#pragma comment (lib, "iphlpapi.lib")

#include "NetworkInfoDlg.h"
#include "afxwin.h"
#include "StaticEx.h"
#include "Common.h"
#include "TaskBarDlg.h"
#include "HistoryTrafficDlg.h"
#include "OptionsDlg.h"
#include "IconSelectDlg.h"
#include "DrawCommon.h"
#include "IniHelper.h"
#include "AdapterCommon.h"
#include "AboutDlg.h"
#include "core/MonitorService.h"

// CTrafficMonitorDlg 对话框（隐藏宿主窗口，仅承载任务栏窗口、托盘与监控调度）
class CTrafficMonitorDlg : public CDialog
{
    // 构造
public:
    CTrafficMonitorDlg(CWnd* pParent = NULL);   // 标准构造函数
    ~CTrafficMonitorDlg();
    CTaskBarDlg* GetTaskbarWindow() const;

    static CTrafficMonitorDlg* Instance();

    // 对话框数据
#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_TRAFFICMONITOR_DIALOG };
#endif

    // 磁盘利用率是否由 PDH 获取（供选项对话框的硬盘选择）
    bool IsGetDiskUsageByPdh() const { return m_monitor_service.GetDiskUsageByPdh(); }
    // PDH 硬盘名称列表（供选项对话框的硬盘选择）
    const std::vector<CString>& GetDiskNames() const { return m_monitor_service.GetDiskNames(); }

#ifndef WITHOUT_TEMPERATURE
    // 硬件数据提供者（封装 OpenHardwareMonitor 访问，供 MonitorService 采样）
    class CHardwareDataProvider : public IHardwareDataProvider
    {
    public:
        bool IsAvailable() const override;
        void Acquire() override;
        float CpuTemperature() const override;
        float CpuFreq() const override;
        float GpuTemperature() const override;
        float HddTemperature() const override;
        float MainboardTemperature() const override;
        int GpuUsage() const override;
        std::map<std::wstring, float> AllCpuTemperature() const override;
        std::map<std::wstring, float> AllHddTemperature() const override;
        std::map<std::wstring, float> AllHddUsage() const override;
    };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持


// 实现
protected:
    NOTIFYICONDATA m_ntIcon;    //通知区域图标
    CTaskBarDlg* m_tBarDlg{};     //任务栏窗口的指针

    bool m_first_start{ true };     //初始时为true，在定时器第一次启动后置为flase

    // https://www.jianshu.com/p/9d4b68cdbd99
    struct Monitors
    {
        std::vector<MONITORINFO> monitorinfos;

        static BOOL CALLBACK MonitorEnum(HMONITOR hMon, HDC hdc, LPRECT lprcMonitor, LPARAM pData)
        {
            MONITORINFO iMonitor;
            iMonitor.cbSize = sizeof(MONITORINFO);
            GetMonitorInfo(hMon, &iMonitor);

            Monitors* pThis = reinterpret_cast<Monitors*>(pData);
            pThis->monitorinfos.push_back(iMonitor);
            return TRUE;
        }

        Monitors()
        {
            EnumDisplayMonitors(0, 0, MonitorEnum, (LPARAM)this);
        }
    };

    vector<CRect> m_screen_rects;       //所有屏幕的范围（不包含任务栏）
    vector<CRect> m_last_screen_rects;       //上一次所有屏幕的范围（不包含任务栏）
    CSize m_screen_size;        //屏幕的大小（包含任务栏）

    unsigned int m_timer_cnt{};     //定时器触发次数（自程序启动以来的秒数）
    unsigned int m_taskbar_timer_cnt{0}; //适用于TaskBarDlg的定时器触发次数（自程序启动以来的秒数）
    uint64_t m_last_drawn_revision{ UINT64_MAX };   //上次重绘时的数据修订号（R1 脏标记）
    ULONGLONG m_last_paint_time{};  //上次重绘时间（R5 节流）
    int m_insert_to_taskbar_cnt{};  //用来统计尝试嵌入任务栏的次数
    int m_cannot_insert_to_task_bar_warning{ true };   //指示是否会在无法嵌入任务栏时弹出提示框

    static unsigned int m_WM_TASKBARCREATED;    //任务栏重启消息

    SYSTEMTIME m_start_time;    //程序启动时的时间

    bool m_is_foreground_fullscreen{ false };   //指示前台窗口是否正在全局显示
    bool m_menu_popuped{ false };               //指示当前是否有菜单处于弹出状态

    HDC m_desktop_dc;

    // 统一采样引擎
    MonitorService m_monitor_service;
#ifndef WITHOUT_TEMPERATURE
    CHardwareDataProvider m_hardware_provider;
#endif

    // 从全局设置提取采样配置
    MonitorService::Config GetMonitorConfig() const;

    void DoMonitorAcquisition();    //获取一次监控信息并同步到全局成员
    static UINT MonitorThreadCallback(LPVOID dwUser);   //获取监控信息的线程函数
    bool m_monitor_data_required{ false };          //线程中需要获取监控数据标志，当需要获取监控数据时置为true，获取到一次监控数据时置为false
    bool m_is_thread_exit{ false }; //线程退出标志
    CEvent m_threadExitEvent;       //用于通知主线程工作线程已退出
public:
    void ExitMonitorThread();       //停止监控线程

protected:
    void GetScreenSize();           //获取屏幕的大小

    void IniConnectionMenu(CMenu* pMenu);   //初始化“选择网络连接”菜单
    void IniTaskBarConnectionMenu();        //初始化任务栏窗口的“选择网络连接”菜单
    void SetConnectionMenuState(CMenu* pMenu);      //设置“选择网络连接”菜单中选中的项目

    void CloseTaskBarWnd(); //关闭任务栏窗口
    void OpenTaskBarWnd();  //打开任务栏窗口

    void AddNotifyIcon();       //添加通知区图标
    void DeleteNotifyIcon();
public:
    void ShowNotifyTip(const wchar_t* title, const wchar_t* message);       //显示通知区提示
protected:
    void UpdateNotifyIconTip();     //更新通知区图标的鼠标提示

    void SaveHistoryTraffic();        // 增量保存，只更新第一行和今天的记录
    void SaveHistoryTrafficFull();    // 完整保存，用于程序退出时确保所有数据都保存
    void LoadHistoryTraffic();
    void BackupHistoryTrafficFile();

    void _OnOptions(int tab, CWnd* pParent);   //打开“选项”对话框的处理，tab：打开时切换的标签

    void ApplySettings(COptionsDlg& optionsDlg);

public:
    bool IsTaskbarWndValid() const;
protected:

    void TaskbarShowHideItem(DisplayItem type);

public:
    bool IsTemperatureNeeded() const;       //判断是否需要显示温度信息

protected:
    // 生成的消息映射函数
    virtual BOOL OnInitDialog();
    DECLARE_MESSAGE_MAP()
public:
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnNetworkInfo();
    afx_msg void OnClose();
    virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
    virtual BOOL PreTranslateMessage(MSG* pMsg);
protected:
    afx_msg LRESULT OnNotifyIcon(WPARAM wParam, LPARAM lParam);
public:
    afx_msg void OnShowNotifyIcon();
    afx_msg void OnDestroy();
    afx_msg void OnShowTaskBarWnd();
    afx_msg void OnAppAbout();
    afx_msg void OnShowCpuMemory2();
    afx_msg LRESULT OnTaskBarCreated(WPARAM wParam, LPARAM lParam);
    afx_msg void OnTrafficHistory();
    afx_msg void OnOptions2();
protected:
    afx_msg LRESULT OnExitmenuloop(WPARAM wParam, LPARAM lParam);
public:
    afx_msg void OnCheckUpdate();
    afx_msg void OnChangeNotifyIcon();
protected:
    afx_msg LRESULT OnTaskbarMenuPopedUp(WPARAM wParam, LPARAM lParam);
public:
    afx_msg void OnShowNetSpeed();
    afx_msg BOOL OnQueryEndSession();
protected:
    afx_msg LRESULT OnDpichanged(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnTaskbarWndClosed(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnMonitorInfoUpdated(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnDisplaychange(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnReopenTaksbarWnd(WPARAM wParam, LPARAM lParam);
public:
    afx_msg void OnOpenTaskManager();
protected:
    afx_msg LRESULT OnSettingsApplied(WPARAM wParam, LPARAM lParam);
public:
    afx_msg void OnDisplaySettings();
    afx_msg void OnRefreshConnectionList();
protected:
    afx_msg LRESULT OnTabletQuerysystemgesturestatus(WPARAM wParam, LPARAM lParam);
public:
    afx_msg UINT OnPowerBroadcast(UINT nPowerEvent, LPARAM nEventData);
    afx_msg void OnColorizationColorChanged(DWORD dwColorizationColor, BOOL bOpacity);
};
