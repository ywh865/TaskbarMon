// MonitorService.h : unified sampling engine (core layer)
#pragma once

#include "MonitorTypes.h"
#include "AdapterCommon.h"
#include "HistoryTrafficFile.h"
#include "PdhHardwareQuery/CPUUsage.h"
#include "PdhHardwareQuery/CpuFreq.h"
#include "PdhHardwareQuery/GpuUsage.h"
#include "PdhHardwareQuery/DiskUsage.h"

#include <deque>
#include <functional>
#include <mutex>
#include <set>
#include <vector>

class MonitorService
{
public:
    struct Config
    {
        int monitor_time_span{ 1000 };
        bool select_all{};
        bool auto_select{};
        bool show_all_interface{};
        std::string connection_name;
        std::set<std::wstring> connections_hide;
        bool cpu_usage_by_time{};
        unsigned int hardware_monitor_item{};
        std::wstring hard_disk_name;
        std::wstring cpu_core_name;
        std::wstring history_traffic_path;
        std::wstring log_path;
        std::wstring config_dir;
        bool debug_log{};
    };

    // An atomic-at-the-API-boundary copy of the data required by network UI.
    // interface_rows and connections come from the same table generation.
    struct NetworkStateSnapshot
    {
        std::vector<NetWorkConection> connections;
        std::vector<MIB_IFROW> interface_rows;
        int selected_index{ -1 };
        DWORD sampled_interface_index{};
    };

    explicit MonitorService(const Config& config);
    ~MonitorService();

    void ApplyConfig(const Config& config);

    // ---- Connection management ----
    void InitConnections();
    void AutoSelect();
    bool ConsumeDelayedAutoSelectPending();

    // Legacy UI accessors return a per-thread copy, not mutable service state.
    // New callers should use the explicit snapshot APIs below.
    std::vector<NetWorkConection>& Connections();
    MIB_IFTABLE* IfTable();
    std::vector<NetWorkConection> ConnectionsSnapshot() const;
    NetworkStateSnapshot GetNetworkStateSnapshot() const;

    int SelectedIndex() const;
    std::string SelectedConnectionName() const;
    void SelectConnection(int index);
    void SetSelectAll(bool select_all);
    bool IsConnectionChanged() const;
    void ClearConnectionChanged();
    int RestartCount() const;

    // ---- Hardware provider ----
    void SetHardwareProvider(IHardwareDataProvider* provider);
    bool GetDiskUsageByPdh() const;
    const std::vector<CString>& GetDiskNames() const;

    // ---- History traffic ----
    // Legacy direct reference. It is only safe before sampling begins or while
    // the caller externally serializes access. Prefer the snapshot/totals APIs.
    CHistoryTrafficFile& HistoryFile() { return m_history_traffic; }
    void InitializeTodayTrafficFromHistory();
    void InitializeTodayTraffic(uint64_t up_bytes, uint64_t down_bytes);
    // Persist under the same lock that guards sampling and history mutation.
    // `today_only` currently uses the history file's atomic full replacement
    // implementation, so both paths are crash-safe.
    bool SaveHistoryTraffic(bool today_only) const;
    uint64_t TodayUpTraffic() const;
    uint64_t TodayDownTraffic() const;
    std::deque<HistoryTraffic> HistoryTrafficSnapshot() const;
    std::function<void()> on_history_save;

    // Runs a complete sample. Calls are serialized; UI code can safely use the
    // snapshot APIs concurrently with this method.
    void Sample();

    // ---- Metric snapshots ----
    MonitorSnapshot Snapshot() const;
    uint64_t Revision() const;

private:
    bool RefreshIfTableLocked();
    void InitConnectionsLocked();
    void AutoSelectLocked();
    void ReindexConnectionsLocked();
    void ResetNetworkBaselineLocked();

    int FindConnectionByInterfaceIndexLocked(DWORD interface_index) const;
    int FindSingleGatewayFallbackLocked() const;
    bool IsOperationalInterfaceLocked(DWORD interface_index) const;
    bool ReadInterfaceCountersLocked(DWORD interface_index, uint64_t& in_bytes, uint64_t& out_bytes) const;
    std::string InterfaceDescriptionLocked(DWORD interface_index) const;
    std::string SelectedConnectionNameLocked() const;

    void AcquireCpuMemoryLocked();
    void AcquireHardwareDataLocked();
    bool UpdateHistoryTrafficLocked(uint64_t in_delta, uint64_t out_delta);
    void InitializeTodayTrafficLocked(uint64_t up_bytes, uint64_t down_bytes);
    void CheckConnectionsChangedLocked();

    Config m_config;
    IHardwareDataProvider* m_hardware_provider{};

    mutable std::mutex m_mutex;
    MonitorSnapshot m_snapshot;
    uint64_t m_revision{};

    std::vector<NetWorkConection> m_connections;
    std::vector<BYTE> m_if_table_storage;
    MIB_IFTABLE* m_pIfTable{};
    int m_connection_selected{ -1 };
    DWORD m_sampled_interface_index{};
    uint64_t m_in_bytes{};
    uint64_t m_out_bytes{};
    uint64_t m_last_in_bytes{};
    uint64_t m_last_out_bytes{};
    DWORD m_baseline_interface_index{};
    bool m_network_baseline_valid{};
    ULONGLONG m_last_net_sample_tick{};
    std::string m_connection_name_preferd;
    int m_zero_speed_cnt{};
    int m_restart_cnt{ -1 };
    bool m_connection_change_flag{};
    bool m_delayed_auto_select_pending{};
    DWORD m_last_interface_count{};
    bool m_has_last_interface_count{};

    CHistoryTrafficFile m_history_traffic;
    uint64_t m_today_up_traffic{};
    uint64_t m_today_down_traffic{};
    unsigned int m_monitor_time_cnt{};
    int m_last_history_day{ -1 };
    uint64_t m_last_saved_today_kbytes{};
    bool m_last_saved_today_kbytes_initialized{};

    CCPUUsage m_cpu_usage_helper;
    CPdhCpuFreq m_cpu_freq_helper;
    CPdhGPUUsage m_gpu_usage_helper;
    CPdhDiskUsage m_disk_usage_helper;
    bool m_get_disk_usage_by_pdh{};
    bool m_gpu_usage_acquired{};
    bool m_cpu_freq_acquired{};
};
