#pragma once
#include "kson/common/common.hpp"

namespace kson
{
    struct TimeSig
    {
        std::int64_t numerator = 4;
        std::int64_t denominator = 4;
    };

	void to_json(nlohmann::json& j, const TimeSig& timeSig);

    void from_json(const nlohmann::json& j, TimeSig& timeSig);
}
