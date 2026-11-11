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
#include <cmath>
#include <string.h>
#include "thirdparty/memutils.h"
#include "lagcompfix.h"
#include "modulememory.h"
#include "patchexceptions.h"

#define LAGCOMP_WINDOW_MAX_USERCMDS 127

int LagCompWindowUsercmds(float windowSeconds, float tickInterval)
{
	if (!std::isfinite(windowSeconds) || !std::isfinite(tickInterval) || windowSeconds <= 0.0f || tickInterval <= 0.0f)
	{
		return 5;
	}

	double usercmds = std::floor((double)windowSeconds / (double)tickInterval + 0.5);
	if (!(usercmds >= 5.0))
	{
		return 5;
	}
	if (usercmds > LAGCOMP_WINDOW_MAX_USERCMDS)
	{
		return LAGCOMP_WINDOW_MAX_USERCMDS;
	}
	return (int)usercmds;
}

bool LagCompWindowSecondsFromParam(float requested, float& seconds)
{
	if (!std::isfinite(requested) || requested < 0.01f || requested > 1.0f)
	{
		return false;
	}
	seconds = requested;
	return true;
}

#if SOURCE_ENGINE == SE_LEFT4DEAD2 && defined (_LINUX)
LagCompWindowPatch::LagCompWindowPatch(IServerGameDLL* gamedll, float windowSeconds, float tickInterval)
	: m_pImmediate(NULL), m_original(LAGCOMP_WINDOW_VANILLA_USERCMDS), m_windowSeconds(windowSeconds),
	m_tickInterval(tickInterval), m_usercmds(LagCompWindowUsercmds(windowSeconds, tickInterval)),
	m_bPatched(false), m_bFirstTime(true)
{
	BYTE* pFunc = (BYTE*)g_MemUtils.SimpleResolve(gamedll, SYM_CTERRORPLAYER_WANTSLAGCOMPENSATIONONENTITY);
	if (pFunc == NULL)
	{
		throw PatchException("Couldn't find 'CTerrorPlayer::WantsLagCompensationOnEntity'.");
	}

	DynLibInfo lib = {};
	if (!g_MemUtils.GetLibraryInfo(gamedll, lib))
	{
		throw PatchException("Couldn't get server module information.");
	}
	if (!IsInModule(pFunc, LAGCOMP_WINDOW_PATCH_OFFSET + LAGCOMP_WINDOW_PATCH_BYTES_SIZE, lib))
	{
		throw PatchException("'CTerrorPlayer::WantsLagCompensationOnEntity' patch offset outside server module.");
	}

	BYTE anchor[LAGCOMP_WINDOW_ANCHOR_BYTES_SIZE];
	BYTE instruction[LAGCOMP_WINDOW_PATCH_BYTES_SIZE];
	if (!ReadModuleMemory(anchor, pFunc + LAGCOMP_WINDOW_ANCHOR_OFFSET, sizeof(anchor), lib)
		|| !ReadModuleMemory(instruction, pFunc + LAGCOMP_WINDOW_PATCH_OFFSET, sizeof(instruction), lib))
	{
		throw PatchException("'CTerrorPlayer::WantsLagCompensationOnEntity' is not readable.");
	}
	if (memcmp(anchor, LAGCOMP_WINDOW_ANCHOR_BYTES, sizeof(anchor)) != 0)
	{
		throw PatchException("'CTerrorPlayer::WantsLagCompensationOnEntity' patch offset incorrect (unexpected usercmd checks).");
	}
	if (memcmp(instruction, LAGCOMP_WINDOW_PATCH_BYTES, sizeof(instruction)) != 0)
	{
		throw PatchException("'CTerrorPlayer::WantsLagCompensationOnEntity' patch offset incorrect (expected 'cmp ecx, 5').");
	}

	m_pImmediate = pFunc + LAGCOMP_WINDOW_IMMEDIATE_OFFSET;
	m_original = instruction[LAGCOMP_WINDOW_IMMEDIATE_OFFSET - LAGCOMP_WINDOW_PATCH_OFFSET];
}

LagCompWindowPatch::~LagCompWindowPatch()
{
	Unpatch();
}

void LagCompWindowPatch::Patch()
{
	if (m_bPatched)
	{
		return;
	}
	if (m_bFirstTime)
	{
		g_MemUtils.SetMemPatchable(m_pImmediate, 1);
		m_bFirstTime = false;
	}
	*m_pImmediate = (BYTE)m_usercmds;
	m_bPatched = true;
}

void LagCompWindowPatch::Unpatch()
{
	if (!m_bPatched)
	{
		return;
	}
	*m_pImmediate = m_original;
	m_bPatched = false;
}

void LagCompWindowPatch::SetTickInterval(float tickInterval)
{
	if (tickInterval == m_tickInterval)
	{
		return;
	}
	m_tickInterval = tickInterval;

	int usercmds = LagCompWindowUsercmds(m_windowSeconds, tickInterval);
	if (usercmds == m_usercmds)
	{
		return;
	}
	m_usercmds = usercmds;
	if (m_bPatched)
	{
		*m_pImmediate = (BYTE)usercmds;
	}
}
#endif
