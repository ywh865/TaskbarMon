#include "stdafx.h"
#include "CommonData.h"
#include "Common.h"
#include "CalendarHelper.h"
#include "TaskbarMon.h"
#include "WindowsSettingHelper.h"

#include <cwctype>

namespace
{
    constexpr size_t kMaximumStringSetItems = 128;
    constexpr size_t kMaximumStringSetItemLength = 128;

    bool IsConfigWhitespace(wchar_t value)
    {
        return std::iswspace(static_cast<wint_t>(value)) != 0;
    }
}

///////////////////////////////////////////////////////////////////////////////////
int Date::week() const
{
    //计算当前是一年的第几天
    int days{};
    for (int i{ 1 }; i < month; i++)
    {
        days += CCalendarHelper::DaysInMonth(year, i);
    }
    days += day;
    //计算这一年的1月1日是星期几
    int week_day = CCalendarHelper::CaculateWeekDay(year, 1, 1);
    if (theApp.m_cfg_data.m_sunday_first)
    {
        days += (week_day - 1);
    }
    else
    {
        days += (week_day - 2);
    }
    return days / 7 + 1;
}

bool Date::DateGreater(const Date& a, const Date& b)
{
    if (a.year != b.year)
        return a.year > b.year;
    else if (a.month != b.month)
        return a.month > b.month;
    else if (a.day != b.day)
        return a.day > b.day;
    else
        return false;
}

bool Date::DateEqual(const Date& a, const Date& b)
{
    return a.year == b.year && a.month == b.month && a.day == b.day;
}


///////////////////////////////////////////////////////////////////////////////////
//HistoryTraffic
unsigned __int64 HistoryTraffic::kBytes() const
{
    return up_kBytes + down_kBytes;
}


///////////////////////////////////////////////////////////////////////////////////
wstring& DispStrings::Get(CommonDisplayItem item)
{
    return map_str[item];
}

const wstring& DispStrings::GetConst(CommonDisplayItem item) const
{
    auto iter = map_str.find(item);
    if (iter != map_str.end())
        return iter->second;
    static wstring empty_str;
    return empty_str;
}

const std::map<CommonDisplayItem, wstring>& DispStrings::GetAllItems() const
{
    return map_str;
}

bool DispStrings::operator==(const DispStrings& disp_str) const
{
    return map_str == disp_str.map_str;
}

bool DispStrings::IsInvalid() const
{
    return map_str.empty();
}

///////////////////////////////////////////////////////////////////////////////////
bool FontInfo::operator==(const FontInfo& a) const
{
    return name == a.name && size == a.size && bold == a.bold && italic == a.italic
        && underline == a.underline && strike_out == a.strike_out;
}

///////////////////////////////////////////////////////////////////////////////////
bool StringSet::Contains(const std::wstring& str) const
{
    return string_set.count(str) != 0;
}

void StringSet::SetStrContained(const std::wstring& str, bool contained)
{
    if (contained)
        string_set.insert(str);
    else
        string_set.erase(str);
}

void StringSet::FromString(const std::wstring& str)
{
    string_set.clear();
    size_t item_begin{};
    size_t item_count{};
    while (item_begin <= str.size())
    {
        // StringSet is fed by user-editable configuration.  Stop before a
        // comma-heavy value can allocate an unbounded vector or set.
        if (item_count >= kMaximumStringSetItems)
        {
            string_set.clear();
            return;
        }
        ++item_count;

        size_t item_end = str.find(L',', item_begin);
        if (item_end == std::wstring::npos)
            item_end = str.size();

        size_t begin = item_begin;
        size_t end = item_end;
        while (begin < end && IsConfigWhitespace(str[begin]))
            ++begin;
        while (end > begin && IsConfigWhitespace(str[end - 1]))
            --end;

        if (begin != end)
        {
            if (end - begin > kMaximumStringSetItemLength)
            {
                string_set.clear();
                return;
            }
            string_set.emplace(str, begin, end - begin);
        }

        if (item_end == str.size())
            return;
        item_begin = item_end + 1;
    }
}

std::wstring StringSet::ToString() const
{
    std::vector<std::wstring> item_vect;
    for (const auto& i : string_set)
        item_vect.push_back(i);
    std::wstring item_str;
    for (const auto& i : item_vect)
    {
        item_str += i;
        item_str += L',';
    }
    if (!item_str.empty())
        item_str.pop_back();
    return item_str;
}

void StringSet::FromVector(const std::vector<std::wstring>& vec)
{
    string_set.clear();
    for (const auto& str : vec)
        string_set.insert(str);
}

std::vector<std::wstring> StringSet::ToVector() const
{
    std::vector<std::wstring> vec;
    for (const auto& str : string_set)
        vec.push_back(str);
    return vec;
}

std::set<std::wstring>& StringSet::data()
{
    return string_set;
}

///////////////////////////////////////////////////////////////////////////////////
bool TaskBarSettingData::IsTaskbarTransparent() const
{
    if (CWindowsSettingHelper::IsWindows10LightTheme() || theApp.m_win_version.IsWindows8Or8point1() || theApp.IsWindows11Taskbar())
        return (transparent_color == back_color);
    else
        return transparent_color == 0;
}

void TaskBarSettingData::SetTaskabrTransparent(bool transparent)
{
    if (transparent)
    {
        if (CWindowsSettingHelper::IsWindows10LightTheme() || theApp.m_win_version.IsWindows8Or8point1() || theApp.IsWindows11Taskbar())
        {
            //浅色模式下要设置任务栏窗口透明，只需将透明色设置成和背景色一样即可
            CCommon::TransparentColorConvert(back_color);
            transparent_color = back_color;
        }
        else
        {
            //深色模式下，背景色透明将透明色设置成黑色
            transparent_color = 0;
        }
    }
    else
    {
        //要设置任务栏窗口不透明，只需将透明色设置成和背景色不一样即可
        if (back_color != TASKBAR_TRANSPARENT_COLOR1)
            transparent_color = TASKBAR_TRANSPARENT_COLOR1;
        else
            transparent_color = TASKBAR_TRANSPARENT_COLOR2;
    }
}

void TaskBarSettingData::ValidItemSpace()
{
    if (item_space < 0)
        item_space = 0;
    if (item_space > 32)
        item_space = 32;
}

void TaskBarSettingData::ValidVerticalMargin()
{
    if (vertical_margin < -10)
        vertical_margin = -10;
    if (vertical_margin > 10)
        vertical_margin = 10;
}

void TaskBarSettingData::ValidWindowOffsetTop()
{
    if (window_offset_top < -20)
        window_offset_top = -20;
    if (window_offset_top > 20)
        window_offset_top = 20;
}

void TaskBarSettingData::ValidWindowOffsetLeft()
{
    if (window_offset_left < -800)
        window_offset_left = -800;
    if (window_offset_left > 800)
        window_offset_left = 800;
}

unsigned __int64 TaskBarSettingData::GetNetspeedFigureMaxValueInBytes() const
{
    if (netspeed_figure_max_value_unit == 0)        //单位为KB
        return static_cast<unsigned __int64>(netspeed_figure_max_value) * 1024;
    else
        return static_cast<unsigned __int64>(netspeed_figure_max_value) * 1024 * 1024;
}

COLORREF TaskBarSettingData::GetUsageGraphColor() const
{
    if (graph_color_following_system)
    {
        COLORREF theme_color = theApp.GetThemeColor();
        //转换为HLS
        double h, l, s;
        CDrawingManager::RGBtoHSL(theme_color, &h, &s, &l);
        //根据当前系统深浅色模式指定亮度
        if (theApp.m_last_light_mode)
        {
            //浅色任务栏，将亮度设为0.7
            l = 0.7;
        }
        else
        {
            //深色任务栏，将亮度设为0.4
            l = 0.4;
        }
        //转换回RGB
        COLORREF graph_color = CDrawingManager::HLStoRGB_ONE(h, l, s);
        return graph_color;
    }
    else
    {
        return status_bar_color;
    }
}

wstring LanguageInfo::toConfigString() const
{
    return bcp_47 + L"|" + display_name + L"|" + translator;
}

void LanguageInfo::fromConfigString(const wstring& config_str)
{
    // Only the first three fields are consumed.  Do not use StringSplit here:
    // a user-editable value containing a large number of separators would
    // otherwise materialize an equally large vector of empty strings.
    constexpr size_t kMaxLanguageConfigFieldLength = 128;
    wstring* fields[] = { &bcp_47, &display_name, &translator };
    size_t field_begin{};
    for (size_t i{}; i < _countof(fields) && field_begin != wstring::npos; ++i)
    {
        const size_t field_end = config_str.find(L'|', field_begin);
        const size_t field_length = (field_end == wstring::npos ? config_str.size() : field_end) - field_begin;
        wstring field = config_str.substr(field_begin, (std::min)(field_length, kMaxLanguageConfigFieldLength));
        CCommon::StringNormalize(field);
        *fields[i] = std::move(field);
        field_begin = field_end == wstring::npos ? wstring::npos : field_end + 1;
    }
    const LCID locale_id = LocaleNameToLCID(bcp_47.c_str(), 0);
    language_id = LANGIDFROMLCID(locale_id);
}
