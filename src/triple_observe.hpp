#pragma once
// Dev-only observation for the three-view spike (docs/2026-10-30-triple-views-and-clean-launch-plan.md, step 2).
namespace TripleObserve {
// True for the life of a process started with DBCE_OUTRUN_TRIPLE_OBSERVE set (read once).
bool On();
// One CalcCameraMatrix (0x484BD0) call: the camera and the address the call returns to.
void Camera(const void* camera, const void* returnAddress);
}
