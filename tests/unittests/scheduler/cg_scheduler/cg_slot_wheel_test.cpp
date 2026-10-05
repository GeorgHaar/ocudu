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

/// \brief Counts the offsets of the whole wheel at which \c rnti has a CG opportunity.
/// \param cg_period_slots Period the wheel was built for, from which its size follows.
unsigned count_entries(const cg_slot_wheel& wheel, rnti_t rnti, unsigned cg_period_slots)
{
  unsigned count = 0;
  for (unsigned offset = 0; offset != cg_slot_wheel::ring_size_for(cg_period_slots); ++offset) {
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

  cg_slot_wheel wheel{period};
  ASSERT_EQ(0, wheel.add_ue(rnti1, offset, period));

  ASSERT_EQ(cg_slot_wheel::ring_size_for(period) / period, count_entries(wheel, rnti1, period));
  for (unsigned n = 0; n != cg_slot_wheel::ring_size_for(period); ++n) {
    const slot_point sl{to_numerology_value(subcarrier_spacing::kHz15), n};
    ASSERT_EQ(n % period == offset, wheel.contains(sl, rnti1)) << "unexpected wheel content at offset " << n;
  }
}

TEST(cg_slot_wheel_test, rem_ue_undoes_add_ue)
{
  constexpr unsigned offset = 7;
  constexpr unsigned period = 20;

  cg_slot_wheel wheel{period};
  ASSERT_EQ(0, wheel.add_ue(rnti1, offset, period));
  ASSERT_EQ(0, wheel.rem_ue(rnti1, offset, period));

  ASSERT_EQ(0, count_entries(wheel, rnti1, period));
}

TEST(cg_slot_wheel_test, adding_the_same_ue_twice_does_not_duplicate_its_grants)
{
  constexpr unsigned offset      = 5;
  constexpr unsigned period      = 40;
  constexpr unsigned nof_entries = cg_slot_wheel::ring_size_for(period) / period;

  cg_slot_wheel wheel{period};
  ASSERT_EQ(0, wheel.add_ue(rnti1, offset, period));

  // The second add reports every offset as a duplicate and leaves the wheel untouched, so a single removal suffices.
  ASSERT_EQ(nof_entries, wheel.add_ue(rnti1, offset, period));
  ASSERT_EQ(nof_entries, count_entries(wheel, rnti1, period));

  ASSERT_EQ(0, wheel.rem_ue(rnti1, offset, period));
  ASSERT_EQ(0, count_entries(wheel, rnti1, period));
}

TEST(cg_slot_wheel_test, removing_a_ue_that_was_never_added_reports_every_offset_as_missing)
{
  constexpr unsigned offset = 1;
  constexpr unsigned period = 80;

  cg_slot_wheel wheel{period};
  ASSERT_EQ(cg_slot_wheel::ring_size_for(period) / period, wheel.rem_ue(rnti1, offset, period));
}

TEST(cg_slot_wheel_test, removing_one_ue_preserves_the_grants_of_another)
{
  constexpr unsigned offset = 2;
  constexpr unsigned period = 10;

  // Both UEs share the same offset, so they sit in the same per-slot lists and a removal must not disturb the other.
  cg_slot_wheel wheel{period};
  ASSERT_EQ(0, wheel.add_ue(rnti1, offset, period));
  ASSERT_EQ(0, wheel.add_ue(rnti2, offset, period));

  ASSERT_EQ(0, wheel.rem_ue(rnti1, offset, period));

  ASSERT_EQ(0, count_entries(wheel, rnti1, period));
  ASSERT_EQ(cg_slot_wheel::ring_size_for(period) / period, count_entries(wheel, rnti2, period));
}

TEST(cg_slot_wheel_test, rem_ue_with_the_wrong_period_leaves_stale_grants_that_a_sweep_reclaims)
{
  constexpr unsigned offset     = 4;
  constexpr unsigned period     = 10;
  constexpr unsigned bad_period = 40;

  // The entry counts below are exact only under these conditions: the UE's first entry must fall before its second one
  // for the period to divide the wheel evenly, and bad_period must be a multiple of period that divides the wheel, so
  // that every offset rem_ue() visits does hold an entry and none of its steps falls off the end of the wheel.
  static_assert(offset < period, "offset must fall within the first period");
  static_assert(cg_slot_wheel::ring_size_for(period) % period == 0, "period must divide the wheel");
  static_assert(cg_slot_wheel::ring_size_for(period) % bad_period == 0, "bad_period must divide the wheel");
  static_assert(bad_period % period == 0, "bad_period must be a multiple of period");
  // Keep the wrong period from being exactly twice the right one, which would make the number of entries reached equal
  // the number left stale and let a test that confuses the two still pass.
  static_assert(bad_period != 2 * period, "bad_period must not make reached and stale counts coincide");

  constexpr unsigned nof_added   = cg_slot_wheel::ring_size_for(period) / period;
  constexpr unsigned nof_reached = cg_slot_wheel::ring_size_for(period) / bad_period;
  constexpr unsigned nof_stale   = nof_added - nof_reached;

  cg_slot_wheel wheel{period};
  ASSERT_EQ(0, wheel.add_ue(rnti1, offset, period));
  ASSERT_EQ(nof_added, count_entries(wheel, rnti1, period));

  // Removing with a period the UE was not added with steps over most of its entries. Every offset it does visit holds
  // an entry, so nothing is reported missing and the caller has no way to tell that the rest leaked.
  ASSERT_EQ(0, wheel.rem_ue(rnti1, offset, bad_period));
  ASSERT_EQ(nof_stale, count_entries(wheel, rnti1, period));

  // A sweep reclaims exactly those, without needing to know the offset or the period.
  ASSERT_EQ(nof_stale, wheel.rem_ue_everywhere(rnti1));
  ASSERT_EQ(0, count_entries(wheel, rnti1, period));
}

TEST(cg_slot_wheel_test, sweep_removes_all_the_grants_of_one_ue_only)
{
  constexpr unsigned period     = 10;
  constexpr unsigned period_ue2 = 40;

  // The wheel is built for one period, so both UEs' periods have to divide its size, or they would be written to
  // entries other than the slots their grants recur on.
  static_assert(cg_slot_wheel::ring_size_for(period) % period == 0, "period must divide the wheel");
  static_assert(cg_slot_wheel::ring_size_for(period) % period_ue2 == 0, "period_ue2 must divide the wheel");

  cg_slot_wheel wheel{period};
  ASSERT_EQ(0, wheel.add_ue(rnti1, 3, period));
  ASSERT_EQ(0, wheel.add_ue(rnti2, 8, period_ue2));

  ASSERT_EQ(cg_slot_wheel::ring_size_for(period) / period, wheel.rem_ue_everywhere(rnti1));

  ASSERT_EQ(0, count_entries(wheel, rnti1, period));
  ASSERT_EQ(cg_slot_wheel::ring_size_for(period) / period_ue2, count_entries(wheel, rnti2, period));

  // Sweeping a UE that is not in the wheel is a no-op.
  ASSERT_EQ(0, wheel.rem_ue_everywhere(rnti1));
}

TEST(cg_slot_wheel_test, clear_empties_the_whole_wheel)
{
  constexpr unsigned period = 10;

  cg_slot_wheel wheel{period};
  ASSERT_EQ(0, wheel.add_ue(rnti1, 0, period));
  ASSERT_EQ(0, wheel.add_ue(rnti2, 5, period));

  wheel.clear();

  ASSERT_EQ(0, count_entries(wheel, rnti1, period));
  ASSERT_EQ(0, count_entries(wheel, rnti2, period));
}

} // namespace
