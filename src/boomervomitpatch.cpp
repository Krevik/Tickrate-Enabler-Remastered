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
#include <sys/uio.h>
#include <unistd.h>
#endif
#include "thirdparty/memutils.h"
#include "boomervomitpatch.h"
#include "patchexceptions.h"

static bool IsInModule(const void* address, size_t length, const DynLibInfo& lib)
{
	uintptr_t startAddress = reinterpret_cast<uintptr_t>(address);
	uintptr_t baseAddress = reinterpret_cast<uintptr_t>(lib.baseAddress);
	return startAddress >= baseAddress && length <= lib.memorySize
		&& startAddress - baseAddress <= lib.memorySize - length;
}

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
	return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == (ssize_t)length;
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

BYTE* BoomerVomitFrameTimePatch::FindCVomitUpdateAbility(void* gamedll)
{
#if defined (_LINUX)
	return (BYTE*)g_MemUtils.SimpleResolve(gamedll, SYM_CVOMIT_UPDATEABILITY);
#elif defined (_WIN32)
	return (BYTE*)g_MemUtils.FindLibPattern(gamedll, SIG_CVOMIT_UPDATEABILITY, SIG_CVOMIT_UPDATEABILITY_SIZE);
#endif
}
