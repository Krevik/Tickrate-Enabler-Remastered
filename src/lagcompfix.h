/**
 * vim: set ts=4 :
 * =============================================================================
 * Lag compensation window fix (L4D2, Linux)
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
#ifndef __LAGCOMPFIX_H__
#define __LAGCOMPFIX_H__

#include "eiface.h"
#include "thirdparty/codepatch/icodepatch.h"
#include "sigs.h"

// Time the vanilla window covers at the tickrate the game was tuned for (5 usercmds at 30 tick).
#define LAGCOMP_WINDOW_VANILLA_SECONDS (5.0f / 30.0f)

// Window in usercmds that covers 'windowSeconds' at the given tick interval: never shorter than the
// vanilla 5 usercmds and at most 127 (the patched instruction holds a signed byte).
int LagCompWindowUsercmds(float windowSeconds, float tickInterval);

// Validates the -lagcompwindow value: finite seconds in [0.01, 1]. Returns false if unusable.
bool LagCompWindowSecondsFromParam(float requested, float& seconds);

#if SOURCE_ENGINE == SE_LEFT4DEAD2 && defined (_LINUX)
/*
 * CTerrorPlayer::WantsLagCompensationOnEntity keeps rewinding players and common infected for only 5
 * usercmds after the last weapon fire / shove swing start. A usercmd is one tick, so at 100 tick a
 * shove (traced for 0.2 s) hits un-rewound targets after its first 50 ms (166 ms at 30 tick). The
 * patch rewrites the 5 so the window lasts 'windowSeconds' at the current tickrate. Opt-in only
 * (-lagcompwindow <seconds>).
 */
class LagCompWindowPatch : public ICodePatch
{
public:
	LagCompWindowPatch(IServerGameDLL* gamedll, float windowSeconds, float tickInterval);
	~LagCompWindowPatch();
	void Patch();
	void Unpatch();

	// Recompute the window for a new tick interval; rewrites the instruction only when it changes.
	void SetTickInterval(float tickInterval);
	int GetWindowUsercmds() const { return m_usercmds; }

private:
	BYTE* m_pImmediate;
	BYTE m_original;
	float m_windowSeconds;
	float m_tickInterval;
	int m_usercmds;
	bool m_bPatched;
	bool m_bFirstTime;
};
#endif

#endif // __LAGCOMPFIX_H__
