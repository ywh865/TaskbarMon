#include "stdafx.h"
#include "IniHelper.h"
#include "Common.h"

#include <limits>

namespace
{
    constexpr std::streamoff kMaxIniFileSize = 1024 * 1024;
    constexpr size_t kMaxIniStringListItems = 256;
    constexpr size_t kMaxIniStringListItemLength = 256;
    constexpr size_t kMaxIniSections = 128;
    constexpr size_t kMaxIniSectionNameLength = 128;
    constexpr size_t kMaxIniKeyValuesPerSection = 1024;
    constexpr size_t kMaxIniLinesPerSection = 4096;
    constexpr size_t kMaxIniKeyLength = 256;
    constexpr size_t kMaxIniValueLength = 4096;
    constexpr size_t kUtf8BomLength = 3;

    bool IsMissingFileError(DWORD error)
    {
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }

    bool TryDecodeIniText(const string& encoded, bool utf8, wstring& decoded)
    {
        if (encoded.empty())
        {
            decoded.clear();
            return true;
        }

        if (encoded.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
            return false;

        const UINT code_page = utf8 ? CP_UTF8 : CP_ACP;
        const DWORD flags = utf8 ? MB_ERR_INVALID_CHARS : 0;
        const int source_length = static_cast<int>(encoded.size());
        const int decoded_length = ::MultiByteToWideChar(code_page, flags, encoded.data(), source_length, nullptr, 0);
        if (decoded_length <= 0)
            return false;

        wstring converted(static_cast<size_t>(decoded_length), L'\0');
        if (::MultiByteToWideChar(code_page, flags, encoded.data(), source_length, &converted[0], decoded_length) != decoded_length)
            return false;

        decoded.swap(converted);
        return true;
    }

    bool TryEncodeIniText(const wstring& decoded, bool utf8, string& encoded)
    {
        if (decoded.empty())
        {
            encoded.clear();
            return true;
        }

        if (decoded.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
            return false;

        const UINT code_page = utf8 ? CP_UTF8 : CP_ACP;
        const DWORD flags = utf8 ? WC_ERR_INVALID_CHARS : 0;
        const int source_length = static_cast<int>(decoded.size());
        const int encoded_length = ::WideCharToMultiByte(code_page, flags, decoded.data(), source_length, nullptr, 0, nullptr, nullptr);
        if (encoded_length <= 0)
            return false;

        string converted(static_cast<size_t>(encoded_length), '\0');
        if (::WideCharToMultiByte(code_page, flags, decoded.data(), source_length, &converted[0], encoded_length, nullptr, nullptr) != encoded_length)
            return false;

        encoded.swap(converted);
        return true;
    }
}

CIniHelper::CIniHelper(const wstring& file_path, bool force_utf8)
{
    m_file_path = file_path;
    if (m_file_path.empty())
    {
        m_load_failed = true;
        return;
    }

    const DWORD attributes = ::GetFileAttributesW(m_file_path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        if (IsMissingFileError(::GetLastError()))
            return; // A missing file is the only case in which Save may create one.
        m_load_failed = true;
        return;
    }
    if ((attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
    {
        // Configuration is written back atomically. Refuse a directory,
        // device, or reparse point so a user-controlled link cannot redirect
        // that replacement outside the application data directory.
        m_load_failed = true;
        return;
    }

    try
    {
        ifstream file_stream{ m_file_path, std::ios::binary | std::ios::ate };
        if (!file_stream.is_open())
        {
            m_load_failed = true;
            return;
        }

        const std::streampos end_position = file_stream.tellg();
        if (end_position == std::streampos(-1))
        {
            m_load_failed = true;
            return;
        }
        const std::streamoff file_size = static_cast<std::streamoff>(end_position);
        if (file_size < 0 || file_size > kMaxIniFileSize)
        {
            m_load_failed = true;
            return;
        }

        file_stream.seekg(0, std::ios::beg);
        if (!file_stream)
        {
            m_load_failed = true;
            return;
        }

        string ini_str(static_cast<size_t>(file_size), '\0');
        if (file_size > 0)
        {
            file_stream.read(&ini_str[0], static_cast<std::streamsize>(file_size));
            if (file_stream.gcount() != static_cast<std::streamsize>(file_size))
            {
                m_load_failed = true;
                return;
            }
        }

        // INI text cannot contain embedded NUL characters.  Reject it instead
        // of parsing a different prefix than the persisted bytes represent.
        if (ini_str.find('\0') != string::npos)
        {
            m_load_failed = true;
            return;
        }

        if (!ini_str.empty() && ini_str.back() != '\n')
            ini_str.push_back('\n');

        const bool has_utf8_bom = ini_str.size() >= 3 &&
            static_cast<unsigned char>(ini_str[0]) == 0xEF &&
            static_cast<unsigned char>(ini_str[1]) == 0xBB &&
            static_cast<unsigned char>(ini_str[2]) == 0xBF;
        const bool is_utf8 = force_utf8 || has_utf8_bom;
        if (has_utf8_bom)
        {
            ini_str = ini_str.substr(3);
        }

        wstring decoded;
        if (!TryDecodeIniText(ini_str, is_utf8, decoded))
        {
            m_load_failed = true;
            return;
        }

        m_ini_str.swap(decoded);
        m_file_existed_at_load = true;
    }
    catch (...)
    {
        m_ini_str.clear();
        m_load_failed = true;
    }
}

CIniHelper::CIniHelper(UINT id, bool is_utf8)
{
    m_ini_str = CCommon::GetTextResource(id, is_utf8 ? 1 : 0);
}

CIniHelper::CIniHelper()
{
}


CIniHelper::~CIniHelper()
{
}

void CIniHelper::FromDirectString(const wstring& str_content)
{
    m_ini_str = str_content;
}

void CIniHelper::SetSaveAsUTF8(bool utf8)
{
    m_save_as_utf8 = utf8;
}

void CIniHelper::WriteString(const wchar_t * AppName, const wchar_t * KeyName, const wstring& str)
{
    wstring write_str{ str };
    if (!write_str.empty() && (write_str[0] == L' ' || write_str.back() == L' '))       //如果字符串前后含有空格，则在字符串前后添加引号
    {
        write_str = DEF_CH + write_str;
        write_str.push_back(DEF_CH);
    }
    _WriteString(AppName, KeyName, write_str);
}

wstring CIniHelper::GetString(const wchar_t * AppName, const wchar_t * KeyName, const wchar_t* default_str) const
{
    wstring rtn{ default_str };
    GetString(AppName, KeyName, rtn);
    return rtn;
}

bool CIniHelper::GetString(const wchar_t* AppName, const wchar_t* KeyName, wstring& str) const
{
    bool rtn = _GetString(AppName, KeyName, str);
    //如果读取的字符串前后有指定的字符，则删除它
    if (!str.empty() && (str.front() == L'$' || str.front() == DEF_CH))
        str = str.substr(1);
    if (!str.empty() && (str.back() == L'$' || str.back() == DEF_CH))
        str.pop_back();
    return rtn;
}

void CIniHelper::WriteInt(const wchar_t * AppName, const wchar_t * KeyName, int value)
{
    _WriteString(AppName, KeyName, std::to_wstring(value));
}

int CIniHelper::GetInt(const wchar_t * AppName, const wchar_t * KeyName, int default_value) const
{
    wstring rtn{ std::to_wstring(default_value) };
    _GetString(AppName, KeyName, rtn);
    return _ttoi(rtn.c_str());
}

void CIniHelper::WriteBool(const wchar_t * AppName, const wchar_t * KeyName, bool value)
{
    if(value)
        _WriteString(AppName, KeyName, wstring(L"true"));
    else
        _WriteString(AppName, KeyName, wstring(L"false"));
}

bool CIniHelper::GetBool(const wchar_t * AppName, const wchar_t * KeyName, bool default_value) const
{
    wstring rtn{ default_value ? L"true" : L"false" };
    _GetString(AppName, KeyName, rtn);
    if (rtn == L"true")
        return true;
    else if (rtn == L"false")
        return false;
    else
        return (_ttoi(rtn.c_str()) != 0);
}

void CIniHelper::WriteIntArray(const wchar_t * AppName, const wchar_t * KeyName, const int * values, int size)
{
    CString str, tmp;
    for (int i{}; i < size; i++)
    {
        tmp.Format(_T("%d,"), values[i]);
        str += tmp;
    }
    _WriteString(AppName, KeyName, wstring(str));
}

void CIniHelper::GetIntArray(const wchar_t * AppName, const wchar_t * KeyName, int * values, int size, int default_value) const
{
    CString default_str;
    default_str.Format(_T("%d"), default_value);
    wstring str{ default_str.GetString() };
    _GetString(AppName, KeyName, str);
    std::vector<wstring> split_result;
    CCommon::StringSplit(str, L',', split_result);
    for (int i = 0; i < size; i++)
    {
        if (static_cast<size_t>(i) < split_result.size())
            values[i] = _wtoi(split_result[i].c_str());
        else if (i > 0)
            values[i] = values[i - 1];
        else
            values[i] = default_value;
    }
}

void CIniHelper::WriteBoolArray(const wchar_t * AppName, const wchar_t * KeyName, const bool * values, int size)
{
    int value{};
    for (int i{}; i < size; i++)
    {
        if (values[i])
            value |= (1 << i);
    }
    return WriteInt(AppName, KeyName, value);
}

void CIniHelper::GetBoolArray(const wchar_t * AppName, const wchar_t * KeyName, bool * values, int size, bool default_value) const
{
    int value = GetInt(AppName, KeyName, 0);
    for (int i{}; i < size; i++)
    {
        values[i] = ((value >> i) % 2 != 0);
    }
}

void CIniHelper::WriteStringList(const wchar_t* AppName, const wchar_t* KeyName, const vector<wstring>& values)
{
    wstring str_write = MergeStringList(values);
    _WriteString(AppName, KeyName, str_write);
}

void CIniHelper::GetStringList(const wchar_t* AppName, const wchar_t* KeyName, vector<wstring>& values, const vector<wstring>& default_value) const
{
    wstring str_value = MergeStringList(default_value);
    _GetString(AppName, KeyName, str_value);
    SplitStringList(values, str_value);
}

vector<wstring> CIniHelper::GetAllAppName(const wstring& prefix) const
{
    vector<wstring> list;
    if (prefix.size() > kMaxIniSectionNameLength)
        return list;

    const wstring section_prefix = L"\n[" + prefix;
    size_t pos{};
    while (list.size() < kMaxIniSections && (pos = m_ini_str.find(section_prefix, pos)) != wstring::npos)
    {
        const size_t name_begin = pos + section_prefix.size();
        const size_t line_end = m_ini_str.find(L'\n', name_begin);
        const size_t end = m_ini_str.find(L']', name_begin);
        if (end != wstring::npos && (line_end == wstring::npos || end < line_end))
        {
            const size_t name_length = end - name_begin;
            if (name_length <= kMaxIniSectionNameLength)
                list.emplace_back(m_ini_str, name_begin, name_length);
            pos = end + 1;
        }
        else
        {
            // A malformed section header must still advance the scan.  This
            // also prevents an oversized header from being copied repeatedly.
            pos = line_end == wstring::npos ? m_ini_str.size() : line_end + 1;
        }
    }
    return list;
}

void CIniHelper::GetAllKeyValues(const wstring& AppName, std::map<wstring, wstring>& map) const
{
    if (AppName.empty() || AppName.size() > kMaxIniSectionNameLength)
        return;

    wstring app_str{ L"[" };
    app_str.append(AppName).append(L"]");
    const size_t app_pos = m_ini_str.find(app_str);
    if (app_pos == wstring::npos)
        return;

    const size_t next_section = m_ini_str.find(L"\n[", app_pos + app_str.size());
    const size_t section_end = next_section == wstring::npos ? m_ini_str.size() : next_section + 1;
    size_t line_begin = m_ini_str.find(L'\n', app_pos);
    if (line_begin == wstring::npos || line_begin >= section_end)
        return;
    ++line_begin;

    size_t key_value_count{};
    size_t line_count{};
    while (line_begin < section_end && key_value_count < kMaxIniKeyValuesPerSection && line_count < kMaxIniLinesPerSection)
    {
        const size_t line_end = (std::min)(m_ini_str.find(L'\n', line_begin), section_end);
        ++line_count;
        if (line_begin < line_end && m_ini_str[line_begin] != L';' && m_ini_str[line_begin] != L'#')
        {
            const auto separator_iter = std::find(m_ini_str.begin() + line_begin, m_ini_str.begin() + line_end, L'=');
            if (separator_iter != m_ini_str.begin() + line_end)
            {
                const size_t separator = static_cast<size_t>(separator_iter - m_ini_str.begin());
                if (separator - line_begin <= kMaxIniKeyLength
                    && line_end - separator - 1 <= kMaxIniValueLength)
                {
                    wstring key(m_ini_str, line_begin, separator - line_begin);
                    wstring value(m_ini_str, separator + 1, line_end - separator - 1);
                    CCommon::StringNormalize(key);
                    CCommon::StringNormalize(value);
                    if (!key.empty() && !value.empty()
                        && key.size() <= kMaxIniKeyLength && value.size() <= kMaxIniValueLength)
                    {
                        if (value.front() == L'\"' && value.back() == L'\"')
                            value = value.substr(1, value.size() - 2);
                        UnEscapeString(value);
                        map[key] = std::move(value);
                        ++key_value_count;
                    }
                }
            }
        }
        line_begin = line_end == section_end ? section_end : line_end + 1;
    }
}

bool CIniHelper::RemoveSection(const wstring& AppName)
{
    if (AppName.empty())
        return false;
    wstring app_str{ L"[" };
    app_str.append(AppName).append(L"]");
    size_t app_pos{}, app_end_pos{};
    app_pos = m_ini_str.find(app_str);
    if (app_pos == wstring::npos)       //找不到AppName，返回默认字符串
        return false;

    app_end_pos = m_ini_str.find(L"\n[", app_pos + 2);
    if (app_end_pos != wstring::npos)
        app_end_pos++;

    m_ini_str.erase(app_pos, app_end_pos - app_pos);

    return true;
}

bool CIniHelper::Save()
{
    if (m_file_path.empty() || m_load_failed)
        return false;

    if (m_ini_str.find(L'\0') != wstring::npos)
        return false;

    string serialized_ini;
    try
    {
        if (!TryEncodeIniText(m_ini_str, m_save_as_utf8, serialized_ini))
            return false;
    }
    catch (...)
    {
        return false;
    }

    const size_t bom_length = m_save_as_utf8 ? kUtf8BomLength : 0;
    if (serialized_ini.size() > kMaxIniFileSize - bom_length)
        return false;

    const size_t separator = m_file_path.find_last_of(L"\\/");
    const wstring directory = separator == wstring::npos ? L"." : m_file_path.substr(0, separator);
    wchar_t temporary_path[MAX_PATH]{};
    if (::GetTempFileNameW(directory.c_str(), L"TBM", 0, temporary_path) == 0)
        return false;

    ofstream temporary_stream{ temporary_path, std::ios::binary | std::ios::trunc };
    if (!temporary_stream.is_open())
    {
        ::DeleteFileW(temporary_path);
        return false;
    }

    if (m_save_as_utf8)
    {
        const char utf8_bom[] = { static_cast<char>(-17), static_cast<char>(-69), static_cast<char>(-65) };
        temporary_stream.write(utf8_bom, sizeof(utf8_bom));
    }
    temporary_stream << serialized_ini;
    temporary_stream.flush();
    const bool write_succeeded = temporary_stream.good();
    temporary_stream.close();
    const bool close_succeeded = temporary_stream.good();
    const DWORD move_flags = MOVEFILE_WRITE_THROUGH | (m_file_existed_at_load ? MOVEFILE_REPLACE_EXISTING : 0);
    if (!write_succeeded || !close_succeeded || !::MoveFileExW(temporary_path, m_file_path.c_str(), move_flags))
    {
        ::DeleteFileW(temporary_path);
        return false;
    }
    // A helper constructed for a missing file can be reused.  Once the first
    // atomic creation succeeds, later saves must replace that file.
    m_file_existed_at_load = true;
    return true;
}

void CIniHelper::UnEscapeString(wstring& str)
{
    bool escape{ false };
    wstring result;
    result.reserve(str.size());
    for (size_t i = 0; i < str.size(); i++)
    {
        wchar_t ch = str[i];
        if (escape)
        {
            switch (ch)
            {
            case L'n': result += L'\n'; break;
            case L'r': result += L'\r'; break;
            case L't': result += L'\t'; break;
            case L'"': result += L'"'; break;
            case L';': result += L';'; break;
            case L'#': result += L'#'; break;
            case L'\\': result += L'\\'; break;
            default:result += '\\'; result += ch; break;
            }
            escape = false;
        }
        else if (ch == L'\\')
            escape = true;
        else if (i > 0 && ch == '\"' && str[i - 1] == '\"')     //两个连续的引号只保留一个引号
            continue;
        else
            result += ch;
    }
    str.swap(result);
}

void CIniHelper::_WriteString(const wchar_t * AppName, const wchar_t * KeyName, const wstring & str)
{
    wstring app_str{ L"[" };
    app_str.append(AppName).append(L"]");
    size_t app_pos{}, app_end_pos, key_pos;
    app_pos = m_ini_str.find(app_str);
    if (app_pos == wstring::npos)       //找不到AppName，则在最后面添加
    {
        if (!m_ini_str.empty() && m_ini_str.back() != L'\n')
            m_ini_str += L"\n";
        app_pos = m_ini_str.size();
        m_ini_str += app_str;
        m_ini_str += L"\n";
    }
    app_end_pos = m_ini_str.find(L"\n[", app_pos + 2);
    if (app_end_pos != wstring::npos)
        app_end_pos++;

    key_pos = m_ini_str.find(wstring(L"\n") + KeyName + L' ', app_pos);     //查找“\nkey_name ”
    if (key_pos >= app_end_pos)     //如果找不到“\nkey_name ”，则查找“\nkey_name=”
        key_pos = m_ini_str.find(wstring(L"\n") + KeyName + L'=', app_pos);
    if (key_pos >= app_end_pos)             //找不到KeyName，则插入一个
    {
        //wchar_t buff[256];
        //swprintf_s(buff, L"%s = %s\n", KeyName, str.c_str());
        std::wstring str_temp = KeyName;
        str_temp += L" = ";
        str_temp += str;
        str_temp += L"\n";
        if (app_end_pos == wstring::npos)
            m_ini_str += str_temp;
        else
            m_ini_str.insert(app_end_pos, str_temp);
    }
    else    //找到了KeyName，将等号到换行符之间的文本替换
    {
        size_t str_pos;
        str_pos = m_ini_str.find(L'=', key_pos + 2);
        size_t line_end_pos = m_ini_str.find(L'\n', key_pos + 2);
        if (str_pos > line_end_pos) //所在行没有等号，则插入一个等号
        {
            m_ini_str.insert(key_pos + wcslen(KeyName) + 1, L" =");
            str_pos = key_pos + wcslen(KeyName) + 2;
        }
        else
        {
            str_pos++;
        }
        size_t str_end_pos;
        str_end_pos = m_ini_str.find(L"\n", str_pos);
        m_ini_str.replace(str_pos, str_end_pos - str_pos, L" " + str);
    }
}

bool CIniHelper::_GetString(const wchar_t* AppName, const wchar_t* KeyName, wstring& str) const
{
    wstring app_str{ L"[" };
    app_str.append(AppName).append(L"]");
    size_t app_pos{}, app_end_pos, key_pos;
    app_pos = m_ini_str.find(app_str);
    if (app_pos == wstring::npos)       //找不到AppName，返回默认字符串
        return false;

    app_end_pos = m_ini_str.find(L"\n[", app_pos + 2);
    if (app_end_pos != wstring::npos)
        app_end_pos++;

    key_pos = m_ini_str.find(wstring(L"\n") + KeyName + L' ', app_pos);     //查找“\nkey_name ”
    if (key_pos >= app_end_pos)     //如果找不到“\nkey_name ”，则查找“\nkey_name=”
        key_pos = m_ini_str.find(wstring(L"\n") + KeyName + L'=', app_pos);
    if (key_pos >= app_end_pos)             //找不到KeyName，返回默认字符串
    {
        return false;
    }
    else    //找到了KeyName，获取等号到换行符之间的文本
    {
        size_t str_pos;
        str_pos = m_ini_str.find(L'=', key_pos + 2);
        size_t line_end_pos = m_ini_str.find(L'\n', key_pos + 2);
        if (str_pos > line_end_pos) //所在行没有等号，返回默认字符串
        {
            return false;
        }
        else
        {
            str_pos++;
        }
        size_t str_end_pos;
        str_end_pos = m_ini_str.find(L"\n", str_pos);
        //获取文本
        wstring return_str{ m_ini_str.substr(str_pos, str_end_pos - str_pos) };
        //如果前后有空格，则将其删除
        CCommon::StringNormalize(return_str);
        str = return_str;
        return true;
    }
}

wstring CIniHelper::MergeStringList(const vector<wstring>& values)
{
    wstring str_merge;
    int index = 0;
    //在每个字符串前后加上引号，再将它们用逗号连接起来
    for (const wstring& str : values)
    {
        if (index > 0)
            str_merge.push_back(L',');
        str_merge.push_back(L'\"');
        str_merge += str;
        str_merge.push_back(L'\"');
        index++;
    }
    return str_merge;
}

void CIniHelper::SplitStringList(vector<wstring>& values, const wstring& str_value)
{
    values.clear();
    size_t item_begin{};
    while (item_begin < str_value.size())
    {
        // String lists are serialized as "item1","item2".  Do not try to
        // repair a malformed value: this input originates in a user-editable
        // INI file, and stripping characters from an empty item used to call
        // pop_back() on an empty string.
        if (str_value[item_begin] != L'\"')
        {
            values.clear();
            return;
        }

        const size_t item_end = str_value.find(L'\"', item_begin + 1);
        if (item_end == wstring::npos)
        {
            values.clear();
            return;
        }

        const size_t item_length = item_end - item_begin - 1;
        if (values.size() >= kMaxIniStringListItems || item_length > kMaxIniStringListItemLength)
        {
            values.clear();
            return;
        }
        values.emplace_back(str_value, item_begin + 1, item_length);
        item_begin = item_end + 1;
        if (item_begin == str_value.size())
            return;
        if (str_value[item_begin] != L',' || item_begin + 1 >= str_value.size())
        {
            values.clear();
            return;
        }
        ++item_begin;
    }
}
