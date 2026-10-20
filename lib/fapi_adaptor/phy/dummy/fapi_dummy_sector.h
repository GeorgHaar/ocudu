// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ul_slot_ind_context.h"
#include "ocudu/adt/spmc_slot_ring.h"
#include "ocudu/fapi/common/error_indication.h"
#include "ocudu/fapi/p5/p5_requests_gateway.h"
#include "ocudu/fapi/p7/p7_last_request_notifier.h"
#include "ocudu/fapi/p7/p7_requests_gateway.h"
#include "ocudu/fapi_adaptor/phy/p5/phy_fapi_p5_sector_adaptor.h"
#include "ocudu/fapi_adaptor/phy/p7/phy_fapi_p7_sector_adaptor.h"
#include "ocudu/fapi_adaptor/phy/phy_fapi_adaptor.h"
#include "ocudu/ocudulog/logger.h"
#include "ocudu/ran/slot_point_extended.h"
#include "ocudu/support/synchronization/stop_event.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace ocudu {

class task_executor;

namespace fapi_adaptor {

/// FAPI dummy sector configuration.
struct fapi_dummy_sector_config {
  unsigned           sector_id;
  subcarrier_spacing scs;
  /// Number of slots between an uplink slot and the notification of its indications. Must be at least one.
  unsigned ul_indication_delay_slots;
  /// Number of slots a SLOT.indication is notified in advance of the transmission time.
  unsigned max_processing_delay_slots;
  /// Whether requests arriving after their transmission time are reported as errors.
  bool late_request_detection;
};

/// \brief FAPI dummy sector.
///
/// Emulates the PHY of one cell at the FAPI interface. Slot boundaries are provided by an external clock. The sector
/// logic runs in the given sequential executor.
class fapi_dummy_sector : public phy_fapi_sector_adaptor,
                          private phy_fapi_p5_sector_adaptor,
                          private phy_fapi_p7_sector_adaptor,
                          private fapi::p5_requests_gateway,
                          private fapi::p7_requests_gateway,
                          private fapi::p7_last_request_notifier
{
public:
  /// Last slot boundary a sector has finished processing.
  struct slot_boundary_progress {
    /// Slot of the boundary, or an invalid slot if no boundary was processed.
    slot_point slot;
    /// Whether the sector was running at the boundary and waits for the last request of each slot.
    bool running = false;
  };

  fapi_dummy_sector(const fapi_dummy_sector_config& config, ocudulog::basic_logger& logger_, task_executor& executor_);

  ~fapi_dummy_sector() override;

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

  /// \brief Returns the last slot boundary the sector has finished processing.
  ///
  /// A running sector finishes a slot boundary when it receives the last request of the slot.
  slot_boundary_progress get_progress() const;

private:
  /// Progress value when no slot boundary was processed.
  static constexpr uint32_t invalid_progress = std::numeric_limits<uint32_t>::max();

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

  /// Defers the given task to the sector executor.
  template <typename Func>
  void defer_task(const char* task_name, Func&& task);

  /// Processes a slot boundary in the sector executor.
  void process_slot_boundary(slot_point_extended slot, std::chrono::system_clock::time_point time_point);

  /// Processes the last request of a slot in the sector executor.
  void process_last_message(slot_point slot);

  /// Defers the late check of a request to the sector executor.
  void check_request(slot_point slot, fapi::message_type_id msg_id);

  /// \brief Checks in the sector executor whether a request is late.
  ///
  /// Every late request is logged, and the first late request of a slot is notified as an error.
  void process_request(slot_point slot, fapi::message_type_id msg_id);

  /// Returns true if the current time has passed the given slot.
  bool is_late(slot_point slot) const;

  /// Publishes the given slot boundary as processed.
  void publish_progress(slot_point slot);

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
  /// Sequential executor of the sector logic.
  task_executor& executor;
  /// Number of receive antennas reported in the CONFIG.request.
  unsigned nof_rx_antennas = 1;
  /// Whether the sector notifies slot indications.
  bool running = false;
  /// Whether a STOP.request takes effect at the next slot boundary.
  bool stop_requested = false;
  /// Whether the MAC completed a slot since the sector started.
  bool mac_completed_slots = false;
  /// Last slot notified in a SLOT.indication.
  std::optional<slot_point> current_slot;
  /// Slots whose requests completed in time while running, indexed like the uplink contexts.
  std::vector<std::optional<slot_point>> completed_slots;
  /// Slots with late requests while running, indexed like the uplink contexts.
  std::vector<std::optional<slot_point>> late_slots;
  /// Last processed slot boundary and running flag, packed for the clock.
  std::atomic<uint32_t> progress{invalid_progress};
  /// Uplink indication contexts pending notification, indexed by slot count.
  spmc_slot_ring<ul_slot_ind_context> ul_repository;
  /// Padding-only MAC PDU used as the content of all received transport blocks.
  std::vector<uint8_t>               padding_pdu;
  fapi::p5_responses_notifier*       p5_notifier;
  fapi::error_indication_notifier*   error_notifier;
  fapi::p7_slot_indication_notifier* slot_notifier;
  fapi::p7_indications_notifier*     p7_notifier;
  /// Stop control of the tasks deferred to the sector executor.
  stop_event_source stop_control;
};

} // namespace fapi_adaptor
} // namespace ocudu
