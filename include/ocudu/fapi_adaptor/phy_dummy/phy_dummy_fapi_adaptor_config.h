// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI

#pragma once

#include "ocudu/ocudulog/logger.h"
#include "ocudu/ran/subcarrier_spacing.h"
#include <vector>

namespace ocudu {

class task_executor;

namespace fapi_adaptor {

/// Slot clock modes of the PHY dummy FAPI adaptor.
enum class phy_dummy_fapi_adaptor_clock_mode {
  /// Slots advance with the system time.
  realtime,
  /// A slot advances once every running sector has received the last request of the previous slot.
  free_running
};

/// PHY dummy FAPI adaptor configuration.
struct phy_dummy_fapi_adaptor_config {
  /// Subcarrier spacing of all sectors.
  subcarrier_spacing scs = subcarrier_spacing::kHz30;
  /// Number of sectors.
  unsigned nof_sectors = 1;
  /// Slot clock mode.
  phy_dummy_fapi_adaptor_clock_mode clock_mode = phy_dummy_fapi_adaptor_clock_mode::realtime;
  /// Number of slots between an uplink slot and the notification of its indications. Must be at least one.
  unsigned ul_indication_delay_slots = 4;
  /// Number of slots a SLOT.indication is notified in advance of the transmission time.
  unsigned max_processing_delay_slots = 6;
};

/// PHY dummy FAPI adaptor dependencies.
struct phy_dummy_fapi_adaptor_dependencies {
  /// Logger.
  ocudulog::basic_logger& logger;
  /// Executor that runs the slot clock loop.
  task_executor& clock_executor;
  /// Sequential executor of each sector, indexed by sector. They must run until the PHY dummy FAPI adaptor is
  /// destroyed.
  std::vector<task_executor*> sector_executors;
};

} // namespace fapi_adaptor
} // namespace ocudu
