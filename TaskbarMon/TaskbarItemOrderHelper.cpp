#include "stdafx.h"
#include "TaskbarItemOrderHelper.h"
#include "Common.h"
#include "TaskbarMon.h"

#include <cwctype>

namespace
{
    bool TryParseItemIndex(const std::wstring& value, size_t begin, size_t end,
                           size_t item_count, int& item_index)
    {
        if (item_count == 0)
            return false;

        while (begin < end && std::iswspace(static_cast<wint_t>(value[begin])))
            ++begin;
        if (begin < end && value[begin] == L'+')
            ++begin;

        const size_t maximum_index = item_count - 1;
        size_t parsed_value{};
        bool has_digit{};
        while (begin < end && std::iswdigit(static_cast<wint_t>(value[begin])))
        {
            const size_t digit = static_cast<size_t>(value[begin] - L'0');
            if (digit > maximum_index ||
                parsed_value > (maximum_index - digit) / 10)
            {
                return false;
            }

            parsed_value = parsed_value * 10 + digit;
            has_digit = true;
            ++begin;
        }

        while (begin < end && std::iswspace(static_cast<wint_t>(value[begin])))
            ++begin;
        if (!has_digit || begin != end)
            return false;

        item_index = static_cast<int>(parsed_value);
        return true;
    }
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

CTaskbarItemOrderHelper::CTaskbarItemOrderHelper(bool displayed_only)
    : m_displayed_only(displayed_only)
{
}

void CTaskbarItemOrderHelper::Init()
{
    m_all_item_in_default_order.clear();
    m_all_item_in_default_order.reserve(AllDisplayItems.size());
    for (const auto& item : AllDisplayItems)
    {
        m_all_item_in_default_order.push_back(item);
    }
}

std::vector<CommonDisplayItem> CTaskbarItemOrderHelper::GetAllDisplayItemsWithOrder() const
{
    std::vector<CommonDisplayItem> items;
    for (auto i : m_item_order)
    {
        if (i >= 0 && i < static_cast<int>(m_all_item_in_default_order.size()))
        {
            if (m_displayed_only && !IsItemDisplayed(m_all_item_in_default_order[i]))
            {
                continue;
            }
            items.push_back(m_all_item_in_default_order[i]);
        }
    }

    return items;
}

void CTaskbarItemOrderHelper::FromString(const std::wstring& str)
{
    m_item_order.clear();
    const size_t item_count = AllDisplayItems.size();
    if (item_count == 0)
        return;

    // item_order is user-editable INI input.  Parse it without materialising
    // every comma-separated token and retain at most one occurrence of each
    // valid item.  This keeps a value such as "0,0,0,..." linear in input
    // size instead of feeding a huge vector into erase-based de-duplication.
    m_item_order.reserve(item_count);
    std::vector<bool> seen(item_count);
    size_t item_begin{};
    while (item_begin <= str.size())
    {
        size_t item_end = str.find(L',', item_begin);
        if (item_end == std::wstring::npos)
            item_end = str.size();

        int item_index{};
        if (TryParseItemIndex(str, item_begin, item_end, item_count, item_index) &&
            !seen[static_cast<size_t>(item_index)])
        {
            seen[static_cast<size_t>(item_index)] = true;
            m_item_order.push_back(item_index);
            if (m_item_order.size() == item_count)
                break;
        }

        if (item_end == str.size())
            break;
        item_begin = item_end + 1;
    }
    NormalizeItemOrder();
}

std::wstring CTaskbarItemOrderHelper::ToString() const
{
    std::wstring result;
    for (int i : m_item_order)
    {
        result += std::to_wstring(i);
        result.push_back(L',');
    }
    if (!m_item_order.empty())
        result.pop_back();
    return result;
}

void CTaskbarItemOrderHelper::SetOrder(const vector<int>& item_order)
{
    m_item_order = item_order;
    NormalizeItemOrder();
}

const vector<int>& CTaskbarItemOrderHelper::GetItemOrderConst() const
{
    return m_item_order;
}

vector<int>& CTaskbarItemOrderHelper::GetItemOrder()
{
    return m_item_order;
}

CString CTaskbarItemOrderHelper::GetItemDisplayName(CommonDisplayItem item)
{
    return item.GetItemName();
}

bool CTaskbarItemOrderHelper::IsItemDisplayed(CommonDisplayItem item)
{
    bool displayed = true;
    if ((item == TDI_CPU_TEMP) && !theApp.m_general_data.IsHardwareEnable(HI_CPU))
        displayed = false;
    if ((item == TDI_GPU_TEMP) && !theApp.m_general_data.IsHardwareEnable(HI_GPU))
        displayed = false;
    if ((item == TDI_HDD_TEMP) && !theApp.m_general_data.IsHardwareEnable(HI_HDD))
        displayed = false;
    if (item == TDI_MAIN_BOARD_TEMP && !theApp.m_general_data.IsHardwareEnable(HI_MBD))
        displayed = false;

    return displayed;
}

void CTaskbarItemOrderHelper::NormalizeItemOrder()
{
    const size_t item_num = AllDisplayItems.size();
    std::vector<bool> seen(item_num);
    vector<int> normalized_order;
    normalized_order.reserve(item_num);

    for (int item_index : m_item_order)
    {
        if (item_index < 0 || static_cast<size_t>(item_index) >= item_num ||
            seen[static_cast<size_t>(item_index)])
        {
            continue;
        }

        const size_t index = static_cast<size_t>(item_index);
        if (m_displayed_only && index < m_all_item_in_default_order.size() &&
            !IsItemDisplayed(m_all_item_in_default_order[index]))
        {
            continue;
        }

        seen[index] = true;
        normalized_order.push_back(item_index);
    }

    // Keep every built-in item in the persisted order.  Items currently
    // hidden by hardware settings are appended after the visible entries, as
    // before, so their preference is retained when they become available.
    for (size_t index{}; index < item_num; ++index)
    {
        if (!seen[index])
            normalized_order.push_back(static_cast<int>(index));
    }

    m_item_order.swap(normalized_order);
}
