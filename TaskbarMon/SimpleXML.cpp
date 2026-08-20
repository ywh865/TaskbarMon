#include "stdafx.h"
#include "SimpleXML.h"
#include <new>


CSimpleXML::CSimpleXML(const wstring & xml_path)
{
	ifstream file_stream{ xml_path, std::ios::binary };
	if (!file_stream.is_open())
		return;

	constexpr size_t kMaxSimpleXmlBytes = 16 * 1024 * 1024;
	string xml_str;
	char read_buffer[4096];
	for (;;)
	{
		file_stream.read(read_buffer, static_cast<std::streamsize>(sizeof(read_buffer)));
		const std::streamsize bytes_read = file_stream.gcount();
		if (bytes_read == 0 && !file_stream.eof())
			return;
		if (bytes_read > 0)
		{
			const size_t chunk_size = static_cast<size_t>(bytes_read);
			if (chunk_size > kMaxSimpleXmlBytes - xml_str.size())
				return;
			try
			{
				xml_str.append(read_buffer, chunk_size);
			}
			catch (const std::bad_alloc&)
			{
				xml_str.clear();
				return;
			}
		}

		if (file_stream.bad() || (!file_stream.eof() && file_stream.fail()))
		{
			xml_str.clear();
			return;
		}
		if (file_stream.eof())
			break;
	}
	if (!xml_str.empty() && xml_str.back() != '\n')
		xml_str.push_back('\n');
	bool is_utf8;
	if (xml_str.size() >= 3 && xml_str[0] == -17 && xml_str[1] == -69 && xml_str[2] == -65)
	{
		//如果有UTF8的BOM，则删除BOM
		is_utf8 = true;
		xml_str = xml_str.substr(3);
	}
	else
	{
		is_utf8 = false;
	}
	//转换成Unicode
	m_xml_content = CCommon::StrToUnicode(xml_str.c_str(), is_utf8);
}

CSimpleXML::CSimpleXML()
{
}


CSimpleXML::~CSimpleXML()
{
}

wstring CSimpleXML::GetNode(const wchar_t * node, const wchar_t * parent) const
{
	wstring node_content = _GetNode(parent, m_xml_content);
	return _GetNode(node, node_content);
}

wstring CSimpleXML::GetNode(const wchar_t * node) const
{
	return _GetNode(node, m_xml_content);
}

wstring CSimpleXML::_GetNode(const wchar_t * node, const wstring & content)
{
	if (node == nullptr || *node == L'\0')
		return wstring();

	wstring node_start{ L'<' };
	wstring node_end{ L'<' };
	node_start += node;
	node_start += L'>';
	node_end += L'/';
	node_end += node;
	node_end += L'>';

	const size_t index_start = content.find(node_start);
	if (index_start == wstring::npos)
		return wstring();

	const size_t content_start = index_start + node_start.size();
	const size_t index_end = content.find(node_end, content_start);
	if (index_end == wstring::npos)
		return wstring();

	return content.substr(content_start, index_end - content_start);
}
