# SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
# SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
# SPDX-License-Identifier: BSD-3-Clause-Open-MPI
# Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

add_executable(fapi_dummy_test fapi_dummy_test.cpp)
target_include_directories(fapi_dummy_test PRIVATE ${CMAKE_SOURCE_DIR}/lib/fapi_adaptor/phy/dummy)
target_link_libraries(fapi_dummy_test PRIVATE ocudu_fapi_dummy ocudu_support ocudulog gtest)
gtest_discover_tests(fapi_dummy_test)
