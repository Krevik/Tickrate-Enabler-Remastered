#include <cassert>
#include <sys/mman.h>
#include "../src/hitboxlagcompfix.cpp"
#include "../src/thirdparty/codepatch/patchmanager.cpp"

// Fake server code: the checked parts of BacktrackEntity, FinishLagCompensation and FireTerrorBullets,
// plus RestoreEntityFromRecords and InvalidateBoneCache. Executable, so the patched call can be run.
alignas(4096) static BYTE g_ServerMemory[4 * 4096];
static BYTE* const g_Backtrack = g_ServerMemory + 0x100;
static BYTE* const g_Finish = g_ServerMemory + 0x2000;
static BYTE* const g_FireBullets = g_ServerMemory + 0x2400;
static BYTE* const g_Restore = g_ServerMemory + 0x2800;
static BYTE* const g_Invalidate = g_ServerMemory + 0x2900;
static DynLibInfo g_ServerLibrary;
static bool g_HasLibraryInfo = true;

// Globals the bullet code refers to; only their addresses matter.
static void* g_LagCompensation;
static float g_Vec3Origin[3];
static float g_Vec3Angle[3];

static void* g_SymBacktrack = NULL;
static void* g_SymFinish = NULL;
static void* g_SymRestore = NULL;
static void* g_SymInvalidate = NULL;
static void* g_SymFireBullets = NULL;
static void* g_SymLagCompensation = NULL;
static void* g_SymVecOrigin = NULL;
static void* g_SymVecAngle = NULL;

MemoryUtils g_MemUtils;
MemoryUtils::MemoryUtils() {}
MemoryUtils::~MemoryUtils() {}
void MemoryUtils::SetMemPatchable(void*, size_t) {}
void* MemoryUtils::SimpleResolve(void*, const char* symbol)
{
	if (strcmp(symbol, SYM_CLAGCOMPENSATIONMANAGER_BACKTRACKENTITY) == 0) return g_SymBacktrack;
	if (strcmp(symbol, SYM_CLAGCOMPENSATIONMANAGER_FINISHLAGCOMPENSATION) == 0) return g_SymFinish;
	if (strcmp(symbol, SYM_CLAGCOMPENSATIONMANAGER_RESTOREENTITYFROMRECORDS) == 0) return g_SymRestore;
	if (strcmp(symbol, SYM_CBASEANIMATING_INVALIDATEBONECACHE) == 0) return g_SymInvalidate;
	if (strcmp(symbol, SYM_FIRETERRORBULLETS) == 0) return g_SymFireBullets;
	if (strcmp(symbol, SYM_LAGCOMPENSATION) == 0) return g_SymLagCompensation;
	if (strcmp(symbol, SYM_VEC3_ORIGIN) == 0) return g_SymVecOrigin;
	if (strcmp(symbol, SYM_VEC3_ANGLE) == 0) return g_SymVecAngle;
	return NULL;
}
bool MemoryUtils::GetLibraryInfo(const void*, DynLibInfo& lib)
{
	lib = g_ServerLibrary;
	return g_HasLibraryInfo;
}

// An entity as the game code sees it: only the vtable is used.
struct FakeEntity
{
	void** vtable;
};
static void* g_EntityVTable[CBASEENTITY_GETBASEANIMATING_VTABLE_OFFSET / sizeof(void*) + 1];
static FakeEntity g_Entity;
static int g_Animating; // only its address is passed around
static void* g_AnimatingResult = NULL;
static int g_Manager, g_RestoreRecord, g_ChangeRecord;

// Order of the calls made for one restored entity.
static int g_Step = 0;
static int g_RestoreStep = 0;
static int g_GetAnimatingStep = 0;
static int g_InvalidateStep = 0;
static void* g_InvalidateArgument = NULL;

static void FakeRestore(void* manager, void* entity, void* restore, void* change, int wasChanged)
{
	assert(manager == &g_Manager && entity == &g_Entity && restore == &g_RestoreRecord && change == &g_ChangeRecord);
	assert(wasChanged == 1);
	g_RestoreStep = ++g_Step;
}

static void* FakeGetBaseAnimating(void* entity)
{
	assert(entity == &g_Entity);
	g_GetAnimatingStep = ++g_Step;
	return g_AnimatingResult;
}

static void FakeInvalidate(void* animating)
{
	g_InvalidateStep = ++g_Step;
	g_InvalidateArgument = animating;
}

static void WriteRelative(BYTE* instruction, BYTE opcode, const void* target)
{
	instruction[0] = opcode;
	int32_t displacement = (int32_t)(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(instruction) + 5));
	memcpy(instruction + 1, &displacement, sizeof(displacement));
}

static void WriteOperand(BYTE* operand, const void* address)
{
	uint32_t value = reinterpret_cast<uintptr_t>(address);
	memcpy(operand, &value, sizeof(value));
}

static void WriteVanillaCode()
{
	memset(g_ServerMemory, 0xCC, sizeof(g_ServerMemory));

	memcpy(g_Backtrack + LAGCOMP_FLUSH_TEST_OFFSET, LAGCOMP_FLUSH_TEST_BYTES, LAGCOMP_FLUSH_TEST_BYTES_SIZE);
	memcpy(g_Backtrack + LAGCOMP_FLUSH_GETANIMATING_OFFSET, LAGCOMP_FLUSH_GETANIMATING_BYTES, LAGCOMP_FLUSH_GETANIMATING_BYTES_SIZE);
	memcpy(g_Backtrack + LAGCOMP_FLUSH_INVALIDATE_OFFSET, LAGCOMP_FLUSH_INVALIDATE_BYTES, LAGCOMP_FLUSH_INVALIDATE_BYTES_SIZE);
	WriteRelative(g_Backtrack + LAGCOMP_FLUSH_INVALIDATE_CALL_OFFSET, 0xE8, g_Invalidate);

	memcpy(g_Finish + LAGCOMP_RESTORE_ARGS_OFFSET, LAGCOMP_RESTORE_ARGS_BYTES, LAGCOMP_RESTORE_ARGS_BYTES_SIZE);
	WriteRelative(g_Finish + LAGCOMP_RESTORE_CALL_OFFSET, 0xE8, g_Restore);

	g_FireBullets[BULLET_LAGCOMP_LOAD_OFFSET] = 0xA1;
	WriteOperand(g_FireBullets + BULLET_LAGCOMP_LOAD_OFFSET + 1, &g_LagCompensation);
	memcpy(g_FireBullets + BULLET_LAGCOMP_ARGS_OFFSET, BULLET_LAGCOMP_ARGS_BYTES, BULLET_LAGCOMP_ARGS_BYTES_SIZE);
	WriteOperand(g_FireBullets + BULLET_LAGCOMP_ANGLES_OPERAND_OFFSET, g_Vec3Angle);
	memcpy(g_FireBullets + BULLET_LAGCOMP_ORIGIN_OFFSET, BULLET_LAGCOMP_ORIGIN_BYTES, BULLET_LAGCOMP_ORIGIN_BYTES_SIZE);
	WriteOperand(g_FireBullets + BULLET_LAGCOMP_ORIGIN_OPERAND_OFFSET, g_Vec3Origin);
	memcpy(g_FireBullets + BULLET_LAGCOMP_TYPE_OFFSET, BULLET_LAGCOMP_TYPE_BYTES, BULLET_LAGCOMP_TYPE_BYTES_SIZE);

	// The two game functions start with a jump to the fakes, so calling them works.
	WriteRelative(g_Restore, 0xE9, reinterpret_cast<const void*>(&FakeRestore));
	WriteRelative(g_Invalidate, 0xE9, reinterpret_cast<const void*>(&FakeInvalidate));

	g_SymBacktrack = g_Backtrack;
	g_SymFinish = g_Finish;
	g_SymRestore = g_Restore;
	g_SymInvalidate = g_Invalidate;
	g_SymFireBullets = g_FireBullets;
	g_SymLagCompensation = &g_LagCompensation;
	g_SymVecOrigin = g_Vec3Origin;
	g_SymVecAngle = g_Vec3Angle;
}

// What FinishLagCompensation's 'call' reaches right now.
static RestoreEntityFn RestoreCallTarget()
{
	return reinterpret_cast<RestoreEntityFn>(RelativeTarget(g_Finish + LAGCOMP_RESTORE_CALL_OFFSET,
		g_Finish + LAGCOMP_RESTORE_CALL_OFFSET));
}

static void RestoreEntity(void* animating)
{
	g_Step = g_RestoreStep = g_GetAnimatingStep = g_InvalidateStep = 0;
	g_InvalidateArgument = NULL;
	g_AnimatingResult = animating;
	g_Entity.vtable = g_EntityVTable;
	RestoreCallTarget()(&g_Manager, &g_Entity, &g_RestoreRecord, &g_ChangeRecord, 1);
}

static uint32_t FlushMask()
{
	uint32_t mask;
	memcpy(&mask, g_Backtrack + LAGCOMP_FLUSH_MASK_OFFSET, sizeof(mask));
	return mask;
}

static uint32_t BulletType()
{
	uint32_t type;
	memcpy(&type, g_FireBullets + BULLET_LAGCOMP_TYPE_IMMEDIATE_OFFSET, sizeof(type));
	return type;
}

// The bytes outside [changed, changed + 4) are the same in both buffers.
static void ExpectOnlyChanged(const BYTE* original, const BYTE* changedA, const BYTE* changedB)
{
	for (size_t i = 0; i < sizeof(g_ServerMemory); i++)
	{
		const BYTE* p = g_ServerMemory + i;
		const bool patched = (p >= changedA && p < changedA + 4) || (changedB != NULL && p >= changedB && p < changedB + 4);
		assert(patched || g_ServerMemory[i] == original[i]);
	}
}

static void TestBoneCachePatch()
{
	WriteVanillaCode();
	static BYTE original[sizeof(g_ServerMemory)];
	memcpy(original, g_ServerMemory, sizeof(original));

	// Vanilla: only the animation flag flushes, and a restore is just a restore.
	assert(FlushMask() == LAGCOMP_CHANGE_ANIMATION);
	RestoreEntity(&g_Animating);
	assert(g_RestoreStep == 1 && g_GetAnimatingStep == 0 && g_InvalidateStep == 0);

	{
		LagCompBoneCachePatch patch(NULL);
		assert(memcmp(original, g_ServerMemory, sizeof(original)) == 0);
		patch.Patch();

		// Only the flag mask and the call's operand changed.
		ExpectOnlyChanged(original, g_Backtrack + LAGCOMP_FLUSH_MASK_OFFSET, g_Finish + LAGCOMP_RESTORE_CALL_OFFSET + 1);
		assert(FlushMask() == LAGCOMP_CHANGE_ANY);
		assert((LAGCOMP_CHANGE_ANY & 0x100) && (LAGCOMP_CHANGE_ANY & 0x200) && (LAGCOMP_CHANGE_ANY & 0x400)
			&& (LAGCOMP_CHANGE_ANY & LAGCOMP_CHANGE_ANIMATION));
		assert(g_Finish[LAGCOMP_RESTORE_CALL_OFFSET] == 0xE8);
		assert(RestoreCallTarget() == &OnRestoreEntityFromRecords);

		// The entity is restored first, then its bone cache is flushed.
		RestoreEntity(&g_Animating);
		assert(g_RestoreStep == 1 && g_GetAnimatingStep == 2 && g_InvalidateStep == 3);
		assert(g_InvalidateArgument == &g_Animating);

		// An entity that is not a CBaseAnimating has no bone cache.
		RestoreEntity(NULL);
		assert(g_RestoreStep == 1 && g_GetAnimatingStep == 2 && g_InvalidateStep == 0);

		patch.Unpatch();
		assert(memcmp(original, g_ServerMemory, sizeof(original)) == 0);
		RestoreEntity(&g_Animating);
		assert(g_RestoreStep == 1 && g_InvalidateStep == 0);

		patch.Patch();
		RestoreEntity(&g_Animating);
		assert(g_InvalidateStep == 3);
	}
	// Destroying the patch restores the game code.
	assert(memcmp(original, g_ServerMemory, sizeof(original)) == 0);
}

static void TestBulletPatch()
{
	WriteVanillaCode();
	static BYTE original[sizeof(g_ServerMemory)];
	memcpy(original, g_ServerMemory, sizeof(original));
	assert(BulletType() == LAG_COMPENSATE_HITBOXES_ALONG_RAY);

	{
		BulletHitboxLagCompPatch patch(NULL);
		assert(memcmp(original, g_ServerMemory, sizeof(original)) == 0);
		patch.Patch();
		ExpectOnlyChanged(original, g_FireBullets + BULLET_LAGCOMP_TYPE_IMMEDIATE_OFFSET, NULL);
		assert(BulletType() == LAG_COMPENSATE_HITBOXES);

		patch.Unpatch();
		assert(memcmp(original, g_ServerMemory, sizeof(original)) == 0);
		patch.Patch();
		assert(BulletType() == LAG_COMPENSATE_HITBOXES);
	}
	assert(memcmp(original, g_ServerMemory, sizeof(original)) == 0);
}

static void ExpectBoneCacheRejected()
{
	bool rejected = false;
	try
	{
		LagCompBoneCachePatch patch(NULL);
	}
	catch (PatchException&)
	{
		rejected = true;
	}
	assert(rejected);
}

static void ExpectBulletRejected()
{
	bool rejected = false;
	try
	{
		BulletHitboxLagCompPatch patch(NULL);
	}
	catch (PatchException&)
	{
		rejected = true;
	}
	assert(rejected);
}

static void FlipEachByte(BYTE* code, size_t length, void (*expectRejected)())
{
	for (size_t i = 0; i < length; i++)
	{
		WriteVanillaCode();
		code[i] ^= 0x01;
		expectRejected();
	}
}

static void TestRejected()
{
	void** const boneCacheSymbols[] = { &g_SymBacktrack, &g_SymFinish, &g_SymRestore, &g_SymInvalidate };
	for (size_t i = 0; i < sizeof(boneCacheSymbols) / sizeof(boneCacheSymbols[0]); i++)
	{
		WriteVanillaCode();
		*boneCacheSymbols[i] = NULL;
		ExpectBoneCacheRejected();
	}
	void** const bulletSymbols[] = { &g_SymFireBullets, &g_SymLagCompensation, &g_SymVecOrigin, &g_SymVecAngle };
	for (size_t i = 0; i < sizeof(bulletSymbols) / sizeof(bulletSymbols[0]); i++)
	{
		WriteVanillaCode();
		*bulletSymbols[i] = NULL;
		ExpectBulletRejected();
	}

	FlipEachByte(g_Backtrack + LAGCOMP_FLUSH_TEST_OFFSET, LAGCOMP_FLUSH_TEST_BYTES_SIZE, ExpectBoneCacheRejected);
	FlipEachByte(g_Backtrack + LAGCOMP_FLUSH_GETANIMATING_OFFSET, LAGCOMP_FLUSH_GETANIMATING_BYTES_SIZE, ExpectBoneCacheRejected);
	FlipEachByte(g_Backtrack + LAGCOMP_FLUSH_INVALIDATE_OFFSET, LAGCOMP_FLUSH_INVALIDATE_BYTES_SIZE, ExpectBoneCacheRejected);
	FlipEachByte(g_Finish + LAGCOMP_RESTORE_ARGS_OFFSET, LAGCOMP_RESTORE_ARGS_BYTES_SIZE, ExpectBoneCacheRejected);

	// The two calls must target InvalidateBoneCache and RestoreEntityFromRecords.
	WriteVanillaCode();
	g_Backtrack[LAGCOMP_FLUSH_INVALIDATE_CALL_OFFSET + 1] ^= 0x01;
	ExpectBoneCacheRejected();
	WriteVanillaCode();
	g_Finish[LAGCOMP_RESTORE_CALL_OFFSET + 1] ^= 0x01;
	ExpectBoneCacheRejected();

	FlipEachByte(g_FireBullets + BULLET_LAGCOMP_LOAD_OFFSET, 5, ExpectBulletRejected);
	FlipEachByte(g_FireBullets + BULLET_LAGCOMP_ARGS_OFFSET, BULLET_LAGCOMP_ARGS_BYTES_SIZE, ExpectBulletRejected);
	FlipEachByte(g_FireBullets + BULLET_LAGCOMP_ANGLES_OPERAND_OFFSET, 4, ExpectBulletRejected);
	FlipEachByte(g_FireBullets + BULLET_LAGCOMP_ORIGIN_OFFSET, BULLET_LAGCOMP_ORIGIN_BYTES_SIZE, ExpectBulletRejected);
	FlipEachByte(g_FireBullets + BULLET_LAGCOMP_ORIGIN_OPERAND_OFFSET, 4, ExpectBulletRejected);
	FlipEachByte(g_FireBullets + BULLET_LAGCOMP_TYPE_OFFSET, BULLET_LAGCOMP_TYPE_BYTES_SIZE, ExpectBulletRejected);

	// A weapon ray that is not the default one (someone else already fixed the call) is left alone.
	WriteVanillaCode();
	WriteOperand(g_FireBullets + BULLET_LAGCOMP_ORIGIN_OPERAND_OFFSET, g_Vec3Angle);
	ExpectBulletRejected();

	// Code that reaches past the module.
	WriteVanillaCode();
	const size_t fullModuleSize = g_ServerLibrary.memorySize;
	g_ServerLibrary.memorySize = reinterpret_cast<uintptr_t>(g_Backtrack) - reinterpret_cast<uintptr_t>(g_ServerLibrary.baseAddress)
		+ LAGCOMP_FLUSH_CODE_SIZE - 1;
	ExpectBoneCacheRejected();
	ExpectBulletRejected();
	g_ServerLibrary.memorySize = fullModuleSize;
	g_HasLibraryInfo = false;
	ExpectBoneCacheRejected();
	ExpectBulletRejected();
	g_HasLibraryInfo = true;
	{
		LagCompBoneCachePatch boneCache(NULL);
		BulletHitboxLagCompPatch bullets(NULL);
	}
}

int main()
{
	Dl_info moduleInfo;
	assert(dladdr(g_ServerMemory, &moduleInfo) != 0);
	g_ServerLibrary.baseAddress = moduleInfo.dli_fbase;
	g_ServerLibrary.memorySize = reinterpret_cast<uintptr_t>(g_ServerMemory) - reinterpret_cast<uintptr_t>(moduleInfo.dli_fbase)
		+ sizeof(g_ServerMemory);
	assert(mprotect(g_ServerMemory, sizeof(g_ServerMemory), PROT_READ | PROT_WRITE | PROT_EXEC) == 0);

	g_EntityVTable[CBASEENTITY_GETBASEANIMATING_VTABLE_OFFSET / sizeof(void*)] = reinterpret_cast<void*>(&FakeGetBaseAnimating);

	TestBoneCachePatch();
	TestBulletPatch();
	TestRejected();
	return 0;
}
