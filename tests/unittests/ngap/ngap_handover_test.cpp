// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "ngap_test_helpers.h"
#include "tests/ocudu_test_requirements.h"
#include "tests/unittests/ngap/ngap_test_messages.h"
#include "ocudu/adt/format.h"
#include "ocudu/ngap/ngap_handover.h"
#include "ocudu/ran/cu_types.h"
#include "ocudu/ran/rb_id.h"
#include "ocudu/support/async/async_test_utils.h"
#include <gtest/gtest.h>

using namespace ocudu;
using namespace ocucp;

/// Test successful handover preparation procedure
TEST_F(ngap_test, when_ue_missing_then_handover_preparation_procedure_fails)
{
  OCUDU_TEST_REQUIREMENTS("MVP-FUNC-MOB-4");

  ngap_handover_preparation_request request = {};
  request.ue_index                          = uint_to_ue_index(0);

  // Action 1: Launch HO preparation procedure
  test_logger.info("Launch source NGAP handover preparation procedure");
  async_task<ngap_handover_preparation_response>         t = ngap->handle_handover_preparation_request(request);
  lazy_task_launcher<ngap_handover_preparation_response> t_launcher(t);

  // Status: should have failed already, as there is no UE.
  ASSERT_TRUE(t.ready());

  // Procedure should have failed.
  ASSERT_FALSE(t.get().success);

  // Make sure no NGAP pdu was sent
  ASSERT_TRUE(n2_gw.last_ngap_msgs.empty());
}

/// Handover Request with a valid target cell ID in the transparent container requests a UE for the target cell.
TEST_F(ngap_test, when_handover_request_received_then_ue_index_allocation_is_requested_for_target_cell)
{
  OCUDU_TEST_REQUIREMENTS("MVP-FUNC-MOB-4");

  ngap->handle_message(generate_valid_handover_request(amf_ue_id_t(1)));

  ASSERT_TRUE(cu_cp_notifier.last_ue_index_allocation_cgi.has_value());
  ASSERT_EQ(cu_cp_notifier.last_ue_index_allocation_cgi->plmn_id, plmn_identity::test_value());
}

/// Handover Request with an invalid PLMN in the target cell ID of the transparent container is rejected with a Handover
/// Failure before a UE is requested.
TEST_F(ngap_test, when_handover_request_with_invalid_target_cell_plmn_received_then_handover_failure_is_sent)
{
  OCUDU_TEST_REQUIREMENTS("MVP-FUNC-MOB-4");

  handover_request_params params;
  params.invalid_target_cell_plmn_id = true;
  ngap->handle_message(generate_valid_handover_request(amf_ue_id_t(1), params));

  ASSERT_FALSE(cu_cp_notifier.last_ue_index_allocation_cgi.has_value());
  ASSERT_FALSE(n2_gw.last_ngap_msgs.empty());
  const auto& pdu = n2_gw.last_ngap_msgs.back().pdu;
  ASSERT_EQ(pdu.type(), asn1::ngap::ngap_pdu_c::types_opts::unsuccessful_outcome);
  ASSERT_EQ(pdu.unsuccessful_outcome().value.type(),
            asn1::ngap::ngap_elem_procs_o::unsuccessful_outcome_c::types_opts::ho_fail);
}
