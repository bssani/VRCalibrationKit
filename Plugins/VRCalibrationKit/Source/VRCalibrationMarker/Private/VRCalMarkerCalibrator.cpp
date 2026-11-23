#include "VRCalMarkerCalibrator.h"

#include "Algo/AllOf.h"
#include "Components/InputComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "MRUKPassthroughCameraAccess.h"
#include "MRUtilityKitSubsystem.h"
#include "VRCalAlignPointComponent.h"
#include "VRCalAlignmentComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogVRCalMarker, Log, All);

bool SolveVRCalMarker(const FVector2f (&Pixels)[4], double SideMm, const FVRCalCameraModel& Model,
	const FTransform& PawnWorld, FVRCalMarkerObservation& Out);
void DetectVRCalMarkers(FVRCalCameraFrame& Frame, TFunctionRef<void(int32, const FVector2f (&)[4])> Visit);

namespace
{
template <typename SampleType, typename Projection>
double MarkerMedian(const TArray<SampleType>& Samples, Projection Project)
{
	TArray<double> Values;
	Values.Reserve(Samples.Num());
	for (const auto& Sample : Samples)
	{
		Values.Add(Project(Sample));
	}
	Values.Sort();
	const int32 Middle = Values.Num() / 2;
	return (Values[(Values.Num() - 1) / 2] + Values[Middle]) * 0.5;
}

FVector MarkerMedianCenter(const TArray<FVRCalMarkerObservation>& Samples)
{
	return FVector(
		MarkerMedian(Samples, [](const auto& S) { return S.CenterStage.X; }),
		MarkerMedian(Samples, [](const auto& S) { return S.CenterStage.Y; }),
		MarkerMedian(Samples, [](const auto& S) { return S.CenterStage.Z; }));
}

double MarkerSpreadMm(const TArray<FVRCalMarkerObservation>& Samples, const FVector& CenterStage)
{
	TArray<double> DistancesMm;
	DistancesMm.Reserve(Samples.Num());
	for (const auto& Sample : Samples)
	{
		DistancesMm.Add(FVector::Distance(Sample.CenterStage, CenterStage) * 10);
	}
	DistancesMm.Sort();
	// Nearest rank (K1 §2)
	return DistancesMm[FMath::CeilToInt32(0.8 * DistancesMm.Num()) - 1];
}
}

AVRCalMarkerCalibrator::AVRCalMarkerCalibrator()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AVRCalMarkerCalibrator::BeginPlay()
{
	Super::BeginPlay();
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		WorldLockSubsystem = GameInstance->GetSubsystem<UMRUKSubsystem>();
		if (UMRUKSubsystem* Subsystem = WorldLockSubsystem.Get())
		{
			bPreviousWorldLock = Subsystem->EnableWorldLock;
			Subsystem->EnableWorldLock = false;
		}
	}

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (auto* Candidate = It->FindComponentByClass<UVRCalAlignmentComponent>())
		{
			if (Alignment.IsValid())
			{
				Alignment.Reset();
				Publish(TEXT("Multiple vehicle alignment components"));
				break;
			}
			Alignment = Candidate;
		}
	}

	if (Alignment.IsValid())
	{ Alignment->OnPointRecord.AddUObject(this, &AVRCalMarkerCalibrator::HandleAlignmentRecord); }

	if (APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0))
	{
		EnableInput(Controller);
		InputComponent->BindKey(EKeys::M, IE_Pressed, this, &AVRCalMarkerCalibrator::ToggleCapture);
		InputComponent->BindKey(EKeys::G, IE_Pressed, this, &AVRCalMarkerCalibrator::ApplyFit);
		InputComponent->BindKey(EKeys::H, IE_Pressed, this, &AVRCalMarkerCalibrator::RestorePawn);
		InputComponent->BindKey(EKeys::S, IE_Pressed, this, &AVRCalMarkerCalibrator::SaveFit);
	}
	RecenterHandle = FCoreDelegates::VRHeadsetRecenter.AddUObject(this, &AVRCalMarkerCalibrator::CancelForRecenter);
	Publish(TEXT("M: Record or cancel   G: Preview fit   H: Restore   S: Save fit"));
	bCameraStarted = StartCamera();
	if (!bCameraStarted)
	{
		Publish(TEXT("Camera start failed — check Link settings (Passthrough over Link, camera API permission)"));
	}
}

void AVRCalMarkerCalibrator::EndPlay(EEndPlayReason::Type Reason)
{
	if (Alignment.IsValid()) { Alignment->OnPointRecord.RemoveAll(this); }
	FCoreDelegates::VRHeadsetRecenter.Remove(RecenterHandle);
	StopCamera();
	if (UMRUKSubsystem* Subsystem = WorldLockSubsystem.Get())
	{
		Subsystem->EnableWorldLock = bPreviousWorldLock;
	}
	Super::EndPlay(Reason);
}

void AVRCalMarkerCalibrator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = GetWorld()->GetRealTimeSeconds();
	if (bCapturing && Now >= CaptureStart + MarkerSessionSeconds)
	{
		FinishCapture(Now);
	}
	if (bCameraStarted && (!bModelChecked || bCapturing))
	{
		FVRCalCameraFrame Frame;
		if (ReadCameraFrame(Frame, bCapturing))
		{
			ProcessFrame(Frame, Now);
		}
	}
	UpdateVisuals();
}

void AVRCalMarkerCalibrator::ToggleCapture()
{
	if (bCapturing)
	{
		bCapturing = false;
		Markers.Reset();
		Orders.Reset();
		Publish(TEXT("Marker capture canceled"));
		return;
	}
	StartCapture(GetWorld()->GetRealTimeSeconds());
}

void AVRCalMarkerCalibrator::StartCapture(double Now)
{
	if (!bCameraStarted)
	{
		bCameraStarted = StartCamera();
		if (!bCameraStarted)
		{
			Publish(TEXT("Camera start failed — check Link settings (Passthrough over Link, camera API permission)"));
			return;
		}
	}
	if (bModelChecked && !bModelValid)
	{
		Publish(TEXT("Camera model mismatch — capture unavailable"));
		return;
	}
	if (!Alignment.IsValid() || !IsValid(ReadPawn()))
	{
		Publish(TEXT("Vehicle alignment or player pawn unavailable"));
		return;
	}
	if (GetMarkerArrowWorld(*Alignment->GetOwner()).IsZero())
	{
		Publish(TEXT("Marker arrow direction invalid"));
		return;
	}

	const FString ProfileFailure = Alignment->ValidateBuckProfiles();
	if (!ProfileFailure.IsEmpty())
	{
		Publish(TEXT("Buck profiles invalid — ") + ProfileFailure);
		return;
	}
	TArray<UVRCalAlignPointComponent*> Points;
	TMap<int32, int32> BuckIndices;
	if (Alignment->Bucks.IsEmpty())
	{
		Alignment->GetOwner()->GetComponents(Points);
		Points.Sort([](const UVRCalAlignPointComponent& A, const UVRCalAlignPointComponent& B) { return A.Order < B.Order; });
		if (Points.Num() != 4)
		{
			Publish(TEXT("Expected four vehicle alignment points"));
			return;
		}
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const int32 Order = Points[Index]->Order;
			if (Order < 1 || Order >= 50 || (Index > 0 && Order == Points[Index - 1]->Order))
			{
				Publish(TEXT("Expected distinct alignment point orders from 1 to 49"));
				return;
			}
		}
	}
	else
	{
		for (int32 Index = 0; Index < Alignment->Bucks.Num(); ++Index)
		{
			const auto& Buck = Alignment->Bucks[Index];
			TArray<UVRCalAlignPointComponent*> BuckPoints;
			Alignment->FindAlignPoints(BuckPoints, Buck.Name);
			if (BuckPoints.Num() != 4)
			{
				Publish(FString::Printf(TEXT("Expected four vehicle alignment points for buck %s"), *Buck.Name));
				return;
			}
			for (const auto* Point : BuckPoints) { BuckIndices.Add(Buck.FirstMarkerId + Point->Order - 1, Index); }
		}
	}
	Markers.Reset();
	Orders.Reset();
	for (auto* Point : Points)
	{
		Orders.Add(Point->Order);
		Markers.Add(Point->Order).Point = Point;
	}
	MarkerBuckIndices = MoveTemp(BuckIndices);
	IgnoredMarkerCounts.Reset();
	SessionBuck.Reset();
	Fit.Reset();
	LastCameraRotation.Reset();
	if (Camera)
	{
		LastTimestamp = Camera->GetTimestamp();
	}
	CaptureStart = Now;
	bCapturing = true;
	Publish(TEXT("Marker capture started"));
}

FVector AVRCalMarkerCalibrator::GetMarkerArrowWorld(const AActor& Car) const
{
	if (MarkerArrowLocal.ContainsNaN())
	{
		return FVector::ZeroVector;
	}
	return Car.GetActorTransform().TransformVectorNoScale(MarkerArrowLocal).GetSafeNormal2D();
}

bool AVRCalMarkerCalibrator::CheckModel(const FVRCalCameraFrame& Frame)
{
	double MaxError = 0;
	for (const FVector Local : {FVector(70, 3, 8), FVector(82, -17, -12), FVector(95, 11, 18)})
	{
		const FVector2D Expected = Frame.Model.Project(Frame.Model.Pose.TransformPosition(Local));
		// MRUK treats local +Z as image down (K1 §2)
		const FVector Reflected(Local.X, Local.Y, -Local.Z);
		const FVector2D Actual = ProjectMruk(Frame.Model.Pose.TransformPosition(Reflected)) *
			FVector2D(Frame.Size.X, Frame.Size.Y);
		const double Error = FVector2D::Distance(Expected, Actual);
		MaxError = FMath::IsFinite(Error) ? FMath::Max(MaxError, Error) : TNumericLimits<double>::Max();
	}
	const bool bValid = MaxError <= 0.5;
	Publish(bValid ? FString::Printf(TEXT("Camera model check passed (%.3f px)"), MaxError) :
		FString::Printf(TEXT("Camera model mismatch %.3g px (MRUK projection convention changed?)"), MaxError));
	return bValid;
}

void AVRCalMarkerCalibrator::ProcessFrame(FVRCalCameraFrame& Frame, double Now)
{
	if (Frame.Timestamp <= LastTimestamp)
	{
		return;
	}
	const double Interval = (Frame.Timestamp - LastTimestamp).GetTotalSeconds();
	LastTimestamp = Frame.Timestamp;
	if (!bModelChecked)
	{
		bModelChecked = true;
		bModelValid = CheckModel(Frame);
		if (!bModelValid)
		{
			bCapturing = false;
			return;
		}
	}
	const APawn* Pawn = ReadPawn();
	if (!bCapturing || !bModelValid || !IsValid(Pawn))
	{
		return;
	}
	const FQuat Rotation = Frame.Model.Pose.GetRotation();
	const bool bFastHead = LastCameraRotation.IsSet() &&
		FMath::RadiansToDegrees(LastCameraRotation->AngularDistance(Rotation)) > MarkerMaxHeadDegPerSec * Interval;
	LastCameraRotation = Rotation;
	const FTransform PawnWorld = Pawn->GetActorTransform();
	DetectVRCalMarkers(Frame, [this, &Frame, &PawnWorld, bFastHead](int32 Id, const FVector2f (&Corners)[4])
	{
		const FMarker* Marker = Markers.Find(Id);
		if (!Marker && !MarkerBuckIndices.Contains(Id)) { return; }
		if (Marker && Marker->bComplete) { return; }
		if (!Marker && !SessionBuck.IsEmpty())
		{
			RecordObservation(Id, nullptr, bFastHead);
			return;
		}
		FVRCalMarkerObservation Observation;
		const bool bValid = !bFastHead && SolveVRCalMarker(Corners, MarkerSizeMm, Frame.Model, PawnWorld, Observation);
		RecordObservation(Id, bValid ? &Observation : nullptr, bFastHead);
	});
	if (!Orders.IsEmpty() && Algo::AllOf(Orders, [this](int32 Id) { return Markers[Id].bComplete; }))
	{
		FinishCapture(Now);
	}
}

void AVRCalMarkerCalibrator::RecordObservation(int32 Id, const FVRCalMarkerObservation* Observation, bool bFastHead)
{
	FMarker* Marker = Markers.Find(Id);
	const bool bAccepted = !bFastHead && Observation && Observation->ReprojectionPx <= MarkerReprojLimitPx &&
		Observation->SidePx >= MarkerMinSidePx;
	if (!Marker)
	{
		const int32* Index = MarkerBuckIndices.Find(Id);
		if (!Index || !Alignment.IsValid() || !Alignment->Bucks.IsValidIndex(*Index)) { return; }
		if (!SessionBuck.IsEmpty())
		{
			++IgnoredMarkerCounts.FindOrAdd(Id);
			return;
		}
		if (!bAccepted) { return; }
		const auto& Buck = Alignment->Bucks[*Index];
		TArray<UVRCalAlignPointComponent*> Points;
		if (!Alignment->FindAlignPoints(Points, Buck.Name).IsEmpty() || Points.Num() != 4) { return; }
		SessionBuck = Buck.Name;
		for (auto* Point : Points)
		{
			const int32 MarkerId = Buck.FirstMarkerId + Point->Order - 1;
			Orders.Add(MarkerId);
			Markers.Add(MarkerId).Point = Point;
		}
		Marker = Markers.Find(Id);
		Publish(TEXT("Marker capture started — buck ") + SessionBuck);
	}
	if (!Marker || Marker->bComplete) { return; }
	if (!bAccepted)
	{
		++Marker->Rejected;
		return;
	}
	for (const auto& Sample : Marker->Samples)
	{
		Marker->MaxPairMm = FMath::Max(Marker->MaxPairMm,
			FVector::Distance(Sample.CenterStage, Observation->CenterStage) * 10);
	}
	Marker->Samples.Add(*Observation);
	Marker->bComplete = Marker->Samples.Num() >= FMath::Max(1, MarkerMinFrames);
	Marker->CenterStage = Observation->CenterStage;
	if (Marker->bComplete)
	{
		Marker->CenterStage = MarkerMedianCenter(Marker->Samples);
		Marker->SpreadMm = MarkerSpreadMm(Marker->Samples, Marker->CenterStage);
		Marker->bRejected = Marker->SpreadMm > MarkerSpreadLimitMm;
	}
}

void AVRCalMarkerCalibrator::FinishCapture(double Now)
{
	bCapturing = false;
	Publish(FString::Printf(TEXT("Marker session %.2f s"), Now - CaptureStart));
	if (!MarkerBuckIndices.IsEmpty())
	{
		if (SessionBuck.IsEmpty()) { Publish(TEXT("Incomplete — no buck identified")); return; }
		if (Alignment.IsValid())
		{
			TArray<int32> Ids;
			IgnoredMarkerCounts.GetKeys(Ids);
			Ids.Sort();
			for (int32 Id : Ids)
			{
				const int32 Index = MarkerBuckIndices[Id];
				if (!Alignment->Bucks.IsValidIndex(Index)) { continue; }
				Publish(FString::Printf(TEXT("Ignored markers from buck %s: ID %d ×%d"),
					*Alignment->Bucks[Index].Name, Id, IgnoredMarkerCounts[Id]));
			}
		}
	}
	bool bReady = true;
	TArray<FVector> Stage;
	TArray<FVector> World;
	for (int32 Id : Orders)
	{
		const FMarker& Marker = Markers[Id];
		const auto* Point = Marker.Point.Get();
		const FString Label = Point && !Point->Label.IsEmpty() ? FString::Printf(TEXT(" (%s)"), *Point->Label) : FString();
		double ErrorSum = 0;
		for (const auto& Sample : Marker.Samples)
		{
			ErrorSum += Sample.ReprojectionPx;
		}
		const double SpreadMm = !Marker.bComplete && !Marker.Samples.IsEmpty() ?
			MarkerSpreadMm(Marker.Samples, MarkerMedianCenter(Marker.Samples)) : Marker.SpreadMm;
		Publish(FString::Printf(TEXT("Marker %d%s | spread %.2f mm | max pair %.2f mm | accepted %d | rejected %d | mean reprojection %s"),
			Id, *Label, SpreadMm, Marker.MaxPairMm, Marker.Samples.Num(), Marker.Rejected,
			Marker.Samples.IsEmpty() ? TEXT("unavailable") :
			*FString::Printf(TEXT("%.2f px"), ErrorSum / Marker.Samples.Num())));
		if (!Marker.bComplete)
		{
			Publish(FString::Printf(TEXT("Incomplete — missing ID %d (%d/%d frames)"), Id, Marker.Samples.Num(), MarkerMinFrames));
			bReady = false;
		}
		else if (Marker.bRejected)
		{
			Publish(FString::Printf(TEXT("Marker %d rejected: spread %.2f mm"), Id, Marker.SpreadMm));
			bReady = false;
		}
		if (Point)
		{
			Stage.Add(Marker.CenterStage);
			World.Add(Alignment->GetOwner()->GetActorTransform().InverseTransformPosition(Point->GetComponentLocation()));
		}
	}
	if (!bReady)
	{
		return;
	}

	const auto* Settings = Alignment.Get();
	const auto* Pawn = ReadPawn();
	FVRCalPointFit Candidate;
	if (!Settings || !IsValid(Pawn) || Stage.Num() != Orders.Num() ||
		!FVRCalAlignment::FitPoints(Stage, World, Settings->FloorLocalZCm, Candidate))
	{
		Publish(TEXT("Marker fit unavailable"));
		return;
	}
	const AActor& Car = *Settings->GetOwner();
	const FVector CarFrontWorld = GetMarkerArrowWorld(Car);
	if (CarFrontWorld.IsZero())
	{
		Publish(TEXT("Marker arrow direction invalid"));
		return;
	}
	const FVector CarFrontStage = Candidate.Pawn.InverseTransformVectorNoScale(MarkerArrowLocal.GetSafeNormal2D());
	const double CarYaw = CarFrontStage.Rotation().Yaw;
	bool bArrowRejected = false;
	for (int32 Index = 0; Index < Orders.Num(); ++Index)
	{
		const int32 Id = Orders[Index];
		FMarker& Marker = Markers[Id];
		Candidate.HeightCm[Index] -= MarkerHeightOffsetCm;
		Publish(FString::Printf(TEXT("Marker %d | XY %.2f cm | height %+.2f cm"),
			Id, Candidate.XYCm[Index], Candidate.HeightCm[Index]));
		const FVector MeasuredWorld = Candidate.Pawn.TransformPosition(Stage[Index]);
		const FVector Residual = MeasuredWorld - World[Index];
		Publish(FString::Printf(TEXT("Marker %d | Stage (%.2f, %.2f, %.2f) cm | car-local residual X %+.2f cm | Y %+.2f cm"),
			Id, Stage[Index].X, Stage[Index].Y, Stage[Index].Z, Residual.X, Residual.Y), false);

		const double Tilt = MarkerMedian(Marker.Samples, [](const auto& S) { return S.TiltDeg; });
		if (Tilt > 60)
		{
			Publish(FString::Printf(TEXT("Marker %d arrow check skipped (tilt %.1f deg)"), Id, Tilt));
			continue;
		}
		// Unwrap around a measured arrow before taking its median (wrap at 180 deg)
		const double AnchorYaw = Marker.Samples[0].ArrowYawDeg;
		const double ArrowYaw = AnchorYaw + MarkerMedian(Marker.Samples, [AnchorYaw](const auto& S)
		{
			return FMath::FindDeltaAngleDegrees(AnchorYaw, S.ArrowYawDeg);
		});
		const double Error = FMath::Abs(FMath::FindDeltaAngleDegrees(CarYaw, FRotator::NormalizeAxis(ArrowYaw)));
		if (Error > MarkerYawLimitDeg)
		{
			Marker.bRejected = true;
			bArrowRejected = true;
			Publish(FString::Printf(TEXT("Marker %d arrow %.0f deg off car front — markers swapped or rotated?"), Id, Error));
		}
	}
	for (int32 A = 0; A < Orders.Num(); ++A)
	{
		for (int32 B = A + 1; B < Orders.Num(); ++B)
		{
			const double DistanceDelta = FVector::Dist2D(Stage[A], Stage[B]) - FVector::Dist2D(World[A], World[B]);
			Publish(FString::Printf(TEXT("Marker pair %d-%d | XY distance delta %+.2f cm"),
				Orders[A], Orders[B], DistanceDelta), false);
		}
	}
	if (bArrowRejected)
	{
		return;
	}

	const FTransform Baseline = OriginalPawnPose.IsSet() && PreviewPawn.Get() == Pawn ?
		OriginalPawnPose.GetValue() : Pawn->GetActorTransform().GetRelativeTransform(Car.GetActorTransform());
	const FVector Delta = Candidate.Pawn.GetLocation() - Baseline.GetLocation();
	const double DeltaYaw = FMath::FindDeltaAngleDegrees(Baseline.Rotator().Yaw, Candidate.Pawn.Rotator().Yaw);
	const double At70 = 70 * FMath::Abs(FMath::Tan(FMath::DegreesToRadians(DeltaYaw)));
	const FString State = StaticEnum<EVRCalAlignmentState>()->GetNameStringByValue(static_cast<int64>(Settings->State));
	Publish(FString::Printf(TEXT("Max XY %.2f cm / %.2f cm%s"), Candidate.MaxXYCm, Settings->PointFitLimitCm,
		Candidate.MaxXYCm > Settings->PointFitLimitCm ? TEXT(" (over limit)") : TEXT("")));
	Publish(FString::Printf(TEXT("Marker fit vs pawn (%s) | dX %+.2f cm | dY %+.2f cm | dYaw %+.2f deg | at 70 cm %.2f cm"),
		*State, Delta.X, Delta.Y, DeltaYaw, At70));
	Fit = MoveTemp(Candidate);
}

void AVRCalMarkerCalibrator::SaveFit()
{
	if (!Fit.IsSet() || !Alignment.IsValid())
	{
		Publish(TEXT("No marker fit to save"));
		return;
	}
	TArray<FVRCalAlignPointSample> Samples;
	for (int32 Id : Orders)
	{
		const FMarker& Marker = Markers[Id];
		const auto* Point = Marker.Point.Get();
		if (!Point) { Publish(TEXT("Marker save rejected — alignment points changed")); return; }
		FVRCalAlignPointSample& Sample = Samples.AddDefaulted_GetRef();
		Sample.Order = Point->Order;
		Sample.StageCm = Marker.CenterStage;
		Sample.SpreadMm = Marker.SpreadMm;
		Sample.Frames = Marker.Samples.Num();
	}
	FString Failure;
	if (!Alignment->SaveMarkerPoints(SessionBuck, Samples, Failure))
	{
		Publish(TEXT("Marker save rejected — ") + Failure);
		return;
	}
	const auto* AnchorSwitch = IConsoleManager::Get().FindConsoleVariable(TEXT("VRCal.AnchorAlignment"));
	// AnchorResult supplies wait/success/failure lines; don't invite an early EndPlay
	if (!AnchorSwitch || AnchorSwitch->GetInt() == 0)
	{ Publish(SessionBuck.IsEmpty() ? TEXT("Saved alignment — open L_Sample") : TEXT("Saved for buck ") + SessionBuck + TEXT(" — open L_Sample")); }
}

void AVRCalMarkerCalibrator::HandleAlignmentRecord(const FString& Line)
{
	Publish(Line);
}

void AVRCalMarkerCalibrator::CancelForRecenter()
{
	if (bCapturing)
	{
		bCapturing = false;
		Publish(TEXT("Recenter — marker capture canceled"));
	}
	Markers.Reset();
	Orders.Reset();
	Fit.Reset();
}

void AVRCalMarkerCalibrator::ApplyFit()
{
	APawn* Pawn = ReadPawn();
	if (!Fit.IsSet() || !IsValid(Pawn) || !Alignment.IsValid())
	{
		Publish(TEXT("No marker fit"));
		return;
	}
	if (PreviewPawn.Get() != Pawn || !OriginalPawnPose.IsSet())
	{
		OriginalPawnPose = Pawn->GetActorTransform().GetRelativeTransform(Alignment->GetOwner()->GetActorTransform());
		OriginalParent = Pawn->GetAttachParentActor();
		PreviewPawn = Pawn;
	}
	Pawn->AttachToActor(Alignment->GetOwner(), FAttachmentTransformRules::KeepWorldTransform);
	Pawn->SetActorRelativeTransform(Fit->Pawn, false, nullptr, ETeleportType::TeleportPhysics);
	if (Alignment.IsValid())
	{
		TArray<UVRCalAlignPointComponent*> Points;
		Alignment->GetOwner()->GetComponents(Points);
		for (auto* Point : Points)
		{
			Point->ShowPoint(Alignment->Bucks.IsEmpty() || Point->Buck == SessionBuck);
		}
	}
	Publish(TEXT("Marker fit applied temporarily"));
}

void AVRCalMarkerCalibrator::RestorePawn()
{
	if (!OriginalPawnPose.IsSet())
	{
		return;
	}
	if (APawn* Pawn = PreviewPawn.Get(); Pawn && Alignment.IsValid())
	{
		const FTransform WorldPose = OriginalPawnPose.GetValue() * Alignment->GetOwner()->GetActorTransform();
		if (OriginalParent.IsValid()) { Pawn->AttachToActor(OriginalParent.Get(), FAttachmentTransformRules::KeepWorldTransform); }
		else { Pawn->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform); }
		Pawn->SetActorTransform(WorldPose, false, nullptr, ETeleportType::TeleportPhysics);
	}
	OriginalPawnPose.Reset();
	PreviewPawn.Reset();
	if (Alignment.IsValid())
	{
		TArray<UVRCalAlignPointComponent*> Points;
		Alignment->GetOwner()->GetComponents(Points);
		for (auto* Point : Points)
		{
			Point->ShowPoint(false);
		}
	}
	Publish(TEXT("Previous pawn pose restored"));
}

APawn* AVRCalMarkerCalibrator::ReadPawn() const
{
	return UGameplayStatics::GetPlayerPawn(this, 0);
}

void AVRCalMarkerCalibrator::UpdateVisuals()
{
	const auto* Pawn = ReadPawn();
	if (!IsValid(Pawn))
	{
		return;
	}
	for (const auto& Entry : Markers)
	{
		const FMarker& Marker = Entry.Value;
		if (!Marker.Samples.IsEmpty())
		{
			const FColor Color = Marker.bRejected ? FColor::Red : (Marker.bComplete ? FColor::Green : FColor::Yellow);
			DrawDebugSphere(GetWorld(), Pawn->GetActorTransform().TransformPosition(Marker.CenterStage), 1, 12, Color);
		}
	}
}

void AVRCalMarkerCalibrator::Publish(const FString& Line, bool bShowOnScreen)
{
	UE_LOG(LogVRCalMarker, Log, TEXT("%s"), *Line);
	if (bShowOnScreen && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 10, FColor::Cyan, Line);
	}
#if WITH_DEV_AUTOMATION_TESTS
	if (bCollectTestMessages)
	{
		TestMessages.Add(Line);
	}
#endif
}
