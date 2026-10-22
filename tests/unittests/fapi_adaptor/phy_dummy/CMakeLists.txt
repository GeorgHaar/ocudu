# SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
# SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
# SPDX-License-Identifier: BSD-3-Clause-Open-MPI

add_executable(phy_dummy_fapi_adaptor_test phy_dummy_fapi_adaptor_test.cpp)
target_include_directories(phy_dummy_fapi_adaptor_test PRIVATE ${CMAKE_SOURCE_DIR}/lib/fapi_adaptor/phy_dummy)
target_link_libraries(phy_dummy_fapi_adaptor_test PRIVATE ocudu_phy_dummy_fapi_adaptor ocudu_support ocudulog gtest)
gtest_discover_tests(phy_dummy_fapi_adaptor_test)
