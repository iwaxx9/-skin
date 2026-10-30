#pragma warning(disable : 6387 4715)

#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <clocale>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include "CheatManager.hpp"

#include "Config.hpp"
#include "Hooks.hpp"
#include "Memory.hpp"
#include "RuntimeProfile.hpp"

#include "SDK/GameState.hpp"

namespace {
	constexpr auto gameClientTimeout{ std::chrono::seconds(120) };
	constexpr auto gameStateTimeout{ std::chrono::seconds(120) };
	constexpr auto signatureTimeout{ std::chrono::seconds(15) };

	std::string summarizeMissing(const SignatureScanReport& report)
	{
		std::ostringstream stream;
		const auto maximumEntries{ std::min<std::size_t>(report.missing.size(), 8u) };
		for (auto index{ 0u }; index < maximumEntries; ++index) {
			if (index)
				stream << ", ";
			stream << report.missing[index];
		}

		if (report.missing.size() > maximumEntries)
			stream << " (+" << report.missing.size() - maximumEntries << " more)";
		return stream.str();
	}

	void advanceStage(RuntimeProfile& profile, const StartupStage stage, std::string detail = {})
	{
		setStartupStage(profile, stage, std::move(detail));
		(void)writeCompatibilityReport(profile);
		cheatManager.logger->addLog("Compatibility stage: %s%s%s\n",
			toString(stage),
			profile.stageDetail.empty() ? "" : " - ",
			profile.stageDetail.c_str());
	}

	[[noreturn]] void failStartup(RuntimeProfile& profile, const StartupStage failedStage, std::string reason, const HMODULE module) noexcept
	{
		markRuntimeFailure(profile, failedStage, std::move(reason));
		(void)writeCompatibilityReport(profile);

		cheatManager.logger->addLog("Startup failed at %s: %s\n", profile.failureStage.c_str(), profile.failureReason.c_str());

		std::string message{ "R3nzSkin compatibility check failed.\n\nStage: " };
		message += profile.failureStage;
		message += "\nReason: ";
		message += profile.failureReason;
		if (!profile.diagnosticPath.empty()) {
			message += "\n\nDiagnostics: ";
			message += profile.diagnosticPath;
		}
		message += "\n\nThe game process will not be modified.";

		::MessageBoxA(nullptr, message.c_str(), "R3nzSkin compatibility", MB_OK | MB_ICONERROR);
		::FreeLibraryAndExitThread(module, EXIT_FAILURE);
	}
}

__declspec(safebuffers) static void WINAPI DllAttach(LPVOID parameter) noexcept
{
	using namespace std::chrono_literals;

	const auto module{ static_cast<HMODULE>(parameter) };
	cheatManager.start();
	auto& profile{ *cheatManager.profile };

	advanceStage(profile, StartupStage::ProfileReady, "Build and client metadata collected.");

	profile.boundary = detectRuntimeCompatibilityBoundary();
	if (!profile.boundary.injectionAllowed)
		failStartup(profile, StartupStage::BoundaryChecked, profile.boundary.reason, module);
	advanceStage(profile, StartupStage::BoundaryChecked, profile.boundary.reason);

	advanceStage(profile, StartupStage::SearchingGameClient, "Waiting for the game client module.");
	const auto gameClientDeadline{ std::chrono::steady_clock::now() + gameClientTimeout };
	auto gameClientFound{ false };
	while (std::chrono::steady_clock::now() < gameClientDeadline) {
		if (cheatManager.memory->Search(true)) {
			gameClientFound = true;
			break;
		}
		std::this_thread::sleep_for(500ms);
	}

	if (!gameClientFound || !cheatManager.memory->client) {
		const auto missing{ summarizeMissing(profile.gameClientSignatures) };
		failStartup(profile, StartupStage::SearchingGameClient,
			"GameClient was not resolved before timeout. Missing signatures: " + (missing.empty() ? std::string("unknown") : missing),
			module);
	}
	advanceStage(profile, StartupStage::GameClientReady, "GameClient resolved.");

	advanceStage(profile, StartupStage::WaitingForGame, "Waiting for game state Running.");
	const auto gameStateDeadline{ std::chrono::steady_clock::now() + gameStateTimeout };
	while (std::chrono::steady_clock::now() < gameStateDeadline) {
		if (!cheatManager.memory->client)
			break;
		if (cheatManager.memory->client->game_state == GGameState_s::Running)
			break;
		std::this_thread::sleep_for(250ms);
	}

	if (!cheatManager.memory->client || cheatManager.memory->client->game_state != GGameState_s::Running)
		failStartup(profile, StartupStage::WaitingForGame, "The game did not reach GGameState_s::Running before timeout.", module);

	advanceStage(profile, StartupStage::ResolvingSignatures, "Resolving all required runtime signatures.");
	const auto signatureDeadline{ std::chrono::steady_clock::now() + signatureTimeout };
	auto signaturesResolved{ false };
	while (std::chrono::steady_clock::now() < signatureDeadline) {
		if (cheatManager.memory->Search(false)) {
			signaturesResolved = true;
			break;
		}
		std::this_thread::sleep_for(1s);
	}

	if (!signaturesResolved) {
		const auto missing{ summarizeMissing(profile.runtimeSignatures) };
		failStartup(profile, StartupStage::ResolvingSignatures,
			"Runtime signatures were not resolved before timeout. Missing signatures: " + (missing.empty() ? std::string("unknown") : missing),
			module);
	}
	advanceStage(profile, StartupStage::SignaturesResolved, "All runtime signatures resolved.");

	cheatManager.config->init();
	cheatManager.config->load();
	advanceStage(profile, StartupStage::ConfigLoaded, "Configuration loaded.");

	if (!cheatManager.hooks->install())
		failStartup(profile, StartupStage::HooksInstalled, "DXGI hook installation failed. Verify that the game is using DirectX 11.", module);
	advanceStage(profile, StartupStage::HooksInstalled, "DXGI present and resize hooks installed.");

	while (cheatManager.cheatState)
		std::this_thread::sleep_for(250ms);

	::FreeLibraryAndExitThread(module, EXIT_SUCCESS);
}

__declspec(safebuffers) BOOL APIENTRY DllMain(const HMODULE hModule, const DWORD reason, [[maybe_unused]] LPVOID reserved)
{
	DisableThreadLibraryCalls(hModule);

	if (reason != DLL_PROCESS_ATTACH)
		return FALSE;

	std::setlocale(LC_ALL, ".utf8");

	const auto thread{ reinterpret_cast<HANDLE>(::_beginthreadex(nullptr, 0u, reinterpret_cast<_beginthreadex_proc_type>(DllAttach), hModule, 0u, nullptr)) };
	if (!thread)
		return FALSE;

	::CloseHandle(thread);
	return TRUE;
}
