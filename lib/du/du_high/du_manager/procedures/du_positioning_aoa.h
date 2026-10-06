// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/f1ap/du/f1ap_du_positioning_handler.h"
#include "ocudu/mac/mac_positioning_measurement_handler.h"
#include "ocudu/ran/positioning/common.h"
#include "ocudu/ran/positioning/ul_aoa_mapping.h"
#include "ocudu/support/ocudu_assert.h"
#include <optional>

namespace ocudu::odu {

/// \brief Returns the UL Angle of Arrival result of an SRS measurement, as per TS 38.473 section 9.3.1.167.
///
/// The MAC gives the angles in the LCS of the antenna array. The result carries the orientation of the array as the
/// LCS to GCS Translation, so that the LMF can convert the angles to the GCS. When the orientation is empty, the angles
/// are in the GCS.
///
/// \param[in] meas        SRS measurement of the MAC. It must contain the azimuth angle.
/// \param[in] orientation Orientation of the antenna array of the cell, if configured.
inline pos_meas_result_ul_aoa make_ul_aoa_result(const mac_positioning_measurement_response::ul_srs_pos_meas& meas,
                                                 const std::optional<lcs_to_gcs_translation_t>& orientation)
{
  ocudu_assert(meas.azimuth_aoa_deg.has_value(), "The SRS measurement has no azimuth angle of arrival");

  pos_meas_result_ul_aoa result{.azimuth_aoa = azimuth_aoa_to_reported_value(*meas.azimuth_aoa_deg)};
  if (meas.zenith_aoa_deg.has_value()) {
    result.zenith_aoa = zenith_aoa_to_reported_value(*meas.zenith_aoa_deg);
  }
  result.lcs_to_gcs_translation = orientation;
  return result;
}

} // namespace ocudu::odu
