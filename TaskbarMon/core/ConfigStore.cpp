// ConfigStore.cpp : 配置读写实现
#include "stdafx.h"
#include "ConfigStore.h"
#include "Common.h"
#include "SettingsHelper.h"
#include "TaskbarDefaultStyle.h"
#include "TaskBarDlgDrawCommon.h"

namespace
{
    constexpr size_t kMaximumPluginDisplayItemConfigLength = 4096;
    constexpr size_t kMaximumDoubleClickExecutableLength = 32767;
    constexpr int kMaximumNotifyInterval = 24 * 60 * 60;
    constexpr size_t kMaximumDeviceNameLength = 256;

    bool IsOutOfRange(int value, int minimum, int maximum)
    {
        return value < minimum || value > maximum;
    }

    void ResetIfOutOfRange(int& value, int minimum, int maximum, int fallback)
    {
        if (IsOutOfRange(value, minimum, maximum))
            value = fallback;
    }

    void LimitStringLength(std::wstring& value, size_t maximum_length)
    {
        if (value.size() > maximum_length)
            value.resize(maximum_length);
    }
}

ConfigStore::ConfigStore(const std::wstring& path)
    : m_path(path)
{
}

void ConfigStore::Load(GeneralSettingData& general, TaskBarSettingData& taskbar, MainConfigData& config,
                       const EnvironmentDefaults& defaults)
{
    CSettingsHelper ini{ m_path };

    //常规设置
    general.check_update_when_start = ini.GetBool(_T("general"), _T("check_update_when_start"), true);
    general.update_source = ini.GetInt(L"general", L"update_source", defaults.update_source_default);
    general.show_all_interface = ini.GetBool(L"general", L"show_all_interface", false);
    general.cpu_usage_acquire_method = static_cast<GeneralSettingData::CpuUsageAcquireMethod>(ini.GetInt(L"general", L"cpu_usage_acquire_method", GeneralSettingData::CA_PDH));
    general.monitor_time_span = ini.GetInt(L"general", L"monitor_time_span", 1000);
    general.hard_disk_name = ini.GetString(L"general", L"hard_disk_name", L"");
    // Store the localized display value because the settings dialog persists
    // the selected combo-box text. Keep legacy English values compatible in
    // MonitorService when an older config is opened in another locale.
    general.cpu_core_name = ini.GetString(
        L"general", L"cpu_core_name", CCommon::LoadText(IDS_AVREAGE_TEMPERATURE).GetString());
    general.hardware_monitor_item = ini.GetInt(L"general", L"hardware_monitor_item", 0);
    std::vector<std::wstring> connections_hide;
    ini.GetStringList(L"general", L"connections_hide", connections_hide, std::vector<std::wstring>{});
    general.connections_hide.FromVector(connections_hide);

    //网络连接设置
    config.m_show_task_bar_wnd = ini.GetBool(_T("config"), _T("show_task_bar_wnd"), false);
    config.m_auto_select = ini.GetBool(_T("connection"), _T("auto_select"), true);
    config.m_select_all = ini.GetBool(_T("connection"), _T("select_all"), false);
    std::wstring connection_name = ini.GetString(L"connection", L"connection_name", L"");
    LimitStringLength(connection_name, kMaximumDeviceNameLength);
    config.m_connection_name = CCommon::UnicodeToStr(connection_name.c_str());
    general.show_notify_icon = ini.GetBool(_T("config"), _T("show_notify_icon"), true);
    config.m_notify_icon_selected = ini.GetInt(_T("config"), _T("notify_icon_selected"), (defaults.is_windows7_or_8 ? 2 : defaults.default_notify_icon));
    config.m_notify_icon_auto_adapt = ini.GetBool(_T("config"), _T("notify_icon_auto_adapt"), true);

    //通知消息设置
    general.traffic_tip_enable = ini.GetBool(L"notify_tip", L"traffic_tip_enable", false);
    general.traffic_tip_value = ini.GetInt(L"notify_tip", L"traffic_tip_value", 200);
    general.traffic_tip_unit = ini.GetInt(L"notify_tip", L"traffic_tip_unit", 0);
    general.memory_usage_tip.enable = ini.GetBool(L"notify_tip", L"memory_usage_tip_enable", false);
    general.memory_usage_tip.tip_value = ini.GetInt(L"notify_tip", L"memory_tip_value", 80);
    general.cpu_temp_tip.enable = ini.GetBool(L"notify_tip", L"cpu_temperature_tip_enable", false);
    general.cpu_temp_tip.tip_value = ini.GetInt(L"notify_tip", L"cpu_temperature_tip_value", 80);
    general.gpu_temp_tip.enable = ini.GetBool(L"notify_tip", L"gpu_temperature_tip_enable", false);
    general.gpu_temp_tip.tip_value = ini.GetInt(L"notify_tip", L"gpu_temperature_tip_value", 80);
    general.hdd_temp_tip.enable = ini.GetBool(L"notify_tip", L"hdd_temperature_tip_enable", false);
    general.hdd_temp_tip.tip_value = ini.GetInt(L"notify_tip", L"hdd_temperature_tip_value", 80);
    general.mainboard_temp_tip.enable = ini.GetBool(L"notify_tip", L"mainboard_temperature_tip_enable", false);
    general.mainboard_temp_tip.tip_value = ini.GetInt(L"notify_tip", L"mainboard_temperature_tip_value", 80);

    //任务栏窗口设置
    taskbar.back_color = ini.GetInt(_T("task_bar"), _T("task_bar_back_color"), taskbar.dft_back_color);
    taskbar.transparent_color = ini.GetInt(_T("task_bar"), _T("transparent_color"), taskbar.dft_transparent_color);
    if (taskbar.IsTaskbarTransparent()) //如果任务栏背景透明，则需要将颜色转换一下
    {
        CCommon::TransparentColorConvert(taskbar.back_color);
        CCommon::TransparentColorConvert(taskbar.transparent_color);
    }
    taskbar.status_bar_color = ini.GetInt(_T("task_bar"), _T("status_bar_color"), taskbar.dft_status_bar_color);
    ini.LoadTaskbarWndColors(_T("task_bar"), _T("task_bar_text_color"), taskbar.text_colors, taskbar.dft_text_colors);
    taskbar.specify_each_item_color = ini.GetBool(L"task_bar", L"specify_each_item_color", false);
    taskbar.display_item.FromInt(ini.GetInt(L"task_bar", L"tbar_display_item", DisplayItemSet{ TDI_UP, TDI_DOWN }.ToInt()));
    taskbar.show_taskbar_wnd_in_secondary_display = ini.GetBool(L"task_bar", L"show_taskbar_wnd_in_secondary_display", false);
    taskbar.secondary_display_index = ini.GetInt(L"task_bar", L"secondary_display_index", 0);

    if (taskbar.back_color == 0 && !taskbar.text_colors.empty() && taskbar.text_colors.begin()->second.label == 0)     //万一读取到的背景色和文本颜色都为0（黑色），则将文本色和背景色设置成默认颜色
    {
        taskbar.back_color = taskbar.dft_back_color;
        taskbar.text_colors.begin()->second.label = taskbar.dft_text_colors;
    }

    FontInfo default_font;
    default_font.name = defaults.default_font_name.c_str();
    default_font.size = 9;
    ini.LoadFontData(_T("task_bar"), taskbar.font, default_font);

    //载入显示文本设置
    ini.LoadDisplayStr(L"task_bar", taskbar.disp_str, false);

    taskbar.tbar_wnd_on_left = ini.GetBool(_T("task_bar"), _T("task_bar_wnd_on_left"), false);
    taskbar.speed_short_mode = ini.GetBool(_T("task_bar"), _T("task_bar_speed_short_mode"), true);
    taskbar.tbar_wnd_snap = ini.GetBool(_T("task_bar"), _T("task_bar_wnd_snap"), false);
    taskbar.unit_byte = ini.GetBool(_T("task_bar"), _T("unit_byte"), true);
    taskbar.speed_unit = static_cast<SpeedUnit>(ini.GetInt(_T("task_bar"), _T("task_bar_speed_unit"), 0));
    taskbar.hide_unit = ini.GetBool(_T("task_bar"), _T("task_bar_hide_unit"), false);
    taskbar.hide_percent = ini.GetBool(_T("task_bar"), _T("task_bar_hide_percent"), false);
    taskbar.value_right_align = ini.GetBool(_T("task_bar"), _T("value_right_align"), true);
    taskbar.horizontal_arrange = ini.GetBool(_T("task_bar"), _T("horizontal_arrange"), false);
    taskbar.show_status_bar = ini.GetBool(_T("task_bar"), _T("show_status_bar"), true);
    taskbar.separate_value_unit_with_space = ini.GetBool(_T("task_bar"), _T("separate_value_unit_with_space"), true);
    taskbar.show_tool_tip = ini.GetBool(_T("task_bar"), _T("show_tool_tip"), true);
    taskbar.digits_number = ini.GetInt(_T("task_bar"), _T("digits_number"), 4);
    taskbar.memory_display = static_cast<MemoryDisplay>(ini.GetInt(L"task_bar", L"memory_display", static_cast<int>(MemoryDisplay::USAGE_PERCENTAGE)));
    taskbar.double_click_action = static_cast<DoubleClickAction>(ini.GetInt(_T("task_bar"), _T("double_click_action"), 0));
    taskbar.double_click_exe = ini.GetString(L"task_bar", L"double_click_exe", (defaults.system_dir + L"\\Taskmgr.exe").c_str());
    taskbar.cm_graph_type = ini.GetBool(_T("task_bar"), _T("cm_graph_type"), true);
    taskbar.show_graph_dashed_box = ini.GetBool(L"task_bar", L"show_graph_dashed_box", false);
    taskbar.item_space = ini.GetInt(L"task_bar", L"item_space", 8);
    taskbar.vertical_margin = ini.GetInt(L"task_bar", L"vertical_margin", 0);
    taskbar.window_offset_top = ini.GetInt(L"task_bar", L"window_offset_top", 0);
    taskbar.window_offset_left = ini.GetInt(L"task_bar", L"window_offset_left", 0);
    taskbar.avoid_overlap_with_widgets = ini.GetBool(_T("task_bar"), _T("avoid_overlap_with_widgets"), false);
    taskbar.taskbar_left_space_win11 = ini.GetInt(L"task_bar", L"taskbar_left_space_win11", 160);
    taskbar.taskbar_right_space_win11 = ini.GetInt(L"task_bar", L"taskbar_right_space_win11", 280);

    if (defaults.is_windows10_or_later)     //只有Win10才支持自动适应系统深色/浅色主题
        taskbar.auto_adapt_light_theme = ini.GetBool(L"task_bar", L"auto_adapt_light_theme", false);
    else
        taskbar.auto_adapt_light_theme = false;
    taskbar.dark_default_style = ini.GetInt(L"task_bar", L"dark_default_style", 0);
    taskbar.light_default_style = ini.GetInt(L"task_bar", L"light_default_style", TASKBAR_DEFAULT_LIGHT_STYLE_INDEX);

    if (defaults.is_windows8_or_later)
        taskbar.auto_set_background_color = ini.GetBool(L"task_bar", L"auto_set_background_color", false);
    else
        taskbar.auto_set_background_color = false;

    taskbar.item_order.Init();
    taskbar.item_order.FromString(ini.GetString(L"task_bar", L"item_order", L""));
    const std::wstring plugin_display_item = ini.GetString(L"task_bar", L"plugin_display_item", L"");
    if (plugin_display_item.size() <= kMaximumPluginDisplayItemConfigLength)
        taskbar.plugin_display_item.FromString(plugin_display_item);
    else
        taskbar.plugin_display_item.FromString(L"");
    taskbar.auto_save_taskbar_color_settings_to_preset = ini.GetBool(L"task_bar", L"auto_save_taskbar_color_settings_to_preset", true);

    taskbar.show_netspeed_figure = ini.GetBool(L"task_bar", L"show_netspeed_figure", true);
    taskbar.netspeed_figure_max_value = ini.GetInt(L"task_bar", L"netspeed_figure_max_value", 10);
    taskbar.netspeed_figure_max_value_unit = ini.GetInt(L"task_bar", L"netspeed_figure_max_value_unit", 1);
    taskbar.graph_color_following_system = ini.GetBool(L"task_bar", L"graph_color_following_system", true);

    if (defaults.d2d_supported)
        taskbar.disable_d2d = ini.GetBool(L"task_bar", L"disable_d2d", true);
    else
        taskbar.disable_d2d = true;
    taskbar.enable_colorful_emoji = ini.GetBool(L"task_bar", L"enable_colorful_emoji", true);

    //其他设置
    config.m_use_log_scale = ini.GetBool(_T("histroy_traffic"), _T("use_log_scale"), true);
    config.m_sunday_first = ini.GetBool(_T("histroy_traffic"), _T("sunday_first"), true);
    config.m_view_type = static_cast<HistoryTrafficViewType>(ini.GetInt(_T("histroy_traffic"), _T("view_type"), static_cast<int>(HistoryTrafficViewType::HV_DAY)));

    if (config.m_notify_icon_selected < 0 || config.m_notify_icon_selected >= MAX_NOTIFY_ICON)
        config.m_notify_icon_selected = defaults.is_windows7_or_8 ? 2 : defaults.default_notify_icon;

    const int history_view_type = static_cast<int>(config.m_view_type);
    if (IsOutOfRange(history_view_type, static_cast<int>(HistoryTrafficViewType::HV_DAY),
                     static_cast<int>(HistoryTrafficViewType::HV_YEAR)))
    {
        config.m_view_type = HistoryTrafficViewType::HV_DAY;
    }
}

bool ConfigStore::Save(const GeneralSettingData& general, const TaskBarSettingData& taskbar,
                       const MainConfigData& config, const OtherSettings& other, const wchar_t* version)
{
    CSettingsHelper ini{ m_path };

    //常规设置
    ini.WriteBool(_T("general"), _T("check_update_when_start"), general.check_update_when_start);
    ini.WriteString(_T("general"), _T("language"), general.language.toConfigString());
    ini.WriteInt(L"general", L"update_source", general.update_source);
    ini.WriteBool(L"general", L"show_all_interface", general.show_all_interface);
    ini.WriteInt(L"general", L"cpu_usage_acquire_method", general.cpu_usage_acquire_method);
    ini.WriteInt(L"general", L"monitor_time_span", general.monitor_time_span);
    ini.WriteString(L"general", L"hard_disk_name", general.hard_disk_name);
    ini.WriteString(L"general", L"cpu_core_name", general.cpu_core_name);
    ini.WriteInt(L"general", L"hardware_monitor_item", general.hardware_monitor_item);
    ini.WriteStringList(L"general", L"connections_hide", general.connections_hide.ToVector());

    //网络连接设置
    ini.WriteBool(L"config", L"show_notify_icon", general.show_notify_icon);
    ini.WriteBool(L"config", L"show_task_bar_wnd", config.m_show_task_bar_wnd);
    ini.WriteBool(L"connection", L"auto_select", config.m_auto_select);
    ini.WriteBool(L"connection", L"select_all", config.m_select_all);
    ini.WriteString(L"connection", L"connection_name", CCommon::StrToUnicode(config.m_connection_name.c_str()));

    ini.WriteInt(L"config", L"notify_icon_selected", config.m_notify_icon_selected);
    ini.WriteBool(L"config", L"notify_icon_auto_adapt", config.m_notify_icon_auto_adapt);

    ini.WriteBool(L"notify_tip", L"traffic_tip_enable", general.traffic_tip_enable);
    ini.WriteInt(L"notify_tip", L"traffic_tip_value", general.traffic_tip_value);
    ini.WriteInt(L"notify_tip", L"traffic_tip_unit", general.traffic_tip_unit);
    ini.WriteBool(L"notify_tip", L"memory_usage_tip_enable", general.memory_usage_tip.enable);
    ini.WriteInt(L"notify_tip", L"memory_tip_value", general.memory_usage_tip.tip_value);
    ini.WriteBool(L"notify_tip", L"cpu_temperature_tip_enable", general.cpu_temp_tip.enable);
    ini.WriteInt(L"notify_tip", L"cpu_temperature_tip_value", general.cpu_temp_tip.tip_value);
    ini.WriteBool(L"notify_tip", L"gpu_temperature_tip_enable", general.gpu_temp_tip.enable);
    ini.WriteInt(L"notify_tip", L"gpu_temperature_tip_value", general.gpu_temp_tip.tip_value);
    ini.WriteBool(L"notify_tip", L"hdd_temperature_tip_enable", general.hdd_temp_tip.enable);
    ini.WriteInt(L"notify_tip", L"hdd_temperature_tip_value", general.hdd_temp_tip.tip_value);
    ini.WriteBool(L"notify_tip", L"mainboard_temperature_tip_enable", general.mainboard_temp_tip.enable);
    ini.WriteInt(L"notify_tip", L"mainboard_temperature_tip_value", general.mainboard_temp_tip.tip_value);

    //任务栏窗口设置
    ini.WriteInt(L"task_bar", L"task_bar_back_color", taskbar.back_color);
    ini.WriteInt(L"task_bar", L"transparent_color", taskbar.transparent_color);
    ini.WriteInt(L"task_bar", L"status_bar_color", taskbar.status_bar_color);
    ini.SaveTaskbarWndColors(L"task_bar", L"task_bar_text_color", taskbar.text_colors);
    ini.WriteBool(L"task_bar", L"specify_each_item_color", taskbar.specify_each_item_color);
    ini.WriteInt(L"task_bar", L"tbar_display_item", taskbar.display_item.ToInt());
    ini.SaveFontData(L"task_bar", taskbar.font);
    ini.WriteBool(L"task_bar", L"show_taskbar_wnd_in_secondary_display", taskbar.show_taskbar_wnd_in_secondary_display);
    ini.WriteInt(L"task_bar", L"secondary_display_index", taskbar.secondary_display_index);

    ini.SaveDisplayStr(L"task_bar", taskbar.disp_str);

    ini.WriteBool(L"task_bar", L"task_bar_wnd_on_left", taskbar.tbar_wnd_on_left);
    ini.WriteBool(L"task_bar", L"task_bar_wnd_snap", taskbar.tbar_wnd_snap);
    ini.WriteBool(L"task_bar", L"task_bar_speed_short_mode", taskbar.speed_short_mode);
    ini.WriteBool(L"task_bar", L"unit_byte", taskbar.unit_byte);
    ini.WriteInt(L"task_bar", L"task_bar_speed_unit", static_cast<int>(taskbar.speed_unit));
    ini.WriteBool(L"task_bar", L"task_bar_hide_unit", taskbar.hide_unit);
    ini.WriteBool(L"task_bar", L"task_bar_hide_percent", taskbar.hide_percent);
    ini.WriteBool(L"task_bar", L"value_right_align", taskbar.value_right_align);
    ini.WriteBool(L"task_bar", L"horizontal_arrange", taskbar.horizontal_arrange);
    ini.WriteBool(L"task_bar", L"show_status_bar", taskbar.show_status_bar);
    ini.WriteBool(L"task_bar", L"separate_value_unit_with_space", taskbar.separate_value_unit_with_space);
    ini.WriteBool(L"task_bar", L"show_tool_tip", taskbar.show_tool_tip);
    ini.WriteInt(L"task_bar", L"digits_number", taskbar.digits_number);
    ini.WriteInt(L"task_bar", L"memory_display", static_cast<int>(taskbar.memory_display));
    ini.WriteInt(L"task_bar", L"double_click_action", static_cast<int>(taskbar.double_click_action));
    ini.WriteString(L"task_bar", L"double_click_exe", taskbar.double_click_exe);
    ini.WriteBool(L"task_bar", L"cm_graph_type", taskbar.cm_graph_type);
    ini.WriteBool(L"task_bar", L"show_graph_dashed_box", taskbar.show_graph_dashed_box);
    ini.WriteInt(L"task_bar", L"item_space", taskbar.item_space);
    ini.WriteInt(L"task_bar", L"vertical_margin", taskbar.vertical_margin);
    ini.WriteInt(L"task_bar", L"window_offset_top", taskbar.window_offset_top);
    ini.WriteInt(L"task_bar", L"window_offset_left", taskbar.window_offset_left);
    ini.WriteBool(L"task_bar", L"avoid_overlap_with_widgets", taskbar.avoid_overlap_with_widgets);
    ini.WriteInt(L"task_bar", L"taskbar_left_space_win11", taskbar.taskbar_left_space_win11);
    ini.WriteInt(L"task_bar", L"taskbar_right_space_win11", taskbar.taskbar_right_space_win11);

    ini.WriteBool(L"task_bar", L"auto_adapt_light_theme", taskbar.auto_adapt_light_theme);
    ini.WriteInt(L"task_bar", L"dark_default_style", taskbar.dark_default_style);
    ini.WriteInt(L"task_bar", L"light_default_style", taskbar.light_default_style);
    ini.WriteBool(L"task_bar", L"auto_set_background_color", taskbar.auto_set_background_color);

    ini.WriteString(L"task_bar", L"item_order", taskbar.item_order.ToString());
    ini.WriteString(L"task_bar", L"plugin_display_item", taskbar.plugin_display_item.ToString());
    ini.WriteBool(L"task_bar", L"auto_save_taskbar_color_settings_to_preset", taskbar.auto_save_taskbar_color_settings_to_preset);

    ini.WriteBool(L"task_bar", L"show_netspeed_figure", taskbar.show_netspeed_figure);
    ini.WriteInt(L"task_bar", L"netspeed_figure_max_value", taskbar.netspeed_figure_max_value);
    ini.WriteInt(L"task_bar", L"netspeed_figure_max_value_unit", taskbar.netspeed_figure_max_value_unit);
    ini.WriteBool(L"task_bar", L"graph_color_following_system", taskbar.graph_color_following_system);

    ini.WriteBool(L"task_bar", L"disable_d2d", taskbar.disable_d2d);
    ini.WriteBool(L"task_bar", L"enable_colorful_emoji", taskbar.enable_colorful_emoji);

    //其他设置
    ini.WriteBool(L"histroy_traffic", L"use_log_scale", config.m_use_log_scale);
    ini.WriteBool(L"histroy_traffic", L"sunday_first", config.m_sunday_first);
    ini.WriteInt(L"histroy_traffic", L"view_type", static_cast<int>(config.m_view_type));

    ini.WriteBool(_T("other"), _T("no_multistart_warning"), other.no_multistart_warning);
    ini.WriteBool(_T("other"), _T("exit_when_start_by_restart_manager"), other.exit_when_start_by_restart_manager);
    ini.WriteBool(_T("other"), _T("debug_log"), other.debug_log);
    ini.WriteInt(_T("other"), _T("notify_interval"), other.notify_interval);
    ini.WriteBool(_T("other"), _T("taksbar_transparent_color_enable"), other.taksbar_transparent_color_enable);
    ini.WriteBool(_T("other"), _T("last_light_mode"), other.last_light_mode);
    ini.WriteBool(_T("other"), _T("show_dot_net_notinstalled_tip"), other.show_dot_net_notinstalled_tip);

    ini.WriteString(L"app", L"version", version);

    return ini.Save();
}

void ConfigStore::LoadOther(OtherSettings& other, const EnvironmentDefaults& defaults)
{
    CSettingsHelper ini{ m_path };
    other.no_multistart_warning = ini.GetBool(_T("other"), _T("no_multistart_warning"), false);
    other.exit_when_start_by_restart_manager = ini.GetBool(_T("other"), _T("exit_when_start_by_restart_manager"), true);
    other.debug_log = ini.GetBool(_T("other"), _T("debug_log"), false);
    other.notify_interval = ini.GetInt(_T("other"), _T("notify_interval"), 60);
    ResetIfOutOfRange(other.notify_interval, 0, kMaximumNotifyInterval, 60);

    //由于Win7系统中设置任务栏窗口透明色会导致任务栏窗口不可见，因此默认在Win7中禁用透明色的设定
    other.taksbar_transparent_color_enable = ini.GetBool(L"other", L"taksbar_transparent_color_enable", !defaults.is_windows7);
    other.last_light_mode = ini.GetBool(L"other", L"last_light_mode", defaults.default_light_theme);
    other.show_dot_net_notinstalled_tip = ini.GetBool(L"other", L"show_dot_net_notinstalled_tip", true);
}

bool ConfigStore::SaveOther(const OtherSettings& other)
{
    CSettingsHelper ini{ m_path };
    ini.WriteBool(_T("other"), _T("no_multistart_warning"), other.no_multistart_warning);
    ini.WriteBool(_T("other"), _T("exit_when_start_by_restart_manager"), other.exit_when_start_by_restart_manager);
    ini.WriteBool(_T("other"), _T("debug_log"), other.debug_log);
    ini.WriteInt(_T("other"), _T("notify_interval"), other.notify_interval);
    ini.WriteBool(_T("other"), _T("taksbar_transparent_color_enable"), other.taksbar_transparent_color_enable);
    ini.WriteBool(_T("other"), _T("last_light_mode"), other.last_light_mode);
    ini.WriteBool(_T("other"), _T("show_dot_net_notinstalled_tip"), other.show_dot_net_notinstalled_tip);
    return ini.Save();
}

void ConfigStore::LoadLanguage(LanguageInfo& language)
{
    CIniHelper ini{ m_path };
    language.fromConfigString(ini.GetString(_T("general"), _T("language"), L""));
}

void ConfigStore::Validate(GeneralSettingData& general, TaskBarSettingData& taskbar)
{
    //监控时间间隔钳制
    if (general.monitor_time_span < MONITOR_TIME_SPAN_MIN || general.monitor_time_span > MONITOR_TIME_SPAN_MAX)
        general.monitor_time_span = 1000;

    // TaskbarMon has no project-owned signed update channel yet. Clear the
    // legacy opt-in so imported TrafficMonitor settings cannot re-enable it.
    general.check_update_when_start = false;
    ResetIfOutOfRange(general.update_source, 0, 1, 0);
    if (general.cpu_usage_acquire_method != GeneralSettingData::CA_CPU_TIME &&
        general.cpu_usage_acquire_method != GeneralSettingData::CA_PDH)
    {
        general.cpu_usage_acquire_method = GeneralSettingData::CA_PDH;
    }
    general.hardware_monitor_item &= static_cast<unsigned int>(HI_CPU | HI_GPU | HI_HDD | HI_MBD);
#ifdef WITHOUT_TEMPERATURE
    // Shipped builds do not load the third-party hardware monitor. Clear
    // imported temperature settings instead of retaining stale state.
    general.hardware_monitor_item = 0;
    general.cpu_temp_tip.enable = false;
    general.gpu_temp_tip.enable = false;
    general.hdd_temp_tip.enable = false;
    general.mainboard_temp_tip.enable = false;
#endif
    LimitStringLength(general.hard_disk_name, kMaximumDeviceNameLength);
    LimitStringLength(general.cpu_core_name, kMaximumDeviceNameLength);

    ResetIfOutOfRange(general.traffic_tip_value, 1, 32767, 200);
    ResetIfOutOfRange(general.traffic_tip_unit, 0, 1, 0);
    ResetIfOutOfRange(general.memory_usage_tip.tip_value, 1, 100, 80);
    ResetIfOutOfRange(general.cpu_temp_tip.tip_value, 1, 120, 80);
    ResetIfOutOfRange(general.gpu_temp_tip.tip_value, 1, 120, 80);
    ResetIfOutOfRange(general.hdd_temp_tip.tip_value, 1, 120, 80);
    ResetIfOutOfRange(general.mainboard_temp_tip.tip_value, 1, 120, 80);

    if (taskbar.font.size < MIN_FONT_SIZE || taskbar.font.size > MAX_FONT_SIZE)
        taskbar.font.size = 9;
    if (taskbar.font.name.GetLength() >= LF_FACESIZE)
        taskbar.font.name = taskbar.font.name.Left(LF_FACESIZE - 1);
    LimitStringLength(taskbar.double_click_exe, kMaximumDoubleClickExecutableLength);

    if (taskbar.speed_unit < SpeedUnit::AUTO || taskbar.speed_unit > SpeedUnit::MBPS)
        taskbar.speed_unit = SpeedUnit::AUTO;
    if (taskbar.memory_display < MemoryDisplay::USAGE_PERCENTAGE ||
        taskbar.memory_display > MemoryDisplay::MEMORY_AVAILABLE)
    {
        taskbar.memory_display = MemoryDisplay::USAGE_PERCENTAGE;
    }
    if (taskbar.double_click_action < DoubleClickAction::CONNECTION_INFO ||
        taskbar.double_click_action > DoubleClickAction::NONE)
    {
        taskbar.double_click_action = DoubleClickAction::CONNECTION_INFO;
    }
    ResetIfOutOfRange(taskbar.digits_number, 3, 7, 4);

    //任务栏窗口布局参数校验
    taskbar.ValidItemSpace();
    taskbar.ValidVerticalMargin();
    taskbar.ValidWindowOffsetTop();
    if (taskbar.secondary_display_index < 0)
        taskbar.secondary_display_index = 0;
    ResetIfOutOfRange(taskbar.taskbar_left_space_win11, 0, 300, 160);
    ResetIfOutOfRange(taskbar.taskbar_right_space_win11, 0, 300, 280);
    ResetIfOutOfRange(taskbar.dark_default_style, 0, TASKBAR_DEFAULT_STYLE_NUM - 1, 0);
    ResetIfOutOfRange(taskbar.light_default_style, 0, TASKBAR_DEFAULT_STYLE_NUM - 1,
                      TASKBAR_DEFAULT_LIGHT_STYLE_INDEX);


    // Keep INI-controlled graph settings within the ranges accepted by the UI.
    if (taskbar.netspeed_figure_max_value < 1 || taskbar.netspeed_figure_max_value > 1024)
        taskbar.netspeed_figure_max_value = 10;
    if (taskbar.netspeed_figure_max_value_unit < 0 || taskbar.netspeed_figure_max_value_unit > 1)
        taskbar.netspeed_figure_max_value_unit = 1;
    taskbar.ValidWindowOffsetLeft();

    //不含温度监控的版本，不显示温度监控相关项目
#ifdef WITHOUT_TEMPERATURE
    taskbar.display_item.Remove(TDI_CPU_TEMP);
    taskbar.display_item.Remove(TDI_GPU_TEMP);
    taskbar.display_item.Remove(TDI_HDD_TEMP);
    taskbar.display_item.Remove(TDI_MAIN_BOARD_TEMP);
#endif

    //如果选项设置中关闭了某个硬件监控，则不显示对应的温度监控相关项目
    if (!general.IsHardwareEnable(HI_CPU))
        taskbar.display_item.Remove(TDI_CPU_TEMP);
    if (!general.IsHardwareEnable(HI_GPU))
        taskbar.display_item.Remove(TDI_GPU_TEMP);
    if (!general.IsHardwareEnable(HI_HDD))
        taskbar.display_item.Remove(TDI_HDD_TEMP);
    if (!general.IsHardwareEnable(HI_MBD))
        taskbar.display_item.Remove(TDI_MAIN_BOARD_TEMP);
}
