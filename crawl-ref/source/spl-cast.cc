/**
 * @file
 * @brief Spell casting and miscast functions.
**/

#include "AppHdr.h"

#include "spl-cast.h"

#include <iomanip>
#include <sstream>
#include <cmath>

#include "act-iter.h"
#include "areas.h"
#include "art-enum.h"
#include "beam.h"
#include "chardump.h"
#include "cloud.h"
#include "colour.h"
#include "coordit.h"
#include "database.h"
#include "describe.h"
#include "directn.h"
#include "english.h"
#include "env.h"
#include "evoke.h"
#include "exercise.h"
#include "format.h"
#include "god-abil.h"
#include "god-conduct.h"
#include "god-item.h"
#include "god-passive.h" // passive_t::shadow_spells
#include "hints.h"
#include "items.h"
#include "item-prop.h"
#include "item-use.h"
#include "libutil.h"
#include "macro.h"
#include "menu.h"
#include "message.h"
#include "misc.h"
#include "mon-behv.h"
#include "mon-cast.h"
#include "mon-explode.h"
#include "mon-place.h"
#include "mon-project.h"
#include "mon-util.h"
#include "mutation.h"
#include "options.h"
#include "ouch.h"
#include "output.h"
#include "player.h"
#include "player-stats.h"
#include "prompt.h"
#include "religion.h"
#include "shout.h"
#include "skills.h"
#include "spl-book.h"
#include "spl-clouds.h"
#include "spl-damage.h"
#include "spl-goditem.h"
#include "spl-miscast.h"
#include "spl-monench.h"
#include "spl-other.h"
#include "spl-selfench.h"
#include "spl-summoning.h"
#include "spl-transloc.h"
#include "spl-wpnench.h"
#include "spl-zap.h"
#include "state.h"
#include "stepdown.h"
#include "stringutil.h"
#include "tag-version.h"
#include "target.h"
#include "teleport.h"
#include "terrain.h"
#include "tilepick.h"
#include "transform.h"
#include "unicode.h"
#include "unwind.h"
#include "view.h"
#include "viewchar.h" // stringize_glyph

static int _spell_enhancement(spell_type spell);
static int _apply_enhancement(const int initial_power,
                              const int enhancer_levels);

void surge_power(const int enhanced)
{
    if (enhanced)               // one way or the other {dlb}
    {
        const string modifier = (enhanced  < -2) ? "extraordinarily" :
                                (enhanced == -2) ? "extremely" :
                                (enhanced ==  2) ? "strong" :
                                (enhanced  >  2) ? "huge"
                                                 : "";
        mprf("You feel %s %s",
             !modifier.length() ? "a"
                                : article_a(modifier).c_str(),
             (enhanced < 0) ? "numb sensation."
                            : "surge of power!");
    }
}

void surge_power_wand(const int mp_cost)
{
    if (mp_cost)
    {
        const bool slight = mp_cost < 3;
        mprf("You feel a %ssurge of power%s",
             slight ? "slight " : "",
             slight ? "."      : "!");
    }
}

static string _spell_base_description(spell_type spell, bool viewing)
{
    ostringstream desc;

    int highlight =  spell_highlight_by_utility(spell, COL_UNKNOWN, !viewing);

    desc << "<" << colour_to_str(highlight) << ">" << left;

    // spell name
    desc << chop_string(spell_title(spell), 30);

    // spell schools
    desc << spell_schools_string(spell);

    const int so_far = strwidth(desc.str()) - (strwidth(colour_to_str(highlight))+2);
    if (so_far < 60)
        desc << string(60 - so_far, ' ');
    desc << "</" << colour_to_str(highlight) <<">";

    // spell level
    desc << spell_difficulty(spell);
    desc << " ";

    return desc.str();
}

static string _spell_extra_description(spell_type spell, bool viewing)
{
    ostringstream desc;

    int highlight =  spell_highlight_by_utility(spell, COL_UNKNOWN, !viewing);

    desc << "<" << colour_to_str(highlight) << ">" << left;

    // spell name
    desc << chop_string(spell_title(spell), 30);

    // spell power, spell range, noise
    const string rangestring = spell_range_string(spell);
    const string damagestring = spell_damage_string(spell);

    desc << chop_string(spell_power_string(spell), 10)
         << chop_string(damagestring.length() ? damagestring : "N/A", 10)
         << chop_string(rangestring, 10)
         << chop_string(spell_noise_string(spell, 10), 14);

    desc << "</" << colour_to_str(highlight) <<">";

    return desc.str();
}

class SpellMenuEntry : public ToggleableMenuEntry
{
public:
    SpellMenuEntry(const string &txt,
                   const string &alt_txt,
                   MenuEntryLevel lev,
                   int qty, int hotk)
        : ToggleableMenuEntry(txt, alt_txt, lev, qty, hotk)
    {
    }

    bool preselected = false;
protected:
    virtual string _get_text_preface() const override
    {
        if (preselected)
            return make_stringf(" %s + ", keycode_to_name(hotkeys[0]).c_str());
        return ToggleableMenuEntry::_get_text_preface();
    }
};

class SpellMenu : public ToggleableMenu
{
public:
    SpellMenu()
        : ToggleableMenu(MF_SINGLESELECT | MF_ANYPRINTABLE
            | MF_NO_WRAP_ROWS | MF_ALLOW_FORMATTING
            | MF_ARROWS_SELECT | MF_INIT_HOVER) {}
protected:
    bool process_command(command_type c) override
    {
        get_selected(&sel);
        // if there's a preselected item, and no current selection, select it.
        // for arrow selection, the hover starts on the preselected item so no
        // special handling is needed.
        if (menu_action == ACT_EXECUTE && c == CMD_MENU_SELECT
            && !(flags & MF_ARROWS_SELECT) && sel.empty())
        {
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (static_cast<SpellMenuEntry*>(items[i])->preselected)
                {
                    select_index(i, 1);
                    break;
                }
            }
        }
        return ToggleableMenu::process_command(c);
    }

    bool examine_index(int i) override
    {
        ASSERT(i >= 0 && i < static_cast<int>(items.size()));
        if (items[i]->hotkeys.size())
            describe_spell(get_spell_by_letter(items[i]->hotkeys[0]), nullptr);
        return true;
    }
};

// selector is a boolean function that filters spells according
// to certain criteria. Currently used for Tiles to distinguish
// spells targeted on player vs. spells targeted on monsters.
int list_spells(bool toggle_with_I, bool viewing, bool allow_preselect,
                const string &action)
{
    if (toggle_with_I && get_spell_by_letter('I') != SPELL_NO_SPELL)
        toggle_with_I = false;

    const string real_action = viewing ? "describe" : action;

    SpellMenu spell_menu;
    const string titlestring = make_stringf("%-25.25s",
            make_stringf("Your spells (%s)", real_action.c_str()).c_str());

    {
        ToggleableMenuEntry* me =
            new ToggleableMenuEntry(
                titlestring + "         Type                          Level",
                titlestring + "         Power     Damage    Range     Noise         ",
                MEL_TITLE);
        spell_menu.set_title(me, true, true);
    }
    spell_menu.set_highlighter(nullptr);
    spell_menu.set_tag("spell");
    // TODO: add toggling to describe mode with `?`, add help string, etc...
    spell_menu.add_toggle_from_command(CMD_MENU_CYCLE_MODE);
    spell_menu.add_toggle_from_command(CMD_MENU_CYCLE_MODE_REVERSE);

    string more_str = make_stringf("<lightgrey>Select a spell to %s</lightgrey>",
        real_action.c_str());
    string toggle_desc = menu_keyhelp_cmd(CMD_MENU_CYCLE_MODE);
    if (toggle_with_I)
    {
        // why `I`?
        spell_menu.add_toggle_key('I');
        toggle_desc += "/[<w>I</w>]";
    }
    toggle_desc += " toggle spell headers";
    more_str = pad_more_with(more_str, toggle_desc);
    spell_menu.set_more(formatted_string::parse_string(more_str));
    // TODO: should allow toggling between execute and examine
    spell_menu.menu_action = viewing ? Menu::ACT_EXAMINE : Menu::ACT_EXECUTE;

    int initial_hover = 0;
    for (int i = 0; i < 52; ++i)
    {
        const char letter = index_to_letter(i);
        const spell_type spell = get_spell_by_letter(letter);

        if (!is_valid_spell(spell))
            continue;

        SpellMenuEntry* me =
            new SpellMenuEntry(_spell_base_description(spell, viewing),
                               _spell_extra_description(spell, viewing),
                               MEL_ITEM, 1, letter);
        me->colour = spell_highlight_by_utility(spell, COL_UNKNOWN, !viewing);
        // TODO: maybe fill this from the quiver if there's a quivered spell and
        // no last cast one?
        if (allow_preselect && you.last_cast_spell == spell)
        {
            initial_hover = i;
            me->preselected = true;
        }

        me->add_tile(tile_def(tileidx_spell(spell)));
        spell_menu.add_entry(me);
    }
    spell_menu.set_hovered(initial_hover);

    int choice = 0;
    spell_menu.on_single_selection = [&choice](const MenuEntry& item)
    {
        ASSERT(item.hotkeys.size() == 1);
        choice = item.hotkeys[0];
        return false;
    };

    spell_menu.show();
    if (!crawl_state.doing_prev_cmd_again)
    {
        redraw_screen();
        update_screen();
    }
    return choice;
}

// Effects that happen after spells which are otherwise simple zaps.
static void _apply_post_zap_effect(spell_type spell)
{
    switch (spell)
    {
    case SPELL_KISS_OF_DEATH:
        drain_player(20, true, true);
        break;
    default:
        break;
    }
}

static int _skill_power(spell_type spell)
{
    // average of spell schools, plus spellcasting
    int power = 0;
    const spschools_type disciplines = get_spell_disciplines(spell);
    const int skillcount = count_bits(disciplines);
    if (skillcount)
    {
        for (const auto bit : spschools_type::range())
            if (disciplines & bit)
                power += you.skill(spell_type2skill(bit));
        power /= skillcount;
    }
    return power + you.skill(SK_SPELLCASTING);
}

/*
 * Calculate spell power.
 *
 * @param spell         the spell to check
 *
 * @return the resulting spell power.
 */
int calc_spell_power(spell_type spell)
{
    int power = _skill_power(spell);

    if (you.divine_exegesis)
        power += you.skill(SK_INVOCATIONS);

    // [dshaligram] Enhancers don't affect fail rates any more, only spell
    // power. Note that this does not affect Vehumet's boost in castability.
    power = _apply_enhancement(power, _spell_enhancement(spell));

    // Wild magic boosts spell power, subdued magic decreases it.
    power += you.get_mutation_level(MUT_WILD_MAGIC);
    power -= you.get_mutation_level(MUT_SUBDUED_MAGIC);

    // Augmentation boosts spell power at high HP.
    power += augmentation_amount();

    // Each level of horror reduces spellpower by 10%
    if (you.duration[DUR_HORROR])
    {
        power *= 10;
        power /= 10 + (you.props[HORROR_PENALTY_KEY].get_int() * 3) / 2;
    }

    const int cap = spell_power_cap(spell);
    if (cap > 0)
        power = min(power, cap);

    return power;
}

static int _spell_enhancement(spell_type spell)
{
    const spschools_type typeflags = get_spell_disciplines(spell);
    int enhanced = 0;

    if (typeflags & spschool::enchantments)
        enhanced += player_spec_conj();

    if (typeflags & spschool::hexes)
        enhanced += player_spec_hex();

    if (typeflags & spschool::summoning)
        enhanced += player_spec_summ();

    if (typeflags & spschool::poison)
        enhanced += player_spec_poison();

    if (typeflags & spschool::necromancy)
        enhanced += player_spec_death();

    if (typeflags & spschool::translocation)
        enhanced += player_spec_tloc();

    if (typeflags & spschool::transmutation)
        enhanced += player_spec_tmut();

    if (typeflags & spschool::fire)
        enhanced += player_spec_fire();

    if (typeflags & spschool::ice)
        enhanced += player_spec_cold();

    if (typeflags & spschool::earth)
        enhanced += player_spec_earth();

    if (typeflags & spschool::air)
        enhanced += player_spec_air();

    if (you.form == transformation::shadow)
        enhanced -= 2;

    if (player_equip_unrand(UNRAND_BATTLE))
    {
        if (vehumet_supports_spell(spell))
            enhanced++;
        else
            enhanced--;
    }

    enhanced += you.archmagi();
    enhanced += you.duration[DUR_BRILLIANCE] > 0
                || player_equip_unrand(UNRAND_FOLLY);

    // These are used in an exponential way, so we'll limit them a bit. -- bwr
    if (enhanced > 3)
        enhanced = 3;
    else if (enhanced < -3)
        enhanced = -3;

    return enhanced;
}

/**
 * Apply the effects of spell enhancers (and de-enhancers) on spellpower.
 *
 * @param initial_power     The power of the spell before enhancers are added.
 * @param enhancer_levels   The number of enhancements levels to apply.
 * @return                  The power of the spell with enhancers considered.
 */
static int _apply_enhancement(const int initial_power,
                              const int enhancer_levels)
{
    int power = initial_power;

    return power + enhancer_levels;
}

void inspect_spells()
{
    if (!you.spell_no)
    {
        canned_msg(MSG_NO_SPELLS);
        return;
    }

    list_spells(true, true);
}

/**
 * Can the player cast any spell at all? Checks for things that limit
 * spellcasting regardless of the specific spell we want to cast.
 *
 * @param quiet    If true, don't print a reason why no spell can be cast.
 * @return True if we could cast a spell, false otherwise.
*/
bool can_cast_spells(bool quiet)
{
    if (!get_form()->can_cast)
    {
        if (!quiet)
            canned_msg(MSG_PRESENT_FORM);
        return false;
    }

    if (you.duration[DUR_WATER_HOLD] && !you.res_water_drowning())
    {
        if (!quiet)
            mpr("You cannot cast spells while unable to breathe!");
        return false;
    }

    if (you.duration[DUR_BRAINLESS])
    {
        if (!quiet)
            mpr("You lack the mental capacity to cast spells.");
        return false;
    }

    if (you.duration[DUR_NO_CAST])
    {
        if (!quiet)
            mpr("You are unable to access your magic!");
        return false;
    }

    // Randart weapons.
    if (you.no_cast())
    {
        if (!quiet)
            mpr("Something interferes with your magic!");
        return false;
    }

    if (you.berserk())
    {
        if (!quiet)
            canned_msg(MSG_TOO_BERSERK);
        return false;
    }

    if (you.confused())
    {
        if (!quiet)
            mpr("You're too confused to cast spells.");
        return false;
    }

    if (silenced(you.pos()))
    {
        if (!quiet)
            mpr("You cannot cast spells when silenced!");
        // included in default force_more_message
        return false;
    }

    return true;
}

void do_cast_spell_cmd(bool force)
{
    if (cast_a_spell(!force) == spret::abort)
        flush_input_buffer(FLUSH_ON_FAILURE);
}

static void _handle_channeling(int cost, spret cast_result)
{
    if (you.has_mutation(MUT_HP_CASTING) || cast_result == spret::abort)
        return;

    const int sources = player_channeling();
    if (!sources)
        return;

    // Miscasts always get refunded, successes only sometimes do.
    if (cast_result != spret::fail && !x_chance_in_y(sources, 5))
        return;

    mpr("Magical energy flows into your mind!");
    inc_mp(cost, true);
    did_god_conduct(DID_WIZARDLY_ITEM, 10);
}

/**
 * Let the Majin-Bo congratulate you on casting a spell while using it.
 *
 * @param spell     The spell just successfully cast.
 */
static void _majin_speak(spell_type spell)
{
    // since this isn't obviously mental communication, let it be silenced
    if (silenced(you.pos()))
        return;

    const int level = spell_difficulty(spell);
    const bool weak = level <= 4;
    const string lookup = weak ? "majin-bo cast weak" : "majin-bo cast";
    const string msg = "A voice whispers, \"" + getSpeakString(lookup) + "\"";
    mprf(MSGCH_TALK, "%s", msg.c_str());
}

static bool _majin_charge_hp()
{
    return player_equip_unrand(UNRAND_MAJIN) && !you.duration[DUR_DEATHS_DOOR];
}


/**
 * Cast a spell.
 *
 * Handles general preconditions & costs.
 *
 * @param check_range   If true, abort if no targets are in range. (z vs Z)
 * @param spell         The type of spell to be cast.
 * @param force_failure True if the spell's failure has already been determined
 *                      in advance (for spells being cast via Divine Exegesis).
 * @return              Whether the spell was successfully cast.
 **/
spret cast_a_spell(bool check_range, spell_type spell, dist *_target,
                   bool force_failure)
{
    // If you don't have any spells memorized (and aren't using exegesis),
    // you can't cast any spells. Simple as.
    if (!you.spell_no && !you.divine_exegesis)
    {
        canned_msg(MSG_NO_SPELLS);
        return spret::abort;
    }

    if (!can_cast_spells())
    {
        crawl_state.zero_turns_taken();
        return spret::abort;
    }

    if (crawl_state.game_is_hints())
        Hints.hints_spell_counter++;

    if (spell == SPELL_NO_SPELL)
    {
        int keyin = 0;

        string luachoice;
        if (!clua.callfn("c_choose_spell", ">s", &luachoice))
        {
            if (!clua.error.empty())
                mprf(MSGCH_ERROR, "Lua error: %s", clua.error.c_str());
        }
        else if (!luachoice.empty() && isalpha(luachoice[0]))
        {
            keyin = luachoice[0];
            const spell_type spl = get_spell_by_letter(keyin);

            // Bad entry from lua, defer to the user
            if (!is_valid_spell(spl))
                keyin = 0;
        }

        while (true)
        {
            if (keyin == 0 && !Options.spell_menu)
            {
                if (you.spell_no == 1)
                {
                    // Set last_cast_spell to the current only spell.
                    for (int i = 0; i < 52; ++i)
                    {
                        const char letter = index_to_letter(i);
                        const spell_type spl = get_spell_by_letter(letter);

                        if (!is_valid_spell(spl))
                            continue;

                        you.last_cast_spell = spl;
                        break;
                    }
                }

                // We allow setting last cast spell by Divine Exegesis, but we
                // don't allow recasting it with the UI unless we actually have
                // the spell memorized.
                if (you.last_cast_spell != SPELL_NO_SPELL
                    && !you.has_spell(you.last_cast_spell))
                {
                    you.last_cast_spell = SPELL_NO_SPELL;
                }

                if (you.last_cast_spell == SPELL_NO_SPELL
                    || !Options.enable_recast_spell)
                {
                    mprf(MSGCH_PROMPT, "Cast which spell? (? or * to list) ");
                }
                else
                {
                    mprf(MSGCH_PROMPT, "Casting: <w>%s</w>",
                                       spell_title(you.last_cast_spell));
                    mprf(MSGCH_PROMPT, "Confirm with . or Enter, or press "
                                       "? or * to list all spells.");
                }

                keyin = numpad_to_regular(get_ch());
            }

            if (keyin == '?' || keyin == '*' || Options.spell_menu)
            {
                keyin = list_spells(true, false);
                if (!keyin)
                    keyin = ESCAPE;

                if (!crawl_state.doing_prev_cmd_again)
                {
                    redraw_screen();
                    update_screen();
                }

                if (isaalpha(keyin) || key_is_escape(keyin))
                    break;
                else
                    clear_messages();

                keyin = 0;
            }
            else
                break;
        }

        if (key_is_escape(keyin))
        {
            canned_msg(MSG_OK);
            crawl_state.zero_turns_taken();
            return spret::abort;
        }
        else if (Options.enable_recast_spell
                 && (keyin == '.' || keyin == CK_ENTER))
        {
            spell = you.last_cast_spell;
        }
        else if (!isaalpha(keyin))
        {
            mpr("You don't know that spell.");
            crawl_state.zero_turns_taken();
            return spret::abort;
        }
        else
            spell = get_spell_by_letter(keyin);
    }

    if (spell == SPELL_NO_SPELL)
    {
        mpr("You don't know that spell.");
        crawl_state.zero_turns_taken();
        return spret::abort;
    }

    // MP, confusion, Ru sacs
    const auto reason = casting_uselessness_reason(spell, true);
    if (!reason.empty())
    {
        mpr(reason);
        crawl_state.zero_turns_taken();
        return spret::abort;
    }

    if (check_range && spell_no_hostile_in_range(spell))
    {
        // Abort if there are no hostiles within range, but flash the range
        // markers for a short while.
        mpr("You can't see any susceptible monsters within range! "
            "(Use <w>Z</w> to cast anyway.)");

        if ((Options.use_animations & UA_RANGE) && Options.darken_beyond_range)
        {
            targeter_smite range(&you, calc_spell_range(spell), 0, 0, true);
            range_view_annotator show_range(&range);
            delay(50);
        }
        crawl_state.zero_turns_taken();
        return spret::abort;
    }

    if (god_punishes_spell(spell, you.religion)
        && !crawl_state.disables[DIS_CONFIRMATIONS])
    {
        // None currently dock just piety, right?
        if (!yesno("Casting this spell will place you under penance. "
                   "Really cast?", true, 'n'))
        {
            canned_msg(MSG_OK);
            crawl_state.zero_turns_taken();
            return spret::abort;
        }
    }

    you.last_cast_spell = spell;
    // Silently take MP before the spell.
    const int cost = spell_mana(spell);
    pay_mp(cost);

    // Majin Bo HP cost taken at the same time
    // (but after hp costs from HP casting)
    const int hp_cost = min(spell_mana(spell), you.hp - 1);
    if (_majin_charge_hp())
        pay_hp(hp_cost);

    const spret cast_result = your_spells(spell, 0, !you.divine_exegesis,
                                          nullptr, _target, force_failure);
    if (cast_result == spret::abort
        || you.divine_exegesis && cast_result == spret::fail)
    {
        if (cast_result == spret::abort)
            crawl_state.zero_turns_taken();
        // Return the MP since the spell is aborted.
        refund_mp(cost);
        if (_majin_charge_hp())
            refund_hp(hp_cost);

        redraw_screen();
        update_screen();
        return cast_result;
    }

    practise_casting(spell, cast_result == spret::success);
    _handle_channeling(cost, cast_result);
    if (cast_result == spret::success)
    {
        if (player_equip_unrand(UNRAND_MAJIN) && one_chance_in(500))
            _majin_speak(spell);
        did_god_conduct(DID_SPELL_CASTING, 1 + random2(5));
        count_action(CACT_CAST, spell);
    }

    finalize_mp_cost(_majin_charge_hp() ? hp_cost : 0);
    you.turn_is_over = true;
    alert_nearby_monsters();

    return cast_result;
}

/**
 * Handles divine response to spellcasting.
 *
 * @param spell         The type of spell just cast.
 */
static void _spellcasting_god_conduct(spell_type spell)
{
    // If you are casting while a god is acting, then don't do conducts.
    // (Presumably Xom is forcing you to cast a spell.)
    if (crawl_state.is_god_acting())
        return;

    const int conduct_level = 10 + spell_difficulty(spell);

    if (is_evil_spell(spell) || you.spellcasting_unholy())
        did_god_conduct(DID_EVIL, conduct_level);

    if (is_unclean_spell(spell))
        did_god_conduct(DID_UNCLEAN, conduct_level);

    if (is_chaotic_spell(spell))
        did_god_conduct(DID_CHAOS, conduct_level);

    // not is_hasty_spell since the other ones handle the conduct themselves.
    if (spell == SPELL_SWIFTNESS)
        did_god_conduct(DID_HASTY, conduct_level);
}

/**
 * Handles side effects of successfully casting a spell.
 *
 * Spell noise, magic 'sap' effects, and god conducts.
 *
 * @param spell         The type of spell just cast.
 * @param god           Which god is casting the spell; NO_GOD if it's you.
 * @param fake_spell    true if the spell is evoked or from an innate or divine ability
 *                      false if it is a spell being cast normally.
 */
static void _spellcasting_side_effects(spell_type spell, god_type god,
                                       bool fake_spell)
{
    _spellcasting_god_conduct(spell);

    if (god == GOD_NO_GOD)
    {
        if (you.duration[DUR_SAP_MAGIC] && !fake_spell)
        {
            mprf(MSGCH_WARN, "You lose access to your magic!");
            you.increase_duration(DUR_NO_CAST, 3 + random2(3));
        }

        // Make some noise if it's actually the player casting.
        noisy(spell_noise(spell), you.pos());
    }

    alert_nearby_monsters();

}

#ifdef WIZARD
static void _try_monster_cast(spell_type spell, int /*powc*/,
                              dist &spd, bolt &beam)
{
    if (monster_at(you.pos()))
    {
        mpr("Couldn't try casting monster spell because you're "
            "on top of a monster.");
        return;
    }

    monster* mon = get_free_monster();
    if (!mon)
    {
        mpr("Couldn't try casting monster spell because there is "
            "no empty monster slot.");
        return;
    }

    mpr("Invalid player spell, attempting to cast it as monster spell.");

    mon->mname      = "Dummy Monster";
    mon->type       = MONS_HUMAN;
    mon->behaviour  = BEH_SEEK;
    mon->attitude   = ATT_FRIENDLY;
    mon->flags      = (MF_NO_REWARD | MF_JUST_SUMMONED | MF_SEEN
                       | MF_WAS_IN_VIEW | MF_HARD_RESET);
    mon->hit_points = you.hp;
    mon->set_hit_dice(you.experience_level);
    mon->set_position(you.pos());
    mon->target     = spd.target;
    mon->mid        = MID_PLAYER;

    if (!spd.isTarget)
        mon->foe = MHITNOT;
    else if (!monster_at(spd.target))
    {
        if (spd.isMe())
            mon->foe = MHITYOU;
        else
            mon->foe = MHITNOT;
    }
    else
        mon->foe = env.mgrid(spd.target);

    env.mgrid(you.pos()) = mon->mindex();

    mons_cast(mon, beam, spell, MON_SPELL_NO_FLAGS);

    mon->reset();
}
#endif // WIZARD

static spret _do_cast(spell_type spell, int powc, const dist& spd,
                           bolt& beam, god_type god, bool fail,
                           bool actual_spell);

/**
 * Should this spell be aborted before casting properly starts, either because
 * it can't legally be cast in this circumstance, or because the player opts
 * to cancel it in response to a prompt?
 *
 * @param spell         The spell to be checked
 * @param fake_spell    true if the spell is evoked or from an innate or divine ability
 *                      false if it is a spell being cast normally.
 * @return              Whether the spellcasting should be aborted.
 */
static bool _spellcasting_aborted(spell_type spell)
{
    string msg;

    // casting-general checks (MP etc) are not carried out here
    msg = spell_uselessness_reason(spell, true, true, true);

    if (!msg.empty())
    {
        mpr(msg);
        return true;
    }

    vector<text_pattern> &actions = Options.confirm_action;
    if (!actions.empty())
    {
        const char* name = spell_title(spell);
        for (const text_pattern &action : actions)
        {
            if (!action.matches(name))
                continue;

            string prompt = "Really cast " + string(name) + "?";
            if (!yesno(prompt.c_str(), false, 'n'))
            {
                canned_msg(MSG_OK);
                return true;
            }
            break;
        }
    }

    return false;
}

// this is a crude approximation used for the convenience UI targeter of
// Dragon's call and Manifold Assault
static vector<coord_def> _simple_find_all_hostiles()
{
    vector<coord_def> result;
    for (monster_near_iterator mi(you.pos(), LOS_NO_TRANS); mi; ++mi)
    {
        if (!mons_aligned(&you, *mi)
            && mons_is_threatening(**mi)
            && you.can_see(**mi))
        {
            result.push_back((*mi)->pos());
        }
    }

    return result;
}

// TODO: refactor into target.cc, move custom classes out of target.h
unique_ptr<targeter> find_spell_targeter(spell_type spell, int pow, int range)
{
    switch (spell)
    {
    case SPELL_FIREBALL:
    case SPELL_ICEBLAST:
    case SPELL_FASTROOT:
        return make_unique<targeter_beam>(&you, range, spell_to_zap(spell), pow,
                                          1, 1);
    case SPELL_HURL_DAMNATION:
        return make_unique<targeter_beam>(&you, range, ZAP_HURL_DAMNATION, pow,
                                          1, 1);
    case SPELL_MEPHITIC_CLOUD:
        return make_unique<targeter_beam>(&you, range, ZAP_MEPHITIC, pow,
                                          pow >= 100 ? 1 : 0, 1);
    case SPELL_FIRE_STORM:
        return make_unique<targeter_smite>(&you, range, 2, pow > 76 ? 3 : 2);
    case SPELL_FREEZING_CLOUD:
    case SPELL_POISONOUS_CLOUD:
    case SPELL_HOLY_BREATH:
        return make_unique<targeter_cloud>(&you, range);
    case SPELL_STEAM_BURST:
        return make_unique<targeter_cloud>(&you, 1);
    case SPELL_THUNDERBOLT:
        return make_unique<targeter_thunderbolt>(&you, range,
                   get_thunderbolt_last_aim(&you));
    case SPELL_LRD:
        return make_unique<targeter_fragment>(&you, pow, range);
    case SPELL_AIRSTRIKE:
        return make_unique<targeter_airstrike>();
    case SPELL_MOMENTUM_STRIKE:
    case SPELL_DIMENSIONAL_BULLSEYE:
        return make_unique<targeter_smite>(&you, range);
    case SPELL_FULMINANT_PRISM:
        return make_unique<targeter_smite>(&you, range, 0, 2);
    case SPELL_GLACIATE:
        return make_unique<targeter_cone>(&you, range);
    case SPELL_GRAVITAS:
        return make_unique<targeter_smite>(&you, range,
                                           gravitas_range(pow),
                                           gravitas_range(pow),
                                           false,
                                           [](const coord_def& p) -> bool {
                                              return you.pos() != p; });
    case SPELL_VIOLENT_UNRAVELLING:
        return make_unique<targeter_unravelling>();
    case SPELL_INFESTATION:
        return make_unique<targeter_smite>(&you, range, 2, 2, false,
                                           [](const coord_def& p) -> bool {
                                              return you.pos() != p; });
    case SPELL_PASSWALL:
        return make_unique<targeter_passwall>(range);
    case SPELL_DIG:
        return make_unique<targeter_dig>(range);
    case SPELL_ELECTRIC_CHARGE:
        return make_unique<targeter_charge>(&you, range);

    // untargeted spells -- everything beyond here is a static targeter
    case SPELL_HAILSTORM:
        return make_unique<targeter_radius>(&you, LOS_NO_TRANS, range, 0, 2);
    case SPELL_ISKENDERUNS_MYSTIC_BLAST:
        return make_unique<targeter_radius>(&you, LOS_SOLID_SEE, range, 0, 1);
    case SPELL_BORGNJORS_VILE_CLUTCH:
        return make_unique<targeter_radius>(&you, LOS_SOLID_SEE, range, 0, 1);
    case SPELL_WINTERS_EMBRACE:
        return make_unique<targeter_radius>(&you, LOS_SOLID_SEE, range, 0, 1);
    case SPELL_WARP_GRAVITY:
        return make_unique<targeter_radius>(&you, LOS_SOLID_SEE, range, 0, 1);
    case SPELL_STARBURST:
        return make_unique<targeter_starburst>(&you, range, pow);
    case SPELL_IRRADIATE:
        return make_unique<targeter_maybe_radius>(&you, LOS_NO_TRANS, 1, 0, 1);
    case SPELL_DISCHARGE: // not entirely accurate...maybe should highlight
                          // all potentially affected monsters?
        return make_unique<targeter_maybe_radius>(&you, LOS_NO_TRANS, 1);
    case SPELL_ARCJOLT:
        return make_unique<targeter_multiposition>(&you,
                                                   arcjolt_targets(you, false));
    case SPELL_PLASMA_BEAM:
    {
        auto plasma_targets = plasma_beam_targets(you, pow, false);
        auto plasma_paths = plasma_beam_paths(you.pos(), plasma_targets);
        const aff_type a = plasma_targets.size() == 1 ? AFF_YES : AFF_MAYBE;
        return make_unique<targeter_multiposition>(&you, plasma_paths, a);
    }
    case SPELL_CHAIN_LIGHTNING:
        return make_unique<targeter_chain_lightning>();
    case SPELL_MAXWELLS_COUPLING:
        return make_unique<targeter_maxwells_coupling>();
    case SPELL_FROZEN_RAMPARTS:
        return make_unique<targeter_walls>(&you, find_ramparts_walls());
    case SPELL_DISPERSAL:
    case SPELL_DISJUNCTION:
    case SPELL_DAZZLING_FLASH:
        return make_unique<targeter_maybe_radius>(&you, LOS_SOLID_SEE, range,
                                                  0, 1);
    case SPELL_INNER_FLAME:
        return make_unique<targeter_inner_flame>(&you, range);
    case SPELL_SIMULACRUM:
        return make_unique<targeter_simulacrum>(&you, range);
    case SPELL_LEDAS_LIQUEFACTION:
        return make_unique<targeter_radius>(&you, LOS_NO_TRANS,
                                            liquefaction_max_range(pow),
                                            0, 0, 1);
    case SPELL_SILENCE:
        return make_unique<targeter_radius>(&you, LOS_NO_TRANS,
                                            silence_max_range(pow),
                                            0, 0,
                                            silence_min_range(pow));
    case SPELL_POISONOUS_VAPOURS:
        return make_unique<targeter_poisonous_vapours>(&you, range);

    // at player's position only but not a selfench (wait, why wereblood?)
    case SPELL_WEREBLOOD:
    case SPELL_SUBLIMATION_OF_BLOOD:
    case SPELL_BORGNJORS_REVIVIFICATION:
    case SPELL_BLASTMOTE:
    case SPELL_TOMB_OF_DOROKLOHE:
        return make_unique<targeter_radius>(&you, LOS_SOLID_SEE, 0);

    // LOS radius:
    case SPELL_OZOCUBUS_REFRIGERATION:
        return make_unique<targeter_refrig>(&you);
    case SPELL_OLGREBS_TOXIC_RADIANCE:
        return make_unique<targeter_maybe_radius>(&you, LOS_NO_TRANS,
                                                  LOS_RADIUS, 0, 1);
    case SPELL_POLAR_VORTEX:
        return make_unique<targeter_radius>(&you, LOS_NO_TRANS,
                                            POLAR_VORTEX_RADIUS, 0, 1);
    case SPELL_SHATTER:
        return make_unique<targeter_shatter>(&you); // special version that
                                                    // affects walls
    case SPELL_IGNITE_POISON: // many cases
        return make_unique<targeter_ignite_poison>(&you);
    case SPELL_ROT:
        return make_unique<targeter_rot>(&you);
    case SPELL_CAUSE_FEAR: // for these, we just mark the eligible monsters
        return make_unique<targeter_fear>();
    case SPELL_ANGUISH:
        return make_unique<targeter_anguish>();
    case SPELL_INTOXICATE:
        return make_unique<targeter_intoxicate>();
    case SPELL_ENGLACIATION:
        return make_unique<targeter_englaciate>();
    case SPELL_DRAIN_LIFE:
        return make_unique<targeter_drain_life>();
    case SPELL_FRIGID_HALO:
        return make_unique<targeter_frigid_halo>();
    case SPELL_DISCORD:
        return make_unique<targeter_discord>();
    case SPELL_IGNITION:
        return make_unique<targeter_multifireball>(&you,
                   get_ignition_blast_sources(&you, true));

    // Summons. Most summons have a simple range 2 radius, see
    // find_newmons_square
    case SPELL_SUMMON_SMALL_MAMMAL:
    case SPELL_CALL_CANINE_FAMILIAR:
    case SPELL_ANIMATE_ARMOUR:
    case SPELL_ICE_STATUE:
    case SPELL_MONSTROUS_MENAGERIE:
    case SPELL_SUMMON_CACTUS:
    case SPELL_SUMMON_HYDRA:
    case SPELL_SUMMON_MANA_VIPER:
    case SPELL_CONJURE_BALL_LIGHTNING:
    case SPELL_SHADOW_CREATURES: // used for ?summoning
    case SPELL_AMBULATORY_BOMB:
    case SPELL_CALL_IMP:
    case SPELL_SUMMON_HORRIBLE_THINGS:
    case SPELL_SPELLFORGED_SERVITOR:
    case SPELL_SUMMON_LIGHTNING_SPIRE:
    case SPELL_BATTLESPHERE:
    case SPELL_SUMMON_ELEMENTAL:
        return make_unique<targeter_maybe_radius>(&you, LOS_NO_TRANS, 2, 0, 1);
    case SPELL_FOXFIRE:
        return make_unique<targeter_maybe_radius>(&you, LOS_NO_TRANS, 1, 0, 1);
    // TODO: these two actually have pretty wtf positioning that uses compass
    // directions, so this targeter is not entirely accurate.
    case SPELL_MALIGN_GATEWAY:
    case SPELL_SUMMON_FOREST:
        return make_unique<targeter_radius>(&you, LOS_NO_TRANS, LOS_RADIUS,
                                            0, 2);

    case SPELL_BLINK:
        return make_unique<targeter_multiposition>(&you, find_blink_targets());
    case SPELL_MANIFOLD_ASSAULT:
        return make_unique<targeter_multiposition>(&you,
                                                   _simple_find_all_hostiles());
    case SPELL_SCORCH:
        return make_unique<targeter_scorch>(you, range, false);
    case SPELL_DRAGON_CALL: // this is just convenience: you can start the spell
                            // with no enemies in sight
        return make_unique<targeter_multifireball>(&you,
                                                   _simple_find_all_hostiles());
    case SPELL_NOXIOUS_BOG:
        return make_unique<targeter_bog>(&you, pow);
    case SPELL_FLAME_WAVE:
        return make_unique<targeter_flame_wave>(range);
    case SPELL_GOLUBRIAS_PASSAGE:
        return make_unique<targeter_passage>(range);
    case SPELL_SIGIL_OF_BINDING:
        return make_unique<targeter_multiposition>(&you,
                                                   find_sigil_locations(true));
    case SPELL_BOULDER:
        return make_unique<targeter_boulder>(&you);
    case SPELL_PETRIFY:
        return make_unique<targeter_petrify>(&you, range);
    case SPELL_PERMAFROST_ERUPTION:
        return make_unique<targeter_permafrost>(you, pow);

    default:
        break;
    }

    if (spell_to_zap(spell) != NUM_ZAPS)
    {
        return make_unique<targeter_beam>(&you, range, spell_to_zap(spell),
                                          pow, 0, 0);
    }

    // selfench is used mainly for monster AI, so it is a bit over-applied in
    // the spell data
    if (get_spell_flags(spell) & spflag::selfench
        && !spell_typematch(spell, spschool::summoning) // all summoning spells are selfench
        && !spell_typematch(spell, spschool::translocation) // blink, passage
        && spell != SPELL_PHANTOM_MIRROR) // ??
    {
        return make_unique<targeter_radius>(&you, LOS_SOLID_SEE, 0);
    }

    return nullptr;
}

bool spell_has_targeter(spell_type spell)
{
    return bool(find_spell_targeter(spell, 1, 1));
}

// Returns the nth triangular number.
static int _triangular_number(int n)
{
    return n * (n+1) / 2;
}

// _tetrahedral_number: returns the nth tetrahedral number.
// This is the number of triples of nonnegative integers with sum < n.
static int _tetrahedral_number(int n)
{
    return n * (n+1) * (n+2) / 6;
}

/**
 * Compute success chance for WL-checking spells and abilities.
 *
 * @param wl The willpower of the target.
 * @param powc The enchantment power.
 * @param scale The denominator of the result.
 * @param round_up Should the resulting chance be rounded up (true) or
 *        down (false, the default)?
 *
 * @return The chance, out of scale, that the enchantment affects the target.
 */
int hex_success_chance(const int wl, int powc, int scale, bool round_up)
{
    const int pow = ench_power_stepdown(powc);
    const int target = wl + 100 - pow;
    const int denom = 101 * 100;
    const int adjust = round_up ? denom - 1 : 0;

    if (target <= 0)
        return scale;
    if (target > 200)
        return 0;
    if (target <= 100)
        return (scale * (denom - _triangular_number(target)) + adjust) / denom;
    return (scale * _triangular_number(201 - target) + adjust) / denom;
}

// approximates _test_beam_hit in a deterministic fashion.
static int _to_hit_pct(const monster_info& mi, int acc)
{
    if (acc == AUTOMATIC_HIT)
        return 100;

    int ev = mi.ev;
    if (mi.is(MB_REPEL_MSL))
        ev += 50;

    return acc - min(ev, 100 - MIN_HIT_PERCENTAGE);
}

static vector<string> _desc_hit_chance(const monster_info& mi, int acc)
{
    if (!acc)
        return vector<string>{};
    const int hit_pct = _to_hit_pct(mi, acc);
    if (hit_pct == -1)
        return vector<string>{};
    return vector<string>{make_stringf("%d%% to hit", hit_pct)};
}

vector<string> desc_beam_hit_chance(const monster_info& mi, targeter* hitfunc)
{
    targeter_beam* beam_hitf = dynamic_cast<targeter_beam*>(hitfunc);
    if (!beam_hitf)
        return vector<string>{};
    return _desc_hit_chance(mi, beam_hitf->beam.hit);
}

static vector<string> _desc_plasma_hit_chance(const monster_info& mi, int powc)
{
    bolt beam;
    zappy(spell_to_zap(SPELL_PLASMA_BEAM), powc, false, beam);
    const int hit_pct = _to_hit_pct(mi, beam.hit);
    if (hit_pct == -1)
        return vector<string>{};
    return vector<string>{make_stringf("2x%d%% to hit", hit_pct)};
}

static vector<string> _desc_intoxicate_chance(const monster_info& mi,
                                              targeter* hitfunc, int pow)
{
    if (hitfunc && !hitfunc->affects_monster(mi))
        return vector<string>{"not susceptible"};

    int conf_pct = min(40 + pow * 3, 100);

    return vector<string>{make_stringf("chance to confuse: %d%%", conf_pct)};
}

static vector<string> _desc_englaciate_chance(const monster_info& mi,
                                              targeter* hitfunc, int pow)
{
    if (hitfunc && !hitfunc->affects_monster(mi))
        return vector<string>{"not susceptible"};

    const int outcomes = pow * pow * pow;
    const int target   = 3 * mi.hd - 2;
    int fail_pct;

    // Tetrahedral number calculation to find the chance
    // 3 d pow < 3 * mi . hd + 1
    if (target <= pow)
        fail_pct = 100 * _tetrahedral_number(target) / outcomes;
    else if (target <= 2 * pow)
    {
        fail_pct = 100 * (_tetrahedral_number(target)
                       - 3 * _tetrahedral_number(target - pow)) / outcomes;
    }
    else if (target <= 3 * pow)
    {
        fail_pct = 100 * (outcomes
                       - _tetrahedral_number(3 * pow - target)) / outcomes;
    }
    else
        fail_pct = 100;

    return vector<string>{make_stringf("chance to slow: %d%%", 100 - fail_pct)};
}

static vector<string> _desc_dazzle_chance(const monster_info& mi, int pow)
{
    if (!mons_can_be_dazzled(mi.type))
        return vector<string>{"not susceptible"};

    const int numerator = dazzle_chance_numerator(mi.hd);
    const int denom = dazzle_chance_denom(pow);
    const int dazzle_pct = max(100 * numerator / denom, 0);

    return vector<string>{make_stringf("chance to dazzle: %d%%", dazzle_pct)};
}

static vector<string> _desc_airstrike_bonus(const monster_info& mi)
{
    const int empty_spaces = airstrike_space_around(mi.pos, false);
    return vector<string>{make_stringf("empty space bonus: %d/8", empty_spaces)};
}

static vector<string> _desc_meph_chance(const monster_info& mi)
{
    if (get_resist(mi.resists(), MR_RES_POISON) >= 1 || mi.is(MB_CLARITY))
        return vector<string>{"not susceptible"};

    int pct_chance = 100 - (100 * mi.hd / 11);
    return vector<string>{make_stringf("chance to affect: %d%%", pct_chance)};
}

static vector<string> _desc_hailstorm_hit_chance(const monster_info& mi, int pow)
{
    bolt beam;
    zappy(ZAP_HAILSTORM, pow, false, beam);
    return _desc_hit_chance(mi, beam.hit);
}

static vector<string> _desc_momentum_strike_hit_chance(const monster_info& mi, int pow)
{
    bolt beam;
    zappy(ZAP_MOMENTUM_STRIKE, pow, false, beam);
    return _desc_hit_chance(mi, beam.hit);
}

static vector<string> _desc_insubstantial(const monster_info& mi, string desc)
{
    if (mons_class_flag(mi.type, M_INSUBSTANTIAL))
        return vector<string>{desc};

    return vector<string>{};
}

static vector<string> _desc_vampiric_draining_valid(const monster_info& mi)
{
    if (mi.mb.get(MB_CANT_DRAIN))
        return vector<string>{"not susceptible"};

    return vector<string>{};
}

static vector<string> _desc_rimeblight_valid(const monster_info& mi)
{
    if (mi.is(MB_RIMEBLIGHT))
        return vector<string>{"already infected"};
    else if (!(mi.holi & (MH_NATURAL | MH_DEMONIC | MH_NONLIVING)))
        return vector<string>{"not susceptible"};

    return vector<string>{};
}

static vector<string> _desc_dispersal_chance(const monster_info& mi, int pow)
{
    const int wl = mi.willpower();
    if (mons_class_is_stationary(mi.type))
        return vector<string>{"stationary"};

    if (wl == WILL_INVULN)
        return vector<string>{"will blink"};

    const int success = hex_success_chance(wl, pow, 100);
    return vector<string>{make_stringf("chance to teleport: %d%%", success)};
}

static vector<string> _desc_enfeeble_chance(const monster_info& mi, int pow)
{
    vector<string> base_effects;
    vector<string> all_effects;
    const int wl = mi.willpower();

    if (!mi.is(MB_NO_ATTACKS))
        base_effects.push_back("weakness");
    if (mi.antimagic_susceptible())
        base_effects.push_back("antimagic");
    if (!base_effects.empty())
    {
        all_effects.push_back("will inflict " +
            comma_separated_line(base_effects.begin(), base_effects.end()));
    }
    if (wl != WILL_INVULN)
    {
        const int success = hex_success_chance(wl, pow, 100);
        all_effects.push_back(make_stringf("chance to daze and slow: %d%%",
             success));
    }

    if (all_effects.empty())
        return vector<string>{"not susceptible"};

    return all_effects;
}

static string _mon_threat_string(const CrawlStoreValue &mon_store)
{
    monster dummy;
    dummy.type = static_cast<monster_type>(mon_store.get_int());
    define_monster(dummy);

    int col;
    string desc;
    monster_info(&dummy).to_string(1, desc, col, true, nullptr, false);
    const string col_name = colour_to_str(col);

    return "<" + col_name + ">" + article_a(desc) + "</" + col_name + ">";
}

// Include success chance in targeter for spells checking monster WL.
vector<string> desc_wl_success_chance(const monster_info& mi, int pow,
                                      targeter* hitfunc)
{
    targeter_beam* beam_hitf = dynamic_cast<targeter_beam*>(hitfunc);
    int wl = mi.willpower();
    if (wl == WILL_INVULN)
        return vector<string>{"infinite will"};
    if (you.wearing_ego(EQ_ALL_ARMOUR, SPARM_GUILE))
        wl = guile_adjust_willpower(wl);
    if (hitfunc && !hitfunc->affects_monster(mi))
        return vector<string>{"not susceptible"};
    vector<string> descs;
    if (beam_hitf && beam_hitf->beam.flavour == BEAM_POLYMORPH)
    {
        // Polymorph has a special effect on ugly things and shapeshifters that
        // does not require passing an WL check.
        if (mi.type == MONS_UGLY_THING || mi.type == MONS_VERY_UGLY_THING)
            return vector<string>{"will change colour"};
        if (mi.is(MB_SHAPESHIFTER))
            return vector<string>{"will change shape"};
        if (mi.type == MONS_SLIME_CREATURE && mi.slime_size > 1)
            descs.push_back("will probably split");

        // list out the normal poly set
        if (!mi.props.exists(POLY_SET_KEY))
            return vector<string>{"not susceptible"};
        const CrawlVector &set = mi.props[POLY_SET_KEY].get_vector();
        if (set.size() <= 0)
            return vector<string>{"not susceptible"};
        descs.push_back("will become "
                        + comma_separated_fn(set.begin(), set.end(),
                                             _mon_threat_string, ", or "));
    }

    const int success = hex_success_chance(wl, pow, 100);
    descs.push_back(make_stringf("chance to affect: %d%%", success));

    return descs;
}

class spell_targeting_behaviour : public targeting_behaviour
{
public:
    spell_targeting_behaviour(spell_type _spell)
        : targeting_behaviour(false), spell(_spell),
          err(spell_uselessness_reason(spell, true, false, true))
    {
    }

    bool targeted() override
    {
        return !!(get_spell_flags(spell) & spflag::targeting_mask);
    }

    string get_error() override
    {
        return err;
    }

    // TODO: provide useful errors for specific targets via get_monster_desc?

private:
    spell_type spell;
    string err;
};

// TODO: is there a way for this to be part of targeter objects, or
// direction_chooser itself?
desc_filter targeter_addl_desc(spell_type spell, int powc, spell_flags flags,
                                       targeter *hitfunc)
{
    // Add success chance to targeted spells checking monster WL
    const bool wl_check = testbits(flags, spflag::WL_check)
                          && !testbits(flags, spflag::helpful);
    if (wl_check && spell != SPELL_DISPERSAL)
    {
        const zap_type zap = spell_to_zap(spell);
        const int eff_pow = zap != NUM_ZAPS ? zap_ench_power(zap, powc,
                                                             false)
                                            :
              //XXX: deduplicate this with mass_enchantment?
              testbits(flags, spflag::area) ? min(200, ( powc * 3 ) / 2)
                                            : powc;

        if (spell == SPELL_ENFEEBLE)
            return bind(_desc_enfeeble_chance, placeholders::_1, eff_pow);
        else
            return bind(desc_wl_success_chance, placeholders::_1, eff_pow,
                        hitfunc);
    }
    switch (spell)
    {
        case SPELL_INTOXICATE:
            return bind(_desc_intoxicate_chance, placeholders::_1,
                        hitfunc, powc);
        case SPELL_ENGLACIATION:
            return bind(_desc_englaciate_chance, placeholders::_1,
                        hitfunc, powc);
        case SPELL_DAZZLING_FLASH:
            return bind(_desc_dazzle_chance, placeholders::_1, powc);
        case SPELL_MEPHITIC_CLOUD:
            return bind(_desc_meph_chance, placeholders::_1);
        case SPELL_VAMPIRIC_DRAINING:
            return bind(_desc_vampiric_draining_valid, placeholders::_1);
        case SPELL_RIMEBLIGHT:
            return bind(_desc_rimeblight_valid, placeholders::_1);
        case SPELL_STARBURST:
        {
            targeter_starburst* burst_hitf =
                dynamic_cast<targeter_starburst*>(hitfunc);
            if (!burst_hitf)
                break;
            targeter_starburst_beam* beam_hitf = &burst_hitf->beams[0];
            return bind(desc_beam_hit_chance, placeholders::_1, beam_hitf);
        }
        case SPELL_DISPERSAL:
            return bind(_desc_dispersal_chance, placeholders::_1, powc);
        case SPELL_AIRSTRIKE:
            return bind(_desc_airstrike_bonus, placeholders::_1);
        case SPELL_HAILSTORM:
            return bind(_desc_hailstorm_hit_chance, placeholders::_1, powc);
        case SPELL_MOMENTUM_STRIKE:
            return bind(_desc_momentum_strike_hit_chance, placeholders::_1, powc);
        case SPELL_FASTROOT:
            return bind(_desc_insubstantial, placeholders::_1, "immune to vines");
        case SPELL_STICKY_FLAME:
            return bind(_desc_insubstantial, placeholders::_1, "unstickable");
        case SPELL_PLASMA_BEAM:
            return bind(_desc_plasma_hit_chance, placeholders::_1, powc);
        default:
            break;
    }
    targeter_beam* beam_hitf = dynamic_cast<targeter_beam*>(hitfunc);
    if (beam_hitf && beam_hitf->beam.hit > 0 && !beam_hitf->beam.is_explosion)
        return bind(desc_beam_hit_chance, placeholders::_1, hitfunc);
    return nullptr;
}

/**
 * Returns the description displayed if targeting a monster with a spell.
 * For the clua api
 *
 * @param mi     The targeted monster.
 * @param spell  The spell being cast.
 * @return       The displayed string.
 **/
string target_desc(const monster_info& mi, spell_type spell)
{
    int powc = calc_spell_power(spell);
    const int range = calc_spell_range(spell, powc, false);

    unique_ptr<targeter> hitfunc = find_spell_targeter(spell, powc, range);
    if (!hitfunc)
        return "";

    desc_filter addl_desc = targeter_addl_desc(spell, powc,
                                get_spell_flags(spell), hitfunc.get());
    if (!addl_desc)
        return "";

    vector<string> d = addl_desc(mi);
    return comma_separated_line(d.begin(), d.end());
}

/**
 * Targets and fires player-cast spells & spell-like effects.
 *
 * Not all of these are actually real spells; invocations, decks or misc.
 * effects might also land us here.
 * Others are currently unused or unimplemented.
 *
 * @param spell         The type of spell being cast.
 * @param powc          Spellpower.
 * @param actual_spell  true if it is a spell being cast normally.
 *                      false if the spell is evoked or from an innate or
 *                      divine ability.
 * @param evoked_wand   The wand the spell was evoked from if applicable, or
 *                      nullptr.
 * @param force_failure True if the spell's failure has already been determined
 *                      in advance (for spells being cast via an innate or
 *                      divine ability).
 * @return spret::success if spell is successfully cast for purposes of
 * exercising, spret::fail otherwise, or spret::abort if the player cancelled
 * the casting.
 **/
spret your_spells(spell_type spell, int powc, bool actual_spell,
                  const item_def* const evoked_wand, dist *target,
                  bool force_failure)
{
    ASSERT(!crawl_state.game_is_arena());
    ASSERT(!(actual_spell && evoked_wand));
    ASSERT(!evoked_wand || evoked_wand->base_type == OBJ_WANDS);
    ASSERT(!force_failure || !actual_spell && !evoked_wand);

    const bool wiz_cast = (crawl_state.prev_cmd == CMD_WIZARD && !actual_spell);

    dist target_local;
    if (!target)
        target = &target_local;
    bolt beam;
    beam.origin_spell = spell;

    // [dshaligram] Any action that depends on the spellcasting attempt to have
    // succeeded must be performed after the switch.
    if (!wiz_cast && _spellcasting_aborted(spell))
        return spret::abort;

    const spell_flags flags = get_spell_flags(spell);

    if (!powc)
        powc = calc_spell_power(spell);

    const int range = calc_spell_range(spell, powc, actual_spell);
    beam.range = range;

    unique_ptr<targeter> hitfunc = find_spell_targeter(spell, powc, range);
    const bool is_targeted = !!(flags & spflag::targeting_mask);

    const god_type god =
        (crawl_state.is_god_acting()) ? crawl_state.which_god_acting()
                                      : GOD_NO_GOD;

    // XXX: This handles only some of the cases where spells need
    // targeting. There are others that do their own that will be
    // missed by this (and thus will not properly ESC without cost
    // because of it). Hopefully, those will eventually be fixed. - bwr
    // TODO: what's the status of the above comment in 2020+?
    const bool use_targeter = is_targeted
        || !god // Don't allow targeting spells cast by Xom
           && hitfunc
           && (target->fire_context // force static targeters when called in
                                    // "fire" mode
               || Options.always_use_static_spell_targeters
               || Options.force_spell_targeter.count(spell) > 0);

    if (use_targeter && spell == SPELL_ELECTRIC_CHARGE)
    {
        // would be nice to do away with this special casing, can this be
        // rolled into more generic code?
        vector<coord_def> target_path; // unused here
        if (!find_charge_target(target_path, range, hitfunc.get(), *target))
            return spret::abort;
        ASSERT(target->isValid);
        // code dup with spell_direction...
        beam.set_target(*target);
        beam.source = you.pos();
    }
    else if (use_targeter)
    {
        const targ_mode_type targ =
              testbits(flags, spflag::neutral)    ? TARG_ANY :
              testbits(flags, spflag::helpful)    ? TARG_FRIEND :
              testbits(flags, spflag::obj)        ? TARG_MOVABLE_OBJECT :
                                                   TARG_HOSTILE;

        // TODO: if any other spells ever need this, add an spflag
        // (right now otherwise used only on god abilities)
        const targeting_type dir =
            spell == SPELL_BLINKBOLT ? DIR_ENFORCE_RANGE
            : testbits(flags, spflag::target) ? DIR_TARGET : DIR_NONE;

        // TODO: it's extremely inconsistent when this prompt shows up, not
        // sure why
        const char *prompt = get_spell_target_prompt(spell);

        const bool needs_path = !testbits(flags, spflag::target)
                                // Apportation must be spflag::target, since a
                                // shift-direction makes no sense for it, but
                                // it nevertheless requires line-of-fire.
                                || spell == SPELL_APPORTATION;

        desc_filter additional_desc
            = targeter_addl_desc(spell, powc, flags, hitfunc.get());

        // `true` on fourth param skips MP check and a few others that have
        // already been carried out
        const bool useless = spell_is_useless(spell, true, false, true);
        const char *spell_title_color = useless ? "darkgrey" : "w";
        const string verb = wait_spell_active(spell)
            ? "<lightred>Restarting spell</lightred>"
            : is_targeted ? "Aiming" : "Casting";
        string title = make_stringf("%s: <%s>%s</%s>", verb.c_str(),
                    spell_title_color, spell_title(spell), spell_title_color);

        spell_targeting_behaviour beh(spell);

        direction_chooser_args args;
        args.hitfunc = hitfunc.get();
        args.restricts = dir;
        args.mode = targ;
        args.range = range;
        args.needs_path = needs_path;
        args.target_prefix = prompt;
        args.top_prompt = title;
        args.behaviour = &beh;

        // if the spell is useless and we have somehow gotten this far, it's
        // a forced cast. Setting this prevents the direction chooser from
        // looking for selecting a default target (which doesn't factor in
        // the spell's capabilities).
        // Also ensure we don't look for a target for static targeters. It might
        // be better to move to an affected position if any?
        if (useless || !is_targeted)
            args.default_place = you.pos();
        if (hitfunc && hitfunc->can_affect_walls())
        {
            args.show_floor_desc = true;
            args.show_boring_feats = false; // don't show "The floor."
        }
        if (testbits(flags, spflag::not_self))
            args.self = confirm_prompt_type::cancel;
        else
            args.self = confirm_prompt_type::none;
        args.get_desc_func = additional_desc;
        if (!spell_direction(*target, beam, &args))
            return spret::abort;

        if (testbits(flags, spflag::not_self) && target->isMe())
        {
            if (spell == SPELL_TELEPORT_OTHER)
                mpr("Sorry, this spell works on others only.");
            else
                canned_msg(MSG_UNTHINKING_ACT);

            return spret::abort;
        }
    }

    if (evoked_wand)
        surge_power_wand(wand_mp_cost());
    else if (actual_spell)
        surge_power(_spell_enhancement(spell));

    int fail = 0;
    if (evoked_wand && evoked_wand->charges == 0)
        return spret::fail;

    dprf("Spell #%d, power=%d", spell, powc);

    // Have to set aim first, in case the spellcast kills its first target
    if (you.props.exists(BATTLESPHERE_KEY)
        && (actual_spell || you.divine_exegesis))
    {
        aim_battlesphere(&you, spell);
    }

    const auto orig_target = monster_at(beam.target);
    const bool self_target = you.pos() == beam.target;
    const bool had_tele = orig_target && orig_target->has_ench(ENCH_TP);

    spret cast_result = _do_cast(spell, powc, *target, beam, god,
                                 force_failure || fail, actual_spell);

    switch (cast_result)
    {
    case spret::success:
    {
        _apply_post_zap_effect(spell);

        const int demonic_magic = you.get_mutation_level(MUT_DEMONIC_MAGIC);

        if ((demonic_magic == 3 && evoked_wand)
            || (demonic_magic > 0 && (actual_spell || you.divine_exegesis)))
        {
            do_demonic_magic(spell_difficulty(spell) * 6, demonic_magic);
        }

        if (you.props.exists(BATTLESPHERE_KEY)
            && (actual_spell || you.divine_exegesis)
            && battlesphere_can_mirror(spell))
        {
            trigger_battlesphere(&you);
        }

        const auto victim = monster_at(beam.target);
        if (will_have_passive(passive_t::shadow_spells)
            && actual_spell
            && !god_hates_spell(spell, you.religion, !actual_spell)
            && (flags & spflag::targeting_mask)
            && !(flags & spflag::neutral)
            && (beam.is_enchantment()
                || battlesphere_can_mirror(spell))
            // Must have a target, but that can't be the player.
            && !self_target
            && orig_target
            // For teleport other, only mimic if the spell hit who we
            // originally targeted and if we failed to change the target's
            // teleport status. This way the mimic won't just undo the effect
            // of a successful cast.
            && (spell != SPELL_TELEPORT_OTHER
                || (orig_target == victim
                    && had_tele == victim->has_ench(ENCH_TP))))
        {
            dithmenos_shadow_spell(&beam, spell);
        }
        _spellcasting_side_effects(spell, god, !actual_spell);
        return spret::success;
    }
    case spret::fail:
    {
        if (actual_spell)
        {
            mprf("You miscast %s.", spell_title(spell));
            flush_input_buffer(FLUSH_ON_FAILURE);
            learned_something_new(HINT_SPELL_MISCAST);
            miscast_effect(spell, fail);
        }

        return spret::fail;
    }

    case spret::abort:
        return spret::abort;

    case spret::none:
#ifdef WIZARD
        if (you.wizard && !actual_spell && is_valid_spell(spell)
            && (flags & spflag::monster))
        {
            _try_monster_cast(spell, powc, *target, beam);
            return spret::success;
        }
#endif

        if (is_valid_spell(spell))
        {
            mprf(MSGCH_ERROR, "Spell '%s' is not a player castable spell.",
                 spell_title(spell));
        }
        else
            mprf(MSGCH_ERROR, "Invalid spell!");

        return spret::abort;
    }

    return spret::success;
}

// Returns spret::success, spret::abort, spret::fail
// or spret::none (not a player spell).
static spret _do_cast(spell_type spell, int powc, const dist& spd,
                           bolt& beam, god_type god, bool fail,
                           bool actual_spell)
{
    if (actual_spell && !you.wizard
        && (get_spell_flags(spell) & (spflag::monster | spflag::testing)))
    {
        return spret::none;
    }

    const coord_def target = spd.isTarget ? beam.target : you.pos() + spd.delta;

    switch (spell)
    {
    case SPELL_FRIGID_HALO:
        return cast_freeze(powc, fail);

    case SPELL_IOOD:
        return cast_iood(&you, powc, &beam, 0, 0, MHITNOT, fail);

    // Clouds and explosions.
    case SPELL_POISONOUS_CLOUD:
    case SPELL_HOLY_BREATH:
    case SPELL_FREEZING_CLOUD:
        return cast_big_c(powc, spell, &you, beam, fail);

    case SPELL_FIRE_STORM:
        return cast_fire_storm(powc, beam, fail);

    // Demonspawn ability, no failure.
    case SPELL_CALL_DOWN_DAMNATION:
        return cast_smitey_damnation(powc, beam) ? spret::success : spret::abort;

    // LOS spells
    case SPELL_SMITING:
        return cast_smiting(powc, monster_at(target), fail);

    case SPELL_AIRSTRIKE:
        return cast_airstrike(powc, spd.target, fail);

    case SPELL_MOMENTUM_STRIKE:
        return cast_momentum_strike(powc, spd.target, fail);

    case SPELL_LRD:
        return cast_fragmentation(powc, &you, spd.target, fail);

    case SPELL_GRAVITAS:
        return cast_gravitas(powc, beam.target, fail);

    case SPELL_VIOLENT_UNRAVELLING:
        return cast_unravelling(spd.target, powc, fail);

    // other effects
    case SPELL_DISCHARGE:
        return cast_discharge(powc, you, fail);

    case SPELL_ARCJOLT:
        return cast_arcjolt(powc, you, fail);

    case SPELL_PLASMA_BEAM:
        return cast_plasma_beam(powc, you, fail);

    case SPELL_CHAIN_LIGHTNING:
        return cast_chain_lightning(powc, you, fail);

    case SPELL_DISPERSAL:
        return cast_dispersal(powc, fail);

    case SPELL_SHATTER:
        return cast_shatter(powc, fail);

    case SPELL_SCORCH:
        return cast_scorch(powc, fail);

    case SPELL_IRRADIATE:
        return cast_irradiate(powc, you, fail);

    case SPELL_WARP_GRAVITY:
        return warp_gravity(powc, fail);

    case SPELL_FORCE_QUAKE:
        return cast_force_quake(fail);

    case SPELL_LEDAS_LIQUEFACTION:
        return cast_liquefaction(powc, fail);

    case SPELL_WINTERS_EMBRACE:
        return cast_winters_embrace(powc, fail);

    case SPELL_OLGREBS_TOXIC_RADIANCE:
        return cast_toxic_radiance(&you, powc, fail);

    case SPELL_IGNITE_POISON:
        return cast_ignite_poison(&you, powc, fail);

    case SPELL_POLAR_VORTEX:
        return cast_polar_vortex(powc, fail);

    case SPELL_THUNDERBOLT:
        return cast_thunderbolt(&you, powc, target, fail);

    case SPELL_DAZZLING_FLASH:
        return cast_dazzling_flash(powc, fail);

    case SPELL_CHAIN_OF_CHAOS:
        return cast_chain_spell(SPELL_CHAIN_OF_CHAOS, powc, &you, fail);

    case SPELL_IGNITION:
        return cast_ignition(&you, powc, fail);

    case SPELL_FROZEN_RAMPARTS:
        return cast_frozen_ramparts(powc, fail);

    case SPELL_THUNDERBOLT_HD:
        return cast_thunderbolt_hd(powc, fail);

    // Summoning spells, and other spells that create new monsters.
    // If a god is making you cast one of these spells, any monsters
    // produced will count as god gifts.
    case SPELL_SUMMON_SMALL_MAMMAL:
        return cast_summon_small_mammal(powc, god, fail);

    case SPELL_CALL_CANINE_FAMILIAR:
        return cast_call_canine_familiar(powc, god, fail);

    case SPELL_ANIMATE_ARMOUR:
        return cast_summon_armour_spirit(powc, god, fail);

    case SPELL_ICE_STATUE:
        return cast_summon_ice_statue(powc, god, fail);

    case SPELL_SUMMON_CACTUS:
        return cast_summon_cactus(powc, god, fail);

    case SPELL_MONSTROUS_MENAGERIE:
        return cast_monstrous_menagerie(&you, powc, god, fail);

    case SPELL_SUMMON_DRAGON:
        return cast_summon_dragon(&you, powc, god, fail);

    case SPELL_DRAGON_CALL:
        return cast_dragon_call(powc, fail);

    case SPELL_SUMMON_HYDRA:
        return cast_summon_hydra(&you, powc, god, fail);

    case SPELL_SUMMON_MANA_VIPER:
        return cast_summon_mana_viper(powc, god, fail);

    case SPELL_SUMMON_ELEMENTAL:
        return cast_summon_elemental(fail);

    case SPELL_CONJURE_BALL_LIGHTNING:
        return cast_conjure_ball_lightning(powc, god, fail);

    case SPELL_SUMMON_LIGHTNING_SPIRE:
        return cast_summon_lightning_spire(powc, god, fail);

    case SPELL_AMBULATORY_BOMB:
        return cast_summon_guardian_golem(powc, god, fail);

    case SPELL_CALL_IMP:
        return cast_call_imp(powc, god, fail);

    case SPELL_SUMMON_HORRIBLE_THINGS:
        return cast_summon_horrible_things(powc, god, fail);

    case SPELL_MALIGN_GATEWAY:
        return cast_malign_gateway(&you, powc, god, fail);

    case SPELL_SUMMON_FOREST:
        return cast_summon_forest(&you, powc, god, fail);

    case SPELL_ANIMATE_DEAD:
        return cast_animate_dead(powc, fail);

    case SPELL_HAUNT:
        return cast_haunt(powc, beam.target, god, fail);

    case SPELL_GHOSTLY_LEGION:
        return cast_ghostly_legion(powc, fail);

    case SPELL_DEATH_CHANNEL:
        return cast_death_channel(powc, god, fail);

    case SPELL_SPELLFORGED_SERVITOR:
        return cast_spellforged_servitor(powc, god, fail);

    case SPELL_BATTLESPHERE:
        return cast_battlesphere(&you, powc, god, fail);

    case SPELL_INFESTATION:
        return cast_infestation(powc, fail);

    case SPELL_FOXFIRE:
        return cast_foxfire(you, powc, god, fail);

    case SPELL_SANDBLAST:
        return cast_sandblast(powc, fail);

    case SPELL_NOXIOUS_BOG:
        return cast_noxious_bog(powc, fail);

    // Enchantments.
    case SPELL_CONFUSING_TOUCH:
        return cast_confusing_touch(powc, fail);

    case SPELL_CAUSE_FEAR:
        return mass_enchantment(ENCH_FEAR, powc, fail);

    case SPELL_ANGUISH:
        return mass_enchantment(ENCH_ANGUISH, powc, fail);

    case SPELL_INTOXICATE:
        return cast_intoxicate(powc, fail);

    case SPELL_DISCORD:
        return mass_enchantment(ENCH_FRENZIED, powc, fail);

    case SPELL_ENGLACIATION:
        return cast_englaciation(powc, fail);

    case SPELL_BORGNJORS_VILE_CLUTCH:
        return cast_vile_clutch(powc, fail);

    case SPELL_ROT:
        return cast_dreadful_rot(&you, powc, fail);

    // Our few remaining self-enchantments.
    case SPELL_SWIFTNESS:
        return cast_swiftness(powc, fail);

    case SPELL_DEFLECT_MISSILES:
        return deflection(powc, fail);

    case SPELL_OZOCUBUS_ARMOUR:
        return ice_armour(powc, fail);

    case SPELL_PHASE_SHIFT:
        return cast_phase_shift(powc, fail);

    case SPELL_CONDENSATION_SHIELD:
        return cast_condensation_shield(powc, fail);

    case SPELL_SILENCE:
        return cast_silence(powc, fail);

    case SPELL_WEREBLOOD:
        return cast_wereblood(powc, fail);

    case SPELL_ELDRITCH_ICHOR:
        return cast_eldritch_ichor(powc, fail);

    case SPELL_DIMENSIONAL_BULLSEYE:
        return cast_dimensional_bullseye(powc, monster_at(target), fail);

    // other
    case SPELL_BORGNJORS_REVIVIFICATION:
        return cast_revivification(powc, fail);

    case SPELL_SUBLIMATION_OF_BLOOD:
        return cast_sublimation_of_blood(powc, fail);

    case SPELL_DEATHS_DOOR:
        return cast_deaths_door(powc, fail);

    // Escape spells.
    case SPELL_BLINK:
        return cast_blink(powc, fail);

    case SPELL_CONTROLLED_BLINK:
        return cast_controlled_blink();

    case SPELL_BLASTMOTE:
        return kindle_blastmotes(powc, fail);

    case SPELL_TOMB_OF_DOROKLOHE:
        return cast_tomb(powc, &you, you.mindex(), fail);

    case SPELL_PASSWALL:
        return cast_passwall(beam.target, powc, fail);

    case SPELL_APPORTATION:
        return cast_apportation(powc, beam, fail);

    case SPELL_DISJUNCTION:
        return cast_disjunction(powc, fail);

    case SPELL_MANIFOLD_ASSAULT:
        return cast_manifold_assault(powc, fail);

    case SPELL_GOLUBRIAS_PASSAGE:
        return cast_golubrias_passage(powc, beam.target, fail);

    case SPELL_FULMINANT_PRISM:
        return cast_fulminating_prism(&you, powc, beam.target, fail);

    case SPELL_SEARING_RAY:
        return cast_searing_ray(powc, beam, fail);

    case SPELL_FLAME_WAVE:
        return cast_flame_wave(powc, fail);

    case SPELL_INNER_FLAME:
        return cast_inner_flame(spd.target, powc, fail);

    case SPELL_SIMULACRUM:
        return cast_simulacrum(spd.target, powc, fail);

    case SPELL_GLACIATE:
        return cast_glaciate(&you, powc, target, fail);

    case SPELL_POISONOUS_VAPOURS:
        return cast_poisonous_vapours(powc, spd, fail);

    case SPELL_BLINKBOLT:
        return blinkbolt(powc, beam, fail);

    case SPELL_ELECTRIC_CHARGE:
        return electric_charge(powc, fail, beam.target); // hack - should take beam?

    case SPELL_STARBURST:
        return cast_starburst(powc, fail);

    case SPELL_HAILSTORM:
        return cast_hailstorm(powc, fail);

    case SPELL_MAXWELLS_COUPLING:
        return cast_maxwells_coupling(powc, fail);

    case SPELL_ISKENDERUNS_MYSTIC_BLAST:
        return cast_imb(powc, fail);

    case SPELL_JINXBITE:
        return cast_jinxbite(powc, fail);

    case SPELL_SIGIL_OF_BINDING:
        return cast_sigil_of_binding(powc, fail, false);

    case SPELL_BOULDER:
        return cast_broms_barrelling_boulder(you, beam.target, powc, fail);

    case SPELL_PERMAFROST_ERUPTION:
        return cast_permafrost_eruption(you, powc, fail);

    case SPELL_FLAME_LANCE:
        return cast_flame_lance(powc, fail);

    case SPELL_STEAM_BURST:
        return cast_steam_burst(powc, fail);

    // non-player spells that have a zap, but that shouldn't be called (e.g
    // because they will crash as a player zap).
    case SPELL_DRAIN_LIFE:
        return spret::none;

    default:
        if (spell_removed(spell))
        {
            mpr("Sorry, this spell is gone!");
            return spret::abort;
        }
        break;
    }

    // Finally, try zaps.
    zap_type zap = spell_to_zap(spell);
    if (zap != NUM_ZAPS)
    {
        return zapping(zap, spell_zap_power(spell, powc),
                       beam, true, nullptr, fail);
    }

    return spret::none;
}

int fail_severity()
{
    return 0;
}

string spell_noise_string(spell_type spell, int chop_wiz_display_width)
{
    const int casting_noise = spell_noise(spell);
    int effect_noise = spell_effect_noise(spell);

    // A typical amount of noise.
    if (spell == SPELL_POLAR_VORTEX)
        effect_noise = 15;

    const int noise = max(casting_noise, effect_noise);

    const char* noise_descriptions[] =
    {
        "Silent", "Almost silent", "Quiet", "A bit loud", "Loud", "Very loud",
        "Extremely loud", "Deafening"
    };

    const int breakpoints[] = { 1, 2, 4, 8, 15, 20, 30 };
    COMPILE_CHECK(ARRAYSZ(noise_descriptions) == 1 + ARRAYSZ(breakpoints));

    const char* desc = noise_descriptions[breakpoint_rank(noise, breakpoints,
                                                ARRAYSZ(breakpoints))];

#ifdef WIZARD
    if (you.wizard)
    {
        if (chop_wiz_display_width > 0)
        {
            ostringstream shortdesc;
            shortdesc << chop_string(desc, chop_wiz_display_width)
                      << "(" << to_string(noise) << ")";
            return shortdesc.str();
        }
        else
            return make_stringf("%s (%d)", desc, noise);
    }
    else
#endif
        return desc;
}

int power_to_barcount(int power)
{
    if (power == -1)
        return -1;

    const int breakpoints[] = { 10, 15, 25, 35, 50, 75, 100, 150, 200 };
    return breakpoint_rank(power, breakpoints, ARRAYSZ(breakpoints)) + 1;
}

#ifdef WIZARD
static string _wizard_spell_power_numeric_string(spell_type spell)
{
    const int cap = spell_power_cap(spell);
    if (cap == 0)
        return "N/A";
    const int power = min(calc_spell_power(spell), cap);
    return make_stringf("%d (%d)", power, cap);
}
#endif

// TODO: deduplicate with the same-named function in describe-spells.cc
static dice_def _spell_damage(spell_type spell, int power)
{
    if (power < 0)
        return dice_def(0,0);
    switch (spell)
    {
        case SPELL_FRIGID_HALO:
            return freeze_damage(power);
        case SPELL_WINTERS_EMBRACE:
            return winter_damage(power);
        case SPELL_FULMINANT_PRISM:
            return prism_damage(prism_hd(power, false), true);
        case SPELL_CONJURE_BALL_LIGHTNING:
            return ball_lightning_damage(ball_lightning_hd(power, false));
        case SPELL_IOOD:
            return iood_damage(power, INFINITE_DISTANCE, false);
        case SPELL_IRRADIATE:
            return irradiate_damage(power, false);
        case SPELL_SHATTER:
            return shatter_damage(power);
        case SPELL_SCORCH:
            return scorch_damage(power);
        case SPELL_BATTLESPHERE:
            return battlesphere_damage(power);
        case SPELL_FROZEN_RAMPARTS:
            return ramparts_damage(power, false);
        case SPELL_LRD:
            return base_fragmentation_damage(power, false);
        case SPELL_ARCJOLT:
            return arcjolt_damage(power, false);
        case SPELL_POLAR_VORTEX:
            return polar_vortex_dice(power, false);
        case SPELL_NOXIOUS_BOG:
            return toxic_bog_damage();
        case SPELL_BOULDER:
            return boulder_damage(power, false);
        default:
            break;
    }
    const zap_type zap = spell_to_zap(spell);
    if (zap == NUM_ZAPS)
        return dice_def(0,0);
    return zap_damage(zap, power, false, false);
}

string spell_max_damage_string(spell_type spell)
{
    switch (spell)
    {
    case SPELL_MAXWELLS_COUPLING:
    case SPELL_FREEZING_CLOUD:
    case SPELL_STEAM_BURST:
        // These have damage strings, but don't scale with power.
        return "";
    default:
        break;
    }
    // Only show a distinct max damage string if we're not at max power
    // already. Otherwise, it's redundant!
    const int pow = calc_spell_power(spell);
    const int max_pow = spell_power_cap(spell);
    if (pow >= max_pow)
        return "";
    return spell_damage_string(spell, false, max_pow);
}

string spell_damage_string(spell_type spell, bool evoked, int pow)
{
    if (pow == -1)
        pow = evoked ? wand_power(spell) : calc_spell_power(spell);
    switch (spell)
    {
        case SPELL_MAXWELLS_COUPLING:
            return Options.char_set == CSET_ASCII ? "death" : "\u221e"; //"∞"
        case SPELL_THUNDERBOLT_HD:
        {
            const int max = 50 + 10 * pow;
            return make_stringf("40-%d", max);
        }
        case SPELL_FREEZING_CLOUD:
            return desc_cloud_damage(CLOUD_COLD, false);
        case SPELL_STEAM_BURST:
            return desc_cloud_damage(CLOUD_STEAM, false);
        case SPELL_DISCHARGE:
        {
            const int max = discharge_max_damage(pow);
            return make_stringf("%d-%d/arc", FLAT_DISCHARGE_ARC_DAMAGE, max);
        }
        case SPELL_AIRSTRIKE:
            return describe_airstrike_dam(base_airstrike_damage(pow));
        case SPELL_RIMEBLIGHT:
            return describe_rimeblight_damage(pow, true);
        default:
            break;
    }
    const dice_def dam = _spell_damage(spell, pow);
    if (dam.num == 0 || dam.size == 0)
        return "";
    string mult = "";
    switch (spell)
    {
        case SPELL_FOXFIRE:
        case SPELL_PLASMA_BEAM:
        case SPELL_PERMAFROST_ERUPTION:
            mult = "2x";
            break;
        case SPELL_CONJURE_BALL_LIGHTNING:
            mult = "3x";
            break;
        default:
            break;
    }
    const string dam_str = make_stringf("%s%dd%d", mult.c_str(), dam.num, dam.size);
    if (spell == SPELL_LRD || spell == SPELL_SHATTER || spell == SPELL_POLAR_VORTEX)
        return dam_str + "*"; // many special cases of more/less damage
    return dam_str;
}

int spell_acc(spell_type spell)
{
    const zap_type zap = spell_to_zap(spell);
    if (zap == NUM_ZAPS)
        return -1;
    if (zap_explodes(zap) || zap_is_enchantment(zap))
        return -1;
    const int power = calc_spell_power(spell);
    if (power < 0)
        return -1;
    const int acc = zap_to_hit(zap, power, false);
    if (acc == AUTOMATIC_HIT)
        return -1;
    return acc;
}

int spell_power_percent(spell_type spell)
{
    const int pow = calc_spell_power(spell);
    const int max_pow = spell_power_cap(spell);
    if (max_pow == 0)
        return -1; // should never happen for player spells
    return pow * 100 / max_pow;
}

string spell_power_string(spell_type spell)
{
    return _wizard_spell_power_numeric_string(spell);
}

int calc_spell_range(spell_type spell, int power, bool allow_bonus,
                     bool ignore_shadows)
{
    if (power == 0)
        power = calc_spell_power(spell);
    const int range = spell_range(spell, power, allow_bonus, ignore_shadows);

    return range;
}

/**
 * Give a string visually describing a given spell's range, as cast by the
 * player.
 *
 * @param spell     The spell in question.
 * @return          Something like "@-->.."
 */
string spell_range_string(spell_type spell)
{
    if (spell == SPELL_HAILSTORM)
        return "@.->"; // Special case: hailstorm is a ring

    const int cap      = spell_power_cap(spell);
    const int range    = calc_spell_range(spell, 0);
    const int maxrange = calc_spell_range(spell, cap, true, true);

    return range_string(range, maxrange, '@');
}

/**
 * Give a string visually describing a given spell's range.
 *
 * E.g., for a spell of fixed range 1 (melee), "@>"
 *       for a spell of range 3, max range 5, "@-->.."
 *
 * @param range         The current range of the spell.
 * @param maxrange      The range the spell would have at max power.
 * @param caster_char   The character used to represent the caster.
 *                      Usually @ for the player.
 * @return              See above.
 */
string range_string(int range, int maxrange, char32_t caster_char)
{
    if (range <= 0)
        return "N/A";

    return stringize_glyph(caster_char) + string(range - 1, '-')
           + string(">") + string(maxrange - range, '.');
}

string spell_schools_string(spell_type spell)
{
    string desc;

    bool already = false;
    for (const auto bit : spschools_type::range())
    {
        if (spell_typematch(spell, bit))
        {
            if (already)
                desc += "/";
            desc += spelltype_long_name(bit);
            already = true;
        }
    }

    return desc;
}

void spell_skills(spell_type spell, set<skill_type> &skills)
{
    const spschools_type disciplines = get_spell_disciplines(spell);
    for (const auto bit : spschools_type::range())
        if (disciplines & bit)
            skills.insert(spell_type2skill(bit));
}

void do_demonic_magic(int pow, int rank)
{
    if (rank < 1)
        return;

    mprf("Malevolent energies surge around you.");

    for (radius_iterator ri(you.pos(), rank, C_SQUARE, LOS_NO_TRANS, true); ri; ++ri)
    {
        monster *mons = monster_at(*ri);

        if (!mons || mons->wont_attack() || !mons_is_threatening(*mons))
            continue;

        if (mons->check_willpower(&you, pow) <= 0)
            mons->paralyse(&you, random_range(2, 5));
    }
}
