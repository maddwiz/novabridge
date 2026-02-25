#include "NovaBridgeRuntimeModule.h"

#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
#include "IWebSocketServer.h"
#endif

DEFINE_LOG_CATEGORY(LogNovaBridgeRuntime);

void FNovaBridgeRuntimeModule::StartupModule()
{
	if (GIsEditor)
	{
		UE_LOG(LogNovaBridgeRuntime, Log, TEXT("NovaBridgeRuntime module loaded in editor process; runtime server remains disabled."));
		return;
	}

	if (!IsRuntimeEnabledByConfig())
	{
		UE_LOG(LogNovaBridgeRuntime, Log, TEXT("NovaBridgeRuntime disabled (set -NovaBridgeRuntime=1 or NOVABRIDGE_RUNTIME=1 to enable)."));
		return;
	}

	bRuntimeEnabled = true;
	StartHttpServer();
}

void FNovaBridgeRuntimeModule::ShutdownModule()
{
	StopHttpServer();
	bRuntimeEnabled = false;
}

IMPLEMENT_MODULE(FNovaBridgeRuntimeModule, NovaBridgeRuntime)
