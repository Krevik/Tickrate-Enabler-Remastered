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
#ifndef __BOOMERVOMITPATCH_H__
#define __BOOMERVOMITPATCH_H__

#include "eiface.h"
#include "thirdparty/codepatch/icodepatch.h"
#include "thirdparty/codepatch/patchmanager.h"
#include "basicbinpatch.h"
#include "misc_asm.h"
#include "sigs.h"

class BoomerVomitFrameTimePatch : public ICodePatch
{
public:
	// realGlobals: the game's real gpGlobals, used to verify that patched instructions read it.
	BoomerVomitFrameTimePatch(IServerGameDLL* gamedll, const void* realGlobals);
	~BoomerVomitFrameTimePatch();
	void Patch();
	void Unpatch();
private:
	void InitializeBinPatches(IServerGameDLL* gamedll, const void* realGlobals);
	BYTE* FindCVomitUpdateAbility(void* gamedll);
	PatchManager m_patches;
};

#if SOURCE_ENGINE == SE_LEFT4DEAD2
/*
 * CVomit::ActivateAbility starts the vomit effect, sound and duration right away, but schedules the
 * first hit traces (m_nextSpray) only 0.25 s later, so survivors get covered noticeably after the
 * boomer visibly starts vomiting. The patch replaces that hard-coded 0.25 s with 'delay'.
 */
class BoomerVomitDelayPatch : public ICodePatch
{
public:
	BoomerVomitDelayPatch(IServerGameDLL* gamedll, float delay);
	~BoomerVomitDelayPatch();
	void Patch();
	void Unpatch();
private:
	ICodePatch* m_patch;
};
#endif

#if SOURCE_ENGINE == SE_LEFT4DEAD2 && defined (_LINUX)
class CGlobalVars;

/*
 * CVomit::UpdateAbility traces the human boomer's vomit stream every tick and stops at the first entity
 * it hits, so the survivor in front absorbs the whole stream and the survivors behind them stay clean.
 * With this patch the stream passes through survivors: every survivor along it is vomited on (exactly
 * like the one the stream would have stopped at), and only the world / props stop it.
 * Bot boomers spray physical CVomitParticle entities instead and are not affected.
 */
class BoomerVomitPiercePatch : public ICodePatch
{
public:
	// globals: the game's gpGlobals (tick count and max clients).
	BoomerVomitPiercePatch(IServerGameDLL* gamedll, const CGlobalVars* globals);
	~BoomerVomitPiercePatch();
	void Patch();
	void Unpatch();
private:
	ICodePatch* m_tracePatch;
	ICodePatch* m_finishPatch;
};
#endif

#endif // __BOOMERVOMITPATCH_H__
