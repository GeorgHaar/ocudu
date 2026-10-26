// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "lib/cu_cp/cu_cp_controller/amf_selector.h"
#include "tests/test_doubles/utils/test_rng.h"
#include <gtest/gtest.h>

using namespace ocudu;
using namespace ocucp;

namespace {

const plmn_identity plmn_a = plmn_identity::test_value();
const plmn_identity plmn_b = plmn_identity::parse("99902").value();

guami_t make_guami(uint16_t amf_set_id, uint8_t amf_pointer, plmn_identity plmn = plmn_a)
{
  return guami_t{.plmn = plmn, .amf_set_id = amf_set_id, .amf_pointer = amf_pointer, .amf_region_id = 2};
}

amf_candidate make_candidate(unsigned amf_idx, const guami_t& guami, uint16_t capacity = 255, bool connected = true)
{
  amf_candidate candidate;
  candidate.amf_index             = uint_to_cu_cp_amf_index(amf_idx);
  candidate.connected             = connected;
  candidate.plmns                 = {guami.plmn};
  candidate.served_guamis         = {ngap_served_guami_item{guami, std::nullopt, std::nullopt}};
  candidate.relative_amf_capacity = capacity;
  return candidate;
}

/// The AMF selection draws random numbers. A test of a rule that selects one AMF gives this AMF capacity 0 and the
/// other AMFs a higher capacity. Only the rule can then select this AMF. A failed test prints the seed of the test run.
class amf_selector_test : public ::testing::Test
{
protected:
  std::optional<cu_cp_amf_index_t> select(const amf_selection_request& request)
  {
    return select_amf(candidates, request, rng);
  }

  /// Counts how often each AMF is selected for a UE that identifies no AMF.
  std::vector<unsigned> count_selections(unsigned nof_selections)
  {
    std::vector<unsigned> counts(candidates.size(), 0);
    for (unsigned i = 0; i != nof_selections; ++i) {
      std::optional<cu_cp_amf_index_t> amf_index = select(amf_selection_request{.plmn = plmn_a});
      EXPECT_TRUE(amf_index.has_value());
      ++counts[cu_cp_amf_index_to_uint(amf_index.value())];
    }
    return counts;
  }

  std::vector<amf_candidate> candidates;
  std::mt19937&              rng = test_rng::tls_gen();
};

} // namespace

TEST_F(amf_selector_test, when_ue_has_registered_guami_then_amf_of_this_guami_is_selected)
{
  candidates = {make_candidate(0, make_guami(1, 0), 255), make_candidate(1, make_guami(1, 1), 0)};

  ASSERT_EQ(select({.plmn = plmn_a, .registered_guami = make_guami(1, 1)}), uint_to_cu_cp_amf_index(1));
}

TEST_F(amf_selector_test, when_ue_has_mapped_guami_then_only_a_mapped_served_guami_matches)
{
  candidates = {make_candidate(0, make_guami(1, 0), 255), make_candidate(1, make_guami(1, 0), 0)};
  candidates[1].served_guamis[0].guami_type = guami_type_t::mapped;

  ASSERT_EQ(
      select({.plmn = plmn_a, .registered_guami = make_guami(1, 0), .registered_guami_type = guami_type_t::mapped}),
      uint_to_cu_cp_amf_index(1));
}

TEST_F(amf_selector_test, when_ue_has_5g_s_tmsi_then_amf_of_its_set_and_pointer_is_selected)
{
  candidates = {make_candidate(0, make_guami(1, 0), 255), make_candidate(1, make_guami(1, 1), 0)};

  ASSERT_EQ(select({.plmn = plmn_a, .five_g_s_tmsi = five_g_s_tmsi_t{1, 1, 0x1234}}), uint_to_cu_cp_amf_index(1));
}

TEST_F(amf_selector_test, when_amf_of_the_ue_is_not_connected_then_an_amf_of_the_same_set_is_selected)
{
  candidates = {make_candidate(0, make_guami(2, 0), 255),
                make_candidate(1, make_guami(1, 0), 255, false),
                make_candidate(2, make_guami(1, 1), 0)};

  ASSERT_EQ(select({.plmn = plmn_a, .registered_guami = make_guami(1, 0)}), uint_to_cu_cp_amf_index(2));
  ASSERT_EQ(select({.plmn = plmn_a, .five_g_s_tmsi = five_g_s_tmsi_t{1, 0, 0x1234}}), uint_to_cu_cp_amf_index(2));
}

TEST_F(amf_selector_test, when_several_amfs_serve_the_registered_guami_then_selection_is_weighted_by_capacity)
{
  candidates = {make_candidate(0, make_guami(1, 0), 0), make_candidate(1, make_guami(1, 0), 255)};

  ASSERT_EQ(select({.plmn = plmn_a, .registered_guami = make_guami(1, 0)}), uint_to_cu_cp_amf_index(1));
}

TEST_F(amf_selector_test, when_amf_of_the_registered_guami_does_not_support_the_plmn_then_another_amf_is_selected)
{
  candidates          = {make_candidate(0, make_guami(1, 0)), make_candidate(1, make_guami(1, 1), 1)};
  candidates[0].plmns = {plmn_b};

  ASSERT_EQ(select({.plmn = plmn_a, .registered_guami = make_guami(1, 0)}), uint_to_cu_cp_amf_index(1));
  ASSERT_EQ(select({.plmn = plmn_a, .five_g_s_tmsi = five_g_s_tmsi_t{1, 0, 0x1234}}), uint_to_cu_cp_amf_index(1));
}

TEST_F(amf_selector_test, when_ue_identifies_an_amf_set_then_only_amfs_of_the_same_guami_type_are_selected)
{
  // AMF 0 serves the AMF Set with native GUAMIs, AMF 1 with mapped GUAMIs.
  candidates = {make_candidate(0, make_guami(1, 0), 255), make_candidate(1, make_guami(1, 1), 0)};
  candidates[1].served_guamis[0].guami_type = guami_type_t::mapped;

  ASSERT_EQ(
      select({.plmn = plmn_a, .registered_guami = make_guami(1, 5), .registered_guami_type = guami_type_t::mapped}),
      uint_to_cu_cp_amf_index(1));
}

TEST_F(amf_selector_test, when_ue_identifies_no_amf_then_selection_is_weighted_by_relative_amf_capacity)
{
  candidates = {make_candidate(0, make_guami(1, 0), 255), make_candidate(1, make_guami(1, 1), 85)};

  std::vector<unsigned> counts = count_selections(4000);

  // The expected ratio is 255:85, i.e. 3000 and 1000 selections.
  EXPECT_NEAR(counts[0], 3000, 150);
  EXPECT_NEAR(counts[1], 1000, 150);
}

TEST_F(amf_selector_test, when_amf_has_capacity_zero_then_it_is_not_selected)
{
  candidates = {make_candidate(0, make_guami(1, 0), 0), make_candidate(1, make_guami(1, 1), 1)};

  std::vector<unsigned> counts = count_selections(100);

  ASSERT_EQ(counts[0], 0);
  ASSERT_EQ(counts[1], 100);
}

TEST_F(amf_selector_test, when_all_amfs_have_capacity_zero_then_all_amfs_are_selected)
{
  candidates = {make_candidate(0, make_guami(1, 0), 0), make_candidate(1, make_guami(1, 1), 0)};

  std::vector<unsigned> counts = count_selections(100);

  ASSERT_GT(counts[0], 0);
  ASSERT_GT(counts[1], 0);
}

TEST_F(amf_selector_test, when_no_connected_amf_supports_the_plmn_then_no_amf_is_selected)
{
  candidates = {make_candidate(0, make_guami(1, 0, plmn_b)), make_candidate(1, make_guami(1, 1), 255, false)};

  ASSERT_FALSE(select({.plmn = plmn_a}).has_value());
}

TEST_F(amf_selector_test, when_guami_is_served_then_its_amf_is_found)
{
  candidates = {make_candidate(0, make_guami(1, 0)), make_candidate(1, make_guami(1, 1))};

  ASSERT_EQ(select_amf_for_guami(candidates, make_guami(1, 1)), uint_to_cu_cp_amf_index(1));
}

TEST_F(amf_selector_test, when_amf_of_guami_is_not_connected_then_an_amf_of_the_same_set_is_found)
{
  candidates = {make_candidate(0, make_guami(1, 0), 255, false), make_candidate(1, make_guami(1, 1))};

  ASSERT_EQ(select_amf_for_guami(candidates, make_guami(1, 0)), uint_to_cu_cp_amf_index(1));
}

TEST_F(amf_selector_test, when_no_amf_of_the_guami_set_is_connected_then_no_amf_is_found)
{
  candidates = {make_candidate(0, make_guami(1, 0), 255, false), make_candidate(1, make_guami(2, 0))};

  ASSERT_FALSE(select_amf_for_guami(candidates, make_guami(1, 0)).has_value());
}

TEST_F(amf_selector_test, when_amf_set_is_requested_then_a_connected_amf_of_this_set_is_selected)
{
  candidates = {make_candidate(0, make_guami(1, 0), 255),
                make_candidate(1, make_guami(2, 0), 255, false),
                make_candidate(2, make_guami(2, 1), 0)};

  ASSERT_EQ(select_amf_for_amf_set(candidates, plmn_a, 2, rng), uint_to_cu_cp_amf_index(2));
}

TEST_F(amf_selector_test, when_no_amf_of_the_requested_set_is_connected_then_no_amf_is_selected)
{
  candidates = {make_candidate(0, make_guami(1, 0)), make_candidate(1, make_guami(2, 0), 255, false)};

  ASSERT_FALSE(select_amf_for_amf_set(candidates, plmn_a, 2, rng).has_value());
}
