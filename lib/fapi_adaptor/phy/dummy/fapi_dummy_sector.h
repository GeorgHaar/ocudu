// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ul_request_repository.h"
#include "ocudu/fapi/common/error_indication.h"
#include "ocudu/fapi/p5/p5_requests_gateway.h"
#include "ocudu/fapi/p7/p7_last_request_notifier.h"
#include "ocudu/fapi/p7/p7_requests_gateway.h"
#include "ocudu/fapi_adaptor/phy/p5/phy_fapi_p5_sector_adaptor.h"
#include "ocudu/fapi_adaptor/phy/p7/phy_fapi_p7_sector_adaptor.h"
#include "ocudu/fapi_adaptor/phy/phy_fapi_adaptor.h"
#include "ocudu/ocudulog/logger.h"
#include "ocudu/ran/slot_point_extended.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

namespace ocudu {

namespace fapi_adaptor {

/// FAPI dummy sector configuration.
struct fapi_dummy_sector_config {
  unsigned           sector_id;
  subcarrier_spacing scs;
  /// Number of slots between an uplink slot and the notification of its indications.
  unsigned ul_indication_delay_slots;
  /// Number of slots a SLOT.indication is notified in advance of the transmission time.
  unsigned max_processing_delay_slots;
  /// Whether requests arriving after their transmission time are reported as errors.
  bool late_request_detection;
};

/// \brief FAPI dummy sector.
///
/// Emulates the PHY of one cell at the FAPI interface. Slot boundaries are provided by an external clock.
class fapi_dummy_sector : public phy_fapi_sector_adaptor,
                          private phy_fapi_p5_sector_adaptor,
                          private phy_fapi_p7_sector_adaptor,
                          private fapi::p5_requests_gateway,
                          private fapi::p7_requests_gateway,
                          private fapi::p7_last_request_notifier
{
public:
  fapi_dummy_sector(const fapi_dummy_sector_config& config, ocudulog::basic_logger& logger_);

  // See interface for documentation.
  phy_fapi_p5_sector_adaptor& get_p5_sector_adaptor() override { return *this; }

  // See interface for documentation.
  phy_fapi_p7_sector_adaptor& get_p7_sector_adaptor() override { return *this; }

  // See interface for documentation.
  void start() override {}

  // See interface for documentation.
  void stop() override {}

  /// \brief Handles a new slot boundary.
  ///
  /// \param[in] slot       Slot notified in the SLOT.indication, in advance of the current time.
  /// \param[in] time_point Transmission time of the notified slot.
  void handle_slot_boundary(slot_point_extended slot, std::chrono::system_clock::time_point time_point);

  /// Returns true if the sector notifies slot indications.
  bool is_running() const { return state.load(std::memory_order_acquire) == sector_state::running; }

  /// Returns true if the last request of the given slot has been received.
  bool is_slot_completed(slot_point slot) const
  {
    return last_completed_slot.load(std::memory_order_acquire) == slot.count();
  }

private:
  enum class sector_state : uint8_t { idle, start_requested, running, stop_requested };

  /// Value of the slot counters when no slot is set.
  static constexpr uint32_t invalid_slot_count = UINT32_MAX;

  // See interface for documentation.
  fapi::p5_requests_gateway& get_p5_requests_gateway() override { return *this; }

  // See interface for documentation.
  void set_p5_responses_notifier(fapi::p5_responses_notifier& notifier) override { p5_notifier = &notifier; }

  // See interface for documentation.
  void set_error_indication_notifier(fapi::error_indication_notifier& notifier) override { error_notifier = &notifier; }

  // See interface for documentation.
  fapi::p7_requests_gateway& get_p7_requests_gateway() override { return *this; }

  // See interface for documentation.
  fapi::p7_last_request_notifier& get_p7_last_request_notifier() override { return *this; }

  // See interface for documentation.
  void set_p7_slot_indication_notifier(fapi::p7_slot_indication_notifier& notifier) override
  {
    slot_notifier = &notifier;
  }

  // See interface for documentation.
  void set_p7_indications_notifier(fapi::p7_indications_notifier& notifier) override { p7_notifier = &notifier; }

  // See interface for documentation.
  void send_param_request(const fapi::param_request& msg) override;

  // See interface for documentation.
  void send_config_request(const fapi::config_request& msg) override;

  // See interface for documentation.
  void send_start_request(const fapi::start_request& msg) override;

  // See interface for documentation.
  void send_stop_request(const fapi::stop_request& msg) override;

  // See interface for documentation.
  void send_dl_tti_request(const fapi::dl_tti_request& msg) override;

  // See interface for documentation.
  void send_ul_tti_request(const fapi::ul_tti_request& msg) override;

  // See interface for documentation.
  void send_ul_dci_request(const fapi::ul_dci_request& msg) override;

  // See interface for documentation.
  void send_tx_data_request(const fapi::tx_data_request& msg) override;

  // See interface for documentation.
  void on_last_message(slot_point slot) override;

  /// Returns true if a request for the given slot can be accepted, and notifies an error otherwise.
  bool check_request(slot_point            slot,
                     fapi::message_type_id msg_id,
                     fapi::error_indication (*build_error)(slot_point, fapi::message_type_id, slot_point));

  /// Notifies the uplink indications of the given slot.
  void notify_ul_indications(slot_point slot, const ul_slot_ind_context& context);

  /// Notifies the indications of a PUSCH.
  void notify_pusch_indications(slot_point slot, const pusch_ind_context& pusch);

  /// Notifies the indication of a PUCCH.
  void notify_pucch_indication(slot_point slot, const pucch_ind_context& pucch);

  /// Notifies the indication of an SRS.
  void notify_srs_indication(slot_point slot, const srs_ind_context& srs);

  const unsigned           sector_id;
  const subcarrier_spacing scs;
  const unsigned           ul_indication_delay_slots;
  const unsigned           max_processing_delay_slots;
  const bool               late_request_detection;
  ocudulog::basic_logger&  logger;
  /// Number of receive antennas reported in the CONFIG.request.
  unsigned nof_rx_antennas = 1;
  /// Operational state.
  std::atomic<sector_state> state{sector_state::idle};
  /// Count of the last slot notified in a SLOT.indication.
  std::atomic<uint32_t> current_slot{invalid_slot_count};
  /// Count of the last slot whose requests are complete.
  std::atomic<uint32_t> last_completed_slot{invalid_slot_count};
  /// Uplink indication contexts pending notification.
  ul_request_repository ul_repository;
  /// Padding-only MAC PDU used as the content of all received transport blocks.
  std::vector<uint8_t>               padding_pdu;
  fapi::p5_responses_notifier*       p5_notifier;
  fapi::error_indication_notifier*   error_notifier;
  fapi::p7_slot_indication_notifier* slot_notifier;
  fapi::p7_indications_notifier*     p7_notifier;
};

} // namespace fapi_adaptor
} // namespace ocudu
