// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/ran/five_g_s_tmsi.h"
#include "ocudu/ran/guami.h"
#include "ocudu/ran/plmn_identity.h"
#include <optional>

namespace ocudu::ocucp {

/// Information about a UE that the AMF selection uses.
struct amf_selection_request {
  /// PLMN that the UE selected.
  plmn_identity plmn = plmn_identity::test_value();
  /// GUAMI of the AMF where the UE is registered (TS 38.331 section 6.2.2, registeredAMF).
  std::optional<guami_t> registered_guami;
  /// Origin of the registered GUAMI (TS 38.331 section 6.2.2, guami-Type).
  guami_type_t registered_guami_type = guami_type_t::native;
  /// 5G-S-TMSI of the UE.
  std::optional<five_g_s_tmsi_t> five_g_s_tmsi;
};

} // namespace ocudu::ocucp
