// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "f1ap_cu_test_helpers.h"
#include "lib/f1ap/cu_cp/ue_context/f1ap_cu_ue_context.h"
#include "tests/ocudu_test_requirements.h"
#include "tests/test_doubles/utils/test_rng.h"
#include "ocudu/adt/format.h"
#include "ocudu/f1ap/f1ap_ue_id_types.h"
#include "ocudu/support/executors/manual_task_worker.h"
#include <cstdint>
#include <gtest/gtest.h>

using namespace ocudu;
using namespace ocucp;

class gnb_cu_ue_f1ap_id_allocator_test : public gnb_cu_ue_f1ap_id_allocator
{
public:
  void set_next_id(gnb_cu_ue_f1ap_id_t id) { next_id = id; }
};

/// Fixture class for F1AP UE context
class f1ap_cu_ue_context_test : public ::testing::Test
{
protected:
  f1ap_cu_ue_context_test()
  {
    OCUDU_TEST_REQUIREMENTS("MVP-ARCH-INTF-9");

    f1ap_logger.set_level(ocudulog::basic_levels::debug);
    ocudulog::init();
  }
  ~f1ap_cu_ue_context_test()
  {
    // flush logger after each test
    ocudulog::flush();
  }

  cu_cp_ue_index_t generate_random_ue_index()
  {
    return uint_to_ue_index(test_rng::uniform_int<uint64_t>(to_underlying(cu_cp_ue_index_t::min),
                                                            to_underlying(cu_cp_ue_index_t::max) - 1));
  }

  ocudulog::basic_logger&          f1ap_logger = ocudulog::fetch_basic_logger("CU-CP-F1");
  timer_manager                    timer_mng;
  manual_task_worker               ctrl_worker{128};
  timer_factory                    timers{timer_mng, ctrl_worker};
  gnb_cu_ue_f1ap_id_allocator_test cu_ue_id_alloc;
  f1ap_ue_context_list             ue_ctxt_list{cu_ue_id_alloc, timers, f1ap_logger};
};

TEST_F(f1ap_cu_ue_context_test, when_ue_added_then_ue_exists)
{
  cu_cp_ue_index_t    ue_index      = generate_random_ue_index();
  gnb_cu_ue_f1ap_id_t cu_ue_f1ap_id = generate_random_gnb_cu_ue_f1ap_id();

  ue_ctxt_list.add_ue(ue_index, cu_ue_f1ap_id);

  ASSERT_TRUE(ue_ctxt_list.contains(cu_ue_f1ap_id));
  ASSERT_TRUE(ue_ctxt_list.contains(ue_index));

  ASSERT_EQ(ue_ctxt_list[cu_ue_f1ap_id].ue_ids.cu_ue_f1ap_id, cu_ue_f1ap_id);
  ASSERT_EQ(ue_ctxt_list[cu_ue_f1ap_id].ue_ids.ue_index, ue_index);
  ASSERT_EQ(ue_ctxt_list[ue_index].ue_ids.cu_ue_f1ap_id, cu_ue_f1ap_id);
  ASSERT_EQ(ue_ctxt_list[ue_index].ue_ids.ue_index, ue_index);
}

TEST_F(f1ap_cu_ue_context_test, when_ue_not_added_then_ue_doesnt_exist)
{
  cu_cp_ue_index_t    ue_index      = generate_random_ue_index();
  gnb_cu_ue_f1ap_id_t cu_ue_f1ap_id = generate_random_gnb_cu_ue_f1ap_id();

  ASSERT_FALSE(ue_ctxt_list.contains(cu_ue_f1ap_id));
  ASSERT_FALSE(ue_ctxt_list.contains(ue_index));
}

TEST_F(f1ap_cu_ue_context_test, when_ue_exists_then_removal_succeeds)
{
  cu_cp_ue_index_t    ue_index      = generate_random_ue_index();
  gnb_cu_ue_f1ap_id_t cu_ue_f1ap_id = generate_random_gnb_cu_ue_f1ap_id();

  ue_ctxt_list.add_ue(ue_index, cu_ue_f1ap_id);

  // test removal
  ue_ctxt_list.remove_ue(ue_index);

  ASSERT_FALSE(ue_ctxt_list.contains(cu_ue_f1ap_id));
  ASSERT_FALSE(ue_ctxt_list.contains(ue_index));
}

TEST_F(f1ap_cu_ue_context_test, when_ue_is_added_then_next_ue_id_is_increased)
{
  cu_cp_ue_index_t    ue_index      = generate_random_ue_index();
  gnb_cu_ue_f1ap_id_t cu_ue_f1ap_id = ue_ctxt_list.allocate_gnb_cu_ue_f1ap_id();

  ASSERT_EQ((unsigned)cu_ue_f1ap_id, (unsigned)gnb_cu_ue_f1ap_id_t::min);

  ue_ctxt_list.add_ue(ue_index, cu_ue_f1ap_id);

  // remove ue
  ue_ctxt_list.remove_ue(ue_index);

  ASSERT_FALSE(ue_ctxt_list.contains(cu_ue_f1ap_id));
  ASSERT_FALSE(ue_ctxt_list.contains(ue_index));

  ASSERT_EQ((unsigned)ue_ctxt_list.allocate_gnb_cu_ue_f1ap_id(), (unsigned)gnb_cu_ue_f1ap_id_t::min + 1);
}

TEST_F(f1ap_cu_ue_context_test, when_ue_is_removed_then_its_cu_ue_f1ap_id_is_released)
{
  gnb_cu_ue_f1ap_id_t cu_ue_f1ap_id = ue_ctxt_list.allocate_gnb_cu_ue_f1ap_id();
  cu_cp_ue_index_t    ue_index      = generate_random_ue_index();
  ue_ctxt_list.add_ue(ue_index, cu_ue_f1ap_id);
  ASSERT_EQ(cu_ue_id_alloc.size(), 1);

  ue_ctxt_list.remove_ue(ue_index);

  ASSERT_EQ(cu_ue_id_alloc.size(), 0);
}

TEST_F(f1ap_cu_ue_context_test, when_two_ue_context_lists_share_an_allocator_then_cu_ue_f1ap_ids_are_unique)
{
  f1ap_ue_context_list other_ue_ctxt_list{cu_ue_id_alloc, timers, f1ap_logger};

  gnb_cu_ue_f1ap_id_t cu_ue_f1ap_id       = ue_ctxt_list.allocate_gnb_cu_ue_f1ap_id();
  gnb_cu_ue_f1ap_id_t other_cu_ue_f1ap_id = other_ue_ctxt_list.allocate_gnb_cu_ue_f1ap_id();

  ASSERT_NE(cu_ue_f1ap_id, other_cu_ue_f1ap_id);
}

TEST_F(f1ap_cu_ue_context_test, when_ue_context_list_is_destroyed_then_its_cu_ue_f1ap_ids_are_released)
{
  {
    f1ap_ue_context_list other_ue_ctxt_list{cu_ue_id_alloc, timers, f1ap_logger};
    other_ue_ctxt_list.add_ue(generate_random_ue_index(), other_ue_ctxt_list.allocate_gnb_cu_ue_f1ap_id());
    ASSERT_EQ(cu_ue_id_alloc.size(), 1);
  }

  ASSERT_EQ(cu_ue_id_alloc.size(), 0);
}

TEST_F(f1ap_cu_ue_context_test, when_next_cu_ue_f1ap_id_is_in_use_then_allocator_skips_it)
{
  gnb_cu_ue_f1ap_id_t first_id  = cu_ue_id_alloc.allocate();
  gnb_cu_ue_f1ap_id_t second_id = cu_ue_id_alloc.allocate();
  cu_ue_id_alloc.release(second_id);

  cu_ue_id_alloc.set_next_id(first_id);

  ASSERT_EQ(cu_ue_id_alloc.allocate(), second_id);
}

TEST_F(f1ap_cu_ue_context_test, when_max_cu_ue_f1ap_id_is_allocated_then_allocator_wraps_around)
{
  cu_ue_id_alloc.set_next_id(gnb_cu_ue_f1ap_id_t::max);

  ASSERT_EQ(cu_ue_id_alloc.allocate(), gnb_cu_ue_f1ap_id_t::max);
  ASSERT_EQ(cu_ue_id_alloc.allocate(), gnb_cu_ue_f1ap_id_t::min);
}
