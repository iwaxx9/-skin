#pragma once

#include <Windows.h>
#include <TlHelp32.h>
#include <winsvc.h>

#include <string>
#include <vector>

struct RuntimeCompatibilityBoundary {
	bool injectionAllowed{ true };
	std::string reason{ "No active Vanguard service or process detected." };
	std::vector<std::string> evidence;
};

inline bool isWindowsServiceActive(const wchar_t* serviceName) noexcept
{
	const auto manager{ ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT) };
	if (!manager)
		return false;

	const auto service{ ::OpenServiceW(manager, serviceName, SERVICE_QUERY_STATUS) };
	if (!service) {
		::CloseServiceHandle(manager);
		return false;
	}

	SERVICE_STATUS_PROCESS status{};
	DWORD bytesNeeded{};
	const auto queryResult{ ::QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded) };

	::CloseServiceHandle(service);
	::CloseServiceHandle(manager);

	if (!queryResult)
		return false;

	return status.dwCurrentState == SERVICE_RUNNING
		|| status.dwCurrentState == SERVICE_START_PENDING
		|| status.dwCurrentState == SERVICE_CONTINUE_PENDING;
}

inline bool isProcessActive(const wchar_t* processName) noexcept
{
	const auto snapshot{ ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
	if (snapshot == INVALID_HANDLE_VALUE)
		return false;

	PROCESSENTRY32W entry{};
	entry.dwSize = sizeof(entry);

	auto found{ false };
	if (::Process32FirstW(snapshot, &entry)) {
		do {
			if (::_wcsicmp(entry.szExeFile, processName) == 0) {
				found = true;
				break;
			}
		} while (::Process32NextW(snapshot, &entry));
	}

	::CloseHandle(snapshot);
	return found;
}

inline RuntimeCompatibilityBoundary detectRuntimeCompatibilityBoundary() noexcept
{
	RuntimeCompatibilityBoundary boundary{};

	if (isWindowsServiceActive(L"vgk")) {
		boundary.evidence.emplace_back("Windows service vgk is active.");
		boundary.injectionAllowed = false;
	}

	if (isWindowsServiceActive(L"vgc")) {
		boundary.evidence.emplace_back("Windows service vgc is active.");
		boundary.injectionAllowed = false;
	}

	if (isProcessActive(L"vgc.exe")) {
		boundary.evidence.emplace_back("Process vgc.exe is active.");
		boundary.injectionAllowed = false;
	}

	if (!boundary.injectionAllowed) {
		boundary.reason = "Vanguard is active. Runtime injection is disabled by the compatibility boundary.";
	}

	return boundary;
}
