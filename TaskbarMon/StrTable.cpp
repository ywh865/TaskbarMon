#include "stdafx.h"
#include "StrTable.h"
#include "Common.h"
#include "IniHelper.h"
#include "TaskbarMon.h"

namespace
{
    constexpr size_t kMaxExternalLanguageFiles = 32;
    constexpr size_t kMaxLanguageListEntries = 128;
    constexpr size_t kMaxLanguageDisplayNameLength = 128;
    constexpr size_t kMaxLanguageBcp47Length = 128;
    constexpr size_t kMaxLanguageFontNameLength = 128;
    constexpr size_t kMaxLanguageTranslatorLength = 256;
    constexpr size_t kMaxLanguageTranslatorUrlLength = 2048;

    bool IsValidExternalLanguageInfo(const LanguageInfo& language_info)
    {
        return !language_info.bcp_47.empty()
            && language_info.display_name.size() <= kMaxLanguageDisplayNameLength
            && language_info.bcp_47.size() <= kMaxLanguageBcp47Length
            && language_info.default_font_name.size() <= kMaxLanguageFontNameLength
            && language_info.translator.size() <= kMaxLanguageTranslatorLength
            && language_info.translator_url.size() <= kMaxLanguageTranslatorUrlLength;
    }
}

CStrTable::CStrTable()
{
}

CStrTable::~CStrTable()
{
}

static void LanguageInfoFromIni(LanguageInfo& language_info, const CIniHelper& ini)
{
    language_info.display_name = ini.GetString(L"general", L"DISPLAY_NAME", L"");
    language_info.bcp_47 = ini.GetString(L"general", L"BCP_47", L"");
    language_info.default_font_name = ini.GetString(L"general", L"DEFAULT_FONT", L"Microsoft Sans Serif");
    language_info.translator = ini.GetString(L"general", L"TRANSLATOR", L"<Unknown>");
    language_info.translator_url = ini.GetString(L"general", L"TRANSLATOR_URL", L"");
}

// 回调函数，用于枚举资源语言
static BOOL CALLBACK EnumResLangProc(HMODULE hModule, LPCTSTR lpType, LPCTSTR lpName, WORD wIDLanguage, LONG_PTR lParam)
{
    std::vector<LanguageInfo>* pLanguages = reinterpret_cast<std::vector<LanguageInfo>*>(lParam);

    // 获取资源句柄
    HRSRC hRes = FindResourceEx(hModule, lpType, lpName, wIDLanguage);
    if (hRes != NULL)
    {
        // 加载资源
        HGLOBAL hGlobal = LoadResource(hModule, hRes);
        if (hGlobal != NULL)
        {
            // 锁定资源并获取数据指针
            LPVOID pResourceData = LockResource(hGlobal);
            if (pResourceData != NULL)
            {
                // 获取资源大小
                DWORD resSize = SizeofResource(hModule, hRes);

                // 将资源内容转换为字符串
                std::string strData((const char*)pResourceData, resSize);
                std::wstring resData = CCommon::StrToUnicode(strData.c_str(), true).c_str();

                CIniHelper ini;
                ini.FromDirectString(resData);
                LanguageInfo lanugage_info;
                LanguageInfoFromIni(lanugage_info, ini);
                lanugage_info.language_id = wIDLanguage;
                pLanguages->push_back(lanugage_info);
            }
        }
    }

    return TRUE; // 继续枚举
}


void CStrTable::Init()
{
    // 获取 IDR_LANGUAGE 资源的所有语言版本
    EnumResourceLanguages(NULL, _T("TEXT"), MAKEINTRESOURCE(IDR_LANGUAGE), EnumResLangProc, reinterpret_cast<LONG_PTR>(&m_language_list));
    //按bcp47排序
    std::sort(m_language_list.begin(), m_language_list.end(), [](const LanguageInfo& a, const LanguageInfo& b) {
        return a.bcp_47 < b.bcp_47;
    });

    //先读取默认字符串资源，如果当前语言中没有对应字符串，则会使用默认的字符串资源
    CIniHelper ini_default(IDR_LANGUAGE_DEFAULT);
    ReadStringtableFronIni(ini_default);

    //读取字符串资源
    CIniHelper ini(IDR_LANGUAGE);
    ReadStringtableFronIni(ini);
    LanguageInfoFromIni(m_language_info, ini);

    //从外部language文件夹获取语言文件
    vector<wstring> files;
    std::wstring language_dir;
#ifdef _DEBUG
    language_dir = L".\\language";
#else
    language_dir = theApp.m_module_dir + L"language";
#endif
    CCommon::GetFiles((language_dir + L"\\*.ini").c_str(), files, kMaxExternalLanguageFiles);
    std::set<wstring> language_codes;
    for (const auto& language_info : m_language_list)
        language_codes.insert(language_info.bcp_47);

    bool external_current_language_loaded{};
    for (const wstring& file_name : files)
    {
        std::wstring file_path{ language_dir + file_name };
        CIniHelper ini_file(file_path, true);
        LanguageInfo language_info;
        LanguageInfoFromIni(language_info, ini_file);
        if (!IsValidExternalLanguageInfo(language_info))
            continue;

        const LCID locale_id = LocaleNameToLCID(language_info.bcp_47.c_str(), 0);
        if (locale_id == 0)
            continue;
        language_info.language_id = LANGIDFROMLCID(locale_id);  //根据语言bcp-47代码获取语言id
        //从外部语言文件读取到当前语言，先从外部语言文件加载
        if (!external_current_language_loaded && language_info == theApp.m_general_data.language)
        {
            m_language_info = language_info;
            ReadStringtableFronIni(ini_file);
            external_current_language_loaded = true;
        }

        //只按 BCP-47 代码保留一项，避免重复的外部文件使去重退化为 O(n²)。
        if (language_codes.insert(language_info.bcp_47).second && m_language_list.size() < kMaxLanguageListEntries)
            m_language_list.push_back(language_info);
    }
}

const wstring& CStrTable::LoadText(const wstring& key) const
{
    const auto& text_string_table = GetTextStringTable();
    return LoadText(key, text_string_table);
}

wstring CStrTable::LoadTextFormat(const wstring& key, const std::initializer_list<CVariant>& paras) const
{
    const auto& text_string_table = GetTextStringTable();
    return LoadTextFormat(key, text_string_table, paras);
}

const wstring& CStrTable::LoadMenuText(const wstring& key) const
{
    const auto& menu_string_table = GetMenuStringTable();
    return LoadText(key, menu_string_table);
}

const wstring& CStrTable::LoadText(const wstring& key, const wstring& section)
{
    auto iter = m_string_table.find(section);
    if (iter != m_string_table.end())
    {
        return LoadText(key, iter->second);
    }
    static std::wstring str_empty;
    return str_empty;
}

void CStrTable::ReadStringtableFronIni(const CIniHelper& ini)
{
    // Language files only define these sections.  Enumerating arbitrary
    // section names lets a malformed external INI turn this into repeated
    // whole-file scans and unbounded string-table growth.
    for (const wchar_t* section : { L"general", L"text", L"menu" })
    {
        auto& value_map = m_string_table[section];
        ini.GetAllKeyValues(section, value_map);
    }
}

const std::map<std::wstring, std::wstring>& CStrTable::GetTextStringTable() const
{
    auto iter = m_string_table.find(L"text");
    if (iter != m_string_table.end())
        return iter->second;
    static std::map<std::wstring, std::wstring> empty_map;
    return empty_map;
}

const std::map<std::wstring, std::wstring>& CStrTable::GetMenuStringTable() const
{
    auto iter = m_string_table.find(L"menu");
    if (iter != m_string_table.end())
        return iter->second;
    static std::map<std::wstring, std::wstring> empty_map;
    return empty_map;
}

const wstring& CStrTable::LoadText(const wstring& key, const std::map<std::wstring, std::wstring>& string_table)
{
    auto iter = string_table.find(key);
    if (iter != string_table.end())
        return iter->second;
    ASSERT(false);
    static std::wstring str_empty;
    return str_empty;
}

wstring CStrTable::LoadTextFormat(const wstring& key, const std::map<std::wstring, std::wstring>& string_table, const std::initializer_list<CVariant>& paras)
{
    auto iter = string_table.find(key);
    if (iter == string_table.end())
    {
        ASSERT(false);
        return std::wstring();
    }
    wstring str{ iter->second };    // 复制以避免原始字符串修改
    int index{ 1 };
    for (const auto& para : paras)
    {
        wstring format_str{ L"<%" + std::to_wstring(index) + L"%>" };
        CCommon::StringReplace(str, format_str, para.ToString().GetString());
        ++index;
    }
    return str;
}
