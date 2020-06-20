#include "Settings.h"
#include "IPlayer.h"
#include "JSONHelpers.h"
#include "Log.h"
#include "PathUtils.h"
#include "PlayerListJSON.h"
#include "PlatformSpecific/Steam.h"

#include <mh/text/case_insensitive_string.hpp>
#include <mh/text/string_insertion.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>

using namespace tf2_bot_detector;
using namespace std::string_literals;
using namespace std::string_view_literals;

static std::filesystem::path s_SettingsPath("cfg/settings.json");

namespace tf2_bot_detector
{
	void to_json(nlohmann::json& j, const Settings::Theme::Colors& d)
	{
		j =
		{
			{ "scoreboard_cheater", d.m_ScoreboardCheater },
			{ "scoreboard_suspicious", d.m_ScoreboardSuspicious },
			{ "scoreboard_exploiter", d.m_ScoreboardExploiter },
			{ "scoreboard_racism", d.m_ScoreboardRacist },
			{ "scoreboard_you", d.m_ScoreboardYou },
			{ "scoreboard_connecting", d.m_ScoreboardConnecting },
			{ "friendly_team", d.m_FriendlyTeam },
			{ "enemy_team", d.m_EnemyTeam },
		};
	}

	void to_json(nlohmann::json& j, const Settings::Theme& d)
	{
		j =
		{
			{ "colors", d.m_Colors }
		};
	}

	void from_json(const nlohmann::json& j, Settings::Theme::Colors& d)
	{
		try_get_to(j, "scoreboard_cheater", d.m_ScoreboardCheater);
		try_get_to(j, "scoreboard_suspicious", d.m_ScoreboardSuspicious);
		try_get_to(j, "scoreboard_exploiter", d.m_ScoreboardExploiter);
		try_get_to(j, "scoreboard_racism", d.m_ScoreboardRacist);
		try_get_to(j, "scoreboard_you", d.m_ScoreboardYou);
		try_get_to(j, "scoreboard_connecting", d.m_ScoreboardConnecting);
		try_get_to(j, "friendly_team", d.m_FriendlyTeam);
		try_get_to(j, "enemy_team", d.m_EnemyTeam);
	}

	void from_json(const nlohmann::json& j, Settings::Theme& d)
	{
		d.m_Colors = j.at("colors");
	}
}

void tf2_bot_detector::to_json(nlohmann::json& j, const ProgramUpdateCheckMode& d)
{
	switch (d)
	{
	case ProgramUpdateCheckMode::Unknown:   j = nullptr; return;
	case ProgramUpdateCheckMode::Releases:  j = "releases"; return;
	case ProgramUpdateCheckMode::Previews:  j = "previews"; return;
	case ProgramUpdateCheckMode::Disabled:  j = "disabled"; return;

	default:
		throw std::invalid_argument("Unknown ProgramUpdateCheckMode "s << +std::underlying_type_t<ProgramUpdateCheckMode>(d));
	}
}

void tf2_bot_detector::from_json(const nlohmann::json& j, ProgramUpdateCheckMode& d)
{
	if (j.is_null())
	{
		d = ProgramUpdateCheckMode::Unknown;
		return;
	}

	auto value = j.get<std::string_view>();
	if (value == "releases"sv)
		d = ProgramUpdateCheckMode::Releases;
	else if (value == "previews"sv)
		d = ProgramUpdateCheckMode::Previews;
	else if (value == "disabled"sv)
		d = ProgramUpdateCheckMode::Disabled;
	else
		throw std::invalid_argument("Unknown ProgramUpdateCheckMode "s << std::quoted(value));
}

Settings::Settings()
{
	// Immediately load and resave to normalize any formatting
	LoadFile();
	SaveFile();
}

bool Settings::LoadFile()
{
	nlohmann::json json;
	{
		std::ifstream file(s_SettingsPath);
		if (!file.good())
		{
			Log(std::string(__FUNCTION__ ": Failed to open ") << s_SettingsPath, { 1, 0.5, 0, 1 });
			return false;
		}

		try
		{
			file >> json;
		}
		catch (const nlohmann::json::exception& e)
		{
			Log(std::string(__FUNCTION__ ": Failed to parse JSON from ") << s_SettingsPath << ": " << e.what(), { 1, 0.25, 0, 1 });
			return false;
		}
	}

	if (auto found = json.find("general"); found != json.end())
	{
		try_get_to(*found, "local_steamid_override", m_LocalSteamIDOverride);
		try_get_to(*found, "sleep_when_unfocused", m_SleepWhenUnfocused);
		try_get_to(*found, "auto_temp_mute", m_AutoTempMute);
		try_get_to(*found, "allow_internet_usage", m_AllowInternetUsage);
		try_get_to(*found, "program_update_check_mode", m_ProgramUpdateCheckMode);
		try_get_to(*found, "command_timeout_seconds", m_CommandTimeoutSeconds);
		try_get_to(*found, "steam_api_key", m_SteamAPIKey);

		if (auto foundDir = found->find("steam_dir_override"); foundDir != found->end())
			m_SteamDirOverride = foundDir->get<std::string_view>();
		if (auto foundDir = found->find("tf_game_dir_override"); foundDir != found->end())
			m_TFDirOverride = foundDir->get<std::string_view>();
	}

	try_get_to(json, "theme", m_Theme);

	if (auto foundTFDir = FindTFDir(GetSteamDir()); foundTFDir != m_TFDir)
	{
		DebugLog("Detected TF directory as "s << foundTFDir);
		m_TFDir = std::move(foundTFDir);
	}

	return true;
}

bool Settings::SaveFile() const
{
	nlohmann::json json =
	{
		{ "$schema", "https://raw.githubusercontent.com/PazerOP/tf2_bot_detector/master/schemas/v3/settings.schema.json" },
		{ "theme", m_Theme },
		{ "general",
			{
				{ "sleep_when_unfocused", m_SleepWhenUnfocused },
				{ "auto_temp_mute", m_AutoTempMute },
				{ "program_update_check_mode", m_ProgramUpdateCheckMode },
				{ "command_timeout_seconds", m_CommandTimeoutSeconds },
				{ "steam_api_key", m_SteamAPIKey },
			}
		}
	};

	if (!m_SteamDirOverride.empty())
		json["general"]["steam_dir_override"] = m_SteamDirOverride.string();
	if (!m_TFDirOverride.empty())
		json["general"]["tf_game_dir_override"] = m_TFDirOverride.string();
	if (m_LocalSteamIDOverride.IsValid())
		json["general"]["local_steamid_override"] = m_LocalSteamIDOverride;
	if (m_AllowInternetUsage)
		json["general"]["allow_internet_usage"] = *m_AllowInternetUsage;

	// Make sure we successfully serialize BEFORE we destroy our file
	auto jsonString = json.dump(1, '\t', true);
	{
		std::filesystem::create_directories(std::filesystem::path(s_SettingsPath).remove_filename());
		std::ofstream file(s_SettingsPath, std::ios::binary);
		if (!file.good())
		{
			LogError(std::string(__FUNCTION__ ": Failed to open settings file for writing: ") << s_SettingsPath);
			return false;
		}

		file << jsonString << '\n';
		if (!file.good())
		{
			LogError(std::string(__FUNCTION__ ": Failed to write settings to ") << s_SettingsPath);
			return false;
		}
	}

	return true;
}

SteamID AutoDetectedSettings::GetLocalSteamID() const
{
	if (m_LocalSteamIDOverride.IsValid())
		return m_LocalSteamIDOverride;

	if (auto sid = GetCurrentActiveSteamID(); sid.IsValid())
		return sid;

	//LogError(std::string(__FUNCTION__) << ": Failed to find a configured SteamID");
	return {};
}

std::filesystem::path AutoDetectedSettings::GetTFDir() const
{
	if (!m_TFDirOverride.empty())
		return m_TFDirOverride;

	return FindTFDir(GetSteamDir());
}

std::filesystem::path AutoDetectedSettings::GetSteamDir() const
{
	if (!m_SteamDirOverride.empty())
		return m_SteamDirOverride;

	return GetCurrentSteamDir();
}
