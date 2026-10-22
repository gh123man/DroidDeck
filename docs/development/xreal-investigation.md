# XREAL 1S / One Pro feasibility — 2026-10-14

The user owns XREAL 1S and also wants One Pro support. Supporting these in DroidDeck
is feasible based on existing open drivers, but the supplied Monado Air driver is
not their transport. The initial investigation below preceded implementation.
An experimental native 1S driver is now enabled; see the hardware validation
section at the end for what has actually been verified.

## Supplied Monado source

Inspected `C:/Users/conno/Downloads/monado-main/monado-main/src/xrt/drivers/xreal_air/`
and `src/xrt/targets/common/target_builder_xreal_air.c`.

| Model | USB vendor:product | Sensor/control interface | Sensor report |
| --- | --- | --- | --- |
| Air | 3318:0424 | 3 / 4 | 64 bytes |
| Air 2 | 3318:0428 | 3 / 4 | 64 bytes |
| Air 2 Pro | 3318:0432 | 3 / 4 | 64 bytes |
| Air 2 Ultra | 3318:0426 | 2 / 0 | 512 bytes |

This driver implements HID IMU input, segmented factory-calibration JSON reads,
gravity/gyro fusion, brightness 0–7 and 2D/3D mode commands. It does not register
1S or One Pro. Copying those Air HID commands to the new products is unjustified.
Its source carries Tobias Frisch's BSL-1.0 notice.

## Applicable One-family references

* [XRLinuxDriver](https://github.com/wheaney/XRLinuxDriver), inspected commit
  `36e2139dd482f5984112d0c3ebafcc07a78f99c0`. `src/devices/xreal.c` lists vendor
  `3318`, One Pro products `0435/0436`, and 1S products `043d/043e`. These use the
  non-HID transport. Its README explicitly lists both models and requires current
  firmware with glasses stabilization/anchor disabled when using external tracking.
* [xrealOneDeviceKit](https://github.com/wheaney/xrealOneDeviceKit), inspected commit
  `f5833f611265f418b1ea549da20dc4764d86a4ec`. Its IMU adapter consumes the One TCP
  driver, interprets gyro as radians/sec and acceleration as m/s², then feeds fusion.
  Its factory-calibration JSON hook returns unsupported. The maintained Linux
  integration disables its SBS-control capability when no MCU connection is open;
  a static model table alone is not proof of working display switching.
* [xreal_one_driver](https://github.com/rohitsangwan01/xreal_one_driver), MIT,
  inspected commit `3e912315ec16e94e9a47688770d649cfef293624`. Opens TCP
  `169.254.2.1:52998` with a two-second timeout, assembles 84-byte IMU records,
  reads a timestamp at byte 14, gyro floats at 34 and acceleration floats at 46.
  This is a transport/parser reference, not a drop-in Android glasses controller.

The source check confirms a practical path for both requested models. It does not
establish that this Samsung phone exposes the glasses' USB Ethernet interface to
ordinary Android apps, or establish the physical tracking axes on this hardware.

## DroidDeck integration path

1. Attach the 1S and inspect its USB descriptors, presentation display identity,
   Ethernet network/link properties and whether the IMU endpoint is reachable.
   Obtain the same evidence for One Pro before claiming hardware validation.
2. Add exact model discovery and external-display selection. Preserve each panel's
   Android-advertised dimensions and modes; do not assume the Air's resolution.
3. Implement a bounded One-family stream parser and cancellable socket transport.
   On Android, bind the socket to the relevant Ethernet `Network` when available,
   leaving Steam's Internet route alone. If Android does not expose that link,
   a USB network transport is additional work; no root requirement is assumed.
4. Establish gyro units, timestamp deltas and axis mapping using stationary,
   directed yaw, pitch and roll captures. Route the validated fused pose through
   the same glasses/HeadTracker/SteamVR pipeline as VITURE and RayNeo. Avoid layering
   DroidDeck tracking on top of the glasses' own anchor/stabilization.
5. Gate brightness and automatic stereo switching separately on a verified control
   protocol. IMU streaming alone does not justify showing those controls as working.
6. Test malformed/fragmented/coalesced records, missing network, timeout/reconnect,
   USB detach, screen handoff, SteamVR launch and each physical movement direction.

At this initial investigation stage, no external SDK was installed, firmware was
modified, or XREAL commands were sent.

## Further source investigation while the phone charges

Inspected [xr-tools](https://github.com/0xcaff/xr-tools), commit
`65c11105434a998f09ac4a5f8e4de8736bb4d36c`, particularly
`packages/xreal_one_driver/src/proto/net/`, `src/proto/usb/mod.rs`, and the IMU
example. This describes considerably more of the One protocol than the simple
TCP IMU reader above:

* Reports use TCP `169.254.2.1:52998`; controls use `169.254.2.1:52999`.
  Both have a two-byte message identifier followed by a four-byte **big-endian**
  body length. Control messages additionally contain a big-endian transaction ID
  and protobuf payload. A port must bound lengths, use unique transaction IDs,
  enforce deadlines, and close both sockets on detach.
* Sensor messages `28 36` have a **128-byte body**, hence 134 bytes including the
  header. The older reader's 84-byte extraction is not the complete frame format.
  Body offsets: device ID 0, device timestamp 8, report type 24, gyro 28,
  acceleration 40, magnetometer 52, temperature 64 and IMU ID 68. Numeric sensor
  fields are little-endian; timestamp is documented as nanoseconds, gyro rad/s,
  acceleration m/s². Type 11 is IMU; type 4 is magnetometer. Do not interpret every
  report as gyro data or use arrival time as the device's integration interval.
* Control `27 1f` reads factory JSON: IMU bias, temperature-dependent gyro bias,
  and display-to-IMU transforms. Its example subtracts the factory biases, fuses
  gyro/acceleration and applies the display rotation. This is preferable to
  guessing a fixed mounting angle, but the example's frame convention still
  needs conversion and directed-motion verification in DroidDeck.
* `27 1c` sets brightness 0–9; `27 27` sets dimming; `28 22` selects regular/SBS
  input; `27 5e/5f` reads/sets display configuration; `28 a6/a7` reads/sets EIS.
  Documented display configurations are 1920×1080 at 60/90/120 Hz and 3840×1080
  at 60 Hz. These are evidence for One/One Pro, not confirmed 1S modes. Preserve
  the Android-advertised modes and verify controls separately on 1S firmware.
* `28 29` is labelled buttons-enabled/buttons-disabled scene mode. Do not assume
  that command disables stabilization; the separate EIS control exists.
* This project's USB detection labels `3318:0436` One Pro and `3318:0438` One as
  normal devices, and `0435/0437` as **recovery** devices. XRLinuxDriver accepting
  both IDs does not justify opening recovery products as ordinary glasses or
  sending firmware commands. xr-tools does not register 1S; its applicability
  there is a protocol-family inference, not hardware validation.

No license file or Cargo license declaration was found in this inspected xr-tools
revision. No implementation was copied into DroidDeck. Use protocol observations
for an independently written adapter, or resolve source licensing before a port.
The MIT TCP driver and GPL XRLinuxDriver remain explicitly licensed references.

Also inspected XRLinuxDriver commit
[`05d0cfb`](https://github.com/wheaney/XRLinuxDriver/commit/05d0cfb86abaacbf073e1a285ad4eabf681ccb05).
It moves per-device pitch adjustment into the core pose path, after coordinate
conversion; it does not add an Android transport or new One control protocol.
Its pitch constants should not replace the GT mapping already verified by the user.

Next hardware check remains read-only: enumerate the 1S USB interfaces, display
and Ethernet link; attempt bounded reads from both TCP services, save the factory
configuration, then capture stationary/yaw/pitch/roll data. No XREAL support is
advertised in this build. Android Ethernet availability and 1S control compatibility
are still unverified.

## Official XREAL documentation and SDK inspection

Checked the user's [official documentation](https://docs.xreal.com/) on
2026-10-14. This establishes an additional integration candidate alongside the
direct TCP driver; it does not make that candidate a drop-in Android controller.

* The [device page](https://docs.xreal.com/XREALDevices/XREAL%20Glasses) explicitly
  includes One, One Pro and 1S in the X1-based One family with rotational tracking.
* [SDK 3.1.0 release notes](https://docs.xreal.com/Release%20Note/XREAL%20SDK%203.1.0)
  add positional tracking for the One series with XREAL Eye and updated firmware.
  Beam Pro and Samsung S25 are its listed tested hosts. This does not establish
  6DoF on a camera-less 1S or validation on the user's SM-F976U.
* [ControlGlasses](https://docs.xreal.com/Tools/ControlGlasses) is a required server
  for official SDK apps on compatible Android phones. It supplies device services,
  3D switching and dynamically loaded algorithm libraries. Beam Pro uses MyGlasses
  instead. An SDK integration would have a companion-app/version dependency.
* The [quickstart](https://docs.xreal.com/Getting%20Started%20with%20XREAL%20SDK)
  describes Unity XR Plugin integration, ARM64 and OpenGL ES3. The
  [dual-display guide](https://docs.xreal.com/Tools/Dual%20Screen%20Display)
  confirms separate phone/glasses views and continued glasses output while the
  phone runs another app. This demonstrates the desired behavior in the SDK;
  it does not itself supply a native Kotlin implementation.
* The [compatibility page](https://docs.xreal.com/XREALDevices/Compatibility)
  currently flags unresolved Android 16 compatibility. The
  [FAQ](https://docs.xreal.com/Frequently%20Asked%20Questions) also says DeX can
  interfere with SDK display operation, and Android 16 USB permission may require
  camera permission first when the device exposes an enabled UVC interface. Apply
  that permission requirement only when relevant; ordinary tracking must not
  start camera capture or enable UVC automatically.

Downloaded, but did not install or bundle, the official release archive linked
from the 3.1.0 release notes:
`https://public-resource.xreal.com/download/XREALSDK_Release_3.1.0.20251125/com.xreal.xr.tar.gz`.
SHA-256: `fd7d0fce103712e7acc85bfcbb8b127007e1d84f64596efb6c11580bea058d90`.
Size: 248,169,074 bytes.

Package inspection found Android AARs including `nr_api` and `nr_loader`, ARM64
`libXREALXRPlugin.so` / `libXREALNativeSessionManager.so`, and Java
`com.xreal.sdk.display.GlassesDisplay` plus display/surface listeners. The C# layer
calls native exports for tracking type, resolution, refresh rate, component poses
and session lifecycle; the loader also integrates Unity subsystems. Presence of
these libraries does not establish a supported standalone JNI API or permission
to redistribute a stripped subset. No C headers were present in the archive file
listing inspected. No SDK source or binaries were added to the app.

Decision: retain both candidates. Before choosing the official native-library
route, establish non-Unity initialization, head-pose access, ControlGlasses
compatibility, display-surface ownership and distribution terms. Before choosing
direct TCP, establish the Android network link and 1S protocol behavior. Compare
both against DroidDeck's existing compositor instead of replacing its rendering
architecture merely to fit the SDK's Unity sample.

## Native 1S implementation and hardware validation

The pre-implementation checkpoint is `4b045519`. The new driver uses independently
written Kotlin TCP framing, control transactions, sensor parsing and gyro/gravity
fusion. It does not bundle XREAL SDK binaries or copy the unlicensed xr-tools
source. Protocol observations came from the references above and
[xr-tools](https://github.com/0xcaff/xr-tools), inspected commit
`65c11105434a998f09ac4a5f8e4de8736bb4d36c`.

Verified on the connected SM-F976U (Android reports release 17):

* Normal 1S USB identity is `3318:043e`, product name `XREAL 1S`. Only that exact
  product enables this driver; Air, One Pro and alternate/recovery IDs are excluded.
* Android exposes NCM/ECM Ethernet networks at `169.254.2.10/24` and
  `169.254.1.10/24`. Individual sockets bound to the first network reach the
  glasses at `169.254.2.1`, control port 52999 and report port 52998. The process
  default network remains unchanged, preserving Steam's Internet connectivity.
* Requests require the high bit of the transaction ID; responses clear that bit.
  Replies must match both command and transaction before values are published.
* GET_DISPLAY returned payload `22 02 10 1d`: mode **29**, with Android reporting
  **1920 x 1200 at 60 Hz**. This differs from the older One's mode 2 (1080 lines).
  The 1S default 60 Hz mapping preserves mode 29.
* A two-second capture delivered **2,000 valid IMU poses (1,000 Hz)**. Reports
  use the 128-byte body, type 11, little-endian device nanoseconds and float
  radians/sec / m/s² format. The framing handles partial and coalesced TCP reads.

The driver plugs into the shared glasses display and head-pose paths, including
SteamVR. The FOV calculation now uses the actual per-eye aspect ratio, including
the 1S's 16:10 display. Controls use acknowledged commands, bounded queues and
timeouts; connection failures clear tracking state and retry. Volume is hidden
because no volume protocol has been established. Missing Ethernet is reported
without requesting HID permission or claiming USB interfaces from Android.

Directed wearer captures on the same device established the 1S sensor basis:
level acceleration is approximately `(0, -9.65, -1.07)` m/s²; turning left gives
negative gyro Y; looking up gives positive gyro X; tilting toward the right
shoulder gives positive gyro Z with negative acceleration X. Both vectors are
mapped as `(x, -y, -z)` into the shared right/up/back basis before initial gravity
alignment and fusion. No RayNeo GT mounting angle is reused. Regression tests
exercise those measured signs through `HeadPose`, check an upright first pose
before recenter, and preserve the device's radians/sec and nanosecond units.

Stereo switching was verified at 3840 x 1080 / 60 Hz, returning to native
1920 x 1200. On 1S, SET_INPUT performs the EDID switch itself; immediately sending
SET_DISPLAY returns busy status 5001 (`22 03 08 89 27`). The driver now polls
GET_DISPLAY across the temporary empty reply (`22 00`) and only sends a second
mode command if the settled mode differs. Hardware retesting kept the connection
and sensor stream running through the switch. The busy status is never accepted
as a successful acknowledgement.

After installing the corrected build on 2026-10-14, the wearer confirmed that
the view and motion feel good in response to the upright-view, yaw, pitch and
head-tilt check. The SteamVR-requested session was running with a rendered first
frame, stereo output and active tracking. All 181 glasses, shared-pose, SteamVR
launch and glasses-UI regression checks passed; packaged runtime inputs verified.

The wearer also confirmed on 2026-10-14 that XREAL 1S brightness control works.
Higher refresh rates were not tested.

Still experimental: dimmer, 90/120 Hz compatibility modes and
extended gameplay stability need hardware validation. One successful view/motion
check does not establish long-session stability. Users must select Follow mode and disable the glasses' own
stabilization. Factory calibration and XREAL Eye/6DoF are not implemented.
One Pro is not enabled or claimed as tested.

## XREAL One test adapter (2026-10-21)

A separate experimental profile now recognizes normal One `3318:0438`; recovery `0437` remains rejected. One uses the manufacturer's 50° FOV and firmware-native 2D, while 1S keeps 52° and mode 29. Family-based routing shares the TCP connection without requesting Android USB-interface access. Outgoing mode requests are checked against the selected profile before any control writes.

The existing 1S sensor basis/fusion is reused. Initial One / Retroid Pocket 6 hardware validation is recorded below; long-session stability and quantitative axis/drift validation remain open. One brightness and dimming are disabled pending verification, and Eye/6DoF remains unsupported. See the [test checklist](../smart-glasses-support.md#xreal-one-test-build).

### Connection and stereo-switching regression fixes

Investigation of a One / Retroid Pocket 6 connection-error report found that the
startup display query rejected both temporary empty properties and numeric modes
outside the outgoing command whitelist. It now polls temporary empty properties
with a bounded timeout and accepts reported numeric modes without authorizing
those modes for writes. Malformed replies and device errors still fail.

The original switching sequence also waited for SET_INPUT to change the EDID
before allowing SET_DISPLAY. One now permits an explicit SBS EDID request when
the reported mode differs from the target. Busy status 5001 causes a bounded retry;
an acknowledged write is not repeated, and success still requires GET_DISPLAY
to report the target. The 1S path retains its captured SET_INPUT-driven stereo
transition. Android's physical display confirmation remains separate.

Focused JVM tests reproduce the previous failures and cover startup, unmanaged
mode reads, explicit SBS/native switching, busy replies, rejection, and stalled
transitions.

Live investigation on a Retroid Pocket 6 running Android 13 confirmed the startup
failure: GET_DISPLAY returned `22 02 10 09`, which the old whitelist rejected.
Android exposed both Ethernet interfaces, including the direct control route at
`169.254.2.10/24`. A diagnostic using the corrected protocol accepted mode 9 and
read 2,006 valid IMU reports in about two seconds. An SBS request returned mode 5,
and Android confirmed 3840×1080 at 60 Hz with the sensor stream still active.

Returning to regular input restored mode 9 (1920×1080 at 90 Hz). Sending the
legacy mode-2 EDID afterward failed with `22 03 08 91 4e` (10001). One therefore
uses SET_INPUT to restore firmware-native 2D instead of writing legacy 2/3/4
EDIDs; Android selects from the resulting advertised refresh rates. A regression
test covers this captured native-mode response and ensures no legacy EDID is
written. The 1S behavior is unchanged. These protocol checks alone do not
establish correct tracking axes, a successful SteamVR session, or long-session
stability.

### Wearer validation and remaining frame-pacing issue

The diagnostic XR Dev APK identified in
[`2026-10-21-xreal-one-retroid-debug.json`](deployments/2026-10-21-xreal-one-retroid-debug.json)
was installed after its source and deployment record were pushed. Settings then
reported connected/control-ready with no communication error. SteamVR reached
mode 5, physical 3840×1080/60 Hz, with tracking active. The wearer confirmed a VR
image and working head tracking, but reported stutters/jumps. This is functional
VR validation, not a claim that motion is smooth.

A two-second in-app capture counted 2,000 poses (999.5 Hz). The read-only
`tools/xreal/ReportTiming.kt` probe subsequently measured 5,014 IMU samples in five
seconds during VR: arrival gaps were 0.97 ms median, 3.51 ms p99, and 6.40 ms
maximum, with no gaps over 10 ms. A separate TCP_QUICKACK comparison did not
improve those figures. These independent socket captures do not measure
motion-to-photon latency or exclude pauses in downstream pose delivery.

Android's external-display presentation history contained 122 intervals near
16.7 ms and three near 33.4 ms. The app's compositor logged roughly 58–60 fps;
SteamVR's presenter logged about 60 fps and occasional compositor
`WaitForAcquire timed out` warnings. The driver initially advertised 120 Hz before
the automatic SBS switch updated its reported panel rate to 60 Hz; SteamVR's
startup display log selected 120 Hz. These are leads for further frame-pacing
investigation, not a proven cause of the wearer's stutter. Sensor filtering and
TCP acknowledgement behavior were not changed on that evidence.

### Intermittent snapping and hot-path audit

The wearer subsequently reported brief smooth periods followed by snapping,
including with Follow mode selected and the glasses' Stabilizer disabled.
Starting SteamVR after SBS was established produced a confirmed 60 Hz startup,
but did not consistently resolve the symptom.

The installed driver now retains the last valid head pose when a seqlock read
collides with the writer. The regression reproduces the previous neutral-pose
snap, but wearer testing shows this was not the only issue.

A later shared-pose capture included head sweeps while the wearer reported
snapping. In its first three ten-second segments the maximum orientation steps
were 2.00°, 0.45°, and 0.71°, with maximum publication gaps of 21.09, 14.01, and
15.05 ms. Tracking flags remained enabled/live. This does not measure the
displayed frame's age, but did not reproduce a large pose reset. The XREAL filter
also rejects non-increasing sensor timestamps.

No per-sample logging runs in the XREAL path. Semaphore tracing was disabled;
routine presenter/compositor statistics run every ten seconds. The driver did
retry an absent Quest tracking file on every pose read: failed opens now back
off for 250 ms, while mapped tracking reads remain unrestricted. A regression
covers a file appearing during backoff and subsequent immediate pose updates.

Rendering remains a stronger lead: the Android compositor recorded buffer
release work up to 32.47 ms and composition up to 19.45 ms against a 16.7 ms
display period. The glasses presenter also returned from `Submit` before its
worker finished copying the submitted eye textures. It now waits for completion,
as the Quest frame path already did, and wakes a waiting submitter on shutdown.
The concurrency regression fails with the former asynchronous flat path and
passes with the wait. This closes an early-return window for texture reuse;
hardware effectiveness and the separate SteamVR sync-texture warnings still
require validation. Neither the earlier transient smooth period nor the unit
test establishes that intermittent snapping is fixed.

The copy-completion diagnostic was pushed and installed using
[`2026-10-21-xreal-present-completion.json`](deployments/2026-10-21-xreal-present-completion.json).
The staged driver hash matched the record. Session `2026-10-21-12-steam` sustained
597–601 presented frames per ten seconds after startup, but SteamVR still reported
frequent sync-texture acquisition timeouts (over 1,100 suppressed warnings per
30-second interval). The wearer confirmed that snapping was unchanged. This
diagnostic did not resolve the issue. A debug/release hypothesis was considered
but deferred in favor of investigating frame synchronization; no non-debuggable
APK was installed.

### Retained producer ownership for glasses frames

The global submit counter only waits for preceding work; it does not prevent
later SteamVR submissions from overwriting the eye textures during a copy. The
glasses path now uses the existing Quest producer lease: the Vulkan layer drains
the producer's recorded queues, retains its submission lock across the copy, and
releases it only after the presenter's GPU fence completes. Unavailable leases
skip the frame. Failed copies stop presentation instead of showing a potentially
incomplete image. The fixed-delay/global-counter fallback is bypassed when the
lease is enabled. An explicit diagnostic override of the lease flag is preserved.

[Linux CI](https://github.com/gh123man/DroidDeck/actions/runs/39677373599) passed
21 launcher/producer tests, including a real Vulkan delayed-completion and frozen
overwrite test using Lavapipe. The actual presenter with mocked Vulkan calls
also verifies retained ownership, missing-lease rejection, and failed-copy
rejection. Presenter completion, fence tracking, compositor submission, and
semaphore bridge regressions passed. The driver and lease regression cross-compile
for ARM64 Linux. The hardware diagnostic is identified by
[`2026-10-21-xreal-producer-lease.json`](deployments/2026-10-21-xreal-producer-lease.json);
the installed Vulkan layer already contains the lease server, so only the driver
and launcher helper assets change. Hardware effectiveness remains unconfirmed.

The diagnostic APK installed successfully after deployment-record commit
`4dc28f7e` was pushed. Android then reported no attached glasses and the session
was idle. The on-disk runtime still contained the preceding driver/helper, which
are staged from APK assets when a new session starts. Runtime hash verification,
lease activity, and sustained wearer comparison remain pending reconnection.

After wireless ADB reconnected, session `2026-10-21-13-steam` started with SBS
already established at 3840×1080/60 Hz. Both runtime hashes matched the producer
lease deployment record. The lease activation log and producer socket were
present. After startup the presenter showed 553–569 frames per ten seconds,
with zero lease misses and maximum acquisition times of 9.4–12.3 ms. The wearer
reported improvement but continued snapping. SteamVR still reported roughly
1,400–1,500 sync-texture acquisition timeouts per 30 seconds. Retaining producer
ownership is therefore not a complete fix and reduced throughput relative to the
preceding approximately 60 fps diagnostic.

A subsequent audit found that the presenter reused one binary presentation
semaphore across every swapchain image. Its copy fence does not establish that
presentation has consumed that semaphore. Following the
[Vulkan semaphore reuse guidance](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html),
the presenter now allocates one semaphore per swapchain image and selects it by
the acquired image index. The regression keeps presentation waits pending after
copy completion and exercises non-sequential image acquisition. Partial semaphore
allocation failure also releases only successfully created resources. This
corrects a separate synchronization violation; hardware impact remains untested.

The per-image driver semaphore diagnostic was installed after deployment record
`ae0d1383` was pushed. Session `2026-10-21-14-steam` verified driver SHA-256
`5fcc3e6a2f52c7609a6810bac6e6b6555e87abc56f06a9b6d7edc707d6bfba23`.
The wearer reported a substantial further improvement, but clarified that abrupt
snaps remained. Presenter intervals still contained about 562–573 frames per ten
seconds. One SurfaceFlinger sample had 123 display intervals near 16.7 ms and two
near 33.4 ms; those missed refreshes alone do not explain backward orientation
snaps. The Android compositor was then found to reuse its own present semaphores
by submission slot, independently of swapchain image ownership. That downstream
path is being corrected and validated separately; its hardware effect is pending.

The Android compositor correction passed [Linux tests and a full Android native
compositor build](https://github.com/gh123man/DroidDeck/actions/runs/39690167782).
The prepared diagnostic is recorded in
[`2026-10-21-xreal-android-semaphores.json`](deployments/2026-10-21-xreal-android-semaphores.json).
Only the Android compositor library changes from the installed diagnostic; the
Linux presenter and Java/Dex payload are unchanged. Deployment record `a811f967`
was pushed, but wireless ADB disconnected before installation. The last connection
port refused reconnect, so this third synchronization diagnostic is not yet
installed or hardware-validated.


### Investigation reset — 2026-10-22

The wearer now reports that snapping is unchanged overall and that the experiments
have not produced a notable, durable improvement. Earlier reports of improvement
must not be treated as demonstrated efficacy or proof of a synchronization cause.
The original connection/SBS fix remains functionally validated; the motion problem
remains unresolved. Code-level regressions establish specific defects, not their
causal contribution to the reported symptom.

The Android compositor diagnostic was installed on October 22. Session
`2026-10-22-01-steam` reached READY with stereo established at 3840×1080/60 Hz.
The extracted and process-mapped compositor matched SHA-256
`d5b24a67e4319707c0cf0e465d46c1094fafd7dd0c80fbd8573abaa714d06d50`;
the Linux driver remained
`5fcc3e6a2f52c7609a6810bac6e6b6555e87abc56f06a9b6d7edc707d6bfba23`.
A post-startup interval showed 573 frames per ten seconds and no lease misses.
This is installation/runtime verification, not evidence that snapping improved.

#### What the existing measurements do and do not establish

- The separate report-socket probe found short arrival gaps during a short capture.
  It did not observe the application's own receive/filter pipeline end to end.
- Shared-pose captures did not show a large orientation reset in the sampled
  windows. They did not record the exact pose supplied by `Hmd::GetPose` or
  `TrackedDevicePoseUpdated`, nor identify a specific displayed snap.
- Presenter and compositor rates count different stages. Ten-second averages
  conceal bursty delays, stale images, repeats, and backward image ordering.
- SurfaceFlinger intervals describe display-buffer presentation timing, not which
  head pose or source texture generation the pixels represent.
- SteamVR sync-texture warnings and tested semaphore defects are leads. Neither
  has been correlated to an observed snap. Leases also reduced presenter
  throughput, so they cannot be assumed to be an unconditional improvement.
- No controlled debug/release comparison was performed. Build mode, thermal load,
  scene content, and session restarts were not systematically controlled.

#### Measurement-first protocol

Freeze the installed artifact and settings as a named baseline. Make the next
build diagnostic-only, with the probes switchable in the same binary. Do not
change filtering, prediction, throttling, or synchronization alongside probes.
Record scene, display mode, software hashes, thermal state, and capture interval.

Start with correlated pose and frame-request records:

1. At the actual XREAL receive/filter path: sensor timestamp, host receive time,
   accepted/rejected sample identity, filtered orientation, and filter/recenter
   reset events. Preserve sample identity through shared-pose publication.
2. At the driver: the shared sequence actually consumed, read/cache outcome,
   orientation, validity, timestamp/poseTimeOffset, and the exact pose submitted
   to SteamVR. The current glasses path sets poseTimeOffset to zero; its effect
   is an untested timing hypothesis, not a proven fix.
3. At SubmitLayer/Present: frame-request ID, source texture handles, submitted HMD
   matrices, layer count, and timestamps. Verify that the runtime supplies useful
   HMD matrices on this path before relying on them as rendered-pose evidence.
4. At the presenter: lease acquire/release, copy submission/fence completion,
   swapchain image index, and present call/result. Record scheduled vsync and
   PostPresent wait intervals to expose deadlines and stalls hidden by averages.

Use preallocated, prefaulted binary rings with nonblocking per-thread writes,
explicit overflow counters, and export after capture. Avoid per-sample formatting,
file flushes, allocation, or new blocking locks. Validate clock domains, including
sensor-to-host offset/drift; host monotonic timestamps should not be compared to
sensor ticks without that mapping. Measure probe overhead with tracing off/on.
Test the analyzer with injected pose resets, delayed samples, and reordered IDs;
handle quaternion sign equivalence so q/-q is not mistaken for a snap.

Use one unchanged SteamVR scene and a repeatable synthetic yaw trajectory injected
at the driver pose boundary. Compare live input, fixed pose, and synthetic motion
in the same diagnostic build. A fixed pose alone is insufficient: it can hide
motion-dependent image-ordering errors. Synthetic motion that reproduces the snap
would show that live glasses motion is not necessary for that reproduction; a
clean synthetic run alone would not prove the rendering path correct under every
live-input timing pattern.

If exact driver poses jump, trace upstream to sample/filter/publication timing.
If driver poses are smooth but trustworthy submitted render poses jump, investigate
SteamVR pose interpretation, prediction, timing, and coordinate conversion. If
both are smooth while the visible scene jumps, trace image content and presentation
rather than adding sensor smoothing.

For the latter branch, carry buffer identities into Android commit/composition
and correlate them with actual presentation feedback. A Present ID labels a CPU
request, not the contents of a reused source texture: monotonic Present IDs alone
cannot disprove stale or reordered imagery. Use a diagnostic source-content marker
or deterministic rendered pattern, preserve it through GPU copying, and validate
its sequence at the output. A marker painted only after copying would not prove
source-texture freshness. Software presentation feedback also does not prove the
optical output of the glasses; if software stages remain correct, a through-lens
observation is a separate final boundary test.

Make a targeted fix only after finding the first failing boundary. Compare it
against the instrumented baseline with interleaved repeated runs and an objective
failure count/trace signature. Keep a change for this symptom only if that signature
improves reproducibly without unacceptable latency or throughput cost. Investigate
debug/release or CPU/GPU scheduling if traces show workload-dependent stalls; do
not use it as another unmeasured build swap.

#### Controlled synthetic results, 2026-10-22

Installed diagnostic APK SHA256
`d5f182aa3bef6eb104a78247e85ddaf77e4cfd567b3b9f8aa0482832e2b5eef7`,
source `0d20c7064bc4b57bc6766160bfa81ffaef8375eb`, driver
`28e81d15cf3a40137793240eccfd60589cbe5cd9847e0ce665f6906fb315f325`.
Retroid Pocket 6, XREAL One, SBS 3840×1080 at 60 Hz, Follow mode and Stabilizer off,
same SteamVR session. Each capture window was 25 seconds.

| Run | Input | Per-frame trace | Wearer report |
|---|---|---|---|
| 1792678252796 | live slow left/right turns | on | abrupt snapping |
| 1792678359490 | ±30° sine yaw, 8-second period, head still | on | abrupt snapping during sweep |
| 1792678593616 | identical sine yaw, head still | off | still snaps |

Both recorded runs exported all boundaries with zero drops and no recenter/reset
or sequence reversal. In the synthetic run, all 10,302 driver samples matched the
analytic waveform, and the actual callback poses matched those samples. All 1,438
first-left SubmitLayer matrices were valid and varied with the synthetic sweep.
Layer inter-arrival median was 16.676 ms, p99 33.400 ms, maximum 34.651 ms; maximum
angular step was 0.806°, within the waveform speed envelope for those intervals.
The analyzer’s reproducible yaw fit found about 2 ms age relative to submission, median absolute
residual 0.00881°, maximum 0.18792°, with no backward steps when the expected
step exceeded 0.1°. This checks metadata, not actual rendered image content.

The reproduction does not require live sensor input or per-frame trace recording.
It does not establish that trace recording has no performance cost. The first
observed discontinuity remains the wearer's visual report; recorded pose boundaries
have not reproduced it. Stop changing sensor filters and synchronization until
actual image content is measured. The next opt-in probe reads the actual left-eye
source pixels in the driver's existing GPU copy, joined to SubmitLayer handles.
Compare its added GPU cost with the pose-only run before interpreting results.
Raw captures currently reside in `/tmp/droiddeck-motion/{live-1,sine-1,sine-off-1}`
on the development Mac and are not committed to the repository.

#### First measured image discontinuity: imported source-eye copy

Source-pixel diagnostic APK
`2ec89adba9b12d822865b109c7688b60b5e1aa66114efa20f0c74e7d5dc05429`
was installed after its deployment record was pushed. Installed APK, Android
library, and staged/mapped driver were verified. Final-source CI logs confirm
35 Python checks and native regressions passed despite stale queued API status.
The readback probe adds observation only; synchronization/filtering are unchanged.

Run `1792680393015`: synthetic ±30° yaw/8 seconds, 25-second window, SBS60,
SteamVR desktop/dashboard scene. Captured 1,391 actual 128×72 RGBA source-eye
images with zero drops. 1,390 join to first-left SubmitLayer records; the initial
image's layer call precedes the window. All repeated-image examples join exactly.
The driver waveform is exact; render poses have zero backward steps, median
waveform residual 0.00764°, maximum 0.26380°.

**163 images repeat every RGBA byte of the image exactly three Present IDs
earlier, always on the same texture handle.** There are no adjacent duplicates.
116 repeats occur with more than 0.5° change in submitted yaw. Repeat age spans
38.62–88.61 ms. This is exact content equality, not an optical-flow estimate.

| Present ID | Submitted yaw | Pixel contents equal Present ID | Content age |
|---|---:|---:|---:|
| 4298 | 4.11310° | 4295 (yaw 2.53422°) | 53.38 ms |
| 4299 | 4.50528° | 4296 (yaw 3.32822°) | 42.75 ms |
| 4300 | 4.89782° | 4297 (yaw 3.72028°) | 41.23 ms |

Frames 4295–4297 contain different images. Replaying that sequence at 4298–4300
moves the image backward while the pose continues forward. The first such repeat
is at 0.17833 seconds. The recorder uses one GPU capture image/readback buffer;
the three-frame pattern follows reuse of the source texture set. SHA256 values,
handles, all repeat pairs, and trace summaries are preserved in
`xreal-source-pixel-results-2026-10-22.json`. Raw images remain local under
`/tmp/droiddeck-motion/pixels-1`; they contain desktop UI and are not committed.

This localizes the earliest **measured** discontinuity to the imported eye-image
copy boundary, before Android composition/presentation. It does not yet distinguish
an unwritten/newly selected texture, stale producer input, or external-memory
visibility. The producer lease waits for already submitted work and freezes later
submissions; it does not identify which generation of an eye image was written.
Do not infer that a completed lease proves the expected texture generation exists.
The next discriminating observation is a per-texture producer write/submission and
completion generation correlated with these readback repeat IDs, not another
sensor filter or Android semaphore adjustment.

Limits: the wearer confirmed abrupt snaps during this pixel run, and the
exact content signature has been measured in one run. The probe adds GPU work.
Copy-fence p50/p99 were 2.365/7.226 ms versus 2.718/6.504 ms in the earlier
pose-only synthetic run; frame count fell from 1,438 to 1,391 across sessions.
That comparison is not an isolated overhead measurement. Prior synthetic motion
with all per-frame recording off still reproduced the visible symptom, but does
not alone prove identical internal cause. Preserve these distinctions in the PR.


### Producer write probe: coverage preflight and first capture

Installed source `e6d64a3f476aa6961340b4c56f054b380a882880` under deployment
`2026-10-22-xreal-producer-writes.json`. The one-second live preflight
`1792682773281` matched FD identities between producer and importer, but reported
4,954 historical coverage gaps. Synthetic run `1792683019511` captured 1,442
source images with no recorder drops, including 232 exact prior-image repeats.
Of the 227 repeats joined to consecutive uses of the same FD and both lease
snapshots, all had unchanged recorded producer write serials. There were 1,204
changed-image pairs with changed serials; eight frames lacked lease snapshots.

These are observations, **not an exclusion of unrecorded writes**: coverage gaps
increased from 18,813 to 21,069 during the run, and the strict analyzer marked all
frames inconclusive. Wearer confirmation for this first sweep was not obtained;
the user requested a repeat. Do not classify these missing records as absent GPU
writes or blame imported-memory visibility.

The next recorder revision adds per-command-buffer coverage certificates and
separate metadata/recording/submission/lease gap counters. It records a summary
for every submitted command buffer even when the diagnostic lock is unavailable.
Only a successfully ended recording with an unchanged recording gap epoch can be
complete. Object-mapping gaps remain disqualifying. The analyzer requires all
successful command buffers and declared writes between the two lease snapshots;
it can therefore use fully observed intervals after unrelated earlier command
recording gaps. Lease snapshots use a compact memory list and emit outside the
metadata lock to reduce the recorder's own contention. No GPU waits, barriers,
submissions, producer lease behavior, or rendering behavior changed.


The user-requested repeat `1792683529308` reproduced visible snapping. It captured
1,343 source images with zero dropped records, including 175 exact older-image
repeats and no adjacent duplicates. The 169 repeats with both lease snapshots
again had unchanged observed write serials; all 1,165 changed-image pairs had
changed serials. Six frames lacked snapshots. Sixty repeated-image cases have a
positively recorded next write attempt after lease READY and before the driver's
GPU-copy-completion event (stage 7). This establishes that these draw attempts
arrived while the lease prevented their submission; it does not exclude other
unrecorded writes. The old recorder still marks every interval inconclusive.
Hashes, observed timelines, and explicit limitations are saved in
`xreal-producer-results-2026-10-22.json`; raw captures remain local.


### Covered producer intervals: the copy precedes the next texture write

Source `15fb4ec7ae74ef0a9cdd48110da7d0b501cb2a2d`, pinned in
`deployments/2026-10-22-xreal-producer-coverage.json`, passed Linux/native and
Android CI run 39806904556. Installed APK SHA256
`09195fb27dd7bf5e6d047998561fc8cd383173d94bcd491f9a4472e3aa68546a`
and staged layer/driver hashes match. Only the Vulkan-layer asset changed.
No GPU synchronization or rendering behavior was changed.

Synthetic run **1792684243897** captured 1,313 source images, all 24 event streams,
and zero dropped/unregistered records. The wearer confirmed snapping. There
were 178 exact older-image replays, no adjacent duplicates, and zero lost object
mappings. Of 1,312 submitted command buffers, 1,095 have complete recording
coverage; 217 do not. The analyzer rejects every affected texture interval,
retaining **119 fully covered replay events** and 906 changed-image intervals.
The other 59 replay events remain inconclusive.

In **all 119 covered replay events**, the completed producer write serial is
unchanged since the preceding use of that FD. The next draw to the same texture
is submitted only **after the driver's GPU-copy completion event**. In 71 cases
its attempt was observed before lease READY; in 34 it arrived between READY and
copy completion. The other 14 lack the attempt event, but retain the successful
submission event after the copy. All observed writes to the eye images are draw
commands. The eye-image creation contract has color-attachment and transfer
usage, without storage-image usage. There are **zero** covered old-image replays
after a new completed write, and **zero** covered changed-image intervals without
a new completed write. Eighty-four covered replays occur despite more than 0.5°
change in submitted yaw.

For example, frame 7076 repeats frame 7073 byte-for-byte, 60.62 ms later, despite
1.548° of submitted yaw advance. Both lease snapshots identify FD inode 17643,
device 9, last write serial 7096. The next write (7099) is attempted at
80388305621988 ns. Lease READY follows at 80388305808654 ns; the driver's GPU copy
finishes at 80388309493602 ns; write 7099 returns successfully only at
80388309869279 ns. The existing submission lock is held throughout READY/copy,
so this is work that has not entered the producer queue before the copy, rather
than a new write that has finished but remains invisible to the importer.

This identifies an early-copy / texture-generation handoff failure for these
replays: the lease drains already submitted work, then freezes submission before
the next write to the selected texture has occurred. Waiting for the importer
copy to finish cannot make that texture current. A future fix needs to identify
and wait for the required producer generation **before** retaining the lease,
then prove that the stale-image sequence disappears with the same probe.
`Present(sync)` currently records but does not wait on its sync handle; whether
that handle can provide the required generation ordering needs investigation.
No synchronization change is included in this step.

There is also one separate submitted-pose backward step at frame 7474 (7.665 s):
yaw moves from -8.296° to -9.578° and then resumes its forward sweep. That frame
is not one of the 119 covered pixel replays. Synthetic driver poses remain exact;
all layer matrices are valid, but this run must not be described as entirely
smooth at the layer-metadata boundary. It does not explain the repeated texture
pattern and remains an additional issue to investigate.

Full classification, exact timelines, capture hashes, and limitations are in
`xreal-producer-coverage-results-2026-10-22.json`. Raw data remains local under
`/tmp/droiddeck-motion/producer-v2`. The result rules in early copying for these
events; it does not prove that no independent importer bug exists. Instrumentation
has overhead, and unsupported extension writes/unusual memory aliases are outside
the monitored path. The automatic sweep ended and tracking returned to live.
