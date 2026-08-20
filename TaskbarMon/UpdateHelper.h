#pragma once
class CUpdateHelper
{
public:
    CUpdateHelper();
    ~CUpdateHelper();

    enum class UpdateSource
    {
        GitHubSource,
        GiteeSource
    };

    void SetUpdateSource(UpdateSource update_source);

    // TaskbarMon does not currently publish a project-owned, signed update
    // manifest. Keep this legacy API so existing configuration remains
    // readable, but never query the upstream TrafficMonitor feed.
    bool IsUpdateCheckSupported() const noexcept;
    bool CheckForUpdate();

    const std::wstring& GetVersion() const;
    const std::wstring& GetLink() const;
    const std::wstring& GetLink64() const;
    const std::wstring& GetLinkArm64ec() const;
    const std::wstring& GetContentsEn() const;
    const std::wstring& GetContentsZhCn() const;
    const std::wstring& GetContentsZhTw() const;
    bool IsRowData();

private:
    void ClearUpdateInfo() noexcept;

private:
    std::wstring m_version;
    std::wstring m_link;
    std::wstring m_link64;
    std::wstring m_link_arm64ec;
    std::wstring m_contents_en;
    std::wstring m_contents_zh_cn;
    std::wstring m_contents_zh_tw;
    bool m_row_data{ true };
    // Retained for configuration compatibility only. It is intentionally not
    // used until TaskbarMon has an authenticated update channel of its own.
    UpdateSource m_update_source{ UpdateSource::GitHubSource };
};
