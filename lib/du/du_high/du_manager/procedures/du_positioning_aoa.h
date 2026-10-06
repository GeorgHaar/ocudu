// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/f1ap/du/f1ap_du_positioning_handler.h"
#include "ocudu/mac/mac_positioning_measurement_handler.h"
#include "ocudu/ran/positioning/ul_aoa_mapping.h"
#include "ocudu/support/ocudu_assert.h"

namespace ocudu::odu {

/// \brief Returns the UL Angle of Arrival result of an SRS measurement, as per TS 38.473 section 9.3.1.167.
///
/// \param[in] meas SRS measurement of the MAC. It must contain the azimuth angle.
inline pos_meas_result_ul_aoa make_ul_aoa_result(const mac_positioning_measurement_response::ul_srs_pos_meas& meas)
{
  ocudu_assert(meas.azimuth_aoa_deg.has_value(), "The SRS measurement has no azimuth angle of arrival");

  pos_meas_result_ul_aoa result{.azimuth_aoa = azimuth_aoa_to_reported_value(*meas.azimuth_aoa_deg)};
  if (meas.zenith_aoa_deg.has_value()) {
    result.zenith_aoa = zenith_aoa_to_reported_value(*meas.zenith_aoa_deg);
  }
  return result;
}

} // namespace ocudu::odu
