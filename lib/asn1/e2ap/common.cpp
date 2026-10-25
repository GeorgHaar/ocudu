// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "ocudu/asn1/e2ap/common.h"

using namespace asn1;
using namespace asn1::e2ap;

/*******************************************************************************
 *                                Struct Methods
 ******************************************************************************/

// TriggeringMessage ::= ENUMERATED
const char* trigger_msg_opts::to_string() const
{
  static const char* names[] = {"initiating-message", "successful-outcome", "unsuccessfull-outcome"};
  return convert_enum_idx(names, 3, value, "trigger_msg_e");
}
