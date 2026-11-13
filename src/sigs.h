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

/* CVomit::ActivateAbility(): m_nextSpray.Start(0.25) - delay between the start of the vomit and the
   first hit traces in CVomit::UpdateAbility. L4D2 only. The patched instruction reads the shared
   0.25 constant through an absolute address, which is redirected to our own value. */
#if SOURCE_ENGINE == SE_LEFT4DEAD2
	#if defined (_LINUX)
		#define SYM_CVOMIT_ACTIVATEABILITY "_ZN6CVomit15ActivateAbilityEv"

		// F3 0F 58 05 <abs32>		addss xmm0, dword ptr [0.25]	(Now() + 0.25)
		#define VOMIT_DELAY_PATCH_OFFSET 0x112
		DEFINE_SIG(VOMIT_DELAY_PATCH_OPCODE, "\xF3\x0F\x58\x05");
	#elif defined (_WIN32)
		// Calls IsAbilityReadyToFire (vtable +0x328), owner handle at +0x438; references "Vomit.Use".
		// 55 8B EC 51 57 8B F9 8B 07 8B 90 28 03 00 00 FF D2 84 C0 0F 84 ? ? ? ? 8B 87 38 04 00 00 83 F8 FF 0F 84 ? ? ? ? 8B 15
		DEFINE_SIG(SIG_CVOMIT_ACTIVATEABILITY, "\x55\x8B\xEC\x51\x57\x8B\xF9\x8B\x07\x8B\x90\x28\x03\x00\x00\xFF\xD2\x84\xC0\x0F\x84\x2A\x2A\x2A\x2A\x8B\x87\x38\x04\x00\x00\x83\xF8\xFF\x0F\x84\x2A\x2A\x2A\x2A\x8B\x15");

		// D8 05 <abs32>			fadd dword ptr [0.25]			(Now() + 0.25)
		#define VOMIT_DELAY_PATCH_OFFSET 0xBC
		DEFINE_SIG(VOMIT_DELAY_PATCH_OPCODE, "\xD8\x05");
	#endif

	#define VOMIT_DELAY_VANILLA 0.25f
#endif

/* CVomit::UpdateAbility(): vomit stream piercing survivors. L4D2 linux only.
   Every tick the human boomer's vomit stream is traced segment by segment; the first entity a segment
   hits gets OnVomitCollide() and the stream stops, so a survivor body-blocks the vomit for everyone
   standing behind them. The patch redirects the two interface pointer loads below to our proxies. */
#if SOURCE_ENGINE == SE_LEFT4DEAD2 && defined (_LINUX)
	#define SYM_ONVOMITCOLLIDE "_Z14OnVomitCollideP13CTerrorPlayerP11CBaseEntityS2_i6VectorS3_"
	#define SYM_CBASEENTITY_GETTEAMNUMBER "_ZNK11CBaseEntity13GetTeamNumberEv"
	#define SYM_ENGINETRACE "enginetrace"
	#define SYM_LAGCOMPENSATION "lagcompensation"

	// A1 <&enginetrace>		mov eax, enginetrace			(stream segment trace)
	#define VOMIT_PIERCE_TRACE_LOAD_OFFSET 0x45B
	// 8B 10				mov edx, [eax]
	#define VOMIT_PIERCE_TRACE_VTABLE_OFFSET 0x4DA
	// FF 52 14				call [edx+14h]					(IEngineTrace::TraceRay)
	#define VOMIT_PIERCE_TRACE_CALL_OFFSET 0x4F5
	// E8 <rel32>				call OnVomitCollide				(segment hit something)
	#define VOMIT_PIERCE_COLLIDE_CALL_OFFSET 0x9C7
	// A1 <&lagcompensation>	mov eax, lagcompensation		(end of the stream trace)
	#define VOMIT_PIERCE_FINISH_LOAD_OFFSET 0xA40
	// 8B 10				mov edx, [eax]
	#define VOMIT_PIERCE_FINISH_VTABLE_OFFSET 0xA4B
	// FF 52 04				call [edx+4]					(ILagCompensationManager::FinishLagCompensation)
	#define VOMIT_PIERCE_FINISH_CALL_OFFSET 0xA54

	// Bytes of CVomit::UpdateAbility read for verification.
	#define VOMIT_PIERCE_CODE_SIZE (VOMIT_PIERCE_FINISH_CALL_OFFSET + 3)
#endif

/* CTerrorPlayer::WantsLagCompensationOnEntity(): other entities are only rewound for a usercmd that
   holds IN_ATTACK / IN_ATTACK2, or at most 5 usercmds after the last weapon fire / melee swing start
   (m_iLastWeaponFireUsercmd). Usercmds are ticks, so the window is 166 ms at 30 tick but 50 ms at
   100 tick, while a shove keeps tracing for z_gun_swing_duration (0.2 s). L4D2 linux only. */
#if SOURCE_ENGINE == SE_LEFT4DEAD2 && defined (_LINUX)
	#define SYM_CTERRORPLAYER_WANTSLAGCOMPENSATIONONENTITY "_ZNK13CTerrorPlayer28WantsLagCompensationOnEntityEPK11CBaseEntityPK8CUserCmdPK7CBitVecILi2048EE"

	// 8B 4E 04				mov ecx, [esi+4]				(cmd->command_number)
	// 89 45 E4				mov [ebp-1Ch], eax
	// 8B 46 24				mov eax, [esi+24h]				(cmd->buttons)
	// 8B 97 8C 29 00 00		mov edx, [edi+298Ch]			(m_iLastWeaponFireUsercmd)
	// A8 01 75 09 F6 C4 08	test al, 1; jnz; test ah, 8	(IN_ATTACK / IN_ATTACK2)
	#define LAGCOMP_WINDOW_ANCHOR_OFFSET 0x15
	DEFINE_SIG(LAGCOMP_WINDOW_ANCHOR_BYTES, "\x8B\x4E\x04\x89\x45\xE4\x8B\x46\x24\x8B\x97\x8C\x29\x00\x00\xA8\x01\x75\x09\xF6\xC4\x08");

	// 29 D1				sub ecx, edx
	// 83 F9 05				cmp ecx, 5
	// 0F 8E <rel32>		jle <still lag compensate>
	#define LAGCOMP_WINDOW_PATCH_OFFSET 0xC0
	DEFINE_SIG(LAGCOMP_WINDOW_PATCH_BYTES, "\x29\xD1\x83\xF9\x05\x0F\x8E");
	#define LAGCOMP_WINDOW_IMMEDIATE_OFFSET (LAGCOMP_WINDOW_PATCH_OFFSET + 4)
	#define LAGCOMP_WINDOW_VANILLA_USERCMDS 5
#endif

/* Lag compensated hitboxes. L4D2 linux only.
   CLagCompensationManager::BacktrackEntity moves an entity back to where the attacker saw it. It only
   flushes the entity's bone cache when it also rewound the animation (LC_ANIMATION_CHANGED), and
   FinishLagCompensation never flushes it when the entity is put back. Bullets never rewind the animation:
   FireTerrorBullets asks for LAG_COMPENSATE_HITBOXES_ALONG_RAY but passes the default (zero) weapon
   position, angles and range, so only entities within 100 units of the map origin qualify. */
#if SOURCE_ENGINE == SE_LEFT4DEAD2 && defined (_LINUX)
	#define SYM_CLAGCOMPENSATIONMANAGER_BACKTRACKENTITY "_ZN23CLagCompensationManager15BacktrackEntityEP11CBaseEntityfP19CUtlFixedLinkedListI9LagRecordEPS3_S6_b"
	#define SYM_CLAGCOMPENSATIONMANAGER_FINISHLAGCOMPENSATION "_ZN23CLagCompensationManager21FinishLagCompensationEP11CBasePlayer"
	#define SYM_CLAGCOMPENSATIONMANAGER_RESTOREENTITYFROMRECORDS "_ZN23CLagCompensationManager24RestoreEntityFromRecordsEP11CBaseEntityP9LagRecordS3_b"
	#define SYM_CBASEANIMATING_INVALIDATEBONECACHE "_ZN14CBaseAnimating19InvalidateBoneCacheEv"
	#define SYM_FIRETERRORBULLETS "_Z17FireTerrorBulletsiRK6VectorRK6QAngle10CSWeaponIDiif"
	#define SYM_VEC3_ORIGIN "vec3_origin"
	#define SYM_VEC3_ANGLE "vec3_angle"

	// Change flags of a rewound entity: origin 100h, angles 200h, bounds 400h, animation 800h.
	#define LAGCOMP_CHANGE_ANIMATION 0x800
	#define LAGCOMP_CHANGE_ANY 0xF00

	// BacktrackEntity, after sv_lagflushbonecache was checked:
	// F7 85 E8 FE FF FF 00 08 00 00	test dword ptr [ebp-118h], 800h	(flags & LC_ANIMATION_CHANGED)
	// 0F 84 <rel32>				jz <no flush>
	#define LAGCOMP_FLUSH_TEST_OFFSET 0xEF8
	DEFINE_SIG(LAGCOMP_FLUSH_TEST_BYTES, "\xF7\x85\xE8\xFE\xFF\xFF\x00\x08\x00\x00\x0F\x84");
	#define LAGCOMP_FLUSH_MASK_OFFSET (LAGCOMP_FLUSH_TEST_OFFSET + 6)

	// 8B 45 0C 8B 4D 0C 8B 00		mov eax, [ebp+0Ch]; mov ecx, [ebp+0Ch]; mov eax, [eax]	(entity)
	// 89 0C 24 FF 90 F0 00 00 00	mov [esp], ecx; call [eax+0F0h]		(entity->GetBaseAnimating())
	// 85 C0 0F 84 <rel32>			test eax, eax; jz <no flush>
	#define LAGCOMP_FLUSH_GETANIMATING_OFFSET 0xF08
	DEFINE_SIG(LAGCOMP_FLUSH_GETANIMATING_BYTES, "\x8B\x45\x0C\x8B\x4D\x0C\x8B\x00\x89\x0C\x24\xFF\x90\xF0\x00\x00\x00\x85\xC0\x0F\x84");
	#define CBASEENTITY_GETBASEANIMATING_VTABLE_OFFSET 0xF0

	// 8B 45 0C 8B 55 0C 8B 00 89 14 24 FF 90 F0 00 00 00	(entity->GetBaseAnimating() again)
	// 89 04 24 E8 <rel32>			mov [esp], eax; call CBaseAnimating::InvalidateBoneCache
	#define LAGCOMP_FLUSH_INVALIDATE_OFFSET 0xF21
	DEFINE_SIG(LAGCOMP_FLUSH_INVALIDATE_BYTES, "\x8B\x45\x0C\x8B\x55\x0C\x8B\x00\x89\x14\x24\xFF\x90\xF0\x00\x00\x00\x89\x04\x24\xE8");
	#define LAGCOMP_FLUSH_INVALIDATE_CALL_OFFSET 0xF35
	#define LAGCOMP_FLUSH_CODE_SIZE (LAGCOMP_FLUSH_INVALIDATE_CALL_OFFSET + 5)

	// FinishLagCompensation, for every entity that was rewound:
	// C7 44 24 10 01 00 00 00		mov dword ptr [esp+10h], 1
	// 89 4C 24 0C 89 44 24 08		mov [esp+0Ch], ecx; mov [esp+8], eax	(change, restore records)
	// 89 54 24 04 89 34 24			mov [esp+4], edx; mov [esp], esi		(entity, this)
	// E8 <rel32>					call RestoreEntityFromRecords
	#define LAGCOMP_RESTORE_ARGS_OFFSET 0x15A
	DEFINE_SIG(LAGCOMP_RESTORE_ARGS_BYTES, "\xC7\x44\x24\x10\x01\x00\x00\x00\x89\x4C\x24\x0C\x89\x44\x24\x08\x89\x54\x24\x04\x89\x34\x24\xE8");
	#define LAGCOMP_RESTORE_CALL_OFFSET 0x171
	#define LAGCOMP_RESTORE_CODE_SIZE (LAGCOMP_RESTORE_CALL_OFFSET + 5)

	// FireTerrorBullets:
	// A1 <&lagcompensation>		mov eax, lagcompensation
	#define BULLET_LAGCOMP_LOAD_OFFSET 0x11C
	// 8B 10 89 04 24				mov edx, [eax]; mov [esp], eax
	// C7 44 24 14 00 00 00 00		mov dword ptr [esp+14h], 0			(weapon range 0.0)
	// C7 44 24 10 <&vec3_angle>	mov dword ptr [esp+10h], &vec3_angle	(weapon angles)
	#define BULLET_LAGCOMP_ARGS_OFFSET 0x121
	DEFINE_SIG(BULLET_LAGCOMP_ARGS_BYTES, "\x8B\x10\x89\x04\x24\xC7\x44\x24\x14\x00\x00\x00\x00\xC7\x44\x24\x10");
	#define BULLET_LAGCOMP_ANGLES_OPERAND_OFFSET 0x132
	// C7 44 24 0C <&vec3_origin>	mov dword ptr [esp+0Ch], &vec3_origin	(weapon position)
	#define BULLET_LAGCOMP_ORIGIN_OFFSET 0x136
	DEFINE_SIG(BULLET_LAGCOMP_ORIGIN_BYTES, "\xC7\x44\x24\x0C");
	#define BULLET_LAGCOMP_ORIGIN_OPERAND_OFFSET 0x13A
	// C7 44 24 08 02 00 00 00		mov dword ptr [esp+8], 2			(LAG_COMPENSATE_HITBOXES_ALONG_RAY)
	// 89 5C 24 04 FF 12			mov [esp+4], ebx; call [edx]		(StartLagCompensation(player, ...))
	#define BULLET_LAGCOMP_TYPE_OFFSET 0x13E
	DEFINE_SIG(BULLET_LAGCOMP_TYPE_BYTES, "\xC7\x44\x24\x08\x02\x00\x00\x00\x89\x5C\x24\x04\xFF\x12");
	#define BULLET_LAGCOMP_TYPE_IMMEDIATE_OFFSET (BULLET_LAGCOMP_TYPE_OFFSET + 4)
	#define BULLET_LAGCOMP_CODE_SIZE (BULLET_LAGCOMP_TYPE_OFFSET + BULLET_LAGCOMP_TYPE_BYTES_SIZE)
	#define LAG_COMPENSATE_HITBOXES 1
	#define LAG_COMPENSATE_HITBOXES_ALONG_RAY 2
#endif

/* engine: CGameClient::SetUpdateRate / CBaseClient::SetUpdateRate clamp every client's snapshot rate to
   100 per second. L4D2 linux only (engine_srv.so). */
#if SOURCE_ENGINE == SE_LEFT4DEAD2 && defined (_LINUX)
	#define SYM_CBASECLIENT_SETUPDATERATE "_ZN11CBaseClient13SetUpdateRateEib"
	#define SYM_CGAMECLIENT_SETUPDATERATE "_ZN11CGameClient13SetUpdateRateEib"

	// BA 64 00 00 00			mov edx, 100
	// 83 F8 64				cmp eax, 100
	// 0F 4E D0				cmovle edx, eax				(rate = min(rate, 100))
	// 8B 45 08				mov eax, [ebp+8]
	// F3 0F 2A CA			cvtsi2ss xmm1, edx
	// F3 0F 5E C1			divss xmm0, xmm1				(1.0 / rate)
	// F3 0F 11 80 20 02 00 00	movss [eax+220h], xmm0		(m_fSnapshotInterval)
	#define UPDATERATE_BASECLIENT_CLAMP_OFFSET 0x18
	DEFINE_SIG(UPDATERATE_BASECLIENT_CLAMP_BYTES, "\xBA\x64\x00\x00\x00\x83\xF8\x64\x0F\x4E\xD0\x8B\x45\x08\xF3\x0F\x2A\xCA\xF3\x0F\x5E\xC1\xF3\x0F\x11\x80\x20\x02\x00\x00");
	#define UPDATERATE_BASECLIENT_PATCH_SIZE 11
	#define UPDATERATE_BASECLIENT_CODE_SIZE (UPDATERATE_BASECLIENT_CLAMP_OFFSET + UPDATERATE_BASECLIENT_CLAMP_BYTES_SIZE)

	// 83 FA 64				cmp edx, 100					(rate after sv_min/maxupdaterate)
	// B8 64 00 00 00			mov eax, 100
	// 7F 07				jg +7						(-> rate = 100)
	// 39 F2 89 F0 0F 4D C2		cmp edx, esi; mov eax, esi; cmovge eax, edx
	#define UPDATERATE_GAMECLIENT_CLAMP_OFFSET 0x39
	DEFINE_SIG(UPDATERATE_GAMECLIENT_CLAMP_BYTES, "\x83\xFA\x64\xB8\x64\x00\x00\x00\x7F\x07\x39\xF2\x89\xF0\x0F\x4D\xC2");
	#define UPDATERATE_GAMECLIENT_PATCH_SIZE 10
	// E9 <rel32>				jmp CBaseClient::SetUpdateRate
	#define UPDATERATE_GAMECLIENT_JMP_OFFSET 0x59
	#define UPDATERATE_GAMECLIENT_CODE_SIZE (UPDATERATE_GAMECLIENT_JMP_OFFSET + 5)
#endif


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

		// Prologue of WitchAttack::IsValidEnemy(CBaseEntity*), used to verify the patched call.
		// 55 8B EC 56 8B 75 08 85 F6 75 05 32 C0
		DEFINE_SIG(SIG_WITCHATTACK_ISVALIDENEMY, "\x55\x8B\xEC\x56\x8B\x75\x08\x85\xF6\x75\x05\x32\xC0");
	#endif
#endif

#endif // _SIGS_H_
