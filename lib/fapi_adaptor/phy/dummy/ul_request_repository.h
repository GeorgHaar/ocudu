// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/adt/static_vector.h"
#include "ocudu/ran/harq_id.h"
#include "ocudu/ran/pucch/pucch_mapping.h"
#include "ocudu/ran/rnti.h"
#include "ocudu/ran/slot_pdu_capacity_constants.h"
#include "ocudu/ran/slot_point.h"
#include "ocudu/support/units.h"
#include <atomic>
#include <optional>
#include <utility>
#include <vector>

namespace ocudu {
namespace fapi_adaptor {

/// Uplink PDU information needed to generate the indications of a PUSCH.
struct pusch_ind_context {
  rnti_t                 rnti;
  harq_id_t              harq_id;
  std::optional<uint8_t> rapid;
  /// Transport block size, if the PUSCH carries data.
  std::optional<units::bytes> tb_size;
  /// Whether the PUSCH carries UCI.
  bool        has_uci;
  units::bits harq_ack_bits;
  units::bits csi_part1_bits;
};

/// Uplink PDU information needed to generate the indication of a PUCCH.
struct pucch_ind_context {
  rnti_t       rnti;
  pucch_format format;
  /// SR opportunity flag for PUCCH Formats 0 and 1.
  bool        sr_present;
  units::bits sr_bits;
  units::bits harq_ack_bits;
  units::bits csi_part1_bits;
};

/// Uplink PDU information needed to generate the indication of an SRS.
struct srs_ind_context {
  rnti_t   rnti;
  unsigned nof_tx_ports;
  bool     iq_matrix_report;
  bool     positioning_report;
};

/// Uplink indication contexts of a slot.
struct ul_slot_ind_context {
  static_vector<pusch_ind_context, MAX_PUSCH_PDUS_PER_SLOT> puschs;
  static_vector<pucch_ind_context, MAX_PUCCH_PDUS_PER_SLOT> pucchs;
  static_vector<srs_ind_context, MAX_SRS_PDUS_PER_SLOT>     srss;

  void clear()
  {
    puschs.clear();
    pucchs.clear();
    srss.clear();
  }
};

/// \brief Lock-free ring of uplink indication contexts indexed by slot.
///
/// Supports one writer and one reader per slot running concurrently.
class ul_request_repository
{
  enum class entry_state : uint8_t { empty, writing, ready, reading };

  struct entry {
    std::atomic<entry_state> state{entry_state::empty};
    slot_point               slot;
    ul_slot_ind_context      context;
  };

public:
  /// Creates a repository able to hold the given number of consecutive slots.
  explicit ul_request_repository(unsigned nof_slots) : entries(nof_slots) {}

  /// \brief Stores the contexts of a slot, filled by the given function.
  ///
  /// Returns false if the entry is being accessed by another operation.
  template <typename Func>
  bool write(slot_point slot, const Func& fill)
  {
    entry& e = get_entry(slot);

    entry_state expected = e.state.load(std::memory_order_relaxed);
    if (expected != entry_state::empty && expected != entry_state::ready) {
      return false;
    }
    if (!e.state.compare_exchange_strong(expected, entry_state::writing, std::memory_order_acquire)) {
      return false;
    }

    e.slot = slot;
    e.context.clear();
    fill(e.context);
    e.state.store(entry_state::ready, std::memory_order_release);
    return true;
  }

  /// \brief Passes the contexts of a slot to the given function and releases them.
  ///
  /// The function is not called if the slot has no stored contexts.
  template <typename Func>
  void read(slot_point slot, const Func& consume)
  {
    entry& e = get_entry(slot);

    entry_state expected = entry_state::ready;
    if (!e.state.compare_exchange_strong(expected, entry_state::reading, std::memory_order_acquire)) {
      return;
    }

    // An entry of an older slot was never read, for example, because the sector stopped.
    if (e.slot == slot) {
      consume(std::as_const(e.context));
    }
    e.state.store(entry_state::empty, std::memory_order_release);
  }

  /// \brief Releases the contexts of a slot without consuming them.
  ///
  /// Returns true if the slot had stored contexts.
  bool discard(slot_point slot)
  {
    entry& e = get_entry(slot);

    entry_state expected = entry_state::ready;
    if (!e.state.compare_exchange_strong(expected, entry_state::reading, std::memory_order_acquire)) {
      return false;
    }

    bool found = (e.slot == slot);
    e.state.store(entry_state::empty, std::memory_order_release);
    return found;
  }

private:
  entry& get_entry(slot_point slot) { return entries[slot.count() % entries.size()]; }

  /// Ring of slot entries.
  std::vector<entry> entries;
};

} // namespace fapi_adaptor
} // namespace ocudu
