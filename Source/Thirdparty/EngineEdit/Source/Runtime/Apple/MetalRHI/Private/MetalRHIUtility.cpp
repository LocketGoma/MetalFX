#include "MetalRHIUtility.h"

FMetalCommandBuffer* FMetalRHIUtility::GetCurrentCommandBuffer(FMetalRHICommandContext* Context)
{
	if (Context)
	{
		return Context->GetCurrentCommandBuffer();
	}
}