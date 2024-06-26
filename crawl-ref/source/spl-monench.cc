/**
 * @file
 * @brief Monster-affecting enchantment spells.
 *           Other targeted enchantments are handled in spl-zap.cc.
**/

#include "AppHdr.h"

#include "spl-monench.h"

#include "beam.h"
#include "coordit.h"
#include "english.h" // apostrophise
#include "env.h"
#include "fight.h"
#include "losglobal.h"
#include "message.h"
#include "spl-util.h"
#include "stringutil.h" // make_stringf
#include "terrain.h"
#include "view.h"
#include "viewchar.h"

int englaciate(coord_def where, int pow, actor *agent)
{
    actor *victim = actor_at(where);

    if (!victim || victim == agent)
        return 0;

    if (mons_aligned(agent, victim))
        return 0; // don't let monsters hit friendlies

    monster* mons = victim->as_monster();

    if (victim->is_stationary())
    {
        if (!mons)
            canned_msg(MSG_YOU_UNAFFECTED);
        else if (!mons_is_firewood(*mons))
            simple_monster_message(*mons, " is unaffected.");
        return 0;
    }

    const int rc = victim->res_cold();

    int duration = 1 + random2(2 + pow) - random2(victim->get_hit_dice());
    duration -= rc * 4;

    if ((!mons && you.get_mutation_level(MUT_COLD_BLOODED))
        || (mons && mons_class_flag(mons->type, M_COLD_BLOOD)))
    {
        duration *= 2;
    }

    // Guarantee a minimum duration.
    duration = max(duration, 2);

    if (!mons)
        return slow_player(duration);

    return do_slow_monster(*mons, agent, duration * BASELINE_DELAY);
}

spret cast_englaciation(int pow, bool fail)
{
    fail_check();
    mpr("You radiate an aura of cold.");
    apply_area_visible([pow] (coord_def where) {
        return englaciate(where, pow, &you);
    }, you.pos());
    return spret::success;
}

/** Corona a monster.
 *
 *  @param mons the monster to get a backlight.
 *  @returns true if it got backlit (even if it was already).
 */
bool backlight_monster(monster* mons)
{
    const mon_enchant bklt = mons->get_ench(ENCH_CORONA);
    const mon_enchant zin_bklt = mons->get_ench(ENCH_SILVER_CORONA);
    const int lvl = bklt.degree + zin_bklt.degree;

    mons->add_ench(mon_enchant(ENCH_CORONA, 1));

    if (lvl == 0)
        simple_monster_message(*mons, " is outlined in light.");
    else if (lvl == 4)
        simple_monster_message(*mons, " glows brighter for a moment.");
    else
        simple_monster_message(*mons, " glows brighter.");

    return true;
}

bool do_slow_monster(monster& mon, const actor* agent, int dur)
{
    if (mon.stasis())
        return true;

    if (!mon.is_stationary()
        && mon.add_ench(mon_enchant(ENCH_SLOW, 0, agent, dur)))
    {
        if (!mon.paralysed() && !mon.petrified()
            && simple_monster_message(mon, " seems to slow down."))
        {
            return true;
        }
    }

    return false;
}

bool enfeeble_monster(monster &mon, int pow)
{
    const int res_margin = mon.check_willpower(&you, pow);
    vector<enchant_type> hexes;

    if (mons_has_attacks(mon))
        hexes.push_back(ENCH_WEAK);
    if (mon.antimagic_susceptible())
        hexes.push_back(ENCH_ANTIMAGIC);
    if (res_margin <= 0)
    {
        hexes.push_back(ENCH_SLOW);
        hexes.push_back(ENCH_DAZED);
    }

    // Resisted the upgraded effects, and immune to the irresistible effects.
    if (hexes.empty())
    {
        return simple_monster_message(mon,
                   mon.resist_margin_phrase(res_margin).c_str());
    }

    const int max_extra_dur = div_rand_round(pow, 5);
    const int dur = 5 + random2(1 + max_extra_dur);

    for (auto hex : hexes)
    {
        if (mon.has_ench(hex))
        {
            mon_enchant ench = mon.get_ench(hex);
            ench.duration = max(dur * BASELINE_DELAY, ench.duration);
            mon.update_ench(ench);
        }
        else
            mon.add_ench(mon_enchant(hex, 0, &you, dur * BASELINE_DELAY));
    }

    if (res_margin > 0)
        simple_monster_message(mon, " partially resists.");

    return simple_monster_message(mon, " is enfeebled!");
}

spret cast_vile_clutch(int pow, bool fail, bool tracer)
{
    int range = spell_range(SPELL_BORGNJORS_VILE_CLUTCH, pow);
    auto hitfunc = find_spell_targeter(SPELL_BORGNJORS_VILE_CLUTCH, pow, range);
    bool (*vulnerable) (const actor *) = [](const actor * act) -> bool
    {
        return act->is_monster() && !act->as_monster()->is_insubstantial();
    };

    if (tracer)
    {
        for (radius_iterator ri(you.pos(), range, C_SQUARE, LOS_SOLID_SEE, true); ri; ++ri)
        {
            if (!in_bounds(*ri))
                continue;

            const monster* mon = monster_at(*ri);

            if (!mon || !you.can_see(*mon))
                continue;

            if (!mon->friendly() && (*vulnerable)(mon))
                return spret::success;
        }

        return spret::abort;
    }

    if (stop_attack_prompt(*hitfunc, "clutch", vulnerable))
        return spret::abort;

    fail_check();
    mpr("Decaying hands burst forth from the earth!");

    bolt beam;
    beam.name         = "vile clutch";
    beam.aux_source   = "vile_clutch";
    beam.flavour      = BEAM_VILE_CLUTCH;
    beam.glyph        = dchar_glyph(DCHAR_FIRED_BURST);
    beam.colour       = GREEN;
    beam.source_id    = MID_PLAYER;
    beam.thrower      = KILL_YOU;
    beam.is_explosion = true;
    beam.ex_size      = 2;
    beam.ench_power   = pow;
    beam.origin_spell = SPELL_BORGNJORS_VILE_CLUTCH;
    beam.loudness = 0;
    beam.explode(true, true);

    return spret::success;
}

string mons_simulacrum_immune_reason(const monster *mons)
{
    if (!mons || !you.can_see(*mons))
        return "You can't see anything there.";

    if (mons->has_ench(ENCH_SIMULACRUM) || mons->has_ench(ENCH_BOUND_SOUL))
    {
        return make_stringf("%s's soul is already gripped in ice!",
                            mons->name(DESC_THE).c_str());
    }

    if (!mons_can_be_spectralised(*mons))
        return "You can't make simulacra of that!";

    return "";
}

spret cast_simulacrum(coord_def target, int pow, bool fail)
{
    if (cell_is_solid(target))
    {
        canned_msg(MSG_UNTHINKING_ACT);
        return spret::abort;
    }

    monster* mons = monster_at(target);
    const string immune_reason = mons_simulacrum_immune_reason(mons);
    if (!immune_reason.empty())
    {
        mprf("%s", immune_reason.c_str());
        return spret::abort;
    }

    fail_check();
    int dur = 20 + random2(1 + div_rand_round(pow, 10));
    mprf("You freeze %s soul.", apostrophise(mons->name(DESC_THE)).c_str());
    mons->add_ench(mon_enchant(ENCH_SIMULACRUM, 0, &you, dur * BASELINE_DELAY));
    mons->props[SIMULACRUM_POWER_KEY] = pow;
    return spret::success;
}

bool start_ranged_constriction(actor& caster, actor& target, int duration,
                               constrict_type type)
{
    if (!caster.can_constrict(target, type))
        return false;

    if (target.is_player())
    {
        you.increase_duration(DUR_GRASPING_ROOTS, duration);
        caster.start_constricting(you);
        mprf(MSGCH_WARN, "The grasping roots grab you!");
    }
    else
    {
        enchant_type etype = (type == CONSTRICT_ROOTS ? ENCH_GRASPING_ROOTS
                                                      : ENCH_VILE_CLUTCH);
        auto ench = mon_enchant(etype, 0, &caster, duration * BASELINE_DELAY);
        target.as_monster()->add_ench(ench);
    }

    return true;
}

dice_def rimeblight_dot_damage(int pow)
{
    return dice_def(2, 3 + div_rand_round(pow, 3));
}

string describe_rimeblight_damage(int pow, bool terse)
{
    dice_def dot_damage = rimeblight_dot_damage(pow);
    dice_def shards_damage = zap_damage(ZAP_RIMEBLIGHT_SHARDS, pow, false, false);

    if (terse)
    {
        return make_stringf("%dd%d/%dd%d", dot_damage.num, dot_damage.size,
                                           shards_damage.num, shards_damage.size);
    }

    return make_stringf("%dd%d (primary target), %dd%d (explosion)",
                        dot_damage.num, dot_damage.size,
                        shards_damage.num, shards_damage.size);
}

bool apply_rimeblight(monster& victim, int power, bool quiet)
{
    if (victim.has_ench(ENCH_RIMEBLIGHT)
        || !(victim.holiness() & (MH_NATURAL | MH_DEMONIC | MH_NONLIVING)))
    {
        return false;
    }

    int duration = (random_range(5, 9) + div_rand_round(power, 3))
                    * BASELINE_DELAY;
    victim.add_ench(mon_enchant(ENCH_RIMEBLIGHT, 0, &you, duration));
    victim.props[RIMEBLIGHT_POWER_KEY] = power;
    victim.props[RIMEBLIGHT_TICKS_KEY] = random_range(0, 2);

    if (!quiet)
        simple_monster_message(victim, " is afflicted with rimeblight.");

    return true;
}

void do_rimeblight_explosion(coord_def pos, int power, int size)
{
    bolt shards;
    zappy(ZAP_RIMEBLIGHT_SHARDS, power, false, shards);
    shards.ex_size = size;
    shards.source_id     = MID_PLAYER;
    shards.thrower       = KILL_YOU_MISSILE;
    shards.origin_spell  = SPELL_RIMEBLIGHT;
    shards.target        = pos;
    shards.source        = pos;
    shards.hit_verb      = "hits";
    shards.aimed_at_spot = true;
    shards.explode();
}

void tick_rimeblight(monster& victim)
{
    const int pow = victim.props[RIMEBLIGHT_POWER_KEY].get_int();
    int ticks = victim.props[RIMEBLIGHT_TICKS_KEY].get_int();

    // Determine chance to explode with ice (rises over time)
    // Never happens below 3, always happens at 4, random chance beyond that
    if (ticks == 4 || ticks > 4 && x_chance_in_y(ticks, ticks + 12))
    {
        if (you.can_see(victim))
            mprf("Shards of ice erupt from the %s body!", victim.name(DESC_ITS).c_str());
        do_rimeblight_explosion(victim.pos(), pow, 1);
    }

    // Injury bond or some other effects may have killed us by now
    if (!victim.alive())
        return;

    // Apply direct AC-ignoring cold damage
    int dmg = rimeblight_dot_damage(pow).roll();
    dmg = resist_adjust_damage(&victim, BEAM_COLD, dmg);
    victim.hurt(&you, dmg, BEAM_COLD, KILLED_BY_FREEZING);

    // Increment how long rimeblight has been active
    if (victim.alive())
        victim.props[RIMEBLIGHT_TICKS_KEY] = (++ticks);
}
