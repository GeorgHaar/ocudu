// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "ocudu/scheduler/result/vrb_alloc.h"
#include "ocudu/support/enum_utils.h"

using namespace ocudu;

/// Computes the BWP VRBs covered by an RBG.
static vrb_interval get_rbg_vrbs(crb_interval bwp_rbs, nominal_rbg_size P, unsigned first_rbg_size, unsigned rbg_idx)
{
  const unsigned vrb_start = (rbg_idx == 0) ? 0 : (rbg_idx - 1) * to_underlying(P) + first_rbg_size;
  return {vrb_start, vrb_start + get_rbg_size(bwp_rbs, P, rbg_idx)};
}

vrb_bitmap ocudu::convert_rbgs_to_vrbs(const rbg_bitmap& rbgs, crb_interval bwp_rbs, nominal_rbg_size P)
{
  const unsigned first_rbg_size = get_rbg_size(bwp_rbs, P, 0);

  vrb_bitmap vrbs(bwp_rbs.length());
  for (unsigned idx = 0, nof_rbgs = rbgs.size(); idx != nof_rbgs; ++idx) {
    if (rbgs.test(idx)) {
      vrbs |= get_rbg_vrbs(bwp_rbs, P, first_rbg_size, idx);
    }
  }
  return vrbs;
}

rbg_bitmap ocudu::convert_vrbs_to_rbgs(const vrb_bitmap& vrbs, crb_interval bwp_rbs, nominal_rbg_size P)
{
  const unsigned first_rbg_size = get_rbg_size(bwp_rbs, P, 0);

  rbg_bitmap rbgs(get_nof_rbgs(bwp_rbs, P));
  for (unsigned idx = 0, nof_rbgs = rbgs.size(); idx != nof_rbgs; ++idx) {
    const vrb_interval rbg_vrbs = get_rbg_vrbs(bwp_rbs, P, first_rbg_size, idx);
    if (vrbs.any(rbg_vrbs.start(), rbg_vrbs.stop())) {
      rbgs.set(idx);
    }
  }
  return rbgs;
}
