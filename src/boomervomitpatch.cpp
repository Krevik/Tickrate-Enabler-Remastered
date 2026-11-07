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
#include "thirdparty/memutils.h"
#include "boomervomitpatch.h"
#include "patchexceptions.h"

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

	for (size_t i = 0; i < NUM_FRAMETIME_READS; i++)
	{
		DevMsg("Setting up patch for frametime read %d (offs:0x%x).\n", i, g_FrameTimeReadOffsets[i]);

		// Calculate first offset target
		BYTE* pTarget = pCVomitUpdateAbility + g_FrameTimeReadOffsets[i];

		int offs = mov_src_operand_offset(pTarget); // Find offset of disp32 in this particular mov instruction
		if (offs == 0 || !mov_is_patchable_disp32(pTarget))
		{
			// Unexpected instruction (or a form whose length would change after rewriting)
			throw PatchException("'CVomit::UpdateAbility' patch offset incorrect (unexpected instruction).");
		}

		// For absolute 'mov reg, [addr]' we can verify that 'addr' really is the game's gpGlobals
		// variable. This catches stale offsets after game updates that would otherwise silently
		// redirect some unrelated global. (Register-relative PIC forms on Linux can't be checked here.)
		if (mov_is_absolute_disp32(pTarget))
		{
			void* pGlobalsAddr = *(void**)(pTarget + offs);

			if (realGlobals != NULL && *(const void**)pGlobalsAddr != realGlobals)
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

		memcpy(instr_buf, pTarget, MAX_MOV_INSTR_LEN);

		// make this instruction read from an immediate address
		mov_to_disp32(instr_buf);

		// Plug in our super cool immediate address.
#if defined (_WIN32)
		*(fakeGlobals***)(instr_buf + offs) = &gp_FakeGlobals;
#elif defined (_LINUX)
	#if SOURCE_ENGINE == SE_LEFT4DEAD
			*(fakeGlobals****)(instr_buf + offs) = &gpp_FakeGlobals;
	#elif SOURCE_ENGINE == SE_LEFT4DEAD2
			*(fakeGlobals***)(instr_buf + offs) = &gp_FakeGlobals;
	#endif
#endif

		// Generate BasicBinPatch
		m_patches.Register(new BasicStaticBinPatch<MAX_MOV_INSTR_LEN>(pTarget, instr_buf));
	}
}

BYTE* BoomerVomitFrameTimePatch::FindCVomitUpdateAbility(void* gamedll)
{
#if defined (_LINUX)
	return (BYTE*)g_MemUtils.SimpleResolve(gamedll, SYM_CVOMIT_UPDATEABILITY);
#elif defined (_WIN32)
	return (BYTE*)g_MemUtils.FindLibPattern(gamedll, SIG_CVOMIT_UPDATEABILITY, SIG_CVOMIT_UPDATEABILITY_SIZE);
#endif
}
