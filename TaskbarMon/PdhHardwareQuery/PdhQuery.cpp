#include "stdafx.h"
#include "PdhQuery.h"

#include <cstdint>
#include <cmath>
#include <new>
#include <utility>
#include <vector>

namespace
{
    // PDH sizes and item names originate with the installed counter provider.
    // Bound them before allocation or dereference so a corrupt provider cannot
    // consume arbitrary memory or cause an out-of-bounds read.
    constexpr DWORD kMaximumCounterArrayBytes = 4u * 1024u * 1024u;
    constexpr DWORD kMaximumCounterArrayItems = 65536u;

    bool GetBoundedCounterNameLength(const TCHAR* name, const BYTE* buffer_begin,
        size_t buffer_size, size_t& name_length)
    {
        name_length = 0;
        if (name == nullptr || buffer_begin == nullptr || buffer_size < sizeof(TCHAR))
            return false;

        const uintptr_t begin = reinterpret_cast<uintptr_t>(buffer_begin);
        const uintptr_t pointer = reinterpret_cast<uintptr_t>(name);
        if (pointer < begin || pointer - begin > buffer_size - sizeof(TCHAR) ||
            pointer % alignof(TCHAR) != 0)
        {
            return false;
        }

        const size_t offset = static_cast<size_t>(pointer - begin);
        const size_t remaining_characters = (buffer_size - offset) / sizeof(TCHAR);
        while (name_length < remaining_characters && name[name_length] != _T('\0'))
            ++name_length;
        return name_length < remaining_characters;
    }
}
CPdhQuery::CPdhQuery(LPCTSTR _fullCounterPath)
    : fullCounterPath(_fullCounterPath)
{
    Initialize();
}

CPdhQuery::~CPdhQuery()
{
    //关闭查询
    PdhCloseQuery(query);
}

bool CPdhQuery::Initialize()
{
    if (isInitialized)
        return true;

    PDH_STATUS status;
    //打开查询
    status = PdhOpenQuery(NULL, NULL, &query);
    if (status != ERROR_SUCCESS)
        return false;

    //添加计数器
    status = PdhAddCounter(query, fullCounterPath.GetString(), NULL, &counter);
    //先调用PdhAddCounter，如果失败使用PdhAddEnglishCounter再试一次
    if (status != ERROR_SUCCESS)
    {
        status = PdhAddEnglishCounter(query, fullCounterPath.GetString(), NULL, &counter);
        if (status != ERROR_SUCCESS)
        {
            PdhCloseQuery(query);
            query = nullptr;
            return false;
        }
    }

    //初始化计数器
    if (PdhCollectQueryData(query) != ERROR_SUCCESS)
    {
        PdhCloseQuery(query);
        query = nullptr;
        counter = nullptr;
        return false;
    }

    isInitialized = true;
    return true;
}

bool CPdhQuery::QueryValue(double& value)
{
    if (!isInitialized)
        return false;

    //更新数据
    const PDH_STATUS collect_status = PdhCollectQueryData(query);
    if (collect_status != ERROR_SUCCESS)
        return false;

    PDH_FMT_COUNTERVALUE pdhValue{};
    DWORD dwValue{};
    PDH_STATUS status = PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, &dwValue, &pdhValue);
    if (status != ERROR_SUCCESS || pdhValue.CStatus != ERROR_SUCCESS ||
        !std::isfinite(pdhValue.doubleValue))
    {
        return false;
    }
    value = pdhValue.doubleValue;
    return true;
}

bool CPdhQuery::QueryValues(std::vector<CounterValueItem>& values)
{
    values.clear();
    if (!isInitialized)
        return false;

    const PDH_STATUS collect_status = PdhCollectQueryData(query);
    if (collect_status != ERROR_SUCCESS)
        return false;

    DWORD buffer_size{};
    DWORD item_count{};
    const PDH_STATUS first_status = PdhGetFormattedCounterArray(
        counter, PDH_FMT_DOUBLE, &buffer_size, &item_count, nullptr);
    if (first_status != PDH_MORE_DATA ||
        buffer_size < sizeof(PDH_FMT_COUNTERVALUE_ITEM) ||
        buffer_size > kMaximumCounterArrayBytes ||
        item_count > kMaximumCounterArrayItems)
    {
        return false;
    }

    try
    {
        std::vector<BYTE> buffer(buffer_size);
        const DWORD allocated_buffer_size = buffer_size;
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM*>(buffer.data());
        const PDH_STATUS second_status = PdhGetFormattedCounterArray(
            counter, PDH_FMT_DOUBLE, &buffer_size, &item_count, items);
        if (second_status != ERROR_SUCCESS ||
            buffer_size > allocated_buffer_size ||
            item_count > kMaximumCounterArrayItems ||
            static_cast<size_t>(item_count) > buffer.size() / sizeof(PDH_FMT_COUNTERVALUE_ITEM))
        {
            return false;
        }

        std::vector<CounterValueItem> collected_values;
        collected_values.reserve(item_count);
        for (DWORD index = 0; index < item_count; ++index)
        {
            size_t name_length{};
            if (!GetBoundedCounterNameLength(items[index].szName, buffer.data(), buffer.size(), name_length))
                return false;

            CounterValueItem value_item;
            value_item.name.assign(items[index].szName, name_length);
            if (items[index].FmtValue.CStatus != ERROR_SUCCESS ||
                !std::isfinite(items[index].FmtValue.doubleValue))
            {
                return false;
            }
            value_item.value = items[index].FmtValue.doubleValue;
            collected_values.push_back(std::move(value_item));
        }

        values.swap(collected_values);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
}
