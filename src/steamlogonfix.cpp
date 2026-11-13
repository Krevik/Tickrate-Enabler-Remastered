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
#include <stdint.h>
#include <string.h>
#include "thirdparty/memutils.h"
#include "steamlogonfix.h"
#include "basicbinpatch.h"
#include "modulememory.h"
#include "patchexceptions.h"
#include "sigs.h"

bool IsNoSteamLogonResponse(int response)
{
	return response == 1 || response == 6 || response == 7 || response == 8;
}

const char* SteamAuthResponseName(int response)
{
	static const char* const names[] = {
		"OK", "UserNotConnectedToSteam", "NoLicenseOrExpired", "VACBanned", "LoggedInElseWhere",
		"VACCheckTimedOut", "AuthTicketCanceled", "AuthTicketInvalidAlreadyUsed", "AuthTicketInvalid",
		"PublisherIssuedBan",
	};
	if (response < 0 || response >= (int)(sizeof(names) / sizeof(names[0])))
	{
		return "Unknown";
	}
	return names[response];
}

#if SOURCE_ENGINE == SE_LEFT4DEAD2 && defined (_LINUX)
typedef void (*AuthFailureHelperFn)(void* steam3Server, void* client, int response);
typedef const char* (*ClientStringFn)(void* client);

#define CLIENT_GETCLIENTNAME_INDEX 7			// call [eax+1Ch]
#define CLIENT_GETNETWORKIDSTRING_INDEX 11		// call [eax+2Ch]

static AuthFailureHelperFn s_AuthFailureHelper = NULL;
static SteamAuthFailureLogFn s_AuthFailureLog = NULL;

// Replaces the engine's call to CSteam3Server::OnValidateAuthTicketResponseHelper(client, response).
__attribute__((force_align_arg_pointer))
static void OnSteamAuthFailure(void* steam3Server, void* client, int response)
{
	const bool keep = IsNoSteamLogonResponse(response);
	if (s_AuthFailureLog != NULL)
	{
		// Before the helper runs: it disconnects the client.
		void** vtable = *reinterpret_cast<void***>(client);
		const char* name = reinterpret_cast<ClientStringFn>(vtable[CLIENT_GETCLIENTNAME_INDEX])(client);
		const char* networkId = reinterpret_cast<ClientStringFn>(vtable[CLIENT_GETNETWORKIDSTRING_INDEX])(client);
		const bool validated = static_cast<const BYTE*>(client)[STEAMAUTH_CLIENT_FULLYAUTHENTICATED_OFFSET] != 0;
		s_AuthFailureLog(name != NULL ? name : "", networkId != NULL ? networkId : "", response, validated, keep);
	}
	if (!keep)
	{
		s_AuthFailureHelper(steam3Server, client, response);
	}
}

static void ExpectCode(const BYTE* code, size_t offset, const char* bytes, size_t length, const char* error)
{
	if (memcmp(code + offset, bytes, length) != 0)
	{
		throw PatchException(error);
	}
}

// Target of the 'call/jmp rel32' at 'instruction' ('code' holds a copy of its 5 bytes).
static BYTE* RelativeTarget(BYTE* instruction, const BYTE* code)
{
	int32_t displacement;
	memcpy(&displacement, code + 1, sizeof(displacement));
	return reinterpret_cast<BYTE*>(reinterpret_cast<uintptr_t>(instruction) + 5
		+ static_cast<uintptr_t>(static_cast<intptr_t>(displacement)));
}

SteamLogonKickPatch::SteamLogonKickPatch(void* engineModule, SteamAuthFailureLogFn log) : m_patch(NULL)
{
	BYTE* pWrapper = (BYTE*)g_MemUtils.SimpleResolve(engineModule, SYM_CSTEAM3SERVER_ONVALIDATEAUTHTICKETRESPONSE);
	BYTE* pHelper = (BYTE*)g_MemUtils.SimpleResolve(engineModule, SYM_CSTEAM3SERVER_ONVALIDATEAUTHTICKETRESPONSEHELPER);
	if (pWrapper == NULL || pHelper == NULL)
	{
		throw PatchException("Couldn't find 'CSteam3Server::OnValidateAuthTicketResponse' / 'OnValidateAuthTicketResponseHelper'.");
	}

	DynLibInfo lib = {};
	if (!g_MemUtils.GetLibraryInfo(engineModule, lib))
	{
		throw PatchException("Couldn't get engine module information.");
	}
	if (!IsInModule(pWrapper, STEAMAUTH_WRAPPER_CODE_SIZE, lib) || !IsInModule(pHelper, STEAMAUTH_HELPER_CODE_SIZE, lib))
	{
		throw PatchException("Steam auth patch offsets outside engine module.");
	}

	BYTE wrapperCode[STEAMAUTH_WRAPPER_CODE_SIZE];
	BYTE helperCode[STEAMAUTH_HELPER_CODE_SIZE];
	if (!ReadModuleMemory(wrapperCode, pWrapper, sizeof(wrapperCode), lib)
		|| !ReadModuleMemory(helperCode, pHelper, sizeof(helperCode), lib))
	{
		throw PatchException("'CSteam3Server::OnValidateAuthTicketResponse' is not readable.");
	}
	ExpectCode(wrapperCode, STEAMAUTH_WRAPPER_TAIL_OFFSET, STEAMAUTH_WRAPPER_TAIL_BYTES, STEAMAUTH_WRAPPER_TAIL_BYTES_SIZE,
		"'CSteam3Server::OnValidateAuthTicketResponse' patch offset incorrect (expected the jump to its body).");
	ExpectCode(helperCode, STEAMAUTH_HELPER_ARGS_OFFSET, STEAMAUTH_HELPER_ARGS_BYTES, STEAMAUTH_HELPER_ARGS_BYTES_SIZE,
		"'OnValidateAuthTicketResponseHelper' patch offset incorrect (expected its arguments and the GetClientName call).");

	// The function's code is in a separate part the short wrapper jumps to.
	BYTE* pBody = RelativeTarget(pWrapper + STEAMAUTH_WRAPPER_JMP_OFFSET, wrapperCode + STEAMAUTH_WRAPPER_JMP_OFFSET);
	if (!IsInModule(pBody, STEAMAUTH_BODY_CODE_SIZE, lib))
	{
		throw PatchException("Steam auth patch offsets outside engine module.");
	}
	BYTE bodyCode[STEAMAUTH_BODY_CODE_SIZE];
	if (!ReadModuleMemory(bodyCode, pBody, sizeof(bodyCode), lib))
	{
		throw PatchException("'CSteam3Server::OnValidateAuthTicketResponse' is not readable.");
	}
	ExpectCode(bodyCode, STEAMAUTH_BODY_RESPONSE_OFFSET, STEAMAUTH_BODY_RESPONSE_BYTES, STEAMAUTH_BODY_RESPONSE_BYTES_SIZE,
		"'CSteam3Server::OnValidateAuthTicketResponse' patch offset incorrect (expected the auth response check).");
	ExpectCode(bodyCode, STEAMAUTH_BODY_AUTHENTICATED_OFFSET, STEAMAUTH_BODY_AUTHENTICATED_BYTES, STEAMAUTH_BODY_AUTHENTICATED_BYTES_SIZE,
		"'CSteam3Server::OnValidateAuthTicketResponse' patch offset incorrect (expected the fully authenticated flag).");
	ExpectCode(bodyCode, STEAMAUTH_BODY_FAILURE_OFFSET, STEAMAUTH_BODY_FAILURE_BYTES, STEAMAUTH_BODY_FAILURE_BYTES_SIZE,
		"'CSteam3Server::OnValidateAuthTicketResponse' patch offset incorrect (expected the failure helper call).");
	ExpectCode(bodyCode, STEAMAUTH_BODY_NETWORKID_OFFSET, STEAMAUTH_BODY_NETWORKID_BYTES, STEAMAUTH_BODY_NETWORKID_BYTES_SIZE,
		"'CSteam3Server::OnValidateAuthTicketResponse' patch offset incorrect (expected the GetNetworkIDString call).");

	BYTE* pCall = pBody + STEAMAUTH_BODY_HELPER_CALL_OFFSET;
	if (RelativeTarget(pCall, bodyCode + STEAMAUTH_BODY_HELPER_CALL_OFFSET) != pHelper)
	{
		throw PatchException("'CSteam3Server::OnValidateAuthTicketResponse' patch offset incorrect (expected the call to OnValidateAuthTicketResponseHelper).");
	}

	s_AuthFailureHelper = reinterpret_cast<AuthFailureHelperFn>(pHelper);
	s_AuthFailureLog = log;

	// call OnValidateAuthTicketResponseHelper -> call OnSteamAuthFailure
	int32_t displacement = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&OnSteamAuthFailure)
		- (reinterpret_cast<uintptr_t>(pCall) + 5));
	BYTE replacement[sizeof(displacement)];
	memcpy(replacement, &displacement, sizeof(replacement));
	m_patch = new BasicStaticBinPatch<sizeof(replacement)>(pCall + 1, replacement);
}

SteamLogonKickPatch::~SteamLogonKickPatch()
{
	delete m_patch;
}

void SteamLogonKickPatch::Patch()
{
	m_patch->Patch();
}

void SteamLogonKickPatch::Unpatch()
{
	m_patch->Unpatch();
}
#endif
