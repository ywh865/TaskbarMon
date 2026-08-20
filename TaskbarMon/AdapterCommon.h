#pragma once
#include "Common.h"
#include <iphlpapi.h>

//保存一个网络连接信息
struct NetWorkConection
{
	// index is the current row in MIB_IFTABLE. It is a presentation
	// convenience only; interface_index is the stable identity used for
	// sampling and must be preferred after a table refresh.
	int index{ -1 };
	DWORD interface_index{};	// MIB_IFROW::dwIndex
	string description;		//网络描述（获取自GetAdapterInfo）
	string description_2;	//网络描述（获取自GetIfTable）
	unsigned int in_bytes;	//初始时已接收字节数
	unsigned int out_bytes;	//初始时已发送字节数
	wstring ip_address{ L"-.-.-.-" };	//IP地址
	wstring subnet_mask{ L"-.-.-.-" };	//子网掩码
	wstring default_gateway{ L"-.-.-.-" };	//默认网关
};

class CAdapterCommon
{
public:
	CAdapterCommon();
	~CAdapterCommon();

	//获取网络连接列表，填充网络描述、IP地址、子网掩码、默认网关信息
	static void GetAdapterInfo(vector<NetWorkConection>& adapters);

	//刷新网络连接列表中的IP地址、子网掩码、默认网关信息
	static void RefreshIpAddress(vector<NetWorkConection>& adapters);

	//获取网络列表中每个网络连接的MIB_IFTABLE中的索引、初始时已接收/发送字节数的信息
	static void GetIfTableInfo(vector<NetWorkConection>& adapters, MIB_IFTABLE* pIfTable);

	//直接将MIB_IFTABLE中的所有连接添加到adapters容器中
	static void GetAllIfTableInfo(vector<NetWorkConection>& adapters, MIB_IFTABLE* pIfTable);

	// Return a row for a Windows interface index, or -1 when the interface is
	// absent. MIB_IFTABLE row positions are not stable across refreshes.
	static int FindIfTableRowByInterfaceIndex(DWORD interface_index, const MIB_IFTABLE* pIfTable);

	// Ask Windows which interface owns the unique lowest-effective-metric
	// IPv4/IPv6 default route. Zero means no unambiguous default route was
	// resolved; callers must make a conservative fallback.
	static DWORD GetDefaultRouteInterfaceIndex();

	// bDescr is length-delimited by dwDescrLen and is not guaranteed to be NUL
	// terminated. Use this helper for all MIB description conversions.
	static string GetIfTableDescription(const MIB_IFROW& row);
private:
	//根据一个网络连接描述判断是否在IfTable列表里，返回索引，找不到则返回-1
	static int FindConnectionInIfTable(string connection, MIB_IFTABLE* pIfTable);

	//根据一个网络连接描述判断是否在IfTable接列表里，返回索引，找不到则返回-1。只需要部分匹配
	static int FindConnectionInIfTableFuzzy(string connection, MIB_IFTABLE* pIfTable);
};

