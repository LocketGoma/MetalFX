#include "MetalRHIUtility.h"
#include "MetalRHI.h"
#include "MetalRHIPrivate.h"
#include "MetalRHIContext.h"
#include "MetalCommandBuffer.h"

FMetalCommandBuffer* FMetalRHIUtility::GetCurrentCommandBuffer(FMetalRHICommandContext* Context)
{	
	if (Context)
	{
		return Context->GetCurrentCommandBuffer();
	}
	return nullptr;
}

FMetalCommandBuffer* FMetalRHIUtility::GetCurrentCommandBufferFromCmdList(FRHICommandList& CmdList)
{
	FMetalRHICommandContext& MetalContext = FMetalRHICommandContext::Get(CmdList);
	return GetCurrentCommandBuffer(&MetalContext);
}
