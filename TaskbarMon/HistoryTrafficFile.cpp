#include "stdafx.h"
#include "HistoryTrafficFile.h"
#include "Common.h"

namespace
{
    // Normal history files contain one short record per day.  These limits keep
    // malformed or user-replaced files from causing unbounded allocations or work.
    constexpr size_t kMaxHistoryTrafficRecords = 32768;
    constexpr size_t kMaxHistoryTrafficBlankLines = 64;
    constexpr size_t kMaxHistoryTrafficPhysicalDataLines = kMaxHistoryTrafficRecords + kMaxHistoryTrafficBlankLines;
    constexpr size_t kMaxHistoryTrafficLineLength = 256;
    // About 50 TB per direction; this also keeps bounded aggregate byte totals representable.
    constexpr unsigned __int64 kMaxTrafficKBytes = 50000000000ULL;

    bool IsMissingHistoryFileError(DWORD error)
    {
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }

    bool IsSafeExistingHistoryFile(DWORD attributes)
    {
        return attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
    }

    bool IsTrailingWhitespace(char character)
    {
        return character == ' ' || character == '\t' || character == '\r';
    }

    bool TryParseUnsignedValue(const string& value, unsigned __int64 maximum, unsigned __int64& parsed_value)
    {
        if (value.empty())
            return false;

        unsigned __int64 parsed{};
        size_t index{};
        for (; index < value.size(); ++index)
        {
            const char character = value[index];
            if (character < '0' || character > '9')
                break;

            const unsigned __int64 digit = static_cast<unsigned __int64>(character - '0');
            if (parsed > maximum / 10 || (parsed == maximum / 10 && digit > maximum % 10))
                return false;
            parsed = parsed * 10 + digit;
        }
        if (index == 0)
            return false;

        while (index < value.size() && IsTrailingWhitespace(value[index]))
            ++index;
        if (index != value.size())
            return false;

        parsed_value = parsed;
        return true;
    }

    bool TryParseFixedDecimal(const string& value, size_t begin, size_t length, int minimum, int maximum, int& parsed_value)
    {
        if (begin > value.size() || length > value.size() - begin)
            return false;

        int parsed{};
        for (size_t index{}; index < length; ++index)
        {
            const char character = value[begin + index];
            if (character < '0' || character > '9')
                return false;
            parsed = parsed * 10 + character - '0';
        }
        if (parsed < minimum || parsed > maximum)
            return false;

        parsed_value = parsed;
        return true;
    }

    bool TryReadHistoryLine(ifstream& file, string& line)
    {
        line.clear();
        char character{};
        while (file.get(character))
        {
            if (character == '\n')
                return true;
            if (character == '\0' || line.size() >= kMaxHistoryTrafficLineLength)
            {
                file.setstate(std::ios::failbit);
                return false;
            }
            line += character;
        }

        return !line.empty() && file.eof();
    }
    bool TryParseHistoryHeader(const string& header, size_t& declared_record_count)
    {
        constexpr char kHeaderPrefix[] = "lines: \"";
        constexpr size_t kHeaderPrefixLength = sizeof(kHeaderPrefix) - 1;
        if (header.compare(0, kHeaderPrefixLength, kHeaderPrefix) != 0)
            return false;

        const size_t closing_quote = header.find('"', kHeaderPrefixLength);
        if (closing_quote == string::npos || closing_quote == kHeaderPrefixLength)
            return false;

        unsigned __int64 parsed_record_count{};
        if (!TryParseUnsignedValue(header.substr(kHeaderPrefixLength, closing_quote - kHeaderPrefixLength),
            static_cast<unsigned __int64>(kMaxHistoryTrafficRecords), parsed_record_count) || parsed_record_count == 0)
        {
            return false;
        }

        size_t tail = closing_quote + 1;
        while (tail < header.size() && IsTrailingWhitespace(header[tail]))
            ++tail;
        if (tail != header.size())
            return false;

        declared_record_count = static_cast<size_t>(parsed_record_count);
        return true;
    }

    bool TryParseHistoryRecord(const string& line, HistoryTraffic& traffic)
    {
        if (line.size() < 12 || line[4] != '/' || line[7] != '/' || line[10] != ' ')
            return false;

        if (!TryParseFixedDecimal(line, 0, 4, 1900, 3000, traffic.year) ||
            !TryParseFixedDecimal(line, 5, 2, 1, 12, traffic.month) ||
            !TryParseFixedDecimal(line, 8, 2, 1, 31, traffic.day))
        {
            return false;
        }

        const size_t separator_index = line.find('/', 11);
        traffic.mixed = separator_index == string::npos;
        if (traffic.mixed)
        {
            if (!TryParseUnsignedValue(line.substr(11), kMaxTrafficKBytes, traffic.down_kBytes))
                return false;
            traffic.up_kBytes = 0;
        }
        else
        {
            if (!TryParseUnsignedValue(line.substr(11, separator_index - 11), kMaxTrafficKBytes, traffic.up_kBytes) ||
                !TryParseUnsignedValue(line.substr(separator_index + 1), kMaxTrafficKBytes, traffic.down_kBytes))
            {
                return false;
            }
        }
        return true;
    }
}

CHistoryTrafficFile::CHistoryTrafficFile(const wstring& file_path)
	: m_file_path(file_path)
{
}

CHistoryTrafficFile::~CHistoryTrafficFile()
{
}

HistoryTraffic CHistoryTrafficFile::CreateTodayTraffic() const
{
	SYSTEMTIME current_time;
	GetLocalTime(&current_time);
	HistoryTraffic today_traffic;
	today_traffic.year = current_time.wYear;
	today_traffic.month = current_time.wMonth;
	today_traffic.day = current_time.wDay;
	today_traffic.up_kBytes = 0;
	today_traffic.down_kBytes = 0;
	today_traffic.mixed = false;
	return today_traffic;
}

void CHistoryTrafficFile::WriteTrafficRecord(ofstream& file, const HistoryTraffic& traffic) const
{
	char buff[64];
	if (traffic.mixed)
	{
		sprintf_s(buff, "%.4d/%.2d/%.2d %llu", traffic.year, traffic.month, 
			traffic.day, traffic.down_kBytes);
	}
	else
	{
		sprintf_s(buff, "%.4d/%.2d/%.2d %llu/%llu", traffic.year, traffic.month, 
			traffic.day, traffic.up_kBytes, traffic.down_kBytes);
	}
	file << buff << "\n";
}

void CHistoryTrafficFile::UpdateCache() const
{
	// 更新缓存：合并今天的记录和历史记录链表
	m_traffics_cache.clear();
	m_traffics_cache.push_front(m_today_traffic);
	m_traffics_cache.insert(m_traffics_cache.end(), m_history_traffics.begin(), m_history_traffics.end());
	m_cache_dirty = false; // 标记缓存已更新
}

bool CHistoryTrafficFile::Save() const
{
    if (m_file_path.empty() || (m_load_state != LoadState::Missing && m_load_state != LoadState::Valid) ||
        m_history_traffics.size() >= kMaxHistoryTrafficRecords)
    {
        return false;
    }

    const size_t separator = m_file_path.find_last_of(L"\\/");
    const wstring directory = separator == wstring::npos ? L"." : m_file_path.substr(0, separator);
    wchar_t temporary_path[MAX_PATH]{};
    if (::GetTempFileNameW(directory.c_str(), L"TBH", 0, temporary_path) == 0)
        return false;

    ofstream file{ temporary_path, std::ios::binary | std::ios::trunc };
    if (!file.is_open())
    {
        ::DeleteFileW(temporary_path);
        return false;
    }

    char buff[64];
    const size_t total_size = 1 + m_history_traffics.size();
    sprintf_s(buff, "lines: \"%u\"", static_cast<unsigned int>(total_size));
    file << buff << "\n";
    WriteTrafficRecord(file, m_today_traffic);
    for (const auto& history_traffic : m_history_traffics)
    {
        WriteTrafficRecord(file, history_traffic);
    }

    file.flush();
    const bool write_succeeded = file.good();
    file.close();
    const bool close_succeeded = file.good();
    const DWORD move_flags = MOVEFILE_WRITE_THROUGH | (m_destination_can_be_replaced ? MOVEFILE_REPLACE_EXISTING : 0);
    if (!write_succeeded || !close_succeeded || !::MoveFileExW(temporary_path, m_file_path.c_str(), move_flags))
    {
        ::DeleteFileW(temporary_path);
        return false;
    }

    m_destination_can_be_replaced = true;
    m_load_state = LoadState::Valid;
    return true;
}

bool CHistoryTrafficFile::IsTodayRecord() const
{
	// 检查今天的记录日期是否正确
	SYSTEMTIME current_time;
	GetLocalTime(&current_time);
	
	return (m_today_traffic.year == current_time.wYear &&
			m_today_traffic.month == current_time.wMonth &&
			m_today_traffic.day == current_time.wDay);
}

bool CHistoryTrafficFile::SaveTodayOnly() const
{
    // Full atomic replacement avoids leaving a truncated history file after a crash.
    return Save();
}

void CHistoryTrafficFile::Load()
{
    m_load_state = LoadState::Uninitialized;
    m_destination_can_be_replaced = false;
    m_today_traffic = HistoryTraffic{};
    m_history_traffics.clear();
    InvalidateCache();

    if (m_file_path.empty())
    {
        MormalizeData();
        return;
    }

    const DWORD attributes = ::GetFileAttributesW(m_file_path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        if (IsMissingHistoryFileError(::GetLastError()))
            m_load_state = LoadState::Missing;
        else
            m_load_state = LoadState::Failed;
        MormalizeData();
        return;
    }
    if (!IsSafeExistingHistoryFile(attributes))
    {
        m_load_state = LoadState::Failed;
        MormalizeData();
        return;
    }

    m_destination_can_be_replaced = true;
    ifstream file{ m_file_path, std::ios::binary };
    size_t declared_record_count{};
    string current_line;
    if (!file.is_open() || !TryReadHistoryLine(file, current_line) ||
        !TryParseHistoryHeader(current_line, declared_record_count))
    {
        m_load_state = LoadState::Failed;
        MormalizeData();
        return;
    }

    bool load_failed{};
    bool is_first_data_line = true;
    size_t lines_read{};
    size_t physical_lines_read{};
    while (TryReadHistoryLine(file, current_line))
    {
        if (++physical_lines_read > kMaxHistoryTrafficPhysicalDataLines)
        {
            load_failed = true;
            break;
        }
        if (current_line.empty() || current_line == "\r")
            continue;
        if (++lines_read > kMaxHistoryTrafficRecords)
        {
            load_failed = true;
            break;
        }

        HistoryTraffic traffic{};
        if (!TryParseHistoryRecord(current_line, traffic))
        {
            load_failed = true;
            break;
        }

        if (is_first_data_line)
        {
            m_today_traffic = traffic;
            is_first_data_line = false;
        }
        else
        {
            m_history_traffics.push_back(traffic);
        }
    }

    if (!file.eof() || file.bad() || is_first_data_line || lines_read != declared_record_count)
        load_failed = true;
    if (load_failed)
    {
        m_today_traffic = HistoryTraffic{};
        m_history_traffics.clear();
        m_load_state = LoadState::Failed;
        MormalizeData();
        return;
    }

    m_load_state = LoadState::Valid;
    MormalizeData();
}

bool CHistoryTrafficFile::RecoverFromBackup(const CHistoryTrafficFile& backup)
{
    if (m_load_state != LoadState::Failed || !m_destination_can_be_replaced || !backup.IsLoadValid())
        return false;

    m_today_traffic = backup.m_today_traffic;
    m_history_traffics = backup.m_history_traffics;
    MormalizeData();
    m_load_state = LoadState::Valid;
    return true;
}

void CHistoryTrafficFile::LoadSize()
{
    m_size = 0;

    if (m_file_path.empty())
        return;

    const DWORD attributes = ::GetFileAttributesW(m_file_path.c_str());
    if (!IsSafeExistingHistoryFile(attributes))
        return;

    ifstream file{ m_file_path, std::ios::binary };
    string header;
    size_t declared_record_count{};
    if (!file.is_open() || !TryReadHistoryLine(file, header) ||
        !TryParseHistoryHeader(header, declared_record_count))
    {
        return;
    }

    m_size = declared_record_count;
}

void CHistoryTrafficFile::Merge(const CHistoryTrafficFile& history_traffic, bool ignore_same_data)
{
    if (!history_traffic.IsLoadValid())
        return;
	HistoryTraffic today_traffic = CreateTodayTraffic();

	// 合并今天的记录（只合并日期相同的）
	// 注意：如果 ignore_same_data=true，说明是从备份恢复，应该取较大的值而不是累加，避免重复累加
	if (HistoryTraffic::DateEqual(m_today_traffic, history_traffic.m_today_traffic))
	{
		if (ignore_same_data)
		{
			// 从备份恢复时，取较大的值（避免重复累加）
			// 备份文件通常是程序退出时的完整数据，当前文件可能是程序启动后的不完整数据
			if (history_traffic.m_today_traffic.up_kBytes > m_today_traffic.up_kBytes)
			{
				m_today_traffic.up_kBytes = history_traffic.m_today_traffic.up_kBytes;
			}
			if (history_traffic.m_today_traffic.down_kBytes > m_today_traffic.down_kBytes)
			{
				m_today_traffic.down_kBytes = history_traffic.m_today_traffic.down_kBytes;
			}
		}
		else
		{
			// 正常合并时，累加数据
			m_today_traffic.up_kBytes += history_traffic.m_today_traffic.up_kBytes;
			m_today_traffic.down_kBytes += history_traffic.m_today_traffic.down_kBytes;
		}
	}

	// 合并历史记录链表
	for (const HistoryTraffic& traffic : history_traffic.m_history_traffics)
	{
		// 跳过"未来"的记录（系统时间可能被调整了）
		if (HistoryTraffic::DateGreater(traffic, today_traffic))
		{
			continue; // 跳过"未来"的记录
		}

		if (ignore_same_data)
		{
			// 如果要忽略相同日期的项，使用线性查找（list不支持随机访问）
			auto it = std::find_if(m_history_traffics.begin(), m_history_traffics.end(),
				[&traffic](const HistoryTraffic& existing) {
					return HistoryTraffic::DateEqual(existing, traffic);
				});
			if (it != m_history_traffics.end())
			{
				continue; // 找到相同日期的记录，跳过
			}
		}
		m_history_traffics.push_back(traffic);
	}
	
	MormalizeData();
	InvalidateCache(); // 标记缓存过期
}

void CHistoryTrafficFile::OnDateChanged()
{
	// 日期改变时，将今天的记录移到历史记录链表的前面，然后创建新的今天的记录
	
	// 如果今天的记录有数据，将其移到历史记录链表
	if (m_today_traffic.kBytes() > 0)
	{
		m_history_traffics.push_front(m_today_traffic);
		// 立即排序，确保数据一致性（按日期从大到小）
		if (m_history_traffics.size() >= 2)
		{
			m_history_traffics.sort(HistoryTraffic::DateGreater);
		}
	}
	
	// 创建新的今天的记录
	m_today_traffic = CreateTodayTraffic();
	
	// 更新统计
	m_today_up_traffic = 0;
	m_today_down_traffic = 0;
	m_size = 1 + m_history_traffics.size();
	InvalidateCache(); // 标记缓存过期
}

void CHistoryTrafficFile::MormalizeData()
{
	HistoryTraffic today_traffic = CreateTodayTraffic();

	// 先对历史记录链表排序（按日期从大到小），以便后续查找和合并
	if (m_history_traffics.size() >= 2)
	{
		m_history_traffics.sort(HistoryTraffic::DateGreater);

		// 合并相同日期的记录
		auto it = m_history_traffics.begin();
		while (it != m_history_traffics.end())
		{
			auto next_it = it;
			++next_it;
			if (next_it != m_history_traffics.end() && HistoryTraffic::DateEqual(*it, *next_it))
			{
				it->up_kBytes += next_it->up_kBytes;
				it->down_kBytes += next_it->down_kBytes;
				m_history_traffics.erase(next_it);
			}
			else
			{
				++it;
			}
		}
	}

	// 清理日期晚于当前日期的历史记录（系统时间可能被调整了）
	// 历史记录应该都是过去的日期，不应该有未来的日期
	if (!m_history_traffics.empty())
	{
		auto it = m_history_traffics.begin();
		while (it != m_history_traffics.end())
		{
			// 如果历史记录的日期晚于今天，说明是"未来"的记录，应该删除
			if (HistoryTraffic::DateGreater(*it, today_traffic))
			{
				it = m_history_traffics.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	// 如果 m_today_traffic 的日期也晚于当前日期，说明系统时间被调整了，应该重置
	if (HistoryTraffic::DateGreater(m_today_traffic, today_traffic))
	{
		// 如果今天的记录有数据，应该将其移到历史记录（但日期晚于今天，会被上面的清理逻辑删除）
		// 直接重置为今天的记录
		m_today_traffic = today_traffic;
	}

	// 在历史记录中查找今天的记录（可能历史记录中包含了今天的数据）
	auto it = std::find_if(m_history_traffics.begin(), m_history_traffics.end(),
		[&today_traffic](const HistoryTraffic& traffic) {
			return HistoryTraffic::DateEqual(traffic, today_traffic);
		});

	if (it != m_history_traffics.end())
	{
		// 历史记录中找到了今天的记录
		if (HistoryTraffic::DateEqual(m_today_traffic, today_traffic))
		{
			// 如果 m_today_traffic 也是今天的，合并数据（避免数据丢失）
			m_today_traffic.up_kBytes += it->up_kBytes;
			m_today_traffic.down_kBytes += it->down_kBytes;
		}
		else
		{
			// 如果 m_today_traffic 不是今天的，用历史记录中的替换
			m_today_traffic = *it;
		}
		// 从历史记录中删除今天的记录（因为应该只在 m_today_traffic 中）
		m_history_traffics.erase(it);
	}
	else if (!HistoryTraffic::DateEqual(m_today_traffic, today_traffic))
	{
		// 历史记录中没有今天的记录，且 m_today_traffic 也不是今天的
		// 如果 m_today_traffic 有数据，应该将其移到历史记录链表
		if (m_today_traffic.kBytes() > 0)
		{
			m_history_traffics.push_front(m_today_traffic);
			// 重新排序（因为插入了新记录）
			if (m_history_traffics.size() >= 2)
			{
				m_history_traffics.sort(HistoryTraffic::DateGreater);
			}
		}
		// 创建新的今天的记录
		m_today_traffic = today_traffic;
	}

	// 更新今天的流量统计
	m_today_up_traffic = static_cast<__int64>(m_today_traffic.up_kBytes) * 1024;
	m_today_down_traffic = static_cast<__int64>(m_today_traffic.down_kBytes) * 1024;
	m_today_traffic.mixed = false;

	// 更新总记录数
	m_size = 1 + m_history_traffics.size();
	InvalidateCache(); // 标记缓存过期
}
