// ConfigStore.h : 配置持久化（core 层）
// 职责：config.ini 的类型化读写与校验
#pragma once
#include <string>
#include "CommonData.h"

// 配置读写
class ConfigStore
{
public:
    explicit ConfigStore(const std::wstring& path);

    // 与运行环境相关的默认值（由 UI 层提供）
    struct EnvironmentDefaults
    {
        std::wstring default_font_name;     // 当前语言的默认字体
        std::wstring system_dir;            // 系统目录（双击动作默认程序路径）
        int default_notify_icon{ 0 };       // 系统相关的通知图标默认值（Win7/8 为 2）
        int update_source_default{ 0 };     // 更新源默认值（简体中文默认 1=Gitee）
        bool is_windows7_or_8{};
        bool is_windows8_or_later{ true };
        bool is_windows10_or_later{ true };
        bool is_windows7{};
        bool default_light_theme{};         // 系统当前是否为浅色主题
        bool d2d_supported{ true };         // 是否支持 D2D 渲染
    };

    // 其他杂项设置（App 私有成员）
    struct OtherSettings
    {
        bool no_multistart_warning{};
        bool exit_when_start_by_restart_manager{ true };
        bool debug_log{};
        int notify_interval{ 60 };
        bool taksbar_transparent_color_enable{ true };
        bool last_light_mode{};
        bool show_dot_net_notinstalled_tip{ true };
    };

    // 读取配置
    void Load(GeneralSettingData& general, TaskBarSettingData& taskbar, MainConfigData& config,
              const EnvironmentDefaults& defaults);

    // 保存配置
    // version: 当前程序版本，写入 [app]/version
    // 返回是否保存成功
    bool Save(const GeneralSettingData& general, const TaskBarSettingData& taskbar,
              const MainConfigData& config, const OtherSettings& other, const wchar_t* version);

    // 读取杂项设置
    void LoadOther(OtherSettings& other, const EnvironmentDefaults& defaults);
    // 保存杂项设置
    bool SaveOther(const OtherSettings& other);

    // 读取语言设置（独立于 Load，需在语言初始化前调用）
    void LoadLanguage(LanguageInfo& language);

    // 校验并修正配置值（读取后调用）
    static void Validate(GeneralSettingData& general, TaskBarSettingData& taskbar);

private:
    std::wstring m_path;
};
