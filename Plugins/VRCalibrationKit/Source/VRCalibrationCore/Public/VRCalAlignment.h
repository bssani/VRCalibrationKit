#pragma once

#include "CoreMinimal.h"

struct VRCALIBRATIONCORE_API FVRCalAlignPointSample
{
	int32 Order = 1;
	FVector StageCm = FVector::ZeroVector;
	double SpreadMm = 0;
	int32 Frames = 0;
};

struct VRCALIBRATIONCORE_API FVRCalPointFit
{
	FTransform Pawn;
	TArray<double> XYCm, HeightCm;
	double MaxXYCm = 0, SpanCm = 0;
};

struct VRCALIBRATIONCORE_API FVRCalSavedAlignment
{
	static constexpr int32 FormatVersion = 2;
	int32 Version = FormatVersion;
	TArray<FVRCalAlignPointSample> Points;
	FString AnchorUuid;
	FTransform AnchorStage = FTransform::Identity;
	FString SavedAt;
	int32 TrackingOrigin = 0;
	FVector2D PlayAreaCm = FVector2D::ZeroVector;
};

struct VRCALIBRATIONCORE_API FVRCalApplyDecision
{
	bool bApply = false;
	bool bNeedsCheck = false;
	FString Reason;
};

struct VRCALIBRATIONCORE_API FVRCalAlignment
{
	static bool FitPoints(const TArray<FVector>& Stage, const TArray<FVector>& World, double FloorZCm, FVRCalPointFit& Out);
	static FVRCalApplyDecision CheckEnvironment(const FVRCalSavedAlignment& Saved, int32 CurrentOrigin,
		const FVector2D& CurrentPlayAreaCm, double ToleranceCm);
};

struct VRCALIBRATIONCORE_API FVRCalAlignmentStore
{
	static bool Load(const FString& Path, FVRCalSavedAlignment& Out, FString& Reason);
	static bool Save(const FString& Path, const FVRCalSavedAlignment& Candidate, FString& Reason);
private:
#if WITH_DEV_AUTOMATION_TESTS && PLATFORM_WINDOWS
	friend struct FSaveTestAccess;
	static uint32 (*ReplaceForTest)(const FString& CandidatePath, const FString& Path);
#endif
};
