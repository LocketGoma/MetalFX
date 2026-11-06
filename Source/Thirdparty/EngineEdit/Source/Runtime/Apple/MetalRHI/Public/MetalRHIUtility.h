#pragma once
#include "CoreMinimal.h"
// Metal RHI public headers.

class FMetalCommandBuffer;
class FRHICommandList;

// Owns native synchronization resources inside MetalRHI; no private engine types cross the module boundary.
class METALRHI_API FMetalRHIExternalCommands
{
public:
	FMetalRHIExternalCommands();
	~FMetalRHIExternalCommands();
	UE_NONCOPYABLE(FMetalRHIExternalCommands)

	FMetalCommandBuffer* Begin(FRHICommandList& CmdList);
	void End();

private:
	struct FImpl;
	TUniquePtr<FImpl> Impl;
};
