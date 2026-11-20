#pragma once

#include <chrono>
#include <ctime>
#include <google/protobuf/timestamp.pb.h>
#include <iomanip>
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

} // namespace Aquarium::Common
