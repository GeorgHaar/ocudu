// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/adt/circular_vector.h"
#include "ocudu/adt/static_vector.h"
#include "ocudu/ran/rnti.h"
#include "ocudu/ran/slot_pdu_capacity_constants.h"
#include "ocudu/ran/slot_point.h"
#include <algorithm>

namespace ocudu {

/// \brief Storage of the periodic CG PUSCH opportunities to be scheduled in the resource grid.
///
/// Each position of the wheel represents a slot in a ring-like structure (i.e. slot % max_cg_slot_periodicity) and
/// holds the list of RNTIs with a CG grant due in that slot. Used by both the CG Type 1 and Type 2 schedulers.
class cg_slot_wheel
{
public:
  /// List of UEs with a CG opportunity in a given slot.
  using rnti_list = static_vector<rnti_t, MAX_CG_PUSCH_PDUS_PER_SLOT>;

  /// Maximum CG periodicity in slots: sym5120x14 = 5120 slots (normal CP, 14 symbols/slot).
  static constexpr unsigned max_cg_slot_periodicity = 5120U;

  cg_slot_wheel()
  {
    // NOF_SLOTS_PER_HYPER_SYSTEM_FRAME % max_cg_slot_periodicity = 0, no need to adjust the wheel size to a value
    // different from max_cg_slot_periodicity.
    wheel.resize(max_cg_slot_periodicity);
  }

  /// \brief Adds a UE's CG grant positions to the wheel, one every \c period_slots slots.
  /// \return Number of offsets at which the UE was already present, and which were therefore left untouched. A
  /// non-zero value denotes a bookkeeping error in the caller.
  unsigned add_ue(rnti_t rnti, unsigned time_domain_offset, unsigned period_slots)
  {
    unsigned nof_dup = 0;
    for (unsigned offset = time_domain_offset; offset < max_cg_slot_periodicity; offset += period_slots) {
      rnti_list& slot_wheel = wheel[offset];
      if (std::find(slot_wheel.begin(), slot_wheel.end(), rnti) != slot_wheel.end()) {
        ++nof_dup;
        continue;
      }
      // The RRM admits a UE only once it has checked that the slot offset has room, so a full list here means the two
      // went out of sync. Pushing regardless would overrun the static_vector, so drop the entry instead: the UE loses
      // this occasion rather than the cell losing its scheduler state.
      ocudu_assert(not slot_wheel.full(),
                   "rnti={}: no room left for a CG grant at slot offset={}, which already holds {} UEs",
                   rnti,
                   offset,
                   slot_wheel.size());
      if (slot_wheel.full()) {
        continue;
      }
      slot_wheel.push_back(rnti);
    }
    return nof_dup;
  }

  /// \brief Removes a UE's CG grant positions from the wheel.
  ///
  /// \c time_domain_offset and \c period_slots must be the same values that were passed to add_ue(), otherwise stale
  /// entries are left behind.
  /// \return Number of offsets at which the UE was not found. This is expected to be non-zero for a UE that was never
  /// added to the wheel, e.g. a CG Type 2 UE still pending activation.
  unsigned rem_ue(rnti_t rnti, unsigned time_domain_offset, unsigned period_slots)
  {
    unsigned nof_missing = 0;
    for (unsigned offset = time_domain_offset; offset < max_cg_slot_periodicity; offset += period_slots) {
      rnti_list& slot_wheel = wheel[offset];
      auto*      it         = std::find(slot_wheel.begin(), slot_wheel.end(), rnti);
      if (it == slot_wheel.end()) {
        ++nof_missing;
        continue;
      }
      // Swap with last element and pop for O(1) removal.
      std::swap(*it, slot_wheel.back());
      slot_wheel.pop_back();
    }
    return nof_missing;
  }

  /// \brief Removes a UE from every position of the wheel.
  ///
  /// Scans the whole wheel, so it is meant as a recovery path for when the offset and period the UE was added with can
  /// no longer be recovered; prefer rem_ue() otherwise.
  /// \return Number of offsets from which the UE was removed.
  unsigned rem_ue_everywhere(rnti_t rnti)
  {
    unsigned nof_removed = 0;
    for (rnti_list& slot_wheel : wheel) {
      auto* it = std::find(slot_wheel.begin(), slot_wheel.end(), rnti);
      if (it == slot_wheel.end()) {
        continue;
      }
      std::swap(*it, slot_wheel.back());
      slot_wheel.pop_back();
      ++nof_removed;
    }
    return nof_removed;
  }

  /// Returns the list of UEs with a CG opportunity in the given slot.
  const rnti_list& operator[](slot_point slot) const { return wheel[slot.count()]; }

  /// Returns whether the given UE has a CG opportunity in the given slot.
  bool contains(slot_point slot, rnti_t rnti) const
  {
    const rnti_list& slot_wheel = wheel[slot.count()];
    return std::find(slot_wheel.begin(), slot_wheel.end(), rnti) != slot_wheel.end();
  }

  /// Removes all the CG opportunities from the wheel.
  void clear()
  {
    for (rnti_list& slot_wheel : wheel) {
      slot_wheel.clear();
    }
  }

private:
  circular_vector<rnti_list> wheel;
};

} // namespace ocudu
