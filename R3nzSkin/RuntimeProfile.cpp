#include <Windows.h>
#include <ShlObj.h>

#include <cstddef>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

#pragma comment(lib, "version.lib")

#include "RuntimeProfile.hpp"

namespace {
	std::string readExecutableVersion() noexcept
	{
		wchar_t executablePath[MAX_PATH]{};
		if (!::GetModuleFileNameW(nullptr, executablePath, MAX_PATH))
			return "Unknown";

		DWORD ignored{};
		const auto versionSize{ ::GetFileVersionInfoSizeW(executablePath, &ignored) };
		if (!versionSize)
			return "Unknown";

		std::vector<std::byte> versionData(versionSize);
		if (!::GetFileVersionInfoW(executablePath, 0, versionSize, versionData.data()))
			return "Unknown";

		VS_FIXEDFILEINFO* fileInfo{};
		UINT fileInfoSize{};
		if (!::VerQueryValueW(versionData.data(), L"\\", reinterpret_cast<void**>(&fileInfo), &fileInfoSize) || !fileInfo)
			return "Unknown";

		std::ostringstream stream;
		stream << HIWORD(fileInfo->dwFileVersionMS) << '.'
			<< LOWORD(fileInfo->dwFileVersionMS) << '.'
			<< HIWORD(fileInfo->dwFileVersionLS) << '.'
			<< LOWORD(fileInfo->dwFileVersionLS);
		return stream.str();
	}

	std::string currentTimestamp()
	{
		const auto now{ std::time(nullptr) };
		tm localTime{};
		localtime_s(&localTime, &now);

		std::ostringstream stream;
		stream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
		return stream.str();
	}

	void appendSignatureReport(std::ostringstream& stream, const SignatureScanReport& report)
	{
		stream << "  attempted: " << (report.attempted ? "yes" : "no") << '\n';
		stream << "  success: " << (report.success ? "yes" : "no") << '\n';
		stream << "  signature set: " << report.signatureSet << '\n';

		for (const auto& entry : report.entries) {
			stream << "  - " << entry.name << ": " << (entry.found ? "found" : "missing") << '\n';
			if (entry.found) {
				stream << "    address: 0x" << std::hex << entry.address << std::dec << '\n';
				stream << "    pattern: " << entry.matchedPattern << '\n';
			}
		}
	}
}

const char* toString(const ServerRegion region) noexcept
{
	switch (region) {
	case ServerRegion::ChinaServer:
		return "ChinaServer";
	case ServerRegion::RiotGamesServers:
		return "RiotGamesServers";
	default:
		return "Unknown";
	}
}

const char* toString(const StartupStage stage) noexcept
{
	switch (stage) {
	case StartupStage::Created:
		return "Created";
	case StartupStage::ProfileReady:
		return "ProfileReady";
	case StartupStage::BoundaryChecked:
		return "BoundaryChecked";
	case StartupStage::SearchingGameClient:
		return "SearchingGameClient";
	case StartupStage::GameClientReady:
		return "GameClientReady";
	case StartupStage::WaitingForGame:
		return "WaitingForGame";
	case StartupStage::ResolvingSignatures:
		return "ResolvingSignatures";
	case StartupStage::SignaturesResolved:
		return "SignaturesResolved";
	case StartupStage::ConfigLoaded:
		return "ConfigLoaded";
	case StartupStage::HooksInstalled:
		return "HooksInstalled";
	case StartupStage::Running:
		return "Running";
	case StartupStage::Failed:
		return "Failed";
	default:
		return "Unknown";
	}
}

RuntimeProfile createRuntimeProfile() noexcept
{
	RuntimeProfile profile{};

#if defined(_CHINA)
	profile.serverRegion = ServerRegion::ChinaServer;
	profile.buildConfiguration = "ChinaServer";
	profile.signatureSet = "china-baseline-v1";
#elif defined(_RIOT)
	profile.serverRegion = ServerRegion::RiotGamesServers;
	profile.buildConfiguration = "RiotGamesServers";
	profile.signatureSet = "riot-baseline-v1";
#else
	profile.serverRegion = ServerRegion::Unknown;
	profile.buildConfiguration = "Unknown";
	profile.signatureSet = "unknown";
#endif

#if defined(_M_X64)
	profile.buildArchitecture = "x64";
#elif defined(_M_IX86)
	profile.buildArchitecture = "x86";
#else
	profile.buildArchitecture = "Unknown";
#endif

	profile.clientVersion = readExecutableVersion();
	profile.boundary = detectRuntimeCompatibilityBoundary();
	return profile;
}

void setStartupStage(RuntimeProfile& profile, const StartupStage stage, std::string detail) noexcept
{
	profile.stage = stage;
	profile.stageDetail = std::move(detail);
}

void markRuntimeFailure(RuntimeProfile& profile, const StartupStage failedStage, std::string reason) noexcept
{
	profile.failed = true;
	profile.stage = StartupStage::Failed;
	profile.failureStage = toString(failedStage);
	profile.failureReason = std::move(reason);
}

std::string buildCompatibilityReport(const RuntimeProfile& profile)
{
	std::ostringstream stream;
	stream << "R3nzSkin compatibility report\n";
	stream << "Generated: " << currentTimestamp() << "\n\n";
	stream << "Build configuration: " << profile.buildConfiguration << '\n';
	stream << "Build architecture: " << profile.buildArchitecture << '\n';
	stream << "Build timestamp: " << profile.buildTimestamp << '\n';
	stream << "Server region: " << toString(profile.serverRegion) << '\n';
	stream << "Client version: " << profile.clientVersion << '\n';
	stream << "Module base: 0x" << std::hex << profile.moduleBase << std::dec << '\n';
	stream << "Stage: " << toString(profile.stage);
	if (!profile.stageDetail.empty())
		stream << " (" << profile.stageDetail << ')';
	stream << '\n';
	stream << "Failure stage: " << (profile.failureStage.empty() ? "None" : profile.failureStage) << '\n';
	stream << "Failure reason: " << (profile.failureReason.empty() ? "None" : profile.failureReason) << "\n\n";

	stream << "Compatibility boundary: " << (profile.boundary.injectionAllowed ? "allowed" : "blocked") << '\n';
	stream << "Boundary reason: " << profile.boundary.reason << '\n';
	for (const auto& evidence : profile.boundary.evidence)
		stream << "Boundary evidence: " << evidence << '\n';

	stream << "\nRuntime features:\n";
	stream << "  localPlayerSkins: " << profile.features.localPlayerSkins << '\n';
	stream << "  otherChampionSkins: " << profile.features.otherChampionSkins << '\n';
	stream << "  minionSkins: " << profile.features.minionSkins << '\n';
	stream << "  wardSkins: " << profile.features.wardSkins << '\n';
	stream << "  turretSkins: " << profile.features.turretSkins << '\n';
	stream << "  jungleMobSkins: " << profile.features.jungleMobSkins << '\n';
	stream << "  spectatorSupport: " << profile.features.spectatorSupport << "\n\n";

	stream << "Game client signatures:\n";
	appendSignatureReport(stream, profile.gameClientSignatures);

	stream << "\nRuntime signatures:\n";
	appendSignatureReport(stream, profile.runtimeSignatures);
	return stream.str();
}

std::filesystem::path writeCompatibilityReport(RuntimeProfile& profile) noexcept
{
	try {
		PWSTR localAppData{};
		if (FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)))
			return {};

		std::filesystem::path directory{ localAppData };
		::CoTaskMemFree(localAppData);
		directory /= "R3nzSkin";

		std::error_code error;
		std::filesystem::create_directories(directory, error);
		if (error)
			return {};

		const auto reportPath{ directory / "compatibility.log" };
		std::ofstream output{ reportPath, std::ios::trunc };
		if (!output.good())
			return {};

		output << buildCompatibilityReport(profile);
		output.close();

		profile.diagnosticPath = reportPath.string();
		return reportPath;
	} catch (...) {
		return {};
	}
}
