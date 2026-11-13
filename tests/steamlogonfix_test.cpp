#include <cassert>
#include <sys/mman.h>
#include "../src/steamlogonfix.cpp"

// Fake engine code: the OnValidateAuthTicketResponse wrapper, its body and the failure helper.
// Executable, so the patched call can be followed and run.
alignas(4096) static BYTE g_EngineMemory[2 * 4096];
static BYTE* const g_Wrapper = g_EngineMemory + 0x100;
static BYTE* const g_Body = g_EngineMemory + 0x400;
static BYTE* const g_Helper = g_EngineMemory + 0x1000;
static DynLibInfo g_EngineLibrary;
static bool g_HasLibraryInfo = true;
static void* g_SymWrapper = NULL;
static void* g_SymHelper = NULL;

MemoryUtils g_MemUtils;
MemoryUtils::MemoryUtils() {}
MemoryUtils::~MemoryUtils() {}
void MemoryUtils::SetMemPatchable(void*, size_t) {}
void* MemoryUtils::SimpleResolve(void*, const char* symbol)
{
	if (strcmp(symbol, SYM_CSTEAM3SERVER_ONVALIDATEAUTHTICKETRESPONSE) == 0) return g_SymWrapper;
	if (strcmp(symbol, SYM_CSTEAM3SERVER_ONVALIDATEAUTHTICKETRESPONSEHELPER) == 0) return g_SymHelper;
	return NULL;
}
bool MemoryUtils::GetLibraryInfo(const void*, DynLibInfo& lib)
{
	lib = g_EngineLibrary;
	return g_HasLibraryInfo;
}

// A client as the engine code sees it: vtable pointer, then data up to the authenticated flag.
struct FakeClient
{
	void** vtable;
	BYTE data[0x300];
};
static const char* FakeGetClientName(void*) { return "Player"; }
static const char* FakeGetNetworkIDString(void*) { return "STEAM_1:0:12345"; }
static void* g_ClientVTable[CLIENT_GETNETWORKIDSTRING_INDEX + 1];
static FakeClient g_Client;
static int g_Steam3Server; // only its address is passed around

// The engine's helper (kicks the client).
static int g_HelperCalls = 0;
static void* g_HelperServer = NULL;
static void* g_HelperClient = NULL;
static int g_HelperResponse = -1;
static void FakeEngineHelper(void* steam3Server, void* client, int response)
{
	g_HelperCalls++;
	g_HelperServer = steam3Server;
	g_HelperClient = client;
	g_HelperResponse = response;
}

static int g_LogCalls = 0;
static int g_LogResponse = -1;
static bool g_LogValidated = false;
static bool g_LogKept = false;
static void FakeLog(const char* name, const char* networkId, int response, bool validated, bool kept)
{
	assert(strcmp(name, "Player") == 0);
	assert(strcmp(networkId, "STEAM_1:0:12345") == 0);
	assert(g_HelperCalls == 0); // logged before the engine disconnects the client
	g_LogCalls++;
	g_LogResponse = response;
	g_LogValidated = validated;
	g_LogKept = kept;
}

static void WriteRelative(BYTE* instruction, BYTE opcode, const void* target)
{
	instruction[0] = opcode;
	int32_t displacement = (int32_t)(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(instruction) + 5));
	memcpy(instruction + 1, &displacement, sizeof(displacement));
}

static void WriteVanillaCode()
{
	memset(g_EngineMemory, 0xCC, sizeof(g_EngineMemory));
	memcpy(g_Wrapper + STEAMAUTH_WRAPPER_TAIL_OFFSET, STEAMAUTH_WRAPPER_TAIL_BYTES, STEAMAUTH_WRAPPER_TAIL_BYTES_SIZE);
	WriteRelative(g_Wrapper + STEAMAUTH_WRAPPER_JMP_OFFSET, 0xE9, g_Body);
	memcpy(g_Body + STEAMAUTH_BODY_RESPONSE_OFFSET, STEAMAUTH_BODY_RESPONSE_BYTES, STEAMAUTH_BODY_RESPONSE_BYTES_SIZE);
	memcpy(g_Body + STEAMAUTH_BODY_AUTHENTICATED_OFFSET, STEAMAUTH_BODY_AUTHENTICATED_BYTES, STEAMAUTH_BODY_AUTHENTICATED_BYTES_SIZE);
	memcpy(g_Body + STEAMAUTH_BODY_FAILURE_OFFSET, STEAMAUTH_BODY_FAILURE_BYTES, STEAMAUTH_BODY_FAILURE_BYTES_SIZE);
	WriteRelative(g_Body + STEAMAUTH_BODY_HELPER_CALL_OFFSET, 0xE8, g_Helper);
	memcpy(g_Body + STEAMAUTH_BODY_NETWORKID_OFFSET, STEAMAUTH_BODY_NETWORKID_BYTES, STEAMAUTH_BODY_NETWORKID_BYTES_SIZE);
	// The helper starts with a jump to the fake, so calling it works; the checked bytes follow.
	WriteRelative(g_Helper, 0xE9, reinterpret_cast<const void*>(&FakeEngineHelper));
	memcpy(g_Helper + STEAMAUTH_HELPER_ARGS_OFFSET, STEAMAUTH_HELPER_ARGS_BYTES, STEAMAUTH_HELPER_ARGS_BYTES_SIZE);
	g_SymWrapper = g_Wrapper;
	g_SymHelper = g_Helper;
}

// What the engine's 'call' in the body reaches right now.
static AuthFailureHelperFn CallTarget()
{
	return reinterpret_cast<AuthFailureHelperFn>(RelativeTarget(g_Body + STEAMAUTH_BODY_HELPER_CALL_OFFSET,
		g_Body + STEAMAUTH_BODY_HELPER_CALL_OFFSET));
}

static void SendFailure(int response, bool validated)
{
	g_HelperCalls = 0;
	g_HelperServer = g_HelperClient = NULL;
	g_HelperResponse = -1;
	g_LogCalls = 0;
	g_Client.vtable = g_ClientVTable;
	reinterpret_cast<BYTE*>(&g_Client)[STEAMAUTH_CLIENT_FULLYAUTHENTICATED_OFFSET] = validated ? 1 : 0;
	CallTarget()(&g_Steam3Server, &g_Client, response);
}

static void ExpectKicked(int response)
{
	assert(g_HelperCalls == 1);
	assert(g_HelperServer == &g_Steam3Server && g_HelperClient == &g_Client && g_HelperResponse == response);
}

static void ExpectRejected()
{
	bool rejected = false;
	try
	{
		SteamLogonKickPatch patch(NULL, FakeLog);
	}
	catch (PatchException&)
	{
		rejected = true;
	}
	assert(rejected);
}

static void TestResponses()
{
	for (int response = -1; response <= 12; response++)
	{
		assert(IsNoSteamLogonResponse(response) == (response == 1 || response == 6 || response == 7 || response == 8));
	}
	assert(strcmp(SteamAuthResponseName(0), "OK") == 0);
	assert(strcmp(SteamAuthResponseName(1), "UserNotConnectedToSteam") == 0);
	assert(strcmp(SteamAuthResponseName(6), "AuthTicketCanceled") == 0);
	assert(strcmp(SteamAuthResponseName(8), "AuthTicketInvalid") == 0);
	assert(strcmp(SteamAuthResponseName(9), "PublisherIssuedBan") == 0);
	assert(strcmp(SteamAuthResponseName(10), "Unknown") == 0);
	assert(strcmp(SteamAuthResponseName(-1), "Unknown") == 0);
}

static void TestPatch()
{
	WriteVanillaCode();
	BYTE original[sizeof(g_EngineMemory)];
	memcpy(original, g_EngineMemory, sizeof(original));

	// Vanilla: every failure reaches the engine's helper.
	SendFailure(8, true);
	ExpectKicked(8);
	assert(g_LogCalls == 0);

	{
		SteamLogonKickPatch patch(NULL, FakeLog);
		assert(memcmp(original, g_EngineMemory, sizeof(original)) == 0);
		patch.Patch();

		// Only the call's operand changed.
		BYTE* operand = g_Body + STEAMAUTH_BODY_HELPER_CALL_OFFSET + 1;
		assert(memcmp(original, g_EngineMemory, operand - g_EngineMemory) == 0);
		assert(memcmp(operand + 4, original + (operand + 4 - g_EngineMemory),
			sizeof(original) - (operand + 4 - g_EngineMemory)) == 0);
		assert(g_Body[STEAMAUTH_BODY_HELPER_CALL_OFFSET] == 0xE8);
		assert(CallTarget() == &OnSteamAuthFailure);

		// "No Steam logon" responses: logged, client kept.
		const int kept[] = { 1, 6, 7, 8 };
		for (size_t i = 0; i < sizeof(kept) / sizeof(kept[0]); i++)
		{
			for (int validated = 0; validated <= 1; validated++)
			{
				SendFailure(kept[i], validated != 0);
				assert(g_HelperCalls == 0);
				assert(g_LogCalls == 1 && g_LogResponse == kept[i] && g_LogKept);
				assert(g_LogValidated == (validated != 0));
			}
		}

		// Everything else is still kicked by the engine, with the same arguments.
		const int kicked[] = { 0, 2, 3, 4, 5, 9, 10, -1 };
		for (size_t i = 0; i < sizeof(kicked) / sizeof(kicked[0]); i++)
		{
			SendFailure(kicked[i], true);
			ExpectKicked(kicked[i]);
			assert(g_LogCalls == 1 && g_LogResponse == kicked[i] && !g_LogKept && g_LogValidated);
		}

		patch.Unpatch();
		assert(memcmp(original, g_EngineMemory, sizeof(original)) == 0);
		SendFailure(8, true);
		ExpectKicked(8);
		assert(g_LogCalls == 0);

		patch.Patch();
		SendFailure(8, false);
		assert(g_HelperCalls == 0 && g_LogCalls == 1);
	}
	// Destroying the patch restores the engine code.
	assert(memcmp(original, g_EngineMemory, sizeof(original)) == 0);

	// Without a log function the decision is the same.
	{
		SteamLogonKickPatch patch(NULL, NULL);
		patch.Patch();
		SendFailure(6, false);
		assert(g_HelperCalls == 0 && g_LogCalls == 0);
		SendFailure(3, true);
		ExpectKicked(3);
		assert(g_LogCalls == 0);
	}
	assert(memcmp(original, g_EngineMemory, sizeof(original)) == 0);
}

static void FlipEachByte(BYTE* code, size_t length)
{
	for (size_t i = 0; i < length; i++)
	{
		WriteVanillaCode();
		code[i] ^= 0x01;
		ExpectRejected();
	}
}

static void TestRejected()
{
	WriteVanillaCode();
	g_SymWrapper = NULL;
	ExpectRejected();
	WriteVanillaCode();
	g_SymHelper = NULL;
	ExpectRejected();

	FlipEachByte(g_Wrapper + STEAMAUTH_WRAPPER_TAIL_OFFSET, STEAMAUTH_WRAPPER_TAIL_BYTES_SIZE);
	FlipEachByte(g_Body + STEAMAUTH_BODY_RESPONSE_OFFSET, STEAMAUTH_BODY_RESPONSE_BYTES_SIZE);
	FlipEachByte(g_Body + STEAMAUTH_BODY_AUTHENTICATED_OFFSET, STEAMAUTH_BODY_AUTHENTICATED_BYTES_SIZE);
	FlipEachByte(g_Body + STEAMAUTH_BODY_FAILURE_OFFSET, STEAMAUTH_BODY_FAILURE_BYTES_SIZE);
	FlipEachByte(g_Body + STEAMAUTH_BODY_NETWORKID_OFFSET, STEAMAUTH_BODY_NETWORKID_BYTES_SIZE);
	FlipEachByte(g_Helper + STEAMAUTH_HELPER_ARGS_OFFSET, STEAMAUTH_HELPER_ARGS_BYTES_SIZE);

	// The call in the body must target the helper.
	WriteVanillaCode();
	g_Body[STEAMAUTH_BODY_HELPER_CALL_OFFSET + 1] ^= 0x01;
	ExpectRejected();

	// The wrapper's jump must stay inside the module.
	WriteVanillaCode();
	WriteRelative(g_Wrapper + STEAMAUTH_WRAPPER_JMP_OFFSET, 0xE9, g_EngineMemory + sizeof(g_EngineMemory));
	ExpectRejected();

	WriteVanillaCode();
	const size_t fullModuleSize = g_EngineLibrary.memorySize;
	g_EngineLibrary.memorySize = reinterpret_cast<uintptr_t>(g_Helper) - reinterpret_cast<uintptr_t>(g_EngineLibrary.baseAddress)
		+ STEAMAUTH_HELPER_CODE_SIZE - 1;
	ExpectRejected();
	g_EngineLibrary.memorySize = fullModuleSize;
	g_HasLibraryInfo = false;
	ExpectRejected();
	g_HasLibraryInfo = true;
	{
		SteamLogonKickPatch patch(NULL, FakeLog);
	}
}

int main()
{
	Dl_info moduleInfo;
	assert(dladdr(g_EngineMemory, &moduleInfo) != 0);
	g_EngineLibrary.baseAddress = moduleInfo.dli_fbase;
	g_EngineLibrary.memorySize = reinterpret_cast<uintptr_t>(g_EngineMemory) - reinterpret_cast<uintptr_t>(moduleInfo.dli_fbase)
		+ sizeof(g_EngineMemory);
	assert(mprotect(g_EngineMemory, sizeof(g_EngineMemory), PROT_READ | PROT_WRITE | PROT_EXEC) == 0);

	g_ClientVTable[CLIENT_GETCLIENTNAME_INDEX] = reinterpret_cast<void*>(&FakeGetClientName);
	g_ClientVTable[CLIENT_GETNETWORKIDSTRING_INDEX] = reinterpret_cast<void*>(&FakeGetNetworkIDString);

	TestResponses();
	TestPatch();
	TestRejected();
	return 0;
}
