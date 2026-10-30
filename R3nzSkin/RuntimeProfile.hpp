#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "../Shared/RuntimeCompatibility.hpp"

enum class ServerRegion {
	ChinaServer,
	RiotGamesServers,
	Unknown
};

enum class StartupStage {
	Created,
	ProfileReady,
	BoundaryChecked,
	SearchingGameClient,
	GameClientReady,
	WaitingForGame,
	ResolvingSignatures,
	SignaturesResolved,
	ConfigLoaded,
	HooksInstalled,
	Running,
	Failed
};

struct RuntimeFeatureFlags {
	bool localPlayerSkins{ true };
	bool otherChampionSkins{ true };
	bool minionSkins{ true };
	bool wardSkins{ true };
	bool turretSkins{ true };
	bool jungleMobSkins{ true };
	bool spectatorSupport{ true };
};

struct SignatureStatus {
	std::string name;
	bool found{ false };
	std::string matchedPattern;
	std::uintptr_t address{};
};

struct SignatureScanReport {
	std::string signatureSet;
	bool attempted{ false };
	bool success{ false };
	std::vector<SignatureStatus> entries;
	std::vector<std::string> missing;
};

struct RuntimeProfile {
	ServerRegion serverRegion{ ServerRegion::Unknown };
	std::string buildConfiguration{ "Unknown" };
	std::string buildArchitecture{ "Unknown" };
	std::string buildTimestamp{ __DATE__ " " __TIME__ };
	std::string clientVersion{ "Unknown" };
	std::string signatureSet{ "unknown" };
	std::uintptr_t moduleBase{};
	StartupStage stage{ StartupStage::Created };
	std::string stageDetail;
	std::string failureStage;
	std::string failureReason;
	std::string diagnosticPath;
	bool failed{ false };
	RuntimeFeatureFlags features{};
	RuntimeCompatibilityBoundary boundary{};
	SignatureScanReport gameClientSignatures{};
	SignatureScanReport runtimeSignatures{};
};

[[nodiscard]] const char* toString(ServerRegion region) noexcept;
[[nodiscard]] const char* toString(StartupStage stage) noexcept;
[[nodiscard]] RuntimeProfile createRuntimeProfile() noexcept;

void setStartupStage(RuntimeProfile& profile, StartupStage stage, std::string detail = {}) noexcept;
void markRuntimeFailure(RuntimeProfile& profile, StartupStage failedStage, std::string reason) noexcept;

[[nodiscard]] std::string buildCompatibilityReport(const RuntimeProfile& profile);
[[nodiscard]] std::filesystem::path writeCompatibilityReport(RuntimeProfile& profile) noexcept;
