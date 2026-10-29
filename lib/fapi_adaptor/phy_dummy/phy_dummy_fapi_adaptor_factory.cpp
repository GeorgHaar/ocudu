// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI

#include "ocudu/fapi_adaptor/phy_dummy/phy_dummy_fapi_adaptor_factory.h"
#include "phy_dummy_fapi_adaptor_impl.h"

using namespace ocudu;
using namespace fapi_adaptor;

std::unique_ptr<phy_fapi_adaptor>
ocudu::fapi_adaptor::create_phy_dummy_fapi_adaptor(const phy_dummy_fapi_adaptor_config&       config,
                                                   const phy_dummy_fapi_adaptor_dependencies& dependencies)
{
  return std::make_unique<phy_dummy_fapi_adaptor_impl>(config, dependencies);
}
