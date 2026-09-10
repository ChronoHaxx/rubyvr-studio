// vr_layer.h — OpenXR presentation for RubySapphireRecomp.
//
// Puts the GBA framebuffer on a world-locked quad in the headset. PC VR only
// (x64 Windows + SteamVR / Oculus PC runtime); see _docs/rubysapphire-vr.md.
//
// WHO DRIVES TIMING, because it is the whole design:
//
//   The GAME owns its clock. runtime.cpp paces itself to 59.7275 Hz and calls
//   HostWindow::present() when it has a frame. The COMPOSITOR owns a different
//   clock, asking for frames at 72/90/120 Hz. Neither can be made to wait for
//   the other:
//
//     - Block the game on xrWaitFrame and Pokemon runs at the headset's
//       refresh rate. At 90 Hz that is 1.5x too fast.
//     - Block the compositor on the game and you drop frames in a headset,
//       which is felt in the inner ear, not merely seen.
//
//   So they are decoupled. present() copies pixels into a staging buffer and
//   returns immediately (microseconds). A dedicated VR thread runs the OpenXR
//   frame loop and presents whatever the newest complete frame happens to be,
//   repeating it when the game has not produced a new one yet.
//
//   NOTE this supersedes should_advance_source_frame() in openxr-spike/main.c.
//   That accumulator existed because the SPIKE drove the source itself. Here
//   the game drives, so "present the latest complete frame" is both simpler and
//   correct. The spike's reasoning is still worth reading — see
//   _docs/frame-pacing-explained.md — the conclusion just changed.

#pragma once

#include <cstdint>

namespace vr {

// Bring up OpenXR and start the presentation thread. Returns false and leaves
// the process entirely unaffected if no runtime is available, so a normal
// desktop launch on a machine with no headset costs nothing but this call.
//
// Must be called from the MAIN thread: it creates the hidden SDL window and GL
// context that OpenXR binds to, then hands that context to the VR thread.
bool start();

// Stop the thread, tear down the session, release the context. Idempotent.
void stop();

// Wire this into HostWindow::set_frame_sink(). Non-blocking by contract.
void frame_sink(const uint8_t* rgb888, int w, int h, void* user);

}  // namespace vr
