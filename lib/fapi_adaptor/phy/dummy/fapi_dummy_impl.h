// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "fapi_dummy_sector.h"
#include "ocudu/fapi_adaptor/phy/dummy/fapi_dummy_config.h"
#include "ocudu/fapi_adaptor/phy/phy_fapi_adaptor.h"
#include "ocudu/support/synchronization/stop_event.h"
#include <chrono>
#include <memory>
#include <optional>
#include <vector>

namespace ocudu {
namespace fapi_adaptor {

/// FAPI dummy that drives the slot boundaries of its sectors from a clock loop.
class fapi_dummy_impl : public phy_fapi_adaptor
{
public:
  fapi_dummy_impl(const fapi_dummy_config& config, const fapi_dummy_dependencies& dependencies);

  ~fapi_dummy_impl() override;

  // See interface for documentation.
  phy_fapi_sector_adaptor& get_sector_adaptor(unsigned cell_id) override;

private:
  /// Minimum time between clock loop iterations.
  static constexpr std::chrono::microseconds minimum_loop_time{10};

  /// Runs the clock loop until a stop is requested.
  void clock_loop(const rt_stop_event_token& token);

  /// Advances the slot following the steady clock.
  void run_realtime_iteration();

  /// Returns the system slot count given by the steady clock.
  uint64_t get_current_system_slot() const;

  /// Advances the slot once all the running sectors completed the last notified slot.
  void run_free_running_iteration();

  /// Notifies the next slot boundary to all the sectors.
  void advance_slot();

  const fapi_dummy_clock_mode     clock_mode;
  const std::chrono::microseconds slot_duration;
  const unsigned                  max_processing_delay_slots;
  /// Number of slots in all the hyper system frames.
  const uint64_t nof_slots_in_all_hyper_sfns;
  /// Maximum time the free running clock waits for the sectors to complete a slot.
  const std::chrono::milliseconds free_running_timeout{100};
  ocudulog::basic_logger&         logger;
  /// Last slot boundary, without the processing delay.
  slot_point_extended current_slot;
  /// Steady clock time of the clock origin.
  std::chrono::steady_clock::time_point origin_time;
  /// System slot count at the clock origin, given by the system time.
  uint64_t origin_system_slot = 0;
  /// Last slot notified to the sectors.
  std::optional<slot_point> last_notified_slot;
  /// Time of the last slot advance of the free running clock.
  std::chrono::steady_clock::time_point           last_advance_time;
  std::vector<std::unique_ptr<fapi_dummy_sector>> sectors;
  /// Clock loop stop control.
  rt_stop_event_source stop_control;
};

} // namespace fapi_adaptor
} // namespace ocudu
