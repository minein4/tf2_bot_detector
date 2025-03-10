#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING 1

#include "ChatWrappers.h"
#include "Util/JSONUtils.h"
#include "Util/TextUtils.h"
#include "Log.h"

#include <vdf_parser.hpp>
#include <mh/coroutine/generator.hpp>
#include <mh/text/fmtstr.hpp>
#include <mh/text/string_insertion.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <charconv>
#include <compare>
#include <concepts>
#include <execution>
#include <random>
#include <regex>
#include <set>
#include <Filesystem.h>

#undef min
#undef max

using namespace std::string_literals;
using namespace std::string_view_literals;
using namespace tf2_bot_detector;

#ifdef _DEBUG
namespace tf2_bot_detector
{
	extern uint32_t g_StaticRandomSeed;
}
#endif

static constexpr std::string_view LANGUAGES[] =
{
	"brazilian",
	"bulgarian",
	"czech",
	"danish",
	"dutch",
	"english",
	"finnish",
	"french",
	"german",
	"greek",
	"hungarian",
	"italian",
	"japanese",
	"korean",
	"koreana",
	"norwegian",
	"pirate",
	"polish",
	"portuguese",
	"romanian",
	"russian",
	"schinese",
	"spanish",
	"swedish",
	"tchinese",
	"thai",
	"turkish",
	"ukrainian",
};

namespace
{
	struct InvisCharsEntry
	{
		std::u8string_view m_Narrow;
		char16_t m_Wide;
	};
}

#undef INVIS_CHAR_SEQ
#define INVIS_CHAR_SEQ(chars) InvisCharsEntry{ u8 ## chars, u ## chars [0] }

static constexpr const InvisCharsEntry INVISIBLE_CHARS[] =
{
	// 1 through 8 are reserved for color changing

	//u8"\u180E", // Shows up in game, do not use
	INVIS_CHAR_SEQ("\u200B"),
	INVIS_CHAR_SEQ("\u200C"),
	INVIS_CHAR_SEQ("\u200D"),
	INVIS_CHAR_SEQ("\uFEFF"),
	INVIS_CHAR_SEQ("\u2060"),

};

#undef INVIS_CHAR_SEQ

static constexpr size_t MIN_PERMUTATIONS = size_t(ChatCategory::COUNT) * 6;

namespace tf2_bot_detector
{
	struct ChatFormatStrings
	{
		using array_t = std::array<std::string, (size_t)ChatCategory::COUNT>;
		array_t m_English;
		array_t m_Localized;
	};
}

static ChatWrappers::wrapper_t GenerateInvisibleCharSequence(std::mt19937& random, size_t wrapChars)
{
	std::uniform_int_distribution<size_t> dist(0, std::size(INVISIBLE_CHARS) - 1);

	ChatWrappers::wrapper_t sequence;
	//for (size_t i = 0; i < wrapChars; i++)
	while (sequence.m_Wide.size() < wrapChars)
	{
		const auto randomIndex = dist(random);
		const InvisCharsEntry& randomStr = INVISIBLE_CHARS[randomIndex];

		sequence.m_Narrow += reinterpret_cast<const char*>(randomStr.m_Narrow.data());
		sequence.m_Wide += randomStr.m_Wide;
	}

	return sequence;
}

ChatWrappers::ChatWrappers(const ChatFmtStrLengths& wrapChars)
{
	std::set<wrapper_t> generated;

	std::mt19937 random;
#ifdef _DEBUG
	if (g_StaticRandomSeed)
	{
		random.seed(unsigned(g_StaticRandomSeed));
	}
	else
#endif
	{
		std::random_device device;
		random.seed(device());
	}

	int i = 0;
	const auto ProcessWrapper = [&](wrapper_t& wrapper, size_t wrapperLength)
	{
		ChatWrappers::wrapper_t seq;
		do
		{
			seq = GenerateInvisibleCharSequence(random, wrapperLength);

		} while (generated.contains(seq));

		generated.insert(seq);
		wrapper = std::move(seq);
	};

	for (size_t i = 0; i < m_Types.size(); i++)
	{
		auto& type = m_Types[i];
		auto& lengths = wrapChars.m_Types[i];
		const auto maxWrapperLen = lengths.GetMaxWrapperLength();

		ProcessWrapper(type.m_Full.m_Start, maxWrapperLen);
		ProcessWrapper(type.m_Full.m_End, maxWrapperLen);
		ProcessWrapper(type.m_Name.m_Start, maxWrapperLen);
		ProcessWrapper(type.m_Name.m_End, maxWrapperLen);
		ProcessWrapper(type.m_Message.m_Start, maxWrapperLen);
		ProcessWrapper(type.m_Message.m_End, maxWrapperLen);
	}
}

template<typename TFunc>
static void RegexSmartReplace(std::string& str, const std::regex& regex, TFunc&& func)
{
	auto it = str.begin();
	std::match_results<std::string::iterator> match;
	while (std::regex_search(it, str.end(), match, regex))
	{
		const auto& cmatch = match;
		std::string newStr = func(cmatch);

		size_t offs = match[0].first - str.begin();
		str.replace(match[0].first, match[0].second, newStr);
		it = str.begin() + offs + newStr.size();
	}
}

static bool GetChatCategory(const std::string* src, std::string_view* name, ChatCategory* category, bool* isEnglish)
{
	if (!src)
	{
		LogError("nullptr passed to "s << __FUNCTION__);
		return false;
	}

	static constexpr auto TF_CHAT_ROOT = "TF_Chat_"sv;

	std::string_view localName;
	if (!name)
		name = &localName;

	if (src->starts_with(TF_CHAT_ROOT))
	{
		*name = std::string_view(*src).substr(TF_CHAT_ROOT.size());
		if (isEnglish)
			*isEnglish = false;
	}
	else
	{
		return false;
	}

	ChatCategory localCategory;
	if (!category)
		category = &localCategory;

	if (*name == "Team"sv)
		*category = ChatCategory::Team;
	else if (*name == "Team_Dead"sv)
		*category = ChatCategory::TeamDead;
	else if (*name == "Spec"sv)
		*category = ChatCategory::SpecTeam;
	else if (*name == "AllSpec"sv)
		*category = ChatCategory::Spec;
	else if (*name == "All"sv)
		*category = ChatCategory::All;
	else if (*name == "AllDead"sv)
		*category = ChatCategory::AllDead;
	else if (*name == "Coach"sv)
		*category = ChatCategory::Coach;
	else if (*name == "Team_Loc"sv || *name == "Party"sv || *name == "Disabled"sv || *name == "Unavailable"sv)
		return false;
	else
	{
		LogError("Unknown chat type localization string "s << std::quoted(*name));
		return false;
	}

	return true;
}

static void GetChatMsgFormats(const std::string_view& debugInfo, const std::string_view& translations, ChatFormatStrings& strings)
{
	assert(!translations.empty());

	const char* begin = translations.data();
	const char* end = begin + translations.size();
	std::error_code ec;
	auto parsed = tyti::vdf::read(begin, end, ec);
	if (ec)
	{
		LogError("Failed to parse translations from "s << std::quoted(debugInfo) << ": " << ec);
		return;
	}

	if (auto tokens = parsed.childs["Tokens"])
	{
		std::string_view chatType;
		bool isEnglish;

		for (const auto& attrib : tokens->attribs)
		{
			ChatCategory cat;
			if (!GetChatCategory(&attrib.first, &chatType, &cat, &isEnglish))
				continue;

			if (attrib.second.empty())
			{
				LogWarning(MH_SOURCE_LOCATION_CURRENT(), "{}: Empty value read for {} ({})",
					std::quoted(debugInfo), std::quoted(attrib.first), mh::enum_fmt(cat));
			}

			(isEnglish ? strings.m_English : strings.m_Localized)[(int)cat] = attrib.second;
		}
	}
}

static void ApplyChatWrappers(const std::string_view& debugInfo, ChatCategory cat,
	std::string& translation, const ChatWrappers& wrappers)
{
	if (translation.empty())
	{
		LogWarning(MH_SOURCE_LOCATION_CURRENT(), "{}: Translation empty for {}", debugInfo, mh::enum_fmt(cat));
		return;
	}

	static const std::basic_regex s_Regex(R"regex(([\x01-\x05]?)(.*)%s1(.*)%s2(.*))regex",
		std::regex::optimize | std::regex::icase);

	const auto& wrapper = wrappers.m_Types[(int)cat];
	const auto replaceStr = "$1"s << wrapper.m_Full.m_Start.m_Narrow << "$2"
		<< wrapper.m_Name.m_Start.m_Narrow << "%s1" << wrapper.m_Name.m_End.m_Narrow
		<< "$3"
		<< wrapper.m_Message.m_Start.m_Narrow << "%s2" << wrapper.m_Message.m_End.m_Narrow
		<< "$4"
		<< wrapper.m_Full.m_End.m_Narrow;

	auto replaced = std::regex_replace(translation, s_Regex, replaceStr);
	if (replaced == translation)
	{
		LogError("Failed to apply chat message regex to "s << std::quoted(translation));
		return;
	}

	translation = std::move(replaced);
}

static mh::generator<std::filesystem::path> GetLocalizationFiles(
	const std::filesystem::path& tfDir, const std::string_view& language, bool baseFirst = true)
{
	if (baseFirst)
	{
		if (auto path = tfDir / "resource" / ("tf_"s << language << ".txt"); std::filesystem::exists(path))
			co_yield path;
		if (auto path = tfDir / "resource" / ("chat_"s << language << ".txt"); std::filesystem::exists(path))
			co_yield path;
	}

	for (const auto& customDirEntry : std::filesystem::directory_iterator(tfDir / "custom"))
	{
		if (!customDirEntry.is_directory())
			continue;

		const auto customDir = customDirEntry.path();
		if (!std::filesystem::exists(customDir / "resource"))
			continue;

		if (customDir.filename() == TF2BD_CHAT_WRAPPERS_DIR)
			continue;

		if (auto path = customDir / "resource" / ("tf_"s << language << ".txt"); std::filesystem::exists(path))
			co_yield path;
		if (auto path = customDir / "resource" / ("chat_"s << language << ".txt"); std::filesystem::exists(path))
			co_yield path;
	}

	if (!baseFirst)
	{
		if (auto path = tfDir / "resource" / ("tf_"s << language << ".txt"); std::filesystem::exists(path))
			co_yield path;
		if (auto path = tfDir / "resource" / ("chat_"s << language << ".txt"); std::filesystem::exists(path))
			co_yield path;
	}
}

static std::filesystem::path FindExistingChatTranslationFile(
	const std::filesystem::path& tfDir, const std::string_view& language)
{
	const auto desiredFilename = "chat_"s << language << ".txt";
	for (const auto& path : GetLocalizationFiles(tfDir, language, false))
	{
		//Log("Filename: "s << path.filename());
		if (path.filename() == desiredFilename)
			return path;
	}

	return {};
}

#include <stdio.h>

static ChatFormatStrings FindExistingTranslations(const std::filesystem::path& tfdir, const std::string_view& language)
{
	ChatFormatStrings retVal;

	for (const auto& filename : GetLocalizationFiles(tfdir, language)) {
		std::string translationData;

		try {
			translationData = ToMB(ReadWideFile(filename));
		}
		catch (std::runtime_error) {
			LogException("Attempting to parse as-is");
			translationData = IFilesystem::Get().ReadFile(filename);
		}

		GetChatMsgFormats(filename.string(), translationData, retVal);
	}

	return retVal;
}

static constexpr std::string_view GetChatCategoryKey(ChatCategory cat, bool isEnglish)
{
	switch (cat)
	{
	case ChatCategory::All:       return "TF_Chat_All"sv;
	case ChatCategory::AllDead:   return "TF_Chat_AllDead"sv;
	case ChatCategory::Team:      return "TF_Chat_Team"sv;
	case ChatCategory::TeamDead:  return "TF_Chat_Team_Dead"sv;
	case ChatCategory::Spec:      return "TF_Chat_AllSpec"sv;
	case ChatCategory::SpecTeam:  return "TF_Chat_Spec"sv;
	case ChatCategory::Coach:     return "TF_Chat_Coach"sv;

	case ChatCategory::COUNT:	  break;
	}
	LogError(MH_SOURCE_LOCATION_CURRENT(), "Unknown key for {} with isEnglish = {}", mh::enum_fmt(cat), isEnglish);
	return "TF_Chat_UNKNOWN"sv;
}

static void PrintChatWrappers(const ChatWrappers& wrappers)
{
	static const auto Indent = [](std::string& str, int levels)
	{
		while (levels--)
			str.push_back('\t');
	};

	static const auto PrintWrapper = [](std::string& str, const ChatWrappers::wrapper_t& wrapper)
	{
		mh::strwrapperstream os(str);
		os << std::quoted(wrapper.m_Narrow) << " (";

		for (auto c : wrapper.m_Wide)
			os << mh::pfstr<64>("\\x%04X", +c);

		os << ')';
	};
	const auto PrintWrapperPair = [](std::string& str, int indentLevels, const ChatWrappers::wrapper_pair_t& wrapper)
	{
		str << '\n';

		Indent(str, indentLevels + 1);
		str << "begin: ";
		PrintWrapper(str, wrapper.m_Start);

		str << '\n';
		Indent(str, indentLevels + 1);
		str << "end:   ";
		PrintWrapper(str, wrapper.m_End);
	};

	std::string logMsg = "Generated chat message wrappers:";// for "s << cat << '\n';
	for (int i = 0; i < (int)ChatCategory::COUNT; i++)
	{
		auto cat = ChatCategory(i);

		logMsg << '\n';
		Indent(logMsg, 1);
		logMsg << mh::enum_fmt(cat) << ':';

		logMsg << "\n\t\tFull: ";
		PrintWrapperPair(logMsg, 2, wrappers.m_Types[i].m_Full);
		logMsg << "\n\t\tName: ";
		PrintWrapperPair(logMsg, 2, wrappers.m_Types[i].m_Name);
		logMsg << "\n\t\tMessage: ";
		PrintWrapperPair(logMsg, 2, wrappers.m_Types[i].m_Message);
	}
	DebugLog(std::move(logMsg));
}

void tf2_bot_detector::to_json(nlohmann::json& j, const ChatCategory& d)
{
	switch (d)
	{
	case ChatCategory::All:       j = "all"; return;
	case ChatCategory::AllDead:   j = "all_dead"; return;
	case ChatCategory::Team:      j = "team"; return;
	case ChatCategory::TeamDead:  j = "team_dead"; return;
	case ChatCategory::Spec:      j = "spec"; return;
	case ChatCategory::SpecTeam:  j = "spec_team"; return;
	case ChatCategory::Coach:     j = "coach"; return;

	default:
		throw std::invalid_argument("Invalid ChatCategory("s << std::underlying_type_t<ChatCategory>(d) << ')');
	}
}

void tf2_bot_detector::from_json(const nlohmann::json& j, ChatCategory& d)
{
	if (j == "all")
		d = ChatCategory::All;
	else if (j == "all_dead")
		d = ChatCategory::AllDead;
	else if (j == "team")
		d = ChatCategory::Team;
	else if (j == "team_dead")
		d = ChatCategory::TeamDead;
	else if (j == "spec")
		d = ChatCategory::Spec;
	else if (j == "spec_team")
		d = ChatCategory::SpecTeam;
	else if (j == "coach")
		d = ChatCategory::Coach;
	else
		throw std::invalid_argument("Unknown ChatCategory "s << std::quoted(j.get<std::string_view>()));
}

void tf2_bot_detector::to_json(nlohmann::json& j, const ChatWrappers::WrapperPair& d)
{
	j =
	{
		{ "start", d.m_Start },
		{ "end", d.m_End },
	};
}

void tf2_bot_detector::from_json(const nlohmann::json& j, ChatWrappers::WrapperPair& d)
{
	j.at("start").get_to(d.m_Start);
	j.at("end").get_to(d.m_End);
}

void tf2_bot_detector::to_json(nlohmann::json& j, const ChatWrappers::Type& d)
{
	j =
	{
		{ "full", d.m_Full },
		{ "name", d.m_Name },
		{ "message", d.m_Message },
	};
}

void tf2_bot_detector::from_json(const nlohmann::json& j, ChatWrappers::Type& d)
{
	j.at("full").get_to(d.m_Full);
	j.at("name").get_to(d.m_Name);
	j.at("message").get_to(d.m_Message);
}

void tf2_bot_detector::to_json(nlohmann::json& j, const ChatWrappers& d)
{
	j = nlohmann::json::array();

	for (size_t i = 0; i < std::size(d.m_Types); i++)
	{
		auto& elem = j.emplace_back(d.m_Types[i]);
		elem["type"] = ChatCategory(i);
	}
}

void tf2_bot_detector::from_json(const nlohmann::json& j, ChatWrappers& d)
{
	for (const auto& obj : j)
	{
		auto type = (ChatCategory)obj.at("type");
		d.m_Types.at(size_t(type)) = obj;
	}
}

ChatFmtStrLengths ChatFmtStrLengths::Max(const ChatFmtStrLengths& other) const
{
	ChatFmtStrLengths retVal;

	for (size_t i = 0; i < m_Types.size(); i++)
		retVal.m_Types[i] = m_Types[i].Max(other.m_Types[i]);

	return retVal;
}

ChatFmtStrLengths::Type::Type(const std::string_view& str)
{
	const auto s1 = str.find("%s1");
	if (s1 == str.npos)
	{
		LogError(MH_SOURCE_LOCATION_CURRENT(), "Failed to find '%s1' in "s << std::quoted(str));
		return;
	}

	const auto s2 = str.find("%s2");
	if (s2 == str.npos)
	{
		LogError(MH_SOURCE_LOCATION_CURRENT(), "Failed to find '%s2' in "s << std::quoted(str));
		return;
	}

	m_Prefix = s1;
	m_Separator = s2 - (s1 + 3);
	m_Suffix = str.size() - (s2 + 3);
}

auto ChatFmtStrLengths::Type::Max(const Type& other) const -> Type
{
	return Type(
		std::max(m_Prefix, other.m_Prefix),
		std::max(m_Separator, other.m_Separator),
		std::max(m_Suffix, other.m_Suffix)
	);
}

size_t ChatFmtStrLengths::Type::GetAvailableChars() const
{
	constexpr size_t MAX_TOTAL_CHATMSG_LENGTH = 255;
	constexpr size_t MAX_CHATMSG_MSG_LENGTH = 127;
	constexpr size_t MAX_PLAYERNAME_LENGTH = 33;

	const auto availableChars = MAX_TOTAL_CHATMSG_LENGTH
		- MAX_PLAYERNAME_LENGTH
		- MAX_CHATMSG_MSG_LENGTH
		- m_Prefix
		- m_Separator
		- m_Suffix;

	if (availableChars > MAX_TOTAL_CHATMSG_LENGTH)
		return 0;

	return availableChars;
}

size_t ChatFmtStrLengths::Type::GetMaxWrapperLength() const
{
	return 3;
}

ChatWrappers tf2_bot_detector::RandomizeChatWrappers(const std::filesystem::path& tfdir,
	mh::status_reader<ChatWrappersProgress>* progressReader)
{
	mh::status_source<ChatWrappersProgress> progressSource;
	if (progressReader)
		*progressReader = progressSource;

	ChatWrappersProgress progress;

	assert(!tfdir.empty());

	if (auto path = tfdir / "custom" / "tf2_bot_detector"; std::filesystem::exists(path))
	{
		DebugLog("Deleting {}", path);
		std::filesystem::remove_all(path);
	}

	if (auto path = tfdir / "custom" / TF2BD_CHAT_WRAPPERS_DIR; std::filesystem::exists(path))
	{
		DebugLog("Deleting {}", path);
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
		if (ec)
			LogWarning("Failed to delete {}: {}: {}", path, ec.value(), ec.message());
	}

	const auto outputDir = tfdir / "custom" / TF2BD_CHAT_WRAPPERS_DIR / "resource";
	std::filesystem::create_directories(outputDir);

	progress.m_MaxValue = unsigned(std::size(LANGUAGES) * 5);
	progressSource.set(progress);

	const auto IncrementProgress = [&]
	{
		++progress.m_Value;
		progressSource.set(progress);
	};

	ChatFormatStrings translations[std::size(LANGUAGES)];
	ChatFmtStrLengths translationLengths;

	{
		std::mutex lengthsMutex;

		// Get all the existing translations
		std::for_each(std::execution::par_unseq, std::begin(LANGUAGES), std::end(LANGUAGES),
			[&](const std::string_view& lang)
			{
				const size_t index = &lang - std::begin(LANGUAGES);
				const auto& localTrans = translations[index] = FindExistingTranslations(tfdir, lang);

				ChatFmtStrLengths localLengths;

				for (size_t categoryIndex = 0; categoryIndex < size_t(ChatCategory::COUNT); categoryIndex++)
				{
					auto& len = localLengths.m_Types[categoryIndex];

					if (auto& trans = localTrans.m_English[categoryIndex]; !trans.empty())
						len = len.Max(ChatFmtStrLengths::Type(trans));
					if (auto& trans = localTrans.m_Localized[categoryIndex]; !trans.empty())
						len = len.Max(ChatFmtStrLengths::Type(trans));
				}

				{
					std::lock_guard lock(lengthsMutex);
					translationLengths = translationLengths.Max(localLengths);
				}

				IncrementProgress();
			});
	}

	ChatWrappers wrappers(translationLengths);
	PrintChatWrappers(wrappers);

	std::for_each(std::execution::par_unseq, std::begin(LANGUAGES), std::end(LANGUAGES),
		[&](const std::string_view& lang)
		{
			auto& translationsSet = translations[&lang - std::begin(LANGUAGES)];

			// Create our own chat localization file
			using obj = tyti::vdf::object;
			obj file;
			file.name = "lang";
			file.add_attribute("Language", std::string(lang));
			auto& tokens = file.childs["Tokens"] = std::make_shared<obj>();
			tokens->name = "Tokens";

			for (size_t i = 0; i < translationsSet.m_Localized.size(); i++)
			{
				ApplyChatWrappers(lang, ChatCategory(i), translationsSet.m_Localized[i], wrappers);
				const auto key = GetChatCategoryKey(ChatCategory(i), false);
				tokens->attribs[std::string(key)] = translationsSet.m_Localized[i];
			}
			for (size_t i = 0; i < translationsSet.m_English.size(); i++)
			{
				if (translationsSet.m_English[i].empty())
					continue;

				ApplyChatWrappers(lang, ChatCategory(i), translationsSet.m_English[i], wrappers);
				const auto key = GetChatCategoryKey(ChatCategory(i), true);
				tokens->attribs[std::string(key)] = translationsSet.m_English[i];
			}
			IncrementProgress();

			std::string outFile;
			mh::strwrapperstream stream(outFile);
			tyti::vdf::write(stream, file);
			IncrementProgress();

			WriteWideFile(outputDir / ("closecaption_"s << lang << ".txt"), ToU16(outFile));
			IncrementProgress();
		});

	Log("Wrote "s << std::size(LANGUAGES) << " modified translations to " << outputDir);

	return std::move(wrappers);
}
