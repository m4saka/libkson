#pragma once
#include <string>
#include <string_view>
#include <array>
#include <vector>
#include <set>
#include <map>
#include <unordered_map>
#include <cassert>
#include <cstdint>
#include "kson/encoding/encoding.hpp"

namespace kson
{
	constexpr std::int32_t kNumBTLanes = 4;
	constexpr std::int32_t kNumFXLanes = 2;
	constexpr std::int32_t kNumLaserLanes = 2;

	constexpr std::size_t kNumBTLanesSZ = std::size_t{ kNumBTLanes };
	constexpr std::size_t kNumFXLanesSZ = std::size_t{ kNumFXLanes };
	constexpr std::size_t kNumLaserLanesSZ = std::size_t{ kNumLaserLanes };

	using Pulse = std::int64_t;
	using RelPulse = std::int64_t;

	constexpr Pulse kResolution = 240;
	constexpr Pulse kResolution4 = kResolution * 4;

	// The difference between Pulse and RelPulse is only for annotation
	static_assert(std::is_same_v<Pulse, RelPulse>);

	template <typename T>
	using ByPulse = std::map<Pulse, T>;

	template <typename T>
	using BTLane = std::array<ByPulse<T>, kNumBTLanesSZ>;

	template <typename T>
	using FXLane = std::array<ByPulse<T>, kNumFXLanesSZ>;

	template <typename T>
	using LaserLane = std::array<ByPulse<T>, kNumLaserLanesSZ>;

	template <typename T>
	using ByPulseMulti = std::multimap<Pulse, T>;

	template <typename T>
	using ByRelPulse = std::map<RelPulse, T>;

	template <typename T>
	using ByRelPulseMulti = std::multimap<RelPulse, T>;

	template <typename T>
	using ByMeasureIdx = std::map<std::int64_t, T>;

	struct GraphValue
	{
		double v = 0.0;
		double vf = 0.0;

		GraphValue(double v)
			: v(v)
			, vf(v)
		{
		}

		GraphValue(double v, double vf)
			: v(v)
			, vf(vf)
		{
		}
	};

	struct Interval
	{
		RelPulse length = 0;
	};

	using Graph = ByPulse<GraphValue>;

	struct GraphSection
	{
		ByRelPulse<GraphValue> v;
	};

	template <typename T>
	using Dict = std::unordered_map<std::string, T>;

	template <typename T>
	T Lerp(T value1, T value2, double rate)
	{
		return value1 + static_cast<T>((value2 - value1) * rate);
	}

	template <typename T, typename U>
	auto ValueItrAt(const std::map<T, U>& map, T key)
	{
		auto itr = map.upper_bound(key);
		if (itr != map.begin())
		{
			--itr;
		}
		return itr;
	}

	template <typename T>
	std::size_t CountInRange(const ByPulse<T>& map, Pulse start, Pulse end)
	{
		static_assert(std::is_signed_v<Pulse>);
		assert(start <= end);

		const auto itr1 = map.upper_bound(start - Pulse{ 1 });
		if (itr1 == map.end() || itr1->first >= end)
		{
			return std::size_t{ 0 };
		}
		const auto itr2 = map.upper_bound(end - Pulse{ 1 });
		return static_cast<std::size_t>(std::distance(itr1, itr2));
	}

	template <typename T>
	auto FirstInRange(const ByPulse<T>& map, Pulse start, Pulse end)
	{
		static_assert(std::is_signed_v<Pulse>);
		assert(start <= end);

		const auto itr = map.upper_bound(start - Pulse{ 1 });
		if (itr == map.end() || itr->first >= end)
		{
			return map.end();
		}
		return itr;
	}

	template <typename T>
	bool AlmostEquals(T a, T b)
#ifdef __cpp_concepts
		requires std::is_floating_point_v<T>
#endif
	{
		// Not perfect algorithm, but okay for now.
		return std::abs(a - b) <= std::numeric_limits<T>::epsilon();
	}
}
