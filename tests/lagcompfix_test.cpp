#include <cassert>
#include <cmath>
#include <limits>
#include "../src/lagcompfix.cpp"

// Fake CTerrorPlayer::WantsLagCompensationOnEntity.
static BYTE g_ServerMemory[0x200];
static DynLibInfo g_ServerLibrary;
static bool g_HasLibraryInfo = true;
static void* g_SymWantsLagCompensation = NULL;

MemoryUtils g_MemUtils;
MemoryUtils::MemoryUtils() {}
MemoryUtils::~MemoryUtils() {}
void MemoryUtils::SetMemPatchable(void*, size_t) {}
void* MemoryUtils::SimpleResolve(void*, const char* symbol)
{
	return strcmp(symbol, SYM_CTERRORPLAYER_WANTSLAGCOMPENSATIONONENTITY) == 0 ? g_SymWantsLagCompensation : NULL;
}
bool MemoryUtils::GetLibraryInfo(const void*, DynLibInfo& lib)
{
	lib = g_ServerLibrary;
	return g_HasLibraryInfo;
}

static void WriteVanillaCode()
{
	memset(g_ServerMemory, 0x90, sizeof(g_ServerMemory));
	memcpy(g_ServerMemory + LAGCOMP_WINDOW_ANCHOR_OFFSET, LAGCOMP_WINDOW_ANCHOR_BYTES, LAGCOMP_WINDOW_ANCHOR_BYTES_SIZE);
	memcpy(g_ServerMemory + LAGCOMP_WINDOW_PATCH_OFFSET, LAGCOMP_WINDOW_PATCH_BYTES, LAGCOMP_WINDOW_PATCH_BYTES_SIZE);
	g_SymWantsLagCompensation = g_ServerMemory;
}

static BYTE Window()
{
	return g_ServerMemory[LAGCOMP_WINDOW_IMMEDIATE_OFFSET];
}

static void ExpectRejected()
{
	bool rejected = false;
	try
	{
		LagCompWindowPatch patch(NULL, LAGCOMP_WINDOW_VANILLA_SECONDS, 0.01f);
	}
	catch (PatchException&)
	{
		rejected = true;
	}
	assert(rejected);
}

static void TestWindowUsercmds()
{
	const float vanilla = LAGCOMP_WINDOW_VANILLA_SECONDS;
	assert(LagCompWindowUsercmds(vanilla, 1.0f / 30.0f) == 5);	// vanilla at 30 tick
	assert(LagCompWindowUsercmds(vanilla, 0.01f) == 17);			// 100 tick: 166.7 ms -> 17 ticks
	assert(LagCompWindowUsercmds(vanilla, 1.0f / 66.666f) == 11);
	assert(LagCompWindowUsercmds(vanilla, 1.0f / 128.0f) == 21);
	assert(LagCompWindowUsercmds(0.2f, 0.01f) == 20);				// whole 0.2 s shove at 100 tick
	assert(LagCompWindowUsercmds(0.01f, 0.01f) == 5);				// never below vanilla
	assert(LagCompWindowUsercmds(vanilla, 0.1f) == 5);			// 10 tick
	assert(LagCompWindowUsercmds(100.0f, 0.001f) == 127);			// signed byte limit
	assert(LagCompWindowUsercmds(vanilla, 0.0f) == 5);
	assert(LagCompWindowUsercmds(vanilla, -1.0f) == 5);
	assert(LagCompWindowUsercmds(std::numeric_limits<float>::quiet_NaN(), 0.01f) == 5);
	assert(LagCompWindowUsercmds(vanilla, std::numeric_limits<float>::quiet_NaN()) == 5);
	assert(LagCompWindowUsercmds(std::numeric_limits<float>::infinity(), 0.01f) == 5);
	assert(LagCompWindowUsercmds(0.1f, 0.01f) == 10);				// 100 ms at 100 tick
	assert(LagCompWindowUsercmds(0.1f, 1.0f / 108.0f) == 11);		// 100 ms at 108 tick

	// -lagcompwindow <seconds>
	float seconds = -1.0f;
	assert(LagCompWindowSecondsFromParam(0.1f, seconds) && seconds == 0.1f);
	assert(LagCompWindowSecondsFromParam(0.01f, seconds) && seconds == 0.01f);
	assert(LagCompWindowSecondsFromParam(1.0f, seconds) && seconds == 1.0f);
	seconds = -1.0f;
	assert(!LagCompWindowSecondsFromParam(0.0f, seconds) && seconds == -1.0f);
	assert(!LagCompWindowSecondsFromParam(0.005f, seconds));
	assert(!LagCompWindowSecondsFromParam(1.5f, seconds));
	assert(!LagCompWindowSecondsFromParam(-0.1f, seconds));
	assert(!LagCompWindowSecondsFromParam(std::numeric_limits<float>::quiet_NaN(), seconds));
	assert(!LagCompWindowSecondsFromParam(std::numeric_limits<float>::infinity(), seconds));
}

static void TestPatch()
{
	WriteVanillaCode();
	BYTE original[sizeof(g_ServerMemory)];
	memcpy(original, g_ServerMemory, sizeof(original));
	{
		LagCompWindowPatch patch(NULL, LAGCOMP_WINDOW_VANILLA_SECONDS, 0.01f);
		assert(patch.GetWindowUsercmds() == 17);
		assert(Window() == 5); // nothing written before Patch()

		patch.Patch();
		assert(Window() == 17);
		// Only the immediate of 'cmp ecx, imm8' changes.
		for (size_t i = 0; i < sizeof(g_ServerMemory); i++)
		{
			assert(i == LAGCOMP_WINDOW_IMMEDIATE_OFFSET || g_ServerMemory[i] == original[i]);
		}

		patch.SetTickInterval(1.0f / 30.0f);
		assert(patch.GetWindowUsercmds() == 5 && Window() == 5);
		patch.SetTickInterval(1.0f / 128.0f);
		assert(patch.GetWindowUsercmds() == 21 && Window() == 21);

		patch.Unpatch();
		assert(Window() == 5);
		patch.SetTickInterval(0.01f); // remembered, written on the next Patch()
		assert(Window() == 5);
		patch.Patch();
		assert(Window() == 17);
		patch.Patch();
		assert(Window() == 17);
	}
	assert(memcmp(original, g_ServerMemory, sizeof(original)) == 0); // destructor restores

	{
		LagCompWindowPatch patch(NULL, 0.2f, 0.01f);
		patch.Patch();
		assert(Window() == 20);
	}
	assert(Window() == 5);
}

static void TestRejected()
{
	WriteVanillaCode();
	{
		LagCompWindowPatch patch(NULL, LAGCOMP_WINDOW_VANILLA_SECONDS, 0.01f);
	}

	g_SymWantsLagCompensation = NULL;
	ExpectRejected();
	WriteVanillaCode();

	// Any change in the checked bytes, e.g. a different field offset or an already patched window.
	for (size_t i = 0; i < LAGCOMP_WINDOW_ANCHOR_BYTES_SIZE; i++)
	{
		WriteVanillaCode();
		g_ServerMemory[LAGCOMP_WINDOW_ANCHOR_OFFSET + i] ^= 0x01;
		ExpectRejected();
	}
	for (size_t i = 0; i < LAGCOMP_WINDOW_PATCH_BYTES_SIZE; i++)
	{
		WriteVanillaCode();
		g_ServerMemory[LAGCOMP_WINDOW_PATCH_OFFSET + i] ^= 0x01;
		ExpectRejected();
	}
	WriteVanillaCode();
	g_ServerMemory[LAGCOMP_WINDOW_IMMEDIATE_OFFSET] = 17;
	ExpectRejected();

	WriteVanillaCode();
	const size_t fullModuleSize = g_ServerLibrary.memorySize;
	g_ServerLibrary.memorySize = reinterpret_cast<uintptr_t>(g_ServerMemory) - reinterpret_cast<uintptr_t>(g_ServerLibrary.baseAddress)
		+ LAGCOMP_WINDOW_PATCH_OFFSET + LAGCOMP_WINDOW_PATCH_BYTES_SIZE - 1;
	ExpectRejected();
	g_ServerLibrary.memorySize = fullModuleSize;
	g_HasLibraryInfo = false;
	ExpectRejected();
	g_HasLibraryInfo = true;
}

int main()
{
	Dl_info moduleInfo;
	assert(dladdr(g_ServerMemory, &moduleInfo) != 0);
	g_ServerLibrary.baseAddress = moduleInfo.dli_fbase;
	g_ServerLibrary.memorySize = reinterpret_cast<uintptr_t>(g_ServerMemory) - reinterpret_cast<uintptr_t>(moduleInfo.dli_fbase)
		+ sizeof(g_ServerMemory);

	TestWindowUsercmds();
	TestPatch();
	TestRejected();
	return 0;
}
