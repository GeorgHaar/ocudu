// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "amf_selector.h"
#include <algorithm>

using namespace ocudu;
using namespace ocucp;

static bool supports_plmn(const amf_candidate& candidate, const plmn_identity& plmn)
{
  return std::find(candidate.plmns.begin(), candidate.plmns.end(), plmn) != candidate.plmns.end();
}

static bool is_same_amf_set(const guami_t& lhs, const guami_t& rhs)
{
  return lhs.plmn == rhs.plmn and lhs.amf_region_id == rhs.amf_region_id and lhs.amf_set_id == rhs.amf_set_id;
}

static bool is_same_guami(const guami_t& lhs, const guami_t& rhs)
{
  return is_same_amf_set(lhs, rhs) and lhs.amf_pointer == rhs.amf_pointer;
}

/// Picks one of the candidates at random, weighted by the relative AMF capacity.
static std::optional<cu_cp_amf_index_t> pick_by_capacity(span<const amf_candidate* const> pool, std::mt19937& rng)
{
  if (pool.empty()) {
    return std::nullopt;
  }

  // An AMF with capacity 0 gets no new UEs, unless all the AMFs have capacity 0.
  std::vector<double> weights;
  for (const amf_candidate* candidate : pool) {
    weights.push_back(candidate->relative_amf_capacity);
  }
  if (std::all_of(weights.begin(), weights.end(), [](double weight) { return weight == 0; })) {
    std::fill(weights.begin(), weights.end(), 1);
  }

  std::discrete_distribution<size_t> distribution(weights.begin(), weights.end());
  return pool[distribution(rng)]->amf_index;
}

std::optional<cu_cp_amf_index_t>
ocudu::ocucp::select_amf(span<const amf_candidate> candidates, const amf_selection_request& request, std::mt19937& rng)
{
  // Rule 1: The AMFs that serve the registered GUAMI.
  std::vector<const amf_candidate*> pool;
  if (request.registered_guami.has_value()) {
    for (const amf_candidate& candidate : candidates) {
      if (candidate.connected and supports_plmn(candidate, request.plmn) and
          std::any_of(candidate.served_guamis.begin(),
                      candidate.served_guamis.end(),
                      [&request](const ngap_served_guami_item& served) {
                        return is_same_guami(served.guami, *request.registered_guami) and
                               served.guami_type.value_or(guami_type_t::native) == request.registered_guami_type;
                      })) {
        pool.push_back(&candidate);
      }
    }
    if (not pool.empty()) {
      return pick_by_capacity(pool, rng);
    }
  }

  // Rule 2: The AMFs that serve the AMF Set ID and AMF Pointer of the 5G-S-TMSI in the selected PLMN.
  if (request.five_g_s_tmsi.has_value()) {
    for (const amf_candidate& candidate : candidates) {
      if (candidate.connected and supports_plmn(candidate, request.plmn) and
          std::any_of(candidate.served_guamis.begin(),
                      candidate.served_guamis.end(),
                      [&request](const ngap_served_guami_item& served) {
                        return served.guami.plmn == request.plmn and
                               served.guami.amf_set_id == request.five_g_s_tmsi->get_amf_set_id() and
                               served.guami.amf_pointer == request.five_g_s_tmsi->get_amf_pointer();
                      })) {
        pool.push_back(&candidate);
      }
    }
    if (not pool.empty()) {
      return pick_by_capacity(pool, rng);
    }
  }

  // Rule 3: An AMF of the AMF Set that the UE identifies.
  auto serves_amf_set_of_ue = [&request](const amf_candidate& candidate) {
    return std::any_of(candidate.served_guamis.begin(),
                       candidate.served_guamis.end(),
                       [&request](const ngap_served_guami_item& served) {
                         if (request.registered_guami.has_value()) {
                           return is_same_amf_set(served.guami, *request.registered_guami) and
                                  served.guami_type.value_or(guami_type_t::native) == request.registered_guami_type;
                         }
                         return request.five_g_s_tmsi.has_value() and served.guami.plmn == request.plmn and
                                served.guami.amf_set_id == request.five_g_s_tmsi->get_amf_set_id();
                       });
  };
  for (const amf_candidate& candidate : candidates) {
    if (candidate.connected and supports_plmn(candidate, request.plmn) and serves_amf_set_of_ue(candidate)) {
      pool.push_back(&candidate);
    }
  }
  if (not pool.empty()) {
    return pick_by_capacity(pool, rng);
  }

  // Rule 4: Any AMF that supports the selected PLMN.
  for (const amf_candidate& candidate : candidates) {
    if (candidate.connected and supports_plmn(candidate, request.plmn)) {
      pool.push_back(&candidate);
    }
  }
  return pick_by_capacity(pool, rng);
}

std::optional<cu_cp_amf_index_t> ocudu::ocucp::select_amf_for_guami(span<const amf_candidate> candidates,
                                                                    const guami_t&            guami)
{
  std::optional<cu_cp_amf_index_t> amf_of_same_set;
  for (const amf_candidate& candidate : candidates) {
    if (not candidate.connected) {
      continue;
    }
    for (const ngap_served_guami_item& served : candidate.served_guamis) {
      if (is_same_guami(served.guami, guami)) {
        return candidate.amf_index;
      }
      if (not amf_of_same_set.has_value() and is_same_amf_set(served.guami, guami)) {
        amf_of_same_set = candidate.amf_index;
      }
    }
  }
  return amf_of_same_set;
}

std::optional<cu_cp_amf_index_t> ocudu::ocucp::select_amf_for_amf_set(span<const amf_candidate> candidates,
                                                                      const plmn_identity&      plmn,
                                                                      uint16_t                  amf_set_id,
                                                                      std::mt19937&             rng)
{
  std::vector<const amf_candidate*> pool;
  for (const amf_candidate& candidate : candidates) {
    bool serves_amf_set = std::any_of(
        candidate.served_guamis.begin(), candidate.served_guamis.end(), [&](const ngap_served_guami_item& served) {
          return served.guami.plmn == plmn and served.guami.amf_set_id == amf_set_id;
        });
    if (candidate.connected and supports_plmn(candidate, plmn) and serves_amf_set) {
      pool.push_back(&candidate);
    }
  }
  return pick_by_capacity(pool, rng);
}
