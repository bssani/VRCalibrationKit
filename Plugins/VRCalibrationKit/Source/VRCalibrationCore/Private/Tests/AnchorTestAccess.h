#pragma once

#if WITH_DEV_AUTOMATION_TESTS
#include "VRCalAlignmentComponent.h"
#include "Engine/World.h"
#include "OculusXRAnchorComponent.h"

struct FAnchorTestAccess
{
    static void Hook(UVRCalAlignmentComponent* A, int32& Requests)
    { A->AnchorRequestForTest = [&Requests] { ++Requests; }; }
    static void Load(UVRCalAlignmentComponent* A) { A->LoadSetupAlignment(true); }
    static void Found(UVRCalAlignmentComponent* A, const FTransform& Stage) { A->FinishAnchorDiscovery(&Stage, 0); }
    static void Timeout(UVRCalAlignmentComponent* A) { A->AnchorStarted = -11; A->AdvanceAnchorDiscovery(); }
    static void Located(UVRCalAlignmentComponent* A, AActor* Actor)
    {
        auto* Anchor = NewObject<UOculusXRAnchorComponent>(Actor);
        Actor->AddInstanceComponent(Anchor);
        Anchor->RegisterComponent();
        A->UseLocatedAnchor(Anchor);
        A->bAnchorLocationValidForTest = true;
    }
    static void Advance(UVRCalAlignmentComponent* A, bool bAfterCreation)
    {
        A->AnchorActorCreatedFrame = GFrameCounter - (bAfterCreation ? 1 : 0);
        A->AdvanceAnchorDiscovery();
    }
    static void Settle(UVRCalAlignmentComponent* A) { A->AnchorStableSince -= A->AnchorStableSeconds + 1; }
};
#endif
