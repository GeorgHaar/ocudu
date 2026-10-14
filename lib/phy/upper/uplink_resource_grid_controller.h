// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-FileCopyrightText: Copyright (C) 2026 OCUDU contributors
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/phy/support/shared_resource_grid.h"
#include "ocudu/support/synchronization/stop_event.h"
#include <atomic>

namespace ocudu {

class resource_grid;

/// \brief Uplink resource grid controller.
///
/// Wraps a reference to a resource grid with a reference counter and a stop token to control the lifetime of resource
/// grids in the uplink processing chain.
class uplink_resource_grid_controller : private shared_resource_grid::pool_interface
{
public:
  uplink_resource_grid_controller(resource_grid& grid_, std::atomic<unsigned>& ref_counter_) :
    grid(grid_), ref_counter(ref_counter_)
  {
  }

  /// Creates a shared grid in exchange for a stop token that will be released when the grid is released.
  shared_resource_grid create_shared_grid(rt_stop_event_token stop_token_)
  {
    [[maybe_unused]] bool prev_available = available.exchange(false, std::memory_order_acq_rel);
    ocudu_assert(prev_available, "Resource grid controller is not available.");

    [[maybe_unused]] unsigned prev_ref_counter = ref_counter.exchange(1, std::memory_order_release);
    ocudu_assert(prev_ref_counter == 0, "Resource grid was already alive in a scope.");

    // The stop token of the previous scope was released before the controller became available.
    stop_token = std::move(stop_token_);

    return shared_resource_grid(*this, ref_counter);
  }

  /// Returns true if the resource grid is not present in any scope and the stop token has been released.
  bool is_available() const { return available.load(std::memory_order_acquire); }

private:
  // See the shared_resource_grid::pool_interface interface for documentation.
  resource_grid& get() override { return grid; }

  // See the shared_resource_grid::pool_interface interface for documentation.
  void notify_release_scope() override
  {
    stop_token.reset();
    available.store(true, std::memory_order_release);
  }

  /// Reference to the actual resource grid to control.
  resource_grid& grid;
  /// Resource grid reference counter.
  std::atomic<unsigned>& ref_counter;
  /// \brief Whether the resource grid controller is available or not.
  ///
  /// The controller is considered to be available when:
  /// - the resource grid is not present in any scope; and,
  /// - the stop token is released.
  std::atomic<bool> available = true;
  /// Stop token acquired for the slot processing.
  rt_stop_event_token stop_token;
};

} // namespace ocudu
