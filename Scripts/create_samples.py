"""Run once in the Unreal Editor with -ExecutePythonScript=<absolute path>."""
import unreal

assets = unreal.AssetToolsHelpers.get_asset_tools()
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

car_path = "/Game/Samples/BP_SampleCar"
car_bp = unreal.load_asset(car_path)
if car_bp is None:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.load_class(None, "/Script/VRCalibrationKit.VRCalSampleCar"))
    car_bp = assets.create_asset("BP_SampleCar", "/Game/Samples", unreal.Blueprint, factory)
assert car_bp is not None
assert unreal.EditorAssetLibrary.save_loaded_asset(car_bp)
car_class = unreal.EditorAssetLibrary.load_blueprint_class(car_path)
game_mode = unreal.load_class(None, "/Script/VRCalibrationKit.VRCalSampleGameMode")

for name, calibrate in [("L_Calibrate", True), ("L_Sample", False)]:
    path = "/Game/Samples/" + name
    # Explicit one-time creation; never replace an existing sample map.
    assert not unreal.EditorAssetLibrary.does_asset_exist(path), "Sample map already exists: " + path
    assert levels.new_level(path)
    car = actors.spawn_actor_from_class(car_class, unreal.Vector(0, 0, 0))
    assert car is not None
    car.set_actor_label("Sample Car")
    actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-120, -120, 0))
    light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 300), unreal.Rotator(-40, -30, 0))
    assert light is not None
    actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 300))
    if calibrate:
        marker_class = unreal.load_class(None, "/Script/VRCalibrationMarker.VRCalMarkerCalibrator")
        assert actors.spawn_actor_from_class(marker_class, unreal.Vector(0, 0, 0)) is not None
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    settings = world.get_world_settings()
    settings.set_editor_property("default_game_mode", game_mode)
    settings.set_editor_property("world_to_meters", 100.0)
    assert levels.save_current_level()
    unreal.log("K1 sample saved: " + path)

unreal.log("K1 sample asset creation complete")
unreal.SystemLibrary.quit_editor()
