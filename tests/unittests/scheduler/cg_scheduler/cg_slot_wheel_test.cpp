// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

/// \file
/// \brief Unit tests for cg_slot_wheel. Verifies where a UE's CG opportunities land, that add/remove are exact
/// inverses, and that a UE can be swept out of the wheel when the offset and period it was added with are no longer
/// known.

#include "lib/scheduler/configured_grant/cg_slot_wheel.h"
#include <gtest/gtest.h>

using namespace ocudu;

namespace {

constexpr rnti_t rnti1 = to_rnti(0x4601);
constexpr rnti_t rnti2 = to_rnti(0x4602);

/// Counts the offsets of the whole wheel at which \c rnti has a CG opportunity.
unsigned count_entries(const cg_slot_wheel& wheel, rnti_t rnti)
{
  unsigned count = 0;
  for (unsigned offset = 0; offset != cg_slot_wheel::max_cg_slot_periodicity; ++offset) {
    if (wheel.contains(slot_point{to_numerology_value(subcarrier_spacing::kHz15), offset}, rnti)) {
      ++count;
    }
  }
  return count;
}

TEST(cg_slot_wheel_test, ue_is_added_at_every_multiple_of_the_period)
{
  constexpr unsigned offset = 3;
  constexpr unsigned period = 10;

  cg_slot_wheel wheel;
  ASSERT_EQ(0, wheel.add_ue(rnti1, offset, period));

  ASSERT_EQ(cg_slot_wheel::max_cg_slot_periodicity / period, count_entries(wheel, rnti1));
  for (unsigned n = 0; n != cg_slot_wheel::max_cg_slot_periodicity; ++n) {
    const slot_point sl{to_numerology_value(subcarrier_spacing::kHz15), n};
    ASSERT_EQ(n % period == offset, wheel.contains(sl, rnti1)) << "unexpected wheel content at offset " << n;
  }
}

TEST(cg_slot_wheel_test, rem_ue_undoes_add_ue)
{
  constexpr unsigned offset = 7;
  constexpr unsigned period = 20;

  cg_slot_wheel wheel;
  ASSERT_EQ(0, wheel.add_ue(rnti1, offset, period));
  ASSERT_EQ(0, wheel.rem_ue(rnti1, offset, period));

  ASSERT_EQ(0, count_entries(wheel, rnti1));
}

TEST(cg_slot_wheel_test, adding_the_same_ue_twice_does_not_duplicate_its_grants)
{
  constexpr unsigned offset      = 5;
  constexpr unsigned period      = 40;
  constexpr unsigned nof_entries = cg_slot_wheel::max_cg_slot_periodicity / period;

  cg_slot_wheel wheel;
  ASSERT_EQ(0, wheel.add_ue(rnti1, offset, period));

  // The second add reports every offset as a duplicate and leaves the wheel untouched, so a single removal suffices.
  ASSERT_EQ(nof_entries, wheel.add_ue(rnti1, offset, period));
  ASSERT_EQ(nof_entries, count_entries(wheel, rnti1));

  ASSERT_EQ(0, wheel.rem_ue(rnti1, offset, period));
  ASSERT_EQ(0, count_entries(wheel, rnti1));
}

TEST(cg_slot_wheel_test, removing_a_ue_that_was_never_added_reports_every_offset_as_missing)
{
  constexpr unsigned offset = 1;
  constexpr unsigned period = 80;

  cg_slot_wheel wheel;
  ASSERT_EQ(cg_slot_wheel::max_cg_slot_periodicity / period, wheel.rem_ue(rnti1, offset, period));
}

TEST(cg_slot_wheel_test, removing_one_ue_preserves_the_grants_of_another)
{
  constexpr unsigned offset = 2;
  constexpr unsigned period = 10;

  // Both UEs share the same offset, so they sit in the same per-slot lists and a removal must not disturb the other.
  cg_slot_wheel wheel;
  ASSERT_EQ(0, wheel.add_ue(rnti1, offset, period));
  ASSERT_EQ(0, wheel.add_ue(rnti2, offset, period));

  ASSERT_EQ(0, wheel.rem_ue(rnti1, offset, period));

  ASSERT_EQ(0, count_entries(wheel, rnti1));
  ASSERT_EQ(cg_slot_wheel::max_cg_slot_periodicity / period, count_entries(wheel, rnti2));
}

TEST(cg_slot_wheel_test, rem_ue_with_the_wrong_period_leaves_stale_grants_that_a_sweep_reclaims)
{
  constexpr unsigned offset     = 4;
  constexpr unsigned period     = 10;
  constexpr unsigned bad_period = 20;

  cg_slot_wheel wheel;
  ASSERT_EQ(0, wheel.add_ue(rnti1, offset, period));

  // Removing with a period the UE was not added with only reaches half of its entries; the rest leak.
  ASSERT_EQ(0, wheel.rem_ue(rnti1, offset, bad_period));
  const unsigned nof_stale = cg_slot_wheel::max_cg_slot_periodicity / period - count_entries(wheel, rnti1);
  ASSERT_EQ(cg_slot_wheel::max_cg_slot_periodicity / bad_period, nof_stale);
  ASSERT_NE(0, count_entries(wheel, rnti1));

  // A sweep reclaims them without needing to know the offset or the period.
  ASSERT_EQ(cg_slot_wheel::max_cg_slot_periodicity / bad_period, wheel.rem_ue_everywhere(rnti1));
  ASSERT_EQ(0, count_entries(wheel, rnti1));
}

TEST(cg_slot_wheel_test, sweep_removes_all_the_grants_of_one_ue_only)
{
  cg_slot_wheel wheel;
  ASSERT_EQ(0, wheel.add_ue(rnti1, 3, 10));
  ASSERT_EQ(0, wheel.add_ue(rnti2, 8, 40));

  ASSERT_EQ(cg_slot_wheel::max_cg_slot_periodicity / 10, wheel.rem_ue_everywhere(rnti1));

  ASSERT_EQ(0, count_entries(wheel, rnti1));
  ASSERT_EQ(cg_slot_wheel::max_cg_slot_periodicity / 40, count_entries(wheel, rnti2));

  // Sweeping a UE that is not in the wheel is a no-op.
  ASSERT_EQ(0, wheel.rem_ue_everywhere(rnti1));
}

TEST(cg_slot_wheel_test, clear_empties_the_whole_wheel)
{
  cg_slot_wheel wheel;
  ASSERT_EQ(0, wheel.add_ue(rnti1, 0, 10));
  ASSERT_EQ(0, wheel.add_ue(rnti2, 5, 10));

  wheel.clear();

  ASSERT_EQ(0, count_entries(wheel, rnti1));
  ASSERT_EQ(0, count_entries(wheel, rnti2));
}

} // namespace
