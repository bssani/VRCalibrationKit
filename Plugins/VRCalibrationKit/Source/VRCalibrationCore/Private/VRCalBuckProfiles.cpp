#include "VRCalAlignmentComponent.h"
#include "VRCalAlignPointComponent.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

FString UVRCalAlignmentComponent::CalibrationDirectory() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Calibration"));
}

FString UVRCalAlignmentComponent::BuckAlignmentPath(const FString& Name) const
{
	return FPaths::Combine(CalibrationDirectory(), TEXT("Bucks"), Name + TEXT(".json"));
}

FString UVRCalAlignmentComponent::ValidateBuckProfiles() const
{
	if (Bucks.IsEmpty()) { return {}; }
	TSet<FString> Names;
	for (const FVRCalBuckProfile& Buck : Bucks)
	{
		if (Buck.Name.TrimStartAndEnd().IsEmpty()) { return TEXT("Buck name is empty"); }
		if (Buck.Name != Buck.Name.TrimStartAndEnd() || Buck.Name.EndsWith(TEXT(".")) ||
			Buck.Name.Contains(TEXT("/")) || Buck.Name.Contains(TEXT("\\")) || Buck.Name.Contains(TEXT(":")) ||
			Buck.Name.Contains(TEXT("\r")) || Buck.Name.Contains(TEXT("\n")) ||
			!FPaths::ValidatePath(TEXT("Bucks/") + Buck.Name + TEXT(".json")))
		{
			return FString::Printf(TEXT("Invalid buck name %s"), *Buck.Name);
		}
		const FString FileKey = Buck.Name.ToLower();
		if (Names.Contains(FileKey)) { return FString::Printf(TEXT("Duplicate buck name %s"), *Buck.Name); }
		Names.Add(FileKey);
	}

	TArray<UVRCalAlignPointComponent*> Points;
	GetOwner()->GetComponents(Points);
	for (const auto* Point : Points)
	{
		if (!Bucks.ContainsByPredicate([Point](const FVRCalBuckProfile& Buck) { return Buck.Name == Point->Buck; }))
		{
			return FString::Printf(TEXT("Alignment point %s has unknown buck %s"), *Point->GetName(),
				Point->Buck.IsEmpty() ? TEXT("(empty)") : *Point->Buck);
		}
	}

	TSet<int32> UsedIds;
	for (const FVRCalBuckProfile& Buck : Bucks)
	{
		Points.Reset();
		const FString Failure = FindAlignPoints(Points, Buck.Name);
		if (!Failure.IsEmpty()) { return FString::Printf(TEXT("Buck %s: %s"), *Buck.Name, *Failure); }
		if (Buck.FirstMarkerId < 0 || Buck.FirstMarkerId >= 50)
		{
			return FString::Printf(TEXT("Marker ID range outside 0–49 for buck %s"), *Buck.Name);
		}
		for (const auto* Point : Points)
		{
			const int64 Id = static_cast<int64>(Buck.FirstMarkerId) + Point->Order - 1;
			if (Id >= 50) { return FString::Printf(TEXT("Marker ID range outside 0–49 for buck %s"), *Buck.Name); }
			if (UsedIds.Contains(static_cast<int32>(Id)))
			{
				return FString::Printf(TEXT("Marker ID %d overlaps for buck %s"), static_cast<int32>(Id), *Buck.Name);
			}
			UsedIds.Add(static_cast<int32>(Id));
		}
	}
	return {};
}

FString UVRCalAlignmentComponent::ActiveBuckName() const
{
	return Bucks.IsValidIndex(ActiveBuckIndex) ? Bucks[ActiveBuckIndex].Name : FString();
}

void UVRCalAlignmentComponent::ReadActiveBuck(FString& FallbackReason)
{
	ActiveBuckIndex = 0;
	FString Name;
	if (FFileHelper::LoadFileToString(Name, *FPaths::Combine(CalibrationDirectory(), TEXT("ActiveBuck.txt"))))
	{
		Name.TrimStartAndEndInline();
		const int32 Index = Bucks.IndexOfByPredicate([&Name](const FVRCalBuckProfile& Buck) { return Buck.Name == Name; });
		if (Index != INDEX_NONE) { ActiveBuckIndex = Index; return; }
	}
	FallbackReason = TEXT("Active buck file missing — using ") + ActiveBuckName();
}

bool UVRCalAlignmentComponent::WriteActiveBuck(const FString& Name, FString& Failure) const
{
	const FString Directory = CalibrationDirectory();
#if PLATFORM_WINDOWS
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, TEXT("ActiveBuck.txt")));
	const FString Candidate = Path + TEXT(".") + FGuid::NewGuid().ToString();
	IFileManager& Files = IFileManager::Get();
	// Leave the old selection readable if write or replacement fails (K1 §2)
	if (Files.MakeDirectory(*Directory, true) && !Files.IsReadOnly(*Path) &&
		FFileHelper::SaveStringToFile(Name + TEXT("\n"), *Candidate, FFileHelper::EEncodingOptions::ForceUTF8) &&
		::MoveFileExW(*Candidate, *Path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		return true;
	}
	Files.Delete(*Candidate, false, false, true);
#endif
	Failure = TEXT("ActiveBuck.txt write failed");
	return false;
}

FString UVRCalAlignmentComponent::BuckStatus() const
{
	return Bucks.IsValidIndex(ActiveBuckIndex) ?
		FString::Printf(TEXT("Buck: %s (%d/%d)"), *ActiveBuckName(), ActiveBuckIndex + 1, Bucks.Num()) : FString();
}

FString UVRCalAlignmentComponent::NextBuckRejection() const
{
	if (Bucks.Num() <= 1) { return TEXT("One buck or none"); }
	if (IsFindingAnchor()) { return TEXT("Finding anchor"); }
	const FString Failure = ValidateBuckProfiles();
	return Failure.IsEmpty() ? FString() : TEXT("Buck profiles invalid — ") + Failure;
}

bool UVRCalAlignmentComponent::SelectNextBuck(FString& Name, FString& Failure) const
{
	Failure = NextBuckRejection();
	if (!Failure.IsEmpty()) { return false; }
	// Keep the current point set until the level reloads (K1 §2)
	Name = Bucks[(ActiveBuckIndex + 1) % Bucks.Num()].Name;
	return WriteActiveBuck(Name, Failure);
}

bool UVRCalAlignmentComponent::SaveMarkerPoints(const FString& BuckName,
	const TArray<FVRCalAlignPointSample>& Samples, FString& Failure)
{
	const FString ProfileFailure = ValidateBuckProfiles();
	if (!ProfileFailure.IsEmpty()) { Failure = TEXT("Buck profiles invalid — ") + ProfileFailure; return false; }
	if (!Bucks.IsEmpty() && !Bucks.ContainsByPredicate([&BuckName](const FVRCalBuckProfile& Buck) { return Buck.Name == BuckName; }))
	{
		Failure = TEXT("Unknown buck ") + BuckName;
		return false;
	}
	Failure = PointFitRejection(Samples, BuckName);
	if (!Failure.IsEmpty()) { return false; }

	FVRCalSavedAlignment Candidate;
	Candidate.Version = 2;
	Candidate.SavedAt = FDateTime::UtcNow().ToIso8601();
	ReadPointEnvironment(Candidate.TrackingOrigin, Candidate.PlayAreaCm);
	if (Candidate.TrackingOrigin != EHMDTrackingOrigin::Stage ||
		!FMath::IsNearlyEqual(GetWorld()->GetWorldSettings()->WorldToMeters, 100.f))
	{
		Failure = TEXT("Stage tracking origin and WorldToMeters 100 required");
		return false;
	}
	Candidate.Points = Samples;
	const FString Path = Bucks.IsEmpty() ? SavedAlignmentPath() : BuckAlignmentPath(BuckName);
	if (!FVRCalAlignmentStore::Save(Path, Candidate, Failure)) { return false; }
	SaveAlignmentAnchor(Path, Candidate);
	return Bucks.IsEmpty() || WriteActiveBuck(BuckName, Failure);
}
