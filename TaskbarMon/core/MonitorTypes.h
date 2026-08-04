// MonitorTypes.h : 监控数据核心类型定义（core 层，无 UI 依赖）
#pragma once
#include <cstdint>
#include <map>
#include <string>

// 一次采样得到的完整监控数据快照
struct MonitorSnapshot
{
    uint64_t in_speed{};            // 下载速度（字节/秒）
    uint64_t out_speed{};           // 上传速度（字节/秒）
    int cpu_usage{ -1 };            // CPU利用率
    int memory_usage{ -1 };         // 内存利用率
    int used_memory{};              // 已用物理内存（KB）
    int total_memory{};             // 物理内存总量（KB）
    float cpu_temperature{ -1 };    // CPU温度
    float cpu_freq{ -1 };           // CPU频率（GHz）
    float gpu_temperature{ -1 };    // 显卡温度
    float hdd_temperature{ -1 };    // 硬盘温度
    float main_board_temperature{ -1 }; // 主板温度
    int gpu_usage{ -1 };            // 显卡利用率
    int hdd_usage{ -1 };            // 硬盘利用率
    uint64_t today_up_traffic{};    // 今天已使用上传流量（字节）
    uint64_t today_down_traffic{};  // 今天已使用下载流量（字节）
};

// 硬件数据提供者接口（由 UI 层注入 OpenHardwareMonitor 访问）
class IHardwareDataProvider
{
public:
    virtual ~IHardwareDataProvider() = default;

    // 硬件监控是否已初始化且至少启用一项
    virtual bool IsAvailable() const = 0;
    // 执行一次硬件数据采集（内部需加锁）
    virtual void Acquire() = 0;
    // 采集后读取数据
    virtual float CpuTemperature() const = 0;
    virtual float CpuFreq() const = 0;
    virtual float GpuTemperature() const = 0;
    virtual float HddTemperature() const = 0;
    virtual float MainboardTemperature() const = 0;
    virtual int GpuUsage() const = 0;
    virtual std::map<std::wstring, float> AllCpuTemperature() const = 0;
    virtual std::map<std::wstring, float> AllHddTemperature() const = 0;
    virtual std::map<std::wstring, float> AllHddUsage() const = 0;
};
