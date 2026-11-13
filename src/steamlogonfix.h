/**
 * vim: set ts=4 :
 * =============================================================================
 * "No Steam logon" kick fix
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
#ifndef __STEAMLOGONFIX_H__
#define __STEAMLOGONFIX_H__

#include <basetypes.h>
#include "thirdparty/codepatch/icodepatch.h"

// Steam auth responses (EAuthSessionResponse) the engine answers with a "No Steam logon" kick:
// 1 UserNotConnectedToSteam, 6 AuthTicketCanceled, 7 AuthTicketInvalidAlreadyUsed, 8 AuthTicketInvalid.
bool IsNoSteamLogonResponse(int response);

// Name of an EAuthSessionResponse value ("Unknown" outside 0-9).
const char* SteamAuthResponseName(int response);

#if SOURCE_ENGINE == SE_LEFT4DEAD2 && defined (_LINUX)
/*
 * Called for every failed Steam auth response, before the client is kicked or kept.
 *   name, networkId  the client's name and Steam ID string (the ID is the one from the client's ticket)
 *   validated        Steam had confirmed this client's ticket earlier in the connection
 *   kept             the kick was skipped (a "No Steam logon" response); false: the engine kicks as usual
 */
typedef void (*SteamAuthFailureLogFn)(const char* name, const char* networkId, int response, bool validated, bool kept);

/*
 * Steam answers every client's auth ticket through CSteam3Server::OnValidateAuthTicketResponse. A failed
 * answer is handed to OnValidateAuthTicketResponseHelper, which kicks the client: "No Steam logon" for
 * responses 1, 6, 7 and 8. Players who are logged in to Steam get such answers too, e.g. after the game
 * had to repeat a connection attempt from a lobby (reported in ValveSoftware/Source-1-Games#7758).
 *
 * The patch redirects that one call: "No Steam logon" responses are only logged and the client stays;
 * every other failure (no license, VAC ban, logged in elsewhere, VAC timeout) still reaches the engine's
 * kick. A kept client that Steam never validated stays unvalidated (IsClientFullyAuthenticated is false),
 * so SourceMod doesn't authorize it or give it admin rights.
 */
class SteamLogonKickPatch : public ICodePatch
{
public:
	// engineModule: any address inside engine(_srv).so. log may be NULL.
	SteamLogonKickPatch(void* engineModule, SteamAuthFailureLogFn log);
	~SteamLogonKickPatch();
	void Patch();
	void Unpatch();

private:
	ICodePatch* m_patch;
};
#endif

#endif // __STEAMLOGONFIX_H__
