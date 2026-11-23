#pragma once

#include "VRCalAlignmentComponent.h"
#include "VRCalProfileTestAlignment.generated.h"

UCLASS(Transient)
class UVRCalProfileTestAlignment : public UVRCalAlignmentComponent
{
    GENERATED_BODY()
public:
    FString Directory;
    int32 Origin = 3;
    FVector2D Area = FVector2D(300,400);
    virtual FString SavedAlignmentPath() const override { return UVRCalAlignmentComponent::SavedAlignmentPath(); }
protected:
    virtual FString CalibrationDirectory() const override { return Directory; }
    virtual void ReadPointEnvironment(int32& OutOrigin, FVector2D& OutArea) const override { OutOrigin = Origin; OutArea = Area; }
};
