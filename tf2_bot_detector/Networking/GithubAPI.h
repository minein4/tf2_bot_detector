#pragma once

#include "Version.h"

#include <mh/coroutine/task.hpp>

#include <optional>
#include <string>

namespace tf2_bot_detector
{
	class IHTTPClient;
}

namespace tf2_bot_detector::GithubAPI
{
	struct NewVersionResult
	{
		bool IsReleaseAvailable() const { return m_Stable.has_value(); }
		bool IsPreviewAvailable() const { return m_Preview.has_value(); }
		bool IsError() const { return !IsReleaseAvailable() && !IsPreviewAvailable() && m_Error; }
		bool IsUpToDate() const { return !IsReleaseAvailable() && !IsPreviewAvailable(); }

		std::string GetURL() const
		{
			if (IsPreviewAvailable())
				return m_Preview->m_URL;
			if (IsReleaseAvailable())
				return m_Stable->m_URL;

			return {};
		}

		struct Release
		{
			Version m_Version;
			std::string m_URL;
			bool m_IsPrerelease;
		};

		bool m_Error = false;
		std::optional<Release> m_Stable;
		std::optional<Release> m_Preview;
	};

	[[nodiscard]] mh::task<NewVersionResult> CheckForNewVersion(const IHTTPClient& client);
}
