#pragma once

#include <bdex/core/Types.hpp>

namespace bdex {

/// Timing information handed to every scheduled task.
struct FrameTime {
    f64 delta = 0.0;    ///< Seconds since the previous frame (or the fixed step in FixedUpdate).
    /// Seconds of simulated time since the engine started. In FixedUpdate it is
    /// the fixed-step clock at the end of the current step, so successive steps
    /// of one frame see increasing values.
    f64 elapsed = 0.0;
    u64 frame = 0;      ///< Index of the current frame, starting at 0.
    /// Fraction of a fixed step accumulated but not yet simulated, in [0, 1).
    /// Outside FixedUpdate, blend the last two fixed-step states with it
    /// (`mix(previous, current, fixedAlpha)`) for smooth motion at any frame
    /// rate. Always 0 inside FixedUpdate.
    f64 fixedAlpha = 0.0;
};

}  // namespace bdex
