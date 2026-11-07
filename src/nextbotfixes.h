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
 *
 * Fixes for common infected / witch behaviour that depends on how often NextBots
 * are updated (nb_update_frequency), which is commonly lowered on high tickrate servers.
 */
#ifndef __NEXTBOTFIXES_H__
#define __NEXTBOTFIXES_H__

#include "eiface.h"
#include "thirdparty/codepatch/icodepatch.h"
#include "basicbinpatch.h"
#include "misc_asm.h"
#include "sigs.h"

class ConVar;

/*
 * ZombieBotLocomotion::ResolveZombieCollisions pushes overlapping infected apart by a fixed
 * 3 units on every NextBot update. The value was tuned for the default 0.1s update interval;
 * with nb_update_frequency lowered (e.g. to one tick) the push becomes 5-10x stronger per second,
 * causing jitter, commons wedging themselves into corners/walls and the witch being shoved off
 * her path when she runs through a horde.
 *
 * The patch redirects the constant to a variable that is rescaled every frame so the push per
 * second matches the vanilla 0.1s update interval (it is never made stronger than vanilla).
 */
class ZombieCollisionPushPatch : public ICodePatch
{
public:
	ZombieCollisionPushPatch(void* serverModule);
	~ZombieCollisionPushPatch();
	void Patch();
	void Unpatch();

private:
	ICodePatch* m_patch;
};

/*
 * WitchAngry::OnContact raises the witch's rage on every contact event with a survivor by
 * z_witch_hostile_at_me_anger * z_witch_anger_rate * gpGlobals->frametime. Contact events are
 * generated once per NextBot update, not per frame, so the rage gained per second depends on both
 * the tickrate (3x slower at 100 tick with the default nb_update_frequency) and nb_update_frequency
 * (3x faster than vanilla at 100 tick with nb_update_frequency at one tick).
 *
 * The patch makes the function read a "frametime" that is rescaled every frame so the rage gained
 * per second of contact always matches vanilla 30 tick with the default nb_update_frequency 0.1.
 */
class WitchContactAngerPatch : public ICodePatch
{
public:
	// realGlobals: the game's gpGlobals, used to verify the patched instruction.
	WitchContactAngerPatch(void* serverModule, const void* realGlobals);
	~WitchContactAngerPatch();
	void Patch();
	void Unpatch();

private:
	ICodePatch* m_patch;
};

/*
 * Optional gameplay tweak (not a bug fix): the witch climbs ledges with slow, animation driven
 * climb sequences, during which she is an easy target and, with frequent NextBot updates, may get
 * hung up on the ledge. This replaces the ClimbUpToLedge entry of the WitchLocomotion vtable (only
 * witches use it, commons are not affected) with a wrapper that, once a climb has started, raises
 * the witch's m_flPlaybackRate. The climb movement is taken from the animation, so it speeds up
 * together with it. The rate is reset by the game when the next activity starts.
 */
class WitchClimbSpeedPatch : public ICodePatch
{
public:
	// playbackRateOffset: see FindWitchPlaybackRateOffset.
	// pClimbRate: playback rate multiplier while climbing (<= 1 disables the tweak).
	WitchClimbSpeedPatch(void* serverModule, int playbackRateOffset, const ConVar* pClimbRate);
	~WitchClimbSpeedPatch();
	void Patch();
	void Unpatch();

private:
	ICodePatch* m_patch;
};

// Offset of m_flPlaybackRate in the witch entity (from the network tables), -1 if not found.
int FindWitchPlaybackRateOffset(IServerGameDLL* gamedll);

// Recalculate the values used by the patches above from the current nb_update_frequency and
// tick interval. Call once per frame.
void UpdateNextBotFixes(const ConVar* pNbUpdateFrequency, float tickInterval);

/*
 * WitchAttack::OnContact allows the witch to switch her victim to another player she keeps
 * bumping into (z_witch_allow_change_victim). It validates the *current* victim with
 * WitchAttack::IsValidEnemy instead of the new one, so the witch can switch to an infected
 * player (charger, jockey, ...) touching her. When that victim dies the attack action ends
 * ("Killed Infected that was harrassing me") and the witch returns to her idle/angry state and
 * sits down again. Frequent NextBot updates generate many more contact events, which makes this
 * a lot more common.
 *
 * The patch makes IsValidEnemy check the new victim (alive survivor) instead.
 */
class WitchContactVictimPatch : public ICodePatch
{
public:
	WitchContactVictimPatch(void* serverModule);
	~WitchContactVictimPatch();
	void Patch();
	void Unpatch();

private:
	ICodePatch* m_patch;
};

#endif // __NEXTBOTFIXES_H__
