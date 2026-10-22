# Motion discontinuity capture

This changes observation and adds an explicit diagnostic input override. It does
not change filtering, prediction, producer leases, semaphore ownership, copy
ordering, or frame pacing. Normal mode is live tracking.

The Android and Linux driver processes watch `motion-control` in their existing
shared runtime directory. `capture.py` only writes that diagnostic configuration
and reads app-owned results using ADB; it does not execute Linux guest commands.

```sh
python3 tools/motion/capture.py enable
# Restart XR Dev and SteamVR once after enabling.
python3 tools/motion/capture.py status
python3 tools/motion/capture.py arm --mode live --trace on --seconds 25
# After the capture has exported; use the run number returned by arm:
python3 tools/motion/capture.py collect RUN /tmp/motion-RUN
python3 tools/motion/analyze.py '/tmp/motion-RUN/*.bin' --output /tmp/motion-RUN/report.json
```

Set `ADB` to the adb executable and optionally `ANDROID_SERIAL`. This tool targets
only `com.droiddeck.launcher.debug`. With no `motion-enabled` marker at startup,
there is no controller thread, polling, status-file I/O or trace allocation.
Use `capture.py disable` and restart after experimentation. Each process must acknowledge the same run,
mode and absolute CLOCK_MONOTONIC window. The tool checks both controllers are
advancing before arming. Android publication timestamps and joined driver reads
provide an additional cross-process clock check; the glasses' sensor clock is
recorded separately and must not be compared as an absolute host timestamp.

`--mode fixed` supplies identity head orientation. `--mode sine` supplies yaw
`30° * sin(2πt/8 seconds)` at the driver pose boundary. Position, validity,
poseTimeOffset and the render path are unchanged. The override begins at zero
yaw during the warm-up delay and automatically returns to live orientation after
the capture (within the controller's 200 ms polling interval). Warn the wearer
that the view will move independently of their head. Changes into/out of test
mode are outside the analyzed window. Do not test while standing or walking.

First collect a live capture reproducing the snap, then fixed and sine captures
in the same scene, followed by live again. Repeat sine with `--trace off` and on
in interleaved order. Trace-off uses the same preallocated memory and synthetic
function; compare existing compositor timing/SurfaceFlinger measurements and the
wearer's symptom, because trace-off intentionally contains no per-frame records.
Record build hashes, scene, display rate, symptom and capture run for each trial.
Do not infer success from a short smooth interval.

## Recorder

Records are 128-byte little-endian structs; headers are also 128 bytes. Eight
single-writer thread buffers per process hold 65,536 records each (64 MiB total).
The control thread allocates and touches all pages before acknowledging the run.
The event path performs no allocation, text formatting, filesystem access, or
blocking lock acquisition. Per-thread atomic busy flags let the exporter stop
writers without a hot-path mutex. Buffers retain the beginning of the window,
count overflow, and never wrap silently. More than eight emitting threads are
counted as unregistered drops. Export runs after capture, writes temporary files,
and renames only completed files. No file transfer occurs during capture.

Captures are limited to 30 seconds to fit the 1 kHz sensor plus publication
stream. All eight files per process are exported, including empty streams, so a
missing writer is distinguishable from a missing file. Restarting a process
resets synthetic mode to live. Old configurations cannot rearm after their
absolute start time. Stop the app after experimentation to reclaim trace memory.

## Boundaries and record fields

All records contain host monotonic `ns`, integer `id` and `related`, event and
flags, and twelve double values. Integer handles use `related`, not float fields.

| Event | IDs / flags | Values |
|---|---|---|
| Sensor (1) | sample serial / device ns; accepted bit 0, new filter bit 1 | filtered wxyz, raw gyro xyz, raw accel xyz, host receive ns |
| Publish (2) | seqlock sequence / source sample serial; shared flags | rotation matrix (9), publication ns |
| Driver (3) | GetPose call serial / exact consumed sequence; fresh-read bit 0, valid bit 1, spatial override bit 2 | live wxyz, effective wxyz, publication ns, poseTimeOffset, synthetic evaluation ns, source serial |
| Sent (4) | GetPose call serial; valid bit 0 | exact callback wxyz and poseTimeOffset |
| Layer (5) | next Present ID / exact texture handle; flags `layerIndex*2+eye` | unmodified SubmitLayer mHmdPose (12) |
| Present (6) | Present ID / sync handle; begin 0, end 1 | layer count and approximate numeric texture handles (exact handles are in Layer) |
| Copy (7) | Present ID / stage-specific PID or swapchain index | VkResult |
| Vsync (8) | — | announced offset seconds, target monotonic ns, period ns |
| Throttle (9) | Present ID; begin 0, end 1 | target monotonic ns, frames to throttle |
| Recenter (10) | source sample serial, or zero for UI callback | — |
| Android (11) | Android render ID / swapchain index | generation index and VkResult |
| Commit (12) | commit serial / Wayland buffer resource ID | surface resource ID, client PID |

Copy stages: entry 0; lease begin/end 1/2; acquire begin/end 3/4; submit begin/end
5/6; fence complete 7; lease released 8; present begin/end 9/10. Android stages:
render entry 0, acquired 1, submit begin/end 2/3, present begin/end 4/5, fence
complete 6; vsync callback 7 uses `id` for Android frame-time ns.

Source identity follows the synchronous XREAL callback using native thread-local
state. Publication saves it in the first two formerly reserved headpose words,
inside the existing seqlock transaction. Re-publication of unchanged pose retains
its source identity. UI recentering has source zero and its own event. Shared pose
size/version remain 128 bytes/version 1, compatible with old readers.

## Interpretation limits

The analyzer checks q/-q equivalence, sequence reversals (including normal wrap),
exact publication/driver/callback joins, injected waveform error, angular steps,
arrival gaps, lease/copy/present durations, and render-matrix validity. An arrival
gap or a large angle is a candidate, not proof of the visible symptom. Initial
window edges can legitimately lack joins. Overflow, missing boundaries, or
invalid matrices make the corresponding exclusion inconclusive.

A valid varying mHmdPose must also respond to synthetic input before it can be
used as evidence of render-pose continuity. Fixed pose alone cannot exclude a
motion-dependent fault. CPU Present IDs do **not** identify the content generation
of copied pixels. Android commit and render IDs are local to those stages; no
cross-process pixel identity is claimed. If driver and render poses are smooth
while the wearer sees a snap, the next probe must identify source pixels through
copy/composition (or capture optical output), not another synchronization fix.

## Optional source-eye pixel probe

`capture.py arm --mode sine --trace on --pixels --seconds 25 --delay 10`
adds a 128×72 RGBA readback of the actual first left-eye texture and its submitted
crop. The driver adds the blit/readback to the existing copy command buffer,
while retaining producer ownership, and copies mapped bytes only after that
submission's existing GPU fence completes. No additional submission or fence
wait is introduced. GPU allocation/mapping occurs during warm-up; the controller
prefaults a separate 2,048-frame (about 72 MiB) buffer before acknowledging the run.
This is opt-in and adds GPU work and CPU copying; compare copy durations with the
pose-only baseline. It does not measure Android output or the optical display.

The exported `.pixels` file has a 128-byte header with magic `DDPIXELS`, version
1, record size 36,888, and the usual run/window/count/drop fields. Reserved words
2/3 hold width/height. Each record contains int64 host readback timestamp, uint64
Present ID, uint64 source texture handle, and 36,864 RGBA bytes. The pixels come
from the imported source texture; the Present ID labels the copy request, not
proof that the producer wrote the correct generation. Failed readbacks/overflow
count as drops. Files are exported only after capture.

```sh
python3 tools/motion/pixels.py /tmp/capture/FILE.pixels \
  --traces /tmp/capture/*.bin --output /tmp/capture/pixels.json \
  --images /tmp/capture/images
```

This validates capture compatibility, joins source handles to submitted layers,
and can export lossless PPM images. Equal hashes establish equal images only;
scene animation, low resolution, and stationary intervals limit interpretation.
Inspect image movement against the synthetic waveform before claiming the first
failing boundary. A smooth source readback still leaves Android composition and
physical presentation unverified.
