// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI

#pragma once

#include "ocudu/fapi_adaptor/phy/phy_fapi_adaptor.h"
#include "ocudu/fapi_adaptor/phy_dummy/phy_dummy_fapi_adaptor_config.h"
#include <memory>

namespace ocudu {
namespace fapi_adaptor {

/// Creates a PHY dummy FAPI adaptor, which emulates a PHY at the FAPI interface.
std::unique_ptr<phy_fapi_adaptor>
create_phy_dummy_fapi_adaptor(const phy_dummy_fapi_adaptor_config&       config,
                              const phy_dummy_fapi_adaptor_dependencies& dependencies);

} // namespace fapi_adaptor
} // namespace ocudu
