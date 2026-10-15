# Threat model

## What OCUDU is and where untrusted input enters

OCUDU is an open-source 5G RAN: the CU (CU-CP, CU-UP) and DU (L2, PHY) of a gNB, following 3GPP and O-RAN
specifications. It is deployed by operators, private-network integrators and research labs. It is written in C++17 and
the libraries under `lib/` matter most; the applications under `apps/` mostly wire them together.

Untrusted input enters, from most to least exposed:

1. **The air interface, from any UE in radio range, before authentication.** Uplink PHY processing (PRACH, PUCCH, PUSCH
   decoding, UCI, SRS) in `lib/phy/upper`, MAC PDUs and MAC CEs (BSR, PHR, C-RNTI, ...) in `lib/mac`, RLC PDUs and
   status PDUs in `lib/rlc`, PDCP PDUs in `lib/pdcp`, and RRC messages (UL-CCCH, UL-DCCH ASN.1) in `lib/rrc` and
   `lib/asn1`. RRCSetupRequest, RRCReestablishmentRequest and RRCResumeRequest are processed with no security context at
   all. NAS is passed through to the core and not parsed by OCUDU.
2. **The air interface after security activation.** Same layers as above, plus PDCP integrity verification and
   deciphering (`lib/pdcp`, `lib/security`) and ROHC decompression (`lib/rohc`). A UE that has authenticated is still
   untrusted: it may be a malicious subscriber.
3. **User-plane transport.** GTP-U on N3 (from the UPF) and F1-U/NR-U between CU-UP and DU (`lib/gtpu`, `lib/f1u`,
   `lib/nru`), carried over UDP (`lib/gateways`). These often reach across shared transport networks.
4. **Control-plane peers.** NGAP from the AMF (`lib/ngap`), F1AP between CU-CP and DU (`lib/f1ap`), E1AP between CU-CP
   and CU-UP (`lib/e1ap`), XnAP from neighbour gNBs (`lib/xnap`), E2AP/E2SM from a near-RT RIC (`lib/e2`), NRPPa and LPP
   (`lib/nrppa`, `lib/lpp`), all carried over SCTP (`lib/gateways`). These peers are normally authenticated at the
   transport level (IPsec or DTLS), but OCUDU must not corrupt memory for any input they send.
5. **Open Fronthaul (O-RAN 7.2x).** eCPRI, C-plane and U-plane messages from the RU over Ethernet (`lib/ofh`). The
   fronthaul is usually a dedicated network, but a compromised or faulty RU must not be able to corrupt the DU.
6. **The remote control server** (`apps/services/remote_control`), a JSON-over-WebSocket API that is off by default and
   binds to 127.0.0.1.

Treated as trusted: YAML configuration and command line arguments, files named by the configuration (certificates, keys,
PCAP output paths), environment variables, the RF hardware and its driver (UHD, ZMQ, DIFI), the DPDK and accelerator
setup, and the local operator. A bug that requires control of these is not a vulnerability.

## Components that matter most / least

Most important:

- every decoder reachable from a UE before authentication: uplink PHY, MAC CE parsing, RLC, PDCP header handling, RRC
  and the ASN.1 unpackers in `lib/asn1` (RRC NR, F1AP, NGAP, E1AP, XnAP, E2AP code is generated but in scope);
- the scheduler (`lib/scheduler`) where it consumes UE reports (BSR, PHR, CSI, HARQ feedback, SR);
- security: integrity checks that can be skipped or bypassed, acceptance of null or downgraded algorithms when the
  configuration does not allow them, COUNT/key reuse, and anything in `lib/security`;
- the CU-CP UE context handling (`lib/cu_cp`), where messages from UEs, the AMF, DUs and neighbours meet: use after free
  on UE release, handover, reestablishment and RRC reject paths is a known class of bug.

Lower importance, still in scope: GTP-U, F1-U and NR-U, the OFH decoders, E2 service models, NRPPa/LPP, the remote
control server, and the metrics/PCAP writers when they process attacker data.

Out of scope:

- `tests/`, `tests/fuzz/` harness code itself, `apps/examples/`, `utils/`, `scripts/`, `docker/`, `docs/`;
- `external/` (vendored third-party code): report it upstream unless OCUDU uses it incorrectly;
- hardware-specific code that cannot be built here: `lib/hal` (DPDK, accelerators), `lib/cuda`, and the radio drivers in
  `lib/radio` (UHD, Sidekiq). DTLS over SCTP (`lib/gateways/sctp_dtls*`) needs an OpenSSL built with SCTP support and is
  not compiled in this image, but the code is still in scope for review;
- spec non-conformance with no security consequence;
- denial of service through resource exhaustion that is bounded by configured limits (maximum UEs, bearers, queue sizes)
  and recovers on its own.

## How to exercise it

- `/src/build` is a RelWithDebInfo build with ASan + UBSan, `-O1 -g`, frame pointers and normal-level asserts
  (`ocudu_assert` is active, `ocudu_sanity_check` paranoid asserts are not).
- Unit tests are GoogleTest binaries under `/src/build/tests/unittests/<layer>/`; run them with
  `cd /src/build && ctest -R <layer>`. They contain the easiest scaffolding for driving a single layer (e.g.
  `tests/unittests/rlc`, `tests/unittests/pdcp`, `tests/unittests/cu_cp`, `tests/unittests/mac`) and the test doubles
  under `tests/test_doubles/` build valid messages that can then be mutated.
- libFuzzer harnesses are built under `/src/build/tests/fuzz/{ofh,ngap,rrc}/` with seed corpora in
  `/src/tests/fuzz/<layer>/corpus/`. `rrc_ue_fuzzer`, `rrc_cu_cp_fuzzer` and `ngap_cu_cp_fuzzer` drive a full CU-CP; the
  others drive single decoders. Reproduce a crash with `<harness> <crash-file>`.
- End-to-end: `/src/build/apps/gnb/gnb` with the ZMQ radio (`ru_sdr: device_driver: zmq`) and
  `cu_cp: amf: no_core: true` runs a gNB without a core network or RF hardware. `configs/` holds example configurations.
- Prefer a reproducer as a unit test or a fuzz input over a full gNB run.

## How we rate severity

- **Critical**: memory corruption (out-of-bounds write, use after free, double free, type confusion) reachable from a UE
  over the air before authentication, or any attacker-controlled write that plausibly leads to code execution from a UE.
- **High**: memory corruption reachable from a UE after authentication; out-of-bounds reads from a UE that leak memory
  to the attacker; a crash, assertion failure or hang that a single UE can trigger, because it takes down the cell for
  every other user; bypass of PDCP integrity protection or ciphering; memory corruption from GTP-U, F1-U or NR-U.
- **Medium**: memory corruption or a crash triggered by a control-plane peer (AMF, CU-CP/DU/CU-UP, neighbour gNB, RIC)
  or by the RU over the fronthaul; information leaks to such peers; per-UE denial of service that does not affect other
  UEs.
- **Low**: issues that need an unusual non-default configuration, the remote control server on localhost, and hardening
  issues without a demonstrated impact.

An assertion (`ocudu_assert`, `ocudu_assertion_failure`, `report_fatal_error`, `report_fatal_error_if_not`) reachable
from untrusted input is a bug: rate it as the denial of service it causes. A violated `ocudu_assume` is undefined
behaviour: rate it like the memory corruption it can cause. Undefined behaviour reported by UBSan without memory impact
(signed overflow, invalid shift) is at most Medium unless it is shown to cause one of the effects above.

## How we would like reports

- Name the interface and the message (e.g. "UL-DCCH MeasurementReport", "MAC Long BSR CE", "NGAP
  PDUSessionResourceSetupRequest") and say whether it is reachable before authentication.
- A reproducer as a GoogleTest case in the matching `tests/unittests/<layer>/` file, or as a fuzz input for an existing
  harness, is preferred.
- Patches should follow the surrounding code style (`.clang-format` at the repository root) and handle malformed input
  by logging and dropping the PDU/message, or by releasing the offending UE, never by asserting.
- Contributions should follow the [developer guide](https://docs.ocudu.org/dev_guide/), specifically the
  [code guide](https://docs.ocudu.org/dev_guide/code_guide/) and the
  [contributing guide](https://docs.ocudu.org/dev_guide/contributing_guide/).
