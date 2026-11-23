#if WITH_DEV_AUTOMATION_TESTS

#include "VRCalProfileTestAlignment.h"
#include "VRCalMarkerTestActor.h"
#include "VRCalAlignPointComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "HeadMountedDisplayTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	constexpr auto ProfileFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	const FTransform Initial(FVector(-400,50,0));
	const FTransform ExpectedB(FRotator(0,20,0), FVector(100,0,0));

	struct FProfileFiles
	{
		const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), FGuid::NewGuid().ToString() + TEXT("-Profiles"));
		FProfileFiles() { IFileManager::Get().MakeDirectory(*Directory,true); }
		~FProfileFiles() { IFileManager::Get().DeleteDirectory(*Directory,false,true); }
		FString ActivePath() const { return FPaths::Combine(Directory,TEXT("ActiveBuck.txt")); }
		FString LegacyPath() const { return FPaths::Combine(Directory,TEXT("Alignment.json")); }
		FString Path(const FString& Buck) const { return FPaths::Combine(Directory,TEXT("Bucks"),Buck + TEXT(".json")); }
		FString Read(const FString& Path) const { FString Text; FFileHelper::LoadFileToString(Text,*Path); return Text; }
		FString Active() const { return Read(ActivePath()).TrimStartAndEnd(); }
		bool WriteActive(const FString& Buck) const { return FFileHelper::SaveStringToFile(Buck + TEXT("\n"),*ActivePath()); }
	};
}

struct FProfileFixture
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	const FProfileFiles& Files;
	APawn* Pawn;
	AActor* Car;
	UVRCalProfileTestAlignment* Alignment;
	AVRCalMarkerTestActor* Probe;
	TArray<UVRCalAlignPointComponent*> PointsA, PointsB;
	IConsoleVariable* AnchorSwitch = IConsoleManager::Get().FindConsoleVariable(TEXT("VRCal.AnchorAlignment"));
	int32 OriginalAnchor = AnchorSwitch->GetInt();
	int32 AnchorRequests = 0;

	explicit FProfileFixture(const FProfileFiles& InFiles, bool bProfiles = true) : Files(InFiles)
	{
		AnchorSwitch->Set(0, ECVF_SetByCode);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		auto* PC = World->SpawnActor<APlayerController>();
		Pawn = World->SpawnActor<APawn>();
		Pawn->SetRootComponent(NewObject<USceneComponent>(Pawn));
		Pawn->GetRootComponent()->RegisterComponent();
		PC->Possess(Pawn);
		Pawn->SetActorTransform(Initial);
		Car = World->SpawnActor<AActor>();
        Car->SetRootComponent(NewObject<USceneComponent>(Car));
        Car->GetRootComponent()->RegisterComponent();
		Alignment = NewObject<UVRCalProfileTestAlignment>(Car);
		Car->AddInstanceComponent(Alignment);
		Alignment->Directory = Files.Directory;
		Alignment->Origin = EHMDTrackingOrigin::Stage;
		Alignment->RegisterComponent();
		const TArray<FVector> Locations = {FVector(4,-60,50),FVector(4,-14,50),FVector(-44,-14,50),FVector(-44,-60,50)};
        for (int32 I = 0; I < Locations.Num(); ++I)
		{
            auto* Point = NewObject<UVRCalAlignPointComponent>(Car);
            Car->AddInstanceComponent(Point);
            Point->Order = I + 1;
            Point->SetupAttachment(Car->GetRootComponent());
            Point->RegisterComponent();
            Point->SetRelativeLocation(Locations[I]);
            PointsA.Add(Point);
			if (!bProfiles) { continue; }
			Point->Buck = TEXT("A");
			auto* Extra = NewObject<UVRCalAlignPointComponent>(Car);
			Car->AddInstanceComponent(Extra);
			Extra->Buck = TEXT("B");
			Extra->Order = Point->Order;
			Extra->Label = FString::Printf(TEXT("Buck B bolt %d"), Point->Order);
			Extra->SetupAttachment(Car->GetRootComponent());
			Extra->RegisterComponent();
			Extra->SetWorldLocation(Point->GetComponentLocation() + FVector(-30,20,0));
			PointsB.Add(Extra);
		}
		if (bProfiles)
		{
			Alignment->Bucks = {{TEXT("A"), 1}, {TEXT("B"), 5}};
		}
		Probe = World->SpawnActor<AVRCalMarkerTestActor>();
		Probe->Pawn = Pawn;
		Probe->MarkerMinFrames = 3;
		Probe->bCollectTestMessages = true;
	}

	~FProfileFixture()
	{
		World->EndPlay(EEndPlayReason::Quit);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		AnchorSwitch->Set(OriginalAnchor, ECVF_SetByCode);
	}

	void Start()
	{
		World->InitializeActorsForPlay(FURL());
		World->GetWorldSettings()->NotifyBeginPlay();
	}
	FVRCalSavedAlignment Candidate(const TArray<UVRCalAlignPointComponent*>& Points, const FTransform& Pose) const
	{
		FVRCalSavedAlignment Saved;
		Saved.Version = 2;
		Saved.SavedAt = TEXT("2026-11-16T00:00:00Z");
		Saved.TrackingOrigin = Alignment->Origin;
		Saved.PlayAreaCm = Alignment->Area;
		for (const auto* Point : Points)
		{
			FVRCalAlignPointSample& Sample = Saved.Points.AddDefaulted_GetRef();
			Sample.Order = Point->Order;
			Sample.StageCm = Pose.InverseTransformPosition(Point->GetComponentLocation());
			Sample.Frames = 3;
		}
		return Saved;
	}
	void BeginMarkerCapture() { Probe->StartCapture(10); }
	void RecordMarker(int32 Id, const FVector& Stage, double Yaw = -20, double ErrorPx = 0)
	{
		const FVRCalMarkerObservation Observation{Stage,Yaw,0,ErrorPx,32};
		Probe->RecordObservation(Id,&Observation,false);
	}
	void CaptureMarkers(double Yaw = -20, double OffsetCm = 0)
	{
		BeginMarkerCapture();
		const bool bLegacy = PointsB.IsEmpty();
		const auto Samples = Candidate(bLegacy ? PointsA : PointsB, ExpectedB).Points;
		for (int32 I = 0; I < Samples.Num(); ++I)
		{
			const int32 Id = I + (bLegacy ? 1 : 5);
			const FVector Stage = Samples[I].StageCm + FVector(I == 0 ? OffsetCm : 0,0,0);
			const FVRCalMarkerObservation Observation{Stage,Yaw,0,0,32};
			for (int32 Frame = 0; Frame < Probe->MarkerMinFrames; ++Frame)
			{
				Probe->RecordObservation(Id,&Observation,false);
			}
		}
		EndMarkerCapture();
	}
	void EndMarkerCapture() { Probe->FinishCapture(11); }
	bool IsCapturingMarkers() const { return Probe->bCapturing; }
	FString MarkerBuck() const { return Probe->SessionBuck; }
	FTransform MarkerPose() const { return Probe->Fit.IsSet() ? Probe->Fit->Pawn : Initial; }
	void PreviewMarkers() { Probe->ApplyFit(); }
	void SaveMarkers() { Probe->SaveFit(); }
	void HookAnchorSave() { Alignment->OnPointRecord.AddLambda([this](const FString& Line) { if (Line == TEXT("Saving anchor — wait")) { ++AnchorRequests; } }); }
	void AnchorResult(const FString& Message) { Alignment->OnPointRecord.Broadcast(Message); }
	bool MarkerMessage(const FString& Text) const
	{
		return Probe->TestMessages.ContainsByPredicate([&Text](const FString& Message) { return Message.Contains(Text); });
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProfileDefaultFileTest, "VRCalibrationKit.Profiles.E1Legacy", ProfileFlags)
bool FProfileDefaultFileTest::RunTest(const FString&)
{
	FProfileFiles Files;
	FProfileFixture W(Files,false);
	W.Start();
	TestEqual(TEXT("Legacy path"), W.Alignment->SavedAlignmentPath(), Files.LegacyPath());
	TestEqual(TEXT("Next buck disabled"), W.Alignment->NextBuckRejection(), FString(TEXT("One buck or none")));
	TestTrue(TEXT("No buck status"), W.Alignment->BuckStatus().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProfileFallbackTest, "VRCalibrationKit.Profiles.E2Fallback", ProfileFlags)
bool FProfileFallbackTest::RunTest(const FString&)
{
	for (bool bUnknown : {false, true})
	{
		FProfileFiles Files;
		if (bUnknown) { TestTrue(TEXT("Unknown selection fixture"), Files.WriteActive(TEXT("Unknown"))); }
		FProfileFixture W(Files);
		W.Start();
		TestEqual(TEXT("First buck selected"), W.Alignment->ActiveBuckName(), FString(TEXT("A")));
		TestEqual(TEXT("No saved value"), W.Alignment->State, EVRCalAlignmentState::NoSavedValue);
		TestTrue(TEXT("Buck-specific reason"), W.Alignment->Reason.StartsWith(TEXT("No saved value for buck A")));
		TestTrue(TEXT("Fallback reason"), W.Alignment->Reason.Contains(TEXT("Active buck file missing — using A")));
		TestEqual(TEXT("Fallback does not write selection"), Files.Active(), bUnknown ? FString(TEXT("Unknown")) : FString());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProfileValidationTest, "VRCalibrationKit.Profiles.E3Validation", ProfileFlags)
bool FProfileValidationTest::RunTest(const FString&)
{
	const TCHAR* Reasons[] = {TEXT("Duplicate buck name"), TEXT("Buck name is empty"), TEXT("Invalid buck name"),
		TEXT("overlaps"), TEXT("outside 0"), TEXT("order duplicated"), TEXT("unknown buck C"), TEXT("unknown buck (empty)")};
	for (int32 Case = 0; Case < UE_ARRAY_COUNT(Reasons); ++Case)
	{
		FProfileFiles Files;
		FProfileFixture W(Files);
		switch (Case)
		{
		case 0: W.Alignment->Bucks[1].Name = TEXT("a"); break;
		case 1: W.Alignment->Bucks[1].Name.Reset(); break;
		case 2: W.Alignment->Bucks[1].Name = TEXT("A/.."); break;
		case 3: W.Alignment->Bucks[1].FirstMarkerId = 3; break;
		case 4: W.Alignment->Bucks[1].FirstMarkerId = 48; break;
		case 5: W.PointsA[1]->Order = W.PointsA[0]->Order; break;
		case 6: W.PointsA[1]->Buck = TEXT("C"); break;
		case 7: W.PointsA[1]->Buck.Reset(); break;
		}
		W.Start();
		TestEqual(Reasons[Case], W.Alignment->State, EVRCalAlignmentState::NeedsCheck);
		TestTrue(TEXT("Setup reason"), W.Alignment->Reason.Contains(Reasons[Case]));
		TestTrue(TEXT("No pawn move"), W.Pawn->GetActorTransform().Equals(Initial));
		W.BeginMarkerCapture();
		TestFalse(TEXT("Invalid configuration rejected by M"), W.IsCapturingMarkers());
		TestTrue(TEXT("Same marker rejection"), W.MarkerMessage(W.Alignment->Reason));
	}
	FProfileFiles Files;
	FProfileFixture W(Files);
	W.PointsA.Pop()->DestroyComponent();
	W.Start();
	TestTrue(TEXT("Three points valid for restore"), W.Alignment->ValidateBuckProfiles().IsEmpty());
	W.BeginMarkerCapture();
	TestFalse(TEXT("Three points rejected for marker capture"), W.IsCapturingMarkers());
	TestTrue(TEXT("Marker count reason"), W.MarkerMessage(TEXT("Expected four vehicle alignment points for buck A")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProfileReloadTest, "VRCalibrationKit.Profiles.E4Reload", ProfileFlags)
bool FProfileReloadTest::RunTest(const FString&)
{
    FProfileFiles Files;
    {
        FProfileFixture W(Files);
        FString Why, Name;
        TestTrue(TEXT("Saved A"), FVRCalAlignmentStore::Save(Files.Path(TEXT("A")), W.Candidate(W.PointsA,FTransform::Identity), Why));
        TestTrue(TEXT("Saved B"), FVRCalAlignmentStore::Save(Files.Path(TEXT("B")), W.Candidate(W.PointsB,ExpectedB), Why));
        W.Start();
        TestTrue(TEXT("A applied"), W.Pawn->GetActorTransform().Equals(FTransform::Identity,0.01));
        TestTrue(TEXT("Select next"), W.Alignment->SelectNextBuck(Name,Why));
        TestEqual(TEXT("B written"), Files.Active(), FString(TEXT("B")));
        TestEqual(TEXT("In-memory set retained until reload"), W.Alignment->ActiveBuckName(), FString(TEXT("A")));
    }
    {
        FProfileFixture W(Files); W.Start();
        TestTrue(TEXT("Reload applies B"), W.Pawn->GetActorTransform().Equals(ExpectedB,0.01));
        FString Name, Why;
        TestTrue(TEXT("Wraparound"), W.Alignment->SelectNextBuck(Name,Why));
        TestEqual(TEXT("A written"), Files.Active(), FString(TEXT("A")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProfileGuardsTest, "VRCalibrationKit.Profiles.E5Guards", ProfileFlags)
bool FProfileGuardsTest::RunTest(const FString&)
{
    FProfileFiles Files; FProfileFixture W(Files); W.Start();
    FString Name, Why;
    TestTrue(TEXT("Select A fixture"), Files.WriteActive(TEXT("A")));
    const auto B = W.Alignment->Bucks.Pop();
    TestFalse(TEXT("One profile refused"), W.Alignment->SelectNextBuck(Name,Why));
    TestEqual(TEXT("Reason"), Why, FString(TEXT("One buck or none")));
    W.Alignment->Bucks.Add(B);
    IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();
    TestTrue(TEXT("Make read-only"), Platform.SetReadOnly(*Files.ActivePath(),true));
    TestFalse(TEXT("Read-only selection refused"), W.Alignment->SelectNextBuck(Name,Why));
    TestEqual(TEXT("Selection retained"), Files.Active(), FString(TEXT("A")));
    TestTrue(TEXT("Restore write access"), Platform.SetReadOnly(*Files.ActivePath(),false));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProfileMarkerSelectionTest, "VRCalibrationKit.Profiles.E8MarkerSelection", ProfileFlags)
bool FProfileMarkerSelectionTest::RunTest(const FString&)
{
	FProfileFiles Files;
	FProfileFixture W(Files);
	W.Start();
	W.BeginMarkerCapture();
	W.RecordMarker(2,FVector::ZeroVector,0,2);
	TestTrue(TEXT("Rejected observation cannot select A"), W.MarkerBuck().IsEmpty());
	const auto Samples = W.Candidate(W.PointsB,ExpectedB).Points;
	for (int32 I = 0; I < Samples.Num(); ++I)
	{
		for (int32 Frame = 0; Frame < W.Probe->MarkerMinFrames; ++Frame)
		{
			W.RecordMarker(I + 5, Samples[I].StageCm);
		}
	}
	W.RecordMarker(2,FVector::ZeroVector);
	W.RecordMarker(2,FVector::ZeroVector);
	W.EndMarkerCapture();
	TestEqual(TEXT("B selected from marker ID"), W.MarkerBuck(), FString(TEXT("B")));
	TestTrue(TEXT("Other buck counted"), W.MarkerMessage(TEXT("Ignored markers from buck A: ID 2 ×2")));
	TestTrue(TEXT("Fit uses B coordinates"), W.MarkerPose().Equals(ExpectedB,0.01));
	W.PreviewMarkers();
	for (int32 I = 0; I < W.PointsB.Num(); ++I)
	{
		TestTrue(TEXT("G shows only session buck"), W.PointsA[I]->bHiddenInGame && !W.PointsB[I]->bHiddenInGame);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProfileMarkerSaveTest, "VRCalibrationKit.Profiles.E9MarkerSave", ProfileFlags)
bool FProfileMarkerSaveTest::RunTest(const FString&)
{
	FProfileFiles Files;
	FProfileFixture W(Files);
	W.Start();
	const FString Path = Files.Path(TEXT("B"));
	W.SaveMarkers();
	TestTrue(TEXT("No result refused"), W.MarkerMessage(TEXT("No marker fit to save")));
	TestFalse(TEXT("No save before fit"), IFileManager::Get().FileExists(*Path));
	W.CaptureMarkers();
	W.SaveMarkers();
	FString Failure;
	FVRCalSavedAlignment Saved;
	TestTrue(TEXT("Valid markers save B file"), FVRCalAlignmentStore::Load(Path,Saved,Failure));
	TestEqual(TEXT("B active"), Files.Active(), FString(TEXT("B")));
	TestTrue(TEXT("Success message"), W.MarkerMessage(TEXT("Saved for buck B — open L_Sample")));
	const auto Expected = W.Candidate(W.PointsB,ExpectedB).Points;
	TestEqual(TEXT("Four saved Stage centers"), Saved.Points.Num(), Expected.Num());
	for (int32 I = 0; I < Saved.Points.Num() && I < Expected.Num(); ++I)
	{
		TestEqual(TEXT("Mapped point order"), Saved.Points[I].Order, Expected[I].Order);
		TestTrue(TEXT("Saved median Stage center"), Saved.Points[I].StageCm.Equals(Expected[I].StageCm,0.0001));
		TestEqual(TEXT("Frame count"), Saved.Points[I].Frames, W.Probe->MarkerMinFrames);
	}
	const FString Before = Files.Read(Path);
	W.CaptureMarkers(160);
	W.SaveMarkers();
	TestTrue(TEXT("Arrow rejection logged"), W.MarkerMessage(TEXT("off car front")));
	TestEqual(TEXT("Arrow failure keeps save"), Files.Read(Path), Before);
	W.CaptureMarkers(-20,14);
	W.SaveMarkers();
	TestTrue(TEXT("Fit-limit rejection logged"), W.MarkerMessage(TEXT("Marker save rejected — Fit")));
	TestEqual(TEXT("Fit failure keeps save"), Files.Read(Path), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProfileMarkerRoundTripTest, "VRCalibrationKit.Profiles.E10MarkerRoundTrip", ProfileFlags)
bool FProfileMarkerRoundTripTest::RunTest(const FString&)
{
	FProfileFiles Files;
	FProfileFixture W(Files);
	W.Start();
	W.CaptureMarkers();
	const FTransform Fit = W.MarkerPose();
	W.SaveMarkers();
	TestTrue(TEXT("Load through normal reapply"), W.Alignment->RequestReapply());

	TestEqual(TEXT("Marker file applied"), W.Alignment->State, EVRCalAlignmentState::Applied);
	TestTrue(TEXT("Applied position matches marker fit"), W.Pawn->GetActorLocation().Equals(Fit.GetLocation(),0.01));
	TestTrue(TEXT("Applied yaw matches marker fit"), FMath::Abs(FMath::FindDeltaAngleDegrees(W.Pawn->GetActorRotation().Yaw,Fit.Rotator().Yaw)) <= 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProfileStatusTest, "VRCalibrationKit.Profiles.E11Status", ProfileFlags)
bool FProfileStatusTest::RunTest(const FString&)
{
    FProfileFiles Files; FProfileFixture W(Files); FString Why;
    TestTrue(TEXT("B fixture"), FVRCalAlignmentStore::Save(Files.Path(TEXT("B")), W.Candidate(W.PointsB,ExpectedB), Why));
    TestTrue(TEXT("Select B"), Files.WriteActive(TEXT("B"))); W.Start();
    TestEqual(TEXT("B profile line"), W.Alignment->BuckStatus(), FString(TEXT("Buck: B (2/2)")));
    TestEqual(TEXT("Applied state"), W.Alignment->State, EVRCalAlignmentState::Applied);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProfileDefaultPointTagsTest, "VRCalibrationKit.Profiles.E12LegacyPointTags", ProfileFlags)
bool FProfileDefaultPointTagsTest::RunTest(const FString&)
{
	FProfileFiles Files;
	FProfileFixture W(Files,false);
	W.PointsA[0]->Buck = TEXT("Unused");
	W.PointsA[1]->Buck = TEXT("Also unused");
	FString Failure;
	TestTrue(TEXT("Legacy point save"), FVRCalAlignmentStore::Save(Files.LegacyPath(),W.Candidate(W.PointsA,ExpectedB),Failure));
	W.Start();
	TestTrue(TEXT("Legacy fit ignores point buck names"), W.Pawn->GetActorTransform().Equals(ExpectedB,0.01));
	W.BeginMarkerCapture();
	TestTrue(TEXT("Legacy M ignores point buck names"), W.IsCapturingMarkers());
	TestTrue(TEXT("Legacy M has no session buck"), W.MarkerBuck().IsEmpty());
	W.CaptureMarkers();
	W.SaveMarkers();
	FVRCalSavedAlignment Saved;
	TestTrue(TEXT("Legacy marker fit saved to Alignment.json"), FVRCalAlignmentStore::Load(Files.LegacyPath(),Saved,Failure));
	TestTrue(TEXT("Legacy marker save does not select a buck"), Files.Active().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarkerAnchorSaveNoticeTest, "VRCalibrationKit.Anchors.MarkerAnchorSaveNotice", ProfileFlags)
bool FMarkerAnchorSaveNoticeTest::RunTest(const FString&)
{
	auto* Switch = IConsoleManager::Get().FindConsoleVariable(TEXT("VRCal.AnchorAlignment"));
	const int32 Previous = Switch->GetInt(); Switch->Set(1, ECVF_SetByCode);
	{
		FProfileFiles Files; FProfileFixture W(Files); Switch->Set(1, ECVF_SetByCode); W.HookAnchorSave(); W.Start(); W.CaptureMarkers(); W.SaveMarkers();
		TestEqual(TEXT("Marker save requests an anchor"), W.AnchorRequests, 1);
		TestTrue(TEXT("Marker screen asks operator to wait"), W.MarkerMessage(TEXT("Saving anchor — wait")));
		TestFalse(TEXT("Pending save does not ask operator to leave level"), W.MarkerMessage(TEXT("Saved for buck B — open L_Sample")));
		W.AnchorResult(TEXT("anchor saved UUID=test result=0"));
		TestTrue(TEXT("Asynchronous success reaches marker screen"), W.MarkerMessage(TEXT("anchor saved UUID=test result=0")));
		W.SaveMarkers();
		W.AnchorResult(TEXT("anchor save failed result=-1; alignment saved without anchor"));
		TestTrue(TEXT("Asynchronous failure reaches marker screen"), W.MarkerMessage(TEXT("anchor save failed result=-1")));
		FVRCalSavedAlignment Saved; FString Failure;
		TestTrue(TEXT("Point alignment still saved"), FVRCalAlignmentStore::Load(Files.Path(TEXT("B")), Saved, Failure));
		TestTrue(TEXT("Failed anchor leaves valid point file without anchor"), Saved.AnchorUuid.IsEmpty());
	}
	Switch->Set(Previous, ECVF_SetByCode);
	return true;
}

#endif
