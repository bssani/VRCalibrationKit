# VRCalibrationKit

Unreal Engine 5.7 plugin for aligning a virtual vehicle (or any rig) to a physical mock-up in VR.

- **Marker calibration:** printed ArUco markers on the physical mock-up, measured with the Quest 3 headset camera.
- **Anchor restore:** a spatial anchor saved with the calibration restores the alignment after Play restarts or the Link connection drops.
- **Vehicle-relative result:** the VR pawn is attached to the vehicle actor, so the same calibration works for a static vehicle and for a vehicle that drives in VR.

Status: device-tested once on Quest 3 + Link (marker calibration, anchor restore after Link reconnect within 0.6 cm). Reboot and next-day restore not yet tested.

See the [Integration Guide](Docs/Integration-Guide.md) to add the kit to another VR project.

## Requirements

- Unreal Engine 5.7, Windows (PCVR over Meta Horizon Link)
- Meta Quest 3
- **Meta XR plugin 1.205.0** — not included in this repository (Oculus SDK License). Install it from Fab and copy it to `Plugins/MetaXR/`.
- Meta Horizon Link: enable *Spatial Data over Meta Horizon Link* and passthrough camera access.

## Open the sample

1. Install Meta XR **1.205.0 for UE 5.7** in `Plugins/MetaXR/` (the folder must contain `OculusXR.uplugin`). Keep its third-party libraries. This folder is ignored by Git. The Interaction SDK is not needed.
2. Generate project files and build `VRCalibrationKitEditor` in Development / Win64. Open `VRCalibrationKit.uproject`.
3. Open `Content/Samples/L_Calibrate`. Use VR Preview with Quest 3 over Link. Print [markers 1–4](Docs/Markers/markers_aruco4x4_ID1-4.pdf) at 100% scale; the black square is **60 mm**. Put their centers at the car's four alignment points, with FRONT arrows along the car's local +X direction. Point positions and floor height are editable on `BP_SampleCar`.
4. Press **M** to measure (or cancel), **G** to preview, **H** to restore the previous pose, **S** to save. Check the camera-model message, fit residuals and `Marker fit vs pawn` log. Cameras run only in the calibration level, and image processing stays on the PC.
5. Open `Content/Samples/L_Sample` to restore. Press **A** to reapply the saved setup. On the placed car, enable **Enable Debug Movement** before Play, then hold Up/Down to move it. The pawn follows through attachment. Movement is off by default.

The sample is an approximate box, with a camera-only Pawn. Local +X is forward, +Y right, +Z up; distances use cm. `WorldToMeters` is 100 and the tracking origin is Stage. Align points and `FloorLocalZCm` use the owning car's local coordinates; save files retain measured Stage coordinates. Vehicle root scale must be 1 (tolerance 0.001); put import scale on child meshes. A scaled root is rejected before alignment or marker saving.

## Optional anchors

Anchors are **off by default**. To enable them locally, create ignored `Config/UserEngine.ini`:

```ini
[ConsoleVariables]
VRCal.AnchorAlignment=1
```

Wait for `anchor saved ... created vs requested` before leaving calibration. Restore waits for a pose stable for 1 s within 0.5 cm / 0.5°, or falls back to the saved Stage points after 10 s. Logs include drift and pose changes. Recenter and changed play-area bounds require a setup check. After a Link reconnect, measure again with M and confirm `Marker fit vs pawn` dX/dY are within 2 cm.

Default files: `Saved/Calibration/Alignment.json`, or `Saved/Calibration/Bucks/<Name>.json` with `ActiveBuck.txt` when profiles are configured. Version 2 files with or without an anchor are supported; eye-capture version 1 files are outside this kit's scope. With profiles, the first accepted marker selects the session's Buck; IDs are `FirstMarkerId + Order - 1`. `SelectNextBuck` persists selection for the next level load. There is no scenario or operator panel.

## Plugin layout and checks

- `VRCalibrationCore`: local-coordinate fit, v2 files, profiles, alignment and anchors. `State`, `Reason`, and `OnRecord` are available to Blueprints. No dependency on Marker, MRUK or OpenCV.
- `VRCalibrationMarker`: Win64 camera input, ArUco `DICT_4X4_50`, solvePnP and M/G/H/S calibration. No hand tracking.

Close Unreal Editor before building. Set `$ueBuild` to your UE 5.7 `Engine/Build/BatchFiles/Build.bat`, then run:

```powershell
& $ueBuild VRCalibrationKitEditor Win64 Development -Project="$PWD/VRCalibrationKit.uproject" -DisableUnity -Module=VRCalibrationCore
& $ueBuild VRCalibrationKitEditor Win64 Development -Project="$PWD/VRCalibrationKit.uproject" -DisableUnity -Module=VRCalibrationMarker
& $ueBuild VRCalibrationKitEditor Win64 Development -Project="$PWD/VRCalibrationKit.uproject" -ForceUnity -DisableAdaptiveUnity
```

Run `VRCalibrationKit.*` in Session Frontend, or use `UnrealEditor-Cmd.exe` with `-ExecCmds="Automation RunTests VRCalibrationKit.;Quit" -unattended -nullrhi -nosplash -nosound`. Tests pin the anchor switch independently of local settings. K1-E1–E8 cover v2 compatibility, static equivalence, moving cars, asynchronous anchor completion, disabled anchors, switch isolation, Core-only restore, and root-scale rejection. These checks do not establish headset accuracy. Non-Win64 builds and packaging are outside the sample's validation scope.

Committed sample assets can be regenerated once in a fresh checkout without those assets by launching the GUI editor with `-ExecutePythonScript="<absolute path>/Scripts/create_samples.py"`. The script refuses to replace an existing sample map.

## License

Not yet decided.
