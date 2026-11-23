# VRCalibrationKit — Integration Guide

How to add marker calibration and anchor restore to another Unreal Engine VR project.

## 1. What it does

A seated VR experience often pairs a **physical mock-up** (a real seat, a buck) with a **virtual vehicle**. The real seat and the virtual seat must line up, or a user reaches for a virtual part that is not where their hand expects it.

The kit lines them up in two steps:

1. **Calibrate once** (when the mock-up is installed). Print four ArUco markers and place them on the mock-up at known points. The Quest 3 headset camera measures where they are. The kit saves the measured points.
2. **Restore every run.** At Play, the kit reads the saved points and places the VR pawn so the virtual vehicle sits on the mock-up. With anchors enabled, a spatial anchor saved during calibration corrects for the headset's tracking origin moving (a Link reconnect, the headset sleeping), so you do not have to calibrate again.

The result is **vehicle-relative**: the pawn is attached to the vehicle actor. A static vehicle and a vehicle that drives in VR use the same calibration. The physical mock-up never moves, so the calibration never changes.

Measured on Quest 3 + Link (one session): after a Link reconnect moved the tracking origin by 44.5 cm and 17°, the anchor restore matched a fresh marker measurement within 0.3 cm.

## 2. Requirements

| Item | Version / setting |
|---|---|
| Unreal Engine | 5.7 |
| Platform | Windows, PCVR over Meta Horizon Link. Quest standalone (Android) is not supported. |
| Headset | Meta Quest 3 (headset camera is used for markers) |
| Meta XR plugin | **1.205.0** (`OculusXR.uplugin`). Not included in this repository (Oculus SDK License). Install from Fab. |
| Engine plugins | OpenXR, XRBase, OpenCV (Win64; engine plugin, enabled by the kit) |
| Meta Horizon Link app | Settings → Beta: **Spatial Data over Meta Horizon Link** on (anchors). Passthrough over Link and the passthrough camera API permission on (markers). USB-C cable link. |
| Printer | Markers printed at 100% scale. The black square must measure 60 mm. |

The Interaction SDK (`MetaXRInteraction`) is **not** required.

## 3. Modules

| Module | Platforms | Contents | Depends on |
|---|---|---|---|
| `VRCalibrationCore` | all | `UVRCalAlignmentComponent`, `UVRCalAlignPointComponent`, point fit, save/load, anchor save and restore | Engine, Json, HeadMountedDisplay, XRBase, OculusXRAnchors |
| `VRCalibrationMarker` | Win64 | `AVRCalMarkerCalibrator`: headset camera, ArUco detection, measuring session | VRCalibrationCore, MRUtilityKit, OpenCV, RHI |

Restore-only builds need only `VRCalibrationCore`.

## 4. Add the kit to your project

1. **Copy plugins**
   - `Plugins/VRCalibrationKit/` from this repository.
   - Meta XR 1.205.0 into `Plugins/MetaXR/` (the folder contains `OculusXR.uplugin`). Keep its `Source/Thirdparty/**` libraries.
2. **Enable plugins** in your `.uproject`: `VRCalibrationKit`, `OpenXR`, `XRBase`. Meta XR (`OculusXR`) and OpenCV are pulled in by the kit's `.uplugin`.
3. **Project settings** (`Config/DefaultEngine.ini`):
   ```ini
   [/Script/OculusXRHMD.OculusXRHMDRuntimeSettings]
   XrApi=NativeOpenXR
   bAnchorSupportEnabled=True
   bPassthroughCameraAccessEnabled=True
   ```
4. **C++ projects:** if your own code calls the kit, add `VRCalibrationCore` (and `VRCalibrationMarker` on Win64) to your module's `Build.cs`. Blueprint-only use needs nothing here.
5. Regenerate project files and build the editor target.

## 5. Set up your vehicle and pawn

### Vehicle actor (your car Blueprint)
- Add **one** `VRCal Alignment Component` (`UVRCalAlignmentComponent`) to the vehicle actor.
- Add **four** `VRCal Align Point Component`s as children, at the physical points where the markers go (for example, four bolts on the seat frame). Set `Order` 1–4.
  - Marker ID = `FirstMarkerId + Order − 1`. With no buck profiles, IDs 1–4.
  - Points must span at least 20 cm (`PointMinSpanCm`).
- Set `FloorLocalZCm`: the physical floor height in the vehicle's local space (cm). The kit sets pawn height from the floor, not from the user's head, so tall and short users keep their real eye height.
- The vehicle **root scale must be 1**. Put any import scale on child meshes, not on the root.
- Local axes: +X forward, +Y right, +Z up, cm.
- Only one alignment component may exist in the level.

### Pawn
- Player 0's pawn is used. It must have a root component and a camera locked to the HMD.
- `bUseControllerRotationYaw` must be false.
- The kit **attaches the pawn to the vehicle**. A `Character` with movement fights the attachment; use a plain `Pawn`, or disable movement while seated.
- Hand tracking or other components on the pawn are fine. The kit only moves the pawn root.

### Tracking origin
Before the alignment component's `BeginPlay`, set:
- tracking origin = **Stage** (`UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(Stage)`)
- `WorldToMeters` = 100

The sample does this in `AVRCalSampleGameMode::StartPlay`. Copy that, or do the same in your GameMode. Without Stage, the component reports `NeedsCheck` and does not move the pawn.

## 6. Calibration level

Make a separate level for calibration (cameras run only there):
- Place your vehicle (same Blueprint as the main level).
- Place one `AVRCalMarkerCalibrator`. It finds the alignment component on its own.
- Set `MarkerArrowLocal` if the printed FRONT arrows do not point along the vehicle's +X.

Keys (keyboard on the PC):

| Key | Action |
|---|---|
| M | Start measuring (or cancel). Look at the markers until all four turn green. |
| G | Preview the fit (temporarily move the pawn) |
| H | Undo the preview |
| S | Save. With anchors on, wait for `anchor saved … created vs requested`. |

The calibrator turns off the MRUK World Lock while it runs and restores it at the end.

## 7. Anchors (recommended)

Off by default. Turn on per PC in `Config/UserEngine.ini` (ignored by Git):
```ini
[ConsoleVariables]
VRCal.AnchorAlignment=1
```
- On save, the kit creates a spatial anchor near the markers and stores where the anchor actually is.
- On Play, it waits until the anchor pose holds still for 1 s within 0.5 cm and 0.5° (`AnchorStable*` on the component), then remaps the saved points.
- If no stable anchor arrives within 10 s, it falls back to the saved points. Those are correct only if the tracking origin has not moved.
- Look toward the calibrated area at Play. The headset finds the anchor faster when it can see that part of the room.
- **Remove `UserEngine.ini` before running automated tests.** It overrides test defaults.

## 8. Saved data

| File | When |
|---|---|
| `Saved/Calibration/Alignment.json` | No buck profiles (default) |
| `Saved/Calibration/Bucks/<Name>.json` and `ActiveBuck.txt` | Buck profiles set on the component (`Bucks`) |

Version 2 JSON contains the measured points in Stage space (cm), the tracking origin, play-area size, and an optional anchor UUID and pose. Copy the `Saved/Calibration/` folder to move a calibration to another PC with the **same headset and room**. Anchors stay on the headset that made them.

## 9. Runtime state and events

`UVRCalAlignmentComponent` exposes, for Blueprint:
- `State`: `Applied`, `NoSavedValue`, `NeedsCheck`, and `Reason` (text).
- `OnRecord(Message)`: anchor notices (saving, saved, found with drift, not found). Use it to show status on an operator screen. Calibrator messages in the calibration level go to the screen and the output log.
- `RequestReapply()`: reload the saved setup. The sample binds this to **A**.

`NeedsCheck` is raised by: a recenter, a changed play area (guardian), a non-Stage origin, or saved points that no longer fit the vehicle (more than `PointFitLimitCm`, 2 cm).

## 10. Check that it works

1. Calibration level: M → S. Log: `Max XY … cm` (each point's fit error, below 2 cm) and `anchor saved`.
2. Main level: Play. Log: `anchor found … Stage drift since save …`. Drift is near 0 if the headset did not sleep.
3. Disconnect and reconnect Link. Open the calibration level and press **M only**. `Marker fit vs pawn` dX and dY should be within 2 cm. Do not press S, or you replace the anchor you are testing.

Log lines go to the output log (`LogTemp: Alignment …`) and to `OnRecord`.

## 11. Limits

- **Seated mock-up only.** The kit aligns a physical object that stays put. It does not track moving physical objects, and it does not work in a moving real vehicle (headset tracking breaks there).
- **VR driving** (a virtual vehicle moving while the user sits still) works through the attachment. Vehicle physics, input, and motion-sickness handling are not part of the kit. Pitch and roll of a simulated car body are passed on to the pawn as they are.
- PCVR over Link only. The anchor code reads the anchor pose in tracking space to avoid an editor-only Link path in Meta XR that mixes camera and head poses.
- Finding the anchor takes 4–9 s in testing.
- Not tested on device in this repository. Treat as work in progress.
