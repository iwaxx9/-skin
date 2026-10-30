#pragma once

#include <Windows.h>
#include <mutex>

inline std::once_flag change_skins;
inline WNDPROC originalWndProc;

class Hooks {
public:
	void init() noexcept;
	[[nodiscard]] bool install() noexcept;
	void uninstall() noexcept;
};
