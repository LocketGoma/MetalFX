#pragma once
// Metal RHI public headers.

class FMetalCommandBuffer;
class FMetalRHICommandContext;

//Utility Class
class METALRHI_API FMetalRHIUtility
{
	public:
	static FMetalCommandBuffer* GetCurrentCommandBuffer(FMetalRHICommandContext* Context);
	static FMetalCommandBuffer* GetCurrentCommandBufferFromCmdList(FRHICommandList& CmdList);
};
