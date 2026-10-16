// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/ocudulog/logger.h"
#include "ocudu/ran/subcarrier_spacing.h"

namespace ocudu {

class task_executor;

namespace fapi_adaptor {

/// Slot clock modes of the FAPI dummy.
enum class fapi_dummy_clock_mode {
  /// Slots advance with the system time.
  realtime,
  /// A slot advances once every running sector has received the last request of the previous slot.
  free_running
};

/// FAPI dummy configuration.
struct fapi_dummy_config {
  /// Subcarrier spacing of all sectors.
  subcarrier_spacing scs = subcarrier_spacing::kHz30;
  /// Number of sectors.
  unsigned nof_sectors = 1;
  /// Slot clock mode.
  fapi_dummy_clock_mode clock_mode = fapi_dummy_clock_mode::realtime;
  /// Number of slots between an uplink slot and the notification of its indications.
  unsigned ul_indication_delay_slots = 4;
  /// Number of slots a SLOT.indication is notified in advance of the transmission time.
  unsigned max_processing_delay_slots = 2;
};

/// FAPI dummy dependencies.
struct fapi_dummy_dependencies {
  /// Logger.
  ocudulog::basic_logger* logger = nullptr;
  /// Executor that runs the slot clock loop.
  task_executor* executor = nullptr;
};

} // namespace fapi_adaptor
} // namespace ocudu
