// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/fapi_adaptor/phy/dummy/fapi_dummy_config.h"
#include "ocudu/fapi_adaptor/phy/phy_fapi_adaptor.h"
#include <memory>

namespace ocudu {
namespace fapi_adaptor {

/// Creates a FAPI dummy, which emulates a PHY at the FAPI interface.
std::unique_ptr<phy_fapi_adaptor> create_fapi_dummy(const fapi_dummy_config&       config,
                                                    const fapi_dummy_dependencies& dependencies);

} // namespace fapi_adaptor
} // namespace ocudu
