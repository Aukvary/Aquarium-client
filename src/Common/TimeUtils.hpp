#pragma once

#include <chrono>
#include <ctime>
#include <google/protobuf/timestamp.pb.h>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

namespace Aquarium::Common {

inline std::string
FormatTimestamp(const google::protobuf::Timestamp& ts,
                const std::string& format = "%Y-%m-%d %H:%M") {
    if (ts.seconds() == 0 && ts.nanos() == 0) {
        return "-";
    }
    std::time_t rawTime = static_cast<std::time_t>(ts.seconds());
    std::tm timeInfo{};
#if defined(_WIN32)
    localtime_s(&timeInfo, &rawTime);
#else
    localtime_r(&rawTime, &timeInfo);
#endif
    std::ostringstream oss;
    oss << std::put_time(&timeInfo, format.c_str());
    return oss.str();
}

inline std::optional<google::protobuf::Timestamp>
ParseDate(const std::string& dateStr) {
    if (dateStr.empty()) {
        return std::nullopt;
    }
    std::tm tm{};
    std::istringstream ss(dateStr);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    if (ss.fail()) {
        return std::nullopt;
    }
#if defined(_WIN32)
    std::time_t t = _mkgmtime(&tm);
#else
    std::time_t t = timegm(&tm);
#endif
    if (t == -1) {
        return std::nullopt;
    }
    google::protobuf::Timestamp ts;
    ts.set_seconds(t);
    ts.set_nanos(0);
    return ts;
}

} // namespace Aquarium::Common
