#pragma once

#include <phi/core/Types.hpp>

namespace phi {

/// Timing information handed to every scheduled task.
struct FrameTime {
    /// Seconds of game time since the previous frame (or the fixed step in
    /// FixedUpdate): real time times the engine's time scale.
    f64 delta = 0.0;
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
    /// Real seconds since the previous frame, whatever the time scale: for
    /// what must not slow down or pause (a camera, menus, the interface).
    /// Last, so positional initializers written before it keep their meaning.
    f64 unscaledDelta = 0.0;
};

}  // namespace phi
