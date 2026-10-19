// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "fapi_dummy_impl.h"
#include "ocudu/fapi_adaptor/phy/dummy/fapi_dummy_factory.h"
#include "ocudu/ocudulog/ocudulog.h"
#include "ocudu/support/error_handling.h"
#include "ocudu/support/executors/task_executor.h"
#include "ocudu/support/math/math_utils.h"
#include "ocudu/support/ocudu_assert.h"
#include "ocudu/support/rtsan.h"
#include "ocudu/support/synchronization/sync_event.h"
#include <thread>

using namespace ocudu;
using namespace fapi_adaptor;

/// Returns the count of the slot given by the system time.
static uint64_t get_current_system_slot(std::chrono::microseconds slot_duration, uint64_t nof_slots_in_all_hyper_sfns)
{
  auto time_since_epoch =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch());

  return (time_since_epoch / slot_duration) % nof_slots_in_all_hyper_sfns;
}

fapi_dummy_impl::fapi_dummy_impl(const fapi_dummy_config& config, const fapi_dummy_dependencies& dependencies) :
  clock_mode(config.clock_mode),
  slot_duration(1000U / pow2(to_numerology_value(config.scs))),
  max_processing_delay_slots(config.max_processing_delay_slots),
  logger(*dependencies.logger),
  current_slot(config.scs, 0)
{
  ocudu_assert(dependencies.clock_executor != nullptr, "Invalid clock executor");
  ocudu_assert(config.nof_sectors > 0, "The FAPI dummy requires at least one sector");
  ocudu_assert(dependencies.sector_executors.size() == config.nof_sectors,
               "The number of sector executors (i.e., {}) does not match the number of sectors (i.e., {})",
               dependencies.sector_executors.size(),
               config.nof_sectors);

  sectors.reserve(config.nof_sectors);
  for (unsigned i_sector = 0; i_sector != config.nof_sectors; ++i_sector) {
    ocudu_assert(dependencies.sector_executors[i_sector] != nullptr, "Invalid executor of sector {}", i_sector);
    sectors.push_back(std::make_unique<fapi_dummy_sector>(
        fapi_dummy_sector_config{.sector_id                  = i_sector,
                                 .scs                        = config.scs,
                                 .ul_indication_delay_slots  = config.ul_indication_delay_slots,
                                 .max_processing_delay_slots = config.max_processing_delay_slots,
                                 .late_request_detection     = config.clock_mode == fapi_dummy_clock_mode::realtime},
        logger,
        *dependencies.sector_executors[i_sector]));
  }

  current_slot = slot_point_extended(
      config.scs, get_current_system_slot(slot_duration, current_slot.nof_slots_in_all_hyper_sfns()));
  last_advance_time = std::chrono::steady_clock::now();

  sync_event start_event;
  if (!dependencies.clock_executor->defer(
          [this, start_token = start_event.get_token(), stop_token = stop_control.get_token()]() mutable {
            start_token.reset();
            clock_loop(stop_token);
          })) {
    report_fatal_error("Unable to start the FAPI dummy clock loop");
  }
  start_event.wait();
}

fapi_dummy_impl::~fapi_dummy_impl()
{
  // The clock stops before the sectors, so no slot boundary is dispatched to a destroyed sector.
  stop_control.stop();
}

phy_fapi_sector_adaptor& fapi_dummy_impl::get_sector_adaptor(unsigned cell_id)
{
  ocudu_assert(cell_id < sectors.size(), "Invalid cell identifier {}", cell_id);
  return *sectors[cell_id];
}

void fapi_dummy_impl::clock_loop(const rt_stop_event_token& token)
{
  while (!token.is_stop_requested()) {
    if (clock_mode == fapi_dummy_clock_mode::realtime) {
      run_realtime_iteration();
    } else {
      run_free_running_iteration();
    }
  }
}

void fapi_dummy_impl::run_realtime_iteration()
{
  uint64_t slot_count = get_current_system_slot(slot_duration, current_slot.nof_slots_in_all_hyper_sfns());

  if (slot_count == current_slot.system_slot()) {
    OCUDU_RTSAN_SCOPED_DISABLER(scoped_disabler);
    std::this_thread::sleep_for(minimum_loop_time);
    return;
  }

  while (slot_count != current_slot.system_slot()) {
    advance_slot();
  }
}

void fapi_dummy_impl::run_free_running_iteration()
{
  auto now = std::chrono::steady_clock::now();

  bool all_done    = true;
  bool any_running = false;
  if (last_notified_slot.has_value()) {
    for (const auto& sector : sectors) {
      fapi_dummy_sector::slot_boundary_progress progress = sector->get_progress();
      bool                                      done = progress.slot.valid() && progress.slot == *last_notified_slot;
      all_done                                       = all_done && done;
      any_running                                    = any_running || (done && progress.running);
    }
  }

  // Without running sectors, slot boundaries still advance at the slot rate to process START and STOP requests.
  bool advance = all_done && (any_running || now - last_advance_time >= slot_duration);
  if (!all_done && now - last_advance_time >= free_running_timeout) {
    logger.warning("FAPI dummy: Advancing slot {} without all sectors completing it", *last_notified_slot);
    advance = true;
  }

  if (!advance) {
    OCUDU_RTSAN_SCOPED_DISABLER(scoped_disabler);
    std::this_thread::sleep_for(minimum_loop_time);
    return;
  }

  last_advance_time = now;
  advance_slot();
}

void fapi_dummy_impl::advance_slot()
{
  ++current_slot;

  slot_point_extended notified_slot = current_slot + max_processing_delay_slots;
  auto                time_point    = std::chrono::system_clock::now() + slot_duration * max_processing_delay_slots;
  last_notified_slot                = notified_slot.without_hyper_sfn();

  for (auto& sector : sectors) {
    sector->handle_slot_boundary(notified_slot, time_point);
  }
}

std::unique_ptr<phy_fapi_adaptor> ocudu::fapi_adaptor::create_fapi_dummy(const fapi_dummy_config&       config,
                                                                         const fapi_dummy_dependencies& dependencies)
{
  ocudu_assert(dependencies.logger != nullptr, "Invalid logger");
  return std::make_unique<fapi_dummy_impl>(config, dependencies);
}
