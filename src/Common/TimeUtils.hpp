#pragma once

#include <charconv>
#include <ctime>
#include <google/protobuf/timestamp.pb.h>
#include <optional>
#include <string>
#include <string_view>

namespace Aquarium::Common {

inline std::string
FormatTimestamp(const google::protobuf::Timestamp& ts,
                const char* format = "%Y-%m-%d %H:%M") {
    if (ts.seconds() == 0 && ts.nanos() == 0) {
        return "-";
    }
    auto rawTime = static_cast<std::time_t>(ts.seconds());
    std::tm timeInfo{};
#if defined(_WIN32)
    localtime_s(&timeInfo, &rawTime);
#else
    localtime_r(&rawTime, &timeInfo);
#endif
    char buf[64];
    const auto written = std::strftime(buf, sizeof(buf), format, &timeInfo);
    return written > 0 ? std::string(buf, written) : std::string{};
}

inline std::optional<google::protobuf::Timestamp>
ParseDate(std::string_view dateStr) {
    if (dateStr.size() != 10 || dateStr[4] != '-' || dateStr[7] != '-') {
        return std::nullopt;
    }
    int year = 0;
    int month = 0;
    int day = 0;
    const char* begin = dateStr.data();
    auto [p1, ec1] = std::from_chars(begin, begin + 4, year);
    auto [p2, ec2] = std::from_chars(begin + 5, begin + 7, month);
    auto [p3, ec3] = std::from_chars(begin + 8, begin + 10, day);
    if (ec1 != std::errc{} || p1 != begin + 4 || ec2 != std::errc{} ||
        p2 != begin + 7 || ec3 != std::errc{} || p3 != begin + 10 ||
        month < 1 || month > 12 || day < 1 || day > 31) {
        return std::nullopt;
    }

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
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
