// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI

#pragma once

#include "ocudu/fapi_adaptor/precoding_codebook_mapper.h"
#include "ocudu/fapi_adaptor/precoding_codebook_repository.h"
#include "ocudu/ran/antenna_topology.h"
#include "ocudu/ran/precoding/precoding_weight_quantization.h"
#include <memory>

namespace ocudu {
namespace fapi_adaptor {

/// \brief Generates the precoding codebook mapper and repository.
///
/// The codebook gives the precoding that the user equipment reports. The topology gives the antenna array that
/// transmits it. Both must describe the same number of antenna ports.
/// \param[in] quantization Quantization of the Type I single-panel PDSCH weights, see
///                         \ref precoding_weight_quantization. None by default.
std::pair<std::unique_ptr<precoding_codebook_mapper>, std::unique_ptr<precoding_codebook_repository>>
generate_precoding_codebooks(const pmi_codebook_config&           codebook_config,
                             antenna_topology                     topology,
                             unsigned                             sector_id,
                             const precoding_weight_quantization& quantization = {});

} // namespace fapi_adaptor
} // namespace ocudu
