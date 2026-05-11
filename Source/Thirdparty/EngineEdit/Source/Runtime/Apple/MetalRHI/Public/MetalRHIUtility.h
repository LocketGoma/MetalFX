#pragma once
// Metal RHI public headers.
#include "MetalThirdParty.h"
#include "MetalState.h"
#include "MetalResources.h"
#include "MetalViewport.h"
#include "MetalDevice.h"
#include "MetalCommandList.h"
#include "MetalCommandEncoder.h"
#include "MetalRHIRenderQuery.h"
#include "MetalBindlessDescriptors.h"

//Utility Class
class METALRHI_API FMetalRHIUtility
{
public:
	static FMetalCommandBuffer* GetCurrentCommandBuffer(FMetalRHICommandContext* Context);
}