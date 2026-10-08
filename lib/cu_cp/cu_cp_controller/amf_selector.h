// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/adt/span.h"
#include "ocudu/ngap/ngap_setup.h"
#include "ocudu/ran/amf_selection_request.h"
#include "ocudu/ran/cu_cp_types.h"
#include "ocudu/ran/guami.h"
#include "ocudu/ran/plmn_identity.h"
#include <optional>
#include <random>
#include <vector>

namespace ocudu::ocucp {

/// AMF that the CU-CP can select for a UE.
struct amf_candidate {
  cu_cp_amf_index_t amf_index = cu_cp_amf_index_t::invalid;
  /// True if the NG-C interface instance to the AMF is set up.
  bool connected = false;
  /// PLMNs that the AMF supports and that the CU-CP serves through it.
  std::vector<plmn_identity> plmns;
  /// GUAMIs that the AMF serves.
  std::vector<ngap_served_guami_item> served_guamis;
  /// Capacity of the AMF relative to the other AMFs of its AMF Set (TS 38.413 section 9.3.1.32).
  uint16_t relative_amf_capacity = 0;
};

/// \brief Selects the AMF for a UE that accesses the CU-CP (NAS Node Selection Function, TS 38.410 section 5.7 and
/// TS 38.300 section 16.3.4.2).
///
/// The selection applies these rules in order. Each rule takes only the connected AMFs that support the selected PLMN.
/// 1. An AMF that serves the registered GUAMI of the UE with the same GUAMI Type.
/// 2. An AMF that serves the AMF Set ID and AMF Pointer of the 5G-S-TMSI in the selected PLMN.
/// 3. If the UE identifies an AMF Set, an AMF of this AMF Set.
/// 4. Any AMF.
///
/// If a rule finds several AMFs, the selection picks one at random, weighted by its relative AMF capacity. An AMF with
/// capacity 0 is picked only if all the AMFs of the rule have capacity 0. In rule 4, the AMFs can belong to different
/// AMF Sets. The selection then picks an AMF Set with a probability that is proportional to the total capacity of its
/// AMFs. TS 23.501 section 6.3.5 leaves the selection of the AMF Set to local policy.
/// \return The index of the AMF, or std::nullopt if no connected AMF supports the selected PLMN.
std::optional<cu_cp_amf_index_t>
select_amf(span<const amf_candidate> candidates, const amf_selection_request& request, std::mt19937& rng);

/// \brief Finds the connected AMF that serves a GUAMI.
///
/// The AMF that serves the GUAMI comes first. Otherwise, a connected AMF of the same AMF Set is returned.
/// \return The index of the AMF, or std::nullopt if no AMF of the AMF Set is connected.
std::optional<cu_cp_amf_index_t> select_amf_for_guami(span<const amf_candidate> candidates, const guami_t& guami);

} // namespace ocudu::ocucp
