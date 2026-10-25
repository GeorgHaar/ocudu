// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

/*******************************************************************************
 *
 *                      3GPP TS ASN1 E2SM v07.00 (2025-10)
 *
 ******************************************************************************/

#pragma once

#include "ocudu/asn1/asn1_ap_utils.h"

namespace asn1 {
namespace e2sm {

/*******************************************************************************
 *                             Constant Definitions
 ******************************************************************************/

#define ASN1_E2SM_MAX_E1AP_ID 65535
#define ASN1_E2SM_MAX_F1AP_ID 4
#define ASN1_E2SM_MAX_EARFCN 65535
#define ASN1_E2SM_MAX_NR_ARFCN 3279165
#define ASN1_E2SM_MAXNOOF_NR_CELL_BANDS 32
#define ASN1_E2SM_MAX_NROF_SS_BS_1 63
#define ASN1_E2SM_MAXNOOF_CELLS 16384
#define ASN1_E2SM_MAXNOOF_RIC_STYLES 63
#define ASN1_E2SM_MAXNOOF_MEAS_INFO 65535
#define ASN1_E2SM_MAXNOOF_LABEL_INFO 2147483647
#define ASN1_E2SM_MAXNOOF_MEAS_RECORD 65535
#define ASN1_E2SM_MAXNOOF_MEAS_VALUE 2147483647
#define ASN1_E2SM_MAXNOOF_CONDITION_INFO 32768
#define ASN1_E2SM_MAXNOOF_UE_ID 65535
#define ASN1_E2SM_MAXNOOF_CONDITION_INFO_PER_SUB 32768
#define ASN1_E2SM_MAXNOOF_UE_ID_PER_SUB 65535
#define ASN1_E2SM_MAXNOOF_UE_MEAS_REPORT 65535
#define ASN1_E2SM_MAXNOOF_BIN 65535
#define ASN1_E2SM_MAXNOOF_JOBS 65535
#define ASN1_E2SM_MAXNOOF_EVENT_PARAMS 65535
#define ASN1_E2SM_MAXNOOF_EVENT_TRIGGER 65535
#define ASN1_E2SM_MAXNOOF_MSGS 65535
#define ASN1_E2SM_MAXNOOF_E2_INFO_CHANGES 65535
#define ASN1_E2SM_MAXNOOF_UE_INFO_CHANGES 65535
#define ASN1_E2SM_MAXNOOF_RRC_STATE 8
#define ASN1_E2SM_MAXNOOF_PARAMS_TO_REPORT 65535
#define ASN1_E2SM_MAXNOOF_POLICY_CONDITIONS 65535
#define ASN1_E2SM_MAXNOOF_ASSOCIATED_RAN_PARAMS 65535
#define ASN1_E2SM_MAXNOOF_UE_ID 65535
#define ASN1_E2SM_MAXNOOF_CELL_ID 65535
#define ASN1_E2SM_MAXNOOF_RAN_OUTCOME_PARAMS 255
#define ASN1_E2SM_MAXNOOF_PARAMSIN_STRUCTURE 65535
#define ASN1_E2SM_MAXNOOF_ITEMSIN_LIST 65535
#define ASN1_E2SM_MAXNOOF_UE_INFO 65535
#define ASN1_E2SM_MAXNOOF_CELL_INFO 65535
#define ASN1_E2SM_MAXNOOF_UE_EVENT_INFO 65535
#define ASN1_E2SM_MAXNOOF_RA_NPARAM_TEST 255
#define ASN1_E2SM_MAXNOOF_NEIGHBOUR_CELL 65535
#define ASN1_E2SM_MAXNOOF_RIC_STYLES 63
#define ASN1_E2SM_MAXNOOF_CALL_PROCESS_TYPES 65535
#define ASN1_E2SM_MAXNOOF_CALL_PROCESS_BREAKPOINTS 65535
#define ASN1_E2SM_MAXNOOF_INSERT_IND 65535
#define ASN1_E2SM_MAXNOOF_CTRL_ACTION 65535
#define ASN1_E2SM_MAXNOOF_POLICY_ACTION 65535
#define ASN1_E2SM_MAXNOOF_INSERT_IND_ACTIONS 63
#define ASN1_E2SM_MAXNOOF_MUL_CTRL_ACTIONS 63
#define ASN1_E2SM_MAX_GROUP_DEFINITION_ID_PARAMS 255
#define ASN1_E2SM_MAXNOOF_ASSOCIATED_ENTITY_FILTS 255
#define ASN1_E2SM_MAXNOOF_FORMAT_TYPES 63

/*******************************************************************************
 *                              Struct Definitions
 ******************************************************************************/

} // namespace e2sm
} // namespace asn1
