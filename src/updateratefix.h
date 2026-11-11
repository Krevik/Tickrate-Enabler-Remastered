/**
 * vim: set ts=4 :
 * =============================================================================
 * Snapshot (update) rate cap fix
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
#ifndef __UPDATERATEFIX_H__
#define __UPDATERATEFIX_H__

#include <basetypes.h>
#include "thirdparty/codepatch/icodepatch.h"
#include "thirdparty/codepatch/patchmanager.h"
#include "basicbinpatch.h"

#define UPDATERATE_VANILLA_CAP 100
#define UPDATERATE_MAX_CAP 1000

// Snapshots per second needed to send one snapshot every tick: the tickrate rounded up, never below
// the engine's 100 and at most 1000.
int UpdateRateCapForTickInterval(float tickInterval);

#if SOURCE_ENGINE == SE_LEFT4DEAD2 && defined (_LINUX)
/*
 * The engine clamps every client's update rate to 100 snapshots per second, in
 * CGameClient::SetUpdateRate (after sv_min/maxupdaterate) and again in CBaseClient::SetUpdateRate.
 * Above 100 tick the snapshot interval (1/100 s) is then longer than a tick, so snapshots go out on
 * an uneven tick pattern instead of every tick. The patch rewrites both clamps (same length, a full
 * 32-bit limit) to 'cap'.
 */
class UpdateRateCapPatch : public ICodePatch
{
public:
	// engineModule: any address inside engine(_srv).so.
	UpdateRateCapPatch(void* engineModule, int cap);
	~UpdateRateCapPatch();
	void Patch();
	void Unpatch();
	int GetCap() const { return m_cap; }

private:
	PatchManager m_patches;
	int m_cap;
};
#endif

#endif // __UPDATERATEFIX_H__
