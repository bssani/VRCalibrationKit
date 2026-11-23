#pragma once

#include "VRCalAlignmentComponent.h"
#include "VRCalCoreTestAlignment.generated.h"

UCLASS(Transient)
class UVRCalCoreTestAlignment : public UVRCalAlignmentComponent
{
    GENERATED_BODY()
public:
    FString TestPath;
    int32 Origin = 3;
    FVector2D Area = FVector2D(300, 400);
protected:
    virtual FString SavedAlignmentPath() const override { return TestPath; }
    virtual void ReadPointEnvironment(int32& OutOrigin, FVector2D& OutArea) const override { OutOrigin = Origin; OutArea = Area; }
};
