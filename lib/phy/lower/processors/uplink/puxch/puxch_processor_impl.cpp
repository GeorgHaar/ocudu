// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "puxch_processor_impl.h"
#include "ocudu/gateways/baseband/buffer/baseband_gateway_buffer_reader.h"
#include "ocudu/phy/lower/lower_phy_rx_symbol_context.h"
#include "ocudu/phy/support/resource_grid_context.h"
#include "ocudu/phy/support/resource_grid_writer.h"

using namespace ocudu;

bool puxch_processor_impl::process_symbol(const baseband_gateway_buffer_ptr_view& samples,
                                          const lower_phy_rx_symbol_context&      context)
{
  ocudu_assert(notifier != nullptr, "Notifier has not been connected.");

  // Check if the slot has changed.
  if (context.slot != current_slot) {
    // Update slot.
    current_slot = context.slot;

    // The previous slot does not receive more symbols.
    if (current_demodulator != nullptr) {
      current_demodulator->close();
      current_demodulator = nullptr;
    }

    // Exchange an empty request with the current slot with a stored request.
    auto request = requests.exchange({context.slot, shared_resource_grid()});

    resource_grid_context       rg_context  = {.slot = request.slot, .sector = context.sector};
    puxch_baseband_demodulator& demodulator = get_demodulator(current_slot);

    // Handle the returned request. If the request resource grid is invalid, the request is empty and the slot is not
    // demodulated.
    if (request.resource) {
      if (request.slot != current_slot) {
        // If the slot of the request does not match the current slot, then notify a late event.
        notifier->on_puxch_request_late(rg_context);
      } else if (!demodulator.start(request.resource, rg_context)) {
        // If the demodulator is still busy with a previous slot, then the request cannot be processed.
        logger.error(current_slot.sfn(), current_slot.slot_index(), "The demodulator is busy.");
        notifier->on_puxch_request_late(rg_context);
      } else {
        // Otherwise, the demodulator processes the slot.
        current_demodulator = &demodulator;
      }
    }
  }

  // Skip symbol processing if the current slot is not demodulated.
  if (current_demodulator == nullptr) {
    return false;
  }

  // Demodulate the OFDM symbol asynchronously.
  current_demodulator->demodulate_symbol(samples, context);

  // Close the slot after its last OFDM symbol.
  if ((context.nof_symbols == nof_symbols_per_slot - 1) && (current_demodulator != nullptr)) {
    current_demodulator->close();
    current_demodulator = nullptr;
  }

  return true;
}

void puxch_processor_impl::handle_request(const shared_resource_grid& grid, const resource_grid_context& context)
{
  // Ignore request if the processor has stopped.
  if (stopped.load(std::memory_order_relaxed)) {
    return;
  }

  ocudu_assert(notifier != nullptr, "Notifier has not been connected.");

  // Swap the new request by the current request in the circular array.
  auto request = requests.exchange({context.slot, grid.copy()});

  // If there was a request at the same request index, notify a late event with the context of the discarded request.
  if (request.resource) {
    resource_grid_context late_context = {.slot = request.slot, .sector = context.sector};
    notifier->on_puxch_request_late(late_context);
  }
}

void puxch_processor_impl::on_demodulation_completion(const shared_resource_grid&        grid,
                                                      const lower_phy_rx_symbol_context& context)
{
  notifier->on_rx_symbol(grid, context, context.is_valid);
}

lower_phy_center_freq_controller& puxch_processor_impl::get_center_freq_control()
{
  return *this;
}

bool puxch_processor_impl::set_carrier_center_frequency(double carrier_center_frequency_Hz)
{
  common_ofdm_demodulator->set_center_frequency(carrier_center_frequency_Hz);
  return true;
}
