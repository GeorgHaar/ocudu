// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "fapi_dummy_sector.h"
#include "ocudu/fapi/common/error_indication_builder.h"
#include "ocudu/fapi/common/error_indication_notifier.h"
#include "ocudu/fapi/p5/p5_messages.h"
#include "ocudu/fapi/p5/p5_responses_notifier.h"
#include "ocudu/fapi/p7/builders/crc_indication_builder.h"
#include "ocudu/fapi/p7/builders/rx_data_indication_builder.h"
#include "ocudu/fapi/p7/builders/slot_indication_builder.h"
#include "ocudu/fapi/p7/builders/srs_indication_builder.h"
#include "ocudu/fapi/p7/builders/uci_indication_builder.h"
#include "ocudu/fapi/p7/messages/dl_tti_request.h"
#include "ocudu/fapi/p7/messages/tx_data_request.h"
#include "ocudu/fapi/p7/messages/ul_dci_request.h"
#include "ocudu/fapi/p7/messages/ul_tti_request.h"
#include "ocudu/fapi/p7/p7_indications_notifier.h"
#include "ocudu/fapi/p7/p7_slot_indication_notifier.h"
#include "ocudu/ocudulog/ocudulog.h"
#include "ocudu/ran/sch/sch_constants.h"
#include "ocudu/ran/srs/srs_constants.h"
#include "ocudu/support/ocudu_assert.h"
#include <algorithm>

using namespace ocudu;
using namespace fapi_adaptor;

namespace {

class p5_responses_notifier_dummy : public fapi::p5_responses_notifier
{
public:
  void on_param_response(const fapi::param_response& msg) override {}
  void on_config_response(const fapi::config_response& msg) override {}
  void on_stop_indication(const fapi::stop_indication& msg) override {}
};

class error_indication_notifier_dummy : public fapi::error_indication_notifier
{
public:
  void on_error_indication(const fapi::error_indication& msg) override {}
};

class p7_slot_indication_notifier_dummy : public fapi::p7_slot_indication_notifier
{
public:
  void on_slot_indication(const fapi::slot_indication& msg) override {}
};

class p7_indications_notifier_dummy : public fapi::p7_indications_notifier
{
public:
  void on_rx_data_indication(const fapi::rx_data_indication& msg) override {}
  void on_crc_indication(const fapi::crc_indication& msg) override {}
  void on_uci_indication(const fapi::uci_indication& msg) override {}
  void on_srs_indication(const fapi::srs_indication& msg) override {}
  void on_rach_indication(const fapi::rach_indication& msg) override {}
};

} // namespace

static p5_responses_notifier_dummy       dummy_p5_notifier;
static error_indication_notifier_dummy   dummy_error_notifier;
static p7_slot_indication_notifier_dummy dummy_slot_notifier;
static p7_indications_notifier_dummy     dummy_p7_notifier;

/// Uplink SINR reported in all the indications.
static constexpr float ul_sinr_dB = 30.0F;

/// MAC subheader of a padding subPDU, as per TS 38.321, Section 6.2.1.
static constexpr uint8_t padding_subheader = 0x3f;

/// Returns the number of slots the uplink request repository holds.
static unsigned get_repository_size(unsigned ul_indication_delay_slots, unsigned max_processing_delay_slots)
{
  // Margin for requests arriving around the slot boundary.
  static constexpr unsigned margin = 4;

  // A power of two divides the number of slots of a slot point period, so slots map to entries without collisions when
  // the slot count wraps around.
  unsigned nof_slots = 1;
  while (nof_slots < ul_indication_delay_slots + max_processing_delay_slots + margin) {
    nof_slots *= 2;
  }
  return nof_slots;
}

fapi_dummy_sector::fapi_dummy_sector(const fapi_dummy_sector_config& config, ocudulog::basic_logger& logger_) :
  sector_id(config.sector_id),
  scs(config.scs),
  ul_indication_delay_slots(config.ul_indication_delay_slots),
  max_processing_delay_slots(config.max_processing_delay_slots),
  late_request_detection(config.late_request_detection),
  logger(logger_),
  ul_repository(get_repository_size(config.ul_indication_delay_slots, config.max_processing_delay_slots)),
  padding_pdu(tbs_lbrm_default.value(), padding_subheader),
  p5_notifier(&dummy_p5_notifier),
  error_notifier(&dummy_error_notifier),
  slot_notifier(&dummy_slot_notifier),
  p7_notifier(&dummy_p7_notifier)
{
  ocudu_assert(ul_indication_delay_slots > 0, "The uplink indication delay must be at least one slot");
}

void fapi_dummy_sector::send_param_request(const fapi::param_request& msg)
{
  fapi::param_response response;
  response.error_code = fapi::error_code_id::msg_ok;
  p5_notifier->on_param_response(response);
}

void fapi_dummy_sector::send_config_request(const fapi::config_request& msg)
{
  nof_rx_antennas = std::clamp(static_cast<unsigned>(msg.cell_cfg.carrier_cfg.num_rx_ant),
                               1U,
                               static_cast<unsigned>(srs_constants::max_nof_rx_ports));

  fapi::config_response response;
  response.error_code = fapi::error_code_id::msg_ok;
  p5_notifier->on_config_response(response);
}

void fapi_dummy_sector::send_start_request(const fapi::start_request& msg)
{
  sector_state expected = sector_state::idle;
  if (!state.compare_exchange_strong(expected, sector_state::start_requested, std::memory_order_acq_rel)) {
    logger.warning("Sector#{}: FAPI dummy ignored START.request. Cause: sector is not idle", sector_id);
  }
}

void fapi_dummy_sector::send_stop_request(const fapi::stop_request& msg)
{
  sector_state expected = sector_state::running;
  if (state.compare_exchange_strong(expected, sector_state::stop_requested, std::memory_order_acq_rel)) {
    return;
  }

  // A sector that has not notified any slot yet also stops at the next slot boundary.
  if (expected == sector_state::start_requested &&
      state.compare_exchange_strong(expected, sector_state::stop_requested, std::memory_order_acq_rel)) {
    return;
  }

  logger.warning("Sector#{}: FAPI dummy ignored STOP.request. Cause: sector is not running", sector_id);
}

void fapi_dummy_sector::handle_slot_boundary(slot_point_extended slot, std::chrono::system_clock::time_point time_point)
{
  sector_state current_state = state.load(std::memory_order_acquire);

  if (current_state == sector_state::stop_requested) {
    current_slot.store(invalid_slot_count, std::memory_order_relaxed);
    last_completed_slot.store(invalid_slot_count, std::memory_order_relaxed);
    state.store(sector_state::idle, std::memory_order_release);
    p5_notifier->on_stop_indication(fapi::stop_indication{});
    return;
  }

  if (current_state == sector_state::start_requested) {
    // The STOP.request may race with the start transition.
    if (!state.compare_exchange_strong(current_state, sector_state::running, std::memory_order_acq_rel)) {
      return;
    }
    current_state = sector_state::running;
  }

  if (current_state != sector_state::running) {
    return;
  }

  slot_point notified_slot = slot.without_hyper_sfn();
  current_slot.store(notified_slot.count(), std::memory_order_release);

  // The notified slot is ahead of the current time by the processing delay, and the uplink indications are delayed with
  // respect to the reception time.
  slot_point ul_slot = notified_slot - static_cast<int>(max_processing_delay_slots + ul_indication_delay_slots);
  if (auto context = ul_repository.read(ul_slot.count())) {
    notify_ul_indications(ul_slot, *context);
  }

  slot_notifier->on_slot_indication(fapi::build_slot_indication(slot, time_point));
}

bool fapi_dummy_sector::check_request(slot_point slot, fapi::message_type_id msg_id, late_error_builder build_error)
{
  if (state.load(std::memory_order_acquire) != sector_state::running) {
    return false;
  }

  if (!late_request_detection) {
    return true;
  }

  uint32_t current_count = current_slot.load(std::memory_order_acquire);
  if (current_count == invalid_slot_count) {
    return true;
  }

  // A request is late once the current time, which lags the last notified slot by the processing delay, has passed its
  // slot.
  if (slot >= slot_point(scs, current_count) - static_cast<int>(max_processing_delay_slots)) {
    return true;
  }

  notify_late_request(slot, msg_id, build_error);
  return false;
}

bool fapi_dummy_sector::is_ul_indication_due(slot_point ul_slot) const
{
  uint32_t current_count = current_slot.load(std::memory_order_acquire);
  if (current_count == invalid_slot_count) {
    return false;
  }

  return slot_point(scs, current_count) >= ul_slot + max_processing_delay_slots + ul_indication_delay_slots;
}

void fapi_dummy_sector::notify_late_request(slot_point            slot,
                                            fapi::message_type_id msg_id,
                                            late_error_builder    build_error)
{
  slot_point notified_slot(scs, current_slot.load(std::memory_order_acquire));

  logger.warning("Sector#{}: Real-time failure in FAPI dummy: Received late request with msg_id={} for slot {}",
                 sector_id,
                 fmt::underlying(msg_id),
                 slot);
  error_notifier->on_error_indication(build_error(slot, msg_id, notified_slot));
}

void fapi_dummy_sector::send_dl_tti_request(const fapi::dl_tti_request& msg)
{
  check_request(msg.slot, fapi::message_type_id::dl_tti_request, fapi::build_out_of_sync_error_indication);
}

/// Saves the indication contexts of a PUSCH PDU.
static void save_pusch_context(ul_slot_ind_context& context, const fapi::ul_pusch_pdu& pdu)
{
  pusch_ind_context& pusch = context.puschs.emplace_back();
  pusch.rnti               = pdu.rnti;
  pusch.harq_id            = pdu.harq_id;
  pusch.rapid              = pdu.rapid;
  pusch.tb_size            = pdu.pusch_data.has_value() ? std::optional(pdu.pusch_data->tb_size) : std::nullopt;
  pusch.has_uci            = pdu.pusch_uci.has_value();
  pusch.harq_ack_bits      = pusch.has_uci ? pdu.pusch_uci->harq_ack_bit : units::bits(0);
  pusch.csi_part1_bits     = pusch.has_uci ? pdu.pusch_uci->csi_part1_bit : units::bits(0);
}

/// Saves the indication contexts of a PUCCH PDU.
static void save_pucch_context(ul_slot_ind_context& context, const fapi::ul_pucch_pdu& pdu)
{
  pucch_ind_context pucch{.rnti           = pdu.rnti,
                          .format         = pucch_format::FORMAT_0,
                          .sr_bits        = units::bits(0),
                          .harq_ack_bits  = units::bits(0),
                          .csi_part1_bits = units::bits(0)};

  if (const auto* f0 = std::get_if<fapi::ul_pucch_pdu_format_0>(&pdu.format)) {
    pucch.sr_bits       = units::bits(f0->sr_present ? 1 : 0);
    pucch.harq_ack_bits = f0->bit_len_harq;
  } else if (const auto* f1 = std::get_if<fapi::ul_pucch_pdu_format_1>(&pdu.format)) {
    pucch.format        = pucch_format::FORMAT_1;
    pucch.sr_bits       = units::bits(f1->sr_present ? 1 : 0);
    pucch.harq_ack_bits = f1->bit_len_harq;
  } else if (const auto* f2 = std::get_if<fapi::ul_pucch_pdu_format_2>(&pdu.format)) {
    pucch.format         = pucch_format::FORMAT_2;
    pucch.sr_bits        = units::bits(static_cast<unsigned>(f2->sr_bit_len));
    pucch.harq_ack_bits  = f2->bit_len_harq;
    pucch.csi_part1_bits = f2->csi_part1_bit_length;
  } else if (const auto* f3 = std::get_if<fapi::ul_pucch_pdu_format_3>(&pdu.format)) {
    pucch.format         = pucch_format::FORMAT_3;
    pucch.sr_bits        = units::bits(static_cast<unsigned>(f3->sr_bit_len));
    pucch.harq_ack_bits  = f3->bit_len_harq;
    pucch.csi_part1_bits = f3->csi_part1_bit_length;
  } else if (const auto* f4 = std::get_if<fapi::ul_pucch_pdu_format_4>(&pdu.format)) {
    pucch.format         = pucch_format::FORMAT_4;
    pucch.sr_bits        = units::bits(static_cast<unsigned>(f4->sr_bit_len));
    pucch.harq_ack_bits  = f4->bit_len_harq;
    pucch.csi_part1_bits = f4->csi_part1_bit_length;
  } else {
    return;
  }

  context.pucchs.push_back(pucch);
}

/// Saves the indication contexts of an SRS PDU.
static void save_srs_context(ul_slot_ind_context& context, const fapi::ul_srs_pdu& pdu)
{
  if (!pdu.enable_normalized_iq_matrix_report && !pdu.enable_positioning_report) {
    return;
  }

  context.srss.push_back({.rnti               = pdu.rnti,
                          .nof_tx_ports       = static_cast<uint8_t>(pdu.num_ant_ports),
                          .iq_matrix_report   = pdu.enable_normalized_iq_matrix_report,
                          .positioning_report = pdu.enable_positioning_report});
}

void fapi_dummy_sector::send_ul_tti_request(const fapi::ul_tti_request& msg)
{
  if (!check_request(msg.slot, fapi::message_type_id::ul_tti_request, fapi::build_out_of_sync_error_indication)) {
    return;
  }

  {
    auto context = ul_repository.write(msg.slot.count());
    if (context == nullptr) {
      logger.warning(
          "Sector#{}: FAPI dummy dropped UL_TTI.request for slot {}. Cause: slot entry is busy", sector_id, msg.slot);
      return;
    }

    context->clear();
    for (const auto& pdu : msg.pdus) {
      if (const auto* pusch = std::get_if<fapi::ul_pusch_pdu>(&pdu.pdu)) {
        save_pusch_context(*context, *pusch);
      } else if (const auto* pucch = std::get_if<fapi::ul_pucch_pdu>(&pdu.pdu)) {
        save_pucch_context(*context, *pucch);
      } else if (const auto* srs = std::get_if<fapi::ul_srs_pdu>(&pdu.pdu)) {
        save_srs_context(*context, *srs);
      }
    }
  }

  // The request passed the late check, but the clock may have passed the indication time of the slot before the request
  // was stored. The check is best-effort, since it may race with the clock.
  if (is_ul_indication_due(msg.slot)) {
    logger.warning("Sector#{}: Real-time failure in FAPI dummy: UL_TTI.request for slot {} stored after its indication "
                   "time",
                   sector_id,
                   msg.slot);
  }
}

void fapi_dummy_sector::send_ul_dci_request(const fapi::ul_dci_request& msg)
{
  check_request(msg.slot, fapi::message_type_id::ul_dci_request, fapi::build_invalid_sfn_error_indication);
}

void fapi_dummy_sector::send_tx_data_request(const fapi::tx_data_request& msg)
{
  check_request(msg.slot, fapi::message_type_id::tx_data_request, fapi::build_invalid_sfn_error_indication);
}

void fapi_dummy_sector::on_last_message(slot_point slot)
{
  last_completed_slot.store(slot.count(), std::memory_order_release);
}

void fapi_dummy_sector::notify_ul_indications(slot_point slot, const ul_slot_ind_context& context)
{
  for (const pusch_ind_context& pusch : context.puschs) {
    notify_pusch_indications(slot, pusch);
  }
  for (const pucch_ind_context& pucch : context.pucchs) {
    notify_pucch_indication(slot, pucch);
  }
  for (const srs_ind_context& srs : context.srss) {
    notify_srs_indication(slot, srs);
  }
}

/// Returns the detection status of a successfully decoded UCI with the given total number of bits.
static uci_pusch_or_pucch_f2_3_4_detection_status get_uci_detection_status(units::bits nof_uci_bits)
{
  // UCI longer than this number of bits is protected with a CRC, as per TS 38.212, Section 6.3.1.2.1.
  static constexpr units::bits max_nof_bits_without_crc(11);
  return (nof_uci_bits <= max_nof_bits_without_crc) ? uci_pusch_or_pucch_f2_3_4_detection_status::no_dtx
                                                    : uci_pusch_or_pucch_f2_3_4_detection_status::crc_pass;
}

/// Returns a HARQ-ACK payload with all bits set to ACK.
static uci_payload_type get_harq_ack_payload(units::bits nof_bits)
{
  uci_payload_type payload(nof_bits.value());
  payload.fill(0, nof_bits.value(), true);
  return payload;
}

void fapi_dummy_sector::notify_pusch_indications(slot_point slot, const pusch_ind_context& pusch)
{
  static constexpr phy_time_unit time_advance = phy_time_unit::from_seconds(0.0);

  if (pusch.has_uci) {
    fapi::uci_indication         msg;
    fapi::uci_indication_builder builder(msg);
    builder.set_slot(slot);

    fapi::uci_pusch_pdu_builder builder_pdu = builder.add_pusch_pdu();
    builder_pdu.set_ue_specific_parameters(pusch.rnti);
    builder_pdu.set_metrics_parameters(ul_sinr_dB, time_advance, std::nullopt, std::nullopt);

    uci_pusch_or_pucch_f2_3_4_detection_status status =
        get_uci_detection_status(pusch.harq_ack_bits + pusch.csi_part1_bits);
    if (pusch.harq_ack_bits.value() > 0) {
      builder_pdu.set_harq_parameters(status, pusch.harq_ack_bits, get_harq_ack_payload(pusch.harq_ack_bits));
    }
    if (pusch.csi_part1_bits.value() > 0) {
      builder_pdu.set_csi_part1_parameters(
          status, pusch.csi_part1_bits, uci_payload_type(pusch.csi_part1_bits.value()));
    }

    p7_notifier->on_uci_indication(msg);
  }

  if (!pusch.tb_size.has_value()) {
    return;
  }

  fapi::crc_indication         crc_msg;
  fapi::crc_indication_builder crc_builder(crc_msg);
  crc_builder.set_slot(slot);
  crc_builder.set_pdu(pusch.rnti, pusch.harq_id, true, ul_sinr_dB, time_advance, std::nullopt, std::nullopt);
  if (pusch.rapid.has_value()) {
    crc_builder.set_rapid_parameter(*pusch.rapid);
  }
  p7_notifier->on_crc_indication(crc_msg);

  if (pusch.tb_size->value() > padding_pdu.size()) {
    logger.warning("Sector#{}: FAPI dummy truncated the transport block of rnti={} in slot {} to {} bytes",
                   sector_id,
                   pusch.rnti,
                   slot,
                   padding_pdu.size());
  }

  fapi::rx_data_indication         rx_msg;
  fapi::rx_data_indication_builder rx_builder(rx_msg);
  rx_builder.set_slot(slot);
  rx_builder.set_pdu(
      pusch.rnti,
      pusch.harq_id,
      span<const uint8_t>(padding_pdu).first(std::min<size_t>(pusch.tb_size->value(), padding_pdu.size())));
  if (pusch.rapid.has_value()) {
    rx_builder.set_rapid_parameter(*pusch.rapid);
  }
  p7_notifier->on_rx_data_indication(rx_msg);
}

void fapi_dummy_sector::notify_pucch_indication(slot_point slot, const pucch_ind_context& pucch)
{
  static constexpr phy_time_unit time_advance = phy_time_unit::from_seconds(0.0);

  fapi::uci_indication         msg;
  fapi::uci_indication_builder builder(msg);
  builder.set_slot(slot);

  if (pucch.format == pucch_format::FORMAT_0 || pucch.format == pucch_format::FORMAT_1) {
    fapi::uci_pucch_pdu_format_0_1_builder builder_pdu = builder.add_format_0_1_pucch_pdu();
    builder_pdu.set_ue_specific_parameters(pucch.rnti).set_format(pucch.format);
    builder_pdu.set_time_advance(time_advance).set_metrics_parameters(ul_sinr_dB, std::nullopt, std::nullopt);
    if (pucch.sr_bits.value() > 0) {
      builder_pdu.set_sr_parameters(false);
    }
    if (pucch.harq_ack_bits.value() > 0) {
      static_vector<uci_pucch_f0_or_f1_harq_values, fapi::uci_harq_format_0_1::MAX_NUM_HARQ> harq(
          std::min<size_t>(pucch.harq_ack_bits.value(), fapi::uci_harq_format_0_1::MAX_NUM_HARQ),
          uci_pucch_f0_or_f1_harq_values::ack);
      builder_pdu.set_harq_parameters(harq);
    }
    p7_notifier->on_uci_indication(msg);
    return;
  }

  fapi::uci_pucch_pdu_format_2_3_4_builder builder_pdu = builder.add_format_2_3_4_pucch_pdu();
  builder_pdu.set_ue_specific_parameters(pucch.rnti).set_format(pucch.format);
  builder_pdu.set_metrics_parameters(ul_sinr_dB, time_advance, std::nullopt, std::nullopt);

  uci_pusch_or_pucch_f2_3_4_detection_status status =
      get_uci_detection_status(pucch.sr_bits + pucch.harq_ack_bits + pucch.csi_part1_bits);
  if (pucch.sr_bits.value() > 0) {
    builder_pdu.set_sr_parameters(
        bounded_bitset<fapi::sr_pdu_format_2_3_4::MAX_SR_PAYLOAD_SIZE_BITS>(pucch.sr_bits.value()));
  }
  if (pucch.harq_ack_bits.value() > 0) {
    builder_pdu.set_harq_parameters(status, pucch.harq_ack_bits, get_harq_ack_payload(pucch.harq_ack_bits));
  }
  if (pucch.csi_part1_bits.value() > 0) {
    builder_pdu.set_csi_part1_parameters(status, pucch.csi_part1_bits, uci_payload_type(pucch.csi_part1_bits.value()));
  }
  p7_notifier->on_uci_indication(msg);
}

void fapi_dummy_sector::notify_srs_indication(slot_point slot, const srs_ind_context& srs)
{
  static constexpr phy_time_unit time_advance = phy_time_unit::from_seconds(0.0);

  fapi::srs_indication         msg;
  fapi::srs_indication_builder builder(msg);
  builder.set_slot(slot);

  fapi::srs_indication_pdu_builder builder_pdu = builder.set_pdu(srs.rnti);
  builder_pdu.set_metrics_parameters(time_advance);

  if (srs.iq_matrix_report) {
    unsigned nof_tx_ports =
        std::clamp(static_cast<unsigned>(srs.nof_tx_ports), 1U, static_cast<unsigned>(srs_constants::max_nof_tx_ports));
    srs_channel_matrix matrix(nof_rx_antennas, nof_tx_ports);
    for (unsigned i_port = 0, e = std::min(nof_rx_antennas, nof_tx_ports); i_port != e; ++i_port) {
      matrix.set_coefficient(cf_t(1.0F, 0.0F), i_port, i_port);
    }
    builder_pdu.set_codebook_report_matrix(matrix);
  }

  if (srs.positioning_report) {
    builder_pdu.set_positioning_report_parameters(time_advance, std::nullopt, std::nullopt, std::nullopt);
  }

  p7_notifier->on_srs_indication(msg);
}
