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
#include <cmath>
#include <stdint.h>
#include <string.h>
#include "thirdparty/memutils.h"
#include "updateratefix.h"
#include "modulememory.h"
#include "patchexceptions.h"
#include "sigs.h"

int UpdateRateCapForTickInterval(float tickInterval)
{
	if (!std::isfinite(tickInterval) || tickInterval <= 0.0f)
	{
		return UPDATERATE_VANILLA_CAP;
	}

	// Small tolerance so 1 / (1 / 108.0f) doesn't round up to 109.
	double rate = std::ceil(1.0 / (double)tickInterval - 1.0e-3);
	if (!(rate > UPDATERATE_VANILLA_CAP))
	{
		return UPDATERATE_VANILLA_CAP;
	}
	if (rate > UPDATERATE_MAX_CAP)
	{
		return UPDATERATE_MAX_CAP;
	}
	return (int)rate;
}

#if SOURCE_ENGINE == SE_LEFT4DEAD2 && defined (_LINUX)
static void ExpectCode(const BYTE* code, size_t offset, const char* bytes, size_t length, const char* error)
{
	if (memcmp(code + offset, bytes, length) != 0)
	{
		throw PatchException(error);
	}
}

UpdateRateCapPatch::UpdateRateCapPatch(void* engineModule, int cap) : m_cap(cap)
{
	if (cap <= UPDATERATE_VANILLA_CAP || cap > UPDATERATE_MAX_CAP)
	{
		throw PatchException("Update rate cap out of range.");
	}

	BYTE* pBaseClient = (BYTE*)g_MemUtils.SimpleResolve(engineModule, SYM_CBASECLIENT_SETUPDATERATE);
	BYTE* pGameClient = (BYTE*)g_MemUtils.SimpleResolve(engineModule, SYM_CGAMECLIENT_SETUPDATERATE);
	if (pBaseClient == NULL || pGameClient == NULL)
	{
		throw PatchException("Couldn't find 'CBaseClient::SetUpdateRate' / 'CGameClient::SetUpdateRate'.");
	}

	DynLibInfo lib = {};
	if (!g_MemUtils.GetLibraryInfo(engineModule, lib))
	{
		throw PatchException("Couldn't get engine module information.");
	}
	if (!IsInModule(pBaseClient, UPDATERATE_BASECLIENT_CODE_SIZE, lib) || !IsInModule(pGameClient, UPDATERATE_GAMECLIENT_CODE_SIZE, lib))
	{
		throw PatchException("Update rate patch offsets outside engine module.");
	}

	BYTE baseCode[UPDATERATE_BASECLIENT_CODE_SIZE];
	BYTE gameCode[UPDATERATE_GAMECLIENT_CODE_SIZE];
	if (!ReadModuleMemory(baseCode, pBaseClient, sizeof(baseCode), lib)
		|| !ReadModuleMemory(gameCode, pGameClient, sizeof(gameCode), lib))
	{
		throw PatchException("'SetUpdateRate' is not readable.");
	}

	ExpectCode(baseCode, UPDATERATE_BASECLIENT_CLAMP_OFFSET, UPDATERATE_BASECLIENT_CLAMP_BYTES, UPDATERATE_BASECLIENT_CLAMP_BYTES_SIZE,
		"'CBaseClient::SetUpdateRate' patch offset incorrect (expected the 1..100 clamp and snapshot interval store).");
	ExpectCode(gameCode, UPDATERATE_GAMECLIENT_CLAMP_OFFSET, UPDATERATE_GAMECLIENT_CLAMP_BYTES, UPDATERATE_GAMECLIENT_CLAMP_BYTES_SIZE,
		"'CGameClient::SetUpdateRate' patch offset incorrect (expected the clamp to 100).");

	int32_t displacement;
	memcpy(&displacement, gameCode + UPDATERATE_GAMECLIENT_JMP_OFFSET + 1, sizeof(displacement));
	uintptr_t target = reinterpret_cast<uintptr_t>(pGameClient) + UPDATERATE_GAMECLIENT_JMP_OFFSET + 5
		+ static_cast<uintptr_t>(static_cast<intptr_t>(displacement));
	if (gameCode[UPDATERATE_GAMECLIENT_JMP_OFFSET] != 0xE9 || target != reinterpret_cast<uintptr_t>(pBaseClient))
	{
		throw PatchException("'CGameClient::SetUpdateRate' patch offset incorrect (expected the jump to CBaseClient::SetUpdateRate).");
	}

	int32_t cap32 = cap;

	// CBaseClient:  mov edx, 100 / cmp eax, 100 / cmovle edx, eax    (BA 64000000 83F864 0F4ED0)
	//          ->   mov edx, cap / cmp eax, edx / cmovle edx, eax / nop (BA <cap>  39D0   0F4ED0 90)
	BYTE baseReplacement[UPDATERATE_BASECLIENT_PATCH_SIZE] = { 0xBA, 0, 0, 0, 0, 0x39, 0xD0, 0x0F, 0x4E, 0xD0, 0x90 };
	memcpy(baseReplacement + 1, &cap32, sizeof(cap32));

	// CGameClient:  cmp edx, 100 / mov eax, 100 / jg +7          (83FA64 B864000000 7F07)
	//          ->   mov eax, cap / cmp edx, eax / jg +8 / nop    (B8<cap> 39C2      7F08 90)
	// Both jump to the same instruction; the fall-through path doesn't use eax.
	BYTE gameReplacement[UPDATERATE_GAMECLIENT_PATCH_SIZE] = { 0xB8, 0, 0, 0, 0, 0x39, 0xC2, 0x7F, 0x08, 0x90 };
	memcpy(gameReplacement + 1, &cap32, sizeof(cap32));

	m_patches.Register(new BasicStaticBinPatch<sizeof(baseReplacement)>(pBaseClient + UPDATERATE_BASECLIENT_CLAMP_OFFSET, baseReplacement));
	m_patches.Register(new BasicStaticBinPatch<sizeof(gameReplacement)>(pGameClient + UPDATERATE_GAMECLIENT_CLAMP_OFFSET, gameReplacement));
}

UpdateRateCapPatch::~UpdateRateCapPatch()
{
	m_patches.UnregisterAll();
}

void UpdateRateCapPatch::Patch()
{
	m_patches.PatchAll();
}

void UpdateRateCapPatch::Unpatch()
{
	m_patches.UnpatchAll();
}
#endif
