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
#include "ocudu/support/units.h"
#include <cstdint>
#include <optional>

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
  units::bits  sr_bits;
  units::bits  harq_ack_bits;
  units::bits  csi_part1_bits;
};

/// Uplink PDU information needed to generate the indication of an SRS.
struct srs_ind_context {
  rnti_t  rnti;
  uint8_t nof_tx_ports;
  bool    iq_matrix_report;
  bool    positioning_report;
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

} // namespace fapi_adaptor
} // namespace ocudu
