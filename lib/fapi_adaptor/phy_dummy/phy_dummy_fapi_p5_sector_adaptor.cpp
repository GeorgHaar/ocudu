// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI

#include "phy_dummy_fapi_p5_sector_adaptor.h"
#include "ocudu/fapi/common/error_indication_notifier.h"
#include "ocudu/fapi/p5/p5_messages.h"
#include "ocudu/fapi/p5/p5_responses_notifier.h"
#include "ocudu/ran/srs/srs_constants.h"
#include <algorithm>

using namespace ocudu;
using namespace fapi_adaptor;

namespace {

class p5_responses_notifier_dummy : public fapi::p5_responses_notifier
{
public:
  void on_param_response(const fapi::param_response& msg) override {}
  void on_config_response(const fapi::config_response& msg) override {}
  void on_stop_indication(const fapi::stop_indication& msg) override {}
};

class error_indication_notifier_dummy : public fapi::error_indication_notifier
{
public:
  void on_error_indication(const fapi::error_indication& msg) override {}
};

} // namespace

static p5_responses_notifier_dummy     dummy_p5_notifier;
static error_indication_notifier_dummy dummy_error_notifier;

phy_dummy_fapi_p5_sector_adaptor::phy_dummy_fapi_p5_sector_adaptor(unsigned                          sector_id_,
                                                                   ocudulog::basic_logger&           logger_,
                                                                   phy_dummy_fapi_sector_executor&   executor_,
                                                                   phy_dummy_fapi_p7_sector_adaptor& p7_adaptor_) :
  sector_id(sector_id_),
  logger(logger_),
  executor(executor_),
  p7_adaptor(p7_adaptor_),
  p5_notifier(&dummy_p5_notifier),
  error_notifier(&dummy_error_notifier)
{
}

void phy_dummy_fapi_p5_sector_adaptor::send_param_request(const fapi::param_request& msg)
{
  executor.defer("PARAM.request", [this]() {
    fapi::param_response response;
    response.error_code = fapi::error_code_id::msg_ok;
    p5_notifier->on_param_response(response);
  });
}

void phy_dummy_fapi_p5_sector_adaptor::send_config_request(const fapi::config_request& msg)
{
  unsigned nof_rx_ant = std::clamp(static_cast<unsigned>(msg.cell_cfg.carrier_cfg.num_rx_ant),
                                   1U,
                                   static_cast<unsigned>(srs_constants::max_nof_rx_ports));
  executor.defer("CONFIG.request", [this, nof_rx_ant]() {
    nof_rx_antennas = nof_rx_ant;

    fapi::config_response response;
    response.error_code = fapi::error_code_id::msg_ok;
    p5_notifier->on_config_response(response);
  });
}

void phy_dummy_fapi_p5_sector_adaptor::send_start_request(const fapi::start_request& msg)
{
  executor.defer("START.request", [this]() {
    if (p7_adaptor.is_running()) {
      logger.warning("Sector#{}: PHY dummy FAPI adaptor ignored START.request. Cause: sector is running", sector_id);
      return;
    }
    p7_adaptor.start(nof_rx_antennas);
  });
}

void phy_dummy_fapi_p5_sector_adaptor::send_stop_request(const fapi::stop_request& msg)
{
  executor.defer("STOP.request", [this]() {
    if (!p7_adaptor.is_running() || p7_adaptor.is_stop_requested()) {
      logger.warning("Sector#{}: PHY dummy FAPI adaptor ignored STOP.request. Cause: sector is not running", sector_id);
      return;
    }
    p7_adaptor.request_stop(*this);
  });
}

void phy_dummy_fapi_p5_sector_adaptor::on_p7_stopped()
{
  p5_notifier->on_stop_indication(fapi::stop_indication{});
}
