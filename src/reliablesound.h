/**
 * vim: set ts=4 :
 * =============================================================================
 * Reliable sounds (te_reliable_sound)
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
#ifndef __RELIABLESOUND_H__
#define __RELIABLESOUND_H__

#include <stddef.h>
#include "irecipientfilter.h"

class CCommand;
class IEngineSound;

#define RELIABLE_SOUND_MAX_RECIPIENTS 64

/*
 * Sounds emitted by the game and by SourceMod are unreliable: they ride in the next snapshot only, so a
 * lost packet loses the sound for good. A filter that reports IsReliable() makes the engine send each
 * recipient the sound right away on the reliable stream (CGameServer::BroadcastSound ->
 * CGameClient::SendSound(sound, true)), which is retransmitted until it arrives.
 *
 * The sound goes through IEngineSound::EmitSound like any other, so SourceMod normal sound hooks see
 * it too. If one of them returns Plugin_Changed, SourceMod re-emits the sound with its own (unreliable)
 * filter. Don't run the command from inside a SourceMod sound hook; defer it to the next frame.
 */
class ReliableRecipientFilter : public IRecipientFilter
{
public:
	ReliableRecipientFilter() : m_count(0) {}

	bool IsReliable() const { return true; }
	bool IsInitMessage() const { return false; }
	int GetRecipientCount() const { return m_count; }
	int GetRecipientIndex(int slot) const { return slot >= 0 && slot < m_count ? m_clients[slot] : -1; }

	// Adds a client once; returns false if the list is full.
	bool Add(int client);

private:
	int m_clients[RELIABLE_SOUND_MAX_RECIPIENTS];
	int m_count;
};

/*
 * te_reliable_sound <entity> <channel> <soundlevel> <volume> <pitch> <flags> <origin> "<sample>" [clients]
 *   entity   sound source: 0 = world, or an entity index
 *   origin   "x,y,z", or "-" to use the entity's own position (entity must not be the world)
 *   sample   must already be precached; quote it (samples often start with ')', which splits tokens)
 *   clients  optional comma separated client indices; default: every recipient client
 * Same meaning as the arguments of EmitSound / a SourceMod normal sound hook, so a hooked sound can be
 * re-emitted unchanged, only reliably. Clients that aren't recipients are skipped.
 *
 * isSoundRecipient(i) says whether client index i (1..maxClients) should get sounds: connected humans
 * and SourceTV, not bots or free slots.
 * Returns false and fills 'error' if the arguments are invalid; nothing is emitted then.
 */
bool EmitReliableSoundCommand(const CCommand& args, IEngineSound* engineSound, int maxClients,
	bool (*isSoundRecipient)(int client), char* error, size_t errorLength);

#endif // __RELIABLESOUND_H__
