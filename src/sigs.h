/**
 * vim: set ts=4 :
 * =============================================================================
 * BoomerVomitPatch
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
#ifndef _SIGS_H_
#define _SIGS_H_

// @A1m`: Does not truncate strings with 00 bytes, 
// eliminating the need to manually count the number of bytes in a string.
#define DEFINE_SIG(NAME, RAW) \
    constexpr char NAME##_RAW[] = RAW; \
    constexpr size_t NAME##_SIZE = sizeof(NAME##_RAW) - 1; \
    constexpr const char* NAME = NAME##_RAW

/* Platform specific offset+sig data */

/* CVomit::UpdateAbility() function finding */
// Same symbol on both l4d1+2 (linux)
#define SYM_CVOMIT_UPDATEABILITY "_ZN6CVomit13UpdateAbilityEv"

// search for "stopvomit" string in CVomit::StopVomitEffect() + ~0x1A0, xref StopVomitEffect + ~0xE0 (farther than other similar)
#if SOURCE_ENGINE == SE_LEFT4DEAD
	// 81 EC ? ? ? ? 53 55 56 57 8B F9 8B 87
	DEFINE_SIG(SIG_CVOMIT_UPDATEABILITY, "\x81\xEC\x2A\x2A\x2A\x2A\x53\x55\x56\x57\x8B\xF9\x8B\x87");
#elif SOURCE_ENGINE == SE_LEFT4DEAD2
	// 55 8B EC 81 EC ? ? ? ? A1 ? ? ? ? 33 C5 89 45 FC 53 56 57 8B F9 8B 87 ? ? ? ? 83
	DEFINE_SIG(SIG_CVOMIT_UPDATEABILITY, "\x55\x8B\xEC\x81\xEC\x2A\x2A\x2A\x2A\xA1\x2A\x2A\x2A\x2A\x33\xC5\x89\x45\xFC\x53\x56\x57\x8B\xF9\x8B\x87\x2A\x2A\x2A\x2A\x83");
#endif

/* gpGlobals read offsets into CVomit::UpdateAbility() */
const int g_FrameTimeReadOffsets[] =
#if defined (_LINUX)
	#if SOURCE_ENGINE == SE_LEFT4DEAD
		{0x3C1}; // L4D1 LINUX
	#elif SOURCE_ENGINE == SE_LEFT4DEAD2
		{0x5FE}; // L4D2 LINUX
	#endif
#elif defined (_WIN32)
	#if SOURCE_ENGINE == SE_LEFT4DEAD
		{0x2D5, 0x476};
		/* 725, 1142 */
		/* 
		0x2D5	=	725 	= .text:102D0245 A1 90 BA 5E 10                          mov     eax, dword_105EBA90
		0x476	=	1142	= .text:102D03E6 A1 90 BA 5E 10                          mov     eax, dword_105EBA90
		*/
	#elif SOURCE_ENGINE == SE_LEFT4DEAD2
		{0x376, 0x543};
		/* 886, 1347 */
		/* 
		0x376	=	886 	= .text:10372B86 A1 90 76 7F 10                          mov     eax, dword_107F7690
		0x543	=	1347	= .text:10372D53 A1 90 76 7F 10                          mov     eax, dword_107F3590
		*/
	#endif
#endif

#define NUM_FRAMETIME_READS (sizeof(g_FrameTimeReadOffsets)/sizeof(g_FrameTimeReadOffsets[0]))


#if defined (_WIN32)
	#if SOURCE_ENGINE == SE_LEFT4DEAD
		// F3 0F 10 44 24 04 F3 0F 10 0D ? ? ? ? 0F 2F C1 76 10
		DEFINE_SIG(SIG_CNETCHAN_SETDATARATE, "\xF3\x0F\x10\x44\x24\x04\xF3\x0F\x10\x0D\x2A\x2A\x2A\x2A\x0F\x2F\xC1\x76\x10");
		
		// Jump from just after first instruction (6 bytes, loads argument float to xmm0)
		// to the lower-bound comparison instructions
		//(func+35 bytes, jump offset 35-6-2 = 27 = 0x1B)
		#define CNETCHAN_PATCH_OFFSET 6
		// movss
		#define CNETCHAN_PATCH_CHECK_BYTE 0xF3
		#define CNETCHAN_PATCH_JUMP_OFFSET 0x1B
	#elif SOURCE_ENGINE == SE_LEFT4DEAD2
		// 55 8B EC F3 0F 10 45 08 F3 0F 10 0D ? ? ? ? 0F 2F C1 76 ? 0F 28 C1
		DEFINE_SIG(SIG_CNETCHAN_SETDATARATE, "\x55\x8B\xEC\xF3\x0F\x10\x45\x08\xF3\x0F\x10\x0D\x2A\x2A\x2A\x2A\x0F\x2F\xC1\x76\x2A\x0F\x28\xC1");

		#define CNETCHAN_PATCH_OFFSET 8
		#define CNETCHAN_PATCH_CHECK_BYTE 0xF3
		#define CNETCHAN_PATCH_JUMP_OFFSET 0x1C
	#endif
#elif defined (_LINUX)
	#define SIG_CNETCHAN_SETDATARATE "_ZN8CNetChan11SetDataRateEf"

	#if SOURCE_ENGINE == SE_LEFT4DEAD
		// Change comparison jump at +0x21 to NOP2, removing upper bound check.
		#define CNETCHAN_PATCH_OFFSET 0x21
		#define CNETCHAN_PATCH_CHECK_BYTE JA_8_OPCODE
	#elif SOURCE_ENGINE == SE_LEFT4DEAD2
		#define CNETCHAN_PATCH_OFFSET 0x18
		#define CNETCHAN_PATCH_CHECK_BYTE JB_8_OPCODE
	#endif
#endif


// @A1m`:
// Some binaries have a built-in 'ClampClientRate' to 'CGameClient::SetRate' function.
// Need patch fuctions:
// L4D1 windows - 'ClampClientRate'.
// L4D1 linux - 'CGameClient::SetRate', 'ClampClientRate'.
// 
// L4D2 windows - 'CGameClient::SetRate', 'ClampClientRate'.
// L4D2 linux - 'ClampClientRate'.

// @A1m`: 'CGameClient::SetRate' patch, L4D1 linux, L4D2 windows
#if defined (_WIN32) && SOURCE_ENGINE == SE_LEFT4DEAD2
		// 55 8B EC A1 ? ? ? ? 8B 40 30 85 C0 7E 0B
		DEFINE_SIG(SIG_CGAMECLIENT_SETDATARATE, "\x55\x8B\xEC\xA1\x2A\x2A\x2A\x2A\x8B\x40\x30\x85\xC0\x7E\x0B");

		#define CGAMECLIENT_PATCH_OFFSET 0x30
		#define CGAMECLIENT_PATCH_CHECK_BYTE 0x3D
		#define CGAMECLIENT_PATCH_JUMP_OFFSET 0x0C
#elif defined (_LINUX) && SOURCE_ENGINE == SE_LEFT4DEAD
	#define SIG_CGAMECLIENT_SETDATARATE "_ZN11CGameClient7SetRateEib"

	// only for l4d1
	/*
		+0x4C
		B8 30 75 00 00     mov     eax, 7530h
		81 FA 30 75 00 00  cmp     edx, 7530h
		0F 4E C            cmovle  eax, edx
	
		Filling this with NOPs
	*/
	#define CGAMECLIENT_PATCH_OFFSET 0x4C
	#define CGAMECLIENT_PATCH_CHECK_BYTE MOV_R32_IMM32_OPCODE
#endif


// @A1m`: ClampClientRate patch, all binares
#if defined (_WIN32)
	#if SOURCE_ENGINE == SE_LEFT4DEAD
		// A1 ? ? ? ? 8B 40 30 85 C0 7E 0C 8B
		DEFINE_SIG(SIG_CLAMPCLIENTRATE, "\xA1\x2A\x2A\x2A\x2A\x8B\x40\x30\x85\xC0\x7E\x0C\x8B");

		// Offset 0x2F should be cmp eax, 30000; jle +0x06
		// Change to JMP +0x0B

		/*
			+0x2F
			3D 30 75 00 00	cmp     eax, 7530h
			7E 06			jle     short loc_101713EC
		*/
		#define CLAMPCLIENTRATE_PATCH_CHECK_BYTE 0x3D
		#define CLAMPCLIENTRATE_PATCH_OFFSET 0x2F
		#define CLAMPCLIENTRATE_PATCH_JUMP_OFFSET 0x0B
	#elif SOURCE_ENGINE == SE_LEFT4DEAD2
		// 55 8B EC 8B 45 08 53 8B 5D 0C 84 DB 75
		DEFINE_SIG(SIG_CLAMPCLIENTRATE, "\x55\x8B\xEC\x8B\x45\x08\x53\x8B\x5D\x0C\x84\xDB\x75");

		// Jump over the upper bound check
		#define CLAMPCLIENTRATE_PATCH_CHECK_BYTE 0x3D
		#define CLAMPCLIENTRATE_PATCH_OFFSET 0x34
		#define CLAMPCLIENTRATE_PATCH_JUMP_OFFSET 0x16
	#endif
#elif defined (_LINUX)
	#define SIG_CLAMPCLIENTRATE "_Z15ClampClientRatei"

	/*
		+CLAMPCLIENTRATE_PATCH_OFFSET:
		B8 30 75 00 00		mov     eax, 7530h
		81 FA 30 75 00 00	cmp     edx, 7530h
		0F 4E C2			cmovle  eax, edx
	*/
	#define CLAMPCLIENTRATE_PATCH_CHECK_BYTE MOV_R32_IMM32_OPCODE

	#if SOURCE_ENGINE == SE_LEFT4DEAD
		#define CLAMPCLIENTRATE_PATCH_OFFSET 0x40
	#elif SOURCE_ENGINE == SE_LEFT4DEAD2
		#define CLAMPCLIENTRATE_PATCH_OFFSET 0x38
	#endif
#endif

/* NextBot (common infected / witch) fixes for lowered nb_update_frequency, L4D2 only. */
#if SOURCE_ENGINE == SE_LEFT4DEAD2
	#if defined (_LINUX)
		#define SYM_ZOMBIEBOTLOCOMOTION_RESOLVEZOMBIECOLLISIONS "_ZN19ZombieBotLocomotion23ResolveZombieCollisionsERK6Vector"
		#define SYM_WITCHATTACK_ONCONTACT "_ZN11WitchAttack9OnContactEP8InfectedP11CBaseEntityP10CGameTrace"
		#define SYM_WITCHATTACK_ISVALIDENEMY "_ZN11WitchAttack12IsValidEnemyEP11CBaseEntity"
		#define SYM_WITCHANGRY_ONCONTACT "_ZN10WitchAngry9OnContactEP8InfectedP11CBaseEntityP10CGameTrace"
		#define SYM_WITCHLOCOMOTION_VTABLE "_ZTV15WitchLocomotion"
		#define SYM_ZOMBIEBOTLOCOMOTION_CLIMBUPTOLEDGE "_ZN19ZombieBotLocomotion14ClimbUpToLedgeERK6VectorS2_PK11CBaseEntity"
	#elif defined (_WIN32)
		// Vprof string "ZombieBotLocomotion::ResolveZombieCollisions" is referenced from this function.
		// 55 8B EC 83 EC 3C 8B 45 0C 8B 50 04 53 8B 5D 08 56 57 8B F9 8B 08 8B 40 08 89 0B 8B 4F 78
		DEFINE_SIG(SIG_ZOMBIEBOTLOCOMOTION_RESOLVEZOMBIECOLLISIONS, "\x55\x8B\xEC\x83\xEC\x3C\x8B\x45\x0C\x8B\x50\x04\x53\x8B\x5D\x08\x56\x57\x8B\xF9\x8B\x08\x8B\x40\x08\x89\x0B\x8B\x4F\x78");

		// WitchAttack vtable entry after the two OnLeaveGround/OnLandOnGround(Action*) overloads;
		// compares contacted entity class against "prop_door*" / "func_door*".
		// 55 8B EC 51 53 56 57 8B 7D 10 8B F1 89 75 FC 85 FF 0F 84 ? ? ? ? 6A 00 6A 01 6A 00 57 E8
		DEFINE_SIG(SIG_WITCHATTACK_ONCONTACT, "\x55\x8B\xEC\x51\x53\x56\x57\x8B\x7D\x10\x8B\xF1\x89\x75\xFC\x85\xFF\x0F\x84\x2A\x2A\x2A\x2A\x6A\x00\x6A\x01\x6A\x00\x57\xE8");

		// WitchAngry vtable entry 51 (OnContact); reads z_witch_hostile_at_me_anger * z_witch_anger_rate * frametime.
		// 55 8B EC 56 8B 75 10 85 F6 74 ? 8B 06 8B 90 28 01 00 00 8B CE FF D2 84 C0 74 ? 8B 06 8B 90 68 01 00 00 8B CE FF D2 84 C0 74 ? 8B 0D
		DEFINE_SIG(SIG_WITCHANGRY_ONCONTACT, "\x55\x8B\xEC\x56\x8B\x75\x10\x85\xF6\x74\x2A\x8B\x06\x8B\x90\x28\x01\x00\x00\x8B\xCE\xFF\xD2\x84\xC0\x74\x2A\x8B\x06\x8B\x90\x68\x01\x00\x00\x8B\xCE\xFF\xD2\x84\xC0\x74\x2A\x8B\x0D");

		// ZombieBotLocomotion::ClimbUpToLedge (references the 168.0 climb height constant), WitchLocomotion vtable entry 46.
		// 53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC E8 01 00 00 A1 ? ? ? ? 33 C5 89 45 FC 8B 43 0C 56 8B F1 8B 0D ? ? ? ? 83 79 30 00 57 8B 7B 08
		DEFINE_SIG(SIG_ZOMBIEBOTLOCOMOTION_CLIMBUPTOLEDGE, "\x53\x8B\xDC\x83\xEC\x08\x83\xE4\xF0\x83\xC4\x04\x55\x8B\x6B\x04\x89\x6C\x24\x04\x8B\xEC\x81\xEC\xE8\x01\x00\x00\xA1\x2A\x2A\x2A\x2A\x33\xC5\x89\x45\xFC\x8B\x43\x0C\x56\x8B\xF1\x8B\x0D\x2A\x2A\x2A\x2A\x83\x79\x30\x00\x57\x8B\x7B\x08");

		// Prologue of WitchAttack::IsValidEnemy(CBaseEntity*), used to verify the patched call.
		// 55 8B EC 56 8B 75 08 85 F6 75 05 32 C0
		DEFINE_SIG(SIG_WITCHATTACK_ISVALIDENEMY, "\x55\x8B\xEC\x56\x8B\x75\x08\x85\xF6\x75\x05\x32\xC0");
	#endif
#endif

#endif // _SIGS_H_
