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
#ifndef __HITBOXLAGCOMPFIX_H__
#define __HITBOXLAGCOMPFIX_H__

#include "eiface.h"
#include "thirdparty/codepatch/icodepatch.h"
#include "thirdparty/codepatch/patchmanager.h"

#if SOURCE_ENGINE == SE_LEFT4DEAD2 && defined (_LINUX)
/*
 * Hitboxes are built from an entity's bones, and the bones are cached for up to 0.1 s. Lag compensation
 * moves an entity back to where the attacker saw it, but only throws the cached bones away when it also
 * rewound the animation, which never happens for bullets (see BulletHitboxLagCompPatch) or for shoves,
 * melee, claws and vomit. A shot is then tested against bones cached for another position: the entity's
 * real one, or the one an earlier shooter rewound it to. When the entity is put back, the bones of the
 * rewound position stay cached as well.
 *
 * Two changes:
 *   - CLagCompensationManager::BacktrackEntity flushes the bone cache whenever it changed the entity's
 *     origin, angles or bounds, not only its animation;
 *   - FinishLagCompensation's call to RestoreEntityFromRecords is redirected, and the bone cache is
 *     flushed again after the entity is restored.
 * sv_lagflushbonecache 0 still turns the first flush off, as in the game's own code.
 */
class LagCompBoneCachePatch : public ICodePatch
{
public:
	LagCompBoneCachePatch(IServerGameDLL* gamedll);
	~LagCompBoneCachePatch();
	void Patch();
	void Unpatch();

private:
	PatchManager m_patches;
};

/*
 * FireTerrorBullets starts lag compensation with LAG_COMPENSATE_HITBOXES_ALONG_RAY, which rewinds the
 * animation (sequence, cycle, layers) only of entities within 100 units of the weapon's ray. It leaves
 * the ray at its default: position (0,0,0), angles (0,0,0), range 0. Bullets are therefore tested against
 * hitboxes posed by the target's current animation, moved to its old position.
 *
 * The patch asks for LAG_COMPENSATE_HITBOXES instead: every entity rewound for the shot gets its
 * animation rewound too, as in other Source games.
 */
class BulletHitboxLagCompPatch : public ICodePatch
{
public:
	BulletHitboxLagCompPatch(IServerGameDLL* gamedll);
	~BulletHitboxLagCompPatch();
	void Patch();
	void Unpatch();

private:
	ICodePatch* m_patch;
};
#endif

#endif // __HITBOXLAGCOMPFIX_H__
