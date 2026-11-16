#pragma once

enum class AutoStartAction { Query, Enable, Disable, Maintain, Uninstall };
enum class AutoStartState { Unknown, Off, On, Basic, Legacy, NeedsRepair, Conflict, Error };
inline constexpr wchar_t kBasicStartupArgument[] = L"--startup-basic";

struct AutoStartResult {
    AutoStartState state;
    long error = 0;
};

AutoStartResult ManageAutoStart(AutoStartAction action);
int RunAutoStartCommand(const wchar_t* commandLine);
const wchar_t* AutoStartErrorText(long error);
