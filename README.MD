## Tickrate Enabler Remastered

A fork of **Tickrate Enabler** (originally by ProdigySim, maintained at
[accelerator74/Tickrate-Enabler](https://github.com/accelerator74/Tickrate-Enabler)).
Everything the original does is kept; this fork adds **L4D2 fixes for common infected and the witch
on servers that lower `nb_update_frequency`** together with the tickrate, plus an optional witch climbing
speed tweak. See [What's new in this fork](#whats-new-in-this-fork).

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
| Tweak | Faster witch ledge climbing | `te_witch_climb_rate 5` (default `1` = vanilla) |

All fixes can be disabled with the `-nonextbotfixes` launch parameter. Details are in
[NextBot fixes](#nextbot-fixes-l4d2-only) below.

### Description

Enables the "-tickrate" switch to be used from the command line to set the game's tickrate.

Patches Boomer Vomit behavior to fix an issue where vomit range scaled inversely with tickrate.

L4D2: fixes common infected / witch problems caused by a lowered `nb_update_frequency`
(see "NextBot fixes" below).

Removes global upper-limits on the max client data rate (was 30000),
and removes the (30k) limits on rate control cvars:
- sv_maxrate
- sv_minrate
- net_splitpacket_maxrate

### Instructions

1. Place tickrate_enabler.dll (Windows) or tickrate_enabler.so (Linux) in your server's addons folder.
2. Place tickrate_enabler.vdf (L4D2) or tickrate_enabler_l4d1.vdf (L4D1) in your server's addons folder.
3. Add "-tickrate <desired_tickrate>" to your server's launch parameters. e.g. -tickrate 100
4. Make sure the following convar settings are properly set in server.cfg or otherwise:

    sv_maxupdaterate 100
    sv_maxcmdrate 100
    fps_max 0
	sv_maxrate 0 // 0 = unlimited, but you can also set this manually. Default is 30k for 30tick, use math.
	// Setting higher sv_minrate can be used to force users to use higher data rates.
	
	Note that fps_max and sv_maxrate are hidden variables on most scenarios.

5. Done. Enjoy 100 tick L4D/L4D2 gameplay.

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

Optional gameplay tweak (off by default):

- `te_witch_climb_rate <1-10>` (default 1 = vanilla) – speeds up the witch's ledge climbing
  (animation and movement together), e.g. `te_witch_climb_rate 5`. Only witches are affected.
  Vanilla climbing is slow and leaves her an easy target.

The fixes are applied automatically; a fix that can't be located after a game update is skipped
with a console message (the rest of the plugin keeps working). Disable them with `-nonextbotfixes`.

### Changelog / TODO

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

NextBot fixes and witch tweaks in this fork were researched by reverse engineering the L4D2 dedicated
server binaries; the witch update throttling idea comes from the "witch_update_nbm" plugin (Lux, Kether).

Parts of this project contain GPLv3 code adapted from SourceMod (Allied Modders L.L.C.) and Left4Downtown2 (Igor Smirnov et. al.).

This entire project is released under the AlliedModders modified GPLv3.
