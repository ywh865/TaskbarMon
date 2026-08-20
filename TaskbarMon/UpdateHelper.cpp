#include "stdafx.h"
#include "UpdateHelper.h"


CUpdateHelper::CUpdateHelper()
{
}


CUpdateHelper::~CUpdateHelper()
{
}

void CUpdateHelper::SetUpdateSource(UpdateSource update_source)
{
    m_update_source = update_source;
}

bool CUpdateHelper::IsUpdateCheckSupported() const noexcept
{
    return false;
}

bool CUpdateHelper::CheckForUpdate()
{
    // The old implementation consumed unsigned metadata from the upstream
    // TrafficMonitor repository. That metadata is not authoritative for this
    // fork, so update checks fail closed until TaskbarMon publishes a signed
    // manifest and hashes for its own release artifacts.
    ClearUpdateInfo();
    m_row_data = true;
    return false;
}

void CUpdateHelper::ClearUpdateInfo() noexcept
{
    m_version.clear();
    m_link.clear();
    m_link64.clear();
    m_link_arm64ec.clear();
    m_contents_en.clear();
    m_contents_zh_cn.clear();
    m_contents_zh_tw.clear();
}

const std::wstring& CUpdateHelper::GetVersion() const
{
    return m_version;
}

const std::wstring& CUpdateHelper::GetLink() const
{
    return m_link;
}

const std::wstring& CUpdateHelper::GetLink64() const
{
    return m_link64;
}

const std::wstring& CUpdateHelper::GetLinkArm64ec() const
{
    return m_link_arm64ec;
}

const std::wstring& CUpdateHelper::GetContentsEn() const
{
    return m_contents_en;
}

const std::wstring& CUpdateHelper::GetContentsZhCn() const
{
    return m_contents_zh_cn;
}

const std::wstring& CUpdateHelper::GetContentsZhTw() const
{
    return m_contents_zh_tw;
}

bool CUpdateHelper::IsRowData()
{
    return m_row_data;
}
