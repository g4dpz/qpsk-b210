#pragma once

/**
 * @file common_options.hpp
 * @brief The option groups the offline subcommands share, and their conversion to
 *        library configuration.
 *
 * Three groups, named as in the design:
 *   - common:      --profile --mod --sps --rolloff --span --payload-bytes --seed --rate --baud
 *   - receiver:    --detect-threshold
 *   - simulation:  --ebn0 --cfo-hz --phase-deg --timing-frac --delay-samples
 *                  --clock-ppm --amp-db --drift --phase-walk --noise-seed
 *
 * Each group has an add_ function that declares the options on a parser and a
 * read_ function that turns the parsed values into a configuration. The read_
 * functions check the combinations a single option's range cannot (the modulation
 * must be one this build enables, samples per symbol times span must be even) and
 * report them as UsageError naming the options, so the user is not left with a
 * message about a library parameter.
 */

#include <cstdint>
#include <optional>
#include <string>

#include "app/option_parser.hpp"
#include "dsp/modulator.hpp"
#include "dsp/receiver.hpp"
#include "sim/channel.hpp"

namespace mpsk::app {

/**
 * A named set of defaults. "default" is the bench setting (250 kBd, roll-off 0.35). "qo100-nb"
 * fits the 2.7 kHz channel of the QO-100 narrowband transponder (Requirement 12): 2000 Bd,
 * roll-off 0.25 (nominal bandwidth 2.5 kHz) and a pulse span of 21 symbols, which pulls the
 * far skirts of the spectrum inside the channel (see the design). An option given on the
 * command line always overrides the profile's value.
 */
constexpr const char* kProfileDefault = "default";
constexpr const char* kProfileNarrowband = "qo100-nb";

struct PhyOptions {
  std::string profile = kProfileDefault;
  int modulation_order = 4;
  int samples_per_symbol = 4;
  double rolloff = 0.35;
  int span_symbols = 11;
  std::size_t payload_bytes = 128;
  std::uint32_t prbs_seed = 3;
  double sample_rate_hz = 1e6;
  double detect_threshold = 0.25;   ///< stage-1 threshold; set by the receiver group

  double symbol_rate_hz() const { return sample_rate_hz / samples_per_symbol; }

  /**
   * The carrier offset the preamble alone is reliable to: 3% of the symbol rate (7.5 kHz at
   * 250 kBd, measured in task 6; the theoretical limit is 3.125%). A wider search is
   * task 10A.4.
   */
  double acquisition_range_hz() const { return 0.03 * symbol_rate_hz(); }
  dsp::ModulatorConfig modulator() const;
  dsp::ReceiverConfig receiver() const;
};

void add_phy_options(OptionParser& parser);
void add_receiver_options(OptionParser& parser);
void add_simulation_options(OptionParser& parser);

/**
 * @throws UsageError for a modulation this build does not enable (8-PSK in stage 1,
 *         with a message saying so), or a pulse whose samples per symbol times span
 *         is odd.
 */
PhyOptions read_phy_options(const ParsedOptions& values);

/** Reads --detect-threshold into @p phy. */
void read_receiver_options(const ParsedOptions& values, PhyOptions& phy);

struct SimulationOptions {
  sim::ChannelConfig channel;         ///< everything but the noise level
  std::optional<double> ebn0_db;      ///< absent means no noise
};

SimulationOptions read_simulation_options(const ParsedOptions& values, const PhyOptions& phy);

/** The name the user types for a modulation order: "qpsk", "8psk". */
std::string modulation_name(int order);

}  // namespace mpsk::app
