// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI

#pragma once

#include "phy_dummy_fapi_p7_sector_adaptor.h"
#include "phy_dummy_fapi_sector_executor.h"
#include "ocudu/fapi/p5/p5_requests_gateway.h"
#include "ocudu/fapi_adaptor/phy/p5/phy_fapi_p5_sector_adaptor.h"
#include "ocudu/ocudulog/logger.h"

namespace ocudu {
namespace fapi_adaptor {

/// \brief PHY dummy FAPI adaptor P5 sector.
///
/// Accepts every P5 request of one cell, and starts and stops its P7 sector. Its logic runs in the sector executor.
class phy_dummy_fapi_p5_sector_adaptor : public phy_fapi_p5_sector_adaptor,
                                         private fapi::p5_requests_gateway,
                                         private phy_dummy_fapi_p7_stop_notifier
{
public:
  phy_dummy_fapi_p5_sector_adaptor(unsigned                          sector_id_,
                                   ocudulog::basic_logger&           logger_,
                                   phy_dummy_fapi_sector_executor&   executor_,
                                   phy_dummy_fapi_p7_sector_adaptor& p7_adaptor_);

  // See interface for documentation.
  fapi::p5_requests_gateway& get_p5_requests_gateway() override { return *this; }

  // See interface for documentation.
  void set_p5_responses_notifier(fapi::p5_responses_notifier& notifier) override { p5_notifier = &notifier; }

  // See interface for documentation.
  void set_error_indication_notifier(fapi::error_indication_notifier& notifier) override { error_notifier = &notifier; }

private:
  // See interface for documentation.
  void send_param_request(const fapi::param_request& msg) override;

  // See interface for documentation.
  void send_config_request(const fapi::config_request& msg) override;

  // See interface for documentation.
  void send_start_request(const fapi::start_request& msg) override;

  // See interface for documentation.
  void send_stop_request(const fapi::stop_request& msg) override;

  // See interface for documentation.
  void on_p7_stopped() override;

  const unsigned                    sector_id;
  ocudulog::basic_logger&           logger;
  phy_dummy_fapi_sector_executor&   executor;
  phy_dummy_fapi_p7_sector_adaptor& p7_adaptor;
  /// Number of receive antennas reported in the CONFIG.request.
  unsigned                         nof_rx_antennas = 1;
  fapi::p5_responses_notifier*     p5_notifier;
  fapi::error_indication_notifier* error_notifier;
};

} // namespace fapi_adaptor
} // namespace ocudu
