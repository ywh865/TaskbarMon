#include "stdafx.h"
#include "AdapterCommon.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <netioapi.h>
#include <new>
#include <unordered_set>

#pragma comment(lib, "iphlpapi.lib")

namespace
{
    bool EqualsIgnoreCase(const string& left, const string& right)
    {
        if (left.size() != right.size())
            return false;

        for (size_t i = 0; i < left.size(); ++i)
        {
            if (std::tolower(static_cast<unsigned char>(left[i])) !=
                std::tolower(static_cast<unsigned char>(right[i])))
            {
                return false;
            }
        }
        return true;
    }

    bool ContainsIgnoreCase(const string& larger, const string& smaller)
    {
        if (smaller.empty() || smaller.size() > larger.size())
            return false;

        for (size_t offset = 0; offset <= larger.size() - smaller.size(); ++offset)
        {
            bool equal = true;
            for (size_t i = 0; i < smaller.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(larger[offset + i])) !=
                    std::tolower(static_cast<unsigned char>(smaller[i])))
                {
                    equal = false;
                    break;
                }
            }
            if (equal)
                return true;
        }
        return false;
    }

    bool IsUsableTable(const MIB_IFTABLE* table)
    {
        return table != nullptr;
    }

    constexpr ULONG kMaximumAdapterInfoBufferBytes = 1024 * 1024;
    constexpr size_t kMaximumAdapterInfoEntries = 4096;

    bool IsAdapterInfoPointerWithinBuffer(const IP_ADAPTER_INFO* adapter,
                                          const std::vector<BYTE>& buffer) noexcept
    {
        if (adapter == nullptr || buffer.size() < sizeof(IP_ADAPTER_INFO))
            return false;

        const uintptr_t begin = reinterpret_cast<uintptr_t>(buffer.data());
        const uintptr_t pointer = reinterpret_cast<uintptr_t>(adapter);
        if (pointer < begin || pointer - begin > buffer.size() - sizeof(IP_ADAPTER_INFO))
            return false;

        return pointer % alignof(IP_ADAPTER_INFO) == 0;
    }

    template <size_t Length>
    string ReadBoundedAnsiBuffer(const char (&value)[Length])
    {
        size_t actual_length{};
        while (actual_length < Length && value[actual_length] != '\0')
            ++actual_length;
        return string(value, actual_length);
    }

    // GetIpForwardTable2 allocates its result for us.  The documented API
    // owns the buffer bounds, but retain a conservative cap before iterating
    // so a corrupted or unexpectedly large table cannot turn route discovery
    // into unbounded startup work.
    constexpr ULONG kMaximumForwardTableEntries = 65536;

    struct DefaultRouteSelection
    {
        DWORD interface_index{};
        uint64_t effective_metric{ (std::numeric_limits<uint64_t>::max)() };
        bool has_candidate{};
        bool ambiguous{};
    };

    bool IsDefaultRoute(const MIB_IPFORWARD_ROW2& route, ADDRESS_FAMILY family)
    {
        return route.InterfaceIndex != 0
            && !route.Loopback
            && route.DestinationPrefix.Prefix.si_family == family
            && route.DestinationPrefix.PrefixLength == 0;
    }

    bool GetEffectiveMetric(const MIB_IPFORWARD_ROW2& route,
                            ADDRESS_FAMILY family,
                            uint64_t& effective_metric)
    {
        MIB_IPINTERFACE_ROW interface_row{};
        interface_row.Family = family;
        interface_row.InterfaceLuid = route.InterfaceLuid;
        if (GetIpInterfaceEntry(&interface_row) != NO_ERROR)
            return false;

        // MIB_IPFORWARD_ROW2::Metric is the route offset.  Windows selects
        // a route using the sum of that offset and the interface metric.
        effective_metric = static_cast<uint64_t>(route.Metric)
            + static_cast<uint64_t>(interface_row.Metric);
        return true;
    }

    bool ConsiderDefaultRoutesForFamily(ADDRESS_FAMILY family, DefaultRouteSelection& selection)
    {
        PMIB_IPFORWARD_TABLE2 table{};
        const DWORD query_result = GetIpForwardTable2(family, &table);
        if (query_result != NO_ERROR || table == nullptr)
        {
            if (table != nullptr)
                FreeMibTable(table);
            return false;
        }

        const ULONG entry_count = table->NumEntries;
        if (entry_count <= kMaximumForwardTableEntries)
        {
            for (ULONG index = 0; index < entry_count; ++index)
            {
                const MIB_IPFORWARD_ROW2& route = table->Table[index];
                if (!IsDefaultRoute(route, family))
                    continue;

                uint64_t effective_metric{};
                if (!GetEffectiveMetric(route, family, effective_metric))
                    continue;

                if (!selection.has_candidate || effective_metric < selection.effective_metric)
                {
                    selection.interface_index = route.InterfaceIndex;
                    selection.effective_metric = effective_metric;
                    selection.has_candidate = true;
                    selection.ambiguous = false;
                }
                else if (effective_metric == selection.effective_metric &&
                         route.InterfaceIndex != selection.interface_index)
                {
                    // There is no safe way to infer which equally preferred
                    // default route Windows will use for traffic we cannot
                    // attribute.  Emit zero rather than guess an interface.
                    selection.ambiguous = true;
                }
            }
        }

        FreeMibTable(table);
        return true;
    }
}

CAdapterCommon::CAdapterCommon()
{
}

CAdapterCommon::~CAdapterCommon()
{
}

void CAdapterCommon::GetAdapterInfo(vector<NetWorkConection>& adapters)
{
    adapters.clear();

    ULONG buffer_size = sizeof(IP_ADAPTER_INFO);
    std::vector<BYTE> buffer(buffer_size);
    DWORD result = ERROR_BUFFER_OVERFLOW;

    // Adapter lists can change while this query is in flight. Retry a bounded
    // number of times and never dereference an undersized buffer.
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        if (buffer.empty())
            break;

        buffer_size = static_cast<ULONG>(buffer.size());
        result = GetAdaptersInfo(reinterpret_cast<PIP_ADAPTER_INFO>(buffer.data()), &buffer_size);
        if (result != ERROR_BUFFER_OVERFLOW)
            break;

        if (buffer_size <= buffer.size() || buffer_size > kMaximumAdapterInfoBufferBytes)
        {
            result = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        try
        {
            buffer.resize(buffer_size);
        }
        catch (const std::bad_alloc&)
        {
            result = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
    }

    if (result == ERROR_SUCCESS)
    {
        try
        {
            PIP_ADAPTER_INFO current = reinterpret_cast<PIP_ADAPTER_INFO>(buffer.data());
            std::vector<NetWorkConection> parsed_adapters;
            std::unordered_set<uintptr_t> visited_addresses;
            const size_t physical_entry_limit = buffer.size() / sizeof(IP_ADAPTER_INFO);
            const size_t entry_limit = (std::min)(physical_entry_limit, kMaximumAdapterInfoEntries);
            bool valid_list = entry_limit != 0;

            while (current != nullptr)
            {
                const uintptr_t current_address = reinterpret_cast<uintptr_t>(current);
                if (parsed_adapters.size() >= entry_limit ||
                    !IsAdapterInfoPointerWithinBuffer(current, buffer) ||
                    !visited_addresses.insert(current_address).second)
                {
                    valid_list = false;
                    break;
                }

                NetWorkConection connection;
                connection.interface_index = current->Index;
                connection.description = ReadBoundedAnsiBuffer(current->Description);
                const string ip_address = ReadBoundedAnsiBuffer(current->IpAddressList.IpAddress.String);
                const string subnet_mask = ReadBoundedAnsiBuffer(current->IpAddressList.IpMask.String);
                const string default_gateway = ReadBoundedAnsiBuffer(current->GatewayList.IpAddress.String);
                connection.ip_address = CCommon::StrToUnicode(ip_address.c_str());
                connection.subnet_mask = CCommon::StrToUnicode(subnet_mask.c_str());
                connection.default_gateway = CCommon::StrToUnicode(default_gateway.c_str());
                parsed_adapters.push_back(std::move(connection));

                PIP_ADAPTER_INFO next = current->Next;
                if (next != nullptr && !IsAdapterInfoPointerWithinBuffer(next, buffer))
                {
                    valid_list = false;
                    break;
                }
                current = next;
            }

            if (valid_list)
                adapters.swap(parsed_adapters);
        }
        catch (const std::bad_alloc&)
        {
            adapters.clear();
        }
    }

    // Preserve the previous UI contract for an offline machine, but leave the
    // placeholder without a table row so it can never be sampled accidentally.
    if (adapters.empty())
    {
        NetWorkConection connection{};
        connection.description = CCommon::UnicodeToStr(CCommon::LoadText(L"<", IDS_NO_CONNECTION, L">"));
        adapters.push_back(connection);
    }
}

void CAdapterCommon::RefreshIpAddress(vector<NetWorkConection>& adapters)
{
    vector<NetWorkConection> adapters_tmp;
    GetAdapterInfo(adapters_tmp);
    for (const auto& adapter_tmp : adapters_tmp)
    {
        for (auto& adapter : adapters)
        {
            const bool same_interface = adapter.interface_index != 0 &&
                adapter.interface_index == adapter_tmp.interface_index;
            if (same_interface || EqualsIgnoreCase(adapter_tmp.description, adapter.description))
            {
                adapter.ip_address = adapter_tmp.ip_address;
                adapter.subnet_mask = adapter_tmp.subnet_mask;
                adapter.default_gateway = adapter_tmp.default_gateway;
                if (adapter.interface_index == 0)
                    adapter.interface_index = adapter_tmp.interface_index;
                break;
            }
        }
    }
}

void CAdapterCommon::GetIfTableInfo(vector<NetWorkConection>& adapters, MIB_IFTABLE* pIfTable)
{
    if (!IsUsableTable(pIfTable))
    {
        for (auto& adapter : adapters)
        {
            adapter.index = -1;
            adapter.description_2.clear();
        }
        return;
    }

    for (auto& adapter : adapters)
    {
        int table_index = -1;
        if (adapter.interface_index != 0)
            table_index = FindIfTableRowByInterfaceIndex(adapter.interface_index, pIfTable);
        if (table_index == -1 && !adapter.description.empty())
            table_index = FindConnectionInIfTable(adapter.description, pIfTable);
        if (table_index == -1 && !adapter.description.empty())
            table_index = FindConnectionInIfTableFuzzy(adapter.description, pIfTable);

        adapter.index = table_index;
        adapter.description_2.clear();
        adapter.in_bytes = 0;
        adapter.out_bytes = 0;
        if (table_index == -1)
            continue;

        const MIB_IFROW& row = pIfTable->table[table_index];
        adapter.interface_index = row.dwIndex;
        adapter.in_bytes = row.dwInOctets;
        adapter.out_bytes = row.dwOutOctets;
        adapter.description_2 = GetIfTableDescription(row);
    }
}

void CAdapterCommon::GetAllIfTableInfo(vector<NetWorkConection>& adapters, MIB_IFTABLE* pIfTable)
{
    vector<NetWorkConection> adapters_tmp;
    GetAdapterInfo(adapters_tmp);
    adapters.clear();
    if (!IsUsableTable(pIfTable))
        return;

    for (DWORD i = 0; i < pIfTable->dwNumEntries; ++i)
    {
        const MIB_IFROW& row = pIfTable->table[i];
        NetWorkConection connection;
        connection.description = connection.description_2 = GetIfTableDescription(row);
        connection.index = static_cast<int>(i);
        connection.interface_index = row.dwIndex;
        connection.in_bytes = row.dwInOctets;
        connection.out_bytes = row.dwOutOctets;

        for (const auto& adapter : adapters_tmp)
        {
            const bool same_interface = adapter.interface_index != 0 &&
                adapter.interface_index == connection.interface_index;
            if (same_interface ||
                (!adapter.description.empty() && ContainsIgnoreCase(connection.description, adapter.description)))
            {
                connection.ip_address = adapter.ip_address;
                connection.subnet_mask = adapter.subnet_mask;
                connection.default_gateway = adapter.default_gateway;
                break;
            }
        }
        adapters.push_back(connection);
    }
}

int CAdapterCommon::FindIfTableRowByInterfaceIndex(DWORD interface_index, const MIB_IFTABLE* pIfTable)
{
    if (interface_index == 0 || !IsUsableTable(pIfTable))
        return -1;

    for (DWORD i = 0; i < pIfTable->dwNumEntries; ++i)
    {
        if (pIfTable->table[i].dwIndex == interface_index)
            return static_cast<int>(i);
    }
    return -1;
}

DWORD CAdapterCommon::GetDefaultRouteInterfaceIndex()
{
    // A fixed public probe (for example 8.8.8.8) can match a more-specific
    // VPN or policy route and is therefore not evidence of the default route.
    // IPv4 and IPv6 route metrics are independent: comparing a v4 metric to
    // a v6 metric and picking the smaller one silently attributes traffic to
    // the wrong NIC when their default routes differ.  Accept an interface
    // only when every present family resolves it uniquely to that same NIC.
    DefaultRouteSelection ipv4_selection;
    DefaultRouteSelection ipv6_selection;
    if (!ConsiderDefaultRoutesForFamily(AF_INET, ipv4_selection) ||
        !ConsiderDefaultRoutesForFamily(AF_INET6, ipv6_selection))
    {
        return 0;
    }

    if (ipv4_selection.ambiguous || ipv6_selection.ambiguous)
        return 0;

    if (ipv4_selection.has_candidate && ipv6_selection.has_candidate)
    {
        return ipv4_selection.interface_index == ipv6_selection.interface_index
            ? ipv4_selection.interface_index
            : 0;
    }

    if (ipv4_selection.has_candidate)
        return ipv4_selection.interface_index;
    if (ipv6_selection.has_candidate)
        return ipv6_selection.interface_index;
    return 0;
}

string CAdapterCommon::GetIfTableDescription(const MIB_IFROW& row)
{
    size_t length = std::min<size_t>(row.dwDescrLen, sizeof(row.bDescr));
    if (length == 0)
    {
        while (length < sizeof(row.bDescr) && row.bDescr[length] != 0)
            ++length;
    }
    while (length > 0 && row.bDescr[length - 1] == 0)
        --length;
    return string(reinterpret_cast<const char*>(row.bDescr), length);
}

int CAdapterCommon::FindConnectionInIfTable(string connection, MIB_IFTABLE* pIfTable)
{
    if (connection.empty() || !IsUsableTable(pIfTable))
        return -1;

    int match = -1;
    for (DWORD i = 0; i < pIfTable->dwNumEntries; ++i)
    {
        if (!EqualsIgnoreCase(GetIfTableDescription(pIfTable->table[i]), connection))
            continue;

        // Duplicate descriptions are ambiguous. Do not select an arbitrary
        // interface just because its text happens to match.
        if (match != -1)
            return -1;
        match = static_cast<int>(i);
    }
    return match;
}

int CAdapterCommon::FindConnectionInIfTableFuzzy(string connection, MIB_IFTABLE* pIfTable)
{
    if (connection.empty() || !IsUsableTable(pIfTable))
        return -1;

    int match = -1;
    for (DWORD i = 0; i < pIfTable->dwNumEntries; ++i)
    {
        const string description = GetIfTableDescription(pIfTable->table[i]);
        const bool contains = description.size() >= connection.size()
            ? ContainsIgnoreCase(description, connection)
            : ContainsIgnoreCase(connection, description);
        if (!contains)
            continue;

        // Fuzzy matching is retained only as an unambiguous compatibility
        // fallback. The old edit-distance "best" match could bind traffic to
        // an unrelated adapter when descriptions changed.
        if (match != -1)
            return -1;
        match = static_cast<int>(i);
    }
    return match;
}
