#include <cassert>
#include <limits>
#include <sys/mman.h>
#include "../src/boomervomitpatch.cpp"
#include "../src/thirdparty/codepatch/patchmanager.cpp"

static BYTE g_ServerMemory[4096];
alignas(65536) static BYTE g_InaccessibleModuleData[65536];
static DynLibInfo g_ServerLibrary;
static BYTE* g_ActivateAbility = g_ServerMemory;
static BYTE* g_UpdateAbility = g_ServerMemory;
static bool g_HasLibraryInfo = true;

MemoryUtils g_MemUtils;
MemoryUtils::MemoryUtils() {}
MemoryUtils::~MemoryUtils() {}
void MemoryUtils::SetMemPatchable(void*, size_t) {}
void* MemoryUtils::SimpleResolve(void*, const char* symbol)
{
	return strcmp(symbol, SYM_CVOMIT_UPDATEABILITY) == 0 ? g_UpdateAbility : g_ActivateAbility;
}
bool MemoryUtils::GetLibraryInfo(const void*, DynLibInfo& lib)
{
	lib = g_ServerLibrary;
	return g_HasLibraryInfo;
}

static void SetOperand(const float* constant)
{
	memcpy(g_ServerMemory + VOMIT_DELAY_PATCH_OFFSET,
		VOMIT_DELAY_PATCH_OPCODE, VOMIT_DELAY_PATCH_OPCODE_SIZE);
	memcpy(g_ServerMemory + VOMIT_DELAY_PATCH_OFFSET + VOMIT_DELAY_PATCH_OPCODE_SIZE,
		&constant, sizeof(constant));
}

static void ExpectRejected()
{
	bool rejected = false;
	try
	{
		BoomerVomitDelayPatch patch(NULL, 0.05f);
	}
	catch (PatchException&)
	{
		rejected = true;
	}
	assert(rejected);
}

static void SetFrameOperand(const void* const* globalsAddress)
{
	for (size_t index = 0; index < NUM_FRAMETIME_READS; index++)
	{
		BYTE* instruction = g_ServerMemory + g_FrameTimeReadOffsets[index];
		instruction[0] = 0xA1;
		memcpy(instruction + 1, &globalsAddress, sizeof(globalsAddress));
		instruction[MAX_MOV_INSTR_LEN - 1] = 0x90;
	}
}

static void ExpectFrameRejected(const void* realGlobals)
{
	bool rejected = false;
	try
	{
		BoomerVomitFrameTimePatch patch(NULL, realGlobals);
	}
	catch (PatchException&)
	{
		rejected = true;
	}
	assert(rejected);
}

static void TestFrameTimePatch()
{
	const void* realGlobals = &g_FakeGlobals;
	const size_t globalsOffset = sizeof(g_ServerMemory) - sizeof(realGlobals);
	memcpy(g_ServerMemory + globalsOffset, &realGlobals, sizeof(realGlobals));
	const void* const* globalsAddress = reinterpret_cast<const void* const*>(g_ServerMemory + globalsOffset);
	const size_t fullModuleSize = g_ServerLibrary.memorySize;
	g_ServerLibrary.memorySize = reinterpret_cast<uintptr_t>(g_ServerMemory) - reinterpret_cast<uintptr_t>(g_ServerLibrary.baseAddress)
		+ g_FrameTimeReadOffsets[NUM_FRAMETIME_READS - 1] + MAX_MOV_INSTR_LEN;
	assert(!IsInModule(globalsAddress, sizeof(realGlobals), g_ServerLibrary));
	SetFrameOperand(globalsAddress);
	BYTE original[MAX_MOV_INSTR_LEN];
	BYTE* instruction = g_ServerMemory + g_FrameTimeReadOffsets[0];
	memcpy(original, instruction, sizeof(original));
	{
		BoomerVomitFrameTimePatch patch(NULL, realGlobals);
		patch.Patch();
		assert(memcmp(original, instruction, sizeof(original)) != 0);
	}
	assert(memcmp(original, instruction, sizeof(original)) == 0);
	{
		BoomerVomitFrameTimePatch patch(NULL, NULL);
	}
	ExpectFrameRejected(g_ServerMemory);
	SetFrameOperand(NULL);
	ExpectFrameRejected(realGlobals);
	ExpectFrameRejected(NULL);
	SetFrameOperand(reinterpret_cast<const void* const*>(1));
	ExpectFrameRejected(realGlobals);
	SetFrameOperand(&realGlobals); // Readable stack memory is not server module data.
	ExpectFrameRejected(realGlobals);

	const size_t pageSize = (size_t)sysconf(_SC_PAGESIZE);
	assert(pageSize <= sizeof(g_InaccessibleModuleData));
	assert(mprotect(g_InaccessibleModuleData, pageSize, PROT_NONE) == 0);
	SetFrameOperand(reinterpret_cast<const void* const*>(g_InaccessibleModuleData));
	ExpectFrameRejected(realGlobals);
	assert(mprotect(g_InaccessibleModuleData, pageSize, PROT_READ | PROT_WRITE) == 0);

	SetFrameOperand(globalsAddress);
	instruction[0] = 0;
	ExpectFrameRejected(realGlobals);
	SetFrameOperand(globalsAddress);
	g_UpdateAbility = g_ServerMemory + sizeof(g_ServerMemory) - 1;
	ExpectFrameRejected(realGlobals);
	g_UpdateAbility = g_ServerMemory;
	g_HasLibraryInfo = false;
	ExpectFrameRejected(realGlobals);
	g_HasLibraryInfo = true;
	g_ServerLibrary.memorySize = fullModuleSize;
}

// Fallback used when process_vm_readv is blocked: must copy readable memory and reject unreadable
// memory without crashing.
static void TestPipeRead()
{
	static BYTE source[10000];
	static BYTE destination[sizeof(source)];
	for (size_t i = 0; i < sizeof(source); i++)
	{
		source[i] = (BYTE)(i * 7);
	}
	assert(ReadMemoryThroughPipe(destination, source, sizeof(source)));
	assert(memcmp(destination, source, sizeof(source)) == 0);
	assert(ReadMemoryThroughPipe(destination, source + 3, 1));
	assert(destination[0] == source[3]);

	const size_t pageSize = (size_t)sysconf(_SC_PAGESIZE);
	assert(mprotect(g_InaccessibleModuleData, pageSize, PROT_NONE) == 0);
	assert(!ReadMemoryThroughPipe(destination, g_InaccessibleModuleData, 4));
	assert(!ReadMemoryThroughPipe(destination, g_InaccessibleModuleData + pageSize - 2, 4));
	assert(mprotect(g_InaccessibleModuleData, pageSize, PROT_READ | PROT_WRITE) == 0);
}

int main()
{
	TestPipeRead();
	Dl_info moduleInfo;
	assert(dladdr(g_ServerMemory, &moduleInfo) != 0);
	g_ServerLibrary.baseAddress = moduleInfo.dli_fbase;
	g_ServerLibrary.memorySize = reinterpret_cast<uintptr_t>(g_ServerMemory) - reinterpret_cast<uintptr_t>(moduleInfo.dli_fbase)
		+ sizeof(g_ServerMemory);
	TestFrameTimePatch();
	const size_t constantOffset = sizeof(g_ServerMemory) - sizeof(float);
	const float vanillaDelay = VOMIT_DELAY_VANILLA;
	memcpy(g_ServerMemory + constantOffset, &vanillaDelay, sizeof(vanillaDelay));
	const float* originalDelay = reinterpret_cast<const float*>(g_ServerMemory + constantOffset);
	SetOperand(originalDelay);
	{
		BoomerVomitDelayPatch patch(NULL, 0.05f);
		patch.Patch();
		const float* patchedDelay;
		memcpy(&patchedDelay, g_ServerMemory + VOMIT_DELAY_PATCH_OFFSET
			+ VOMIT_DELAY_PATCH_OPCODE_SIZE, sizeof(patchedDelay));
		assert(*patchedDelay == 0.05f);
	}
	const float* restoredDelay;
	memcpy(&restoredDelay, g_ServerMemory + VOMIT_DELAY_PATCH_OFFSET
		+ VOMIT_DELAY_PATCH_OPCODE_SIZE, sizeof(restoredDelay));
	assert(restoredDelay == originalDelay);

	SetOperand(NULL);
	ExpectRejected();
	SetOperand(reinterpret_cast<const float*>(1));
	ExpectRejected();
	SetOperand(reinterpret_cast<const float*>(std::numeric_limits<uintptr_t>::max()));
	ExpectRejected();
	SetOperand(reinterpret_cast<const float*>(g_ServerMemory + constantOffset + 1));
	ExpectRejected();

	SetOperand(originalDelay);
	g_ServerMemory[VOMIT_DELAY_PATCH_OFFSET] = 0;
	ExpectRejected();
	SetOperand(originalDelay);
	const float wrongDelay = 0.5f;
	memcpy(g_ServerMemory + constantOffset, &wrongDelay, sizeof(wrongDelay));
	ExpectRejected();

	const size_t fullModuleSize = g_ServerLibrary.memorySize;
	g_ServerLibrary.memorySize = reinterpret_cast<uintptr_t>(g_ServerMemory) - reinterpret_cast<uintptr_t>(moduleInfo.dli_fbase)
		+ VOMIT_DELAY_PATCH_OFFSET + VOMIT_DELAY_PATCH_OPCODE_SIZE;
	ExpectRejected();
	g_ServerLibrary.memorySize = fullModuleSize;
	g_ActivateAbility = g_ServerMemory + sizeof(g_ServerMemory) - 1;
	ExpectRejected();
	g_ActivateAbility = NULL;
	ExpectRejected();
	g_ActivateAbility = g_ServerMemory;
	g_HasLibraryInfo = false;
	ExpectRejected();
}
