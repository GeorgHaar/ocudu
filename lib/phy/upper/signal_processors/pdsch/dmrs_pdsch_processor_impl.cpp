// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "dmrs_pdsch_processor_impl.h"
#include "../dmrs_helper.h"
#include "ocudu/adt/format.h"
#include "ocudu/adt/static_vector.h"
#include "ocudu/ocuduvec/copy.h"
#include "ocudu/ocuduvec/sc_prod.h"
#include "ocudu/phy/support/re_pattern.h"
#include "ocudu/phy/support/resource_grid_mapper.h"

using namespace ocudu;

/// Resource element allocation patterns within a resource block for PDSCH DM-RS type 1.
static const re_prb_mask& get_re_mask(dmrs_config_type type, unsigned cdm_group_id)
{
  ocudu_assert(type != dmrs_config_type::not_set, "Invalid DM-RS type.");

  static constexpr unsigned MAX_CDM_GROUPS_TYPE1 = 2;
  static constexpr unsigned MAX_CDM_GROUPS_TYPE2 = 3;

  static constexpr std::array<re_prb_mask, MAX_CDM_GROUPS_TYPE1> re_mask_type1 = {
      {{true, false, true, false, true, false, true, false, true, false, true, false},
       {false, true, false, true, false, true, false, true, false, true, false, true}}};

  static constexpr std::array<const re_prb_mask, MAX_CDM_GROUPS_TYPE2> re_mask_type2 = {
      {{true, true, false, false, false, false, true, true, false, false, false, false},
       {false, false, true, true, false, false, false, false, true, true, false, false},
       {false, false, false, false, true, true, false, false, false, false, true, true}}};

  return (type == dmrs_config_type::type1) ? re_mask_type1[cdm_group_id] : re_mask_type2[cdm_group_id];
}

void ocudu::dmrs_pdsch_processor_impl::sequence_generation(span<cf_t>      sequence,
                                                           unsigned        symbol,
                                                           const config_t& config) const
{
  // Get signal amplitude.
  float amplitude = M_SQRT1_2 * config.amplitude;

  // Extract parameters to calculate the PRG initial state.
  unsigned nslot    = config.slot.slot_index();
  unsigned nidnscid = config.scrambling_id;
  unsigned nscid    = config.n_scid ? 1 : 0;
  unsigned nsymb    = get_nsymb_per_slot(cyclic_prefix::NORMAL);

  // Calculate initial sequence state.
  unsigned c_init = ((nsymb * nslot + symbol + 1) * (2 * nidnscid + 1) * pow2(17) + (2 * nidnscid + nscid)) % pow2(31);

  // Initialize sequence.
  prg->init(c_init);

  // Generate sequence.
  dmrs_sequence_generate(
      sequence, *prg, amplitude, config.reference_point_k_rb, get_nof_re_per_prb(config.type), config.rb_mask);
}

void dmrs_pdsch_processor_impl::apply_cdm(span<cf_t>       sequence,
                                          span<const cf_t> base_sequence,
                                          unsigned         dmrs_port,
                                          unsigned         symbol,
                                          const config_t&  config)
{
  // Set l_prime to 1 if the symbol follows another one.
  unsigned l_prime = 0;
  if (symbol != 0) {
    l_prime = config.symbols_mask.test(symbol - 1) ? 1 : 0;
  }

  // Apply CDM to DM-RS ports other than port zero.
  dmrs_pxsch_parameters params = get_pxsch_dmrs_params(config.type, dmrs_port);

  // If no weights are applied, copy the original sequence.
  if ((params.w_t[l_prime] == +1.0F) && (params.w_f[0] == params.w_f[1])) {
    ocuduvec::copy(sequence, base_sequence);
    return;
  }

  // Apply w_t weight can be +1 or -1 depending on l_prime and port.
  if (params.w_t[l_prime] != +1.0F) {
    ocuduvec::sc_prod(sequence, base_sequence, -1);
  } else {
    ocuduvec::copy(sequence, base_sequence);
  }

  // Apply w_f weight. It can be {+1, +1} or {+1, -1} depending on l_prime and port. On the first case, no operation
  // is required. On the second case, RE with odd sequence indices are multiplied by -1.
  if (params.w_f[0] != params.w_f[1]) {
    for (unsigned i_re = 1, nof_re_symbol = sequence.size(); i_re < nof_re_symbol; i_re += 2) {
      sequence[i_re] *= -1;
    }
  }
}

void dmrs_pdsch_processor_impl::map(resource_grid_writer& grid, const config_t& config)
{
  // Number of DM-RS RE in an OFDM symbol.
  unsigned nof_dmrs_re_symbol = get_nof_re_per_prb(config.type) * config.rb_mask.count();

  // Number of DM-RS RE in the entire slot.
  unsigned nof_dmrs_re_slot = nof_dmrs_re_symbol * config.symbols_mask.count();

  // Precoding and beamforming of the transmission.
  const precoding_beamforming_configuration& precoding = config.precoding_and_beamforming;

  // Number of transmit layers. Each layer is carried by a different DM-RS port.
  unsigned nof_layers = precoding.get_nof_layers();
  ocudu_assert(config.dmrs_ports.count() == nof_layers,
               "The number of DM-RS ports (i.e., {}) does not match the number of layers (i.e., {}).",
               config.dmrs_ports.count(),
               nof_layers);
  ocudu_assert(static_cast<unsigned>(config.dmrs_ports.find_highest()) < dmrs_max_ports_type(config.type),
               "The DM-RS ports (i.e., {}) exceed the maximum for DM-RS {} (i.e., {}).",
               config.dmrs_ports,
               config.type,
               dmrs_max_ports_type(config.type));

  // Number of beams that carry the transmission.
  unsigned nof_beams = precoding.get_nof_beams();

  temp_re.resize(config.dmrs_ports.size(), nof_dmrs_re_slot);

  // Generate the DM-RS sequence for each DM-RS symbol. The sequence is common to all DM-RS ports.
  config.symbols_mask.for_each(
      0,
      config.symbols_mask.size(),
      [this, &config, nof_dmrs_re_symbol, base_sequence = temp_re.get_slice(0)](unsigned i_symbol) mutable {
        // Generate sequence for the given symbol.
        sequence_generation(base_sequence.first(nof_dmrs_re_symbol), i_symbol, config);

        // Advance view.
        base_sequence = base_sequence.last(base_sequence.size() - nof_dmrs_re_symbol);
      });

  // Iterate each CDM group.
  for (unsigned i_cdm_group = 0, nof_cdm_groups = get_max_nof_cdm_groups_without_data(config.type);
       i_cdm_group != nof_cdm_groups;
       ++i_cdm_group) {
    // Select the layers whose DM-RS port belongs to the current CDM group.
    static_vector<uint8_t, DMRS_MAX_PORTS_PER_CDM_GROUP> cdm_layers;
    static_vector<uint8_t, DMRS_MAX_PORTS_PER_CDM_GROUP> cdm_ports;
    config.dmrs_ports.for_each(
        0,
        config.dmrs_ports.size(),
        [&config, &cdm_layers, &cdm_ports, i_cdm_group, i_layer = 0](unsigned i_dmrs_port) mutable {
          if (get_dmrs_cdm_group_index(config.type, i_dmrs_port) == i_cdm_group) {
            cdm_layers.emplace_back(i_layer);
            cdm_ports.emplace_back(i_dmrs_port);
          }
          ++i_layer;
        });

    // Skip CDM groups without DM-RS ports.
    if (cdm_ports.empty()) {
      continue;
    }

    // Number of DM-RS ports belonging to the current CDM group.
    unsigned nof_dmrs_ports_cdm = cdm_ports.size();

    // Apply the CDM codes of each port within the CDM group.
    modular_re_buffer<cf_t, DMRS_MAX_PORTS_PER_CDM_GROUP> dmrs_re_buffer(nof_dmrs_ports_cdm, nof_dmrs_re_slot);
    for (unsigned i_cdm_port = 0; i_cdm_port != nof_dmrs_ports_cdm; ++i_cdm_port) {
      // Select absolute DM-RS port.
      unsigned i_dmrs_port = cdm_ports[i_cdm_port];

      // Select base sequence on the first port.
      span<const cf_t> base_sequence = temp_re.get_slice(0);

      // Select the view to the DMRS for this CDM group.
      span<cf_t> cdm_dmrs_sequence = temp_re.get_slice(i_dmrs_port);

      // Set view of the slice.
      dmrs_re_buffer.set_slice(i_cdm_port, cdm_dmrs_sequence);

      // Apply CDM cover code for all ports except the first one.
      if (i_dmrs_port != 0) {
        config.symbols_mask.for_each(
            0,
            config.symbols_mask.size(),
            [&config, &base_sequence, &cdm_dmrs_sequence, nof_dmrs_re_symbol, i_dmrs_port](unsigned i_symbol) {
              // Apply CDM cover code for this OFDM symbol.
              apply_cdm(cdm_dmrs_sequence.first(nof_dmrs_re_symbol),
                        base_sequence.first(nof_dmrs_re_symbol),
                        i_dmrs_port,
                        i_symbol,
                        config);

              // Advance sequence views.
              base_sequence     = base_sequence.last(base_sequence.size() - nof_dmrs_re_symbol);
              cdm_dmrs_sequence = cdm_dmrs_sequence.last(cdm_dmrs_sequence.size() - nof_dmrs_re_symbol);
            });
      }
    }

    // Set DM-RS allocation pattern of the CDM group.
    re_pattern dmrs_pattern_cdm;
    dmrs_pattern_cdm.crb_mask = config.rb_mask;
    dmrs_pattern_cdm.symbols  = config.symbols_mask;
    dmrs_pattern_cdm.re_mask  = get_re_mask(config.type, i_cdm_group);

    // Prepare the precoding and beamforming configuration of the CDM group.
    precoding_beamforming_configuration cdm_group_precoding(
        nof_dmrs_ports_cdm, nof_beams, precoding.get_nof_prg(), precoding.get_prg_size());

    // Extract the precoding and beamforming of the CDM group layers for each PRG.
    for (unsigned i_prg = 0, i_prg_end = precoding.get_nof_prg(); i_prg != i_prg_end; ++i_prg) {
      const precoding_beamforming_composite& prg_composite = precoding.get_prg(i_prg);

      // The MIMO precoding matrix of the CDM group contains the rows of the layers mapped onto the group ports.
      precoding_weight_matrix cdm_precoding(nof_dmrs_ports_cdm, nof_beams);
      for (unsigned i_cdm_port = 0; i_cdm_port != nof_dmrs_ports_cdm; ++i_cdm_port) {
        for (unsigned i_beam = 0; i_beam != nof_beams; ++i_beam) {
          // Extract the coefficient for mapping the layer to the beam.
          cf_t coefficient = prg_composite.mimo.get_coefficient(cdm_layers[i_cdm_port], i_beam);

          // Set coefficient in the CDM group precoding.
          cdm_precoding.set_coefficient(coefficient, i_cdm_port, i_beam);
        }
      }

      // The CDM group ports are carried by the beams of the transmission.
      cdm_group_precoding.set_prg({cdm_precoding, prg_composite.beams}, i_prg);
    }

    // Map the CDM group symbols into the resource grid.
    mapper->map(grid, dmrs_re_buffer, dmrs_pattern_cdm, cdm_group_precoding);
  }
}
