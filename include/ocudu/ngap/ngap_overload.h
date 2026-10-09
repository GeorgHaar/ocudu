// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include <cstdint>
#include <optional>

namespace ocudu::ocucp {

/// Signalling traffic that the NG-RAN node rejects while the AMF is overloaded (TS 38.413 section 9.3.1.105).
enum class ngap_overload_action_t {
  reject_non_emergency_mo_data,
  reject_signalling,
  only_emergency_and_mt,
  only_high_priority_and_mt
};

/// Overload state of the AMF (TS 38.413 section 8.7.7.2).
struct ngap_amf_overload {
  /// Signalling traffic that the NG-RAN node rejects. If absent, the NG-RAN node rejects no traffic.
  std::optional<ngap_overload_action_t> action;
  /// Percentage by which the NG-RAN node reduces the signalling traffic of the action (TS 38.413 section 9.3.1.106).
  /// If absent, the NG-RAN node rejects all the signalling traffic of the action.
  std::optional<uint8_t> traffic_load_reduction;
};

} // namespace ocudu::ocucp
