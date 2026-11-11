## Tickrate Enabler Remastered

A fork of **Tickrate Enabler** (originally by ProdigySim, maintained at
[accelerator74/Tickrate-Enabler](https://github.com/accelerator74/Tickrate-Enabler)).
Everything the original does is kept; this fork adds **L4D2 fixes for common infected and the witch
on servers that lower `nb_update_frequency`** together with the tickrate.
See [What's new in this fork](#whats-new-in-this-fork).

For the original project, its history and issues, please refer to the
[upstream repository](https://github.com/accelerator74/Tickrate-Enabler).

Downloads: see the [Releases](../../releases) page of this repository (`.tar.gz` packages contain the
`addons` folder ready to drop into your server).

### What's new in this fork

L4D2 only (L4D1 builds behave exactly like upstream):

| | What | How to use |
|---|---|---|
| Fix | Infected collision push scaled with the NextBot update interval (commons jittering / wedging in corners, witch shoved off her path) | automatic |
| Fix | Witch no longer switches her victim to infected players she bumps into (and then sits down again after they die) | automatic |
| Fix | Witch anger from touching survivors no longer depends on tickrate / `nb_update_frequency` | automatic |
| Change | Boomer vomit hits survivors 0.05 s after the vomit starts instead of 0.25 s | automatic, `-vomitdelay <seconds>` to change |
| Change | Boomer vomit passes through survivors, so the survivors behind the first one get covered too (Linux) | automatic, `-novomitpierce` for vanilla |
| Fix | Above 100 tick every client gets one snapshot per tick: the engine's hard cap of 100 snapshots/s follows the tickrate (e.g. 108/s at 108 tick, 128/s at 128 tick) (Linux) | automatic, `-noupdateratefix` for vanilla |
| New | `te_reliable_sound` server command: plays a sound on the reliable stream so it can't be lost with a dropped packet (for plugins, e.g. spawn cues) | automatic, `-noreliablesound` to disable |
| Option | Shoves can keep lag compensating their targets longer than the vanilla 5 ticks (50 ms at 100 tick) (Linux) | off by default, `-lagcompwindow <seconds>` |

The NextBot fixes can be disabled with the `-nonextbotfixes` launch parameter. Details are in
[NextBot fixes](#nextbot-fixes-l4d2-only), [Boomer vomit delay](#boomer-vomit-delay-l4d2-only),
[Boomer vomit pierce](#boomer-vomit-pierce-l4d2-linux-only),
[Snapshot rate above 100 tick](#snapshot-rate-above-100-tick-l4d2-linux-only),
[Reliable sounds](#reliable-sounds-te_reliable_sound-l4d2-only) and
[Lag compensation window](#lag-compensation-window-l4d2-linux-only-opt-in) below.

### Description

Enables the "-tickrate" switch to be used from the command line to set the game's tickrate.

Patches Boomer Vomit behavior to fix an issue where vomit range scaled inversely with tickrate.

L4D2: fixes common infected / witch problems caused by a lowered `nb_update_frequency`
(see "NextBot fixes" below).

L4D2: shortens the delay between the start of the boomer's vomit and the first hits on survivors
(see "Boomer vomit delay" below).

L4D2 (Linux): the boomer's vomit stream passes through survivors instead of stopping at the first one
(see "Boomer vomit pierce" below).

L4D2 (Linux): above 100 tick, removes the engine's 100 snapshots per second cap so clients can get one
snapshot every tick (see "Snapshot rate above 100 tick" below).

Removes global upper-limits on the max client data rate (was 30000),
and removes the (30k) limits on rate control cvars:
- sv_maxrate
- sv_minrate
- net_splitpacket_maxrate

### Instructions

1. Place tickrate_enabler.dll (Windows) or tickrate_enabler.so (Linux) in your server's addons folder.
2. Place tickrate_enabler.vdf (L4D2) or tickrate_enabler_l4d1.vdf (L4D1) in your server's addons folder.
3. Add "-tickrate <desired_tickrate>" to your server's launch parameters. e.g. -tickrate 100

    Optional (L4D2): "-vomitdelay <seconds>" sets the boomer vomit hit delay, e.g. -vomitdelay 0.1
    (default 0.05, vanilla 0.25, allowed range 0-1).

    Optional (L4D2, Linux): "-novomitpierce" restores the vanilla vomit stream that stops at the first survivor.

    Optional (L4D2, Linux): "-noupdateratefix" keeps the engine's 100 snapshots/s cap above 100 tick.

    Optional (L4D2): "-noreliablesound" doesn't register the `te_reliable_sound` command.

    Optional (L4D2, Linux): "-lagcompwindow <seconds>" lengthens the shove lag compensation window
    (off by default, see below).
4. Make sure the following convar settings are properly set in server.cfg or otherwise
   (use your tickrate instead of 100; above 100 tick, L4D2 Linux, see "Snapshot rate above 100 tick"):

    sv_minupdaterate 100
    sv_maxupdaterate 100
    sv_mincmdrate 100
    sv_maxcmdrate 100
    fps_max 0
	sv_maxrate 0 // 0 = unlimited, but you can also set this manually. Default is 30k for 30tick, use math.
	// Setting higher sv_minrate can be used to force users to use higher data rates.
	
	Note that fps_max and sv_maxrate are hidden variables on most scenarios.

5. Done. Enjoy 100 tick L4D/L4D2 gameplay.

The `.vdf` loads `addons/tickrate_enabler/tickrate_enabler.so` (`.dll` on Windows), so keep the binary in
that folder. Everything the plugin reports (version, applied tickrate, each patch, failures) is printed to
the server console and appended to `addons/sourcemod/logs/tickrate_enabler.log` (or
`addons/tickrate_enabler.log` on servers without SourceMod). If that file stays empty, the plugin wasn't
loaded at all: check `plugin_print` (from the game as a SourceMod admin: `sm_rcon plugin_print`).

### NextBot fixes (L4D2 only)

Servers often lower `nb_update_frequency` (default 0.1) together with the tickrate so that
commons / witches update more often. Some NextBot code assumes the default 0.1s update interval:

- **Infected collision push** – `ZombieBotLocomotion::ResolveZombieCollisions` pushes overlapping
  infected apart by a fixed 3 units *per update*. At e.g. `nb_update_frequency 0.014` on 100 tick
  that is 10x stronger per second than vanilla, so commons jitter, wedge themselves into
  corners/walls and the witch gets shoved off her path when running through a horde.
  The push is now scaled with the real update interval (identical to vanilla at 0.1, never stronger).
- **Witch switching to infected victims** – `WitchAttack::OnContact` lets the witch switch to another
  player she keeps bumping into, but validated the *old* victim instead of the new one, so she could
  start chasing a charger/jockey/etc. When that infected died she ended the attack and sat down again.
  More frequent updates create many more contact events, which made this common. She now only
  switches to alive survivors.
- **Witch contact anger** – `WitchAngry::OnContact` adds `z_witch_hostile_at_me_anger * z_witch_anger_rate *
  frametime` per contact event, but contact events happen once per NextBot update, not per frame. The
  rage gained from bumping into her therefore depended on both tickrate and `nb_update_frequency`
  (3x slower at 100 tick with the default 0.1, 3x faster at 100 tick with one-tick updates). It now
  always matches vanilla 30 tick / `nb_update_frequency 0.1`.

The fixes are applied automatically; a fix that can't be located after a game update is skipped
with a console message (the rest of the plugin keeps working). Disable them with `-nonextbotfixes`.

### Boomer vomit delay (L4D2 only)

When a boomer vomits, `CVomit::ActivateAbility` starts the vomit effect, the sound and the vomit duration
immediately, but schedules the first hit traces only 0.25 s later (hard-coded). Survivors therefore see
and hear the vomit noticeably before they actually get covered, which on top of the network latency
feels unresponsive. Once the hits start, the vomit stream is traced instantly (with lag compensation),
so this fixed delay is the main source of the lag.

The delay is now 0.05 s by default. Set your own value with the launch parameter
`-vomitdelay <seconds>` (0-1), e.g. `-vomitdelay 0.25` restores vanilla behaviour. Note that the
vanilla delay is also the window in which survivors can shove the boomer before the vomit lands.
The chosen value is printed in the server console on load; if the code can't be located after a game
update, the vanilla delay is kept and a console message explains why.

### Boomer vomit pierce (L4D2, Linux only)

While a (human) boomer vomits, `CVomit::UpdateAbility` traces the vomit stream every tick from the boomer's
mouth along a short arc, segment by segment, and stops at the first thing a segment hits. Survivors are
"things" too, so the survivor in front absorbs the whole stream: a survivor standing right behind them only
gets covered when the small random spread of the stream happens to miss the one in front. This is vanilla
behaviour (the vomit delay change above doesn't affect it), but it makes survivors body-block bile for each
other.

With this patch the stream passes through survivors: every survivor along the stream is vomited on
exactly like the one the stream would have stopped at (same `OnVomitCollide` call, still lag compensated,
at most once per survivor per tick), and only the world and props stop it. Infected and ghosts were
already ignored by the stream. Bot boomers spray physical vomit particles instead of tracing a stream and
are not affected.

Enabled by default; `-novomitpierce` restores the vanilla behaviour. Linux servers only. If the code
can't be located after a game update, the vanilla behaviour is kept and a console message explains why.

### Snapshot rate above 100 tick (L4D2, Linux only)

The engine clamps every client's update rate (snapshots per second) to 100, no matter what
`sv_minupdaterate` / `sv_maxupdaterate` say: once in `CGameClient::SetUpdateRate` and again in
`CBaseClient::SetUpdateRate`. Up to 100 tick that is one snapshot per tick. Above 100 tick the snapshot
interval (10 ms) is longer than a tick (9.26 ms at 108 tick), so snapshots skip a tick every now and then
(108 tick: 100 of 108 ticks) and players see the world in uneven steps.

The patch raises both clamps to the tickrate, rounded up (108 at 108 tick, 128 at 128 tick, at most 1000).
The `sv_minupdaterate` / `sv_maxupdaterate` cvars work as before, so set both to your tickrate to send
one snapshot per tick to everyone:

    sv_minupdaterate 108
    sv_maxupdaterate 108

At 100 tick or below nothing is patched. `-noupdateratefix` keeps the engine's cap. The applied cap is
written to the log; if the code can't be located after a game update, the cap stays at 100 and a console
message explains why.

### Reliable sounds (te_reliable_sound, L4D2 only)

Every sound the game or SourceMod plays is unreliable: it is sent once, inside the next snapshot, and a
lost packet loses it for good. `te_reliable_sound` makes the engine send the sound on the reliable stream
(retransmitted until it arrives) to each recipient right away:

    te_reliable_sound <entity> <channel 0-7> <soundlevel 0-255> <volume 0-1> <pitch 1-255> <flags> <x,y,z|-> "<sample>" [client,client,...]

`entity` is the source (0 = world), `-` instead of an origin uses the entity's own position, and the
optional client list (indices) limits the recipients; by default every connected human and SourceTV get
it. The arguments match `EmitSound` / a SourceMod normal sound hook, so a hooked sound can be re-emitted
unchanged, only reliably. Only the server (console, rcon, plugins via `ServerCommand`) can run it.

- The sample must already be precached (the command never precaches; an unknown sample is rejected with
  a console message). Put it in quotes, as above.
- To make a hooked sound reliable, block it in the hook (`Plugin_Stop`) and run `te_reliable_sound` on the
  next frame. Returning `Plugin_Changed` from a SourceMod sound hook re-emits the sound through SourceMod,
  which is never reliable.
- `-noreliablesound` doesn't register the command.

### Lag compensation window (L4D2, Linux only, opt-in)

`CTerrorPlayer::WantsLagCompensationOnEntity` decides which players and common infected get rewound to
where the attacker saw them. It rewinds while the usercmd holds attack / shove, and for **5 usercmds**
after the last weapon fire or shove start (logic inherited from CS:S). A usercmd is one tick, so that is
166 ms at 30 tick, 50 ms at 100 tick and 46 ms at 108 tick. Only gun fire and shoves (including the shove
with a melee weapon) set that timestamp; melee swings, claws, the tank's punch and vomit are compensated
only while the button is held. A shove keeps tracing its arc for `z_gun_swing_duration` (0.2 s), so with a
tapped shove at high tick, everything after the first ~50 ms is traced against un-rewound positions.

This is vanilla behaviour and competitive players are used to it, so **nothing changes by default**.
`-lagcompwindow <seconds>` (0.01-1) sets the window after a shove / shot: it is converted to usercmds at the
current tickrate (never fewer than the vanilla 5, at most 127), e.g. `-lagcompwindow 0.1` gives 11 usercmds
at 108 tick. The applied window is written to the log. Linux servers only.

### Changelog / TODO

1.8.0 (Remastered fork):
- L4D2 (Linux): above 100 tick the engine's 100 snapshots/s cap follows the tickrate, so clients can get
  one snapshot per tick (`-noupdateratefix` for vanilla).
- L4D2: `te_reliable_sound` server command for reliable (packet-loss proof) sounds (`-noreliablesound`
  to disable).
- L4D2 (Linux): optional longer lag compensation window after shoves / shots (`-lagcompwindow <seconds>`,
  off by default).
- L4D2 (Linux): boomer vomit passes through survivors instead of stopping at the first one
  (`-novomitpierce` launch parameter for vanilla).
- Messages are also written to `addons/sourcemod/logs/tickrate_enabler.log` (or `addons/tickrate_enabler.log`),
  including the applied tickrate, or why the default one was kept.
- Patch failures no longer use the engine's fatal `Error()`; they are logged and the plugin is not loaded.
- Linux: reading game memory for patch verification falls back to a pipe when `process_vm_readv` is blocked
  (seccomp / container profiles), instead of failing all patches.

1.7.3 (Remastered fork, not released separately; included in 1.8.0):
- Safely reject stale boomer vomit patch offsets and unreadable or foreign-module operands.
- Fixed convar maximum restoration on unload: only restore defaults captured during the current load.
- Basic binary patch objects now restore original bytes on destruction and cannot be copied.
- L4D2: guard NextBot scaling against non-finite values, unsafe integer conversions and overflow.
- Preserve fractional `-tickrate` values (e.g. `108.5`) and ignore non-finite tickrates.
- Replaced unaligned pointer writes in boomer patch buffers with `memcpy` and removed the obsolete
  `register` keyword from the pattern scanner.
- Added 32-bit Linux regression tests with undefined-behavior checks.
- CI now builds Linux packages only; Windows was removed from the build matrix.

1.7.2 (Remastered fork):
- Removed the `te_witch_climb_rate` witch climbing tweak (it didn't work reliably).
- L4D2: boomer vomit hits start 0.05 s after the vomit instead of the hard-coded 0.25 s
  (`-vomitdelay <seconds>` launch parameter).

1.7.1 (Remastered fork):
- First public release of the fork (1.7.0 was tagged, but its Linux build failed, so it was never released).
- Linux build fix: older clang no longer fails on SDK warning flags it doesn't know.

1.7.0 (Remastered fork):
- L4D2: NextBot fixes for lowered `nb_update_frequency` (infected collision push, witch victim switching,
  witch contact anger).
- L4D2: `te_witch_climb_rate` cvar to speed up witch ledge climbing.

1.5: Contributed by Accelerator  
- Fixed CVomit::UpdateAbility offsets.  
- Some code optimization  

1.4: Contributed by $atanic $pirit  
- Fixed L4D2 CVomit::UpdateAbility Windows offsets.
- Fixed l4d Linux offsets.(Thanks to epzminion.) 
	
1.3:
- Fixed L4D1 CVomit::UpdateAbility linux offsets.  

1.2: thanks to vintik  
- Fixed maxrate patching on Windows (L4D1+2)  
- Added an extra maxrate patch on Linux that probably does nothing.  

1.1:
- Patches 2 hard coded max rate limits   
- Removes CVar Max on sv_max/minrate, net_splitpacket_maxrate  
- Some code reorganization  

1.0:
- Patches boomer vomit to behave properly on modified high tickrates  
- Code cleaner perhaps  

0.1: 
- -tickrate only pseudo-release  

TODO:
- Investigate other bugs  
- Remove 30K rate limit from HLTV clients.  

### Credits / License

tickrate_enabler is copyright Michael "ProdigySim" Busby 2012

Upstream maintenance: Accelerator and contributors – https://github.com/accelerator74/Tickrate-Enabler

NextBot fixes in this fork were researched by reverse engineering the L4D2 dedicated
server binaries; the witch update throttling idea comes from the "witch_update_nbm" plugin (Lux, Kether).

Parts of this project contain GPLv3 code adapted from SourceMod (Allied Modders L.L.C.) and Left4Downtown2 (Igor Smirnov et. al.).

This entire project is released under the AlliedModders modified GPLv3.
