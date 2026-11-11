/**
 * vim: set ts=4 :
 * =============================================================================
 * BoomerVomitPatch
 * Copyright (C) 2012 Michael "ProdigySim" Busby
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
 *
 * As a special exception, the author(s) give you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, the author(s) grant
 * this exception to all derivative works.  The author(s) define further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */
#include <cstdlib>
#include <limits>
#if defined (_LINUX)
#include <dlfcn.h>
#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>
#endif
#include "thirdparty/memutils.h"
#include "boomervomitpatch.h"
#include "patchexceptions.h"
#if SOURCE_ENGINE == SE_LEFT4DEAD2 && defined (_LINUX)
#include "edict.h"
#include "engine/IEngineTrace.h"
#include "gametrace.h"
#endif

static bool IsInModule(const void* address, size_t length, const DynLibInfo& lib)
{
	uintptr_t startAddress = reinterpret_cast<uintptr_t>(address);
	uintptr_t baseAddress = reinterpret_cast<uintptr_t>(lib.baseAddress);
	return startAddress >= baseAddress && length <= lib.memorySize
		&& startAddress - baseAddress <= lib.memorySize - length;
}

#if defined (_LINUX)
// The kernel copies from 'source' into a pipe and reports EFAULT for unreadable memory instead of
// crashing. Chunks of PIPE_BUF bytes always fit, even into a pipe shrunk to a single page.
static bool ReadMemoryThroughPipe(void* destination, const void* source, size_t length)
{
	int fds[2];
	if (pipe(fds) != 0)
	{
		return false;
	}

	bool ok = true;
	for (size_t done = 0; ok && done < length;)
	{
		size_t chunk = length - done < 4096 ? length - done : 4096;
		ok = write(fds[1], static_cast<const BYTE*>(source) + done, chunk) == (ssize_t)chunk
			&& read(fds[0], static_cast<BYTE*>(destination) + done, chunk) == (ssize_t)chunk;
		done += chunk;
	}

	close(fds[0]);
	close(fds[1]);
	return ok;
}
#endif

// Read through the OS so stale operands fail without dereferencing inaccessible memory.
// Linux data segments can lie outside the executable range reported by GetLibraryInfo.
static bool ReadModuleMemory(void* destination, const void* source, size_t length, const DynLibInfo& lib)
{
	uintptr_t startAddress = reinterpret_cast<uintptr_t>(source);
	if (source == NULL || length == 0 || length - 1 > (std::numeric_limits<uintptr_t>::max)() - startAddress)
	{
		return false;
	}
#if defined (_LINUX)
	Dl_info startInfo;
	Dl_info endInfo;
	const void* endAddress = reinterpret_cast<const void*>(startAddress + length - 1);
	if (dladdr(source, &startInfo) == 0 || dladdr(endAddress, &endInfo) == 0
		|| startInfo.dli_fbase != lib.baseAddress || endInfo.dli_fbase != lib.baseAddress)
	{
		return false;
	}
	iovec local = { destination, length };
	iovec remote = { const_cast<void*>(source), length };
	ssize_t copied = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
	if (copied == (ssize_t)length)
	{
		return true;
	}
	// process_vm_readv is often blocked for game servers (seccomp / container profiles: EPERM,
	// ENOSYS). Only EFAULT and short reads mean the memory itself is not readable.
	return copied < 0 && errno != EFAULT && ReadMemoryThroughPipe(destination, source, length);
#elif defined (_WIN32)
	if (!IsInModule(source, length, lib))
	{
		return false;
	}
	SIZE_T bytesRead = 0;
	return ReadProcessMemory(GetCurrentProcess(), source, destination, length, &bytesRead)
		&& bytesRead == length;
#endif
}

struct fakeGlobals
{
	float padding[4];
	float frametime;
};

fakeGlobals g_FakeGlobals = { {0.0, 0.0, 0.0, 0.0}, 0.033333333 };
fakeGlobals* gp_FakeGlobals = &g_FakeGlobals;

#ifdef _LINUX
	#if SOURCE_ENGINE == SE_LEFT4DEAD
		fakeGlobals** gpp_FakeGlobals = &gp_FakeGlobals; // olol
	#endif
#endif

BoomerVomitFrameTimePatch::BoomerVomitFrameTimePatch(IServerGameDLL* gamedll, const void* realGlobals)
{
	InitializeBinPatches(gamedll, realGlobals);
}

BoomerVomitFrameTimePatch::~BoomerVomitFrameTimePatch()
{
	m_patches.UnregisterAll();
}

void BoomerVomitFrameTimePatch::Patch()
{
	m_patches.PatchAll();
}

void BoomerVomitFrameTimePatch::Unpatch()
{
	m_patches.UnpatchAll();
}

void BoomerVomitFrameTimePatch::InitializeBinPatches(IServerGameDLL* gamedll, const void* realGlobals)
{
	BYTE instr_buf[MAX_MOV_INSTR_LEN];
	void* pFirstGlobalsAddr = NULL;

	BYTE* pCVomitUpdateAbility = FindCVomitUpdateAbility(static_cast<void*>(gamedll));
	DevMsg("CVomitUpdateAbility at 0x%08x\n", pCVomitUpdateAbility);

	if (!pCVomitUpdateAbility)
	{
		throw PatchException("Couldn't find 'CVomit::UpdateAbility' in server memory.");
	}

	DynLibInfo lib = {};
	if (!g_MemUtils.GetLibraryInfo(gamedll, lib))
	{
		throw PatchException("Couldn't get server module information.");
	}

	for (size_t i = 0; i < NUM_FRAMETIME_READS; i++)
	{
		DevMsg("Setting up patch for frametime read %d (offs:0x%x).\n", i, g_FrameTimeReadOffsets[i]);

		if (g_FrameTimeReadOffsets[i] < 0
			|| !IsInModule(pCVomitUpdateAbility, g_FrameTimeReadOffsets[i] + MAX_MOV_INSTR_LEN, lib))
		{
			throw PatchException("'CVomit::UpdateAbility' patch offset outside server module.");
		}
		BYTE* pTarget = pCVomitUpdateAbility + g_FrameTimeReadOffsets[i];
		if (!ReadModuleMemory(instr_buf, pTarget, sizeof(instr_buf), lib))
		{
			throw PatchException("'CVomit::UpdateAbility' patch instruction is not readable.");
		}

		int offs = mov_src_operand_offset(instr_buf); // Find offset of disp32 in this particular mov instruction
		if (offs == 0 || !mov_is_patchable_disp32(instr_buf))
		{
			// Unexpected instruction (or a form whose length would change after rewriting)
			throw PatchException("'CVomit::UpdateAbility' patch offset incorrect (unexpected instruction).");
		}

		// For absolute 'mov reg, [addr]' we can verify that 'addr' really is the game's gpGlobals
		// variable. This catches stale offsets after game updates that would otherwise silently
		// redirect some unrelated global. (Register-relative PIC forms on Linux can't be checked here.)
		if (mov_is_absolute_disp32(instr_buf))
		{
			void* pGlobalsAddr;
			memcpy(&pGlobalsAddr, instr_buf + offs, sizeof(pGlobalsAddr));
			const void* pOriginalGlobals;
			if (!ReadModuleMemory(&pOriginalGlobals, pGlobalsAddr, sizeof(pOriginalGlobals), lib))
			{
				throw PatchException("'CVomit::UpdateAbility' gpGlobals operand is not readable server module data.");
			}

			if (realGlobals != NULL && pOriginalGlobals != realGlobals)
			{
				throw PatchException("'CVomit::UpdateAbility' patch offset incorrect (instruction does not read gpGlobals).");
			}

			if (pFirstGlobalsAddr == NULL)
			{
				pFirstGlobalsAddr = pGlobalsAddr;
			}
			else if (pFirstGlobalsAddr != pGlobalsAddr)
			{
				throw PatchException("'CVomit::UpdateAbility' patch offset incorrect (frametime reads use different globals).");
			}
		}

		// make this instruction read from an immediate address
		mov_to_disp32(instr_buf);

		// Plug in our super cool immediate address.
#if defined (_WIN32)
		fakeGlobals** pFakeGlobals = &gp_FakeGlobals;
#elif defined (_LINUX)
	#if SOURCE_ENGINE == SE_LEFT4DEAD
			fakeGlobals*** pFakeGlobals = &gpp_FakeGlobals;
	#elif SOURCE_ENGINE == SE_LEFT4DEAD2
			fakeGlobals** pFakeGlobals = &gp_FakeGlobals;
	#endif
#endif
		memcpy(instr_buf + offs, &pFakeGlobals, sizeof(pFakeGlobals));

		// Generate BasicBinPatch
		m_patches.Register(new BasicStaticBinPatch<MAX_MOV_INSTR_LEN>(pTarget, instr_buf));
	}
}

#if SOURCE_ENGINE == SE_LEFT4DEAD2
// Read by the patched instruction in CVomit::ActivateAbility instead of the shared 0.25 constant.
static float g_flVomitDelay = VOMIT_DELAY_VANILLA;

BoomerVomitDelayPatch::BoomerVomitDelayPatch(IServerGameDLL* gamedll, float delay) : m_patch(NULL)
{
#if defined (_LINUX)
	BYTE* pActivateAbility = (BYTE*)g_MemUtils.SimpleResolve(gamedll, SYM_CVOMIT_ACTIVATEABILITY);
#elif defined (_WIN32)
	BYTE* pActivateAbility = (BYTE*)g_MemUtils.FindLibPattern(gamedll, SIG_CVOMIT_ACTIVATEABILITY, SIG_CVOMIT_ACTIVATEABILITY_SIZE);
#endif
	if (pActivateAbility == NULL)
	{
		throw PatchException("Couldn't find 'CVomit::ActivateAbility' in server memory.");
	}

	DynLibInfo lib = {};
	if (!g_MemUtils.GetLibraryInfo(gamedll, lib))
	{
		throw PatchException("Couldn't get server module information.");
	}
	if (!IsInModule(pActivateAbility, VOMIT_DELAY_PATCH_OFFSET + VOMIT_DELAY_PATCH_OPCODE_SIZE + sizeof(float*), lib))
	{
		throw PatchException("'CVomit::ActivateAbility' patch offset outside server module.");
	}

	BYTE* pInstr = pActivateAbility + VOMIT_DELAY_PATCH_OFFSET;
	BYTE instruction[VOMIT_DELAY_PATCH_OPCODE_SIZE + sizeof(float*)];
	if (!ReadModuleMemory(instruction, pInstr, sizeof(instruction), lib)
		|| memcmp(instruction, VOMIT_DELAY_PATCH_OPCODE, VOMIT_DELAY_PATCH_OPCODE_SIZE) != 0)
	{
		throw PatchException("'CVomit::ActivateAbility' patch offset incorrect (unexpected instruction).");
	}

	BYTE* pOperand = pInstr + VOMIT_DELAY_PATCH_OPCODE_SIZE;
	const float* pOriginalDelay;
	memcpy(&pOriginalDelay, instruction + VOMIT_DELAY_PATCH_OPCODE_SIZE, sizeof(pOriginalDelay));
	float originalDelay;
	if (!ReadModuleMemory(&originalDelay, pOriginalDelay, sizeof(originalDelay), lib))
	{
		throw PatchException("'CVomit::ActivateAbility' delay constant is not readable server module data.");
	}
	if (originalDelay != VOMIT_DELAY_VANILLA)
	{
		throw PatchException("'CVomit::ActivateAbility' patch offset incorrect (instruction does not read 0.25).");
	}

	g_flVomitDelay = delay;

	float* pDelay = &g_flVomitDelay;
	BYTE replacement[sizeof(pDelay)];
	memcpy(replacement, &pDelay, sizeof(replacement));
	m_patch = new BasicStaticBinPatch<sizeof(replacement)>(pOperand, replacement);
}

BoomerVomitDelayPatch::~BoomerVomitDelayPatch()
{
	delete m_patch;
}

void BoomerVomitDelayPatch::Patch()
{
	m_patch->Patch();
}

void BoomerVomitDelayPatch::Unpatch()
{
	m_patch->Unpatch();
}
#endif

#if SOURCE_ENGINE == SE_LEFT4DEAD2 && defined (_LINUX)
// Same stack layout as a Vector passed by value.
struct VomitVector
{
	float x, y, z;
};

typedef void (*OnVomitCollideFn)(void* attacker, void* victim, void* particle, int team, VomitVector velocity, VomitVector position);
typedef int (*GetTeamNumberFn)(const void* entity);
typedef void (*FinishLagCompensationFn)(void* manager, void* player);

// Object layout the game expects behind an interface pointer: just the vtable pointer.
struct VomitInterfaceProxy
{
	void** vtable;
};

#define VOMIT_PIERCE_MAX_HITS 32
#define ENGINETRACE_TRACERAY_INDEX 5			// call [edx+14h]
#define LAGCOMPENSATION_FINISH_INDEX 1		// call [edx+4]

// Survivors the stream passed through during the current CVomit::UpdateAbility call.
struct VomitPierceHit
{
	IHandleEntity* victim;
	VomitVector velocity;
	VomitVector position;
	int tick;
};

static VomitPierceHit s_PierceHits[VOMIT_PIERCE_MAX_HITS];
static int s_PierceHitCount = 0;

static IEngineTrace* const* s_ppEngineTrace = NULL;
static void* const* s_ppLagCompensation = NULL;
static OnVomitCollideFn s_OnVomitCollide = NULL;
static GetTeamNumberFn s_GetTeamNumber = NULL;
static const CGlobalVars* s_pPierceGlobals = NULL;

static void* s_TraceProxyVTable[ENGINETRACE_TRACERAY_INDEX + 1];
static VomitInterfaceProxy s_TraceProxy = { s_TraceProxyVTable };
static VomitInterfaceProxy* s_pTraceProxy = &s_TraceProxy; // read by the patched 'mov eax, enginetrace'

static void* s_FinishProxyVTable[LAGCOMPENSATION_FINISH_INDEX + 1];
static VomitInterfaceProxy s_FinishProxy = { s_FinishProxyVTable };
static VomitInterfaceProxy* s_pFinishProxy = &s_FinishProxy; // read by the patched 'mov eax, lagcompensation'

static bool IsPiercedVictim(const IHandleEntity* entity)
{
	for (int i = 0; i < s_PierceHitCount; i++)
	{
		if (s_PierceHits[i].victim == entity)
		{
			return true;
		}
	}
	return false;
}

// Players are the only entities the vomit filter lets through besides the world and props: it already
// skips the infected team and ghosts, and spectators / dead survivors are not solid.
static bool IsPlayerEntity(IHandleEntity* entity)
{
	if (entity == NULL)
	{
		return false;
	}
	int index = entity->GetRefEHandle().GetEntryIndex();
	return index >= 1 && index <= s_pPierceGlobals->maxClients;
}

// The game's filter, but survivors the stream already passed through are not hit again.
class VomitPierceFilter : public ITraceFilter
{
public:
	explicit VomitPierceFilter(ITraceFilter* inner) : m_pInner(inner) {}

	bool ShouldHitEntity(IHandleEntity* entity, int contentsMask)
	{
		return !IsPiercedVictim(entity) && m_pInner->ShouldHitEntity(entity, contentsMask);
	}

	TraceType_t GetTraceType() const
	{
		return m_pInner->GetTraceType();
	}

private:
	ITraceFilter* m_pInner;
};

// Replaces enginetrace->TraceRay() for one segment of the vomit stream. A survivor hit by the segment
// is remembered and the segment is traced again without them, so the game only sees what lies behind
// the survivors (world, prop or nothing) and keeps tracing the stream.
__attribute__((force_align_arg_pointer))
static void VomitTraceRay(void* /*proxy*/, const Ray_t* ray, unsigned int mask, ITraceFilter* filter, trace_t* trace)
{
	IEngineTrace* engineTrace = *s_ppEngineTrace;
	const int tick = s_pPierceGlobals->tickcount;
	if (s_PierceHitCount > 0 && s_PierceHits[0].tick != tick)
	{
		s_PierceHitCount = 0; // left over from an UpdateAbility call that never finished
	}

	VomitPierceFilter pierceFilter(filter);
	engineTrace->TraceRay(*ray, mask, &pierceFilter, trace);
	while (trace->fraction < 1.0f && s_PierceHitCount < VOMIT_PIERCE_MAX_HITS)
	{
		IHandleEntity* victim = reinterpret_cast<IHandleEntity*>(trace->m_pEnt);
		if (!IsPlayerEntity(victim))
		{
			break;
		}

		// Same arguments the game passes to OnVomitCollide: stream direction and segment end.
		VomitPierceHit& hit = s_PierceHits[s_PierceHitCount++];
		hit.victim = victim;
		hit.velocity.x = ray->m_Delta.x;
		hit.velocity.y = ray->m_Delta.y;
		hit.velocity.z = ray->m_Delta.z;
		hit.position.x = ray->m_Start.x + ray->m_Delta.x;
		hit.position.y = ray->m_Start.y + ray->m_Delta.y;
		hit.position.z = ray->m_Start.z + ray->m_Delta.z;
		hit.tick = tick;

		engineTrace->TraceRay(*ray, mask, &pierceFilter, trace);
	}
}

// Replaces lagcompensation->FinishLagCompensation(owner) at the end of the stream trace: vomits on every
// survivor the stream passed through (still lag compensated, like the game's own hit), then finishes.
__attribute__((force_align_arg_pointer))
static void VomitFinishLagCompensation(void* /*proxy*/, void* player)
{
	const int count = s_PierceHitCount;
	s_PierceHitCount = 0;
	if (count > 0 && player != NULL)
	{
		VomitPierceHit hits[VOMIT_PIERCE_MAX_HITS];
		memcpy(hits, s_PierceHits, count * sizeof(hits[0]));

		const int tick = s_pPierceGlobals->tickcount;
		const int team = s_GetTeamNumber(player);
		for (int i = 0; i < count; i++)
		{
			if (hits[i].tick == tick)
			{
				s_OnVomitCollide(player, hits[i].victim, NULL, team, hits[i].velocity, hits[i].position);
			}
		}
	}

	void* manager = *s_ppLagCompensation;
	FinishLagCompensationFn finish = reinterpret_cast<FinishLagCompensationFn>(
		(*reinterpret_cast<void***>(manager))[LAGCOMPENSATION_FINISH_INDEX]);
	finish(manager, player);
}

static void ExpectBytes(const BYTE* code, size_t offset, const char* bytes, size_t length, const char* error)
{
	if (memcmp(code + offset, bytes, length) != 0)
	{
		throw PatchException(error);
	}
}

// 'mov eax, [operand]' (A1 <abs32>) reading the given server global.
static void ExpectGlobalLoad(const BYTE* code, size_t offset, const void* global, const char* error)
{
	const void* operand;
	memcpy(&operand, code + offset + 1, sizeof(operand));
	if (code[offset] != 0xA1 || operand != global)
	{
		throw PatchException(error);
	}
}

static ICodePatch* CreateGlobalLoadPatch(BYTE* instruction, VomitInterfaceProxy** ppProxy)
{
	BYTE replacement[1 + sizeof(ppProxy)];
	replacement[0] = 0xA1;
	memcpy(replacement + 1, &ppProxy, sizeof(ppProxy));
	return new BasicStaticBinPatch<sizeof(replacement)>(instruction, replacement);
}

BoomerVomitPiercePatch::BoomerVomitPiercePatch(IServerGameDLL* gamedll, const CGlobalVars* globals)
	: m_tracePatch(NULL), m_finishPatch(NULL)
{
	if (globals == NULL)
	{
		throw PatchException("gpGlobals not available.");
	}

	BYTE* pUpdateAbility = (BYTE*)g_MemUtils.SimpleResolve(gamedll, SYM_CVOMIT_UPDATEABILITY);
	void* pOnVomitCollide = g_MemUtils.SimpleResolve(gamedll, SYM_ONVOMITCOLLIDE);
	void* pGetTeamNumber = g_MemUtils.SimpleResolve(gamedll, SYM_CBASEENTITY_GETTEAMNUMBER);
	void* ppEngineTrace = g_MemUtils.SimpleResolve(gamedll, SYM_ENGINETRACE);
	void* ppLagCompensation = g_MemUtils.SimpleResolve(gamedll, SYM_LAGCOMPENSATION);
	if (pUpdateAbility == NULL || pOnVomitCollide == NULL || pGetTeamNumber == NULL
		|| ppEngineTrace == NULL || ppLagCompensation == NULL)
	{
		throw PatchException("Couldn't find 'CVomit::UpdateAbility', 'OnVomitCollide', 'CBaseEntity::GetTeamNumber', 'enginetrace' or 'lagcompensation'.");
	}

	DynLibInfo lib = {};
	if (!g_MemUtils.GetLibraryInfo(gamedll, lib))
	{
		throw PatchException("Couldn't get server module information.");
	}
	if (!IsInModule(pUpdateAbility, VOMIT_PIERCE_CODE_SIZE, lib))
	{
		throw PatchException("'CVomit::UpdateAbility' pierce patch offsets outside server module.");
	}

	BYTE code[VOMIT_PIERCE_CODE_SIZE];
	if (!ReadModuleMemory(code, pUpdateAbility, sizeof(code), lib))
	{
		throw PatchException("'CVomit::UpdateAbility' is not readable.");
	}

	ExpectGlobalLoad(code, VOMIT_PIERCE_TRACE_LOAD_OFFSET, ppEngineTrace,
		"'CVomit::UpdateAbility' pierce patch offset incorrect (expected enginetrace load).");
	ExpectBytes(code, VOMIT_PIERCE_TRACE_VTABLE_OFFSET, "\x8B\x10", 2,
		"'CVomit::UpdateAbility' pierce patch offset incorrect (expected enginetrace vtable load).");
	ExpectBytes(code, VOMIT_PIERCE_TRACE_CALL_OFFSET, "\xFF\x52\x14", 3,
		"'CVomit::UpdateAbility' pierce patch offset incorrect (expected IEngineTrace::TraceRay call).");

	int32_t collideDisplacement;
	memcpy(&collideDisplacement, code + VOMIT_PIERCE_COLLIDE_CALL_OFFSET + 1, sizeof(collideDisplacement));
	uintptr_t collideTarget = reinterpret_cast<uintptr_t>(pUpdateAbility) + VOMIT_PIERCE_COLLIDE_CALL_OFFSET + 5
		+ static_cast<uintptr_t>(static_cast<intptr_t>(collideDisplacement));
	if (code[VOMIT_PIERCE_COLLIDE_CALL_OFFSET] != 0xE8 || collideTarget != reinterpret_cast<uintptr_t>(pOnVomitCollide))
	{
		throw PatchException("'CVomit::UpdateAbility' pierce patch offset incorrect (expected OnVomitCollide call).");
	}

	ExpectGlobalLoad(code, VOMIT_PIERCE_FINISH_LOAD_OFFSET, ppLagCompensation,
		"'CVomit::UpdateAbility' pierce patch offset incorrect (expected lagcompensation load).");
	ExpectBytes(code, VOMIT_PIERCE_FINISH_VTABLE_OFFSET, "\x8B\x10", 2,
		"'CVomit::UpdateAbility' pierce patch offset incorrect (expected lagcompensation vtable load).");
	ExpectBytes(code, VOMIT_PIERCE_FINISH_CALL_OFFSET, "\xFF\x52\x04", 3,
		"'CVomit::UpdateAbility' pierce patch offset incorrect (expected FinishLagCompensation call).");

	s_ppEngineTrace = static_cast<IEngineTrace* const*>(ppEngineTrace);
	s_ppLagCompensation = static_cast<void* const*>(ppLagCompensation);
	s_OnVomitCollide = reinterpret_cast<OnVomitCollideFn>(pOnVomitCollide);
	s_GetTeamNumber = reinterpret_cast<GetTeamNumberFn>(pGetTeamNumber);
	s_pPierceGlobals = globals;
	s_PierceHitCount = 0;
	s_TraceProxyVTable[ENGINETRACE_TRACERAY_INDEX] = reinterpret_cast<void*>(&VomitTraceRay);
	s_FinishProxyVTable[LAGCOMPENSATION_FINISH_INDEX] = reinterpret_cast<void*>(&VomitFinishLagCompensation);

	m_tracePatch = CreateGlobalLoadPatch(pUpdateAbility + VOMIT_PIERCE_TRACE_LOAD_OFFSET, &s_pTraceProxy);
	m_finishPatch = CreateGlobalLoadPatch(pUpdateAbility + VOMIT_PIERCE_FINISH_LOAD_OFFSET, &s_pFinishProxy);
}

BoomerVomitPiercePatch::~BoomerVomitPiercePatch()
{
	delete m_tracePatch;
	delete m_finishPatch;
}

void BoomerVomitPiercePatch::Patch()
{
	// Finish first: survivors collected by the trace proxy must always get their hit applied.
	m_finishPatch->Patch();
	m_tracePatch->Patch();
}

void BoomerVomitPiercePatch::Unpatch()
{
	m_tracePatch->Unpatch();
	m_finishPatch->Unpatch();
}
#endif

BYTE* BoomerVomitFrameTimePatch::FindCVomitUpdateAbility(void* gamedll)
{
#if defined (_LINUX)
	return (BYTE*)g_MemUtils.SimpleResolve(gamedll, SYM_CVOMIT_UPDATEABILITY);
#elif defined (_WIN32)
	return (BYTE*)g_MemUtils.FindLibPattern(gamedll, SIG_CVOMIT_UPDATEABILITY, SIG_CVOMIT_UPDATEABILITY_SIZE);
#endif
}
