// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/ocudulog/log_channel.h"
#include <optional>

namespace ocudu {

/// Time of the event that a log entry reports. Empty when the entry gets the current time.
using log_event_time = std::optional<std::chrono::system_clock::time_point>;

/// Logs an entry in \c channel with the time \c event_time, or with the current time if \c event_time is empty.
template <typename... Args>
void log_with_event_time(ocudulog::log_channel& channel,
                         const log_event_time&  event_time,
                         const char*            fmtstr,
                         Args&&... args)
{
  if (event_time.has_value()) {
    channel.log_at(*event_time, fmtstr, std::forward<Args>(args)...);
  } else {
    channel(fmtstr, std::forward<Args>(args)...);
  }
}

} // namespace ocudu
