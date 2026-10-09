// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "amf_selector.h"
#include <algorithm>
#include <limits>

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

static const ngap_unavailable_guami_item* find_unavailable_guami(const amf_candidate& candidate, const guami_t& guami)
{
  auto it = std::find_if(candidate.unavailable_guamis.begin(),
                         candidate.unavailable_guamis.end(),
                         [&guami](const ngap_unavailable_guami_item& item) { return item.guami == guami; });
  return it != candidate.unavailable_guamis.end() ? &*it : nullptr;
}

/// Checks if the AMF serves at least one GUAMI that is available.
static bool has_available_guami(const amf_candidate& candidate)
{
  return std::any_of(candidate.served_guamis.begin(),
                     candidate.served_guamis.end(),
                     [&candidate](const ngap_served_guami_item& served) {
                       return find_unavailable_guami(candidate, served.guami) == nullptr;
                     });
}

std::optional<cu_cp_amf_index_t> amf_selector::pick_by_capacity(span<const amf_candidate* const> pool)
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

/// Ranks an AMF for new UEs. An AMF that is available and not overloaded has rank 0, an overloaded AMF has rank 1, and
/// an AMF whose served GUAMIs are all unavailable has rank 2.
static unsigned get_preference_rank(const amf_candidate& candidate)
{
  if (not has_available_guami(candidate)) {
    return 2;
  }
  return candidate.overload.has_value() ? 1 : 0;
}

std::optional<cu_cp_amf_index_t> amf_selector::pick_preferred_by_capacity(span<const amf_candidate* const> pool)
{
  unsigned best_rank = std::numeric_limits<unsigned>::max();
  for (const amf_candidate* candidate : pool) {
    best_rank = std::min(best_rank, get_preference_rank(*candidate));
  }
  std::vector<const amf_candidate*> preferred;
  for (const amf_candidate* candidate : pool) {
    if (get_preference_rank(*candidate) == best_rank) {
      preferred.push_back(candidate);
    }
  }
  return pick_by_capacity(preferred);
}

template <typename CandidateMatch>
std::optional<cu_cp_amf_index_t> amf_selector::pick_matching_by_capacity(span<const amf_candidate> candidates,
                                                                         const CandidateMatch&     matches)
{
  std::vector<const amf_candidate*> pool;
  for (const amf_candidate& candidate : candidates) {
    if (candidate.connected and matches(candidate)) {
      pool.push_back(&candidate);
    }
  }
  return pick_preferred_by_capacity(pool);
}

template <typename GuamiMatch, typename CandidateMatch>
std::optional<cu_cp_amf_index_t> amf_selector::find_amf_of_guami(span<const amf_candidate> candidates,
                                                                 const GuamiMatch&         matches,
                                                                 const CandidateMatch&     is_eligible)
{
  std::vector<const amf_candidate*> pool;
  std::optional<std::string>        backup_amf_name;
  for (const amf_candidate& candidate : candidates) {
    for (const ngap_served_guami_item& served : candidate.served_guamis) {
      if (not matches(served)) {
        continue;
      }
      const ngap_unavailable_guami_item* unavailable = find_unavailable_guami(candidate, served.guami);
      if (candidate.connected and unavailable == nullptr and is_eligible(candidate)) {
        pool.push_back(&candidate);
        break;
      }
      if ((not candidate.connected or unavailable != nullptr) and not backup_amf_name.has_value()) {
        backup_amf_name = unavailable != nullptr and unavailable->backup_amf_name.has_value()
                              ? unavailable->backup_amf_name
                              : served.backup_amf_name;
      }
    }
  }
  if (not pool.empty()) {
    return pick_preferred_by_capacity(pool);
  }

  if (backup_amf_name.has_value()) {
    for (const amf_candidate& backup : candidates) {
      if (backup.connected and backup.amf_name == *backup_amf_name and is_eligible(backup)) {
        return backup.amf_index;
      }
    }
  }
  return std::nullopt;
}

/// Checks if the overload action rejects the establishment cause (TS 38.413 section 8.7.7.2).
static bool is_rejected_by_overload_action(ngap_overload_action_t action, establishment_cause_t cause)
{
  bool is_mo_data = cause == establishment_cause_t::mo_data or cause == establishment_cause_t::mo_sms or
                    cause == establishment_cause_t::mo_video_call or cause == establishment_cause_t::mo_voice_call;
  switch (action) {
    case ngap_overload_action_t::reject_non_emergency_mo_data:
      return is_mo_data;
    case ngap_overload_action_t::reject_signalling:
      return is_mo_data or cause == establishment_cause_t::mo_sig;
    case ngap_overload_action_t::only_emergency_and_mt:
      return cause != establishment_cause_t::emergency and cause != establishment_cause_t::mt_access;
    case ngap_overload_action_t::only_high_priority_and_mt:
      return cause != establishment_cause_t::high_prio_access and cause != establishment_cause_t::mps_prio_access and
             cause != establishment_cause_t::mcs_prio_access and cause != establishment_cause_t::mt_access;
  }
  return false;
}

bool amf_selector::accepts_ue(const amf_candidate& candidate, establishment_cause_t cause)
{
  if (not candidate.overload.has_value() or not candidate.overload->action.has_value() or
      not is_rejected_by_overload_action(*candidate.overload->action, cause)) {
    return true;
  }
  if (not candidate.overload->traffic_load_reduction.has_value()) {
    return false;
  }
  std::bernoulli_distribution is_rejected(*candidate.overload->traffic_load_reduction / 100.0);
  return not is_rejected(rng);
}

std::optional<cu_cp_amf_index_t> amf_selector::select_amf(span<const amf_candidate>    all_candidates,
                                                          const amf_selection_request& request)
{
  std::vector<amf_candidate> candidates;
  for (const amf_candidate& candidate : all_candidates) {
    if (accepts_ue(candidate, request.establishment_cause)) {
      candidates.push_back(candidate);
    }
  }

  auto supports_selected_plmn = [&request](const amf_candidate& candidate) {
    return supports_plmn(candidate, request.plmn);
  };

  // Rule 1: The AMFs that serve the registered GUAMI.
  if (request.registered_guami.has_value()) {
    std::optional<cu_cp_amf_index_t> amf_index = find_amf_of_guami(
        candidates,
        [&request](const ngap_served_guami_item& served) {
          return served.guami == *request.registered_guami and
                 served.guami_type.value_or(guami_type_t::native) == request.registered_guami_type;
        },
        supports_selected_plmn);
    if (amf_index.has_value()) {
      return amf_index;
    }
  }

  // Rule 2: The AMFs that serve the AMF Set ID and AMF Pointer of the 5G-S-TMSI in the selected PLMN.
  if (request.five_g_s_tmsi.has_value()) {
    std::optional<cu_cp_amf_index_t> amf_index = find_amf_of_guami(
        candidates,
        [&request](const ngap_served_guami_item& served) {
          return served.guami.plmn == request.plmn and
                 served.guami.amf_set_id == request.five_g_s_tmsi->get_amf_set_id() and
                 served.guami.amf_pointer == request.five_g_s_tmsi->get_amf_pointer();
        },
        supports_selected_plmn);
    if (amf_index.has_value()) {
      return amf_index;
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
  std::vector<const amf_candidate*> pool;
  for (const amf_candidate& candidate : candidates) {
    if (candidate.connected and supports_plmn(candidate, request.plmn) and has_available_guami(candidate) and
        serves_amf_set_of_ue(candidate)) {
      pool.push_back(&candidate);
    }
  }
  if (not pool.empty()) {
    return pick_preferred_by_capacity(pool);
  }

  // Rule 4: Any AMF that supports the selected PLMN.
  return pick_matching_by_capacity(
      candidates, [&request](const amf_candidate& candidate) { return supports_plmn(candidate, request.plmn); });
}

std::optional<cu_cp_amf_index_t> amf_selector::select_amf_for_guami(span<const amf_candidate> candidates,
                                                                    const guami_t&            guami)
{
  std::optional<cu_cp_amf_index_t> amf_index = find_amf_of_guami(
      candidates,
      [&guami](const ngap_served_guami_item& served) { return served.guami == guami; },
      [](const amf_candidate&) { return true; });
  if (amf_index.has_value()) {
    return amf_index;
  }

  // Another AMF of the AMF Set serves the UE.
  return pick_matching_by_capacity(candidates, [&guami](const amf_candidate& candidate) {
    return std::any_of(candidate.served_guamis.begin(),
                       candidate.served_guamis.end(),
                       [&guami](const ngap_served_guami_item& served) { return is_same_amf_set(served.guami, guami); });
  });
}

std::optional<cu_cp_amf_index_t> amf_selector::select_amf_for_amf_set(span<const amf_candidate> candidates,
                                                                      const plmn_identity&      plmn,
                                                                      uint16_t                  amf_set_id)
{
  auto serves_amf_set = [&plmn, amf_set_id](const amf_candidate& candidate) {
    return supports_plmn(candidate, plmn) and std::any_of(candidate.served_guamis.begin(),
                                                          candidate.served_guamis.end(),
                                                          [&plmn, amf_set_id](const ngap_served_guami_item& served) {
                                                            return served.guami.plmn == plmn and
                                                                   served.guami.amf_set_id == amf_set_id;
                                                          });
  };
  return pick_matching_by_capacity(candidates, serves_amf_set);
}
