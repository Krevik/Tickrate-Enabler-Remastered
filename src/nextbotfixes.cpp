/**
 * vim: set ts=4 :
 * =============================================================================
 * NextBot fixes (L4D2)
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
#include <string.h>
#if defined (_LINUX)
#include <dlfcn.h>
#endif
#include "thirdparty/memutils.h"
#include "thirdparty/sm_convar.h"
#include "server_class.h"
#include "dt_send.h"
#include "iserverunknown.h"
#include "iservernetworkable.h"
#include "edict.h"
#include "nextbotfixes.h"
#include "patchexceptions.h"

#if SOURCE_ENGINE == SE_LEFT4DEAD2

// How far past the function start we search for the instructions to patch.
#define NB_SCAN_LEN 0x480

// Vanilla values the game was tuned for.
#define ZOMBIE_PUSH_DIST_DEFAULT 3.0f
#define NB_UPDATE_INTERVAL_DEFAULT 0.1f
#define TICK_INTERVAL_DEFAULT (1.0f / 30.0f)

// Read by the patched ZombieBotLocomotion::ResolveZombieCollisions instead of its 3.0 constant.
static float g_flZombiePushDist = ZOMBIE_PUSH_DIST_DEFAULT;

// Read by the patched WitchAngry::OnContact instead of gpGlobals (only 'frametime' is used).
struct FakeFrameTimeGlobals
{
	float padding[4];
	float frametime; // same offset as CGlobalVarsBase::frametime
};
static FakeFrameTimeGlobals g_WitchAngerGlobals = { {0.0f, 0.0f, 0.0f, 0.0f}, TICK_INTERVAL_DEFAULT };
static FakeFrameTimeGlobals* gp_WitchAngerGlobals = &g_WitchAngerGlobals;

static bool IsInModule(const void* ptr, size_t len, const DynLibInfo& lib)
{
	const BYTE* p = (const BYTE*)ptr;
	const BYTE* base = (const BYTE*)lib.baseAddress;
	return p >= base && p + len <= base + lib.memorySize;
}

// Like IsInModule, but also accepts the module's writable data segments (on Linux
// DynLibInfo only covers the executable segment, while e.g. gpGlobals lives in .bss).
static bool IsModuleData(const void* ptr, size_t len, const DynLibInfo& lib)
{
	if (IsInModule(ptr, len, lib))
	{
		return true;
	}
#if defined (_LINUX)
	Dl_info info;
	Dl_info infoEnd;
	return dladdr(ptr, &info) != 0 && dladdr((const BYTE*)ptr + len - 1, &infoEnd) != 0
		&& info.dli_fbase == lib.baseAddress && infoEnd.dli_fbase == lib.baseAddress;
#else
	return false;
#endif
}

static void GetModuleInfo(void* serverModule, DynLibInfo& lib)
{
	memset(&lib, 0, sizeof(lib));
	if (!g_MemUtils.GetLibraryInfo(serverModule, lib))
	{
		throw PatchException("Couldn't get server module information.");
	}
}

// Searches [func, func + NB_SCAN_LEN) for pattern (0x2A = wildcard). Returns the only match,
// NULL if there is none or more than one. The scan never leaves the module.
static BYTE* FindUniqueInFunction(BYTE* func, const DynLibInfo& lib, const char* pattern, size_t len, bool (*verify)(BYTE*, const DynLibInfo&) = NULL, size_t scanLen = NB_SCAN_LEN)
{
	BYTE* found = NULL;
	for (size_t i = 0; i < scanLen; i++)
	{
		BYTE* p = func + i;
		// pattern plus the following bytes the verify callbacks may read
		if (!IsInModule(p, len + 16, lib))
		{
			break;
		}

		bool match = true;
		for (size_t j = 0; j < len; j++)
		{
			if (pattern[j] != '\x2A' && (BYTE)pattern[j] != p[j])
			{
				match = false;
				break;
			}
		}

		if (!match || (verify != NULL && !verify(p, lib)))
		{
			continue;
		}

		if (found != NULL)
		{
			return NULL; // ambiguous
		}
		found = p;
	}
	return found;
}

/*
 * ZombieCollisionPushPatch
 */

// movss xmmN, dword ptr [disp32] reading the 3.0 push distance constant
static bool IsPushConstLoad(BYTE* p, const DynLibInfo& lib)
{
	if ((p[3] & 0xC7) != 0x05) // ModRM: mod=00, rm=101 (absolute disp32), any xmm register
	{
		return false;
	}

	const float* pConst = *(const float**)(p + 4);
	return IsInModule(pConst, sizeof(float), lib) && *pConst == ZOMBIE_PUSH_DIST_DEFAULT;
}

ZombieCollisionPushPatch::ZombieCollisionPushPatch(void* serverModule) : m_patch(NULL)
{
	DynLibInfo lib;
	GetModuleInfo(serverModule, lib);

#if defined (_LINUX)
	BYTE* pFunc = (BYTE*)g_MemUtils.SimpleResolve(serverModule, SYM_ZOMBIEBOTLOCOMOTION_RESOLVEZOMBIECOLLISIONS);
#elif defined (_WIN32)
	BYTE* pFunc = (BYTE*)g_MemUtils.FindLibPattern(serverModule, SIG_ZOMBIEBOTLOCOMOTION_RESOLVEZOMBIECOLLISIONS, SIG_ZOMBIEBOTLOCOMOTION_RESOLVEZOMBIECOLLISIONS_SIZE);
#endif
	if (pFunc == NULL)
	{
		throw PatchException("Couldn't find 'ZombieBotLocomotion::ResolveZombieCollisions'.");
	}

	// F3 0F 10 /r disp32 = movss xmm, dword ptr [disp32]
	BYTE* pInstr = FindUniqueInFunction(pFunc, lib, "\xF3\x0F\x10", 3, IsPushConstLoad);
	if (pInstr == NULL)
	{
		throw PatchException("'ZombieBotLocomotion::ResolveZombieCollisions' push distance load not found.");
	}

	float* pNewConst = &g_flZombiePushDist;
	BYTE replacement[sizeof(float*)];
	memcpy(replacement, &pNewConst, sizeof(replacement));

	m_patch = new BasicStaticBinPatch<sizeof(replacement)>(pInstr + 4, replacement);
}

ZombieCollisionPushPatch::~ZombieCollisionPushPatch()
{
	delete m_patch;
}

void ZombieCollisionPushPatch::Patch()
{
	m_patch->Patch();
}

void ZombieCollisionPushPatch::Unpatch()
{
	m_patch->Unpatch();
}

void UpdateNextBotFixes(const ConVar* pNbUpdateFrequency, float tickInterval)
{
	if (pNbUpdateFrequency == NULL || tickInterval <= 0.0f)
	{
		return;
	}

	// Same rounding as NextBotManager: bots are updated every TIME_TO_TICKS(nb_update_frequency)
	// ticks, or every tick if that is 0.
	int ticks = (int)(pNbUpdateFrequency->GetFloat() / tickInterval + 0.5f);
	if (ticks < 1)
	{
		ticks = 1;
	}

	// Fraction of a vanilla update interval that one update covers now.
	float updateScale = (ticks * tickInterval) / NB_UPDATE_INTERVAL_DEFAULT;

	g_flZombiePushDist = ZOMBIE_PUSH_DIST_DEFAULT * (updateScale > 1.0f ? 1.0f : updateScale);

	// Vanilla adds 1/30 s worth of anger per contact event, with 10 events per second.
	g_WitchAngerGlobals.frametime = TICK_INTERVAL_DEFAULT * updateScale;
}

/*
 * WitchContactAngerPatch
 */

static const void* s_pRealGlobals = NULL;

// mov reg, [gpGlobals] followed directly by mulss xmm0, [reg+10h] (gpGlobals->frametime)
static bool IsFrameTimeRead(BYTE* p, const DynLibInfo& lib)
{
	if (!mov_is_absolute_disp32(p))
	{
		return false;
	}

	int offs = mov_src_operand_offset(p);
	const void** ppGlobals = *(const void***)(p + offs);
	if (!IsModuleData(ppGlobals, sizeof(void*), lib) || *ppGlobals != s_pRealGlobals)
	{
		return false;
	}

	// destination register of the mov (eax for the A1 form)
	BYTE reg = (p[0] == 0xA1) ? 0 : ((p[1] >> 3) & 7);
	BYTE* next = p + offs + 4;
	return next[0] == 0xF3 && next[1] == 0x0F && next[2] == 0x59 && next[3] == (0x40 | reg) && next[4] == 0x10;
}

WitchContactAngerPatch::WitchContactAngerPatch(void* serverModule, const void* realGlobals) : m_patch(NULL)
{
	if (realGlobals == NULL)
	{
		throw PatchException("gpGlobals not available.");
	}
	s_pRealGlobals = realGlobals;

	DynLibInfo lib;
	GetModuleInfo(serverModule, lib);

#if defined (_LINUX)
	BYTE* pFunc = (BYTE*)g_MemUtils.SimpleResolve(serverModule, SYM_WITCHANGRY_ONCONTACT);
#elif defined (_WIN32)
	BYTE* pFunc = (BYTE*)g_MemUtils.FindLibPattern(serverModule, SIG_WITCHANGRY_ONCONTACT, SIG_WITCHANGRY_ONCONTACT_SIZE);
#endif
	if (pFunc == NULL)
	{
		throw PatchException("Couldn't find 'WitchAngry::OnContact'.");
	}

	// The function is small (~0x90 bytes); look for both mov forms (A1 / 8B) within it only.
	const size_t scanLen = 0xA0;
	BYTE* pInstr = FindUniqueInFunction(pFunc, lib, "\xA1", 1, IsFrameTimeRead, scanLen);
	if (pInstr == NULL)
	{
		pInstr = FindUniqueInFunction(pFunc, lib, "\x8B", 1, IsFrameTimeRead, scanLen);
	}
	if (pInstr == NULL)
	{
		throw PatchException("'WitchAngry::OnContact' frametime read not found.");
	}

	BYTE replacement[MAX_MOV_INSTR_LEN];
	int offs = mov_src_operand_offset(pInstr);
	memcpy(replacement, pInstr, offs);
	FakeFrameTimeGlobals** ppFake = &gp_WitchAngerGlobals;
	memcpy(replacement + offs, &ppFake, sizeof(ppFake));

	m_patch = new BasicBinPatch(pInstr, replacement, offs + 4);
}

WitchContactAngerPatch::~WitchContactAngerPatch()
{
	delete m_patch;
}

void WitchContactAngerPatch::Patch()
{
	m_patch->Patch();
}

void WitchContactAngerPatch::Unpatch()
{
	m_patch->Unpatch();
}

/*
 * WitchContactVictimPatch
 */

#if defined (_LINUX)
static BYTE* s_pIsValidEnemy = NULL;

// mov eax, [ebp+disp8]; mov [esp], eax; call WitchAttack::IsValidEnemy
static bool IsValidEnemyCallLinux(BYTE* p, const DynLibInfo& lib)
{
	BYTE* pTarget = p + 11 + *(int32_t*)(p + 7);
	return pTarget == s_pIsValidEnemy;
}
#elif defined (_WIN32)
// mov ecx, [ebp+14h]; push ecx; call WitchAttack::IsValidEnemy; add esp, 4; test al, al; jz
static bool IsValidEnemyCallWin(BYTE* p, const DynLibInfo& lib)
{
	BYTE* pTarget = p + 9 + *(int32_t*)(p + 5);
	return IsInModule(pTarget, SIG_WITCHATTACK_ISVALIDENEMY_SIZE, lib)
		&& memcmp(pTarget, SIG_WITCHATTACK_ISVALIDENEMY, SIG_WITCHATTACK_ISVALIDENEMY_SIZE) == 0;
}
#endif

WitchContactVictimPatch::WitchContactVictimPatch(void* serverModule) : m_patch(NULL)
{
	DynLibInfo lib;
	GetModuleInfo(serverModule, lib);

#if defined (_LINUX)
	BYTE* pFunc = (BYTE*)g_MemUtils.SimpleResolve(serverModule, SYM_WITCHATTACK_ONCONTACT);
	s_pIsValidEnemy = (BYTE*)g_MemUtils.SimpleResolve(serverModule, SYM_WITCHATTACK_ISVALIDENEMY);
	if (s_pIsValidEnemy == NULL)
	{
		throw PatchException("Couldn't find 'WitchAttack::IsValidEnemy'.");
	}
#elif defined (_WIN32)
	BYTE* pFunc = (BYTE*)g_MemUtils.FindLibPattern(serverModule, SIG_WITCHATTACK_ONCONTACT, SIG_WITCHATTACK_ONCONTACT_SIZE);
#endif
	if (pFunc == NULL)
	{
		throw PatchException("Couldn't find 'WitchAttack::OnContact'.");
	}

#if defined (_LINUX)
	// 8B 45 ?? 89 04 24 E8 -> the 'other' entity is in edi at this point
	BYTE* pInstr = FindUniqueInFunction(pFunc, lib, "\x8B\x45\x2A\x89\x04\x24\xE8", 7, IsValidEnemyCallLinux);
	const BYTE replacement[] = { 0x89, 0xF8, 0x90 }; // mov eax, edi; nop
#elif defined (_WIN32)
	// 8B 4D 14 51 E8 ?? ?? ?? ?? 83 C4 04 84 C0 0F 84 -> the 'other' entity is in edi at this point
	BYTE* pInstr = FindUniqueInFunction(pFunc, lib, "\x8B\x4D\x14\x51\xE8\x2A\x2A\x2A\x2A\x83\xC4\x04\x84\xC0\x0F\x84", 16, IsValidEnemyCallWin);
	const BYTE replacement[] = { 0x8B, 0xCF, 0x90 }; // mov ecx, edi; nop
#endif
	if (pInstr == NULL)
	{
		throw PatchException("'WitchAttack::OnContact' IsValidEnemy call not found.");
	}

	m_patch = new BasicStaticBinPatch<sizeof(replacement)>(pInstr, replacement);
}

WitchContactVictimPatch::~WitchContactVictimPatch()
{
	delete m_patch;
}

void WitchContactVictimPatch::Patch()
{
	m_patch->Patch();
}

void WitchContactVictimPatch::Unpatch()
{
	m_patch->Unpatch();
}

/*
 * WitchClimbSpeedPatch
 */

// ILocomotion::ClimbUpToLedge(const Vector& landingGoal, const Vector& landingForward, const CBaseEntity* obstacle)
#if defined (_LINUX)
	#define WITCHLOCO_CLIMB_VTABLE_INDEX 47 // two destructor slots
	typedef bool (*ClimbUpToLedgeFn)(void* thisptr, const void* goal, const void* forward, const void* obstacle);
#elif defined (_WIN32)
	#define WITCHLOCO_CLIMB_VTABLE_INDEX 46
	// thiscall emulated with fastcall: 'this' in ecx, edx unused, callee cleans the stack args
	typedef bool (__fastcall *ClimbUpToLedgeFn)(void* thisptr, void* edx, const void* goal, const void* forward, const void* obstacle);
#endif

// INextBotComponent / locomotion member pointing to the bot's entity (both platforms)
#define LOCOMOTION_ENTITY_OFFSET 0x78

static ClimbUpToLedgeFn s_pOrigClimbUpToLedge = NULL;
static const ConVar* s_pWitchClimbRate = NULL;
static int s_iPlaybackRateOffset = -1;

static void OnWitchClimbStarted(void* locomotion)
{
	if (s_pWitchClimbRate == NULL || s_iPlaybackRateOffset < 0)
	{
		return;
	}

	float rate = s_pWitchClimbRate->GetFloat();
	if (rate <= 1.0f)
	{
		return;
	}

	BYTE* pEntity = *(BYTE**)((BYTE*)locomotion + LOCOMOTION_ENTITY_OFFSET);
	if (pEntity == NULL)
	{
		return;
	}

	*(float*)(pEntity + s_iPlaybackRateOffset) = rate;

	// Network the new rate so the client side animation matches the server.
	IServerNetworkable* pNetworkable = ((IServerUnknown*)pEntity)->GetNetworkable();
	edict_t* pEdict = pNetworkable != NULL ? pNetworkable->GetEdict() : NULL;
	if (pEdict != NULL)
	{
		pEdict->m_fStateFlags |= (FL_EDICT_CHANGED | FL_FULL_EDICT_CHANGED);
	}
}

#if defined (_LINUX)
static bool Hook_WitchClimbUpToLedge(void* thisptr, const void* goal, const void* forward, const void* obstacle)
{
	bool result = s_pOrigClimbUpToLedge(thisptr, goal, forward, obstacle);
#elif defined (_WIN32)
static bool __fastcall Hook_WitchClimbUpToLedge(void* thisptr, void* edx, const void* goal, const void* forward, const void* obstacle)
{
	bool result = s_pOrigClimbUpToLedge(thisptr, edx, goal, forward, obstacle);
#endif
	if (result)
	{
		OnWitchClimbStarted(thisptr);
	}
	return result;
}

static int FindSendPropOffset(SendTable* pTable, const char* name)
{
	for (int i = 0; i < pTable->GetNumProps(); i++)
	{
		SendProp* pProp = pTable->GetProp(i);
		if (strcmp(pProp->GetName(), name) == 0)
		{
			return pProp->GetOffset();
		}

		if (pProp->GetType() == DPT_DataTable && pProp->GetDataTable() != NULL)
		{
			int offs = FindSendPropOffset(pProp->GetDataTable(), name);
			if (offs >= 0)
			{
				return pProp->GetOffset() + offs;
			}
		}
	}
	return -1;
}

#if defined (_WIN32)
// Finds the (primary) vtable of an MSVC class through its RTTI, e.g. ".?AVWitchLocomotion@@".
static void** FindMsvcVTable(const DynLibInfo& lib, const char* typeName)
{
	BYTE* base = (BYTE*)lib.baseAddress;
	BYTE* end = base + lib.memorySize;
	size_t nameLen = strlen(typeName) + 1;

	BYTE* pName = (BYTE*)g_MemUtils.FindPattern(base, end - nameLen, typeName, nameLen);
	if (pName == NULL)
	{
		return NULL;
	}
	uintptr_t typeDescriptor = (uintptr_t)(pName - 8); // vftable ptr, spare, name

	// RTTICompleteObjectLocator: signature, offset, cdOffset, pTypeDescriptor, pClassDescriptor
	void** found = NULL;
	for (BYTE* p = base + 12; p + 4 <= end; p += 4)
	{
		if (*(uintptr_t*)p != typeDescriptor)
		{
			continue;
		}

		uintptr_t* col = (uintptr_t*)(p - 12);
		if (col[0] != 0 || col[1] != 0)
		{
			continue;
		}

		// the vtable is preceded by a pointer to its complete object locator
		for (BYTE* q = base; q + 8 <= end; q += 4)
		{
			if (*(uintptr_t*)q == (uintptr_t)col)
			{
				if (found != NULL)
				{
					return NULL; // ambiguous
				}
				found = (void**)(q + 4);
			}
		}
	}
	return found;
}
#endif

int FindWitchPlaybackRateOffset(IServerGameDLL* gamedll)
{
	// expected 0x468 on Linux, 0x454 on Windows
	for (ServerClass* pClass = gamedll->GetAllServerClasses(); pClass != NULL; pClass = pClass->m_pNext)
	{
		if (strcmp(pClass->GetName(), "Witch") == 0)
		{
			return FindSendPropOffset(pClass->m_pTable, "m_flPlaybackRate");
		}
	}
	return -1;
}

WitchClimbSpeedPatch::WitchClimbSpeedPatch(void* serverModule, int playbackRateOffset, const ConVar* pClimbRate) : m_patch(NULL)
{
	if (pClimbRate == NULL)
	{
		throw PatchException("climb rate cvar not available.");
	}

	if (playbackRateOffset <= 0)
	{
		throw PatchException("Couldn't find 'Witch::m_flPlaybackRate'.");
	}
	int offset = playbackRateOffset;
	void* gamedll = serverModule;

	DynLibInfo lib;
	GetModuleInfo(gamedll, lib);

#if defined (_LINUX)
	void** pVTable = (void**)g_MemUtils.SimpleResolve(gamedll, SYM_WITCHLOCOMOTION_VTABLE);
	if (pVTable != NULL)
	{
		pVTable += 2; // offset-to-top, typeinfo
	}
	void* pClimb = g_MemUtils.SimpleResolve(gamedll, SYM_ZOMBIEBOTLOCOMOTION_CLIMBUPTOLEDGE);
#elif defined (_WIN32)
	void** pVTable = FindMsvcVTable(lib, ".?AVWitchLocomotion@@");
	void* pClimb = g_MemUtils.FindLibPattern(gamedll, SIG_ZOMBIEBOTLOCOMOTION_CLIMBUPTOLEDGE, SIG_ZOMBIEBOTLOCOMOTION_CLIMBUPTOLEDGE_SIZE);
#endif
	if (pVTable == NULL || !IsModuleData(pVTable, (WITCHLOCO_CLIMB_VTABLE_INDEX + 1) * sizeof(void*), lib))
	{
		throw PatchException("Couldn't find the 'WitchLocomotion' vtable.");
	}
	if (pClimb == NULL || pVTable[WITCHLOCO_CLIMB_VTABLE_INDEX] != pClimb)
	{
		throw PatchException("'WitchLocomotion' vtable doesn't contain 'ZombieBotLocomotion::ClimbUpToLedge' at the expected index.");
	}

	s_iPlaybackRateOffset = offset;
	s_pWitchClimbRate = pClimbRate;
	s_pOrigClimbUpToLedge = (ClimbUpToLedgeFn)pClimb;

	void* pHook = (void*)&Hook_WitchClimbUpToLedge;
	BYTE replacement[sizeof(void*)];
	memcpy(replacement, &pHook, sizeof(replacement));
	m_patch = new BasicStaticBinPatch<sizeof(replacement)>((BYTE*)&pVTable[WITCHLOCO_CLIMB_VTABLE_INDEX], replacement);
}

WitchClimbSpeedPatch::~WitchClimbSpeedPatch()
{
	delete m_patch;
}

void WitchClimbSpeedPatch::Patch()
{
	m_patch->Patch();
}

void WitchClimbSpeedPatch::Unpatch()
{
	m_patch->Unpatch();
}

#endif // SOURCE_ENGINE == SE_LEFT4DEAD2
