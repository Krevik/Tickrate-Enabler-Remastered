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

#define TE_VERSION "1.7.2"

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

static float Handler_GetTickInterval()
{
	float fDefTickInterval = SH_CALL(g_pGameDll, &IServerGameDLL::GetTickInterval)();
	float fTickInterval = fDefTickInterval;

	if (CommandLine()->CheckParm("-tickrate")) {
		float fTickRate = CommandLine()->ParmValue("-tickrate", 0.0f);

		if (std::isfinite(fTickRate) && fTickRate > 10) {
			fTickInterval = 1.0f / fTickRate;

			static bool s_bReported = false;
			if (!s_bReported) {
				s_bReported = true;
				Msg("[Tickrate_Enabler] Applied tickrate %.2f (interval %.6f); Default tickrate: %.2f (interval: %.6f).\n", fTickRate, fTickInterval, 1.0f / fDefTickInterval, fDefTickInterval);
			}
		}
	}

	RETURN_META_VALUE(MRES_SUPERCEDE, fTickInterval);
}

//---------------------------------------------------------------------------------
// Purpose: called when the plugin is loaded, load the interface we need from the engine
//---------------------------------------------------------------------------------
bool L4DTickRate::Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory)
{
	g_pGameDll = reinterpret_cast<IServerGameDLL*>(gameServerFactory(INTERFACEVERSION_SERVERGAMEDLL, NULL));
	if (g_pGameDll == NULL) {
		Error("[Tickrate_Enabler] Failed to get a pointer on 'IServerGameDLL' interface.\n");
		return false;
	}

	g_pEngine = reinterpret_cast<IVEngineServer*>(interfaceFactory(INTERFACEVERSION_VENGINESERVER, NULL));
	if (g_pEngine == NULL) {
		Error("[Tickrate_Enabler] Failed to get a pointer on 'IVEngineServer' interface.\n");
		return false;
	}

	g_pCvar = reinterpret_cast<ICvar*>(interfaceFactory(CVAR_INTERFACE_VERSION, NULL));
	if (g_pCvar == NULL) {
		Error("[Tickrate_Enabler] Failed to get a pointer on 'ICvar' interface.\n");
		return false;
	}

	// Used only to verify boomer vomit patch offsets; patching still works without it.
	CGlobalVars* pGlobals = NULL;
	IPlayerInfoManager* pPlayerInfoManager = reinterpret_cast<IPlayerInfoManager*>(gameServerFactory(INTERFACEVERSION_PLAYERINFOMANAGER, NULL));
	if (pPlayerInfoManager != NULL) {
		pGlobals = pPlayerInfoManager->GetGlobalVars();
	} else {
		Msg("[Tickrate_Enabler] Failed to get 'IPlayerInfoManager', gpGlobals verification disabled.\n");
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
		Error("!!!!!\nPatch Failure: %s\n!!!!!\n", e.GetDescription());
		Error("Failed to process all tickrate_enabler patches, bailing out.\n");
		return false;
	}
	
	SH_ADD_HOOK(IServerGameDLL, GetTickInterval, g_pGameDll, SH_STATIC(Handler_GetTickInterval), false);

	LoadNextBotFixes(pGlobals);
	LoadBoomerVomitDelay();
	
	for (size_t i = 0; i < g_iResetCvarsCount; i++) {
		ConVar* pCvar = g_pCvar->FindVar(g_ResetCvars[i].GetName());

		if (pCvar == NULL) {
			Msg("[Tickrate_Enabler] Can't find convar '%s'.""\n", g_ResetCvars[i].GetName());
			continue;
		}

		g_ResetCvars[i].m_bDefaultHasMax = pCvar->m_bHasMax;
		g_ResetCvars[i].m_fDefaultMaxValue = pCvar->m_fMaxVal;

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
			Msg("[Tickrate_Enabler] Can't find convar '%s'.""\n", g_ResetCvars[i].GetName());
			continue;
		}

		pCvar->m_bHasMax = g_ResetCvars[i].m_bDefaultHasMax;
		pCvar->m_fMaxVal = g_ResetCvars[i].m_fDefaultMaxValue;
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
		Msg("[Tickrate_Enabler] NextBot fixes disabled by -nonextbotfixes.\n");
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
			Msg("[Tickrate_Enabler] Infected collision push fix disabled: %s\n", e.GetDescription());
		}

		try {
			ICodePatch* pPatch = new WitchContactAngerPatch(g_pGameDll, pGlobals);
			m_optionalPatchManager.Register(pPatch);
			pPatch->Patch();
			g_pNextBotGlobals = pGlobals;
		} catch (PatchException& e) {
			Msg("[Tickrate_Enabler] Witch contact anger fix disabled: %s\n", e.GetDescription());
		}
	} else {
		Msg("[Tickrate_Enabler] Infected collision push / witch contact anger fixes disabled: 'nb_update_frequency' or gpGlobals not available.\n");
	}

	try {
		ICodePatch* pPatch = new WitchContactVictimPatch(g_pGameDll);
		m_optionalPatchManager.Register(pPatch);
		pPatch->Patch();
	} catch (PatchException& e) {
		Msg("[Tickrate_Enabler] Witch victim switch fix disabled: %s\n", e.GetDescription());
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
		Msg("[Tickrate_Enabler] Boomer vomit hit delay: %.3f s (vanilla %.2f s).\n", flDelay, VOMIT_DELAY_VANILLA);
	} catch (PatchException& e) {
		Msg("[Tickrate_Enabler] Boomer vomit delay patch disabled (vanilla %.2f s kept): %s\n", VOMIT_DELAY_VANILLA, e.GetDescription());
	}
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
