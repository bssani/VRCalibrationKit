#if WITH_DEV_AUTOMATION_TESTS
#include "CoreFixture.h"
#include "AnchorTestAccess.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CoreDelegates.h"

namespace
{
    constexpr auto KitFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    const FTransform KitRelative(FRotator(0,20,0), FVector(100,-25,0));
    FVRCalSavedAlignment RelativeValue(const FCoreFixture& W)
    {
        auto S = W.Value(false);
        for (auto& P : S.Points) { P.StageCm = KitRelative.InverseTransformPosition(P.StageCm); }
        return S;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKitE1, "VRCalibrationKit.K1.E1V2Compatibility", KitFlags)
bool FKitE1::RunTest(const FString&)
{
    for (const TCHAR* Name : {TEXT("points-v2.json"), TEXT("anchor-v2.json")})
    {
        FCoreFixture W;
        const FString Fixture = FPaths::ProjectPluginsDir() / TEXT("VRCalibrationKit/Tests/Fixtures") / Name;
        FVRCalSavedAlignment Saved, Roundtrip;
        FString Why;
        if (!TestTrue(TEXT("Read fixed v2 fixture"), FVRCalAlignmentStore::Load(Fixture, Saved, Why))) { return false; }
        TestTrue(TEXT("Roundtrip save"), FVRCalAlignmentStore::Save(W.Path, Saved, Why));
        TestTrue(TEXT("Roundtrip load"), FVRCalAlignmentStore::Load(W.Path, Roundtrip, Why));
        TestEqual(TEXT("UUID preserved"), Roundtrip.AnchorUuid, Saved.AnchorUuid);
        TestEqual(TEXT("Origin preserved"), Roundtrip.TrackingOrigin, Saved.TrackingOrigin);
        TestTrue(TEXT("Area preserved"), Roundtrip.PlayAreaCm.Equals(Saved.PlayAreaCm));
        TestTrue(TEXT("Anchor pose preserved"), Roundtrip.AnchorStage.Equals(Saved.AnchorStage));
        TestEqual(TEXT("Point count"), Roundtrip.Points.Num(), 4);
        for (int32 I = 0; I < Roundtrip.Points.Num(); ++I)
        {
            TestTrue(TEXT("Stage coordinates preserved"), Roundtrip.Points[I].StageCm.Equals(Saved.Points[I].StageCm, 1e-8));
            TestEqual(TEXT("Frames preserved"), Roundtrip.Points[I].Frames, Saved.Points[I].Frames);
        }
        W.A->InitializeAlignment(W.Pawn, &Roundtrip, W.A->Origin, W.A->Area);
        TestTrue(TEXT("Same known Pawn position"), W.Pawn->GetActorTransform().Equals(KitRelative,0.0001));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKitE2, "VRCalibrationKit.K1.E2StaticOrigin", KitFlags)
bool FKitE2::RunTest(const FString&)
{
    FCoreFixture W;
    auto S = RelativeValue(W);
    W.A->FloorLocalZCm = 12;
    TArray<FVector> Stage, World;
    for (int32 I = 0; I < S.Points.Num(); ++I) { Stage.Add(S.Points[I].StageCm); World.Add(W.Points[I]->GetComponentLocation()); }
    FVRCalPointFit Base;
    TestTrue(TEXT("Original world FitPoints baseline"), FVRCalAlignment::FitPoints(Stage, World, 12, Base));
    W.A->InitializeAlignment(W.Pawn, &S, W.A->Origin, W.A->Area);
    TestEqual(TEXT("Applied"), W.A->State, EVRCalAlignmentState::Applied);
    TestTrue(TEXT("BasePawn equivalence within 0.1 mm"), W.Pawn->GetActorTransform().Equals(Base.Pawn, 0.01));
    TestEqual(TEXT("Floor sets height"), W.Pawn->GetActorLocation().Z, 12.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKitE3, "VRCalibrationKit.K1.E3VehicleMovement", KitFlags)
bool FKitE3::RunTest(const FString&)
{
    FCoreFixture W;
    auto S = RelativeValue(W);
    W.Car->SetActorTransform(FTransform(FRotator(0,67,0),FVector(500,-300,45)));
    W.A->InitializeAlignment(W.Pawn, &S, W.A->Origin, W.A->Area);
    TestTrue(TEXT("Same car-relative calibration"), W.Pawn->GetActorTransform().GetRelativeTransform(W.Car->GetActorTransform()).Equals(KitRelative,0.0001));
    TestTrue(TEXT("Pawn attached to car"), W.Pawn->GetAttachParentActor() == W.Car);
    const FTransform Moved(FRotator(0,-18,0),FVector(700,90,80));
    W.Car->SetActorTransform(Moved);
    TestTrue(TEXT("Attachment follows translation, rotation and height without Tick"), W.Pawn->GetActorTransform().Equals(KitRelative * Moved,0.0001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKitE4, "VRCalibrationKit.K1.E4MoveDuringDiscovery", KitFlags)
bool FKitE4::RunTest(const FString&)
{
    FCoreFixture W;
    int32 Requests = 0;
    FAnchorTestAccess::Hook(W.A, Requests);
    auto S = RelativeValue(W);
    S.AnchorUuid = TEXT("00112233445566778899aabbccddeeff");
    S.AnchorStage = FTransform(FVector(10,20,50));
    TestTrue(TEXT("Start anchored restore"), W.Start(S,true));
    TestEqual(TEXT("One discovery"), Requests,1);
    W.Car->SetActorTransform(FTransform(FRotator(0,35,0),FVector(400,600,20)));
    const FTransform StageDrift(FRotator(0,15,0),FVector(30,-20,0));
    FAnchorTestAccess::Found(W.A, S.AnchorStage * StageDrift);
    const FTransform ExpectedRelative = StageDrift.Inverse() * KitRelative;
    TestTrue(TEXT("Current car transform at callback"), W.Pawn->GetActorTransform().Equals(ExpectedRelative * W.Car->GetActorTransform(),0.001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKitE5, "VRCalibrationKit.K1.E5AnchorDisabled", KitFlags)
bool FKitE5::RunTest(const FString&)
{
    FCoreFixture W;
    int32 Requests = 0;
    FAnchorTestAccess::Hook(W.A, Requests);
    TestTrue(TEXT("Start with switch off"), W.Start(W.Value(),false));
    FString Why;
    TestTrue(TEXT("Save points with switch off"), W.A->SaveMarkerPoints(FString(),W.Value(false).Points,Why));
    TestEqual(TEXT("No save or discovery request"),Requests,0);
    TestEqual(TEXT("Applied"),W.A->State,EVRCalAlignmentState::Applied);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKitE6, "VRCalibrationKit.K1.E6UserSwitchIsolation", KitFlags)
bool FKitE6::RunTest(const FString&)
{
    auto* Switch = IConsoleManager::Get().FindConsoleVariable(TEXT("VRCal.AnchorAlignment"));
    const int32 Previous = Switch->GetInt();
    Switch->Set(1, ECVF_SetByCode);
    {
        FCoreFixture W;
        TestEqual(TEXT("Fixture pins anchor default despite user override"),Switch->GetInt(),0);
        int32 Requests = 0;
        FAnchorTestAccess::Hook(W.A,Requests);
        TestTrue(TEXT("Start deterministic fixture"),W.Start(W.Value(),false));
        TestEqual(TEXT("No request"),Requests,0);
    }
    TestEqual(TEXT("User override restored"),Switch->GetInt(),1);
    Switch->Set(Previous,ECVF_SetByCode);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKitE7, "VRCalibrationKit.K1.E7CoreOnlyRestore", KitFlags)
bool FKitE7::RunTest(const FString&)
{
    // This translation unit and its fixture depend on Core only (K1 §4)
    FCoreFixture W;
    TestTrue(TEXT("Core-only saved restore"), W.Start(RelativeValue(W),false));
    TestEqual(TEXT("Core-only state"),W.A->State,EVRCalAlignmentState::Applied);
    TestTrue(TEXT("Core-only pose"), W.Pawn->GetActorTransform().Equals(KitRelative,0.0001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKitEnvironment, "VRCalibrationKit.Core.EnvironmentAndRecenter", KitFlags)
bool FKitEnvironment::RunTest(const FString&)
{
    FCoreFixture W;
    auto S = RelativeValue(W);
    TestTrue(TEXT("Start fixture"),W.Start(S,false));
    const auto Before = W.Pawn->GetActorTransform();
    FCoreDelegates::VRHeadsetRecenter.Broadcast();
    TestEqual(TEXT("Recenter requires check"),W.A->State,EVRCalAlignmentState::NeedsCheck);
    TestTrue(TEXT("Recenter leaves pawn"),W.Pawn->GetActorTransform().Equals(Before));
    W.A->Area.X += 100;
    W.A->RequestReapply();
    TestEqual(TEXT("Changed bounds need check"),W.A->State,EVRCalAlignmentState::NeedsCheck);
    TestTrue(TEXT("Guardian reason"),W.A->Reason.Contains(TEXT("guardian")));
    W.A->Origin = EHMDTrackingOrigin::Local;
    W.Pawn->SetActorLocation(FVector(900,900,0));
    W.A->RequestReapply();
    TestTrue(TEXT("Origin mismatch does not place pawn"),W.Pawn->GetActorLocation().Equals(FVector(900,900,0)));
    return true;
}
#endif
