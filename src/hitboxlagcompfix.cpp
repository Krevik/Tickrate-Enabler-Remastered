/**
 * vim: set ts=4 :
 * =============================================================================
 * Lag compensated hitboxes (L4D2, Linux)
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdint.h>
#include <string.h>
#include "thirdparty/memutils.h"
#include "hitboxlagcompfix.h"
#include "basicbinpatch.h"
#include "modulememory.h"
#include "patchexceptions.h"
#include "sigs.h"

#if SOURCE_ENGINE == SE_LEFT4DEAD2 && defined (_LINUX)
typedef void (*RestoreEntityFn)(void* manager, void* entity, void* restore, void* change, int wasChanged);
typedef void* (*GetBaseAnimatingFn)(void* entity);
typedef void (*InvalidateBoneCacheFn)(void* animating);

static RestoreEntityFn s_RestoreEntity = NULL;
static InvalidateBoneCacheFn s_InvalidateBoneCache = NULL;

// Replaces FinishLagCompensation's call to RestoreEntityFromRecords(entity, restore, change, true).
__attribute__((force_align_arg_pointer))
static void OnRestoreEntityFromRecords(void* manager, void* entity, void* restore, void* change, int wasChanged)
{
	s_RestoreEntity(manager, entity, restore, change, wasChanged);
	if (entity == NULL)
	{
		return;
	}

	// The bones cached while the entity was rewound belong to the position it has just left.
	void** vtable = *reinterpret_cast<void***>(entity);
	void* animating = reinterpret_cast<GetBaseAnimatingFn>(vtable[CBASEENTITY_GETBASEANIMATING_VTABLE_OFFSET / sizeof(void*)])(entity);
	if (animating != NULL)
	{
		s_InvalidateBoneCache(animating);
	}
}

static void ExpectCode(const BYTE* code, const char* bytes, size_t length, const char* error)
{
	if (memcmp(code, bytes, length) != 0)
	{
		throw PatchException(error);
	}
}

// Target of the 'call rel32' at 'instruction' ('code' holds a copy of its 5 bytes).
static BYTE* RelativeTarget(BYTE* instruction, const BYTE* code)
{
	int32_t displacement;
	memcpy(&displacement, code + 1, sizeof(displacement));
	return reinterpret_cast<BYTE*>(reinterpret_cast<uintptr_t>(instruction) + 5
		+ static_cast<uintptr_t>(static_cast<intptr_t>(displacement)));
}

// The 32-bit absolute operand at 'code' is the address 'expected'.
static bool IsOperand(const BYTE* code, const void* expected)
{
	uint32_t operand;
	memcpy(&operand, code, sizeof(operand));
	return operand == reinterpret_cast<uintptr_t>(expected);
}

LagCompBoneCachePatch::LagCompBoneCachePatch(IServerGameDLL* gamedll)
{
	BYTE* pBacktrack = (BYTE*)g_MemUtils.SimpleResolve(gamedll, SYM_CLAGCOMPENSATIONMANAGER_BACKTRACKENTITY);
	BYTE* pFinish = (BYTE*)g_MemUtils.SimpleResolve(gamedll, SYM_CLAGCOMPENSATIONMANAGER_FINISHLAGCOMPENSATION);
	BYTE* pRestore = (BYTE*)g_MemUtils.SimpleResolve(gamedll, SYM_CLAGCOMPENSATIONMANAGER_RESTOREENTITYFROMRECORDS);
	BYTE* pInvalidate = (BYTE*)g_MemUtils.SimpleResolve(gamedll, SYM_CBASEANIMATING_INVALIDATEBONECACHE);
	if (pBacktrack == NULL || pFinish == NULL || pRestore == NULL || pInvalidate == NULL)
	{
		throw PatchException("Couldn't find 'CLagCompensationManager::BacktrackEntity' / 'FinishLagCompensation' / 'RestoreEntityFromRecords' or 'CBaseAnimating::InvalidateBoneCache'.");
	}

	DynLibInfo lib = {};
	if (!g_MemUtils.GetLibraryInfo(gamedll, lib))
	{
		throw PatchException("Couldn't get server module information.");
	}
	if (!IsInModule(pBacktrack, LAGCOMP_FLUSH_CODE_SIZE, lib) || !IsInModule(pFinish, LAGCOMP_RESTORE_CODE_SIZE, lib))
	{
		throw PatchException("Lag compensation bone cache patch offsets outside server module.");
	}

	// Only the checked parts of the two functions are read.
	BYTE flushCode[LAGCOMP_FLUSH_CODE_SIZE - LAGCOMP_FLUSH_TEST_OFFSET];
	BYTE restoreCode[LAGCOMP_RESTORE_CODE_SIZE - LAGCOMP_RESTORE_ARGS_OFFSET];
	if (!ReadModuleMemory(flushCode, pBacktrack + LAGCOMP_FLUSH_TEST_OFFSET, sizeof(flushCode), lib)
		|| !ReadModuleMemory(restoreCode, pFinish + LAGCOMP_RESTORE_ARGS_OFFSET, sizeof(restoreCode), lib))
	{
		throw PatchException("'CLagCompensationManager::BacktrackEntity' / 'FinishLagCompensation' is not readable.");
	}

	ExpectCode(flushCode, LAGCOMP_FLUSH_TEST_BYTES, LAGCOMP_FLUSH_TEST_BYTES_SIZE,
		"'CLagCompensationManager::BacktrackEntity' patch offset incorrect (expected the animation flag test).");
	ExpectCode(flushCode + (LAGCOMP_FLUSH_GETANIMATING_OFFSET - LAGCOMP_FLUSH_TEST_OFFSET), LAGCOMP_FLUSH_GETANIMATING_BYTES,
		LAGCOMP_FLUSH_GETANIMATING_BYTES_SIZE,
		"'CLagCompensationManager::BacktrackEntity' patch offset incorrect (expected the GetBaseAnimating call).");
	ExpectCode(flushCode + (LAGCOMP_FLUSH_INVALIDATE_OFFSET - LAGCOMP_FLUSH_TEST_OFFSET), LAGCOMP_FLUSH_INVALIDATE_BYTES,
		LAGCOMP_FLUSH_INVALIDATE_BYTES_SIZE,
		"'CLagCompensationManager::BacktrackEntity' patch offset incorrect (expected the bone cache flush).");
	if (RelativeTarget(pBacktrack + LAGCOMP_FLUSH_INVALIDATE_CALL_OFFSET,
		flushCode + (LAGCOMP_FLUSH_INVALIDATE_CALL_OFFSET - LAGCOMP_FLUSH_TEST_OFFSET)) != pInvalidate)
	{
		throw PatchException("'CLagCompensationManager::BacktrackEntity' patch offset incorrect (expected the call to InvalidateBoneCache).");
	}

	ExpectCode(restoreCode, LAGCOMP_RESTORE_ARGS_BYTES, LAGCOMP_RESTORE_ARGS_BYTES_SIZE,
		"'CLagCompensationManager::FinishLagCompensation' patch offset incorrect (expected the restore call arguments).");
	BYTE* pCall = pFinish + LAGCOMP_RESTORE_CALL_OFFSET;
	if (RelativeTarget(pCall, restoreCode + (LAGCOMP_RESTORE_CALL_OFFSET - LAGCOMP_RESTORE_ARGS_OFFSET)) != pRestore)
	{
		throw PatchException("'CLagCompensationManager::FinishLagCompensation' patch offset incorrect (expected the call to RestoreEntityFromRecords).");
	}

	s_RestoreEntity = reinterpret_cast<RestoreEntityFn>(pRestore);
	s_InvalidateBoneCache = reinterpret_cast<InvalidateBoneCacheFn>(pInvalidate);

	// test [flags], LC_ANIMATION_CHANGED -> test [flags], <origin | angles | bounds | animation>
	const uint32_t mask = LAGCOMP_CHANGE_ANY;
	BYTE maskBytes[sizeof(mask)];
	memcpy(maskBytes, &mask, sizeof(maskBytes));
	m_patches.Register(new BasicStaticBinPatch<sizeof(maskBytes)>(pBacktrack + LAGCOMP_FLUSH_MASK_OFFSET, maskBytes));

	// call RestoreEntityFromRecords -> call OnRestoreEntityFromRecords
	int32_t displacement = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&OnRestoreEntityFromRecords)
		- (reinterpret_cast<uintptr_t>(pCall) + 5));
	BYTE callBytes[sizeof(displacement)];
	memcpy(callBytes, &displacement, sizeof(callBytes));
	m_patches.Register(new BasicStaticBinPatch<sizeof(callBytes)>(pCall + 1, callBytes));
}

LagCompBoneCachePatch::~LagCompBoneCachePatch()
{
	m_patches.UnregisterAll();
}

void LagCompBoneCachePatch::Patch()
{
	m_patches.PatchAll();
}

void LagCompBoneCachePatch::Unpatch()
{
	m_patches.UnpatchAll();
}

BulletHitboxLagCompPatch::BulletHitboxLagCompPatch(IServerGameDLL* gamedll) : m_patch(NULL)
{
	BYTE* pFireBullets = (BYTE*)g_MemUtils.SimpleResolve(gamedll, SYM_FIRETERRORBULLETS);
	void* ppLagCompensation = g_MemUtils.SimpleResolve(gamedll, SYM_LAGCOMPENSATION);
	void* pVecOrigin = g_MemUtils.SimpleResolve(gamedll, SYM_VEC3_ORIGIN);
	void* pVecAngle = g_MemUtils.SimpleResolve(gamedll, SYM_VEC3_ANGLE);
	if (pFireBullets == NULL || ppLagCompensation == NULL || pVecOrigin == NULL || pVecAngle == NULL)
	{
		throw PatchException("Couldn't find 'FireTerrorBullets', 'lagcompensation', 'vec3_origin' or 'vec3_angle'.");
	}

	DynLibInfo lib = {};
	if (!g_MemUtils.GetLibraryInfo(gamedll, lib))
	{
		throw PatchException("Couldn't get server module information.");
	}
	if (!IsInModule(pFireBullets, BULLET_LAGCOMP_CODE_SIZE, lib))
	{
		throw PatchException("'FireTerrorBullets' patch offset outside server module.");
	}

	BYTE code[BULLET_LAGCOMP_CODE_SIZE];
	if (!ReadModuleMemory(code, pFireBullets, sizeof(code), lib))
	{
		throw PatchException("'FireTerrorBullets' is not readable.");
	}

	if (code[BULLET_LAGCOMP_LOAD_OFFSET] != 0xA1 || !IsOperand(code + BULLET_LAGCOMP_LOAD_OFFSET + 1, ppLagCompensation))
	{
		throw PatchException("'FireTerrorBullets' patch offset incorrect (expected 'mov eax, lagcompensation').");
	}
	ExpectCode(code + BULLET_LAGCOMP_ARGS_OFFSET, BULLET_LAGCOMP_ARGS_BYTES, BULLET_LAGCOMP_ARGS_BYTES_SIZE,
		"'FireTerrorBullets' patch offset incorrect (expected the zero weapon range).");
	ExpectCode(code + BULLET_LAGCOMP_ORIGIN_OFFSET, BULLET_LAGCOMP_ORIGIN_BYTES, BULLET_LAGCOMP_ORIGIN_BYTES_SIZE,
		"'FireTerrorBullets' patch offset incorrect (expected the weapon position argument).");
	if (!IsOperand(code + BULLET_LAGCOMP_ANGLES_OPERAND_OFFSET, pVecAngle)
		|| !IsOperand(code + BULLET_LAGCOMP_ORIGIN_OPERAND_OFFSET, pVecOrigin))
	{
		throw PatchException("'FireTerrorBullets' patch offset incorrect (expected vec3_angle / vec3_origin as the weapon ray).");
	}
	ExpectCode(code + BULLET_LAGCOMP_TYPE_OFFSET, BULLET_LAGCOMP_TYPE_BYTES, BULLET_LAGCOMP_TYPE_BYTES_SIZE,
		"'FireTerrorBullets' patch offset incorrect (expected LAG_COMPENSATE_HITBOXES_ALONG_RAY).");

	// LAG_COMPENSATE_HITBOXES_ALONG_RAY -> LAG_COMPENSATE_HITBOXES
	const uint32_t type = LAG_COMPENSATE_HITBOXES;
	BYTE typeBytes[sizeof(type)];
	memcpy(typeBytes, &type, sizeof(typeBytes));
	m_patch = new BasicStaticBinPatch<sizeof(typeBytes)>(pFireBullets + BULLET_LAGCOMP_TYPE_IMMEDIATE_OFFSET, typeBytes);
}

BulletHitboxLagCompPatch::~BulletHitboxLagCompPatch()
{
	delete m_patch;
}

void BulletHitboxLagCompPatch::Patch()
{
	m_patch->Patch();
}

void BulletHitboxLagCompPatch::Unpatch()
{
	m_patch->Unpatch();
}
#endif
