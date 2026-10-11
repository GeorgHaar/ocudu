// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

/// \file Tests for the parts of the conversion layer the per-type roundtrip tests cannot cover.
///
/// Every individually serialized type has its own field-exhaustive test under roundtrip/. What is left here is only
/// what that harness structurally cannot check:
///  - Aggregate list wiring: the harness calls each type's own converter directly, never convert_decision_to_fb /
///    convert_fb_to_decision, so it would not notice a list encoded from -- or decoded into -- the wrong sched_result
///    member. Hence make_full_sched_result, which populates every list at once.
///  - The few derived encodings a roundtrip is blind to, because both directions convert rather than copy and so could
///    be wrong the same way. See derived_wire_encodings_are_correct.
///  - convert_cell_cfg_to_fb's ocudu::cell_configuration overload, which has no inverse -- the wire only reconstructs
///    a schedtrace::cell_configuration -- and so cannot be roundtripped at all.

#include "lib/scheduler/trace/event_converter.h"
#include "roundtrip/full_sched_result.h"
#include "tests/test_doubles/scheduler/trace/test_messages.h"
#include "tests/unittests/scheduler/test_utils/config_generators.h"
#include "ocudu/scheduler/result/sched_result.h"
#include <gtest/gtest.h>

using namespace ocudu;
using namespace schedtrace;
using namespace schedtrace::roundtrip_test;
using namespace test_helper;

/// Builds a finished buffer with the given decision as root.
static std::vector<uint8_t> decision_to_bytes(const sched_result& result)
{
  flatbuffers::FlatBufferBuilder fbb;
  fbb.Finish(convert_decision_to_fb(fbb, result));
  return {fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize()};
}

/// Converts \p input to a flatbuffer, back to sched_result, then to a flatbuffer again, and checks both buffers are
/// equal. The builder output is deterministic, so byte equality means field equality.
static void check_decision_roundtrip(const sched_result& input)
{
  const std::vector<uint8_t> bytes1 = decision_to_bytes(input);

  sched_result intermediate;
  convert_fb_to_decision(
      intermediate, *flatbuffers::GetRoot<fbs::SlotDecision>(bytes1.data()), make_test_schedtrace_cell_cfg());
  // Check the lists first: byte equality catches a list decoded into the wrong member too, but only as an opaque
  // buffer diff.
  ASSERT_EQ(intermediate.ul.puschs.size(), input.ul.puschs.size());
  ASSERT_EQ(intermediate.ul.pucchs.size(), input.ul.pucchs.size());
  ASSERT_EQ(intermediate.ul.prachs.size(), input.ul.prachs.size());
  ASSERT_EQ(intermediate.ul.srss.size(), input.ul.srss.size());
  ASSERT_EQ(intermediate.dl.dl_pdcchs.size(), input.dl.dl_pdcchs.size());
  ASSERT_EQ(intermediate.dl.ul_pdcchs.size(), input.dl.ul_pdcchs.size());
  ASSERT_EQ(intermediate.dl.bc.sibs.size(), input.dl.bc.sibs.size());
  ASSERT_EQ(intermediate.dl.bc.ssb_info.size(), input.dl.bc.ssb_info.size());
  ASSERT_EQ(intermediate.dl.rar_grants.size(), input.dl.rar_grants.size());
  ASSERT_EQ(intermediate.dl.paging_grants.size(), input.dl.paging_grants.size());
  ASSERT_EQ(intermediate.dl.ue_grants.size(), input.dl.ue_grants.size());
  ASSERT_EQ(intermediate.dl.csi_rs.size(), input.dl.csi_rs.size());

  ASSERT_EQ(bytes1, decision_to_bytes(intermediate));
}

// --- Tests -------------------------------------------------------------------

TEST(event_converter_test, empty_sched_result_roundtrip)
{
  sched_result result;
  result.success = true;
  check_decision_roundtrip(result);
}

TEST(event_converter_test, all_lists_roundtrip)
{
  check_decision_roundtrip(make_full_sched_result());
}

// make_full_sched_result is where all_lists_roundtrip -- and the schedlog log comparison in trace_to_log_test -- get
// their coverage, so a list quietly dropped from it must fail here rather than silently shrink both tests.
TEST(event_converter_test, full_sched_result_populates_every_serialized_list)
{
  const sched_result result = make_full_sched_result();

  EXPECT_FALSE(result.ul.puschs.empty());
  EXPECT_FALSE(result.ul.pucchs.empty());
  EXPECT_FALSE(result.ul.prachs.empty());
  EXPECT_FALSE(result.ul.srss.empty());
  EXPECT_FALSE(result.dl.dl_pdcchs.empty());
  EXPECT_FALSE(result.dl.ul_pdcchs.empty());
  EXPECT_FALSE(result.dl.bc.sibs.empty());
  EXPECT_FALSE(result.dl.bc.ssb_info.empty());
  EXPECT_FALSE(result.dl.rar_grants.empty());
  EXPECT_FALSE(result.dl.paging_grants.empty());
  EXPECT_FALSE(result.dl.ue_grants.empty());
  EXPECT_FALSE(result.dl.csi_rs.empty());
}

TEST(event_converter_test, intervals_are_encoded_as_start_and_length)
{
  sched_result result;
  result.success = true;
  // Intervals whose start, length and stop are all distinct, so each can only match one reading. This is why the
  // roundtrip test values cannot be reused here: they sweep each field to its corner values, and at a corner the three
  // readings collapse into each other (crb_interval{0, 275} has length == stop).
  ssb_information ssb{};
  ssb.ssb_index = 3;
  ssb.crbs      = crb_interval{4, 24};
  ssb.symbols   = ofdm_symbol_range{2, 6};
  result.dl.bc.ssb_info.push_back(ssb);

  flatbuffers::FlatBufferBuilder fbb;
  fbb.Finish(convert_decision_to_fb(fbb, result));
  const auto& decision = *flatbuffers::GetRoot<fbs::SlotDecision>(fbb.GetBufferPointer());

  // Every interval on the wire is (start, length), never (start, stop) -- both converters would have to agree on the
  // same misreading for a roundtrip to notice, and an external reader would then disagree with the schema. Checked
  // once here, on the one struct carrying both a frequency and a symbol interval.
  ASSERT_NE(decision.ssbs(), nullptr);
  const fbs::Ssb& wire_ssb = *decision.ssbs()->Get(0);
  EXPECT_EQ(wire_ssb.crbs().start(), 4U);
  EXPECT_EQ(wire_ssb.crbs().length(), 20U);
  EXPECT_EQ(wire_ssb.sym_start(), 2U);
  EXPECT_EQ(wire_ssb.sym_length(), 4U);
}

TEST(event_converter_test, cell_cfg_pucch_resources_roundtrip)
{
  test_helpers::test_sched_config_manager cfg_mng{cell_config_builder_params{}};
  const ocudu::cell_configuration&        cell_cfg = *cfg_mng.add_cell(cfg_mng.get_default_cell_config_request());
  const cell_pucch_res_config&            expected = cell_cfg.bwp_res[to_bwp_id(0)].ul().pucch;
  ASSERT_GT(expected.nof_total_res(), 0U);

  flatbuffers::FlatBufferBuilder fbb;
  fbb.Finish(convert_cell_cfg_to_fb(fbb, cell_cfg));
  const auto& start_event = *flatbuffers::GetRoot<fbs::CellStartEvent>(fbb.GetBufferPointer());

  schedtrace::cell_configuration schedtrace_cfg;
  convert_fb_to_cell_cfg(schedtrace_cfg, start_event);

  // What is checked is the gathering: every resource is present, common ones first, then the dedicated ones. Matching
  // starting_prb per index is enough to pin that order -- the resources' own serialization is covered by
  // roundtrip_test.pucch_resource.
  ASSERT_EQ(schedtrace_cfg.init_ul_bwp.pucch_resources.size(), expected.nof_total_res());
  for (unsigned i = 0; i != expected.common.size(); ++i) {
    EXPECT_EQ(schedtrace_cfg.init_ul_bwp.pucch_resources[i].starting_prb, expected.common[i].starting_prb);
  }
  for (unsigned i = 0; i != expected.dedicated.size(); ++i) {
    const pucch_resource& ded = schedtrace_cfg.init_ul_bwp.pucch_resources[expected.common.size() + i];
    EXPECT_EQ(ded.starting_prb, expected.dedicated[i].starting_prb);
    // Dedicated resources reached the wire with their symbols intact, not default-constructed.
    EXPECT_FALSE(ded.syms.empty());
  }
}

TEST(event_converter_test, cell_cfg_pdcch_and_prach_roundtrip)
{
  test_helpers::test_sched_config_manager  cfg_mng{cell_config_builder_params{}};
  sched_cell_configuration_request_message req = cfg_mng.get_default_cell_config_request();
  ASSERT_TRUE(req.ran.init_bwp.pdcch_cfg.has_value());
  ASSERT_FALSE(req.ran.init_bwp.pdcch_cfg->coresets.empty());
  coreset_configuration&                                ded_cs = req.ran.init_bwp.pdcch_cfg->coresets.front();
  const coreset_configuration::interleaved_mapping_type interleaving{6, 2, 5};
  ded_cs.set_non_coreset0_interleaved(interleaving);
  const ocudu::cell_configuration& cell_cfg = *cfg_mng.add_cell(req);

  flatbuffers::FlatBufferBuilder fbb;
  fbb.Finish(convert_cell_cfg_to_fb(fbb, cell_cfg));
  schedtrace::cell_configuration schedtrace_cfg;
  convert_fb_to_cell_cfg(schedtrace_cfg, *flatbuffers::GetRoot<fbs::CellStartEvent>(fbb.GetBufferPointer()));

  const auto find_cs = [&schedtrace_cfg](coreset_id id) -> const cell_coreset* {
    for (const cell_coreset& cs : schedtrace_cfg.init_dl_bwp.coresets) {
      if (cs.id == id) {
        return &cs;
      }
    }
    return nullptr;
  };

  // CORESET#0, with its CRBs and the TS 38.211 interleaving resolved.
  const pdcch_config_common& pdcch_common = cell_cfg.params.dl_cfg_common.init_dl_bwp.pdcch_common;
  const cell_coreset*        cs0          = find_cs(to_coreset_id(0));
  ASSERT_NE(cs0, nullptr);
  ASSERT_EQ(cs0->crbs.size(), 1U);
  EXPECT_EQ(cs0->crbs[0], pdcch_common.coreset0->coreset0_crbs());
  EXPECT_EQ(cs0->duration, pdcch_common.coreset0->duration());
  EXPECT_EQ(cs0->interleaving, pdcch_common.coreset0->interleaved_mapping());

  // UE-dedicated CORESET, with the frequencyDomainResources bitmap resolved to CRBs.
  const cell_coreset* cs = find_cs(ded_cs.get_id());
  ASSERT_NE(cs, nullptr);
  ASSERT_FALSE(cs->crbs.empty());
  unsigned nof_crbs = 0;
  for (const crb_interval& crbs : cs->crbs) {
    nof_crbs += crbs.length();
  }
  EXPECT_EQ(cs->crbs.front().start(), cell_cfg.init_bwp.dl.cfg().crbs.start() + ded_cs.get_coreset_start_crb());
  EXPECT_EQ(nof_crbs, ded_cs.freq_domain_resources().count() * pdcch_constants::NOF_RB_PER_FREQ_RESOURCE);
  EXPECT_EQ(cs->duration, ded_cs.duration());
  EXPECT_EQ(cs->interleaving, interleaving);

  // The CORESETs rebuilt for the PDCCHs match the configured UE-dedicated CORESET.
  const std::vector<coreset_configuration> cs_cfgs = make_coreset_cfgs(schedtrace_cfg.init_dl_bwp);
  EXPECT_NE(std::find(cs_cfgs.begin(), cs_cfgs.end(), ded_cs), cs_cfgs.end());

  // Every common and UE-dedicated search space, once.
  std::vector<const search_space_configuration*> expected_ss;
  for (const search_space_configuration& ss : pdcch_common.search_spaces) {
    expected_ss.push_back(&ss);
  }
  for (const search_space_configuration& ss : req.ran.init_bwp.pdcch_cfg->search_spaces) {
    expected_ss.push_back(&ss);
  }
  ASSERT_EQ(schedtrace_cfg.init_dl_bwp.search_spaces.size(), expected_ss.size());
  for (unsigned i = 0; i != expected_ss.size(); ++i) {
    const cell_search_space& ss = schedtrace_cfg.init_dl_bwp.search_spaces[i];
    EXPECT_EQ(ss.id, expected_ss[i]->get_id());
    EXPECT_EQ(ss.cs_id, expected_ss[i]->get_coreset_id());
    EXPECT_EQ(ss.is_common, expected_ss[i]->is_common_search_space());
    const unsigned nof_entries = expected_ss[i]->is_search_space0() ? MAX_NOF_SS0_SSB_CANDIDATES : 1;
    ASSERT_EQ(ss.monitoring_symbols.size(), nof_entries);
    for (unsigned ssb_idx = 0; ssb_idx != nof_entries; ++ssb_idx) {
      EXPECT_EQ(ss.monitoring_symbols[ssb_idx], expected_ss[i]->get_monitoring_symbols_within_slot(ssb_idx));
    }
  }

  const auto& rach_cfg = *cell_cfg.params.ul_cfg_common.init_ul_bwp.rach_cfg_common;
  ASSERT_TRUE(schedtrace_cfg.init_ul_bwp.prach.has_value());
  EXPECT_EQ(schedtrace_cfg.init_ul_bwp.prach->config_index, rach_cfg.rach_cfg_generic.prach_config_index);
  EXPECT_EQ(schedtrace_cfg.init_ul_bwp.prach->frequency_start, rach_cfg.rach_cfg_generic.msg1_frequency_start);
  EXPECT_EQ(schedtrace_cfg.init_ul_bwp.prach->scs, rach_cfg.msg1_scs);
  EXPECT_EQ(schedtrace_cfg.init_dl_bwp.pdsch_vrb_to_prb, cell_cfg.params.init_bwp.pdsch.interleaving_bundle_size);
}

TEST(event_converter_test, cell_cfg_prach_scs_derived_from_config_index_is_absent)
{
  schedtrace::cell_configuration cfg = make_test_schedtrace_cell_cfg();
  cfg.init_ul_bwp.prach              = cell_prach_config{0, 3, subcarrier_spacing::invalid};

  flatbuffers::FlatBufferBuilder fbb;
  fbb.Finish(convert_cell_cfg_to_fb(fbb, cfg));
  const auto& start_event = *flatbuffers::GetRoot<fbs::CellStartEvent>(fbb.GetBufferPointer());
  ASSERT_NE(start_event.init_ul_bwp(), nullptr);
  ASSERT_NE(start_event.init_ul_bwp()->prach(), nullptr);
  EXPECT_FALSE(start_event.init_ul_bwp()->prach()->scs().has_value());

  schedtrace::cell_configuration decoded;
  convert_fb_to_cell_cfg(decoded, start_event);
  ASSERT_TRUE(decoded.init_ul_bwp.prach.has_value());
  EXPECT_EQ(decoded.init_ul_bwp.prach->scs, subcarrier_spacing::invalid);
}

TEST(event_converter_test, pdcch_on_untraced_coreset_resolves_to_stub_coreset)
{
  pdcch_dl_information pdcch{};
  const fbs::DlPdcch   fb_pdcch{
      0x4601, static_cast<uint8_t>(dci_dl_rnti_config_type::c_rnti_f1_1), 2, 2, 0, 0, 0, false, 0, 0, 0};
  convert_fb_to_dl_pdcch(pdcch, fb_pdcch, make_coreset_cfgs(make_test_schedtrace_cell_cfg().init_dl_bwp));
  EXPECT_EQ(pdcch.ctx.coreset_cfg, &get_stub_coreset_cfg(2));
}
