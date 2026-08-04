// MonitorService.h : 统一采样引擎（core 层）
// 职责：网络连接管理、网速/CPU/内存/硬件采样、历史流量统计
// 不依赖 MFC 消息与 UI，采样结果写入 MonitorSnapshot（带修订号）
#pragma once
#include "MonitorTypes.h"
#include "AdapterCommon.h"
#include "HistoryTrafficFile.h"
#include "PdhHardwareQuery/CPUUsage.h"
#include "PdhHardwareQuery/CpuFreq.h"
#include "PdhHardwareQuery/GpuUsage.h"
#include "PdhHardwareQuery/DiskUsage.h"
#include <set>
#include <functional>

class MonitorService
{
public:
    // 采样配置快照（由 UI 层从全局设置提取后传入）
    struct Config
    {
        int monitor_time_span{ 1000 };      // 采样间隔（毫秒）
        bool select_all{};                  // 统计所有连接
        bool auto_select{};                 // 自动选择连接
        bool show_all_interface{};          // 显示所有网络接口
        std::string connection_name;        // 上次选择的连接名称
        std::set<std::wstring> connections_hide;    // 要从连接列表中隐藏的连接
        bool cpu_usage_by_time{};           // CPU利用率获取方式：true=使用时间，false=性能计数器
        unsigned int hardware_monitor_item{};   // 启用的硬件监控项（HardwareItem 位标志）
        std::wstring hard_disk_name;        // 要监控的硬盘名称
        std::wstring cpu_core_name;         // 要监控的 CPU 核心名称
        std::wstring history_traffic_path;  // 历史流量文件路径
        std::wstring log_path;              // 日志文件路径
        std::wstring config_dir;            // 配置目录（用于 connections.log）
        bool debug_log{};
    };

    explicit MonitorService(const Config& config);
    ~MonitorService();

    // 更新配置（设置变化时调用）
    void ApplyConfig(const Config& config);

    // ---- 连接管理 ----
    void InitConnections();             // 初始化/刷新连接列表
    void AutoSelect();                  // 自动选择流量最大的正常连接
    // 重新初始化连接后，是否需要延迟数秒后再自动选择（原 DELAY_TIMER 语义）。
    // 调用 InitConnections 后检查一次，返回 true 则 UI 层应启动延迟定时器并在到时后调用 AutoSelect()
    bool ConsumeDelayedAutoSelectPending();
    const std::vector<NetWorkConection>& Connections() const { return m_connections; }
    // 当前 MIB 接口表（用于网络详情对话框）
    const MIB_IFTABLE* IfTable() const { return m_pIfTable; }
    int SelectedIndex() const { return m_connection_selected; }
    std::string SelectedConnectionName() const;     // 当前选中连接的名称
    void SelectConnection(int index);               // 手动选择连接
    void SetSelectAll(bool select_all);
    bool IsConnectionChanged() const { return m_connection_change_flag; }
    void ClearConnectionChanged() { m_connection_change_flag = false; }
    int RestartCount() const { return m_restart_cnt; }

    // ---- 硬件数据提供者（UI 层注入） ----
    void SetHardwareProvider(IHardwareDataProvider* provider) { m_hardware_provider = provider; }

    // ---- 历史流量 ----
    CHistoryTrafficFile& HistoryFile() { return m_history_traffic; }
    uint64_t TodayUpTraffic() const { return m_today_up_traffic; }
    uint64_t TodayDownTraffic() const { return m_today_down_traffic; }
    // 历史流量文件保存回调（由 UI 层设置，避免核心层直接写文件）
    std::function<void()> on_history_save;

    // ---- 采样 ----
    // 执行一次完整采样（线程安全：应在同一工作线程中调用）
    void Sample();

    // ---- 快照 ----
    const MonitorSnapshot& Snapshot() const { return m_snapshot; }
    // 修订号：每次采样递增，UI 用于脏检测
    uint64_t Revision() const { return m_revision; }

private:
    MIB_IFROW GetConnectIfTable(int connection_index) const;

    // 采样子步骤
    void AcquireNetSpeed();             // 网速（原 DoMonitorAcquisition 网速部分）
    void AcquireCpuMemory();            // CPU/内存
    void AcquireHardwareData();         // 温度/GPU/HDD（含 PDH 优先逻辑）
    void UpdateHistoryTraffic(uint64_t cur_in_speed, uint64_t cur_out_speed);   // 历史流量统计与保存
    void CheckConnectionsChanged();     // 连接数量/名称变化检测

    // 配置
    Config m_config;
    // 硬件数据提供者
    IHardwareDataProvider* m_hardware_provider{};
    // 快照
    MonitorSnapshot m_snapshot;
    uint64_t m_revision{};

    // 连接数据
    std::vector<NetWorkConection> m_connections;
    MIB_IFTABLE* m_pIfTable{};
    DWORD m_dwSize{ sizeof(MIB_IFTABLE) };
    int m_connection_selected{ 0 };
    unsigned __int64 m_in_bytes{};
    unsigned __int64 m_out_bytes{};
    unsigned __int64 m_last_in_bytes{};
    unsigned __int64 m_last_out_bytes{};
    std::string m_connection_name_preferd;  // 用户手动选择的连接名称
    int m_zero_speed_cnt{};
    int m_restart_cnt{ -1 };
    bool m_connection_change_flag{};
    bool m_delayed_auto_select_pending{};   // 是否需要延迟自动选择

    // 历史流量
    CHistoryTrafficFile m_history_traffic;
    uint64_t m_today_up_traffic{};
    uint64_t m_today_down_traffic{};
    unsigned int m_monitor_time_cnt{};

    // 采样辅助
    CCPUUsage m_cpu_usage_helper;
    CPdhCpuFreq m_cpu_freq_helper;
    CPdhGPUUsage m_gpu_usage_helper;
    CPdhDiskUsage m_disk_usage_helper;
    bool m_get_disk_usage_by_pdh{};
    bool m_gpu_usage_acquired{};
    bool m_cpu_freq_acquired{};
};
