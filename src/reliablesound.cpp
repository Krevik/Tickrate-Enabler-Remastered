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
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "tier1/convar.h"
#include "engine/IEngineSound.h"
#include "mathlib/vector.h"
#include "reliablesound.h"

#define RELIABLE_SOUND_MAX_ENTITY 2047	// MAX_EDICTS - 1
#define RELIABLE_SOUND_MAX_CHANNEL 7	// CHAN_AUTO .. CHAN_VOICE2

bool ReliableRecipientFilter::Add(int client)
{
	for (int i = 0; i < m_count; i++)
	{
		if (m_clients[i] == client)
		{
			return true;
		}
	}
	if (m_count >= RELIABLE_SOUND_MAX_RECIPIENTS)
	{
		return false;
	}
	m_clients[m_count++] = client;
	return true;
}

// Whole-token integer in [minValue, maxValue].
static bool ParseInt(const char* text, long minValue, long maxValue, long& value)
{
	if (text == NULL || *text == '\0')
	{
		return false;
	}
	char* end = NULL;
	errno = 0;
	value = strtol(text, &end, 10);
	return errno != ERANGE && end != NULL && *end == '\0' && value >= minValue && value <= maxValue;
}

// Printable sample path: no control characters, and not starting with a space or a non-ASCII byte.
static bool IsValidSample(const char* sample)
{
	size_t length = strlen(sample);
	if (length == 0 || length >= 256)
	{
		return false;
	}
	unsigned char first = (unsigned char)sample[0];
	if (first <= 0x20 || first >= 0x80)
	{
		return false;
	}
	for (size_t i = 0; i < length; i++)
	{
		unsigned char c = (unsigned char)sample[i];
		if (c < 0x20 || c == 0x7F)
		{
			return false;
		}
	}
	return true;
}

// Whole-token finite float in [minValue, maxValue].
static bool ParseFloat(const char* text, float minValue, float maxValue, float& value)
{
	if (text == NULL || *text == '\0')
	{
		return false;
	}
	char* end = NULL;
	double parsed = strtod(text, &end);
	if (end == NULL || *end != '\0' || !std::isfinite(parsed) || parsed < minValue || parsed > maxValue)
	{
		return false;
	}
	value = (float)parsed;
	return true;
}

// "x,y,z"
static bool ParseOrigin(const char* text, Vector& origin)
{
	char buffer[128];
	size_t length = strlen(text);
	if (length == 0 || length >= sizeof(buffer))
	{
		return false;
	}
	memcpy(buffer, text, length + 1);

	float coords[3];
	char* cursor = buffer;
	for (int i = 0; i < 3; i++)
	{
		char* separator = strchr(cursor, ',');
		if ((i < 2) != (separator != NULL))
		{
			return false;
		}
		if (separator != NULL)
		{
			*separator = '\0';
		}
		if (!ParseFloat(cursor, -1.0e6f, 1.0e6f, coords[i]))
		{
			return false;
		}
		cursor = separator != NULL ? separator + 1 : NULL;
	}
	origin.Init(coords[0], coords[1], coords[2]);
	return true;
}

static void SetError(char* error, size_t errorLength, const char* message)
{
	if (error != NULL && errorLength > 0)
	{
		snprintf(error, errorLength, "%s", message);
	}
}

bool EmitReliableSoundCommand(const CCommand& args, IEngineSound* engineSound, int maxClients,
	bool (*isSoundRecipient)(int client), char* error, size_t errorLength)
{
	if (args.ArgC() != 9 && args.ArgC() != 10)
	{
		SetError(error, errorLength, "usage: te_reliable_sound <entity> <channel> <soundlevel> <volume> <pitch> <flags> <x,y,z|-> \"<sample>\" [client,client,...]");
		return false;
	}

	long entity, channel, soundLevel, pitch, flags;
	float volume;
	if (!ParseInt(args.Arg(1), 0, RELIABLE_SOUND_MAX_ENTITY, entity))
	{
		SetError(error, errorLength, "invalid entity (0 = world, up to 2047)");
		return false;
	}
	if (!ParseInt(args.Arg(2), 0, RELIABLE_SOUND_MAX_CHANNEL, channel))
	{
		SetError(error, errorLength, "invalid channel (0-7)");
		return false;
	}
	if (!ParseInt(args.Arg(3), 0, 255, soundLevel))
	{
		SetError(error, errorLength, "invalid sound level (0-255)");
		return false;
	}
	if (!ParseFloat(args.Arg(4), 0.0f, 1.0f, volume))
	{
		SetError(error, errorLength, "invalid volume (0-1)");
		return false;
	}
	if (!ParseInt(args.Arg(5), 1, 255, pitch))
	{
		SetError(error, errorLength, "invalid pitch (1-255)");
		return false;
	}
	if (!ParseInt(args.Arg(6), 0, 0x7FFFFFFF, flags))
	{
		SetError(error, errorLength, "invalid flags");
		return false;
	}

	Vector origin;
	const Vector* pOrigin = NULL;
	if (strcmp(args.Arg(7), "-") == 0)
	{
		if (entity == 0)
		{
			SetError(error, errorLength, "the world has no position of its own, give an origin");
			return false;
		}
	}
	else if (ParseOrigin(args.Arg(7), origin))
	{
		pOrigin = &origin;
	}
	else
	{
		SetError(error, errorLength, "invalid origin (x,y,z or -)");
		return false;
	}

	const char* sample = args.Arg(8);
	if (!IsValidSample(sample))
	{
		SetError(error, errorLength, "invalid sample (printable path, quote it)");
		return false;
	}
	// Precaching during the game can Host_Error the server (full table, bad name); callers precache.
	if (engineSound != NULL && !engineSound->IsSoundPrecached(sample))
	{
		SetError(error, errorLength, "sample is not precached (precache it on map start)");
		return false;
	}

	ReliableRecipientFilter filter;
	if (args.ArgC() == 10)
	{
		char list[512];
		size_t listLength = strlen(args.Arg(9));
		if (listLength == 0 || listLength >= sizeof(list))
		{
			SetError(error, errorLength, "invalid client list");
			return false;
		}
		memcpy(list, args.Arg(9), listLength + 1);
		for (char* token = list; token != NULL;)
		{
			char* separator = strchr(token, ',');
			if (separator != NULL)
			{
				*separator = '\0';
			}
			long client;
			if (!ParseInt(token, 1, maxClients, client))
			{
				SetError(error, errorLength, "invalid client list (client indices 1-maxplayers)");
				return false;
			}
			if (isSoundRecipient((int)client))
			{
				filter.Add((int)client);
			}
			token = separator != NULL ? separator + 1 : NULL;
		}
	}
	else
	{
		for (int client = 1; client <= maxClients; client++)
		{
			if (isSoundRecipient(client))
			{
				filter.Add(client);
			}
		}
	}

	if (engineSound == NULL || filter.GetRecipientCount() == 0)
	{
		return true;
	}

	engineSound->EmitSound(filter, (int)entity, (int)channel, sample, volume, (soundlevel_t)soundLevel,
		(int)flags, (int)pitch, pOrigin, NULL, NULL, true, 0.0f, -1);
	return true;
}
