#include <cassert>
#include <limits>
#include "../src/boomervomitpatch.cpp"

static BYTE g_ServerMemory[1024];
static DynLibInfo g_ServerLibrary = { g_ServerMemory, sizeof(g_ServerMemory) };
static BYTE* g_ActivateAbility = g_ServerMemory;
static bool g_HasLibraryInfo = true;

MemoryUtils g_MemUtils;
MemoryUtils::MemoryUtils() {}
MemoryUtils::~MemoryUtils() {}
void MemoryUtils::SetMemPatchable(void*, size_t) {}
void* MemoryUtils::SimpleResolve(void*, const char*) { return g_ActivateAbility; }
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

int main()
{
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

	g_ServerLibrary.memorySize = VOMIT_DELAY_PATCH_OFFSET + VOMIT_DELAY_PATCH_OPCODE_SIZE;
	ExpectRejected();
	g_ServerLibrary.memorySize = sizeof(g_ServerMemory);
	g_ActivateAbility = g_ServerMemory + sizeof(g_ServerMemory) - 1;
	ExpectRejected();
	g_ActivateAbility = NULL;
	ExpectRejected();
	g_ActivateAbility = g_ServerMemory;
	g_HasLibraryInfo = false;
	ExpectRejected();
}
