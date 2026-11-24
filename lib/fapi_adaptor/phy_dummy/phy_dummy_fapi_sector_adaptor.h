// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI

#pragma once

#include "phy_dummy_fapi_p5_sector_adaptor.h"
#include "phy_dummy_fapi_p7_sector_adaptor.h"
#include "phy_dummy_fapi_sector_executor.h"
#include "ocudu/fapi_adaptor/phy/phy_fapi_adaptor.h"

namespace ocudu {

class task_executor;

namespace fapi_adaptor {

/// \brief PHY dummy FAPI adaptor sector.
///
/// Emulates the PHY of one cell at the FAPI interface. Slot boundaries are provided by an external clock. The P5 and P7
/// logic runs in the given sequential executor.
class phy_dummy_fapi_sector_adaptor : public phy_fapi_sector_adaptor
{
public:
  /// Last slot boundary a sector has finished processing.
  using slot_boundary_progress = phy_dummy_fapi_p7_sector_adaptor::slot_boundary_progress;

  phy_dummy_fapi_sector_adaptor(const phy_dummy_fapi_sector_adaptor_config& config,
                                ocudulog::basic_logger&                     logger,
                                task_executor&                              executor);

  ~phy_dummy_fapi_sector_adaptor() override;

  // See interface for documentation.
  phy_fapi_p5_sector_adaptor& get_p5_sector_adaptor() override { return p5_adaptor; }

  // See interface for documentation.
  phy_fapi_p7_sector_adaptor& get_p7_sector_adaptor() override { return p7_adaptor; }

  // See interface for documentation.
  void start() override {}

  // See interface for documentation.
  void stop() override {}

  /// \brief Handles a new slot boundary.
  ///
  /// \param[in] slot       Slot notified in the SLOT.indication, in advance of the current time.
  /// \param[in] time_point Transmission time of the notified slot.
  void handle_slot_boundary(slot_point_extended slot, std::chrono::system_clock::time_point time_point)
  {
    p7_adaptor.handle_slot_boundary(slot, time_point);
  }

  /// \brief Returns the last slot boundary the sector has finished processing.
  ///
  /// A running sector finishes a slot boundary when it receives the last request of the slot.
  slot_boundary_progress get_progress() const { return p7_adaptor.get_progress(); }

private:
  /// Sequential executor of the P5 and P7 logic.
  phy_dummy_fapi_sector_executor   sector_executor;
  phy_dummy_fapi_p7_sector_adaptor p7_adaptor;
  phy_dummy_fapi_p5_sector_adaptor p5_adaptor;
};

} // namespace fapi_adaptor
} // namespace ocudu
