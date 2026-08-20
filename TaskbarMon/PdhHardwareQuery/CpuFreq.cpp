#include "stdafx.h"
#include "CpuFreq.h"
#include "DllFunctions.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <powerbase.h>
#include <sysinfoapi.h>


///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////

typedef struct _PROCESSOR_POWER_INFORMATION {
    ULONG Number;
    ULONG MaxMhz;
    ULONG CurrentMhz;
    ULONG MhzLimit;
    ULONG MaxIdleState;
    ULONG CurrentIdleState;
} PROCESSOR_POWER_INFORMATION, * PPROCESSOR_POWER_INFORMATION;

namespace
{
    // powrprof.dll is not a KnownDLL. Resolve it from System32 explicitly so
    // the application's directory and current working directory cannot supply
    // an attacker-controlled delay-load DLL.
    const CDllFunction<decltype(&::CallNtPowerInformation)> CallNtPowerInformationFromSystem{
        L"powrprof.dll", "CallNtPowerInformation"};
}

CPdhCpuFreq::CPdhCpuFreq()
    : CPdhQuery(_T("\\Processor Information(_Total)\\% Processor Performance"))
{
    //获取max_cpu_freq
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    if (si.dwNumberOfProcessors == 0 || !CallNtPowerInformationFromSystem.HasValue())
        return;

    auto ppInfo = std::vector<PROCESSOR_POWER_INFORMATION>(si.dwNumberOfProcessors);
    const auto status = CallNtPowerInformationFromSystem(POWER_INFORMATION_LEVEL::ProcessorInformation,
        nullptr, 0, ppInfo.data(), static_cast<ULONG>(sizeof(PROCESSOR_POWER_INFORMATION) * ppInfo.size()));
    if (status != 0)
        return;

    for (size_t i = 0; i < ppInfo.size(); i++)
    {
        max_cpu_freq = (std::max)(max_cpu_freq, ppInfo[i].MaxMhz / 1000.f);
    }
}

bool CPdhCpuFreq::GetCpuFreq(float& freq)
{
    double value{};
    if (max_cpu_freq > 0 && QueryValue(value))
    {
        const double calculated_frequency = value / 100.0 * static_cast<double>(max_cpu_freq);
        if (!std::isfinite(calculated_frequency) ||
            calculated_frequency < static_cast<double>((std::numeric_limits<float>::lowest)()) ||
            calculated_frequency > static_cast<double>((std::numeric_limits<float>::max)()))
        {
            return false;
        }
        freq = static_cast<float>(calculated_frequency);
        return true;
    }
    return false;
}
