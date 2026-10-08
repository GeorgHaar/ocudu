// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "cu_cp_test_environment.h"
#include "tests/test_doubles/f1ap/f1ap_test_message_validators.h"
#include "tests/test_doubles/f1ap/f1ap_test_messages.h"
#include "tests/test_doubles/ngap/ngap_test_message_validators.h"
#include "tests/test_doubles/rrc/rrc_test_messages.h"
#include "tests/unittests/ngap/ngap_test_messages.h"
#include "ocudu/asn1/f1ap/f1ap_pdu_contents_ue.h"
#include "ocudu/asn1/ngap/ngap_pdu_contents.h"
#include "ocudu/f1ap/f1ap_message.h"
#include "ocudu/ngap/ngap_message.h"
#include <gtest/gtest.h>

using namespace ocudu;
using namespace ocucp;

/// Two AMFs of one AMF Set serve the PLMN of the CU-CP. AMF 0 serves the GUAMI with AMF Pointer 0, AMF 1 the GUAMI
/// with AMF Pointer 1.
class cu_cp_amf_pooling_test : public cu_cp_test_environment, public ::testing::Test
{
public:
  cu_cp_amf_pooling_test() :
    cu_cp_test_environment(
        cu_cp_test_env_params{8, 8, 8192, 8, {{default_supported_tracking_area}, {default_supported_tracking_area}}})
  {
  }

  /// Starts the CU-CP with the given relative AMF capacities, and sets up a DU and a CU-UP.
  void start_cu_cp(uint16_t amf_0_capacity, uint16_t amf_1_capacity)
  {
    start_cu_cp(generate_ng_setup_response(plmn_identity::test_value(), 0, amf_0_capacity),
                generate_ng_setup_response(plmn_identity::test_value(), 1, amf_1_capacity));
  }

  /// Starts the CU-CP with the given NG Setup Responses, and sets up a DU and a CU-UP.
  void start_cu_cp(const ngap_message& amf_0_response, const ngap_message& amf_1_response)
  {
    get_amf(0).enqueue_next_tx_pdu(amf_0_response);
    get_amf(1).enqueue_next_tx_pdu(amf_1_response);
    get_cu_cp().start();
    ngap_message ngap_pdu;
    report_fatal_error_if_not(get_amf(0).try_pop_rx_pdu(ngap_pdu), "AMF 0 received no NG Setup Request");
    report_fatal_error_if_not(get_amf(1).try_pop_rx_pdu(ngap_pdu), "AMF 1 received no NG Setup Request");

    std::optional<unsigned> ret = connect_new_du();
    report_fatal_error_if_not(ret.has_value(), "Failed to connect the DU");
    du_idx = ret.value();
    report_fatal_error_if_not(run_f1_setup(du_idx), "F1 Setup failed");

    ret = connect_new_cu_up();
    report_fatal_error_if_not(ret.has_value(), "Failed to connect the CU-UP");
    report_fatal_error_if_not(run_e1_setup(ret.value()), "E1 Setup failed");
  }

  /// Runs the RRC setup of a UE and returns the AMF that receives its Initial UE Message.
  std::optional<unsigned> attach_ue(gnb_du_ue_f1ap_id_t du_ue_id, const asn1::rrc_nr::ul_dcch_msg_s& rrc_setup_complete)
  {
    get_du(du_idx).push_ul_pdu(
        test_helpers::generate_init_ul_rrc_message_transfer(du_ue_id, to_rnti(0x4601 + fmt::underlying(du_ue_id))));
    f1ap_message f1ap_pdu;
    report_fatal_error_if_not(wait_for_f1ap_tx_pdu(du_idx, f1ap_pdu), "No RRC Setup was sent");
    report_fatal_error_if_not(test_helpers::is_valid_dl_rrc_message_transfer_with_msg4(f1ap_pdu), "Invalid Msg4");

    get_du(du_idx).push_rrc_ul_dcch_message(
        du_ue_id, srb_id_t::srb1, test_helpers::pack_ul_dcch_msg(rrc_setup_complete));

    for (unsigned attempt = 0; attempt != 10; ++attempt) {
      for (unsigned amf_idx = 0; amf_idx != 2; ++amf_idx) {
        ngap_message ngap_pdu;
        if (wait_for_ngap_tx_pdu(ngap_pdu, std::chrono::milliseconds{50}, amf_idx)) {
          report_fatal_error_if_not(test_helpers::is_valid_init_ue_message(ngap_pdu), "Invalid Initial UE Message");
          return amf_idx;
        }
      }
    }
    return std::nullopt;
  }

  /// Runs the RRC setup of a UE that sends the given 5G-S-TMSI, and returns the AMF of its Initial UE Message.
  std::optional<unsigned> attach_ue_with_5g_s_tmsi(gnb_du_ue_f1ap_id_t du_ue_id, uint8_t amf_pointer)
  {
    // 5G-S-TMSI with AMF Set ID 1 (TS 23.003 section 2.11).
    uint64_t five_g_s_tmsi = (uint64_t{1} << 38U) + (uint64_t{amf_pointer} << 32U) + 0x1234;
    return attach_ue(du_ue_id, test_helpers::create_rrc_setup_complete(1, five_g_s_tmsi));
  }

  unsigned du_idx = 0;
};

TEST_F(cu_cp_amf_pooling_test, when_amf_has_capacity_zero_then_new_ues_are_served_by_the_other_amf)
{
  start_cu_cp(0, 255);

  for (unsigned i = 0; i != 5; ++i) {
    ASSERT_EQ(attach_ue(int_to_gnb_du_ue_f1ap_id(i), test_helpers::create_rrc_setup_complete()), 1);
  }
}

TEST_F(cu_cp_amf_pooling_test, when_amf_does_not_support_the_plmn_then_it_serves_no_ues)
{
  // AMF 1 supports only another PLMN. Without its PLMN Support List, the UEs would go to AMF 1.
  ngap_message amf_1_response = generate_ng_setup_response(plmn_identity::test_value(), 1, 255);
  amf_1_response.pdu.successful_outcome().value.ng_setup_resp()->plmn_support_list[0].plmn_id =
      plmn_identity::parse("99902").value().to_bytes();
  start_cu_cp(generate_ng_setup_response(plmn_identity::test_value(), 0, 0), amf_1_response);

  for (unsigned i = 0; i != 5; ++i) {
    ASSERT_EQ(attach_ue(int_to_gnb_du_ue_f1ap_id(i), test_helpers::create_rrc_setup_complete()), 0);
  }
}

TEST_F(cu_cp_amf_pooling_test, when_du_is_lost_then_each_amf_gets_an_ng_reset_for_its_own_ues)
{
  start_cu_cp(255, 255);
  ASSERT_EQ(attach_ue_with_5g_s_tmsi(int_to_gnb_du_ue_f1ap_id(0), 0), 0);
  ASSERT_EQ(attach_ue_with_5g_s_tmsi(int_to_gnb_du_ue_f1ap_id(1), 1), 1);

  ASSERT_TRUE(drop_du_connection(du_idx));

  for (unsigned amf_idx = 0; amf_idx != 2; ++amf_idx) {
    ngap_message ngap_pdu;
    ASSERT_TRUE(wait_for_ngap_tx_pdu(ngap_pdu, std::chrono::milliseconds{1000}, amf_idx))
        << "AMF " << amf_idx << " received no NG Reset";
    ASSERT_TRUE(test_helpers::is_valid_ng_reset(ngap_pdu));
    const auto& reset_type = ngap_pdu.pdu.init_msg().value.ng_reset()->reset_type;
    ASSERT_EQ(reset_type.type().value, asn1::ngap::reset_type_c::types_opts::part_of_ng_interface);
    ASSERT_EQ(reset_type.part_of_ng_interface().size(), 1) << "AMF " << amf_idx << " got the NG Reset of another AMF";
  }
}

TEST_F(cu_cp_amf_pooling_test, when_ue_sends_5g_s_tmsi_then_it_is_served_by_the_amf_of_its_amf_pointer)
{
  // Without the 5G-S-TMSI, the UE would go to AMF 0.
  start_cu_cp(255, 0);

  ASSERT_EQ(attach_ue_with_5g_s_tmsi(int_to_gnb_du_ue_f1ap_id(0), 1), 1);
}

TEST_F(cu_cp_amf_pooling_test, when_ue_sends_registered_amf_then_it_is_served_by_this_amf)
{
  // Without the registered AMF, the UE would go to AMF 0.
  start_cu_cp(255, 0);

  // Registered AMF with AMF Region ID 2, AMF Set ID 1 and AMF Pointer 1.
  asn1::rrc_nr::ul_dcch_msg_s rrc_setup_complete = test_helpers::create_rrc_setup_complete();
  auto&                       ies = rrc_setup_complete.msg.c1().rrc_setup_complete().crit_exts.rrc_setup_complete();
  ies.registered_amf_present      = true;
  ies.registered_amf.amf_id.from_number(0x020041);

  ASSERT_EQ(attach_ue(int_to_gnb_du_ue_f1ap_id(0), rrc_setup_complete), 1);
}

TEST_F(cu_cp_amf_pooling_test, when_one_amf_is_lost_then_only_its_ues_are_released_and_the_plmn_stays_served)
{
  start_cu_cp(255, 255);
  ASSERT_EQ(attach_ue_with_5g_s_tmsi(int_to_gnb_du_ue_f1ap_id(0), 0), 0);
  ASSERT_EQ(attach_ue_with_5g_s_tmsi(int_to_gnb_du_ue_f1ap_id(1), 1), 1);

  ASSERT_TRUE(drop_amf_connection(0));

  // The UE that AMF 0 serves is released.
  f1ap_message f1ap_pdu;
  ASSERT_TRUE(wait_for_f1ap_tx_pdu(du_idx, f1ap_pdu));
  ASSERT_TRUE(test_helpers::is_valid_ue_context_release_command(f1ap_pdu));
  ASSERT_EQ(int_to_gnb_du_ue_f1ap_id(f1ap_pdu.pdu.init_msg().value.ue_context_release_cmd()->gnb_du_ue_f1ap_id),
            int_to_gnb_du_ue_f1ap_id(0));
  get_du(du_idx).push_ul_pdu(test_helpers::generate_ue_context_release_complete(f1ap_pdu));

  // AMF 1 still serves the PLMN, so the cell stays active and the UE that AMF 1 serves stays connected.
  ASSERT_FALSE(wait_for_f1ap_tx_pdu(du_idx, f1ap_pdu, std::chrono::milliseconds{100}));

  // A new UE is served by AMF 1.
  ASSERT_EQ(attach_ue(int_to_gnb_du_ue_f1ap_id(2), test_helpers::create_rrc_setup_complete()), 1);
}
