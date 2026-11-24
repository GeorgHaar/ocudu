// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI

#include "phy_dummy_fapi_sector_adaptor.h"

using namespace ocudu;
using namespace fapi_adaptor;

phy_dummy_fapi_sector_adaptor::phy_dummy_fapi_sector_adaptor(const phy_dummy_fapi_sector_adaptor_config& config,
                                                             ocudulog::basic_logger&                     logger,
                                                             task_executor&                              executor) :
  sector_executor(config.sector_id, logger, executor),
  p7_adaptor(config, logger, sector_executor),
  p5_adaptor(config.sector_id, logger, sector_executor, p7_adaptor)
{
}

phy_dummy_fapi_sector_adaptor::~phy_dummy_fapi_sector_adaptor()
{
  // Pending tasks hold references to the P5 and P7 sectors, so they finish before the sectors are destroyed.
  sector_executor.stop();
}
