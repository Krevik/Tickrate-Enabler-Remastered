/**
 * vim: set ts=4 :
 * =============================================================================
 * Tickrate Enabler
 * Copyright (C) 2012 Michael "ProdigySim" Busby
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
 * As a special exception, the author(s) give you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, the author(s) grant
 * this exception to all derivative works.  The author(s) define further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include "thirdparty/memutils.h"
#include "thirdparty/sm_convar.h"
#include "tier0/icommandline.h"
#include "icvar.h"

#include "sourcehook.h"
#include "sourcehook_impl.h"
#include "sourcehook_impl_chookidman.h"

#include "thirdparty/codepatch/patchmanager.h"

#include "tickrate_enabler.h"
#include "edict.h"
#include "game/server/iplayerinfo.h"
#include "patchexceptions.h"

#include "boomervomitpatch.h"
#include "maxrate_patches.h"
#include "nextbotfixes.h"

 // memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define TE_VERSION "1.7.3"

L4DTickRate g_L4DTickRatePlugin;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(L4DTickRate, IServerPluginCallbacks, INTERFACEVERSION_ISERVERPLUGINCALLBACKS, g_L4DTickRatePlugin);

SourceHook::Impl::CSourceHookImpl g_SourceHook;
SourceHook::ISourceHook* g_SHPtr = &g_SourceHook;
int g_PLID = 0;

SH_DECL_HOOK0(IServerGameDLL, GetTickInterval, const, 0, float);

IServerGameDLL* g_pGameDll = NULL;
IVEngineServer* g_pEngine = NULL;
ICvar* g_pCvar = NULL;

// Used by the NextBot fixes (NULL when they are not active).
static CGlobalVars* g_pNextBotGlobals = NULL;
static ConVar* g_pNbUpdateFrequency = NULL;

CvarInfo g_ResetCvars[] = { "sv_maxrate", "sv_minrate", "net_splitpacket_maxrate" };
size_t g_iResetCvarsCount = sizeof(g_ResetCvars) / sizeof(g_ResetCvars[0]);

// Log file next to the SourceMod logs (or in addons/ without SourceMod), so the plugin's messages can
// be read without access to the server console. Empty until the game directory is known.
static char g_szLogFiles[2][512];

static void InitLogFile()
{
	char gameDir[256];
	g_pEngine->GetGameDir(gameDir, sizeof(gameDir));
	snprintf(g_szLogFiles[0], sizeof(g_szLogFiles[0]), "%s/addons/sourcemod/logs/tickrate_enabler.log", gameDir);
	snprintf(g_szLogFiles[1], sizeof(g_szLogFiles[1]), "%s/addons/tickrate_enabler.log", gameDir);
}

// Prints a "[Tickrate_Enabler] ..." line to the console and appends it to the log file.
static void TELog(const char* fmt, ...)
{
	char message[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);

	Msg("[Tickrate_Enabler] %s\n", message);

	for (size_t i = 0; i < sizeof(g_szLogFiles) / sizeof(g_szLogFiles[0]); i++) {
		if (g_szLogFiles[i][0] == '\0') {
			continue;
		}

		FILE* pFile = fopen(g_szLogFiles[i], "a");
		if (pFile == NULL) {
			continue; // e.g. no SourceMod: try addons/
		}

		char timestamp[64] = "";
		time_t now = time(NULL);
		struct tm* pLocal = localtime(&now);
		if (pLocal != NULL) {
			strftime(timestamp, sizeof(timestamp), "%m/%d/%Y - %H:%M:%S", pLocal);
		}
		fprintf(pFile, "L %s: [Tickrate_Enabler] %s\n", timestamp, message);
		fclose(pFile);
		break;
	}
}

static float Handler_GetTickInterval()
{
	float fDefTickInterval = SH_CALL(g_pGameDll, &IServerGameDLL::GetTickInterval)();
	float fTickInterval = fDefTickInterval;
	static bool s_bReported = false;

	if (CommandLine()->CheckParm("-tickrate")) {
		float fTickRate = CommandLine()->ParmValue("-tickrate", 0.0f);

		if (std::isfinite(fTickRate) && fTickRate > 10) {
			fTickInterval = 1.0f / fTickRate;

			if (!s_bReported) {
				s_bReported = true;
				TELog("Applied tickrate %.2f (interval %.6f); Default tickrate: %.2f (interval: %.6f).", fTickRate, fTickInterval, 1.0f / fDefTickInterval, fDefTickInterval);
			}
		} else if (!s_bReported) {
			s_bReported = true;
			TELog("Invalid '-tickrate %s' (must be > 10), keeping the default tickrate %.2f.", CommandLine()->ParmValue("-tickrate", ""), 1.0f / fDefTickInterval);
		}
	} else if (!s_bReported) {
		s_bReported = true;
		TELog("No '-tickrate' launch parameter, keeping the default tickrate %.2f.", 1.0f / fDefTickInterval);
	}

	RETURN_META_VALUE(MRES_SUPERCEDE, fTickInterval);
}

//---------------------------------------------------------------------------------
// Purpose: called when the plugin is loaded, load the interface we need from the engine
//---------------------------------------------------------------------------------
bool L4DTickRate::Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory)
{
	// Error() would shut the dedicated server down; failures are logged and the plugin isn't loaded.
	g_pEngine = reinterpret_cast<IVEngineServer*>(interfaceFactory(INTERFACEVERSION_VENGINESERVER, NULL));
	if (g_pEngine == NULL) {
		TELog("Failed to get a pointer on 'IVEngineServer' interface, plugin not loaded.");
		return false;
	}
	InitLogFile();
	TELog("Loading %s.", GetPluginDescription());

	g_pGameDll = reinterpret_cast<IServerGameDLL*>(gameServerFactory(INTERFACEVERSION_SERVERGAMEDLL, NULL));
	if (g_pGameDll == NULL) {
		TELog("Failed to get a pointer on 'IServerGameDLL' interface, plugin not loaded.");
		return false;
	}

	g_pCvar = reinterpret_cast<ICvar*>(interfaceFactory(CVAR_INTERFACE_VERSION, NULL));
	if (g_pCvar == NULL) {
		TELog("Failed to get a pointer on 'ICvar' interface, plugin not loaded.");
		return false;
	}

	// Used only to verify boomer vomit patch offsets; patching still works without it.
	CGlobalVars* pGlobals = NULL;
	IPlayerInfoManager* pPlayerInfoManager = reinterpret_cast<IPlayerInfoManager*>(gameServerFactory(INTERFACEVERSION_PLAYERINFOMANAGER, NULL));
	if (pPlayerInfoManager != NULL) {
		pGlobals = pPlayerInfoManager->GetGlobalVars();
	} else {
		TELog("Failed to get 'IPlayerInfoManager', gpGlobals verification disabled.");
	}

	try {
		m_patchManager.Register(new BoomerVomitFrameTimePatch(g_pGameDll, pGlobals));
		m_patchManager.Register(new NetChanDataRatePatch((BYTE*)g_pEngine));
#if defined (SIG_CGAMECLIENT_SETDATARATE)
		m_patchManager.Register(new GameClientSetRatePatch((BYTE*)g_pEngine));
#endif

		m_patchManager.Register(new ClampClientRatePatch((BYTE*)g_pEngine));

		m_patchManager.PatchAll();
	} catch (PatchException& e) {
		TELog("!!!!! Patch Failure: %s !!!!!", e.GetDescription());
		TELog("Failed to process all tickrate_enabler patches, bailing out (plugin not loaded, -tickrate has no effect).");
		m_patchManager.UnpatchAll();
		m_patchManager.UnregisterAll();
		return false;
	}
	TELog("Boomer vomit range and rate limit patches applied.");

	SH_ADD_HOOK(IServerGameDLL, GetTickInterval, g_pGameDll, SH_STATIC(Handler_GetTickInterval), false);

	LoadNextBotFixes(pGlobals);
	LoadBoomerVomitDelay();
	LoadBoomerVomitPierce(pGlobals);

	for (size_t i = 0; i < g_iResetCvarsCount; i++) {
		g_ResetCvars[i].ClearSavedDefaults();
		ConVar* pCvar = g_pCvar->FindVar(g_ResetCvars[i].GetName());

		if (pCvar == NULL) {
			TELog("Can't find convar '%s'.", g_ResetCvars[i].GetName());
			continue;
		}

		g_ResetCvars[i].SaveDefaults(pCvar->m_bHasMax, pCvar->m_fMaxVal);

		pCvar->m_bHasMax = false;
		pCvar->m_fMaxVal = 0.0f;
	}

	return true;
}

//---------------------------------------------------------------------------------
// Purpose: called when the plugin is unloaded (turned off)
//---------------------------------------------------------------------------------
void L4DTickRate::Unload(void)
{
	for (size_t i = 0; i < g_iResetCvarsCount; i++) {
		ConVar* pCvar = g_pCvar->FindVar(g_ResetCvars[i].GetName());

		if (pCvar == NULL) {
			TELog("Can't find convar '%s'.", g_ResetCvars[i].GetName());
			g_ResetCvars[i].ClearSavedDefaults();
			continue;
		}

		g_ResetCvars[i].RestoreDefaults(pCvar->m_bHasMax, pCvar->m_fMaxVal);
	}

	m_patchManager.UnpatchAll();
	m_patchManager.UnregisterAll();

	m_optionalPatchManager.UnpatchAll();
	m_optionalPatchManager.UnregisterAll();
	g_pNextBotGlobals = NULL;
	g_pNbUpdateFrequency = NULL;

	SH_REMOVE_HOOK(IServerGameDLL, GetTickInterval, g_pGameDll, SH_STATIC(Handler_GetTickInterval), false);
}

//---------------------------------------------------------------------------------
// Purpose: fixes for common infected / witch issues caused by a lowered nb_update_frequency.
// Optional: a failure only disables the affected fix. Disable all with -nonextbotfixes.
//---------------------------------------------------------------------------------
void L4DTickRate::LoadNextBotFixes(CGlobalVars* pGlobals)
{
#if SOURCE_ENGINE == SE_LEFT4DEAD2
	if (CommandLine()->CheckParm("-nonextbotfixes")) {
		TELog("NextBot fixes disabled by -nonextbotfixes.");
		return;
	}

	if (pGlobals != NULL) {
		g_pNbUpdateFrequency = g_pCvar->FindVar("nb_update_frequency");
	}

	if (g_pNbUpdateFrequency != NULL) {
		try {
			ICodePatch* pPatch = new ZombieCollisionPushPatch(g_pGameDll);
			m_optionalPatchManager.Register(pPatch);
			pPatch->Patch();
			g_pNextBotGlobals = pGlobals;
		} catch (PatchException& e) {
			TELog("Infected collision push fix disabled: %s", e.GetDescription());
		}

		try {
			ICodePatch* pPatch = new WitchContactAngerPatch(g_pGameDll, pGlobals);
			m_optionalPatchManager.Register(pPatch);
			pPatch->Patch();
			g_pNextBotGlobals = pGlobals;
		} catch (PatchException& e) {
			TELog("Witch contact anger fix disabled: %s", e.GetDescription());
		}
	} else {
		TELog("Infected collision push / witch contact anger fixes disabled: 'nb_update_frequency' or gpGlobals not available.");
	}

	try {
		ICodePatch* pPatch = new WitchContactVictimPatch(g_pGameDll);
		m_optionalPatchManager.Register(pPatch);
		pPatch->Patch();
	} catch (PatchException& e) {
		TELog("Witch victim switch fix disabled: %s", e.GetDescription());
	}
#endif
}

//---------------------------------------------------------------------------------
// Purpose: delay between the start of the boomer's vomit and the first hits on survivors
// (vanilla 0.25 s). Set with "-vomitdelay <seconds>", default 0.05.
// Optional: a failure keeps the vanilla delay.
//---------------------------------------------------------------------------------
void L4DTickRate::LoadBoomerVomitDelay()
{
#if SOURCE_ENGINE == SE_LEFT4DEAD2
	float flDelay = 0.05f;
	if (CommandLine()->CheckParm("-vomitdelay")) {
		flDelay = CommandLine()->ParmValue("-vomitdelay", flDelay);
	}

	if (flDelay < 0.0f) {
		flDelay = 0.0f;
	} else if (flDelay > 1.0f) {
		flDelay = 1.0f;
	}

	try {
		ICodePatch* pPatch = new BoomerVomitDelayPatch(g_pGameDll, flDelay);
		m_optionalPatchManager.Register(pPatch);
		pPatch->Patch();
		TELog("Boomer vomit hit delay: %.3f s (vanilla %.2f s).", flDelay, VOMIT_DELAY_VANILLA);
	} catch (PatchException& e) {
		TELog("Boomer vomit delay patch disabled (vanilla %.2f s kept): %s", VOMIT_DELAY_VANILLA, e.GetDescription());
	}
#endif
}

//---------------------------------------------------------------------------------
// Purpose: the boomer's vomit stream passes through survivors instead of stopping at the first one,
// so survivors standing behind each other all get covered. Disable with "-novomitpierce".
// Optional: a failure keeps the vanilla behaviour.
//---------------------------------------------------------------------------------
void L4DTickRate::LoadBoomerVomitPierce(CGlobalVars* pGlobals)
{
#if SOURCE_ENGINE == SE_LEFT4DEAD2
	if (CommandLine()->CheckParm("-novomitpierce")) {
		TELog("Boomer vomit pierce disabled by -novomitpierce.");
		return;
	}

#if defined (_LINUX)
	try {
		ICodePatch* pPatch = new BoomerVomitPiercePatch(g_pGameDll, pGlobals);
		m_optionalPatchManager.Register(pPatch);
		pPatch->Patch();
		TELog("Boomer vomit passes through survivors (-novomitpierce for vanilla).");
	} catch (PatchException& e) {
		TELog("Boomer vomit pierce patch disabled (vanilla kept): %s", e.GetDescription());
	}
#else
	TELog("Boomer vomit pierce patch is only available on Linux servers.");
#endif
#endif
}

//---------------------------------------------------------------------------------
// Purpose: called once per server frame
//---------------------------------------------------------------------------------
void L4DTickRate::GameFrame(bool simulating)
{
#if SOURCE_ENGINE == SE_LEFT4DEAD2
	if (g_pNextBotGlobals != NULL) {
		UpdateNextBotFixes(g_pNbUpdateFrequency, g_pNextBotGlobals->interval_per_tick);
	}
#endif
}

//---------------------------------------------------------------------------------
// Purpose: the name of this plugin, returned in "plugin_print" command
//---------------------------------------------------------------------------------
const char* L4DTickRate::GetPluginDescription(void)
{
#ifdef _DEBUG
	return "Tickrate_Enabler Remastered " TE_VERSION " (dev), ProdigySim.";
#else 
	return "Tickrate_Enabler Remastered " TE_VERSION ", ProdigySim.";
#endif
}
