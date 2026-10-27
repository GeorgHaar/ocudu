// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/f1ap/f1ap_ue_id_types.h"
#include <unordered_set>

namespace ocudu::ocucp {

/// Allocator of gNB-CU UE F1AP IDs that are unique within the gNB-CU logical node (TS 38.401 section 6.2.1).
class gnb_cu_ue_f1ap_id_allocator
{
public:
  /// Allocates a free gNB-CU UE F1AP ID.
  /// \return The allocated ID, or gnb_cu_ue_f1ap_id_t::invalid if all IDs are in use.
  gnb_cu_ue_f1ap_id_t allocate()
  {
    if (allocated_ids.size() == MAX_NOF_CU_F1AP_UES) {
      return gnb_cu_ue_f1ap_id_t::invalid;
    }

    // Start at the next ID and skip the IDs that are in use.
    while (allocated_ids.count(next_id) != 0) {
      increase_next_id();
    }
    gnb_cu_ue_f1ap_id_t id = next_id;
    allocated_ids.insert(id);
    increase_next_id();
    return id;
  }

  /// Releases an allocated gNB-CU UE F1AP ID, so that it can be allocated again.
  void release(gnb_cu_ue_f1ap_id_t id) { allocated_ids.erase(id); }

  /// Returns the number of allocated gNB-CU UE F1AP IDs.
  size_t size() const { return allocated_ids.size(); }

protected:
  gnb_cu_ue_f1ap_id_t next_id = gnb_cu_ue_f1ap_id_t::min;

private:
  void increase_next_id()
  {
    next_id = next_id == gnb_cu_ue_f1ap_id_t::max ? gnb_cu_ue_f1ap_id_t::min
                                                  : int_to_gnb_cu_ue_f1ap_id(static_cast<uint64_t>(next_id) + 1);
  }

  std::unordered_set<gnb_cu_ue_f1ap_id_t> allocated_ids;
};

} // namespace ocudu::ocucp
