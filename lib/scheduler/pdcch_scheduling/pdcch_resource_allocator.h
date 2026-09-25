// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "../cell/resource_grid.h"
#include "../config/ue_configuration.h"

namespace ocudu {

/// PDCCH resource allocation algorithm for a single cell and for both common and UE-dedicated CORESETs.
class pdcch_resource_allocator
{
public:
  virtual ~pdcch_resource_allocator() = default;

  /// Allocates RE space for common PDCCH, avoiding in the process collisions with other PDCCH allocations.
  /// \param rnti RNTI of allocation. Values: SI-RNTI, P-RNTI, RA-RNTIs.
  /// \param ss_id Search Space Id to use.
  /// \param aggr_lvl Aggregation Level of PDCCH allocation.
  /// \param beam Beam that carries the PDCCH. Nullopt selects the default beams.
  /// \return Allocated PDCCH if successful.
  virtual pdcch_dl_information* alloc_dl_pdcch_common(cell_slot_resource_allocator&  slot_alloc,
                                                      rnti_t                         rnti,
                                                      search_space_id                ss_id,
                                                      aggregation_level              aggr_lvl,
                                                      std::optional<beam_identifier> beam) = 0;

  /// Allocates RE space for common UL PDCCH, avoiding in the process collisions with other PDCCH allocations.
  /// \param slot_alloc Grid Resources for the slot where PDCCH is going to be allocated.
  /// \param rnti RNTI of allocation. Potential types: TC-RNTI.
  /// \param ss_id Search Space Id to use.
  /// \param aggr_lvl Aggregation Level of PDCCH allocation.
  /// \param beam Beam that carries the PDCCH. Nullopt selects the default beams.
  /// \return Allocated PDCCH if successful.
  virtual pdcch_ul_information* alloc_ul_pdcch_common(cell_slot_resource_allocator&  slot_alloc,
                                                      rnti_t                         rnti,
                                                      search_space_id                ss_id,
                                                      aggregation_level              aggr_lvl,
                                                      std::optional<beam_identifier> beam) = 0;

  /// Allocates RE space for UE-dedicated DL PDCCH, avoiding in the process collisions with other PDCCH allocations.
  /// \param rnti RNTI of UE being allocated.
  /// \param user UE configuration for the provided cell.
  /// \param bwp_id BWP Id to use.
  /// \param ss_id Search Space Id to use.
  /// \param aggr_lvl Aggregation Level of PDCCH allocation.
  /// \return Allocated PDCCH if successful.
  virtual pdcch_dl_information* alloc_dl_pdcch_ue(cell_slot_resource_allocator& slot_alloc,
                                                  rnti_t                        rnti,
                                                  const ue_cell_configuration&  user,
                                                  search_space_id               ss_id,
                                                  aggregation_level             aggr_lvl) = 0;

  /// Allocates RE space for UE-dedicated DL PDCCH, avoiding in the process collisions with other PDCCH allocations.
  /// \param rnti RNTI of UE being allocated.
  /// \param user UE configuration for the provided cell.
  /// \param bwp_id BWP Id to use.
  /// \param ss_id Search Space Id to use.
  /// \param aggr_lvl Aggregation Level of PDCCH allocation.
  /// \return Allocated PDCCH if successful.
  virtual pdcch_ul_information* alloc_ul_pdcch_ue(cell_slot_resource_allocator& slot_alloc,
                                                  rnti_t                        rnti,
                                                  const ue_cell_configuration&  user,
                                                  search_space_id               ss_id,
                                                  aggregation_level             aggr_lvl) = 0;

  /// \brief Allocates RE space for a UE-dedicated UL PDCCH whose CRC is scrambled with the UE's CS-RNTI, as used by a
  /// Configured Grant Type 2 activation/deactivation.
  ///
  /// Unlike \ref alloc_ul_pdcch_ue, the PDCCH is placed on the candidates the UE monitors with its CS-RNTI. In a
  /// UE-specific SearchSpace those differ from the C-RNTI ones, since the candidate hashing is seeded with the RNTI
  /// (TS 38.213, Section 10.1).
  ///
  /// \param cs_rnti CS-RNTI of the UE being allocated.
  /// \param user UE configuration for the provided cell. Must have a CS-RNTI configured.
  /// \param ss_id Search Space Id to use.
  /// \param aggr_lvl Aggregation Level of PDCCH allocation.
  /// \return Allocated PDCCH if successful, nullptr otherwise.
  virtual pdcch_ul_information* alloc_ul_pdcch_ue_cs_rnti(cell_slot_resource_allocator& slot_alloc,
                                                          rnti_t                        cs_rnti,
                                                          const ue_cell_configuration&  user,
                                                          search_space_id               ss_id,
                                                          aggregation_level             aggr_lvl) = 0;

  /// Cancels the last PDCCH allocation.
  virtual bool cancel_last_pdcch(cell_slot_resource_allocator& slot_alloc) = 0;
};

} // namespace ocudu
