// MonitorService.cpp : 统一采样引擎实现
#include "stdafx.h"
#include "MonitorService.h"
#include "Common.h"

// 计算指定秒数的时间内采样会触发的次数
static int GetSampleCount(int second, int monitor_time_span)
{
    int count = second * 1000 / monitor_time_span;
    if (count <= 0) count = 1;
    return count;
}

MonitorService::MonitorService(const Config& config)
    : m_config(config)
    , m_history_traffic(config.history_traffic_path)
    , m_connection_name_preferd(config.connection_name)
{
    free(m_pIfTable);
    m_dwSize = sizeof(MIB_IFTABLE);
    m_pIfTable = (MIB_IFTABLE*)malloc(m_dwSize);
}

MonitorService::~MonitorService()
{
    free(m_pIfTable);
}

void MonitorService::ApplyConfig(const Config& config)
{
    bool select_all_changed = (m_config.select_all != config.select_all);
    m_config = config;
    if (select_all_changed)
        m_connection_change_flag = true;
}

void MonitorService::InitConnections()
{
    //为m_pIfTable开辟所需大小的内存
    free(m_pIfTable);
    m_dwSize = sizeof(MIB_IFTABLE);
    m_pIfTable = (MIB_IFTABLE*)malloc(m_dwSize);
    int rtn;
    rtn = GetIfTable(m_pIfTable, &m_dwSize, FALSE);
    if (rtn == ERROR_INSUFFICIENT_BUFFER)	//如果函数返回值为ERROR_INSUFFICIENT_BUFFER，说明m_pIfTable的大小不够
    {
        free(m_pIfTable);
        m_pIfTable = (MIB_IFTABLE*)malloc(m_dwSize);	//用新的大小重新开辟一块内存
    }
    GetIfTable(m_pIfTable, &m_dwSize, FALSE);

    //获取当前所有的连接，并保存到m_connections容器中
    if (!m_config.show_all_interface)
    {
        m_connections.clear();
        vector<NetWorkConection> connections;
        CAdapterCommon::GetAdapterInfo(connections);
        for (const auto& item : connections)
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

    //如果在设置了“显示所有网络连接”时设置了“选择全部”，则改为“自动选择”
    if (m_config.show_all_interface && m_config.select_all)
    {
        m_config.select_all = false;
        m_config.auto_select = true;
    }

    //写入调试日志
    if (m_config.debug_log)
    {
        CString log_str;
        log_str += _T("正在初始化网络连接...\n");
        log_str += _T("连接列表：\n");
        for (size_t i{}; i < m_connections.size(); i++)
        {
            log_str += m_connections[i].description.c_str();
            log_str += _T(", ");
            log_str += CCommon::IntToString(m_connections[i].index);
            log_str += _T("\n");
        }
        log_str += _T("IfTable:\n");
        for (size_t i{}; i < m_pIfTable->dwNumEntries; i++)
        {
            log_str += CCommon::IntToString(i);
            log_str += _T(" ");
            log_str += (const char*)m_pIfTable->table[i].bDescr;
            log_str += _T("\n");
        }
        CCommon::WriteLog(log_str, (m_config.config_dir + L".\\connections.log").c_str());
    }

    //选择网络连接
    if (m_config.auto_select)    //自动选择
    {
        //当不是第一次初始化时，需要延时5秒再重新初始化连接（由 UI 层定时器实现）
        m_delayed_auto_select_pending = (m_restart_cnt != -1);
        if (m_restart_cnt == -1)
        {
            AutoSelect();
        }
    }
    else        //查找网络名为上次选择的连接
    {
        m_connection_selected = 0;
        for (size_t i{}; i < m_connections.size(); i++)
        {
            if (m_connections[i].description_2 == m_connection_name_preferd)
                m_connection_selected = i;
        }
    }
    if (m_connection_selected < 0 || m_connection_selected >= static_cast<int>(m_connections.size()))
        m_connection_selected = 0;
    m_config.connection_name = SelectedConnectionName();

    m_restart_cnt++;    //记录初始化次数
    m_connection_change_flag = true;
}

bool MonitorService::ConsumeDelayedAutoSelectPending()
{
    bool pending = m_delayed_auto_select_pending;
    m_delayed_auto_select_pending = false;
    return pending;
}

void MonitorService::AutoSelect()
{
    unsigned __int64 max_in_out_bytes{};
    unsigned __int64 in_out_bytes;
    m_connection_selected = 0;
    //自动选择连接时，查找已发送和已接收字节数之和最多的那个连接，并将其设置为当前查看的连接
    for (size_t i{}; i < m_connections.size(); i++)
    {
        auto table = GetConnectIfTable(i);
        if (table.dwOperStatus == IF_OPER_STATUS_OPERATIONAL)     //只选择网络状态为正常的连接
        {
            in_out_bytes = table.dwInOctets + table.dwOutOctets;
            if (in_out_bytes > max_in_out_bytes)
            {
                max_in_out_bytes = in_out_bytes;
                m_connection_selected = i;
            }
        }
    }
    m_config.connection_name = SelectedConnectionName();
    m_connection_change_flag = true;
}

std::string MonitorService::SelectedConnectionName() const
{
    if (m_connection_selected >= 0 && m_connection_selected < static_cast<int>(m_connections.size()))
        return m_connections[m_connection_selected].description_2;
    return std::string();
}

void MonitorService::SelectConnection(int index)
{
    if (index >= 0 && index < static_cast<int>(m_connections.size()))
    {
        m_connection_selected = index;
        m_connection_name_preferd = SelectedConnectionName();
        m_config.connection_name = m_connection_name_preferd;
        m_config.auto_select = false;
        m_config.select_all = false;
        m_connection_change_flag = true;
    }
}

void MonitorService::SetSelectAll(bool select_all)
{
    m_config.select_all = select_all;
    m_config.auto_select = false;
    m_connection_change_flag = true;
}

MIB_IFROW MonitorService::GetConnectIfTable(int connection_index) const
{
    if (connection_index >= 0 && connection_index < static_cast<int>(m_connections.size()))
    {
        int index = m_connections[connection_index].index;
        if (m_pIfTable != nullptr && index >= 0 && index < static_cast<int>(m_pIfTable->dwNumEntries))
            return m_pIfTable->table[index];
    }
    return MIB_IFROW();
}

void MonitorService::Sample()
{
    //获取网络连接速度
    int rtn{};
    auto getLfTable = [&]() {
        __try
        {
            rtn = GetIfTable(m_pIfTable, &m_dwSize, FALSE);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            free(m_pIfTable);
            m_dwSize = sizeof(MIB_IFTABLE);
            m_pIfTable = (MIB_IFTABLE*)malloc(m_dwSize);
            rtn = GetIfTable(m_pIfTable, &m_dwSize, FALSE);
            if (rtn == ERROR_INSUFFICIENT_BUFFER)	//如果函数返回值为ERROR_INSUFFICIENT_BUFFER，说明m_pIfTable的大小不够
            {
                free(m_pIfTable);
                m_pIfTable = (MIB_IFTABLE*)malloc(m_dwSize);	//用新的大小重新开辟一块内存
            }
            GetIfTable(m_pIfTable, &m_dwSize, FALSE);
        }
    };

    getLfTable();

    if (!m_config.select_all)        //获取当前选中连接的网速
    {
        auto table = GetConnectIfTable(m_connection_selected);
        m_in_bytes = table.dwInOctets;
        m_out_bytes = table.dwOutOctets;
    }
    else        //获取全部连接的网速
    {
        m_in_bytes = 0;
        m_out_bytes = 0;
        for (size_t i{}; i < m_connections.size(); i++)
        {
            auto table = GetConnectIfTable(i);
            m_in_bytes += table.dwInOctets;
            m_out_bytes += table.dwOutOctets;
        }
    }

    unsigned __int64 cur_in_speed{}, cur_out_speed{};       //本次监控时间间隔内的上传和下载速度

    //如果发送和接收的字节数为0或上次发送和接收的字节数为0或当前连接已改变时，网速无效
    if ((m_in_bytes == 0 && m_out_bytes == 0) || (m_last_in_bytes == 0 && m_last_out_bytes == 0) || m_connection_change_flag
        || m_last_in_bytes > m_in_bytes || m_last_out_bytes > m_out_bytes)
    {
        cur_in_speed = 0;
        cur_out_speed = 0;
    }
    else
    {
        cur_in_speed = m_in_bytes - m_last_in_bytes;
        cur_out_speed = m_out_bytes - m_last_out_bytes;
    }

    //计算两次获取网速的时间间隔
    static ULONGLONG last_net_speed_time = 0;
    ULONGLONG net_speed_time = CCommon::GetCurrentTimeSinceEpochMilliseconds();
    int time_span = m_config.monitor_time_span;
    if (last_net_speed_time != 0)
        time_span = static_cast<int>(net_speed_time - last_net_speed_time);
    last_net_speed_time = net_speed_time;

    //将当前监控时间间隔的流量转换成每秒时间间隔内的流量
    m_snapshot.in_speed = static_cast<unsigned __int64>(cur_in_speed * 1000 / time_span);
    m_snapshot.out_speed = static_cast<unsigned __int64>(cur_out_speed * 1000 / time_span);

    m_connection_change_flag = false;    //清除连接发生变化的标志

    m_last_in_bytes = m_in_bytes;
    m_last_out_bytes = m_out_bytes;

    //处于自动选择状态时，如果连续30秒没有网速，则可能自动选择的网络不对，此时执行一次自动选择
    if (m_config.auto_select)
    {
        if (cur_in_speed == 0 && cur_out_speed == 0)
            m_zero_speed_cnt++;
        else
            m_zero_speed_cnt = 0;
        if (m_zero_speed_cnt >= GetSampleCount(30, m_config.monitor_time_span))
        {
            AutoSelect();
            m_zero_speed_cnt = 0;
        }
    }

    //历史流量统计与保存
    UpdateHistoryTraffic(cur_in_speed, cur_out_speed);

    if (rtn == ERROR_INSUFFICIENT_BUFFER)
    {
        InitConnections();
        if (m_config.debug_log)
        {
            CString info = CCommon::LoadTextFormat(IDS_INSUFFICIENT_BUFFER, { m_restart_cnt });
            CCommon::WriteLog(info, m_config.log_path.c_str());
        }
    }

    //重新获取当前连接数量，如果连接数发生变化，则重新初始化连接
    if (m_monitor_time_cnt % GetSampleCount(3, m_config.monitor_time_span) == GetSampleCount(3, m_config.monitor_time_span) - 1)
    {
        static DWORD last_interface_num = -1;
        DWORD interface_num;
        GetNumberOfInterfaces(&interface_num);
        if (last_interface_num != -1 && interface_num != last_interface_num)    //如果连接数发生变化，则重新初始化连接
        {
            if (m_config.debug_log)
            {
                CString info = CCommon::LoadTextFormat(IDS_CONNECTION_NUM_CHANGED, { last_interface_num, interface_num, m_restart_cnt + 1 });
                CCommon::WriteLog(info, m_config.log_path.c_str());
            }
            InitConnections();
            last_interface_num = interface_num;
        }

        //连接名称不匹配时重新初始化连接
        std::string descr;
        descr = (const char*)GetConnectIfTable(m_connection_selected).bDescr;
        if (descr != m_config.connection_name)
        {
            if (m_config.debug_log)
            {
                CString log_str = _T("连接名称不匹配：\r\n");
                log_str += _T("IfTable description: ");
                log_str += descr.c_str();
                log_str += _T("\r\nm_connection_name: ");
                log_str += m_config.connection_name.c_str();
                CCommon::WriteLog(log_str, (m_config.config_dir + L".\\connections.log").c_str());
            }
            InitConnections();
            if (m_config.debug_log)
            {
                CString info = CCommon::LoadTextFormat(IDS_CONNECTION_NOT_MATCH, { m_restart_cnt });
                CCommon::WriteLog(info, m_config.log_path.c_str());
            }
        }
    }

    //CPU/内存采样
    AcquireCpuMemory();
    //硬件数据采样（温度/GPU/HDD）
    AcquireHardwareData();

    m_monitor_time_cnt++;
    m_revision++;
}

void MonitorService::AcquireCpuMemory()
{
    bool lite_version = false;
#ifdef WITHOUT_TEMPERATURE
    lite_version = true;
#endif

    //获取CPU使用率
    m_snapshot.cpu_usage = m_cpu_usage_helper.GetCpuUsage(m_config.cpu_usage_by_time);

    //获取CPU频率（PDH 优先，失败则等待 OHM 兜底）
    m_cpu_freq_acquired = m_cpu_freq_helper.GetCpuFreq(m_snapshot.cpu_freq);
    if (!m_cpu_freq_acquired)
        m_snapshot.cpu_freq = -1;

    //获取GPU利用率（未启用硬件监控时用 PDH，启用后用 OHM）
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

    //获取硬盘利用率（未启用硬件监控时用 PDH，启用后用 OHM）
    m_get_disk_usage_by_pdh = false;
    if (lite_version || !(m_config.hardware_monitor_item & HI_HDD))
    {
        int disk_index = m_disk_usage_helper.FindDiskIndex(m_config.hard_disk_name);
        //没有找到要监控的硬盘时默认使用总体利用率
        if (disk_index < 0)
        {
            disk_index = m_disk_usage_helper.FindDiskIndex(L"_Total");
            if (disk_index >= 0)
            {
                m_config.hard_disk_name = L"_Total";
            }
            //仍然没有找到使用第1块硬盘
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

    //获取内存利用率
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    GlobalMemoryStatusEx(&statex);
    m_snapshot.memory_usage = statex.dwMemoryLoad;
    m_snapshot.used_memory = static_cast<int>((statex.ullTotalPhys - statex.ullAvailPhys) / 1024);
    m_snapshot.total_memory = static_cast<int>(statex.ullTotalPhys / 1024);
}

void MonitorService::AcquireHardwareData()
{
#ifndef WITHOUT_TEMPERATURE
    if (m_hardware_provider == nullptr || !m_hardware_provider->IsAvailable())
    {
        // 硬件监控未启用或未初始化，保持快照中的 -1 默认值
        m_snapshot.cpu_temperature = -1;
        m_snapshot.gpu_temperature = -1;
        m_snapshot.hdd_temperature = -1;
        m_snapshot.main_board_temperature = -1;
        return;
    }

    m_hardware_provider->Acquire();

    m_snapshot.gpu_temperature = m_hardware_provider->GpuTemperature();
    m_snapshot.main_board_temperature = m_hardware_provider->MainboardTemperature();
    //PDH 未获取到 GPU 利用率时使用 OHM 数据
    if (!m_gpu_usage_acquired)
        m_snapshot.gpu_usage = m_hardware_provider->GpuUsage();
    //PDH 未获取到 CPU 频率时使用 OHM 数据
    if (!m_cpu_freq_acquired)
        m_snapshot.cpu_freq = m_hardware_provider->CpuFreq();

    //获取CPU温度
    auto all_cpu_temp = m_hardware_provider->AllCpuTemperature();
    if (!all_cpu_temp.empty())
    {
        if (m_config.cpu_core_name == CCommon::LoadText(IDS_AVREAGE_TEMPERATURE).GetString())  //如果选择了平均温度
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

    //获取硬盘温度
    auto all_hdd_temp = m_hardware_provider->AllHddTemperature();
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

    //获取硬盘利用率（PDH 未获取到时使用 OHM 数据）
    if (!m_get_disk_usage_by_pdh)
    {
        auto all_hdd_usage = m_hardware_provider->AllHddUsage();
        if (!all_hdd_usage.empty())
        {
            auto iter = all_hdd_usage.find(m_config.hard_disk_name);
            if (iter == all_hdd_usage.end())
            {
                iter = all_hdd_usage.begin();
                m_config.hard_disk_name = iter->first;
            }
            m_snapshot.hdd_usage = iter->second;
        }
        else
        {
            m_snapshot.hdd_usage = -1;
        }
    }
#endif
}

void MonitorService::UpdateHistoryTraffic(uint64_t cur_in_speed, uint64_t cur_out_speed)
{
    //检测当前日期是否改变，如果已改变，就向历史流量列表插入一个新的日期
    SYSTEMTIME current_time;
    GetLocalTime(&current_time);
    static int last_check_day = -1;  //用于检测日期变化，重置保存状态
    if (m_history_traffic.GetTodayTraffic().day != current_time.wDay)
    {
        m_history_traffic.OnDateChanged();
        m_today_up_traffic = 0;
        m_today_down_traffic = 0;
        last_check_day = -1;  //重置日期标记，下次检查时会重新初始化保存状态
    }

    //统计今天已使用的流量
    m_today_up_traffic += cur_out_speed;
    m_today_down_traffic += cur_in_speed;
    m_history_traffic.GetTodayTraffic().up_kBytes = m_today_up_traffic / 1024u;
    m_history_traffic.GetTodayTraffic().down_kBytes = m_today_down_traffic / 1024u;
    //每隔30秒保存一次流量历史记录
    if (m_monitor_time_cnt % GetSampleCount(30, m_config.monitor_time_span) == GetSampleCount(30, m_config.monitor_time_span) - 1)
    {
        static unsigned __int64 last_today_kbytes = 0;
        static bool last_today_kbytes_initialized = false;
        unsigned __int64 current_kbytes = m_history_traffic.GetTodayTraffic().kBytes();

        //如果日期改变了，重置初始化状态
        if (last_check_day != current_time.wDay)
        {
            last_today_kbytes_initialized = false;
            last_check_day = current_time.wDay;
        }

        //首次检查时初始化，不保存
        if (!last_today_kbytes_initialized)
        {
            last_today_kbytes = current_kbytes;
            last_today_kbytes_initialized = true;
        }
        else
        {
            //只有当30秒内流量变化超过10MB时才保存历史流量记录，防止磁盘写入过于频繁
            unsigned __int64 change_kbytes = current_kbytes - last_today_kbytes;
            if (change_kbytes >= 10240u) // 10MB = 10240KB
            {
                if (on_history_save)
                    on_history_save();
                last_today_kbytes = current_kbytes;
            }
        }
    }

    //更新快照中的今日流量
    m_snapshot.today_up_traffic = m_today_up_traffic;
    m_snapshot.today_down_traffic = m_today_down_traffic;
}
