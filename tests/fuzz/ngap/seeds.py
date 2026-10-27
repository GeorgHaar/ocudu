# SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
# SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
# SPDX-License-Identifier: BSD-3-Clause-Open-MPI
# Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

"""
Seed corpus of the NGAP fuzz harnesses, from the known-good
(and known-bad) test vectors in the unit tests.

Written to disk by tests/fuzz/gen_corpus.py, see CORPORA at the end of this file.
"""

# ---------------------------------------------------------------------------
# NGAP corpus seeds
# ---------------------------------------------------------------------------
# Taken from tests/unittests/ngap/ngap_test_messages.h and
# tests/unittests/ngap/ngap_asn1_packer_test.cpp
# ---------------------------------------------------------------------------
NGAP_SEEDS = {
    # NGSetupRequest (initiatingMessage / id-NGSetup).
    # Source: ng_setup_request_packed[] in ngap_test_messages.h
    # Contains: GlobalRANNodeID, RANNodeName, SupportedTAList, DefaultPagingDRX.
    "ng_setup_request": bytes.fromhex(
        "00150033000004001b00080000f1100000066c0052400a03807473"
        "74676e6230310066000d00000000070000f110000000080015400160"
    ),

    # InitialContextSetupRequest (initiatingMessage / id-InitialContextSetup).
    # Source: ngap_init_ctx_req in ngap_asn1_packer_test.cpp
    # Contains: security key, UE security capabilities, NAS PDU.
    "initial_context_setup_request": bytes.fromhex(
        "000e008090000008000a0002000c005500020000001c00070000f11002004000000002000100"
        "77000918000c000000000000005e002050636e38151d62356d9a1a0c9f2391885177307ad4"
        "94be15281dfe5fdac06302002240080123456700ffff010026402f2e7e02cf5b405e017e00"
        "42010177000bf200f110020040dd00b06454072000f11000000715020101210201005e0129"
    ),

    # PDU header only (triggers early-exit / truncated-input path).
    "truncated_header": bytes.fromhex("001500"),

    # Corrupt / non-NGAP payload (triggers unpack failure path).
    "invalid_payload": bytes.fromhex("deadbeef"),
}

# ---------------------------------------------------------------------------
# ngap_cu_cp corpus seeds
# ---------------------------------------------------------------------------
# The full-stack harness prefixes a control byte selecting the UE state it
# brings up before injecting the message, so it needs its own corpus.
# ---------------------------------------------------------------------------

CU_CP_AWAITING_SETUP_COMPLETE = 0
CU_CP_CONNECTED = 1
CU_CP_SECURED = 2

# The Initial Context Setup Request drives the UE from connected to secured, so
# it is seeded against a connected UE; everything else against a secured one.
NGAP_CU_CP_STATES = {
    "initial_context_setup_request": CU_CP_CONNECTED,
}

def chain(state: int, *pdus: bytes) -> bytes:
    """Control byte followed by length-prefixed messages."""
    out = bytes([state])
    for pdu in pdus:
        assert len(pdu) < 256, "a seed message must fit a single length byte"
        out += bytes([len(pdu)]) + pdu
    return out


NGAP_CU_CP_SEEDS = {
    name: chain(NGAP_CU_CP_STATES.get(name, CU_CP_SECURED), data)
    for name, data in NGAP_SEEDS.items()
}

# A chain, to seed the mutator with the shape the format exists for: the same
# procedure run twice against one UE, which the single-message seeds cannot
# express.
NGAP_CU_CP_SEEDS["initial_context_setup_request_twice"] = chain(
    CU_CP_CONNECTED,
    NGAP_SEEDS["initial_context_setup_request"],
    NGAP_SEEDS["initial_context_setup_request"],
)

# ---------------------------------------------------------------------------
# Seeds of each fuzz target, with the name of its corpus sub-directory
# ---------------------------------------------------------------------------
CORPORA = {
    "ngap_pdu_decoder_fuzzer": ("ngap", NGAP_SEEDS),
    "ngap_cu_cp_fuzzer":       ("ngap_cu_cp", NGAP_CU_CP_SEEDS),
}
