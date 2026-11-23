# VRCalibrationKit

Unreal Engine 5.7 plugin for aligning a virtual vehicle (or any rig) to a physical mock-up in VR.

- **Marker calibration:** printed ArUco markers on the physical mock-up, measured with the Quest 3 headset camera.
- **Anchor restore:** a spatial anchor saved with the calibration restores the alignment after Play restarts or the Link connection drops.
- **Vehicle-relative result:** the VR pawn is attached to the vehicle actor, so the same calibration works for a static vehicle and for a vehicle that drives in VR.

Status: work in progress. Not yet tested on device in this repository.

## Requirements

- Unreal Engine 5.7, Windows (PCVR over Meta Horizon Link)
- Meta Quest 3
- **Meta XR plugin 1.205.0** — not included in this repository (Oculus SDK License). Install it from Fab and copy it to `Plugins/MetaXR/`.
- Meta Horizon Link: enable *Spatial Data over Meta Horizon Link* and passthrough camera access.

## License

Not yet decided.
