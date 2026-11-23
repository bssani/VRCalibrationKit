#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VRCalAlignment.h"
#include "VRCalMarkerCalibrator.generated.h"

class APawn;
class UMRUKPassthroughCameraAccess;
class UMRUKPassthroughCameraAccessTexture;
class UMRUKSubsystem;
class UVRCalAlignPointComponent;
class UVRCalAlignmentComponent;

struct FVRCalCameraModel
{
	FTransform Pose = FTransform::Identity;
	FVector2D FocalPx = FVector2D::ZeroVector;
	FVector2D PrincipalPx = FVector2D::ZeroVector;

	FVector2D Project(const FVector& World) const;
	FVector Ray(const FVector2D& Pixel) const;
};

struct FVRCalCameraFrame
{
	FDateTime Timestamp;
	FIntPoint Size = FIntPoint::ZeroValue;
	FVRCalCameraModel Model;
	TArray<FColor> Pixels;
};

struct FVRCalMarkerObservation
{
	FVector CenterStage = FVector::ZeroVector;
	double ArrowYawDeg = 0;
	double TiltDeg = 0;
	double ReprojectionPx = 0;
	double SidePx = 0;
};

UCLASS()
class VRCALIBRATIONMARKER_API AVRCalMarkerCalibrator : public AActor
{
	GENERATED_BODY()
public:
	AVRCalMarkerCalibrator();
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category="Marker alignment", meta=(Units="mm", ClampMin="1"))
	double MarkerSizeMm = 60;
	UPROPERTY(EditAnywhere, Category="Marker alignment", meta=(ClampMin="0"))
	double MarkerReprojLimitPx = 1;
	UPROPERTY(EditAnywhere, Category="Marker alignment", meta=(ClampMin="1"))
	double MarkerMinSidePx = 24;
	UPROPERTY(EditAnywhere, Category="Marker alignment", meta=(ClampMin="1"))
	int32 MarkerMinFrames = 30;
	UPROPERTY(EditAnywhere, Category="Marker alignment", meta=(Units="mm", ClampMin="0"))
	double MarkerSpreadLimitMm = 20;
	UPROPERTY(EditAnywhere, Category="Marker alignment", meta=(Units="s", ClampMin="0.01"))
	double MarkerSessionSeconds = 30;
	UPROPERTY(EditAnywhere, Category="Marker alignment", meta=(Units="cm"))
	double MarkerHeightOffsetCm = 0;
	UPROPERTY(EditAnywhere, Category="Marker alignment", meta=(ClampMin="0"))
	double MarkerMaxHeadDegPerSec = 10;
	UPROPERTY(EditAnywhere, Category="Marker alignment", meta=(Units="deg", ClampMin="0", ClampMax="180"))
	double MarkerYawLimitDeg = 30;
	UPROPERTY(EditAnywhere, Category="Marker alignment", meta=(ToolTip="Direction of printed FRONT arrows in vehicle actor space"))
	FVector MarkerArrowLocal = FVector::ForwardVector;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
	virtual bool StartCamera();
	virtual bool ReadCameraFrame(FVRCalCameraFrame& Frame, bool bReadPixels);
	virtual FVector2D ProjectMruk(const FVector& World) const;
	virtual APawn* ReadPawn() const;

private:
	friend struct FMarkerFixture;
	friend struct FProfileFixture;

	struct FMarker
	{
		TWeakObjectPtr<UVRCalAlignPointComponent> Point;
		TArray<FVRCalMarkerObservation> Samples;
		FVector CenterStage = FVector::ZeroVector;
		double SpreadMm = 0;
		double MaxPairMm = 0;
		int32 Rejected = 0;
		bool bComplete = false;
		bool bRejected = false;
	};

	void ToggleCapture();
	void StartCapture(double Now);
	FVector GetMarkerArrowWorld(const AActor& Car) const;
	void ProcessFrame(FVRCalCameraFrame& Frame, double Now);
	bool CheckModel(const FVRCalCameraFrame& Frame);
	void RecordObservation(int32 Id, const FVRCalMarkerObservation* Observation, bool bFastHead);
	void FinishCapture(double Now);
	void SaveFit();
	void HandleAlignmentRecord(const FString& Line);
	void CancelForRecenter();
	void ApplyFit();
	void RestorePawn();
	void UpdateVisuals();
	void Publish(const FString& Line, bool bShowOnScreen = true);
	void StopCamera();

	TWeakObjectPtr<UVRCalAlignmentComponent> Alignment;
	TMap<int32, FMarker> Markers;
	TArray<int32> Orders;
	TMap<int32, int32> MarkerBuckIndices;
	TMap<int32, int32> IgnoredMarkerCounts;
	FString SessionBuck;
	FVector2D CameraFocalPx = FVector2D::ZeroVector;
	FVector2D CameraPrincipalPx = FVector2D::ZeroVector;
	FDateTime LastTimestamp;
	TOptional<FQuat> LastCameraRotation;
	TOptional<FVRCalPointFit> Fit;
	TOptional<FTransform> OriginalPawnPose;
	TWeakObjectPtr<APawn> PreviewPawn;
	TWeakObjectPtr<AActor> OriginalParent;
	double CaptureStart = 0;
	bool bCapturing = false;
	bool bCameraStarted = false;
	bool bModelChecked = false;
	bool bModelValid = false;

#if WITH_DEV_AUTOMATION_TESTS
	bool bCollectTestMessages = false;
	TArray<FString> TestMessages;
#endif

	UPROPERTY(Transient) TObjectPtr<UMRUKPassthroughCameraAccess> Camera;
	UPROPERTY(Transient) TObjectPtr<UMRUKPassthroughCameraAccessTexture> CameraTexture;
	TWeakObjectPtr<UMRUKSubsystem> WorldLockSubsystem;
	bool bPreviousWorldLock = false;
	FDelegateHandle RecenterHandle;
};
