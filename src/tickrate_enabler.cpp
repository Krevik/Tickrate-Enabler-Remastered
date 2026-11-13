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
#include "lagcompfix.h"
#include "hitboxlagcompfix.h"
#include "updateratefix.h"

 // memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define TE_VERSION "2.0.0"

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

// gpGlobals (NULL if IPlayerInfoManager wasn't available).
static CGlobalVars* g_pGlobals = NULL;

// Lag compensation window fix (NULL when not active); owned by m_optionalPatchManager.
#if SOURCE_ENGINE == SE_LEFT4DEAD2 && defined (_LINUX)
static LagCompWindowPatch* g_pLagCompWindowPatch = NULL;
#endif

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

// Tick interval the server will run at, known before the engine applies it (same rules as above).
static float GetConfiguredTickInterval()
{
	if (CommandLine()->CheckParm("-tickrate")) {
		float fTickRate = CommandLine()->ParmValue("-tickrate", 0.0f);
		if (std::isfinite(fTickRate) && fTickRate > 10) {
			return 1.0f / fTickRate;
		}
	}
	return 1.0f / 30.0f;
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

	g_pGlobals = pGlobals;

	LoadNextBotFixes(pGlobals);
	LoadBoomerVomitDelay();
	LoadBoomerVomitPierce(pGlobals);
	LoadLagCompWindowFix();
	LoadHitboxLagCompFixes();
	LoadUpdateRateFix();

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
#if SOURCE_ENGINE == SE_LEFT4DEAD2 && defined (_LINUX)
	g_pLagCompWindowPatch = NULL;
#endif
	g_pGlobals = NULL;
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
// Purpose: OPT-IN. "-lagcompwindow <seconds>" makes shoves keep lag compensating their targets for
// that long after the swing starts (vanilla: 5 usercmds = 50 ms at 100 tick, 166 ms at 30 tick).
// Without the parameter the game's own window is kept.
// Optional: a failure keeps the vanilla behaviour.
//---------------------------------------------------------------------------------
void L4DTickRate::LoadLagCompWindowFix()
{
#if SOURCE_ENGINE == SE_LEFT4DEAD2
	if (!CommandLine()->CheckParm("-lagcompwindow")) {
		TELog("Lag compensation window: vanilla (5 usercmds); -lagcompwindow <seconds> to change it.");
		return;
	}

#if defined (_LINUX)
	float flWindow;
	if (!LagCompWindowSecondsFromParam(CommandLine()->ParmValue("-lagcompwindow", 0.0f), flWindow)) {
		TELog("Invalid '-lagcompwindow %s' (seconds, 0.01-1), lag compensation window left vanilla.", CommandLine()->ParmValue("-lagcompwindow", ""));
		return;
	}

	try {
		float flTickInterval = GetConfiguredTickInterval();
		LagCompWindowPatch* pPatch = new LagCompWindowPatch(g_pGameDll, flWindow, flTickInterval);
		m_optionalPatchManager.Register(pPatch);
		pPatch->Patch();
		g_pLagCompWindowPatch = pPatch;
		TELog("Lag compensation window: %d usercmds (%.0f ms at %.0f tick; vanilla 5 usercmds).",
			pPatch->GetWindowUsercmds(), pPatch->GetWindowUsercmds() * flTickInterval * 1000.0f, 1.0f / flTickInterval);
	} catch (PatchException& e) {
		TELog("Lag compensation window fix disabled (vanilla kept): %s", e.GetDescription());
	}
#else
	TELog("Lag compensation window fix is only available on Linux servers.");
#endif
#endif
}

//---------------------------------------------------------------------------------
// Purpose: hitboxes of lag compensated targets.
// 1. An entity rewound for a shot / swing gets its cached bones flushed, and again when it is put
//    back, so hitboxes are tested at the rewound position. Disable with "-nobonecachefix".
// 2. Bullets rewind the target's animation as well (vanilla asks for it along a zero-length ray, so
//    it never happens). Disable with "-nohitboxanimfix".
// Linux only. Optional: a failure keeps the vanilla behaviour of that part.
//---------------------------------------------------------------------------------
void L4DTickRate::LoadHitboxLagCompFixes()
{
#if SOURCE_ENGINE == SE_LEFT4DEAD2
#if defined (_LINUX)
	if (CommandLine()->CheckParm("-nobonecachefix")) {
		TELog("Lag compensation bone cache fix disabled by -nobonecachefix.");
	} else {
		try {
			ICodePatch* pPatch = new LagCompBoneCachePatch(g_pGameDll);
			m_optionalPatchManager.Register(pPatch);
			pPatch->Patch();
			TELog("Lag compensation flushes cached bones of rewound and restored entities (-nobonecachefix for vanilla).");
		} catch (PatchException& e) {
			TELog("Lag compensation bone cache fix disabled (vanilla kept): %s", e.GetDescription());
		}
	}

	if (CommandLine()->CheckParm("-nohitboxanimfix")) {
		TELog("Bullet hitbox animation rewind disabled by -nohitboxanimfix.");
	} else {
		try {
			ICodePatch* pPatch = new BulletHitboxLagCompPatch(g_pGameDll);
			m_optionalPatchManager.Register(pPatch);
			pPatch->Patch();
			TELog("Bullets rewind the animation of lag compensated targets (-nohitboxanimfix for vanilla).");
		} catch (PatchException& e) {
			TELog("Bullet hitbox animation rewind disabled (vanilla kept): %s", e.GetDescription());
		}
	}
#else
	TELog("Lag compensated hitbox fixes are only available on Linux servers.");
#endif
#endif
}

//---------------------------------------------------------------------------------
// Purpose: above 100 tick, let the engine send one snapshot per tick instead of capping clients at
// 100 snapshots per second (snapshots then go out on an uneven tick pattern). The cap follows the
// tickrate. Disable with "-noupdateratefix". Linux only.
// Optional: a failure keeps the engine's 100 cap.
//---------------------------------------------------------------------------------
void L4DTickRate::LoadUpdateRateFix()
{
#if SOURCE_ENGINE == SE_LEFT4DEAD2
	if (CommandLine()->CheckParm("-noupdateratefix")) {
		TELog("Snapshot rate fix disabled by -noupdateratefix (engine cap 100/s).");
		return;
	}

	int iCap = UpdateRateCapForTickInterval(GetConfiguredTickInterval());
	if (iCap <= UPDATERATE_VANILLA_CAP) {
		TELog("Snapshot rate fix not needed at this tickrate (engine cap 100/s).");
		return;
	}

#if defined (_LINUX)
	try {
		ICodePatch* pPatch = new UpdateRateCapPatch(g_pEngine, iCap);
		m_optionalPatchManager.Register(pPatch);
		pPatch->Patch();
		TELog("Snapshot rate cap raised to %d/s (engine default 100); set sv_minupdaterate / sv_maxupdaterate to %d for one snapshot per tick.", iCap, iCap);
	} catch (PatchException& e) {
		TELog("Snapshot rate fix disabled (engine cap 100/s kept): %s", e.GetDescription());
	}
#else
	TELog("Snapshot rate fix is only available on Linux servers (engine cap 100/s).");
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
#if defined (_LINUX)
	if (g_pLagCompWindowPatch != NULL && g_pGlobals != NULL) {
		int iOldWindow = g_pLagCompWindowPatch->GetWindowUsercmds();
		g_pLagCompWindowPatch->SetTickInterval(g_pGlobals->interval_per_tick);
		if (g_pLagCompWindowPatch->GetWindowUsercmds() != iOldWindow) {
			TELog("Lag compensation window: %d usercmds (tick interval %.6f).", g_pLagCompWindowPatch->GetWindowUsercmds(), g_pGlobals->interval_per_tick);
		}
	}
#endif
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
