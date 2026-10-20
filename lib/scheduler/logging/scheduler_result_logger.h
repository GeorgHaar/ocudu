// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "log_event_time.h"
#include "ocudu/ocudulog/logger.h"
#include "ocudu/ran/pci.h"
#include "ocudu/scheduler/result/sched_result_grid.h"

namespace ocudu {

class scheduler_result_logger
{
public:
  explicit scheduler_result_logger(bool log_broadcast_, pci_t pci_);

  /// Stamps the next log entries with \c t instead of the current time. An empty value restores the current time.
  void set_event_time(log_event_time t) { event_time = t; }

  /// Log scheduler result for a specific cell and slot.
  /// \param[in] result Scheduling result for this slot.
  /// \param[in] slot_latency Latency that it took for the scheduler to make the decision for the slot.
  void on_scheduler_result(sched_slot_result_reader  result,
                           std::chrono::microseconds slot_latency = std::chrono::microseconds{0});

private:
  void log_debug(sched_slot_result_reader result, std::chrono::microseconds slot_latency);

  void log_info(sched_slot_result_reader result, std::chrono::microseconds slot_latency);

  ocudulog::basic_logger& logger;
  bool                    log_broadcast;
  bool                    enabled;
  const pci_t             pci;
  log_event_time          event_time;

  fmt::memory_buffer fmtbuf;
};

} // namespace ocudu
