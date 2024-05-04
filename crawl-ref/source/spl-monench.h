#pragma once

#include "random.h"
#include "spl-cast.h"

struct bolt;
class actor;
class monster;

#define VILE_CLUTCH_POWER_KEY "vile_clutch_power"
#define FASTROOT_POWER_KEY "fastroot_power"
#define RIMEBLIGHT_POWER_KEY "rimeblight_power"
#define RIMEBLIGHT_TICKS_KEY "rimeblight_ticks"
#define RIMEBLIGHT_DEATH_KEY "death_by_rimeblight"

int englaciate(coord_def where, int pow, actor *agent);
spret cast_englaciation(int pow, bool fail);
bool backlight_monster(monster* mons);

//returns true if it slowed the monster
bool do_slow_monster(monster& mon, const actor *agent, int dur = 0);
bool enfeeble_monster(monster &mon, int pow);
string mons_simulacrum_immune_reason(const monster *mons);
spret cast_simulacrum(coord_def target, int pow, bool fail);
spret cast_vile_clutch(int pow, bool fail, bool tracer = false);
void grasp_with_roots(actor &caster, actor &target, int turns);

dice_def rimeblight_dot_damage(int pow);
string describe_rimeblight_damage(int pow, bool terse);
void do_rimeblight_explosion(coord_def pos, int power, int size);
bool apply_rimeblight(monster& victim, int power, bool quiet = false);
void tick_rimeblight(monster& victim);
