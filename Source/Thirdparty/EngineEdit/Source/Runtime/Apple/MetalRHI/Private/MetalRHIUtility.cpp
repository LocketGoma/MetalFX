#include "MetalRHIUtility.h"
#include "MetalRHI.h"
#include "MetalRHIPrivate.h"
#include "MetalRHIContext.h"
#include "MetalCommandBuffer.h"

struct FMetalRHIExternalCommands::FImpl
{
	MTLEventPtr Event;
	MTL::Device* Device = nullptr;
	uint32 Value = 0;
	FMetalRHICommandContext* ActiveContext = nullptr;
	MTL::CommandBuffer* ActiveCommandBuffer = nullptr;
};

FMetalRHIExternalCommands::FMetalRHIExternalCommands()
	: Impl(MakeUnique<FImpl>())
{
}

FMetalRHIExternalCommands::~FMetalRHIExternalCommands() = default;

FMetalCommandBuffer* FMetalRHIExternalCommands::Begin(FRHICommandList& CmdList)
{
	FMetalRHICommandContext& Context = FMetalRHICommandContext::Get(CmdList);
	if (!ensureMsgf(!Impl->ActiveContext && !Context.IsInsideRenderPass(),
		TEXT("External Metal commands require a non-nested scope outside a render pass.")))
	{
		return nullptr;
	}

	FMetalCommandBuffer* CommandBuffer = Context.GetCurrentCommandBuffer();
	MTL::CommandBuffer* NativeCommandBuffer = CommandBuffer ? CommandBuffer->GetMTLCmdBuffer() : nullptr;
	if (!NativeCommandBuffer)
	{
		return nullptr;
	}

	MTL::Device* Device = NativeCommandBuffer->device();
	if (!Impl->Event || Impl->Device != Device || Impl->Value > MAX_uint32 - 2)
	{
		MTLEventPtr Event = NS::TransferPtr(Device->newEvent());
		if (!Event)
		{
			UE_LOG(LogMetal, Error, TEXT("Could not create an event for external Metal commands."));
			return nullptr;
		}
		Impl->Event = MoveTemp(Event);
		Impl->Device = Device;
		Impl->Value = 0;
	}

	// SignalEvent closes the active encoder through the existing public RHI API.
	// The matching wait orders external access to untracked resources after prior GPU work.
	Context.SignalEvent(Impl->Event, ++Impl->Value);
	Context.WaitForEvent(Impl->Event, Impl->Value);
	Context.GetStateCache().ResetBindings();
	Context.GetStateCache().SetStateDirty();
	Impl->ActiveContext = &Context;
	Impl->ActiveCommandBuffer = NativeCommandBuffer;
	return CommandBuffer;
}

void FMetalRHIExternalCommands::End()
{
	if (!Impl->ActiveContext)
	{
		return;
	}

	// Order subsequent RHI passes after the external encoder's writes.
	Impl->ActiveContext->SignalEvent(Impl->Event, ++Impl->Value);
	Impl->ActiveContext->WaitForEvent(Impl->Event, Impl->Value);
	Impl->ActiveContext->GetStateCache().ResetBindings();
	Impl->ActiveContext->GetStateCache().SetStateDirty();

	// RHI command buffers may use unretained references. Keep the event alive through GPU completion.
	MTL::HandlerFunction KeepEventAlive = [Event = Impl->Event](MTL::CommandBuffer*) { (void)Event.get(); };
	Impl->ActiveCommandBuffer->addCompletedHandler(KeepEventAlive);
	Impl->ActiveContext = nullptr;
	Impl->ActiveCommandBuffer = nullptr;
}
