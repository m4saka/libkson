#include "ksh/io/ksh_io.hpp"
#include "ksh/encoding/encoding.hpp"
#include <fstream>
#include <sstream>
#include <optional>
#include <charconv>
#include <cmath>
#include <cassert>

namespace
{
	using namespace ksh;

	constexpr char kOptionSeparator = '=';
	constexpr char kBlockSeparator = '|';
	constexpr std::string_view kMeasureSeparator = "--";
	constexpr char kAudioEffectStrSeparator = ';';

	enum BlockIdx : std::size_t
	{
		kBlockIdxBT = 0,
		kBlockIdxFX,
		kBlockIdxLaser,
	};

	// Maximum value of zoom
	constexpr double kZoomAbsMaxLegacy = 300.0;  // ver <  1.67
	constexpr double kZoomAbsMax = 65535.0;      // ver >= 1.67

	// Maximum number of characters of the zoom value
	constexpr std::size_t kZoomMaxCharLegacy = 4;       // ver <  1.67
	constexpr std::size_t kZoomMaxChar = std::string::npos; // ver >= 1.67

	// Maximum value of center_split / manual tilt
	constexpr double kCenterSplitAbsMax = 65535.0;
	constexpr double kManualTiltAbsMax = 1000.0;

	template <typename T>
	T ParseNumeric(std::string_view str)
	{
		T result;
		std::from_chars_result r;
		if constexpr (std::is_integral_v<T>)
		{
			r = std::from_chars(str.data(), str.data() + str.size(), result, 10);
		}
		else
		{
			r = std::from_chars(str.data(), str.data() + str.size(), result, std::chars_format::fixed);
		}

		if (r.ec == std::errc{})
		{
			return static_cast<T>(result);
		}

		// TODO: Error handling
		return T{ 0 };
	}

	template <typename T>
	T ParseNumeric(std::u8string_view str)
	{
		T result;
		std::from_chars_result r;
		if constexpr (std::is_integral_v<T>)
		{
			r = std::from_chars(reinterpret_cast<const char*>(str.data()), reinterpret_cast<const char*>(str.data() + str.size()), result, 10);
		}
		else
		{
			r = std::from_chars(reinterpret_cast<const char*>(str.data()), reinterpret_cast<const char*>(str.data() + str.size()), result, std::chars_format::fixed);
		}

		if (r.ec == std::errc{})
		{
			return static_cast<T>(result);
		}

		// TODO: Error handling
		return T{ 0 };
	}

	template <typename T, typename U>
	T ParseNumeric(const std::basic_string<U>& str)
	{
		return ParseNumeric<T>(std::basic_string_view<U>(str));
	}

	std::u8string ToUTF8(std::string_view str, bool isUTF8)
	{
		if (isUTF8)
		{
			return std::u8string(str.cbegin(), str.cend());
		}
		else
		{
			return Encoding::ShiftJISToUTF8(str);
		}
	}

	bool IsChartLine(std::string_view line)
	{
		return line.find(kBlockSeparator) != std::string_view::npos;
	}

	bool IsOptionLine(std::string_view line)
	{
		return line.find(kOptionSeparator) != std::string_view::npos;
	}

	bool IsBarLine(std::string_view line)
	{
		return line == kMeasureSeparator;
	}

	bool IsCommentLine(std::string_view line)
	{
		return (line.length() >= 1 && line[0] == ';') || (line.length() >= 2 && line[0] == '/' && line[1] == '/');
	}

	std::pair<std::u8string, std::u8string> SplitOptionLine(std::string_view optionLine, bool isUTF8)
	{
		const std::u8string optionLineUTF8 = ToUTF8(optionLine, isUTF8);
		const std::size_t equalIdx = optionLineUTF8.find_first_of(kOptionSeparator);

		// Option line must have "="
		assert(equalIdx != std::u8string_view::npos);

		return {
			optionLineUTF8.substr(0, equalIdx),
			optionLineUTF8.substr(equalIdx + 1)
		};
	}

	std::tuple<std::u8string, std::int64_t, std::int64_t> SplitAudioEffectStr(std::u8string_view optionLine)
	{
		// TODO: default values of linked params
		using Tuple = std::tuple<std::u8string, std::int64_t, std::int64_t>;

		const std::size_t semicolonIdx1 = optionLine.find_first_of(kAudioEffectStrSeparator);
		if (semicolonIdx1 == std::u8string_view::npos)
		{
			return Tuple{ optionLine, 0, 0 };
		}

		const std::size_t semicolonIdx2 = optionLine.substr(semicolonIdx1 + 1).find_first_of(kAudioEffectStrSeparator);
		const std::int64_t linkedParam1 = ParseNumeric<std::int64_t>(optionLine.substr(semicolonIdx1 + 1));
		if (semicolonIdx2 == std::u8string_view::npos)
		{
			return Tuple{ optionLine.substr(0, semicolonIdx1), linkedParam1, 0 };
		}

		const std::int64_t linkedParam2 = ParseNumeric<std::int64_t>(optionLine.substr(semicolonIdx1 + semicolonIdx2 + 2));

		return Tuple{ optionLine.substr(0, semicolonIdx1), linkedParam1, linkedParam2 };
	}

	std::u8string_view KSHLegacyFXCharToKSHAudioEffectStr(char c)
	{
		switch (c)
		{
		case 'S': return u8"Retrigger;8";
		case 'V': return u8"Retrigger;12";
		case 'T': return u8"Retrigger;16";
		case 'W': return u8"Retrigger;24";
		case 'U': return u8"Retrigger;32";
		case 'G': return u8"Gate;4";
		case 'H': return u8"Gate;8";
		case 'K': return u8"Gate;12";
		case 'I': return u8"Gate;16";
		case 'L': return u8"Gate;24";
		case 'J': return u8"Gate;32";
		case 'F': return u8"Flanger";
		case 'P': return u8"PitchShift;12";
		case 'B': return u8"BitCrusher;5";
		case 'Q': return u8"Phaser";
		case 'X': return u8"Wobble;12";
		case 'A': return u8"TapeStop;17";
		case 'D': return u8"SideChain";
		default:  return u8"";
		}
	}

	const std::unordered_map<std::u8string_view, std::u8string_view> s_kshFXToKsonAudioEffectNameTable
	{
		{ u8"Retrigger", u8"retrigger" },
		{ u8"Gate", u8"gate" },
		{ u8"Flanger", u8"flanger" },
		{ u8"PitchShift", u8"pitch_shift" },
		{ u8"BitCrusher", u8"bitcrusher" },
		{ u8"Phaser", u8"phaser" },
		{ u8"Wobble", u8"wobble" },
		{ u8"TapeStop", u8"tapestop" },
		{ u8"Echo", u8"echo" },
		{ u8"SideChain", u8"sidechain" },
		{ u8"SwitchAudio", u8"audio_swap" },
	};

	const std::unordered_map<std::u8string_view, std::u8string_view> s_kshFilterToKsonAudioEffectNameTable
	{
		{ u8"peak", u8"peaking_filter" },
		{ u8"hpf1", u8"high_pass_filter" },
		{ u8"lpf1", u8"low_pass_filter" },
		{ u8"bitc", u8"bitcrusher" },
		// TODO: Add fallback effect option to audio_swap to simulate "fx;bitc"
	};

	static constexpr std::int32_t kLaserXMax = 100;

	std::int32_t CharToLaserX(char c)
	{
		if (c >= '0' && c <= '9')
		{
			return (c - '0') * kLaserXMax / 50;
		}
		else if (c >= 'A' && c <= 'Z')
		{
			return (c - 'A' + 10) * kLaserXMax / 50;
		}
		else if (c >= 'a' && c <= 'o')
		{
			return (c - 'a' + 36) * kLaserXMax / 50;
		}
		else
		{
			return -1;
		}
	}

	bool IsTiltValueManual(std::u8string_view tiltValueStr)
	{
		return !tiltValueStr.empty() && ((tiltValueStr[0] >= '0' && tiltValueStr[0] <= '9') || tiltValueStr[0] == '-');
	}

	TimeSig ParseTimeSig(std::u8string_view str)
	{
		std::size_t slashIdx = str.find('/');

		// TimeSig must have "/"
		assert(slashIdx != std::string::npos);

		return TimeSig{
			ParseNumeric<std::int64_t>(str.substr(0, slashIdx)),
			ParseNumeric<std::int64_t>(str.substr(slashIdx + 1))
		};
	}

	bool EliminateUTF8BOM(std::istream& stream)
	{
		bool isUTF8;
		std::string firstLine;
		std::getline(stream, firstLine, '\n');
		if (firstLine.length() >= 3 &&
			firstLine[0] == '\xEF' &&
			firstLine[1] == '\xBB' &&
			firstLine[2] == '\xBF')
		{
			isUTF8 = true;
			stream.seekg(3, std::ios_base::beg);
		}
		else
		{
			isUTF8 = false;
			stream.seekg(0, std::ios_base::beg);
		}
		return isUTF8;
	}

	bool InsertBPMChange(decltype(ChartData::beat.bpmChanges)& bpmChanges, Pulse time, std::u8string_view value)
	{
		if (value.find('-') != std::u8string_view::npos)
		{
			return false;
		}

		bpmChanges.insert_or_assign(time, ParseNumeric<double>(value));
		return true;
	}

	class PreparedLongNote
	{
	protected:
		bool m_prepared = false;

		Pulse m_time = 0;

		RelPulse m_length = 0;

		ChartData* m_pTargetChartData = nullptr;

		std::size_t m_targetLaneIdx = 0;

	public:
		PreparedLongNote() = default;

		PreparedLongNote(ChartData* pTargetChartData, std::size_t targetLaneIdx)
			: m_pTargetChartData(pTargetChartData)
			, m_targetLaneIdx(targetLaneIdx)
		{
		}

		virtual ~PreparedLongNote() = default;

		void prepare(Pulse time)
		{
			if (!m_prepared)
			{
				m_prepared = true;
				m_time = time;
				m_length = 0;
			}
		}

		bool prepared() const
		{
			return m_prepared;
		}

		void extendLength(RelPulse relPulse)
		{
			m_length += relPulse;
		}

		virtual void clear()
		{
			m_prepared = false;
			m_time = 0;
			m_length = 0;
		}
	};

	class PreparedLongBTNote : public PreparedLongNote
	{
	public:
		PreparedLongBTNote() = default;

		PreparedLongBTNote(ChartData* pTargetChartData, std::size_t targetLaneIdx)
			: PreparedLongNote(pTargetChartData, targetLaneIdx)
		{
		}

		virtual ~PreparedLongBTNote() = default;

		void publishLongBTNote()
		{
			if (!m_prepared)
			{
				return;
			}

			m_pTargetChartData->note.btLanes[m_targetLaneIdx].emplace(
				m_time,
				m_length);

			clear();
		}
	};

	class PreparedLongFXNote : public PreparedLongNote
	{
	protected:
		// FX audio effect string ("fx-l=" or "fx-r=" in .ksh)
		std::u8string m_audioEffectStr;

		// FX audio effect parameters ("fx-l_param1=" or "fx-r_param1=" in .ksh)
		std::u8string m_audioEffectParamStr;

	public:
		PreparedLongFXNote() = default;

		PreparedLongFXNote(ChartData* pTargetChartData, std::size_t targetLaneIdx)
			: PreparedLongNote(pTargetChartData, targetLaneIdx)
		{
		}

		virtual ~PreparedLongFXNote() = default;

		void prepare(Pulse time) = delete;

		void prepare(Pulse time, std::u8string_view audioEffectStr, std::u8string_view audioEffectParamStr)
		{
			if (m_prepared && (audioEffectStr != m_audioEffectStr || audioEffectParamStr != m_audioEffectParamStr))
			{
				publishLongFXNote();
			}
			if (!m_prepared)
			{
				PreparedLongNote::prepare(time);
				m_audioEffectStr = audioEffectStr;
				m_audioEffectParamStr = audioEffectParamStr;
			}
		}

		void publishLongFXNote()
		{
			if (!m_prepared)
			{
				return;
			}

			auto& targetLane = m_pTargetChartData->note.fxLanes[m_targetLaneIdx];

			// Publish prepared long FX note
			const auto [itr, inserted] = targetLane.emplace(
				m_time,
				m_length);

			if (inserted)
			{
				auto [audioEffectName, audioEffectLinkedParamValue1, audioEffectLinkedParamValue2] = SplitAudioEffectStr(m_audioEffectStr);
				if (s_kshFXToKsonAudioEffectNameTable.contains(audioEffectName))
				{
					// Convert the name of preset audio effects
					audioEffectName = s_kshFXToKsonAudioEffectNameTable.at(audioEffectName);
				}
				m_pTargetChartData->audio.audioEffects.noteEventList[audioEffectName].fx.push_back(ByBtnNote<AudioEffectParams>{
					.laneIdx = m_targetLaneIdx,
					.noteIdx = static_cast<std::size_t>(std::distance(targetLane.begin(), itr)),
					.params = {
						// Store the value of the linked parameter in a temporary key
						// (Since the conversion requires determining the type of audio effect, it is processed
						//  after reading the "#define_fx"/"#define_filter" lines.)
						{ u8"_linked_param1", AudioEffectParam(static_cast<double>(audioEffectLinkedParamValue1)) },
						{ u8"_linked_param2", AudioEffectParam(static_cast<double>(audioEffectLinkedParamValue2)) },
					},
				});
			}

			clear();
		}

		virtual void clear() override
		{
			PreparedLongNote::clear();
			m_audioEffectStr.clear();
			m_audioEffectParamStr.clear();
		}
	};

	Pulse KSHLengthToMeasure(std::string_view str, Pulse resolution)
	{
		return ParseNumeric<Pulse>(str) * resolution * 4 / 192;
	}

	std::tuple<Pulse, std::int64_t, std::int64_t, std::int64_t> SplitSwingParams(std::string_view paramStr, Pulse resolution)
	{
		std::array<std::string, 4> params{
			"192", "250", "3", "2"
		};

		const std::string paramStrClone(paramStr);
		std::stringstream ss(paramStrClone);
		std::string s;
		int i = 0;
		while (i < 4 && std::getline(ss, s, ';'))
		{
			params[i] = s;
			++i;
		}

		return std::make_tuple(
			KSHLengthToMeasure(params[0], resolution),
			ParseNumeric<std::int64_t>(params[1]),
			ParseNumeric<std::int64_t>(params[2]),
			ParseNumeric<std::int64_t>(params[3]));
	}

	constexpr double kKSHToKSONSwingScale = 1.0 / 60.0;

	struct PreparedLaneSpin
	{
		enum class Type
		{
			kNoSpin,
			kNormal,
			kHalf,
			kSwing,
		};
		Type type = Type::kNoSpin;

		enum class Direction
		{
			kUnspecified,
			kLeft,
			kRight,
		};
		Direction direction = Direction::kUnspecified;

		Pulse duration = 0;

		std::int64_t swingAmplitude = 0;

		std::int64_t swingRepeat = 0;

		std::int64_t swingDecayOrder = 0;

		static PreparedLaneSpin FromKSHSpinStr(std::string_view strFromKsh, Pulse resolution) // From .ksh spin string (example: "@(192")
		{
			// A .ksh spin string should have at least 3 chars
			if (strFromKsh.length() < 3)
			{
				return {
					.type = Type::kNoSpin,
					.direction = Direction::kUnspecified,
					.duration = 0,
				};
			}

			// Specify the spin type
			Type type;
			Direction direction;
			if (strFromKsh[0] == '@')
			{
				switch (strFromKsh[1])
				{
				case '(':
					type = Type::kNormal;
					direction = Direction::kLeft;
					break;

				case ')':
					type = Type::kNormal;
					direction = Direction::kRight;
					break;

				case '<':
					type = Type::kHalf;
					direction = Direction::kLeft;
					break;

				case '>':
					type = Type::kHalf;
					direction = Direction::kRight;
					break;

				default:
					type = Type::kNoSpin;
					direction = Direction::kUnspecified;
					break;
				}
			}
			else if (strFromKsh[0] == 'S')
			{
				switch (strFromKsh[1])
				{
				case '<':
					type = Type::kSwing;
					direction = Direction::kLeft;
					break;

				case '>':
					type = Type::kSwing;
					direction = Direction::kRight;
					break;

				default:
					type = Type::kNoSpin;
					direction = Direction::kUnspecified;
					break;
				}
			}
			else
			{
				type = Type::kNoSpin;
				direction = Direction::kUnspecified;
			}

			// Specify the spin length
			Pulse duration;
			std::int64_t swingAmplitude = 0, swingRepeat = 0, swingDecayOrder = 0;
			if (type == Type::kNoSpin || direction == Direction::kUnspecified)
			{
				duration = 0;
			}
			else if (type == Type::kSwing)
			{
				std::tie(duration, swingAmplitude, swingRepeat, swingDecayOrder) = SplitSwingParams(strFromKsh.substr(2), resolution);
			}
			else
			{
				duration = KSHLengthToMeasure(strFromKsh.substr(2), resolution);
			}

			return {
				.type = type,
				.direction = direction,
				.duration = duration,
				.swingAmplitude = swingAmplitude,
				.swingRepeat = swingRepeat,
				.swingDecayOrder = swingDecayOrder,
			};
		}

		bool isValid() const
		{
			return type != Type::kNoSpin && direction != Direction::kUnspecified;
		}
	};

	const std::unordered_map<PreparedLaneSpin::Type, std::u8string_view> s_kshSpinTypeToKsonCamPatternNameTable
	{
		{ PreparedLaneSpin::Type::kNoSpin, u8"" },
		{ PreparedLaneSpin::Type::kNormal, u8"spin" },
		{ PreparedLaneSpin::Type::kHalf, u8"half_spin" },
		{ PreparedLaneSpin::Type::kSwing, u8"swing" },
	};

	constexpr double kNormalTiltAssignRotateZ = 10.0;

	const std::unordered_map<std::u8string_view, double> s_tiltTypeScaleTable
	{
		{ u8"normal", 1.0 },
		{ u8"bigger", 1.75 },
		{ u8"biggest", 2.5 },
		{ u8"keep_normal", 1.0 },
		{ u8"keep_bigger", 1.75 },
		{ u8"keep_biggest", 2.5 },
		{ u8"zero", 0.0 },
		{ u8"big", 1.75 },  // legacy
		{ u8"keep", 1.75 }, // legacy
	};

	const std::unordered_map<std::u8string_view, bool> s_tiltTypeKeepTable
	{
		{ u8"normal", false },
		{ u8"bigger", false },
		{ u8"biggest", false },
		{ u8"keep_normal", true },
		{ u8"keep_bigger", true },
		{ u8"keep_biggest", true },
		{ u8"zero", false },
		{ u8"big", false }, // legacy
		{ u8"keep", true }, // legacy
	};

	class PreparedGraphSection
	{
	protected:
		bool m_prepared = false;

		Pulse m_time = 0;

		ByRelPulse<GraphValue> m_values;

		ChartData* m_pTargetChartData = nullptr;

	public:
		PreparedGraphSection() = default;

		explicit PreparedGraphSection(ChartData* pTargetChartData)
			: m_pTargetChartData(pTargetChartData)
		{
		}

		virtual ~PreparedGraphSection() = default;

		void prepare(Pulse time)
		{
			if (!m_prepared)
			{
				m_prepared = true;
				m_time = time;
				m_values.clear();
			}
		}

		bool prepared() const
		{
			return m_prepared;
		}

		void addGraphPoint(Pulse time, double value)
		{
			const RelPulse relativeTime = time - m_time;
			if (relativeTime < 0)
			{
				return;
			}
			if (m_values.contains(relativeTime))
			{
				m_values.at(relativeTime).vf = value;
			}
			else
			{
				m_values.emplace(relativeTime, value);
			}
		}

		void publishManualTilt()
		{
			if (!m_prepared)
			{
				return;
			}

			m_pTargetChartData->camera.tilts.manualTilts.emplace(
				m_time,
				m_values);

			clear();
		}

		virtual void clear()
		{
			m_prepared = false;
			m_time = 0;
			m_values.clear();
		}
	};

	class PreparedLaserSection : public PreparedGraphSection
	{
	private:
		std::size_t m_targetLaneIdx = 0;
		ByRelPulse<PreparedLaneSpin> m_preparedLaneSpins;

	public:
		PreparedLaserSection() = default;

		PreparedLaserSection(ChartData* pTargetChartData, std::size_t targetLaneIdx)
			: PreparedGraphSection(pTargetChartData)
			, m_targetLaneIdx(targetLaneIdx)
		{
		}

		virtual ~PreparedLaserSection() = default;

		void publishManualTilt() = delete;

		void publishLaserNote()
		{
			if (!m_prepared)
			{
				return;
			}

			auto& targetLane = m_pTargetChartData->note.laserLanes[m_targetLaneIdx];

			// Publish prepared laser section
			const auto [itr, inserted] = targetLane.emplace(
				m_time,
				LaserSection{
					.points = m_values,
					.xScale = 1, // TODO
				});

			if (inserted)
			{
				// Publish prepared lane spin
				const std::size_t sectionIdx = static_cast<std::size_t>(std::distance(targetLane.begin(), itr));
				for (const auto& [relPulse, laneSpin] : m_preparedLaneSpins)
				{
					if (m_values.contains(relPulse) && laneSpin.isValid() && s_kshSpinTypeToKsonCamPatternNameTable.contains(laneSpin.type))
					{
						const std::size_t pointIdx = static_cast<std::size_t>(std::distance(m_values.begin(), m_values.find(relPulse)));

						const std::u8string patternKey(s_kshSpinTypeToKsonCamPatternNameTable.at(laneSpin.type));

						CamPatternParams params;
						if (laneSpin.type == PreparedLaneSpin::Type::kSwing)
						{
							params = {
								.length = laneSpin.duration,
								.scale = static_cast<double>(laneSpin.swingAmplitude) * kKSHToKSONSwingScale,
								.repeat = laneSpin.swingRepeat,
								.decayOrder = static_cast<double>(laneSpin.swingDecayOrder),
							};
						}

						m_pTargetChartData->camera.cams.patternInfo.noteEventList[patternKey].laser.push_back({
							.laneIdx = m_targetLaneIdx,
							.sectionIdx = sectionIdx,
							.pointIdx = pointIdx,
							.params = {
								.length = laneSpin.duration,
							},
						});
					}
				}
			}

			clear();
		}

		virtual void clear() override
		{
			PreparedGraphSection::clear();

			m_targetLaneIdx = 0;
			m_preparedLaneSpins.clear();
		}

		void addLaneSpin(Pulse time, const PreparedLaneSpin& laneSpin)
		{
			m_preparedLaneSpins.emplace(time, laneSpin);
		}
	};

	template <class T, std::size_t N>
	std::array<T, N> MakePreparedLongNoteArray(ChartData* pTargetChartData)
	{
		std::array<T, N> arr;
		for (std::size_t i = 0; i < N; ++i)
		{
			arr[i] = T(pTargetChartData, i);
		}
		return arr;
	}

	struct PreparedLongNoteArray
	{
		std::array<PreparedLongBTNote, kNumBTLanes> bt;
		std::array<PreparedLongFXNote, kNumFXLanes> fx;
		std::array<PreparedLaserSection, kNumLaserLanes> laser;

		explicit PreparedLongNoteArray(ChartData* pTargetChartData)
			: bt(MakePreparedLongNoteArray<PreparedLongBTNote, kNumBTLanes>(pTargetChartData))
			, fx(MakePreparedLongNoteArray<PreparedLongFXNote, kNumFXLanes>(pTargetChartData))
			, laser(MakePreparedLongNoteArray<PreparedLaserSection, kNumLaserLanes>(pTargetChartData))
		{
		}
	};

	struct BufOptionLine
	{
		std::size_t lineIdx;
		std::u8string key;
		std::u8string value;
	};

	std::unordered_map<std::u8string, std::u8string> LoadKSHMetaDataHashMap(std::istream& stream, bool* pIsUTF8)
	{
		std::unordered_map<std::u8string, std::u8string> metaDataHashMap;

		const bool isUTF8 = EliminateUTF8BOM(stream);
		if (pIsUTF8)
		{
			*pIsUTF8 = isUTF8;
		}

		std::string line;
		bool barLineExists = false;
		while (std::getline(stream, line, '\n'))
		{
			// Eliminate CR
			if (!line.empty() && *line.crbegin() == '\r')
			{
				line.pop_back();
			}

			if (IsBarLine(line))
			{
				// Chart meta data is before the first bar line ("--")
				barLineExists = true;
				break;
			}

			// Skip comments
			// TODO: Store comments if editor
			if (IsCommentLine(line))
			{
				continue;
			}

			// Skip unexpected header lines
			if (!IsOptionLine(line))
			{
				continue;
			}

			const auto [key, value] = SplitOptionLine(line, isUTF8);
			metaDataHashMap.insert_or_assign(std::move(key), std::move(value));
		}

		// .ksh files must have at least one bar line ("--")
		assert(barLineExists);

		return metaDataHashMap;
	}
}

ksh::ChartData ksh::LoadKSHChartData(std::istream& stream)
{
	bool isUTF8 = false;
	const auto meta = LoadKSHMetaDataHashMap(stream, &isUTF8);

	ChartData chartData;

	// Insert the first tempo change
	double currentTempo = 120.0;
	if (meta.count(u8"t"))
	{
		if (InsertBPMChange(chartData.beat.bpmChanges, 0, meta.at(u8"t")))
		{
			currentTempo = chartData.beat.bpmChanges.at(0);
		}
	}

	// Insert the first time signature change
	TimeSig currentTimeSig{ 4, 4 };
	if (meta.count(u8"beat"))
	{
		currentTimeSig = ParseTimeSig(meta.at(u8"beat"));
	}
	chartData.beat.timeSigChanges.emplace(0, currentTimeSig);

	if (meta.count(u8"beat"))
	{
		chartData.meta.kshVersion = meta.at(u8"ver");
	}
	else
	{
		chartData.meta.kshVersion = u8"100";
	}
	std::int64_t kshVersionInt = ParseNumeric<std::int64_t>(chartData.meta.kshVersion);

	// For backward compatibility of zoom_top/zoom_bottom/zoom_side
	const double zoomAbsMax = (kshVersionInt >= 167) ? kZoomAbsMax : kZoomAbsMaxLegacy;
	const std::size_t zoomMaxChar = (kshVersionInt >= 167) ? kZoomMaxChar : kZoomMaxCharLegacy;

	// Buffers
	// (needed because actual addition cannot come before the pulse value calculation)
	std::vector<std::string> chartLines;
	std::vector<BufOptionLine> optionLines;
	PreparedLongNoteArray preparedLongNoteArray(&chartData);

	// FX audio effect string ("fx-l=" or "fx-r=" in .ksh)
	std::array<std::u8string, kNumFXLanes> currentFXAudioEffectStrs;

	// FX audio effect parameters ("fx-l_param1=" or "fx-r_param1=" in .ksh; currently no "param2")
	std::array<std::u8string, kNumFXLanes> currentFXAudioEffectParamStrs;

	// GraphSections buffers
	PreparedGraphSection preparedManualTilt(&chartData);

	Pulse currentPulse = 0;
	std::int64_t currentMeasureIdx = 0;

	// Read chart body
	// The stream start from the next of the first bar line ("--")
	std::string line;
	while (std::getline(stream, line, '\n'))
	{
		// Eliminate CR
		if (!line.empty() && *line.crbegin() == '\r')
		{
			line.pop_back();
		}

		// Skip comments
		if (IsCommentLine(line))
		{
			continue;
		}

		// TODO: Read user-defined audio effects
		if (line[0] == '#')
		{
			continue;
		}

		if (IsChartLine(line))
		{
			chartLines.push_back(line);
		}
		else if (IsOptionLine(line))
		{
			const auto [key, value] = SplitOptionLine(line, isUTF8);
			if (key == u8"t")
			{
				if (value.find(u8'-') == std::u8string::npos)
				{
					currentTempo = ParseNumeric<double>(value);
				}
				optionLines.push_back({
					.lineIdx = chartLines.size(),
					.key = key,
					.value = value,
				});
			}
			else if (key == u8"beat")
			{
				currentTimeSig = ParseTimeSig(value);
				chartData.beat.timeSigChanges.emplace(currentMeasureIdx, currentTimeSig);
			}
			else if (key == u8"fx-l")
			{
				currentFXAudioEffectStrs[0] = value;
			}
			else if (key == u8"fx-r")
			{
				currentFXAudioEffectStrs[1] = value;
			}
			else if (key == u8"fx-l_param1")
			{
				currentFXAudioEffectParamStrs[0] = value;
			}
			else if (key == u8"fx-r_param1")
			{
				currentFXAudioEffectParamStrs[1] = value;
			}
			else
			{
				optionLines.push_back({
					.lineIdx = chartLines.size(),
					.key = key,
					.value = value,
				});
			}
		}
		else if (IsBarLine(line))
		{
			const std::size_t bufLineCount = chartLines.size();
			const Pulse oneLinePulse = chartData.beat.resolution * currentTimeSig.numerator / currentTimeSig.denominator / bufLineCount;

			// Add options that require their position
			for (const auto& [lineIdx, key, value] : optionLines)
			{
				Pulse time = currentPulse + lineIdx * oneLinePulse;
				if (key == u8"t")
				{
					InsertBPMChange(chartData.beat.bpmChanges, time, value);
				}
				else if (key == u8"zoom_top")
				{
					const double dValue = ParseNumeric<double>(std::u8string_view(value).substr(0, zoomMaxChar));
					if (std::abs(dValue) <= zoomAbsMax || (kshVersionInt < 167 && chartData.camera.cams.body.rotationX.contains(time)))
					{
						chartData.camera.cams.body.rotationX.insert_or_assign(time, dValue);
					}
				}
				else if (key == u8"zoom_bottom")
				{
					const double dValue = ParseNumeric<double>(std::u8string_view(value).substr(0, zoomMaxChar));
					if (std::abs(dValue) <= zoomAbsMax || (kshVersionInt < 167 && chartData.camera.cams.body.zoom.contains(time)))
					{
						chartData.camera.cams.body.zoom.insert_or_assign(time, dValue);
					}
				}
				else if (key == u8"zoom_side")
				{
					const double dValue = ParseNumeric<double>(std::u8string_view(value).substr(0, zoomMaxChar));
					if (std::abs(dValue) <= zoomAbsMax || (kshVersionInt < 167 && chartData.camera.cams.body.shiftX.contains(time)))
					{
						chartData.camera.cams.body.shiftX.insert_or_assign(time, dValue);
					}
				}
				else if (key == u8"center_split")
				{
					const double dValue = ParseNumeric<double>(value);
					if (std::abs(dValue) <= kCenterSplitAbsMax)
					{
						chartData.camera.cams.body.centerSplit.insert_or_assign(time, dValue);
					}
				}
				else if (key == u8"tilt")
				{
					if (IsTiltValueManual(value))
					{
						const double dValue = ParseNumeric<double>(value);
						if (std::abs(dValue) <= kManualTiltAbsMax)
						{
							preparedManualTilt.prepare(time);
							preparedManualTilt.addGraphPoint(time, dValue);
						}
					}
					else
					{
						if (preparedManualTilt.prepared())
						{
							preparedManualTilt.publishManualTilt();
						}

						double tiltAssignRotateZ = kNormalTiltAssignRotateZ;
						if (s_tiltTypeScaleTable.contains(value))
						{
							tiltAssignRotateZ *= s_tiltTypeScaleTable.at(value);
						}
						auto& target = chartData.camera.cams.tiltAssignScale.rotationZ;
						const double prevValue = target.empty() ? kNormalTiltAssignRotateZ : target.crbegin()->second.vf;
						target.insert_or_assign(time, GraphValue(prevValue, tiltAssignRotateZ));

						// TODO: Keep tilt
					}
				}
				else
				{
					chartData.impl.kshUnknownOptionLines.emplace(time, KSHUnknownOptionLine{
						.key = key,
						.value = value,
					});
				}
			}

			// Add notes
			for (std::size_t i = 0; i < bufLineCount; ++i)
			{
				const std::string_view buf = chartLines.at(i);
				std::size_t currentBlock = 0;
				std::size_t laneIdx = 0;

				const Pulse time = currentPulse + i * oneLinePulse;

				for (std::size_t j = 0; j < buf.size(); ++j)
				{
					if (buf[j] == kBlockSeparator)
					{
						++currentBlock;
						laneIdx = 0;
						continue;
					}

					if (currentBlock == kBlockIdxBT && laneIdx < kNumBTLanes) // BT notes
					{
						auto& preparedLongNoteRef = preparedLongNoteArray.bt[laneIdx];
						switch (buf[j])
						{
						case '2': // Long BT note
							if (!preparedLongNoteRef.prepared())
							{
								preparedLongNoteRef.prepare(time);
							}
							preparedLongNoteRef.extendLength(oneLinePulse);
							break;
						case '1': // Chip BT note
							preparedLongNoteRef.publishLongBTNote();
							chartData.note.btLanes[laneIdx].emplace(time, 0);
							break;
						default:  // Empty
							preparedLongNoteRef.publishLongBTNote();
							break;
						}
					}
					else if (currentBlock == kBlockIdxFX && laneIdx < kNumFXLanes) // FX notes
					{
						auto& preparedLongNoteRef = preparedLongNoteArray.fx[laneIdx];
						switch (buf[j])
						{
						case '2': // Chip FX note
							chartData.note.fxLanes[laneIdx].emplace(time, 0);
							break;
						case '0': // Empty
							preparedLongNoteRef.publishLongFXNote();
							break;
						default:  // Long FX note
							const std::u8string audioEffectStr((buf[j] == '1') ? currentFXAudioEffectStrs[laneIdx] : KSHLegacyFXCharToKSHAudioEffectStr(buf[j]));
							preparedLongNoteRef.prepare(time, audioEffectStr, currentFXAudioEffectParamStrs[laneIdx]);
							preparedLongNoteRef.extendLength(oneLinePulse);
						}
					}
					else if (currentBlock == kBlockIdxLaser && laneIdx < kNumLaserLanes) // Laser notes
					{
						auto& preparedLaserSectionRef = preparedLongNoteArray.laser[laneIdx];
						switch (buf[j])
						{
						case '-': // Empty
							preparedLaserSectionRef.publishLaserNote();
							preparedLaserSectionRef.clear();
							break;
						case ':': // Connection
							break;
						default:
						{
							const std::int32_t laserX = CharToLaserX(buf[j]);
							if (laserX >= 0)
							{
								preparedLaserSectionRef.prepare(time);

								const double dLaserX = static_cast<double>(laserX) / kLaserXMax;
								preparedLaserSectionRef.addGraphPoint(time, dLaserX);
							}
						}
						}
					}
					else if (currentBlock == kBlockIdxLaser && laneIdx == kNumLaserLanes) // Lane spin
					{
						// Create a lane spin from string
						const PreparedLaneSpin laneSpin = PreparedLaneSpin::FromKSHSpinStr(buf.substr(j), chartData.beat.resolution);
						if (laneSpin.isValid())
						{
							// Assign to the laser point if valid
							for (auto& lane : preparedLongNoteArray.laser)
							{
								lane.addLaneSpin(time, laneSpin);
							}
						}
					}
					++laneIdx;
				}
			}
			chartLines.clear();
			optionLines.clear();
			for (auto& str : currentFXAudioEffectStrs)
			{
				str.clear();
			}
			for (auto& str : currentFXAudioEffectParamStrs)
			{
				str.clear();
			}
			currentPulse += chartData.beat.resolution * 4 * currentTimeSig.numerator / currentTimeSig.denominator;
			++currentMeasureIdx;
		}
	}

	// Publish the last manual tilt section if exists
	if (preparedManualTilt.prepared())
	{
		preparedManualTilt.publishManualTilt();
	}

	// KSH file must end with the bar line "--", so there can never be a prepared button note here
	for (const auto& preparedBTNote : preparedLongNoteArray.bt)
	{
		assert(!preparedBTNote.prepared());
	}
	for (const auto& preparedFXNote : preparedLongNoteArray.fx)
	{
		assert(!preparedFXNote.prepared());
	}

	// The prepared laser section is published only when the laser lane is blank ("-"), so there can be unpublished laser sections here
	for (auto& preparedFXSection : preparedLongNoteArray.laser)
	{
		preparedFXSection.publishLaserNote();
	}

	return chartData;
}
