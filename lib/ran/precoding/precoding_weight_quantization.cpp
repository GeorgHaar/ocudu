// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "ocudu/ran/precoding/precoding_weight_quantization.h"
#include "ocudu/support/math/math_utils.h"
#include <algorithm>
#include <cmath>

using namespace ocudu;

cf_t ocudu::quantize_phase(cf_t coefficient, unsigned nof_bits)
{
  ocudu_assert((nof_bits > 0) && (nof_bits <= 16),
               "The number of bits (i.e., {}) must be in the range 1..16.",
               nof_bits);

  // The phase of a zero coefficient is undefined and must not become a non-zero level.
  float magnitude = std::abs(coefficient);
  if (!(magnitude > 0.0F)) {
    return coefficient;
  }

  float step  = TWOPI / static_cast<float>(1U << nof_bits);
  float index = std::arg(coefficient) / step;

  // The DFT grid of the Type I codebook places phases exactly between two levels when the resolution is below the
  // grid spacing. The small bias keeps those ties on the upper level regardless of rounding noise in the input.
  float quantized_index = std::floor(index + 0.5F + 1e-4F);

  return std::polar(magnitude, quantized_index * step);
}

void ocudu::quantize_phase(precoding_weight_matrix& weights, unsigned nof_bits)
{
  for (unsigned i_layer = 0, nof_layers = weights.get_nof_layers(); i_layer != nof_layers; ++i_layer) {
    for (unsigned i_port = 0, nof_ports = weights.get_nof_ports(); i_port != nof_ports; ++i_port) {
      weights.set_coefficient(quantize_phase(weights.get_coefficient(i_layer, i_port), nof_bits), i_layer, i_port);
    }
  }
}

/// Rounds one real value to the sign-and-magnitude fixed-point grid with the given scale \f$2^{M}\f$ and maximum
/// magnitude code \f$2^{M} - 1\f$.
static float round_fixed_point(float value, float scale, float max_code)
{
  // The saturation precedes the scaling so that any finite input stays within the code range.
  float magnitude = std::min(std::abs(value), 1.0F);
  // nearbyint rounds to nearest with ties to even in the default rounding mode.
  float code = std::min(std::nearbyint(magnitude * scale), max_code);
  return std::copysign(code / scale, value);
}

cf_t ocudu::quantize_fixed_point(cf_t value, unsigned nof_fraction_bits)
{
  ocudu_assert((nof_fraction_bits > 0) && (nof_fraction_bits <= 23),
               "The number of fraction bits (i.e., {}) must be in the range 1..23.",
               nof_fraction_bits);

  float scale    = static_cast<float>(1U << nof_fraction_bits);
  float max_code = scale - 1.0F;

  return {round_fixed_point(value.real(), scale, max_code), round_fixed_point(value.imag(), scale, max_code)};
}

void ocudu::quantize_fixed_point(precoding_weight_matrix& weights, unsigned nof_fraction_bits)
{
  for (unsigned i_layer = 0, nof_layers = weights.get_nof_layers(); i_layer != nof_layers; ++i_layer) {
    for (unsigned i_port = 0, nof_ports = weights.get_nof_ports(); i_port != nof_ports; ++i_port) {
      weights.set_coefficient(
          quantize_fixed_point(weights.get_coefficient(i_layer, i_port), nof_fraction_bits), i_layer, i_port);
    }
  }
}

void ocudu::quantize(precoding_weight_matrix& weights, const precoding_weight_quantization& quantization)
{
  if (quantization.phase_bits.has_value()) {
    quantize_phase(weights, *quantization.phase_bits);
  }
  if (quantization.iq_fraction_bits.has_value()) {
    quantize_fixed_point(weights, *quantization.iq_fraction_bits);
  }
}
