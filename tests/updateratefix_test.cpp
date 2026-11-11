#include <cassert>
#include <cmath>
#include <limits>
#include "../src/updateratefix.cpp"
#include "../src/thirdparty/codepatch/patchmanager.cpp"

// Fake CBaseClient::SetUpdateRate and CGameClient::SetUpdateRate.
static BYTE g_EngineMemory[0x200];
static BYTE* const g_BaseClient = g_EngineMemory;
static BYTE* const g_GameClient = g_EngineMemory + 0x100;
static DynLibInfo g_EngineLibrary;
static bool g_HasLibraryInfo = true;
static void* g_SymBaseClient = NULL;
static void* g_SymGameClient = NULL;

MemoryUtils g_MemUtils;
MemoryUtils::MemoryUtils() {}
MemoryUtils::~MemoryUtils() {}
void MemoryUtils::SetMemPatchable(void*, size_t) {}
void* MemoryUtils::SimpleResolve(void*, const char* symbol)
{
	if (strcmp(symbol, SYM_CBASECLIENT_SETUPDATERATE) == 0) return g_SymBaseClient;
	if (strcmp(symbol, SYM_CGAMECLIENT_SETUPDATERATE) == 0) return g_SymGameClient;
	return NULL;
}
bool MemoryUtils::GetLibraryInfo(const void*, DynLibInfo& lib)
{
	lib = g_EngineLibrary;
	return g_HasLibraryInfo;
}

static void WriteVanillaCode()
{
	memset(g_EngineMemory, 0xCC, sizeof(g_EngineMemory));
	memcpy(g_BaseClient + UPDATERATE_BASECLIENT_CLAMP_OFFSET, UPDATERATE_BASECLIENT_CLAMP_BYTES, UPDATERATE_BASECLIENT_CLAMP_BYTES_SIZE);
	memcpy(g_GameClient + UPDATERATE_GAMECLIENT_CLAMP_OFFSET, UPDATERATE_GAMECLIENT_CLAMP_BYTES, UPDATERATE_GAMECLIENT_CLAMP_BYTES_SIZE);
	g_GameClient[UPDATERATE_GAMECLIENT_JMP_OFFSET] = 0xE9;
	int32_t displacement = (int32_t)(g_BaseClient - (g_GameClient + UPDATERATE_GAMECLIENT_JMP_OFFSET + 5));
	memcpy(g_GameClient + UPDATERATE_GAMECLIENT_JMP_OFFSET + 1, &displacement, sizeof(displacement));
	g_SymBaseClient = g_BaseClient;
	g_SymGameClient = g_GameClient;
}

// The rewritten instructions, byte for byte.
static void ExpectPatched(int32_t cap)
{
	BYTE base[UPDATERATE_BASECLIENT_PATCH_SIZE] = { 0xBA, 0, 0, 0, 0, 0x39, 0xD0, 0x0F, 0x4E, 0xD0, 0x90 };
	memcpy(base + 1, &cap, sizeof(cap));
	BYTE game[UPDATERATE_GAMECLIENT_PATCH_SIZE] = { 0xB8, 0, 0, 0, 0, 0x39, 0xC2, 0x7F, 0x08, 0x90 };
	memcpy(game + 1, &cap, sizeof(cap));
	assert(memcmp(g_BaseClient + UPDATERATE_BASECLIENT_CLAMP_OFFSET, base, sizeof(base)) == 0);
	assert(memcmp(g_GameClient + UPDATERATE_GAMECLIENT_CLAMP_OFFSET, game, sizeof(game)) == 0);
	// The rest of the checked code is untouched.
	assert(memcmp(g_BaseClient + UPDATERATE_BASECLIENT_CLAMP_OFFSET + sizeof(base),
		UPDATERATE_BASECLIENT_CLAMP_BYTES + sizeof(base), UPDATERATE_BASECLIENT_CLAMP_BYTES_SIZE - sizeof(base)) == 0);
	assert(memcmp(g_GameClient + UPDATERATE_GAMECLIENT_CLAMP_OFFSET + sizeof(game),
		UPDATERATE_GAMECLIENT_CLAMP_BYTES + sizeof(game), UPDATERATE_GAMECLIENT_CLAMP_BYTES_SIZE - sizeof(game)) == 0);
	// 'jg +8' in the new code lands where 'jg +7' did in the original.
	assert(UPDATERATE_GAMECLIENT_CLAMP_OFFSET + 9 + 8 == UPDATERATE_GAMECLIENT_CLAMP_OFFSET + 10 + 7);
}

static void ExpectRejected(int cap = 108)
{
	bool rejected = false;
	try
	{
		UpdateRateCapPatch patch(NULL, cap);
	}
	catch (PatchException&)
	{
		rejected = true;
	}
	assert(rejected);
}

static void TestCapForTickInterval()
{
	assert(UpdateRateCapForTickInterval(1.0f / 100.0f) == 100);
	assert(UpdateRateCapForTickInterval(1.0f / 108.0f) == 108);
	assert(UpdateRateCapForTickInterval(1.0f / 108.5f) == 109);
	assert(UpdateRateCapForTickInterval(1.0f / 128.0f) == 128);
	assert(UpdateRateCapForTickInterval(1.0f / 66.666f) == 100);
	assert(UpdateRateCapForTickInterval(1.0f / 30.0f) == 100);
	assert(UpdateRateCapForTickInterval(1.0f / 5000.0f) == 1000);
	assert(UpdateRateCapForTickInterval(0.0f) == 100);
	assert(UpdateRateCapForTickInterval(-0.01f) == 100);
	assert(UpdateRateCapForTickInterval(std::numeric_limits<float>::quiet_NaN()) == 100);
	assert(UpdateRateCapForTickInterval(std::numeric_limits<float>::infinity()) == 100);
	// The engine's snapshot interval (1 / cap) must not be longer than a tick.
	for (int tickrate = 101; tickrate <= 1000; tickrate++)
	{
		float tickInterval = 1.0f / (float)tickrate;
		int cap = UpdateRateCapForTickInterval(tickInterval);
		assert(cap == tickrate);
		assert(1.0f / (float)cap <= tickInterval);
	}
}

static void TestPatch()
{
	WriteVanillaCode();
	BYTE original[sizeof(g_EngineMemory)];
	memcpy(original, g_EngineMemory, sizeof(original));
	{
		UpdateRateCapPatch patch(NULL, 108);
		assert(patch.GetCap() == 108);
		assert(memcmp(original, g_EngineMemory, sizeof(original)) == 0);
		patch.Patch();
		ExpectPatched(108);
		patch.Unpatch();
		assert(memcmp(original, g_EngineMemory, sizeof(original)) == 0);
		patch.Patch();
		ExpectPatched(108);
	}
	assert(memcmp(original, g_EngineMemory, sizeof(original)) == 0);

	{
		UpdateRateCapPatch patch(NULL, 128); // more than a signed byte
		patch.Patch();
		ExpectPatched(128);
	}
	{
		UpdateRateCapPatch patch(NULL, 1000);
		patch.Patch();
		ExpectPatched(1000);
	}
	assert(memcmp(original, g_EngineMemory, sizeof(original)) == 0);
}

static void TestRejected()
{
	WriteVanillaCode();
	ExpectRejected(100);
	ExpectRejected(1001);
	ExpectRejected(0);

	g_SymBaseClient = NULL;
	ExpectRejected();
	WriteVanillaCode();
	g_SymGameClient = NULL;
	ExpectRejected();

	for (size_t i = 0; i < UPDATERATE_BASECLIENT_CLAMP_BYTES_SIZE; i++)
	{
		WriteVanillaCode();
		g_BaseClient[UPDATERATE_BASECLIENT_CLAMP_OFFSET + i] ^= 0x01;
		ExpectRejected();
	}
	for (size_t i = 0; i < UPDATERATE_GAMECLIENT_CLAMP_BYTES_SIZE; i++)
	{
		WriteVanillaCode();
		g_GameClient[UPDATERATE_GAMECLIENT_CLAMP_OFFSET + i] ^= 0x01;
		ExpectRejected();
	}
	WriteVanillaCode();
	g_GameClient[UPDATERATE_GAMECLIENT_JMP_OFFSET] = 0xE8;
	ExpectRejected();
	WriteVanillaCode();
	g_GameClient[UPDATERATE_GAMECLIENT_JMP_OFFSET + 1] ^= 0x01;
	ExpectRejected();

	WriteVanillaCode();
	const size_t fullModuleSize = g_EngineLibrary.memorySize;
	g_EngineLibrary.memorySize = reinterpret_cast<uintptr_t>(g_GameClient) - reinterpret_cast<uintptr_t>(g_EngineLibrary.baseAddress)
		+ UPDATERATE_GAMECLIENT_CODE_SIZE - 1;
	ExpectRejected();
	g_EngineLibrary.memorySize = fullModuleSize;
	g_HasLibraryInfo = false;
	ExpectRejected();
	g_HasLibraryInfo = true;
	{
		UpdateRateCapPatch patch(NULL, 108);
	}
}

int main()
{
	Dl_info moduleInfo;
	assert(dladdr(g_EngineMemory, &moduleInfo) != 0);
	g_EngineLibrary.baseAddress = moduleInfo.dli_fbase;
	g_EngineLibrary.memorySize = reinterpret_cast<uintptr_t>(g_EngineMemory) - reinterpret_cast<uintptr_t>(moduleInfo.dli_fbase)
		+ sizeof(g_EngineMemory);

	TestCapForTickInterval();
	TestPatch();
	TestRejected();
	return 0;
}
