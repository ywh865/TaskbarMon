#include "stdafx.h"
#include "DllFunctions.h"

#include <array>
#include <cstddef>
#include <cwchar>

namespace
{
    constexpr DWORD kLoadLibrarySearchSystem32 = 0x00000800;

    bool IsSystemLibraryBasename(const wchar_t* dll_name) noexcept
    {
        return dll_name != nullptr && *dll_name != L'\0' &&
            std::wcspbrk(dll_name, L"\\/:") == nullptr;
    }
}

HMODULE DllFunctions::LoadSystemLibrary(LPCWSTR dll_name) noexcept
{
    if (!IsSystemLibraryBasename(dll_name))
        return nullptr;

    // LOAD_LIBRARY_SEARCH_SYSTEM32 is available on supported modern Windows.
    // An explicit System32 path is retained as a safe compatibility fallback
    // when the search flag is rejected by an older system.
    if (HMODULE module = ::LoadLibraryExW(dll_name, nullptr, kLoadLibrarySearchSystem32))
        return module;

    // Keep this fallback allocation-free because callers construct DLL
    // wrappers during startup and their constructors are noexcept.
    std::array<wchar_t, 32768> system_directory{};
    const UINT length = ::GetSystemDirectoryW(system_directory.data(), static_cast<UINT>(system_directory.size()));
    if (length == 0 || length >= static_cast<UINT>(system_directory.size()))
        return nullptr;

    const std::size_t dll_length = std::wcslen(dll_name);
    const bool needs_separator = system_directory[length - 1] != L'\\';
    const std::size_t full_path_length = static_cast<std::size_t>(length) +
        (needs_separator ? 1u : 0u) + dll_length;
    if (full_path_length >= system_directory.size())
        return nullptr;

    std::array<wchar_t, 32768> full_path{};
    for (std::size_t index = 0; index < length; ++index)
        full_path[index] = system_directory[index];

    std::size_t index = length;
    if (needs_separator)
        full_path[index++] = L'\\';
    for (std::size_t dll_index = 0; dll_index < dll_length; ++dll_index)
        full_path[index++] = dll_name[dll_index];

    return ::LoadLibraryExW(full_path.data(), nullptr, 0);
}

CDllFunctions::CDllFunctions()
{
    // shellscalingapi
    m_shcore_module = DllFunctions::LoadSystemLibrary(L"Shcore.dll");
    if (m_shcore_module != NULL)
    {
        m_getDpiForMonitor = (_GetDpiForMonitor)::GetProcAddress(m_shcore_module, "GetDpiForMonitor");
    }
}

CDllFunctions::~CDllFunctions()
{
    if (m_shcore_module != NULL)
    {
        FreeLibrary(m_shcore_module);
        m_shcore_module = NULL;
    }
}

HRESULT CDllFunctions::GetDpiForMonitor(HMONITOR hmonitor, MONITOR_DPI_TYPE dpiType, UINT* dpiX, UINT* dpiY)
{
    if (m_getDpiForMonitor != nullptr)
        return m_getDpiForMonitor(hmonitor, dpiType, dpiX, dpiY);
    return E_NOINTERFACE;
}

#define TRAFFICMONITOR_DEFINE_STATIC_MEMBER_IN_DLL_FUNCTIONS(member_name, ...) \
    decltype(CDllFunctions::member_name) CDllFunctions::member_name(__VA_ARGS__)

TRAFFICMONITOR_DEFINE_STATIC_MEMBER_IN_DLL_FUNCTIONS(
    D3DCompile,
    _T("d3dcompiler_47.dll"), "D3DCompile");

TRAFFICMONITOR_DEFINE_STATIC_MEMBER_IN_DLL_FUNCTIONS(
    DCompositionCreateDevice,
    _T("dcomp.dll"), "DCompositionCreateDevice");

TRAFFICMONITOR_DEFINE_STATIC_MEMBER_IN_DLL_FUNCTIONS(
    CreateDXGIFactory2,
    _T("dxgi.dll"), "CreateDXGIFactory2");
