"""Open and inspect both sample maps in the GUI editor."""
import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for name, marker_count in [("L_Calibrate", 1), ("L_Sample", 0)]:
    path = "/Game/Samples/" + name
    assert levels.load_level(path)
    all_actors = actors.get_all_level_actors()
    cars = [a for a in all_actors if a.get_class().get_path_name() == "/Game/Samples/BP_SampleCar.BP_SampleCar_C"]
    assert len(cars) == 1
    car = cars[0]
    assert not car.get_editor_property("enable_debug_movement")
    assert car.get_editor_property("alignment") is not None
    points = car.get_editor_property("align_points")
    assert len(points) == 4
    assert sorted(p.get_editor_property("order") for p in points) == [1, 2, 3, 4]
    for point in points:
        for child in point.get_children_components(False):
            assert child.get_attach_parent() == point
    markers = [a for a in all_actors if a.get_class().get_path_name() == "/Script/VRCalibrationMarker.VRCalMarkerCalibrator"]
    assert len(markers) == marker_count
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_world_settings().get_editor_property("world_to_meters") == 100.0
    unreal.log("K1 map verified: " + path + " | car=1 points=4 markers=" + str(marker_count))
unreal.log("K1 GUI sample verification complete")
unreal.SystemLibrary.quit_editor()
