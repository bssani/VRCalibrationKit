#if WITH_DEV_AUTOMATION_TESTS
#include "CoreFixture.h"
#include "AnchorTestAccess.h"
#include "Engine/TargetPoint.h"
#include "Misc/AutomationTest.h"

namespace
{
    constexpr auto AnchorFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    struct FAnchorWorld : FCoreFixture
    {
        int32 Requests = 0;
        FAnchorWorld() { FAnchorTestAccess::Hook(A, Requests); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnchorDisabledTest, "VRCalibrationKit.Anchors.E1Disabled", AnchorFlags)
bool FAnchorDisabledTest::RunTest(const FString&)
{
	FAnchorWorld W; W.Start(W.Value(), false);
	TestEqual(TEXT("No discovery request"), W.Requests, 0);
	TestTrue(TEXT("Legacy point alignment applied"), W.A->State == EVRCalAlignmentState::Applied);
	TestTrue(TEXT("Legacy Pawn unchanged"), W.Pawn->GetActorTransform().Equals(FTransform::Identity, 0.001));
	FString Why; TestTrue(TEXT("Marker save succeeds"), W.A->SaveMarkerPoints(FString(), W.Value(false).Points, Why));
	TestEqual(TEXT("No create request"), W.Requests, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOptionalAnchorStoreTest, "VRCalibrationKit.Anchors.E2OptionalAnchor", AnchorFlags)
bool FOptionalAnchorStoreTest::RunTest(const FString&)
{
	FAnchorWorld W; FString Why; FVRCalSavedAlignment Out;
	for (bool bAnchor : {true, false})
	{
		auto S = W.Value(bAnchor);
		TestTrue(TEXT("Save"), FVRCalAlignmentStore::Save(W.Path, S, Why));
		TestTrue(TEXT("Load"), FVRCalAlignmentStore::Load(W.Path, Out, Why));
		TestEqual(TEXT("Version unchanged"), Out.Version, 2);
		TestEqual(TEXT("Optional UUID roundtrip"), Out.AnchorUuid, S.AnchorUuid);
		if (bAnchor) { TestTrue(TEXT("Stage pose cm and deg roundtrip"), Out.AnchorStage.Equals(S.AnchorStage)); }
	}
	auto Invalid = W.Value(); Invalid.AnchorUuid = TEXT("bad UUID");
	TestFalse(TEXT("Invalid anchor UUID rejected on save"), FVRCalAlignmentStore::Save(W.Path, Invalid, Why));
	W.Start(W.Value(false), true);
	TestEqual(TEXT("Old file needs no discovery even when enabled"), W.Requests, 0);
	TestTrue(TEXT("Old file still applied"), W.A->State == EVRCalAlignmentState::Applied);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnchorSaveAfterPointsTest, "VRCalibrationKit.Anchors.SaveRequestsAfterPointSave", AnchorFlags)
bool FAnchorSaveAfterPointsTest::RunTest(const FString&)
{
	FAnchorWorld W; auto S = W.Value(false); W.Start(S, true);
	FString Why; FVRCalSavedAlignment Loaded;
	TestTrue(TEXT("Marker save succeeds before anchor request"), W.A->SaveMarkerPoints(FString(), S.Points, Why));
	TestEqual(TEXT("Marker save requests one anchor"), W.Requests, 1);
	TestTrue(TEXT("Successful point file remains readable while anchor pending"), FVRCalAlignmentStore::Load(W.Path, Loaded, Why));
	TestTrue(TEXT("Point file has no anchor until device save succeeds"), Loaded.AnchorUuid.IsEmpty());
	TestTrue(TEXT("Reapply after point save"), W.A->RequestReapply());
	TestTrue(TEXT("Anchor operation does not block alignment save"), W.A->State == EVRCalAlignmentState::Applied);
	S.Points[0].StageCm.X += 100;
	TestFalse(TEXT("Rejected fit not saved"), W.A->SaveMarkerPoints(FString(), S.Points, Why));
	TestEqual(TEXT("Rejected save never requests anchor"), W.Requests, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnchorFoundTest, "VRCalibrationKit.Anchors.E3Found", AnchorFlags)
bool FAnchorFoundTest::RunTest(const FString&)
{
	FAnchorWorld W; auto S = W.Value(); W.A->Area += FVector2D(100,100); W.Start(S, true);
	TestEqual(TEXT("One UUID discovery request"), W.Requests, 1);
	TestTrue(TEXT("NeedsCheck while finding"), W.A->State == EVRCalAlignmentState::NeedsCheck);
	const FTransform Delta(FRotator(0,15,0), FVector(30,-20,0));
	FAnchorTestAccess::Found(W.A, S.AnchorStage * Delta);
	TestTrue(TEXT("Bounds difference does not block anchor"), W.A->State == EVRCalAlignmentState::Applied);
	TestTrue(TEXT("Anchor visible in status"), W.A->Reason.Contains(TEXT("anchor")));
	// Pawn maps moved Stage points back to fixed CAD (K1 §2)
	TestTrue(TEXT("Pawn compensates Stage translation and Yaw"), W.Pawn->GetActorTransform().Equals(Delta.Inverse(), 0.001));
	TestTrue(TEXT("A accepted"), W.A->RequestReapply());
	TestEqual(TEXT("A reuses found pose"), W.Requests, 1);
	TestTrue(TEXT("A keeps compensated Pawn"), W.Pawn->GetActorTransform().Equals(Delta.Inverse(), 0.001));
	W.A->Origin = EHMDTrackingOrigin::Local; TestTrue(TEXT("A accepted for environment check"), W.A->RequestReapply());
	TestTrue(TEXT("Non-Stage still blocked"), W.A->State == EVRCalAlignmentState::NeedsCheck);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnchorTimeoutTest, "VRCalibrationKit.Anchors.E4Timeout", AnchorFlags)
bool FAnchorTimeoutTest::RunTest(const FString&)
{
	for (bool bChangedBounds : {false, true})
	{
		FAnchorWorld W; auto S = W.Value(); if (bChangedBounds) { W.A->Area += FVector2D(100,100); }
		W.Pawn->SetActorLocation(FVector(500,600,0)); W.Start(S, true);
		TestTrue(TEXT("Finding anchor reason"), W.A->Reason == TEXT("Finding anchor"));
		TestTrue(TEXT("Pawn held during discovery"), W.Pawn->GetActorLocation().Equals(FVector(500,600,0)));
		FAnchorTestAccess::Timeout(W.A);
		TestTrue(TEXT("Legacy Pawn after timeout"), W.Pawn->GetActorTransform().Equals(FTransform::Identity, 0.001));
		TestTrue(TEXT("Legacy environment decision"), W.A->State == (bChangedBounds ? EVRCalAlignmentState::NeedsCheck : EVRCalAlignmentState::Applied));
		FAnchorTestAccess::Found(W.A, FTransform(FVector(900,900,0)));
		TestTrue(TEXT("Late result ignored"), W.Pawn->GetActorTransform().Equals(FTransform::Identity, 0.001));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWaitForAnchorActorPoseTest, "VRCalibrationKit.Anchors.WaitForAnchorActorPose", AnchorFlags)
bool FWaitForAnchorActorPoseTest::RunTest(const FString&)
{
	FAnchorWorld W; const auto S = W.Value();
	const FTransform Before(FVector(500,600,0)); W.Pawn->SetActorTransform(Before); W.Start(S, true);
	AActor* Actor = W.World->SpawnActor<ATargetPoint>();
	FAnchorTestAccess::Located(W.A, Actor);
	const FTransform Delta(FRotator(0,15,0), FVector(30,-20,0));
	const FTransform LocatedWorld = S.AnchorStage * Delta * Before;
	Actor->SetActorTransform(LocatedWorld);
	FAnchorTestAccess::Advance(W.A, false);
	TestEqual(TEXT("Creation frame never applies an anchor"), W.A->Reason, FString(TEXT("Finding anchor")));
	TestTrue(TEXT("Creation frame keeps Pawn"), W.Pawn->GetActorTransform().Equals(Before));
	Actor->SetActorTransform(FTransform::Identity);
	FAnchorTestAccess::Advance(W.A, true);
	TestEqual(TEXT("Valid handle with untouched Actor still waits"), W.A->Reason, FString(TEXT("Finding anchor")));
	TestTrue(TEXT("Untouched Actor cannot move Pawn"), W.Pawn->GetActorTransform().Equals(Before));
	Actor->SetActorTransform(LocatedWorld);
	FAnchorTestAccess::Advance(W.A, true);
	// First updated pose only starts the stable window (K1 §2)
	TestEqual(TEXT("Updated Actor waits for stable pose"), W.A->Reason, FString(TEXT("Finding anchor")));
	FAnchorTestAccess::Settle(W.A); FAnchorTestAccess::Advance(W.A, true);
	TestTrue(TEXT("Updated Actor applies the anchor"), W.A->State == EVRCalAlignmentState::Applied);
	TestTrue(TEXT("Updated Actor gives the E3 compensation"), W.Pawn->GetActorTransform().Equals(Delta.Inverse(), 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStableAnchorPoseTest, "VRCalibrationKit.Anchors.StableAnchorPose", AnchorFlags)
bool FStableAnchorPoseTest::RunTest(const FString&)
{
	FAnchorWorld W; const auto S = W.Value();
	TArray<FString> Lines; W.A->OnPointRecord.AddLambda([&Lines](const FString& Line) { Lines.Add(Line); });
	const FTransform Before(FVector(500,600,0)); W.Pawn->SetActorTransform(Before); W.Start(S, true);
	AActor* Actor = W.World->SpawnActor<ATargetPoint>();
	FAnchorTestAccess::Located(W.A, Actor);
	const FTransform Delta(FRotator(0,15,0), FVector(30,-20,0));
	const FTransform LocatedWorld = S.AnchorStage * Delta * Before;

	// Unsettled first pose 3 cm off, then the true pose: window restarts
	Actor->SetActorTransform(FTransform(FVector(3,0,0)) * LocatedWorld);
	FAnchorTestAccess::Advance(W.A, true);
	FAnchorTestAccess::Settle(W.A);
	Actor->SetActorTransform(LocatedWorld);
	FAnchorTestAccess::Advance(W.A, true);
	TestEqual(TEXT("Pose change restarts window even after long wait"), W.A->Reason, FString(TEXT("Finding anchor")));
	TestTrue(TEXT("Pawn held while unsettled"), W.Pawn->GetActorTransform().Equals(Before));

	// Small jitter inside tolerance keeps window; no time passed yet
	Actor->SetActorTransform(FTransform(FVector(0.2,0,0)) * LocatedWorld);
	FAnchorTestAccess::Advance(W.A, true);
	TestEqual(TEXT("Waits until window passes"), W.A->Reason, FString(TEXT("Finding anchor")));

	Actor->SetActorTransform(LocatedWorld);
	FAnchorTestAccess::Settle(W.A); FAnchorTestAccess::Advance(W.A, true);
	TestTrue(TEXT("Stable pose applies anchor"), W.A->State == EVRCalAlignmentState::Applied);
	TestTrue(TEXT("Stable pose gives E3 compensation"), W.Pawn->GetActorTransform().Equals(Delta.Inverse(), 0.001));
	const FString* Found = Lines.FindByPredicate([](const FString& L) { return L.StartsWith(TEXT("anchor found result=0")); });
	TestNotNull(TEXT("Found line keeps old prefix"), Found);
	if (Found)
	{
		TestTrue(TEXT("Pose change count logged"), Found->Contains(TEXT("pose changes 1")));
		TestTrue(TEXT("Stage drift Yaw logged"), Found->Contains(TEXT("Yaw +15.00 deg")));
	}
	return true;
}
#endif
