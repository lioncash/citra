// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <spdlog/spdlog.h>
#include "common/string_util.h"
#include "common/logging/log.h"

namespace {

using LoggerMap = std::unordered_map<Log::Class, Log::LoggerPtr>;

LoggerMap loggers;
bool initialized;

// Creates a single-threaded logger with color support.
Log::LoggerPtr CreateColoredLogger(const std::string& name) {
    return spdlog::stdout_logger_st(name, true);
}

} // namespace

namespace Log {

void Initialize() {
    if (initialized)
        return;

    loggers[Class::Audio            ] = CreateColoredLogger("Audio");
    loggers[Class::Audio_DSP        ] = CreateColoredLogger("Audio.DSP");
    loggers[Class::Audio_Sink       ] = CreateColoredLogger("Audio.Sink");
    loggers[Class::Common           ] = CreateColoredLogger("Common");
    loggers[Class::Common_Filesystem] = CreateColoredLogger("Common.Filesystem");
    loggers[Class::Common_Memory    ] = CreateColoredLogger("Common.Memory");
    loggers[Class::Config           ] = CreateColoredLogger("Config");
    loggers[Class::Core             ] = CreateColoredLogger("Core");
    loggers[Class::Core_ARM11       ] = CreateColoredLogger("Core.ARM11");
    loggers[Class::Core_Timing      ] = CreateColoredLogger("Core.Timing");
    loggers[Class::Debug            ] = CreateColoredLogger("Debug");
    loggers[Class::Debug_Breakpoint ] = CreateColoredLogger("Debug.Breakpoint");
    loggers[Class::Debug_Emulated   ] = CreateColoredLogger("Debug.Emulated");
    loggers[Class::Debug_GDBStub    ] = CreateColoredLogger("Debug.GDBStub");
    loggers[Class::Debug_GPU        ] = CreateColoredLogger("Debug.GPU");
    loggers[Class::Frontend         ] = CreateColoredLogger("Frontend");
    loggers[Class::HW               ] = CreateColoredLogger("HW");
    loggers[Class::HW_GPU           ] = CreateColoredLogger("HW.GPU");
    loggers[Class::HW_LCD           ] = CreateColoredLogger("HW.LCD");
    loggers[Class::HW_Memory        ] = CreateColoredLogger("HW.Memory");
    loggers[Class::Kernel           ] = CreateColoredLogger("Kernel");
    loggers[Class::Kernel_SVC       ] = CreateColoredLogger("Kernel.SVC");
    loggers[Class::Loader           ] = CreateColoredLogger("Loader");
    loggers[Class::Log              ] = CreateColoredLogger("Log");
    loggers[Class::Render           ] = CreateColoredLogger("Render");
    loggers[Class::Render_OpenGL    ] = CreateColoredLogger("Render.OGL");
    loggers[Class::Render_Software  ] = CreateColoredLogger("Render.Software");
    loggers[Class::Service          ] = CreateColoredLogger("Service");
    loggers[Class::Service_AC       ] = CreateColoredLogger("Service.AC");
    loggers[Class::Service_AM       ] = CreateColoredLogger("Service.AM");
    loggers[Class::Service_APT      ] = CreateColoredLogger("Service.APT");
    loggers[Class::Service_CAM      ] = CreateColoredLogger("Service.CAM");
    loggers[Class::Service_CECD     ] = CreateColoredLogger("Service.CECD");
    loggers[Class::Service_CFG      ] = CreateColoredLogger("Service.CFG");
    loggers[Class::Service_DLP      ] = CreateColoredLogger("Service.DLP");
    loggers[Class::Service_DSP      ] = CreateColoredLogger("Service.DSP");
    loggers[Class::Service_ERR      ] = CreateColoredLogger("Service.ERR");
    loggers[Class::Service_FRD      ] = CreateColoredLogger("Service.FRD");
    loggers[Class::Service_FS       ] = CreateColoredLogger("Service.FS");
    loggers[Class::Service_GSP      ] = CreateColoredLogger("Service.GSP");
    loggers[Class::Service_HID      ] = CreateColoredLogger("Service.HID");
    loggers[Class::Service_IR       ] = CreateColoredLogger("Service.IR");
    loggers[Class::Service_LDR      ] = CreateColoredLogger("Service.LDR");
    loggers[Class::Service_NDM      ] = CreateColoredLogger("Service.NDM");
    loggers[Class::Service_NIM      ] = CreateColoredLogger("Service.NIM");
    loggers[Class::Service_NWM      ] = CreateColoredLogger("Service.NWM");
    loggers[Class::Service_PTM      ] = CreateColoredLogger("Service.PTM");
    loggers[Class::Service_SOC      ] = CreateColoredLogger("Service.SOC");
    loggers[Class::Service_SRV      ] = CreateColoredLogger("Service.SRV");
    loggers[Class::Service_Y2R      ] = CreateColoredLogger("Service.Y2R");

    initialized = true;
}

LoggerPtr Get(Class log_class) {
    return loggers[log_class];
}

static spdlog::level::level_enum StringToLogLevel(const std::string& level) {
    const auto lower_level = Common::ToLower(level);

    if (lower_level == "trace")
        return spdlog::level::trace;
    if (lower_level == "debug")
        return spdlog::level::debug;
    if (lower_level == "info")
        return spdlog::level::info;
    if (lower_level == "notice")
        return spdlog::level::notice;
    if (lower_level == "warning")
        return spdlog::level::warn;
    if (lower_level == "error")
        return spdlog::level::err;
    if (lower_level == "critical")
        return spdlog::level::critical;
    if (lower_level == "alert")
        return spdlog::level::alert;
    if (lower_level == "emergency")
        return spdlog::level::emerg;

    return spdlog::level::off;
}

// Tokenizes a string based off of a given delimeter.
static std::vector<std::string> SplitString(const std::string& str, char delimeter) {
    std::vector<std::string> tokens;

    std::istringstream parser(str);
    std::string token;

    while (std::getline(parser, token, delimeter)) {
        if (!token.empty())
            tokens.push_back(std::move(token));
    }

    // In the event only one filter is provided.
    if (tokens.empty())
        tokens.push_back(str);

   return tokens;
}

// Finds a log within the logger map based of of a name match based on criteria.
//
// - If a name contains a double colon, then it's a namespaced logger.
// - If the name has no double colon, then it's a top-level logger of a hierarchy.
//
// For example:
//
// Loggers named "Service" and "Service.AC" are considered grouped and
// will both be considered valid candidates with this function if name
// is given the string "Service".
//
// "Service" is the top-level logger, while "Service.AC" is considered a
// logger subclass of "Service".
//
template <typename InputIterator>
static LoggerMap::const_iterator FindLogByString(InputIterator begin, InputIterator end, const std::string& name) {
    return std::find_if(begin, end, [&name](const auto& entry) {
        const std::string& logger_name = entry.second->name();

        // If the double colon wasn't found, then the logger is considered
        // a top-level logger instance and a full name comparison should be done.
        const size_t length = logger_name.rfind(".");
        if (length == std::string::npos)
            return logger_name == name;

        if (length > name.length())
            return false;

        // Otherwise, we treat it as a subclass logger and just check if the namespacing is intact.
        return std::equal(name.begin(), name.begin() + length, logger_name.begin());
    });
}

// Retrieves all related loggers to a given input string.
static std::vector<LoggerMap::const_iterator> FindRelatedLoggers(const std::string& name) {
    std::vector<LoggerMap::const_iterator> found_loggers;

    auto begin = loggers.cbegin();
    auto end   = loggers.cend();
    while ((begin = FindLogByString(begin, end, name)) != end) {
        found_loggers.push_back(begin);
        ++begin;
    }

    return found_loggers;
}

// Parses individual options from the overall logging filter.
// e.g. "*:Warning" is essentially converted to the pair: {"*", spdlog::level::warn}.
static std::vector<std::pair<std::string, spdlog::level::level_enum>> ParseFilterPairs(const std::vector<std::string>& filter_entries) {
    std::vector<std::pair<std::string, spdlog::level::level_enum>> token_pairs;

    for (const auto& entry : filter_entries) {
        const size_t pos = entry.rfind(":");
        if (pos == std::string::npos)
            continue;

        const std::string log_name  = entry.substr(0, pos);
        const std::string log_level = entry.substr(pos + 1, std::string::npos);

        token_pairs.emplace_back(log_name, StringToLogLevel(log_level));
    }

    return token_pairs;
}

void ParseFilter(const std::string& filter) {
    const auto split_elements = SplitString(filter, ' ');
    auto token_pairs = ParseFilterPairs(split_elements);

    // Handle the case where "all logs" is specified.
    if (token_pairs[0].first == "*") {
        spdlog::set_level(token_pairs[0].second);
        token_pairs.erase(token_pairs.begin());
    }

    for (const auto& pair : token_pairs) {
        const auto found_loggers = FindRelatedLoggers(pair.first);

        for (const auto& logger : found_loggers)
            logger->second->set_level(pair.second);
    }
}

} // namespace Log
