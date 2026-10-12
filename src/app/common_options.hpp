#pragma once

/**
 * @file common_options.hpp
 * @brief The option groups the offline subcommands share, and their conversion to
 *        library configuration.
 *
 * Three groups, named as in the design:
 *   - common:      --mod --sps --rolloff --span --payload-bytes --seed --rate
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

#include "app/option_parser.hpp"
#include "dsp/modulator.hpp"
#include "dsp/receiver.hpp"
#include "sim/channel.hpp"

namespace mpsk::app {

struct PhyOptions {
  int modulation_order = 4;
  int samples_per_symbol = 4;
  double rolloff = 0.35;
  int span_symbols = 11;
  std::size_t payload_bytes = 128;
  std::uint32_t prbs_seed = 3;
  double sample_rate_hz = 1e6;
  double detect_threshold = 0.25;   ///< stage-1 threshold; set by the receiver group

  double symbol_rate_hz() const { return sample_rate_hz / samples_per_symbol; }
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
