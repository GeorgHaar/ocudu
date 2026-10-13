// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "demodulation_mapper_qam1024.h"
#include "demodulation_mapper_intervals.h"
#include "ocudu/support/math/complex_math_utils.h"

using namespace ocudu;

// Square root of 1/682.
static const float M_SQRT1_682 = 1.0F / std::sqrt(682.0F);

// Maximum (absolute) value considered for quantization. Larger values will be clipped.
static constexpr float                           RANGE_LIMIT_FLOAT = 20;
static const float                               INTERVAL_WIDTH_01 = 2.0F * M_SQRT1_682;
static constexpr unsigned                        NOF_INTERVALS_01  = 32;
static const std::array<float, NOF_INTERVALS_01> SLOPE_01          = {
    64.0F * M_SQRT1_682, 60.0F * M_SQRT1_682, 56.0F * M_SQRT1_682, 52.0F * M_SQRT1_682, 48.0F * M_SQRT1_682,
    44.0F * M_SQRT1_682, 40.0F * M_SQRT1_682, 36.0F * M_SQRT1_682, 32.0F * M_SQRT1_682, 28.0F * M_SQRT1_682,
    24.0F * M_SQRT1_682, 20.0F * M_SQRT1_682, 16.0F * M_SQRT1_682, 12.0F * M_SQRT1_682, 8.0F * M_SQRT1_682,
    4.0F * M_SQRT1_682,  4.0F * M_SQRT1_682,  8.0F * M_SQRT1_682,  12.0F * M_SQRT1_682, 16.0F * M_SQRT1_682,
    20.0F * M_SQRT1_682, 24.0F * M_SQRT1_682, 28.0F * M_SQRT1_682, 32.0F * M_SQRT1_682, 36.0F * M_SQRT1_682,
    40.0F * M_SQRT1_682, 44.0F * M_SQRT1_682, 48.0F * M_SQRT1_682, 52.0F * M_SQRT1_682, 56.0F * M_SQRT1_682,
    60.0F * M_SQRT1_682, 64.0F * M_SQRT1_682,
};
static constexpr std::array<float, NOF_INTERVALS_01> INTERCEPT_01 = {
    480.0F / 341.0F,
    420.0F / 341.0F,
    364.0F / 341.0F,
    312.0F / 341.0F,
    24.0F / 31.0F,
    20.0F / 31.0F,
    180.0F / 341.0F,
    144.0F / 341.0F,
    112.0F / 341.0F,
    84.0F / 341.0F,
    60.0F / 341.0F,
    40.0F / 341.0F,
    24.0F / 341.0F,
    12.0F / 341.0F,
    4.0F / 341.0F,
    0.0F,
    0.0F,
    -4.0F / 341.0F,
    -12.0F / 341.0F,
    -24.0F / 341.0F,
    -40.0F / 341.0F,
    -60.0F / 341.0F,
    -84.0F / 341.0F,
    -112.0F / 341.0F,
    -144.0F / 341.0F,
    -180.0F / 341.0F,
    -20.0F / 31.0F,
    -24.0F / 31.0F,
    -312.0F / 341.0F,
    -364.0F / 341.0F,
    -420.0F / 341.0F,
    -480.0F / 341.0F,
};

static const float                               INTERVAL_WIDTH_23 = 2.0F * M_SQRT1_682;
static constexpr unsigned                        NOF_INTERVALS_23  = 32;
static const std::array<float, NOF_INTERVALS_23> SLOPE_23          = {
    32.0F * M_SQRT1_682,  28.0F * M_SQRT1_682,  24.0F * M_SQRT1_682,  20.0F * M_SQRT1_682,  16.0F * M_SQRT1_682,
    12.0F * M_SQRT1_682,  8.0F * M_SQRT1_682,   4.0F * M_SQRT1_682,   4.0F * M_SQRT1_682,   8.0F * M_SQRT1_682,
    12.0F * M_SQRT1_682,  16.0F * M_SQRT1_682,  20.0F * M_SQRT1_682,  24.0F * M_SQRT1_682,  28.0F * M_SQRT1_682,
    32.0F * M_SQRT1_682,  -32.0F * M_SQRT1_682, -28.0F * M_SQRT1_682, -24.0F * M_SQRT1_682, -20.0F * M_SQRT1_682,
    -16.0F * M_SQRT1_682, -12.0F * M_SQRT1_682, -8.0F * M_SQRT1_682,  -4.0F * M_SQRT1_682,  -4.0F * M_SQRT1_682,
    -8.0F * M_SQRT1_682,  -12.0F * M_SQRT1_682, -16.0F * M_SQRT1_682, -20.0F * M_SQRT1_682, -24.0F * M_SQRT1_682,
    -28.0F * M_SQRT1_682, -32.0F * M_SQRT1_682,
};
static constexpr std::array<float, NOF_INTERVALS_23> INTERCEPT_23 = {
    368.0F / 341.0F, 28.0F / 31.0F,   252.0F / 341.0F, 200.0F / 341.0F, 152.0F / 341.0F, 108.0F / 341.0F,
    68.0F / 341.0F,  32.0F / 341.0F,  32.0F / 341.0F,  60.0F / 341.0F,  84.0F / 341.0F,  104.0F / 341.0F,
    120.0F / 341.0F, 12.0F / 31.0F,   140.0F / 341.0F, 144.0F / 341.0F, 144.0F / 341.0F, 140.0F / 341.0F,
    12.0F / 31.0F,   120.0F / 341.0F, 104.0F / 341.0F, 84.0F / 341.0F,  60.0F / 341.0F,  32.0F / 341.0F,
    32.0F / 341.0F,  68.0F / 341.0F,  108.0F / 341.0F, 152.0F / 341.0F, 200.0F / 341.0F, 252.0F / 341.0F,
    28.0F / 31.0F,   368.0F / 341.0F,
};

static const float                               INTERVAL_WIDTH_45 = 2.0F * M_SQRT1_682;
static constexpr unsigned                        NOF_INTERVALS_45  = 32;
static const std::array<float, NOF_INTERVALS_45> SLOPE_45          = {
    16.0F * M_SQRT1_682,  12.0F * M_SQRT1_682,  8.0F * M_SQRT1_682,  4.0F * M_SQRT1_682,   4.0F * M_SQRT1_682,
    8.0F * M_SQRT1_682,   12.0F * M_SQRT1_682,  16.0F * M_SQRT1_682, -16.0F * M_SQRT1_682, -12.0F * M_SQRT1_682,
    -8.0F * M_SQRT1_682,  -4.0F * M_SQRT1_682,  -4.0F * M_SQRT1_682, -8.0F * M_SQRT1_682,  -12.0F * M_SQRT1_682,
    -16.0F * M_SQRT1_682, 16.0F * M_SQRT1_682,  12.0F * M_SQRT1_682, 8.0F * M_SQRT1_682,   4.0F * M_SQRT1_682,
    4.0F * M_SQRT1_682,   8.0F * M_SQRT1_682,   12.0F * M_SQRT1_682, 16.0F * M_SQRT1_682,  -16.0F * M_SQRT1_682,
    -12.0F * M_SQRT1_682, -8.0F * M_SQRT1_682,  -4.0F * M_SQRT1_682, -4.0F * M_SQRT1_682,  -8.0F * M_SQRT1_682,
    -12.0F * M_SQRT1_682, -16.0F * M_SQRT1_682,
};
static constexpr std::array<float, NOF_INTERVALS_45> INTERCEPT_45 = {
    216.0F / 341.0F, 156.0F / 341.0F, 100.0F / 341.0F, 48.0F / 341.0F,  48.0F / 341.0F,  92.0F / 341.0F,
    12.0F / 31.0F,   168.0F / 341.0F, -8.0F / 31.0F,   -60.0F / 341.0F, -36.0F / 341.0F, -16.0F / 341.0F,
    -16.0F / 341.0F, -28.0F / 341.0F, -36.0F / 341.0F, -40.0F / 341.0F, -40.0F / 341.0F, -36.0F / 341.0F,
    -28.0F / 341.0F, -16.0F / 341.0F, -16.0F / 341.0F, -36.0F / 341.0F, -60.0F / 341.0F, -8.0F / 31.0F,
    168.0F / 341.0F, 12.0F / 31.0F,   92.0F / 341.0F,  48.0F / 341.0F,  48.0F / 341.0F,  100.0F / 341.0F,
    156.0F / 341.0F, 216.0F / 341.0F,
};

static const float                               INTERVAL_WIDTH_67 = 2.0F * M_SQRT1_682;
static constexpr unsigned                        NOF_INTERVALS_67  = 32;
static const std::array<float, NOF_INTERVALS_67> SLOPE_67          = {
    8.0F * M_SQRT1_682,  4.0F * M_SQRT1_682,  4.0F * M_SQRT1_682,  8.0F * M_SQRT1_682,  -8.0F * M_SQRT1_682,
    -4.0F * M_SQRT1_682, -4.0F * M_SQRT1_682, -8.0F * M_SQRT1_682, 8.0F * M_SQRT1_682,  4.0F * M_SQRT1_682,
    4.0F * M_SQRT1_682,  8.0F * M_SQRT1_682,  -8.0F * M_SQRT1_682, -4.0F * M_SQRT1_682, -4.0F * M_SQRT1_682,
    -8.0F * M_SQRT1_682, 8.0F * M_SQRT1_682,  4.0F * M_SQRT1_682,  4.0F * M_SQRT1_682,  8.0F * M_SQRT1_682,
    -8.0F * M_SQRT1_682, -4.0F * M_SQRT1_682, -4.0F * M_SQRT1_682, -8.0F * M_SQRT1_682, 8.0F * M_SQRT1_682,
    4.0F * M_SQRT1_682,  4.0F * M_SQRT1_682,  8.0F * M_SQRT1_682,  -8.0F * M_SQRT1_682, -4.0F * M_SQRT1_682,
    -4.0F * M_SQRT1_682, -8.0F * M_SQRT1_682,
};

static constexpr std::array<float, NOF_INTERVALS_67> INTERCEPT_67 = {
    116.0F / 341.0F, 56.0F / 341.0F,  56.0F / 341.0F,  108.0F / 341.0F, -84.0F / 341.0F, -40.0F / 341.0F,
    -40.0F / 341.0F, -76.0F / 341.0F, 52.0F / 341.0F,  24.0F / 341.0F,  24.0F / 341.0F,  4.0F / 31.0F,
    -20.0F / 341.0F, -8.0F / 341.0F,  -8.0F / 341.0F,  -12.0F / 341.0F, -12.0F / 341.0F, -8.0F / 341.0F,
    -8.0F / 341.0F,  -20.0F / 341.0F, 4.0F / 31.0F,    24.0F / 341.0F,  24.0F / 341.0F,  52.0F / 341.0F,
    -76.0F / 341.0F, -40.0F / 341.0F, -40.0F / 341.0F, -84.0F / 341.0F, 108.0F / 341.0F, 56.0F / 341.0F,
    56.0F / 341.0F,  116.0F / 341.0F,
};

static const float                               INTERVAL_WIDTH_89 = 4.0F * M_SQRT1_682;
static constexpr unsigned                        NOF_INTERVALS_89  = 16;
static const std::array<float, NOF_INTERVALS_89> SLOPE_89          = {
    4.0F * M_SQRT1_682,
    -4.0F * M_SQRT1_682,
    4.0F * M_SQRT1_682,
    -4.0F * M_SQRT1_682,
    4.0F * M_SQRT1_682,
    -4.0F * M_SQRT1_682,
    4.0F * M_SQRT1_682,
    -4.0F * M_SQRT1_682,
    4.0F * M_SQRT1_682,
    -4.0F * M_SQRT1_682,
    4.0F * M_SQRT1_682,
    -4.0F * M_SQRT1_682,
    4.0F * M_SQRT1_682,
    -4.0F * M_SQRT1_682,
    4.0F * M_SQRT1_682,
    -4.0F * M_SQRT1_682,
};
static constexpr std::array<float, NOF_INTERVALS_89> INTERCEPT_89 = {
    60.0F / 341.0F,
    -52.0F / 341.0F,
    4.0F / 31.0F,
    -36.0F / 341.0F,
    28.0F / 341.0F,
    -20.0F / 341.0F,
    12.0F / 341.0F,
    -4.0F / 341.0F,
    -4.0F / 341.0F,
    12.0F / 341.0F,
    -20.0F / 341.0F,
    28.0F / 341.0F,
    -36.0F / 341.0F,
    4.0F / 31.0F,
    -52.0F / 341.0F,
    60.0F / 341.0F,
};

static log_likelihood_ratio demod_1024QAM_symbol_01(float value, float rcp_noise_var)
{
  float l_value = interval_function(value, rcp_noise_var, INTERVAL_WIDTH_01, NOF_INTERVALS_01, SLOPE_01, INTERCEPT_01);
  return log_likelihood_ratio::quantize(l_value, RANGE_LIMIT_FLOAT);
}

static log_likelihood_ratio demod_1024QAM_symbol_23(float value, float rcp_noise_var)
{
  float l_value = interval_function(value, rcp_noise_var, INTERVAL_WIDTH_23, NOF_INTERVALS_23, SLOPE_23, INTERCEPT_23);
  return log_likelihood_ratio::quantize(l_value, RANGE_LIMIT_FLOAT);
}

static log_likelihood_ratio demod_1024QAM_symbol_45(float value, float rcp_noise_var)
{
  float l_value = interval_function(value, rcp_noise_var, INTERVAL_WIDTH_45, NOF_INTERVALS_45, SLOPE_45, INTERCEPT_45);
  return log_likelihood_ratio::quantize(l_value, RANGE_LIMIT_FLOAT);
}

static log_likelihood_ratio demod_1024QAM_symbol_67(float value, float rcp_noise_var)
{
  float l_value = interval_function(value, rcp_noise_var, INTERVAL_WIDTH_67, NOF_INTERVALS_67, SLOPE_67, INTERCEPT_67);
  return log_likelihood_ratio::quantize(l_value, RANGE_LIMIT_FLOAT);
}

static log_likelihood_ratio demod_1024QAM_symbol_89(float value, float rcp_noise_var)
{
  float l_value = interval_function(value, rcp_noise_var, INTERVAL_WIDTH_89, NOF_INTERVALS_89, SLOPE_89, INTERCEPT_89);
  return log_likelihood_ratio::quantize(l_value, RANGE_LIMIT_FLOAT);
}

void ocudu::demodulate_soft_QAM1024(span<log_likelihood_ratio> llrs,
                                    span<const cf_t>           symbols,
                                    span<const float>          noise_vars)
{
  const cf_t*           symbols_it   = symbols.begin();
  const float*          noise_it     = noise_vars.begin();
  log_likelihood_ratio* llr_it       = llrs.begin();
  std::size_t           symbol_index = 0;

  for (std::size_t symbol_index_end = symbols.size(); symbol_index != symbol_index_end; ++symbol_index) {
    //  Set all LLR to zero if the symbol is near zero.
    if (is_near_zero(*symbols_it)) {
      for (unsigned i_bit = 0; i_bit != 10; ++i_bit) {
        *llr_it++ = 0;
      }
      ++symbols_it;
      ++noise_it;
      continue;
    }

    float rcp_noise = 0;
    // Note: "noise_var > 0" is false also when "noise_var" is NaN.
    if (*noise_it > 0) {
      rcp_noise = 1 / (*noise_it);
    }
    *llr_it++ = demod_1024QAM_symbol_01(std::real(*symbols_it), rcp_noise);
    *llr_it++ = demod_1024QAM_symbol_01(std::imag(*symbols_it), rcp_noise);
    *llr_it++ = demod_1024QAM_symbol_23(std::real(*symbols_it), rcp_noise);
    *llr_it++ = demod_1024QAM_symbol_23(std::imag(*symbols_it), rcp_noise);
    *llr_it++ = demod_1024QAM_symbol_45(std::real(*symbols_it), rcp_noise);
    *llr_it++ = demod_1024QAM_symbol_45(std::imag(*symbols_it), rcp_noise);
    *llr_it++ = demod_1024QAM_symbol_67(std::real(*symbols_it), rcp_noise);
    *llr_it++ = demod_1024QAM_symbol_67(std::imag(*symbols_it), rcp_noise);
    *llr_it++ = demod_1024QAM_symbol_89(std::real(*symbols_it), rcp_noise);
    *llr_it++ = demod_1024QAM_symbol_89(std::imag(*symbols_it), rcp_noise);
    ++symbols_it;
    ++noise_it;
  }
}
