#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <taskschd.h>
#include <filesystem>
#include <string>
#include <vector>
#include "AutoStart.h"

#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "taskschd.lib")
#endif

namespace {
constexpr wchar_t kTaskPrefix[] = L"MiniPulse.Logon.";
constexpr wchar_t kLegacyTask[] = L"MiniPulse";
constexpr wchar_t kTaskDescription[] = L"MiniPulse managed logon task";
constexpr HRESULT kUnsafePath = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x201);
constexpr HRESULT kWrongAccount = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x202);
constexpr HRESULT kTaskConflict = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x203);

void Check(HRESULT hr) {
    if (FAILED(hr)) throw hr;
}

void CheckWin32(BOOL ok) {
    if (ok) return;
    const DWORD error = GetLastError();
    throw error ? HRESULT_FROM_WIN32(error) : E_FAIL;
}

template<class T> struct ComPtr {
    T* value = nullptr;
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ~ComPtr() { if (value) value->Release(); }
    T* operator->() const { return value; }
    T** out() { return &value; }
    explicit operator bool() const { return value != nullptr; }
};

struct BString {
    BSTR value = nullptr;
    BString() = default;
    explicit BString(const std::wstring& text) : value(SysAllocString(text.c_str())) {
        if (!value) throw E_OUTOFMEMORY;
    }
    BString(const BString&) = delete;
    BString& operator=(const BString&) = delete;
    ~BString() { SysFreeString(value); }
    operator BSTR() const { return value; }
    std::wstring str() const { return value ? value : L""; }
};

struct ComScope {
    ComScope() { Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }
    ~ComScope() { CoUninitialize(); }
};

struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value) CloseHandle(value); }
};

struct LocalMemory {
    HLOCAL value = nullptr;
    ~LocalMemory() { LocalFree(value); }
};

bool Equal(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}

std::wstring SidText(PSID sid) {
    LPWSTR text = nullptr;
    CheckWin32(ConvertSidToStringSidW(sid, &text));
    LocalMemory memory{text};
    return text;
}

std::wstring ProcessUser(HANDLE process) {
    Handle token;
    CheckWin32(OpenProcessToken(process, TOKEN_QUERY, &token.value));
    DWORD size = 0;
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
    if (!size) Check(HRESULT_FROM_WIN32(GetLastError()));
    std::vector<BYTE> buffer(size);
    CheckWin32(GetTokenInformation(token.value, TokenUser, buffer.data(), size, &size));
    return SidText(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid);
}

void RequireDesktopUser(const std::wstring& user) {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetShellWindow(), &pid);
    Handle shell{pid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : nullptr};
    if (!shell.value || !Equal(ProcessUser(shell.value), user)) throw kWrongAccount;
}

bool SameUser(const std::wstring& account, const std::wstring& sid) {
    if (Equal(account, sid)) return true;
    if (account.empty()) return false;
    DWORD bytes = 0, chars = 0;
    SID_NAME_USE type;
    LookupAccountNameW(nullptr, account.c_str(), nullptr, &bytes, nullptr, &chars, &type);
    if (!bytes) return false;
    std::vector<BYTE> candidate(bytes);
    std::vector<wchar_t> domain(chars);
    return LookupAccountNameW(nullptr, account.c_str(), candidate.data(), &bytes, domain.data(), &chars, &type) &&
           Equal(SidText(candidate.data()), sid);
}

namespace fs = std::filesystem;

fs::path ExecutablePath() {
    wchar_t path[32768];
    const DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (!length || length == ARRAYSIZE(path)) throw HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME);
    return fs::path(path).lexically_normal();
}

bool TrustedSid(PSID sid) {
    return IsWellKnownSid(sid, WinLocalSystemSid) || IsWellKnownSid(sid, WinBuiltinAdministratorsSid) ||
           SidText(sid) == L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464"; // TrustedInstaller
}

void RequireProtectedObject(const fs::path& path, DWORD writeMask) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) throw kUnsafePath;
    PSID owner = nullptr;
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD info = GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr, &acl, nullptr, &descriptor);
    LocalMemory memory{descriptor};
    if (info != ERROR_SUCCESS || !owner || !acl || !TrustedSid(owner)) throw kUnsafePath;
    for (DWORD i = 0; i < acl->AceCount; ++i) {
        void* entry = nullptr;
        if (!GetAce(acl, i, &entry)) throw kUnsafePath;
        const auto* header = static_cast<const ACE_HEADER*>(entry);
        if (header->AceFlags & INHERIT_ONLY_ACE) continue;
        if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) throw kUnsafePath;
        auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(entry);
        if ((ace->Mask & writeMask) && !TrustedSid(&ace->SidStart)) throw kUnsafePath;
    }
}

void RequireProtectedExecutable(const fs::path& exe) {
    const auto root = exe.root_path();
    if (root.empty() || GetDriveTypeW(root.c_str()) != DRIVE_FIXED ||
        exe.wstring().find(L'%') != std::wstring::npos) throw kUnsafePath;
    constexpr DWORD control = WRITE_DAC | WRITE_OWNER | DELETE | GENERIC_ALL;
    constexpr DWORD write = control | GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA |
        FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD;
    const auto directory = exe.parent_path();
    RequireProtectedObject(exe, write);
    RequireProtectedObject(directory, write);
    for (auto parent = directory.parent_path(); !parent.empty(); parent = parent.parent_path()) {
        RequireProtectedObject(parent, control | FILE_DELETE_CHILD);
        if (parent == root) break;
    }
    // These directories contain DLLs that are loaded by the elevated processes.
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (Equal(entry.path().extension().wstring(), L".dll")) RequireProtectedObject(entry.path(), write);
    }
    const auto sensors = directory / L"sensors";
    if (fs::exists(sensors)) {
        RequireProtectedObject(sensors, write);
        for (const auto& entry : fs::recursive_directory_iterator(sensors)) RequireProtectedObject(entry.path(), write);
    }
}

std::wstring XmlEscape(const std::wstring& value) {
    std::wstring xml;
    for (auto c : value) {
        if (c == L'&') xml += L"&amp;";
        else if (c == L'<') xml += L"&lt;";
        else if (c == L'>') xml += L"&gt;";
        else xml += c;
    }
    return xml;
}

void ReadTask(ITaskFolder* folder, const std::wstring& name, ComPtr<IRegisteredTask>& task) {
    const HRESULT hr = folder->GetTask(BString(name), task.out());
    if (hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && hr != HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) Check(hr);
}

struct TaskStatus {
    bool owned = false;
    bool enabled = false;
    bool valid = false;
};

bool IsMissingMiniPulse(const fs::path& path) {
    if (!path.is_absolute() || !Equal(path.filename().wstring(), L"MiniPulse.exe") ||
        GetDriveTypeW(path.root_path().c_str()) != DRIVE_FIXED) return false;
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return false;
    const DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

TaskStatus Inspect(IRegisteredTask* task, const std::wstring& name, const fs::path& exe,
                   const std::wstring& user, bool legacy, bool repairMissingTarget = false) {
    if (!task) return {};
    ComPtr<ITaskDefinition> definition;
    ComPtr<IPrincipal> principal;
    ComPtr<IActionCollection> actions;
    Check(task->get_Definition(definition.out()));
    Check(definition->get_Principal(principal.out()));
    BString principalUser;
    Check(principal->get_UserId(&principalUser.value));
    if (!user.empty() && !SameUser(principalUser.str(), user)) return {};
    if (!legacy) {
        ComPtr<IRegistrationInfo> registration;
        BString description;
        Check(definition->get_RegistrationInfo(registration.out()));
        Check(registration->get_Description(&description.value));
        if (description.str() != kTaskDescription ||
            !SameUser(principalUser.str(), name.substr(std::size(kTaskPrefix) - 1))) return {};
    }
    Check(definition->get_Actions(actions.out()));
    LONG count = 0;
    Check(actions->get_Count(&count));
    if (count != 1) return {};
    ComPtr<IAction> action;
    TASK_ACTION_TYPE type;
    Check(actions->get_Item(1, action.out()));
    Check(action->get_Type(&type));
    if (type != TASK_ACTION_EXEC) return {};
    ComPtr<IExecAction> exec;
    Check(action->QueryInterface(IID_IExecAction, reinterpret_cast<void**>(exec.out())));
    BString path, args, workingDir;
    Check(exec->get_Path(&path.value));
    Check(exec->get_Arguments(&args.value));
    Check(exec->get_WorkingDirectory(&workingDir.value));
    auto target = path.str();
    if (target.size() >= 2 && target.front() == L'"' && target.back() == L'"') {
        target = target.substr(1, target.size() - 2);
    }
    const auto targetPath = fs::path(target).lexically_normal();
    const bool samePath = exe.empty() ? Equal(targetPath.filename().wstring(), L"MiniPulse.exe") :
        Equal(targetPath.wstring(), exe.wstring());
    if (!samePath && !(repairMissingTarget && !legacy && IsMissingMiniPulse(targetPath))) return {};
    if (legacy && args.str() != L"--enable-sensors") return {};

    VARIANT_BOOL enabled;
    Check(task->get_Enabled(&enabled));
    ComPtr<ITriggerCollection> triggers;
    Check(definition->get_Triggers(triggers.out()));
    Check(triggers->get_Count(&count));
    if (count != 1) return {!legacy, enabled != VARIANT_FALSE, false};
    ComPtr<ITrigger> trigger;
    TASK_TRIGGER_TYPE2 triggerType;
    Check(triggers->get_Item(1, trigger.out()));
    Check(trigger->get_Type(&triggerType));
    if (triggerType != TASK_TRIGGER_LOGON) return {!legacy, enabled != VARIANT_FALSE, false};
    ComPtr<ILogonTrigger> logon;
    Check(trigger->QueryInterface(IID_ILogonTrigger, reinterpret_cast<void**>(logon.out())));
    VARIANT_BOOL triggerEnabled;
    BString triggerUser, endBoundary, triggerLimit;
    Check(logon->get_Enabled(&triggerEnabled));
    Check(logon->get_UserId(&triggerUser.value));
    Check(logon->get_EndBoundary(&endBoundary.value));
    if (legacy) return {true, enabled != VARIANT_FALSE && triggerEnabled != VARIANT_FALSE, false};
    Check(logon->get_ExecutionTimeLimit(&triggerLimit.value));

    TASK_LOGON_TYPE logonType;
    TASK_RUNLEVEL_TYPE runLevel;
    Check(principal->get_LogonType(&logonType));
    Check(principal->get_RunLevel(&runLevel));
    ComPtr<ITaskSettings> settings;
    BString timeLimit;
    VARIANT_BOOL disallowBattery, stopOnBattery;
    TASK_INSTANCES_POLICY instances;
    Check(definition->get_Settings(settings.out()));
    Check(settings->get_ExecutionTimeLimit(&timeLimit.value));
    Check(settings->get_DisallowStartIfOnBatteries(&disallowBattery));
    Check(settings->get_StopIfGoingOnBatteries(&stopOnBattery));
    Check(settings->get_MultipleInstances(&instances));
    const bool valid = samePath && logonType == TASK_LOGON_INTERACTIVE_TOKEN && runLevel == TASK_RUNLEVEL_HIGHEST &&
        triggerEnabled != VARIANT_FALSE && SameUser(triggerUser.str(), user) && endBoundary.str().empty() &&
        triggerLimit.str() == L"PT0S" && timeLimit.str() == L"PT0S" &&
        disallowBattery == VARIANT_FALSE && stopOnBattery == VARIANT_FALSE &&
        instances == TASK_INSTANCES_IGNORE_NEW && args.str().empty() &&
        Equal(workingDir.str(), exe.parent_path().wstring());
    return {true, enabled != VARIANT_FALSE, valid};
}

std::wstring TaskXml(const std::wstring& user, const fs::path& exe) {
    return
        L"<Task xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\" version=\"1.2\">"
        L"<RegistrationInfo><Description>" + std::wstring(kTaskDescription) + L"</Description></RegistrationInfo>"
        L"<Triggers><LogonTrigger><ExecutionTimeLimit>PT0S</ExecutionTimeLimit><UserId>" + user +
        L"</UserId></LogonTrigger></Triggers>"
        L"<Principals><Principal><UserId>" + user +
        L"</UserId><LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>"
        L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
        L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
        L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries><ExecutionTimeLimit>PT0S</ExecutionTimeLimit></Settings>"
        L"<Actions><Exec><Command>" + XmlEscape(exe.wstring()) + L"</Command><WorkingDirectory>" +
        XmlEscape(exe.parent_path().wstring()) + L"</WorkingDirectory></Exec></Actions></Task>";
}

void Register(ITaskFolder* folder, const std::wstring& name, const std::wstring& user,
              const fs::path& exe, bool replace) {
    RequireProtectedExecutable(exe);
    VARIANT empty{}, principal{};
    BString sid(user);
    principal.vt = VT_BSTR;
    principal.bstrVal = sid.value;
    ComPtr<IRegisteredTask> registered;
    Check(folder->RegisterTask(BString(name), BString(TaskXml(user, exe)),
        (replace ? TASK_UPDATE : TASK_CREATE) | TASK_IGNORE_REGISTRATION_TRIGGERS,
        principal, empty, TASK_LOGON_INTERACTIVE_TOKEN, empty, registered.out()));
}

void DeleteOwnedTasks(ITaskFolder* folder, const fs::path& exe) {
    ComPtr<IRegisteredTaskCollection> tasks;
    Check(folder->GetTasks(TASK_ENUM_HIDDEN, tasks.out()));
    LONG count;
    Check(tasks->get_Count(&count));
    HRESULT firstError = S_OK;
    for (LONG i = 1; i <= count; ++i) {
        VARIANT index{};
        index.vt = VT_I4;
        index.lVal = i;
        ComPtr<IRegisteredTask> task;
        BString taskName;
        try {
            Check(tasks->get_Item(index, task.out()));
            Check(task->get_Name(&taskName.value));
            const auto name = taskName.str();
            const bool legacy = Equal(name, kLegacyTask);
            if (!legacy && name.rfind(kTaskPrefix, 0) != 0) continue;
            // Inspect the live definition immediately before deleting.
            ComPtr<IRegisteredTask> live;
            ReadTask(folder, name, live);
            if (Inspect(live.value, name, exe, L"", legacy).owned) Check(folder->DeleteTask(taskName, 0));
        } catch (HRESULT error) {
            if (SUCCEEDED(firstError)) firstError = error;
        }
    }
    Check(firstError);
}
}

AutoStartResult ManageAutoStart(AutoStartAction action) {
    using State = AutoStartState;
    try {
        ComScope com;
        ComPtr<ITaskService> service;
        Check(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
            IID_ITaskService, reinterpret_cast<void**>(service.out())));
        VARIANT empty{};
        Check(service->Connect(empty, empty, empty, empty));
        ComPtr<ITaskFolder> folder;
        Check(service->GetFolder(BString(L"\\"), folder.out()));
        const auto exe = ExecutablePath();
        if (action == AutoStartAction::Uninstall) {
            DeleteOwnedTasks(folder.value, exe);
            return {State::Off};
        }

        const auto user = ProcessUser(GetCurrentProcess());
        if (action != AutoStartAction::Maintain) RequireDesktopUser(user);
        const auto name = kTaskPrefix + user;
        ComPtr<IRegisteredTask> task, legacy;
        ReadTask(folder.value, name, task);
        const auto current = Inspect(task.value, name, exe, user, false, action != AutoStartAction::Maintain);
        ReadTask(folder.value, kLegacyTask, legacy);
        const auto old = Inspect(legacy.value, kLegacyTask, exe, user, true);
        const bool legacyElsewhere = !old.owned && !current.owned &&
            Inspect(legacy.value, kLegacyTask, {}, user, true).enabled;
        const auto state = (task && !current.owned) || legacyElsewhere ? State::Conflict :
            current.enabled ? (current.valid ? State::On : State::NeedsRepair) :
            old.enabled ? State::Legacy : State::Off;
        if (action == AutoStartAction::Query) return {state, state == State::Conflict ? kTaskConflict : 0};
        if (action == AutoStartAction::Maintain && (state == State::Off || state == State::Conflict)) return {state};
        if (state == State::Conflict) return {state, kTaskConflict};
        if (action == AutoStartAction::Maintain) RequireDesktopUser(user);

        if (action == AutoStartAction::Disable) {
            if (old.owned) Check(folder->DeleteTask(BString(kLegacyTask), 0));
            if (current.owned) Check(folder->DeleteTask(BString(name), 0));
            ComPtr<IRegisteredTask> remaining, remainingLegacy;
            ReadTask(folder.value, name, remaining);
            ReadTask(folder.value, kLegacyTask, remainingLegacy);
            if (remaining || Inspect(remainingLegacy.value, kLegacyTask, exe, user, true).owned) throw E_FAIL;
            return {State::Off};
        }

        if (state != State::On) {
            Register(folder.value, name, user, exe, current.owned);
            ComPtr<IRegisteredTask> saved;
            ReadTask(folder.value, name, saved);
            const auto stored = Inspect(saved.value, name, exe, user, false);
            if (!stored.owned || !stored.valid || !stored.enabled) throw E_FAIL;
        }
        if (old.owned) {
            Check(folder->DeleteTask(BString(kLegacyTask), 0));
            ComPtr<IRegisteredTask> remainingLegacy;
            ReadTask(folder.value, kLegacyTask, remainingLegacy);
            if (Inspect(remainingLegacy.value, kLegacyTask, exe, user, true).owned) throw E_FAIL;
        }
        return {State::On};
    } catch (HRESULT error) {
        return {State::Error, error};
    } catch (const fs::filesystem_error&) {
        return {State::Error, kUnsafePath};
    }
}

const wchar_t* AutoStartErrorText(long error) {
    if (error == kUnsafePath) return L"登录自启需要受管理员保护的本地目录。请使用安装程序的默认目录，或检查当前目录及依赖文件的权限。";
    if (error == kWrongAccount) return L"请在当前登录的 Windows 管理员账户下设置自启。";
    if (error == kTaskConflict) return L"当前账户的自启任务已被其他位置的 MiniPulse 使用，或任务已被修改。请先从原位置关闭自启。";
    return L"Windows 未能确认登录自启设置。请确认任务计划程序服务可用，然后重试。";
}

int RunAutoStartCommand(const wchar_t* commandLine) {
    AutoStartAction action;
    if (Equal(commandLine, L"--autostart-enable")) action = AutoStartAction::Enable;
    else if (Equal(commandLine, L"--autostart-maintain")) action = AutoStartAction::Maintain;
    else if (Equal(commandLine, L"--autostart-uninstall")) action = AutoStartAction::Uninstall;
    else return -1;
    const auto result = ManageAutoStart(action);
    return result.error ? static_cast<int>(result.error) : 0;
}
