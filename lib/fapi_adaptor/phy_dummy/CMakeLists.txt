# SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
# SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
# SPDX-License-Identifier: BSD-3-Clause-Open-MPI

add_library(ocudu_phy_dummy_fapi_adaptor STATIC
    phy_dummy_fapi_adaptor_factory.cpp
    phy_dummy_fapi_adaptor_impl.cpp
    phy_dummy_fapi_p5_sector_adaptor.cpp
    phy_dummy_fapi_p7_sector_adaptor.cpp
    phy_dummy_fapi_sector_adaptor.cpp)
target_link_libraries(ocudu_phy_dummy_fapi_adaptor PRIVATE ocudulog)
