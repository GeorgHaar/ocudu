// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/adt/complex.h"
#include "ocudu/ran/precoding/precoding_weight_matrix.h"
#include <optional>

namespace ocudu {

/// \brief Quantization of precoding weights.
///
/// The stages apply in the order of the members: the phase first, the fixed-point representation of the real and
/// imaginary parts second. An absent member disables its stage.
struct precoding_weight_quantization {
  /// Phase resolution in bits, see \ref quantize_phase.
  std::optional<unsigned> phase_bits;
  /// Fraction bits of the sign-and-magnitude fixed-point real and imaginary parts, see \ref quantize_fixed_point.
  std::optional<unsigned> iq_fraction_bits;

  /// Returns true if any stage is active.
  bool is_enabled() const { return phase_bits.has_value() || iq_fraction_bits.has_value(); }
};

/// \brief Quantizes the phase of a coefficient to \f$2^{N}\f$ uniform levels over a full turn, with \f$N\f$ equal to
/// \c nof_bits. The magnitude is preserved.
///
/// A phase that lies exactly between two levels rounds to the higher one. A zero coefficient stays zero.
///
/// \param[in] coefficient Complex coefficient to quantize.
/// \param[in] nof_bits    Phase resolution in bits, from 1 to 16.
/// \return The coefficient with the quantized phase.
cf_t quantize_phase(cf_t coefficient, unsigned nof_bits);

/// Quantizes the phase of every coefficient of a precoding weight matrix in place, as \ref quantize_phase does.
void quantize_phase(precoding_weight_matrix& weights, unsigned nof_bits);

/// \brief Rounds the real and imaginary parts of a coefficient to a sign-and-magnitude fixed-point format.
///
/// The format has one sign bit and \c nof_fraction_bits fraction bits: step \f$2^{-M}\f$, saturation at
/// \f$\pm(1 - 2^{-M})\f$, rounding to nearest with ties to even. Each part is rounded independently.
///
/// \param[in] value             Complex coefficient to round.
/// \param[in] nof_fraction_bits Fraction bits \f$M\f$, from 1 to 23.
/// \return The coefficient with rounded real and imaginary parts.
cf_t quantize_fixed_point(cf_t value, unsigned nof_fraction_bits);

/// Rounds every coefficient of a precoding weight matrix in place, as \ref quantize_fixed_point does.
void quantize_fixed_point(precoding_weight_matrix& weights, unsigned nof_fraction_bits);

/// Applies the active quantization stages to a precoding weight matrix in place.
void quantize(precoding_weight_matrix& weights, const precoding_weight_quantization& quantization);

} // namespace ocudu
