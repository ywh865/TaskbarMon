#include "stdafx.h"
#include "auto_start_helper.h"

#include <comdef.h>
#include <sddl.h>
#include <taskschd.h>
#include <wrl/client.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "taskschd.lib")

namespace
{
    using Microsoft::WRL::ComPtr;

    constexpr wchar_t TASKBAR_MON_TASK_FOLDER[] = L"\\TaskbarMon";
    constexpr wchar_t TASK_NAME_PREFIX[] = L"TaskbarMon autorun ";
    constexpr wchar_t LEGACY_TASK_NAME_PREFIX[] = L"Autorun for ";
    constexpr wchar_t TASKBAR_MON_PRINCIPAL_ID[] = L"TaskbarMonInteractivePrincipal";
    constexpr wchar_t TASKBAR_MON_TRIGGER_ID[] = L"TaskbarMonLogonTrigger";
    constexpr wchar_t TASKBAR_MON_TRIGGER_DELAY[] = L"PT03S";
    constexpr DWORD MAX_TOKEN_INFORMATION_BYTES = 65536;
    constexpr DWORD MAX_ACCOUNT_NAME_CHARACTERS = 32768;
    constexpr DWORD MAX_ACCOUNT_SID_BYTES = 32768;
    constexpr UINT MAX_TASK_STRING_CHARACTERS = 32768;

    class ScopedHandle final
    {
    public:
        ScopedHandle() = default;
        ScopedHandle(const ScopedHandle&) = delete;
        ScopedHandle& operator=(const ScopedHandle&) = delete;

        ~ScopedHandle()
        {
            if (m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE)
                CloseHandle(m_handle);
        }

        HANDLE* Receive()
        {
            return &m_handle;
        }

        HANDLE Get() const
        {
            return m_handle;
        }

    private:
        HANDLE m_handle{};
    };

    class ScopedLocalString final
    {
    public:
        ScopedLocalString() = default;
        ScopedLocalString(const ScopedLocalString&) = delete;
        ScopedLocalString& operator=(const ScopedLocalString&) = delete;

        ~ScopedLocalString()
        {
            if (m_value != nullptr)
                LocalFree(m_value);
        }

        LPWSTR* Receive()
        {
            return &m_value;
        }

        const wchar_t* Get() const
        {
            return m_value;
        }

    private:
        LPWSTR m_value{};
    };

    class ScopedLocalSid final
    {
    public:
        ScopedLocalSid() = default;
        ScopedLocalSid(const ScopedLocalSid&) = delete;
        ScopedLocalSid& operator=(const ScopedLocalSid&) = delete;

        ~ScopedLocalSid()
        {
            if (m_value != nullptr)
                LocalFree(m_value);
        }

        PSID* Receive()
        {
            return &m_value;
        }

        PSID Get() const
        {
            return m_value;
        }

    private:
        PSID m_value{};
    };

    class ScopedBstr final
    {
    public:
        ScopedBstr() = default;
        ScopedBstr(const ScopedBstr&) = delete;
        ScopedBstr& operator=(const ScopedBstr&) = delete;

        ~ScopedBstr()
        {
            if (m_value != nullptr)
                SysFreeString(m_value);
        }

        BSTR* Receive()
        {
            if (m_value != nullptr)
            {
                SysFreeString(m_value);
                m_value = nullptr;
            }
            return &m_value;
        }

        bool TryToWString(std::wstring& value) const
        {
            value.clear();
            if (m_value == nullptr)
                return true;

            const UINT length = SysStringLen(m_value);
            if (length > MAX_TASK_STRING_CHARACTERS)
                return false;

            value.assign(m_value, length);
            return true;
        }

    private:
        BSTR m_value{};
    };

    class ScopedComApartment final
    {
    public:
        ScopedComApartment(const ScopedComApartment&) = delete;
        ScopedComApartment& operator=(const ScopedComApartment&) = delete;

        ScopedComApartment()
            : m_result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))
        {
            // The caller may already have initialized an MTA. COM remains
            // usable in that case; only a successful call is balanced here.
            if (m_result == RPC_E_CHANGED_MODE)
            {
                m_result = S_OK;
            }
            else if (SUCCEEDED(m_result))
            {
                m_should_uninitialize = true;
            }
        }

        ~ScopedComApartment()
        {
            if (m_should_uninitialize)
                CoUninitialize();
        }

        HRESULT Result() const
        {
            return m_result;
        }

    private:
        HRESULT m_result{};
        bool m_should_uninitialize{};
    };

    struct CurrentUserIdentity
    {
        std::vector<BYTE> sid;
        std::wstring account_name;
        std::wstring task_name;
        std::wstring legacy_task_name;
    };

    struct AutorunTaskSpecification
    {
        CurrentUserIdentity user;
        std::wstring executable_path;
        std::wstring working_directory;
    };

    enum class ExistingTaskResult
    {
        Found,
        Missing,
        Failure,
    };

    enum class TaskFolderResult
    {
        Found,
        Missing,
        Failure,
    };

    bool IsNotFound(HRESULT hr)
    {
        return hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ||
            hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
    }

    bool IsAlreadyExists(HRESULT hr)
    {
        return hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }

    bool SameWindowsPath(const std::wstring& left, const std::wstring& right)
    {
        return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
    }

    bool GetCurrentExecutablePath(std::wstring& executable_path)
    {
        // GetModuleFileNameW can report a truncated result. A scheduled task
        // must never be registered with a truncated command.
        std::vector<wchar_t> buffer(MAX_PATH);
        while (buffer.size() <= 32768)
        {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
                return false;
            if (length < buffer.size())
            {
                executable_path.assign(buffer.data(), length);
                return true;
            }
            buffer.resize(buffer.size() * 2);
        }

        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }

    bool GetWorkingDirectory(const std::wstring& executable_path, std::wstring& working_directory)
    {
        const size_t separator = executable_path.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
            return false;

        // Preserve the trailing separator so a root directory stays absolute:
        // C:\\ is correct, whereas C: is drive-relative.
        working_directory = executable_path.substr(0, separator + 1);
        return !working_directory.empty();
    }

    bool GetCurrentUserIdentity(CurrentUserIdentity& identity)
    {
        ScopedHandle token;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.Receive()))
            return false;

        DWORD token_information_size{};
        if (GetTokenInformation(token.Get(), TokenUser, nullptr, 0, &token_information_size) ||
            GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
            token_information_size < sizeof(TOKEN_USER) ||
            token_information_size > MAX_TOKEN_INFORMATION_BYTES)
        {
            return false;
        }

        std::vector<BYTE> token_information(token_information_size);
        if (!GetTokenInformation(token.Get(), TokenUser, token_information.data(),
            token_information_size, &token_information_size))
        {
            return false;
        }

        const TOKEN_USER* token_user = reinterpret_cast<const TOKEN_USER*>(token_information.data());
        if (token_user->User.Sid == nullptr || !IsValidSid(token_user->User.Sid))
            return false;

        const DWORD sid_size = GetLengthSid(token_user->User.Sid);
        if (sid_size == 0)
            return false;

        identity.sid.resize(sid_size);
        if (!CopySid(sid_size, reinterpret_cast<PSID>(identity.sid.data()), token_user->User.Sid))
            return false;

        ScopedLocalString sid_string;
        if (!ConvertSidToStringSidW(reinterpret_cast<PSID>(identity.sid.data()), sid_string.Receive()) ||
            sid_string.Get() == nullptr ||
            sid_string.Get()[0] == L'\0')
        {
            return false;
        }

        DWORD account_length{};
        DWORD domain_length{};
        SID_NAME_USE account_type{};
        if (LookupAccountSidW(nullptr, reinterpret_cast<PSID>(identity.sid.data()),
            nullptr, &account_length, nullptr, &domain_length, &account_type) ||
            GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
            account_length == 0 ||
            domain_length == 0 ||
            account_length > MAX_ACCOUNT_NAME_CHARACTERS ||
            domain_length > MAX_ACCOUNT_NAME_CHARACTERS ||
            account_length + domain_length > MAX_ACCOUNT_NAME_CHARACTERS)
        {
            return false;
        }

        std::vector<wchar_t> account(account_length);
        std::vector<wchar_t> domain(domain_length);
        if (!LookupAccountSidW(nullptr, reinterpret_cast<PSID>(identity.sid.data()),
            account.data(), &account_length, domain.data(), &domain_length, &account_type))
        {
            return false;
        }

        identity.account_name.assign(domain.data());
        identity.account_name += L"\\";
        identity.account_name += account.data();
        // New task names include the immutable token SID, so same-named
        // accounts from different domains cannot collide. The old
        // account-only name is retained solely for safe one-way migration.
        identity.task_name = TASK_NAME_PREFIX;
        identity.task_name += sid_string.Get();
        identity.legacy_task_name = LEGACY_TASK_NAME_PREFIX;
        identity.legacy_task_name += account.data();
        return !identity.account_name.empty();
    }

    bool BuildTaskSpecification(AutorunTaskSpecification& specification)
    {
        if (!GetCurrentUserIdentity(specification.user))
            return false;
        if (!GetCurrentExecutablePath(specification.executable_path))
            return false;
        return GetWorkingDirectory(specification.executable_path, specification.working_directory);
    }

    bool UserIdentifierMatchesCurrentUser(const std::wstring& user_identifier, PSID current_user_sid)
    {
        if (user_identifier.empty() || current_user_sid == nullptr || !IsValidSid(current_user_sid))
            return false;

        ScopedLocalSid sid_from_identifier;
        if (ConvertStringSidToSidW(user_identifier.c_str(), sid_from_identifier.Receive()))
            return EqualSid(sid_from_identifier.Get(), current_user_sid) != FALSE;

        DWORD candidate_sid_size{};
        DWORD candidate_domain_size{};
        SID_NAME_USE candidate_account_type{};
        if (LookupAccountNameW(nullptr, user_identifier.c_str(), nullptr, &candidate_sid_size,
            nullptr, &candidate_domain_size, &candidate_account_type) ||
            GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
            candidate_sid_size == 0 ||
            candidate_sid_size > MAX_ACCOUNT_SID_BYTES ||
            candidate_domain_size > MAX_ACCOUNT_NAME_CHARACTERS)
        {
            return false;
        }

        std::vector<BYTE> candidate_sid(candidate_sid_size);
        std::vector<wchar_t> candidate_domain(candidate_domain_size == 0 ? 1 : candidate_domain_size);
        if (!LookupAccountNameW(nullptr, user_identifier.c_str(), candidate_sid.data(), &candidate_sid_size,
            candidate_domain.data(), &candidate_domain_size, &candidate_account_type))
        {
            return false;
        }

        return EqualSid(reinterpret_cast<PSID>(candidate_sid.data()), current_user_sid) != FALSE;
    }

    bool ConnectTaskService(ComPtr<ITaskService>& service)
    {
        HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
            IID_ITaskService, reinterpret_cast<void**>(service.GetAddressOf()));
        if (FAILED(hr) || service.Get() == nullptr)
            return false;

        hr = service->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
        return SUCCEEDED(hr);
    }

    TaskFolderResult GetTaskbarMonFolder(ITaskService* service, bool create_if_missing,
        ComPtr<ITaskFolder>& folder)
    {
        if (service == nullptr)
            return TaskFolderResult::Failure;

        HRESULT hr = service->GetFolder(_bstr_t(TASKBAR_MON_TASK_FOLDER), folder.GetAddressOf());
        if (SUCCEEDED(hr))
            return folder.Get() != nullptr ? TaskFolderResult::Found : TaskFolderResult::Failure;
        if (!IsNotFound(hr))
            return TaskFolderResult::Failure;
        if (!create_if_missing)
            return TaskFolderResult::Missing;

        folder.Reset();
        ComPtr<ITaskFolder> root_folder;
        hr = service->GetFolder(_bstr_t(L"\\"), root_folder.GetAddressOf());
        if (FAILED(hr) || root_folder.Get() == nullptr)
            return TaskFolderResult::Failure;

        hr = root_folder->CreateFolder(_bstr_t(TASKBAR_MON_TASK_FOLDER), _variant_t(),
            folder.GetAddressOf());
        if (SUCCEEDED(hr))
            return folder.Get() != nullptr ? TaskFolderResult::Found : TaskFolderResult::Failure;
        if (!IsAlreadyExists(hr))
            return TaskFolderResult::Failure;

        folder.Reset();
        hr = service->GetFolder(_bstr_t(TASKBAR_MON_TASK_FOLDER), folder.GetAddressOf());
        return SUCCEEDED(hr) && folder.Get() != nullptr ? TaskFolderResult::Found : TaskFolderResult::Failure;
    }

    ExistingTaskResult GetExistingTask(ITaskFolder* folder, const std::wstring& task_name,
        ComPtr<IRegisteredTask>& task)
    {
        if (folder == nullptr)
            return ExistingTaskResult::Failure;

        const HRESULT hr = folder->GetTask(_bstr_t(task_name.c_str()), task.GetAddressOf());
        if (SUCCEEDED(hr))
            return task.Get() != nullptr ? ExistingTaskResult::Found : ExistingTaskResult::Failure;
        return IsNotFound(hr) ? ExistingTaskResult::Missing : ExistingTaskResult::Failure;
    }

    bool DoesTaskHaveExpectedAction(ITaskDefinition* definition, const AutorunTaskSpecification& specification,
        bool allow_legacy_empty_working_directory)
    {
        if (definition == nullptr)
            return false;

        ComPtr<IActionCollection> actions;
        HRESULT hr = definition->get_Actions(actions.GetAddressOf());
        if (FAILED(hr) || actions.Get() == nullptr)
            return false;

        LONG action_count{};
        hr = actions->get_Count(&action_count);
        if (FAILED(hr) || action_count != 1)
            return false;

        _variant_t action_index(1L);
        ComPtr<IAction> action;
        hr = actions->get_Item(action_index, action.GetAddressOf());
        if (FAILED(hr) || action.Get() == nullptr)
            return false;

        TASK_ACTION_TYPE action_type{};
        hr = action->get_Type(&action_type);
        if (FAILED(hr) || action_type != TASK_ACTION_EXEC)
            return false;

        ComPtr<IExecAction> executable_action;
        hr = action->QueryInterface(IID_IExecAction,
            reinterpret_cast<void**>(executable_action.GetAddressOf()));
        if (FAILED(hr) || executable_action.Get() == nullptr)
            return false;

        ScopedBstr path;
        hr = executable_action->get_Path(path.Receive());
        std::wstring actual_path;
        if (FAILED(hr) || !path.TryToWString(actual_path) ||
            !SameWindowsPath(actual_path, specification.executable_path))
            return false;

        ScopedBstr arguments;
        hr = executable_action->get_Arguments(arguments.Receive());
        std::wstring actual_arguments;
        if (FAILED(hr) || !arguments.TryToWString(actual_arguments) || !actual_arguments.empty())
            return false;

        ScopedBstr working_directory;
        hr = executable_action->get_WorkingDirectory(working_directory.Receive());
        std::wstring actual_working_directory;
        if (FAILED(hr) || !working_directory.TryToWString(actual_working_directory))
            return false;

        return SameWindowsPath(actual_working_directory, specification.working_directory) ||
            (allow_legacy_empty_working_directory && actual_working_directory.empty());
    }

    bool DoesTaskHaveExpectedPrincipal(ITaskDefinition* definition, const AutorunTaskSpecification& specification)
    {
        if (definition == nullptr)
            return false;

        ComPtr<IPrincipal> principal;
        HRESULT hr = definition->get_Principal(principal.GetAddressOf());
        if (FAILED(hr) || principal.Get() == nullptr)
            return false;

        ScopedBstr principal_user_id;
        hr = principal->get_UserId(principal_user_id.Receive());
        std::wstring actual_principal_user_id;
        if (FAILED(hr) || !principal_user_id.TryToWString(actual_principal_user_id) ||
            !UserIdentifierMatchesCurrentUser(actual_principal_user_id,
            reinterpret_cast<PSID>(const_cast<BYTE*>(specification.user.sid.data()))))
        {
            return false;
        }

        TASK_LOGON_TYPE logon_type{};
        hr = principal->get_LogonType(&logon_type);
        if (FAILED(hr) || logon_type != TASK_LOGON_INTERACTIVE_TOKEN)
            return false;

        TASK_RUNLEVEL_TYPE run_level{};
        hr = principal->get_RunLevel(&run_level);
        return SUCCEEDED(hr) && run_level == TASK_RUNLEVEL_LUA;
    }

    bool DoesTaskHaveExpectedTrigger(ITaskDefinition* definition, const AutorunTaskSpecification& specification)
    {
        if (definition == nullptr)
            return false;

        ComPtr<ITriggerCollection> triggers;
        HRESULT hr = definition->get_Triggers(triggers.GetAddressOf());
        if (FAILED(hr) || triggers.Get() == nullptr)
            return false;

        LONG trigger_count{};
        hr = triggers->get_Count(&trigger_count);
        if (FAILED(hr) || trigger_count != 1)
            return false;

        _variant_t trigger_index(1L);
        ComPtr<ITrigger> trigger;
        hr = triggers->get_Item(trigger_index, trigger.GetAddressOf());
        if (FAILED(hr) || trigger.Get() == nullptr)
            return false;

        TASK_TRIGGER_TYPE2 trigger_type{};
        hr = trigger->get_Type(&trigger_type);
        if (FAILED(hr) || trigger_type != TASK_TRIGGER_LOGON)
            return false;

        VARIANT_BOOL trigger_enabled = VARIANT_FALSE;
        hr = trigger->get_Enabled(&trigger_enabled);
        if (FAILED(hr) || trigger_enabled != VARIANT_TRUE)
            return false;

        ComPtr<ILogonTrigger> logon_trigger;
        hr = trigger->QueryInterface(IID_ILogonTrigger,
            reinterpret_cast<void**>(logon_trigger.GetAddressOf()));
        if (FAILED(hr) || logon_trigger.Get() == nullptr)
            return false;

        ScopedBstr trigger_user_id;
        hr = logon_trigger->get_UserId(trigger_user_id.Receive());
        std::wstring actual_trigger_user_id;
        if (FAILED(hr) || !trigger_user_id.TryToWString(actual_trigger_user_id) ||
            !UserIdentifierMatchesCurrentUser(actual_trigger_user_id,
            reinterpret_cast<PSID>(const_cast<BYTE*>(specification.user.sid.data()))))
        {
            return false;
        }

        return true;
    }

    bool DoesTaskMatchSpecification(IRegisteredTask* registered_task,
        const AutorunTaskSpecification& specification, bool require_enabled,
        bool allow_legacy_empty_working_directory)
    {
        if (registered_task == nullptr)
            return false;

        VARIANT_BOOL task_enabled = VARIANT_FALSE;
        HRESULT hr = registered_task->get_Enabled(&task_enabled);
        if (FAILED(hr) || (require_enabled && task_enabled != VARIANT_TRUE))
            return false;

        ComPtr<ITaskDefinition> definition;
        hr = registered_task->get_Definition(definition.GetAddressOf());
        if (FAILED(hr) || definition.Get() == nullptr)
            return false;

        return DoesTaskHaveExpectedAction(definition.Get(), specification, allow_legacy_empty_working_directory) &&
            DoesTaskHaveExpectedPrincipal(definition.Get(), specification) &&
            DoesTaskHaveExpectedTrigger(definition.Get(), specification);
    }

    bool DeleteExistingManagedTasks(ITaskFolder* folder, const AutorunTaskSpecification& specification)
    {
        if (folder == nullptr)
            return false;

        ComPtr<IRegisteredTask> current_task;
        const ExistingTaskResult current_result =
            GetExistingTask(folder, specification.user.task_name, current_task);
        if (current_result == ExistingTaskResult::Failure)
            return false;
        if (current_result == ExistingTaskResult::Found &&
            !DoesTaskMatchSpecification(current_task.Get(), specification, false, false))
        {
            return false;
        }

        ComPtr<IRegisteredTask> legacy_task;
        const ExistingTaskResult legacy_result =
            GetExistingTask(folder, specification.user.legacy_task_name, legacy_task);
        if (legacy_result == ExistingTaskResult::Failure)
            return false;
        if (legacy_result == ExistingTaskResult::Found &&
            // Do not delete a task merely because its old account-only name
            // matches. The current token SID, action, principal, and trigger
            // must prove ownership first. The empty working-directory
            // exception covers only the prior TaskbarMon task format.
            !DoesTaskMatchSpecification(legacy_task.Get(), specification, false, true))
        {
            return false;
        }

        // Validate every candidate before deleting either one. This avoids
        // removing a verified current task when an untrusted legacy-name task
        // is present in the same folder.
        if (legacy_result == ExistingTaskResult::Found)
        {
            const HRESULT legacy_delete_hr =
                folder->DeleteTask(_bstr_t(specification.user.legacy_task_name.c_str()), 0);
            if (FAILED(legacy_delete_hr))
                return false;
        }

        if (current_result == ExistingTaskResult::Found)
        {
            const HRESULT current_delete_hr =
                folder->DeleteTask(_bstr_t(specification.user.task_name.c_str()), 0);
            if (FAILED(current_delete_hr))
                return false;
        }

        return true;
    }

    bool CreateManagedTask(ITaskService* service, ITaskFolder* folder,
        const AutorunTaskSpecification& specification)
    {
        if (service == nullptr || folder == nullptr)
            return false;

        ComPtr<ITaskDefinition> task;
        HRESULT hr = service->NewTask(0, task.GetAddressOf());
        if (FAILED(hr) || task.Get() == nullptr)
            return false;

        {
            ComPtr<IRegistrationInfo> registration_info;
            hr = task->get_RegistrationInfo(registration_info.GetAddressOf());
            if (FAILED(hr) || registration_info.Get() == nullptr)
                return false;
            hr = registration_info->put_Author(_bstr_t(specification.user.account_name.c_str()));
            if (FAILED(hr))
                return false;
        }

        {
            ComPtr<ITaskSettings> settings;
            hr = task->get_Settings(settings.GetAddressOf());
            if (FAILED(hr) || settings.Get() == nullptr)
                return false;
            hr = settings->put_Enabled(VARIANT_TRUE);
            if (FAILED(hr))
                return false;
            hr = settings->put_StartWhenAvailable(VARIANT_FALSE);
            if (FAILED(hr))
                return false;
            hr = settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
            if (FAILED(hr))
                return false;
            hr = settings->put_ExecutionTimeLimit(_bstr_t(L"PT0S"));
            if (FAILED(hr))
                return false;
            hr = settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
            if (FAILED(hr))
                return false;
        }

        {
            ComPtr<ITriggerCollection> trigger_collection;
            hr = task->get_Triggers(trigger_collection.GetAddressOf());
            if (FAILED(hr) || trigger_collection.Get() == nullptr)
                return false;

            ComPtr<ITrigger> trigger;
            hr = trigger_collection->Create(TASK_TRIGGER_LOGON, trigger.GetAddressOf());
            if (FAILED(hr) || trigger.Get() == nullptr)
                return false;

            ComPtr<ILogonTrigger> logon_trigger;
            hr = trigger->QueryInterface(IID_ILogonTrigger,
                reinterpret_cast<void**>(logon_trigger.GetAddressOf()));
            if (FAILED(hr) || logon_trigger.Get() == nullptr)
                return false;

            hr = logon_trigger->put_Id(_bstr_t(TASKBAR_MON_TRIGGER_ID));
            if (FAILED(hr))
                return false;
            hr = logon_trigger->put_Enabled(VARIANT_TRUE);
            if (FAILED(hr))
                return false;
            hr = logon_trigger->put_Delay(_bstr_t(TASKBAR_MON_TRIGGER_DELAY));
            if (FAILED(hr))
                return false;
            hr = logon_trigger->put_UserId(_bstr_t(specification.user.account_name.c_str()));
            if (FAILED(hr))
                return false;
        }

        {
            ComPtr<IActionCollection> action_collection;
            hr = task->get_Actions(action_collection.GetAddressOf());
            if (FAILED(hr) || action_collection.Get() == nullptr)
                return false;

            ComPtr<IAction> action;
            hr = action_collection->Create(TASK_ACTION_EXEC, action.GetAddressOf());
            if (FAILED(hr) || action.Get() == nullptr)
                return false;

            ComPtr<IExecAction> executable_action;
            hr = action->QueryInterface(IID_IExecAction,
                reinterpret_cast<void**>(executable_action.GetAddressOf()));
            if (FAILED(hr) || executable_action.Get() == nullptr)
                return false;

            hr = executable_action->put_Path(_bstr_t(specification.executable_path.c_str()));
            if (FAILED(hr))
                return false;
            hr = executable_action->put_Arguments(_bstr_t(L""));
            if (FAILED(hr))
                return false;
            hr = executable_action->put_WorkingDirectory(_bstr_t(specification.working_directory.c_str()));
            if (FAILED(hr))
                return false;
        }

        {
            ComPtr<IPrincipal> principal;
            hr = task->get_Principal(principal.GetAddressOf());
            if (FAILED(hr) || principal.Get() == nullptr)
                return false;
            hr = principal->put_Id(_bstr_t(TASKBAR_MON_PRINCIPAL_ID));
            if (FAILED(hr))
                return false;
            hr = principal->put_UserId(_bstr_t(specification.user.account_name.c_str()));
            if (FAILED(hr))
                return false;
            hr = principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
            if (FAILED(hr))
                return false;
            hr = principal->put_RunLevel(TASK_RUNLEVEL_LUA);
            if (FAILED(hr))
                return false;
        }

        ComPtr<IRegisteredTask> registered_task;
        // We remove only a validated managed task before reaching this point.
        // TASK_CREATE avoids overwriting a same-name task inserted in a race.
        hr = folder->RegisterTaskDefinition(
            _bstr_t(specification.user.task_name.c_str()),
            task.Get(),
            TASK_CREATE,
            _variant_t(specification.user.account_name.c_str()),
            _variant_t(),
            TASK_LOGON_INTERACTIVE_TOKEN,
            _variant_t(),
            registered_task.GetAddressOf());
        if (FAILED(hr) || registered_task.Get() == nullptr)
            return false;

        if (DoesTaskMatchSpecification(registered_task.Get(), specification, true, false))
            return true;

        // A registration that cannot be read back as the intended definition
        // must not remain enabled as an unverified autorun entry.
        const HRESULT disable_hr = registered_task->put_Enabled(VARIANT_FALSE);
        const HRESULT delete_hr = folder->DeleteTask(_bstr_t(specification.user.task_name.c_str()), 0);
        if (FAILED(disable_hr) || FAILED(delete_hr))
            return false;
        return false;
    }
}

bool create_auto_start_task_for_this_user()
{
    AutorunTaskSpecification specification;
    if (!BuildTaskSpecification(specification))
        return false;

    ScopedComApartment com_apartment;
    if (FAILED(com_apartment.Result()))
        return false;

    ComPtr<ITaskService> service;
    if (!ConnectTaskService(service))
        return false;

    ComPtr<ITaskFolder> folder;
    if (GetTaskbarMonFolder(service.Get(), true, folder) != TaskFolderResult::Found)
        return false;

    if (!DeleteExistingManagedTasks(folder.Get(), specification))
        return false;

    return CreateManagedTask(service.Get(), folder.Get(), specification);
}

bool delete_auto_start_task_for_this_user()
{
    AutorunTaskSpecification specification;
    if (!BuildTaskSpecification(specification))
        return false;

    ScopedComApartment com_apartment;
    if (FAILED(com_apartment.Result()))
        return false;

    ComPtr<ITaskService> service;
    if (!ConnectTaskService(service))
        return false;

    ComPtr<ITaskFolder> folder;
    const TaskFolderResult folder_result = GetTaskbarMonFolder(service.Get(), false, folder);
    if (folder_result == TaskFolderResult::Missing)
        return true;
    if (folder_result != TaskFolderResult::Found)
        return false;

    return DeleteExistingManagedTasks(folder.Get(), specification);
}

bool is_auto_start_task_active_for_this_user(std::wstring* path)
{
    if (path != nullptr)
        path->clear();

    AutorunTaskSpecification specification;
    if (!BuildTaskSpecification(specification))
        return false;

    ScopedComApartment com_apartment;
    if (FAILED(com_apartment.Result()))
        return false;

    ComPtr<ITaskService> service;
    if (!ConnectTaskService(service))
        return false;

    ComPtr<ITaskFolder> folder;
    if (GetTaskbarMonFolder(service.Get(), false, folder) != TaskFolderResult::Found)
        return false;

    ComPtr<IRegisteredTask> task;
    if (GetExistingTask(folder.Get(), specification.user.task_name, task) != ExistingTaskResult::Found)
        return false;

    if (!DoesTaskMatchSpecification(task.Get(), specification, true, false))
        return false;

    if (path != nullptr)
        *path = specification.executable_path;
    return true;
}
