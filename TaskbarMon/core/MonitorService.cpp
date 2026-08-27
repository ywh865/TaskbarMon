#include "stdafx.h"
#include "MonitorService.h"
#include "Common.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <netioapi.h>
#include <new>

#pragma comment(lib, "iphlpapi.lib")

namespace
{
    constexpr size_t kMaximumIfTableBytes = 16u * 1024u * 1024u;

    int EffectiveMonitorTimeSpan(int monitor_time_span)
    {
        return monitor_time_span > 0 ? monitor_time_span : 1000;
    }

    int GetSampleCount(int second, int monitor_time_span)
    {
        const int span = EffectiveMonitorTimeSpan(monitor_time_span);
        const int count = second * 1000 / span;
        return count > 0 ? count : 1;
    }

    uint64_t SaturatingAdd(uint64_t left, uint64_t right)
    {
        const uint64_t maximum = (std::numeric_limits<uint64_t>::max)();
        return left > maximum - right ? maximum : left + right;
    }

    uint64_t BytesPerSecond(uint64_t byte_delta, ULONGLONG elapsed_milliseconds)
    {
        if (elapsed_milliseconds == 0)
            return 0;

        constexpr uint64_t milliseconds_per_second = 1000;
        const uint64_t quotient = byte_delta / elapsed_milliseconds;
        const uint64_t remainder = byte_delta % elapsed_milliseconds;
        const uint64_t maximum = (std::numeric_limits<uint64_t>::max)();
        if (quotient > maximum / milliseconds_per_second)
            return maximum;

        uint64_t result = quotient * milliseconds_per_second;
        // This branch is only relevant after an implausibly long uptime. It is
        // preferable to lose a sub-byte/s fraction to overflowing a rate.
        if (remainder <= maximum / milliseconds_per_second)
            result = SaturatingAdd(result, remainder * milliseconds_per_second / elapsed_milliseconds);
        return result;
    }

    size_t TableEntryCount(const std::vector<BYTE>& storage, const MIB_IFTABLE* table)
    {
        if (table == nullptr || storage.size() < offsetof(MIB_IFTABLE, table))
            return 0;

        const size_t available_entries =
            (storage.size() - offsetof(MIB_IFTABLE, table)) / sizeof(MIB_IFROW);
        return std::min<size_t>(table->dwNumEntries, available_entries);
    }

    bool HasDefaultGateway(const NetWorkConection& connection)
    {
        return !connection.default_gateway.empty() &&
            connection.default_gateway != L"-.-.-.-" &&
            connection.default_gateway != L"0.0.0.0";
    }

    bool IsAverageCpuTemperatureSelection(const std::wstring& value)
    {
        // The settings UI stores the localized combo-box text. These legacy
        // English values were written by older upstream configurations.
        return value.empty() ||
            value == CCommon::LoadText(IDS_AVREAGE_TEMPERATURE).GetString() ||
            value == L"Core Average" ||
            value == L"Average Temperature";
    }

    struct LegacyNetworkView
    {
        std::vector<NetWorkConection> connections;
        std::vector<BYTE> table;
        bool pending_connections{};
        bool pending_table{};
    };

    LegacyNetworkView& CurrentLegacyNetworkView()
    {
        thread_local LegacyNetworkView view;
        return view;
    }
}

MonitorService::MonitorService(const Config& config)
    : m_config(config)
    , m_history_traffic(config.history_traffic_path)
    , m_connection_name_preferd(config.connection_name)
{
}

MonitorService::~MonitorService() = default;

void MonitorService::ApplyConfig(const Config& config)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const bool selection_mode_changed =
        m_config.select_all != config.select_all ||
        m_config.auto_select != config.auto_select ||
        m_config.connection_name != config.connection_name ||
        m_config.show_all_interface != config.show_all_interface ||
        m_config.connections_hide != config.connections_hide;

    m_config = config;
    m_connection_name_preferd = config.connection_name;
    if (selection_mode_changed)
    {
        ResetNetworkBaselineLocked();
        m_connection_change_flag = true;
        if (m_config.auto_select || m_config.select_all)
            AutoSelectLocked();
    }
}

void MonitorService::InitConnections()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    InitConnectionsLocked();
}

void MonitorService::InitConnectionsLocked()
{
    const bool refreshed = RefreshIfTableLocked();
    if (!refreshed && m_pIfTable == nullptr)
    {
        m_connections.clear();
        m_connection_selected = -1;
        m_sampled_interface_index = 0;
        ResetNetworkBaselineLocked();
        m_connection_change_flag = true;
        ++m_restart_cnt;
        return;
    }

    if (!m_config.show_all_interface)
    {
        m_connections.clear();
        vector<NetWorkConection> adapters;
        CAdapterCommon::GetAdapterInfo(adapters);
        for (const auto& item : adapters)
        {
            if (!m_config.connections_hide.count(CCommon::StrToUnicode(item.description.c_str())))
                m_connections.push_back(item);
        }
        CAdapterCommon::GetIfTableInfo(m_connections, m_pIfTable);
    }
    else
    {
        CAdapterCommon::GetAllIfTableInfo(m_connections, m_pIfTable);
    }
    ReindexConnectionsLocked();

    if (m_config.debug_log)
    {
        CString log_str;
        log_str += _T("Initializing network connections...\n");
        log_str += _T("Connection list:\n");
        for (const auto& connection : m_connections)
        {
            log_str += connection.description.c_str();
            log_str += _T(", ");
            log_str += CCommon::IntToString(connection.interface_index);
            log_str += _T("\n");
        }
        log_str += _T("IfTable:\n");
        const size_t entry_count = TableEntryCount(m_if_table_storage, m_pIfTable);
        for (size_t i = 0; i < entry_count; ++i)
        {
            log_str += CCommon::IntToString(static_cast<__int64>(i));
            log_str += _T(" ");
            log_str += CAdapterCommon::GetIfTableDescription(m_pIfTable->table[i]).c_str();
            log_str += _T("\n");
        }
        CCommon::WriteLog(log_str, (m_config.config_dir + L".\\connections.log").c_str());
    }

    const bool route_mode = m_config.auto_select || m_config.select_all;
    m_delayed_auto_select_pending = route_mode && m_restart_cnt != -1;
    if (route_mode)
    {
        // Resolve once immediately for a safe zero/route baseline, then let the
        // legacy delayed UI timer re-resolve after adapters finish starting.
        AutoSelectLocked();
    }
    else
    {
        m_connection_selected = -1;
        for (size_t i = 0; i < m_connections.size(); ++i)
        {
            const auto& connection = m_connections[i];
            if (connection.description_2 == m_connection_name_preferd ||
                connection.description == m_connection_name_preferd)
            {
                m_connection_selected = static_cast<int>(i);
                break;
            }
        }

        if (m_connection_selected >= 0)
            m_sampled_interface_index = m_connections[m_connection_selected].interface_index;
        else
            m_sampled_interface_index = 0;
    }

    ResetNetworkBaselineLocked();
    ++m_restart_cnt;
    m_connection_change_flag = true;
}

bool MonitorService::ConsumeDelayedAutoSelectPending()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const bool pending = m_delayed_auto_select_pending;
    m_delayed_auto_select_pending = false;
    return pending;
}

std::vector<NetWorkConection>& MonitorService::Connections()
{
    auto& view = CurrentLegacyNetworkView();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (view.pending_connections)
    {
        view.pending_connections = false;
        return view.connections;
    }

    view.connections = m_connections;
    view.table = m_if_table_storage;
    view.pending_table = true;
    return view.connections;
}

MIB_IFTABLE* MonitorService::IfTable()
{
    auto& view = CurrentLegacyNetworkView();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (view.pending_table)
    {
        view.pending_table = false;
    }
    else
    {
        view.connections = m_connections;
        view.table = m_if_table_storage;
        view.pending_connections = true;
    }
    // Existing UI dereferences IfTable()->table before it can inspect the
    // connection list. Hand it a valid, empty table rather than nullptr when
    // Windows has no table available yet.
    if (view.table.size() < sizeof(MIB_IFTABLE))
        view.table.assign(sizeof(MIB_IFTABLE), 0);
    return reinterpret_cast<MIB_IFTABLE*>(view.table.data());
}

std::vector<NetWorkConection> MonitorService::ConnectionsSnapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_connections;
}

MonitorService::NetworkStateSnapshot MonitorService::GetNetworkStateSnapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    NetworkStateSnapshot snapshot;
    snapshot.connections = m_connections;
    snapshot.selected_index = m_connection_selected;
    snapshot.sampled_interface_index = m_sampled_interface_index;
    const size_t entry_count = TableEntryCount(m_if_table_storage, m_pIfTable);
    bool counter_cache_matches_table = m_interface_snapshots.size() == entry_count;
    if (counter_cache_matches_table)
    {
        for (size_t i = 0; i < entry_count; ++i)
        {
            if (m_interface_snapshots[i].row.dwIndex != m_pIfTable->table[i].dwIndex)
            {
                counter_cache_matches_table = false;
                break;
            }
        }
    }

    if (counter_cache_matches_table)
    {
        snapshot.interface_rows = m_interface_snapshots;
    }
    else
    {
        // Before the worker has published its first complete counter cache,
        // return the validated table rows without performing network I/O on
        // the UI thread.
        snapshot.interface_rows.reserve(entry_count);
        for (size_t i = 0; i < entry_count; ++i)
        {
            const MIB_IFROW& row = m_pIfTable->table[i];
            InterfaceSnapshot interface_snapshot;
            interface_snapshot.row = row;
            interface_snapshot.in_bytes = row.dwInOctets;
            interface_snapshot.out_bytes = row.dwOutOctets;
            snapshot.interface_rows.push_back(std::move(interface_snapshot));
        }
    }
    return snapshot;
}

int MonitorService::SelectedIndex() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_connection_selected;
}

std::string MonitorService::SelectedConnectionName() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return SelectedConnectionNameLocked();
}

std::string MonitorService::SelectedConnectionNameLocked() const
{
    if (m_connection_selected >= 0 && m_connection_selected < static_cast<int>(m_connections.size()))
    {
        const auto& connection = m_connections[m_connection_selected];
        return !connection.description_2.empty() ? connection.description_2 : connection.description;
    }
    return InterfaceDescriptionLocked(m_sampled_interface_index);
}

void MonitorService::SelectConnection(int index)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index < 0 || index >= static_cast<int>(m_connections.size()))
        return;

    const DWORD interface_index = m_connections[index].interface_index;
    if (interface_index == 0)
        return;

    m_connection_selected = index;
    m_sampled_interface_index = interface_index;
    m_connection_name_preferd = SelectedConnectionNameLocked();
    m_config.connection_name = m_connection_name_preferd;
    m_config.auto_select = false;
    m_config.select_all = false;
    ResetNetworkBaselineLocked();
    m_connection_change_flag = true;
}

void MonitorService::SetSelectAll(bool select_all)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.select_all = select_all;
    if (select_all)
    {
        // "All" used to sum every interface, double-counting traffic when a
        // physical adapter and VPN/tunnel carry the same packets. Preserve the
        // menu state but resolve one Windows default-route interface instead.
        m_config.auto_select = false;
        AutoSelectLocked();
        ResetNetworkBaselineLocked();
        m_connection_change_flag = true;
    }
    else
    {
        ResetNetworkBaselineLocked();
        m_connection_change_flag = true;
    }
}

bool MonitorService::IsConnectionChanged() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_connection_change_flag;
}

void MonitorService::ClearConnectionChanged()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_connection_change_flag = false;
}

int MonitorService::RestartCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_restart_cnt;
}

void MonitorService::SetHardwareProvider(IHardwareDataProvider* provider)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hardware_provider = provider;
}

bool MonitorService::GetDiskUsageByPdh() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_get_disk_usage_by_pdh;
}

const std::vector<CString>& MonitorService::GetDiskNames() const
{
    thread_local std::vector<CString> snapshot;
    std::lock_guard<std::mutex> lock(m_mutex);
    snapshot = m_disk_usage_helper.GetDiskNames();
    return snapshot;
}

void MonitorService::InitializeTodayTrafficFromHistory()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const __int64 up_bytes = m_history_traffic.GetTodayUpTraffic();
    const __int64 down_bytes = m_history_traffic.GetTodayDownTraffic();
    InitializeTodayTrafficLocked(
        up_bytes > 0 ? static_cast<uint64_t>(up_bytes) : 0,
        down_bytes > 0 ? static_cast<uint64_t>(down_bytes) : 0);
}

void MonitorService::InitializeTodayTraffic(uint64_t up_bytes, uint64_t down_bytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    InitializeTodayTrafficLocked(up_bytes, down_bytes);
}

bool MonitorService::SaveHistoryTraffic(bool today_only) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (today_only)
        return m_history_traffic.SaveTodayOnly();
    return m_history_traffic.Save();
}

void MonitorService::InitializeTodayTrafficLocked(uint64_t up_bytes, uint64_t down_bytes)
{
    m_today_up_traffic = up_bytes;
    m_today_down_traffic = down_bytes;
    auto& today = m_history_traffic.GetTodayTraffic();
    today.up_kBytes = up_bytes / 1024u;
    today.down_kBytes = down_bytes / 1024u;
    today.mixed = false;
    m_snapshot.today_up_traffic = up_bytes;
    m_snapshot.today_down_traffic = down_bytes;
    m_last_saved_today_kbytes = today.kBytes();
    m_last_saved_today_kbytes_initialized = true;
}

uint64_t MonitorService::TodayUpTraffic() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_today_up_traffic;
}

uint64_t MonitorService::TodayDownTraffic() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_today_down_traffic;
}

std::deque<HistoryTraffic> MonitorService::HistoryTrafficSnapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_history_traffic.GetTraffics();
}

MonitorSnapshot MonitorService::Snapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_snapshot;
}

uint64_t MonitorService::Revision() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_revision;
}

bool MonitorService::RefreshIfTableLocked()
{
    try
    {
        std::vector<BYTE> candidate = m_if_table_storage;
        if (candidate.size() > kMaximumIfTableBytes)
            candidate.clear();
        if (candidate.size() < sizeof(MIB_IFTABLE))
            candidate.resize(sizeof(MIB_IFTABLE));

        for (int attempt = 0; attempt < 3; ++attempt)
        {
            DWORD size = static_cast<DWORD>(candidate.size());
            const DWORD result = GetIfTable(reinterpret_cast<MIB_IFTABLE*>(candidate.data()), &size, FALSE);
            if (result == NO_ERROR)
            {
                constexpr size_t table_header_size = offsetof(MIB_IFTABLE, table);
                const size_t available_entries =
                    (candidate.size() - table_header_size) / sizeof(MIB_IFROW);
                const MIB_IFTABLE* table = reinterpret_cast<const MIB_IFTABLE*>(candidate.data());
                if (table->dwNumEntries > available_entries)
                    return false;

                m_if_table_storage.swap(candidate);
                m_pIfTable = reinterpret_cast<MIB_IFTABLE*>(m_if_table_storage.data());
                ReindexConnectionsLocked();
                return true;
            }

            if (result != ERROR_INSUFFICIENT_BUFFER ||
                size < offsetof(MIB_IFTABLE, table) ||
                size > kMaximumIfTableBytes)
            {
                return false;
            }
            candidate.resize((std::max)(static_cast<size_t>(size), sizeof(MIB_IFTABLE)));
        }
    }
    catch (const std::bad_alloc&)
    {
        // Preserve the last validated table if a refresh cannot allocate.
        return false;
    }

    return false;
}

void MonitorService::RefreshInterfaceSnapshotsLocked()
{
    m_interface_snapshots.clear();
    if (m_pIfTable == nullptr)
        return;

    const size_t entry_count = TableEntryCount(m_if_table_storage, m_pIfTable);
    m_interface_snapshots.reserve(entry_count);
    for (size_t i = 0; i < entry_count; ++i)
    {
        const MIB_IFROW& row = m_pIfTable->table[i];
        InterfaceSnapshot snapshot;
        snapshot.row = row;
        snapshot.in_bytes = row.dwInOctets;
        snapshot.out_bytes = row.dwOutOctets;

        MIB_IF_ROW2 extended_row{};
        extended_row.InterfaceIndex = row.dwIndex;
        if (GetIfEntry2(&extended_row) == NO_ERROR)
        {
            snapshot.in_bytes = extended_row.InOctets;
            snapshot.out_bytes = extended_row.OutOctets;
        }
        m_interface_snapshots.push_back(std::move(snapshot));
    }
}

void MonitorService::ReindexConnectionsLocked()
{
    if (m_pIfTable == nullptr)
        return;

    for (auto& connection : m_connections)
    {
        int table_index = -1;
        if (connection.interface_index != 0)
            table_index = CAdapterCommon::FindIfTableRowByInterfaceIndex(connection.interface_index, m_pIfTable);
        connection.index = table_index;
        if (table_index >= 0)
        {
            const MIB_IFROW& row = m_pIfTable->table[table_index];
            connection.interface_index = row.dwIndex;
            connection.description_2 = CAdapterCommon::GetIfTableDescription(row);
        }
    }
}

void MonitorService::ResetNetworkBaselineLocked()
{
    m_in_bytes = 0;
    m_out_bytes = 0;
    m_last_in_bytes = 0;
    m_last_out_bytes = 0;
    m_baseline_interface_index = 0;
    m_network_baseline_valid = false;
    m_last_net_sample_tick = 0;
    m_zero_speed_cnt = 0;
}

int MonitorService::FindConnectionByInterfaceIndexLocked(DWORD interface_index) const
{
    if (interface_index == 0)
        return -1;
    for (size_t i = 0; i < m_connections.size(); ++i)
    {
        if (m_connections[i].interface_index == interface_index)
            return static_cast<int>(i);
    }
    return -1;
}

int MonitorService::FindSingleGatewayFallbackLocked() const
{
    int selected = -1;
    for (size_t i = 0; i < m_connections.size(); ++i)
    {
        const auto& connection = m_connections[i];
        if (connection.interface_index == 0 || !HasDefaultGateway(connection) ||
            !IsOperationalInterfaceLocked(connection.interface_index))
        {
            continue;
        }

        if (selected != -1)
            return -1; // Multiple plausible routes: do not guess.
        selected = static_cast<int>(i);
    }
    return selected;
}

bool MonitorService::IsOperationalInterfaceLocked(DWORD interface_index) const
{
    const int table_index = CAdapterCommon::FindIfTableRowByInterfaceIndex(interface_index, m_pIfTable);
    if (table_index < 0)
        return false;
    const DWORD status = m_pIfTable->table[table_index].dwOperStatus;
    return status == IF_OPER_STATUS_OPERATIONAL || status == IF_OPER_STATUS_CONNECTED;
}

void MonitorService::AutoSelect()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // Public callers use this operation to enter automatic selection (the
    // delayed timer calls it too). Keep the service configuration coherent
    // even before the UI persists its mirrored settings.
    m_config.auto_select = true;
    m_config.select_all = false;
    AutoSelectLocked();
}

void MonitorService::AutoSelectLocked()
{
    DWORD new_interface_index = CAdapterCommon::GetDefaultRouteInterfaceIndex();
    int new_selected = -1;

    if (new_interface_index != 0 && IsOperationalInterfaceLocked(new_interface_index))
    {
        new_selected = FindConnectionByInterfaceIndexLocked(new_interface_index);
    }
    else
    {
        // No route was resolvable. Only accept a single operational adapter
        // that explicitly advertises a gateway; otherwise report zero rather
        // than attributing traffic to an arbitrary interface.
        new_interface_index = 0;
        new_selected = FindSingleGatewayFallbackLocked();
        if (new_selected >= 0)
            new_interface_index = m_connections[new_selected].interface_index;
    }

    const bool changed = m_connection_selected != new_selected ||
        m_sampled_interface_index != new_interface_index;
    m_connection_selected = new_selected;
    m_sampled_interface_index = new_interface_index;
    m_config.connection_name = SelectedConnectionNameLocked();
    if (changed)
    {
        ResetNetworkBaselineLocked();
        m_connection_change_flag = true;
    }
}

bool MonitorService::ReadInterfaceCountersLocked(
    DWORD interface_index,
    uint64_t& in_bytes,
    uint64_t& out_bytes) const
{
    in_bytes = 0;
    out_bytes = 0;
    if (interface_index == 0)
        return false;

    MIB_IF_ROW2 row{};
    row.InterfaceIndex = interface_index;
    if (GetIfEntry2(&row) == NO_ERROR)
    {
        in_bytes = row.InOctets;
        out_bytes = row.OutOctets;
        return true;
    }

    // GetIfEntry2 provides 64-bit octets. The legacy table is an explicit
    // fallback only; a 32-bit wrap is treated as a new baseline below.
    const int table_index = CAdapterCommon::FindIfTableRowByInterfaceIndex(interface_index, m_pIfTable);
    if (table_index < 0)
        return false;
    in_bytes = m_pIfTable->table[table_index].dwInOctets;
    out_bytes = m_pIfTable->table[table_index].dwOutOctets;
    return true;
}

std::string MonitorService::InterfaceDescriptionLocked(DWORD interface_index) const
{
    const int table_index = CAdapterCommon::FindIfTableRowByInterfaceIndex(interface_index, m_pIfTable);
    if (table_index >= 0)
        return CAdapterCommon::GetIfTableDescription(m_pIfTable->table[table_index]);
    return std::string();
}

void MonitorService::Sample()
{
    std::function<void()> save_callback;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        RefreshIfTableLocked(); // A failed refresh leaves the last valid table intact.
        RefreshInterfaceSnapshotsLocked();

        // Automatic and former "all interfaces" modes follow the Windows
        // route table. They never aggregate tunnel, virtual and physical NICs.
        if (m_config.auto_select || m_config.select_all)
            AutoSelectLocked();

        uint64_t in_bytes{};
        uint64_t out_bytes{};
        const bool counters_available = ReadInterfaceCountersLocked(
            m_sampled_interface_index, in_bytes, out_bytes);
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG elapsed = m_last_net_sample_tick == 0 ? 0 : now - m_last_net_sample_tick;
        m_last_net_sample_tick = now;

        uint64_t in_delta{};
        uint64_t out_delta{};
        if (!counters_available)
        {
            m_snapshot.in_speed = 0;
            m_snapshot.out_speed = 0;
            m_network_baseline_valid = false;
            m_baseline_interface_index = 0;
        }
        else
        {
            m_in_bytes = in_bytes;
            m_out_bytes = out_bytes;
            const bool establish_new_baseline = !m_network_baseline_valid ||
                m_connection_change_flag ||
                m_baseline_interface_index != m_sampled_interface_index ||
                m_in_bytes < m_last_in_bytes ||
                m_out_bytes < m_last_out_bytes ||
                elapsed == 0;

            if (establish_new_baseline)
            {
                m_snapshot.in_speed = 0;
                m_snapshot.out_speed = 0;
            }
            else
            {
                in_delta = m_in_bytes - m_last_in_bytes;
                out_delta = m_out_bytes - m_last_out_bytes;
                m_snapshot.in_speed = BytesPerSecond(in_delta, elapsed);
                m_snapshot.out_speed = BytesPerSecond(out_delta, elapsed);
            }

            m_last_in_bytes = m_in_bytes;
            m_last_out_bytes = m_out_bytes;
            m_baseline_interface_index = m_sampled_interface_index;
            m_network_baseline_valid = true;
        }

        m_connection_change_flag = false;

        if (m_config.auto_select)
        {
            if (in_delta == 0 && out_delta == 0)
                ++m_zero_speed_cnt;
            else
                m_zero_speed_cnt = 0;
            if (m_zero_speed_cnt >= GetSampleCount(30, m_config.monitor_time_span))
            {
                AutoSelectLocked();
                m_zero_speed_cnt = 0;
            }
        }

        const bool should_save_history = UpdateHistoryTrafficLocked(in_delta, out_delta);
        CheckConnectionsChangedLocked();
        AcquireCpuMemoryLocked();
        AcquireHardwareDataLocked();

        ++m_monitor_time_cnt;
        ++m_revision;
        if (should_save_history)
            save_callback = on_history_save;
    }

    // Do not invoke UI/file callbacks while holding the service mutex.
    if (save_callback)
        save_callback();
}

void MonitorService::CheckConnectionsChangedLocked()
{
    const int sample_count = GetSampleCount(3, m_config.monitor_time_span);
    if (m_monitor_time_cnt % sample_count != sample_count - 1)
        return;

    DWORD interface_count{};
    if (GetNumberOfInterfaces(&interface_count) == NO_ERROR)
    {
        if (m_has_last_interface_count && interface_count != m_last_interface_count)
        {
            if (m_config.debug_log)
                CCommon::WriteLog(CCommon::LoadText(IDS_CONNECTION_NUM_CHANGED), m_config.log_path.c_str());
            InitConnectionsLocked();
            m_last_interface_count = interface_count;
            return;
        }
        m_last_interface_count = interface_count;
        m_has_last_interface_count = true;
    }

    if (!m_config.auto_select && !m_config.select_all &&
        !m_config.connection_name.empty() &&
        SelectedConnectionNameLocked() != m_config.connection_name)
    {
        if (m_config.debug_log)
            CCommon::WriteLog(CCommon::LoadText(IDS_CONNECTION_NOT_MATCH), m_config.log_path.c_str());
        InitConnectionsLocked();
    }
}

void MonitorService::AcquireCpuMemoryLocked()
{
    bool lite_version = false;
#ifdef WITHOUT_TEMPERATURE
    lite_version = true;
#endif

    m_snapshot.cpu_usage = m_cpu_usage_helper.GetCpuUsage(m_config.cpu_usage_by_time);

    m_cpu_freq_acquired = m_cpu_freq_helper.GetCpuFreq(m_snapshot.cpu_freq);
    if (!m_cpu_freq_acquired)
        m_snapshot.cpu_freq = -1;

    m_gpu_usage_acquired = false;
    if (lite_version || !(m_config.hardware_monitor_item & HI_GPU))
    {
        if (m_gpu_usage_helper.GetGpuUsage(m_snapshot.gpu_usage))
            m_gpu_usage_acquired = true;
        else
            m_snapshot.gpu_usage = -1;
    }
    else
    {
        m_snapshot.gpu_usage = -1;
    }

    m_get_disk_usage_by_pdh = false;
    if (lite_version || !(m_config.hardware_monitor_item & HI_HDD))
    {
        int disk_index = m_disk_usage_helper.FindDiskIndex(m_config.hard_disk_name);
        if (disk_index < 0)
        {
            disk_index = m_disk_usage_helper.FindDiskIndex(L"_Total");
            if (disk_index >= 0)
            {
                m_config.hard_disk_name = L"_Total";
            }
            else
            {
                const auto& disk_names = m_disk_usage_helper.GetDiskNames();
                if (!disk_names.empty())
                {
                    disk_index = 0;
                    m_config.hard_disk_name = disk_names.front();
                }
            }
        }
        if (m_disk_usage_helper.GetDiskUsage(disk_index, m_snapshot.hdd_usage))
            m_get_disk_usage_by_pdh = true;
        else
            m_snapshot.hdd_usage = -1;
    }
    else
    {
        m_snapshot.hdd_usage = -1;
    }

    MEMORYSTATUSEX statex{};
    statex.dwLength = sizeof(statex);
    if (GlobalMemoryStatusEx(&statex))
    {
        m_snapshot.memory_usage = statex.dwMemoryLoad;
        m_snapshot.used_memory = static_cast<int>((statex.ullTotalPhys - statex.ullAvailPhys) / 1024);
        m_snapshot.total_memory = static_cast<int>(statex.ullTotalPhys / 1024);
    }
    else
    {
        m_snapshot.memory_usage = -1;
        m_snapshot.used_memory = 0;
        m_snapshot.total_memory = 0;
    }
}

void MonitorService::AcquireHardwareDataLocked()
{
#ifndef WITHOUT_TEMPERATURE
    if (m_config.hardware_monitor_item == 0 ||
        m_hardware_provider == nullptr || !m_hardware_provider->IsAvailable())
    {
        m_snapshot.cpu_temperature = -1;
        m_snapshot.gpu_temperature = -1;
        m_snapshot.hdd_temperature = -1;
        m_snapshot.main_board_temperature = -1;
        return;
    }

    m_hardware_provider->Acquire();

    m_snapshot.gpu_temperature = m_hardware_provider->GpuTemperature();
    m_snapshot.main_board_temperature = m_hardware_provider->MainboardTemperature();
    if (!m_gpu_usage_acquired)
        m_snapshot.gpu_usage = m_hardware_provider->GpuUsage();
    if (!m_cpu_freq_acquired)
        m_snapshot.cpu_freq = m_hardware_provider->CpuFreq();

    const auto all_cpu_temp = m_hardware_provider->AllCpuTemperature();
    if (!all_cpu_temp.empty())
    {
        if (IsAverageCpuTemperatureSelection(m_config.cpu_core_name))
        {
            m_snapshot.cpu_temperature = m_hardware_provider->CpuTemperature();
        }
        else
        {
            auto iter = all_cpu_temp.find(m_config.cpu_core_name);
            if (iter == all_cpu_temp.end())
            {
                iter = all_cpu_temp.begin();
                m_config.cpu_core_name = iter->first;
            }
            m_snapshot.cpu_temperature = iter->second;
        }
    }
    else
    {
        m_snapshot.cpu_temperature = -1;
    }

    const auto all_hdd_temp = m_hardware_provider->AllHddTemperature();
    if (!all_hdd_temp.empty())
    {
        auto iter = all_hdd_temp.find(m_config.hard_disk_name);
        if (iter == all_hdd_temp.end())
        {
            iter = all_hdd_temp.begin();
            m_config.hard_disk_name = iter->first;
        }
        m_snapshot.hdd_temperature = iter->second;
    }
    else
    {
        m_snapshot.hdd_temperature = -1;
    }

    if (!m_get_disk_usage_by_pdh)
    {
        const auto all_hdd_usage = m_hardware_provider->AllHddUsage();
        if (!all_hdd_usage.empty())
        {
            auto iter = all_hdd_usage.find(m_config.hard_disk_name);
            if (iter == all_hdd_usage.end())
            {
                iter = all_hdd_usage.begin();
                m_config.hard_disk_name = iter->first;
            }
            m_snapshot.hdd_usage = static_cast<int>(iter->second);
        }
        else
        {
            m_snapshot.hdd_usage = -1;
        }
    }
#endif
}

bool MonitorService::UpdateHistoryTrafficLocked(uint64_t in_delta, uint64_t out_delta)
{
    SYSTEMTIME current_time{};
    GetLocalTime(&current_time);
    const auto& today_before_update = m_history_traffic.GetTodayTraffic();
    const bool date_changed = today_before_update.year != current_time.wYear ||
        today_before_update.month != current_time.wMonth ||
        today_before_update.day != current_time.wDay;
    if (date_changed)
    {
        m_history_traffic.OnDateChanged();
        m_today_up_traffic = 0;
        m_today_down_traffic = 0;
        m_last_history_day = -1;
        m_last_saved_today_kbytes = 0;
        m_last_saved_today_kbytes_initialized = false;
    }

    m_today_up_traffic = SaturatingAdd(m_today_up_traffic, out_delta);
    m_today_down_traffic = SaturatingAdd(m_today_down_traffic, in_delta);
    auto& today = m_history_traffic.GetTodayTraffic();
    today.up_kBytes = m_today_up_traffic / 1024u;
    today.down_kBytes = m_today_down_traffic / 1024u;
    today.mixed = false;

    bool should_save = false;
    const int save_sample_count = GetSampleCount(30, m_config.monitor_time_span);
    if (m_monitor_time_cnt % save_sample_count == save_sample_count - 1)
    {
        const uint64_t current_kbytes = today.kBytes();
        if (m_last_history_day != current_time.wDay)
        {
            m_last_history_day = current_time.wDay;
            m_last_saved_today_kbytes = current_kbytes;
            m_last_saved_today_kbytes_initialized = true;
        }
        else if (!m_last_saved_today_kbytes_initialized || current_kbytes < m_last_saved_today_kbytes)
        {
            m_last_saved_today_kbytes = current_kbytes;
            m_last_saved_today_kbytes_initialized = true;
        }
        else if (current_kbytes - m_last_saved_today_kbytes >= 10240u)
        {
            should_save = true;
            m_last_saved_today_kbytes = current_kbytes;
        }
    }

    m_snapshot.today_up_traffic = m_today_up_traffic;
    m_snapshot.today_down_traffic = m_today_down_traffic;
    return should_save;
}
