#include "NovaBridgeModule.h"

#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
#include "IWebSocketServer.h"
#endif

DEFINE_LOG_CATEGORY(LogNovaBridge);

void FNovaBridgeModule::StartupModule()
{
	UE_LOG(LogNovaBridge, Log, TEXT("NovaBridge starting up..."));
	StartHttpServer();
	StartWebSocketServer();
	StartEventWebSocketServer();
}

void FNovaBridgeModule::ShutdownModule()
{
	UE_LOG(LogNovaBridge, Log, TEXT("NovaBridge shutting down..."));
	StopEventWebSocketServer();
	StopWebSocketServer();
	CleanupStreamCapture();
	CleanupCapture();
	StopHttpServer();
}

IMPLEMENT_MODULE(FNovaBridgeModule, NovaBridge)
