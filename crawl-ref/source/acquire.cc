/**
 * @file
 * @brief Acquirement and Trog/Oka/Sif gifts.
**/

#include "AppHdr.h"

#include "acquire.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <set>

#include "ability.h"
#include "artefact.h"
#include "art-enum.h"
#include "colour.h"
#include "describe.h"
#include "dungeon.h"
#include "god-item.h"
#include "god-passive.h"
#include "item-name.h"
#include "item-prop.h"
#include "item-status-flag-type.h"
#include "items.h"
#include "item-use.h"
#include "invent.h"
#include "known-items.h"
#include "libutil.h"
#include "macro.h"
#include "message.h"
#include "notes.h"
#include "output.h"
#include "options.h"
#include "prompt.h"
#include "randbook.h"
#include "random.h"
#include "religion.h"
#include "shopping.h"
#include "skills.h"
#include "spl-book.h"
#include "spl-util.h"
#include "state.h"
#include "stringutil.h"
#include "tag-version.h"
#include "terrain.h"
#include "unwind.h"
#include "ui.h"

static equipment_type _acquirement_armour_slot(bool);
static armour_type _acquirement_armour_for_slot(equipment_type, bool);
static armour_type _acquirement_shield_type();
static armour_type _acquirement_body_armour(bool);

/**
 * Get a randomly rounded value for the player's specified skill, unmodified
 * by crosstraining, draining, etc.
 *
 * @param skill     The skill in question; e.g. SK_ARMOUR.
 * @param mult      A multiplier to the skill, for higher precision.
 * @return          A rounded value of that skill; e.g. _skill_rdiv(SK_ARMOUR)
 *                  for a value of 10.9 will return 11 90% of the time &
 *                  10 the remainder.
 */
static int _skill_rdiv(skill_type skill, int mult = 1)
{
    const int scale = 256;
    return div_rand_round(you.skill(skill, mult * scale, true), scale);
}

/**
 * Choose a random subtype of armour to generate through acquirement/divine
 * gifts.
 *
 * Guaranteed to be wearable, in principle.
 *
 * @param divine    Lowers the odds of high-tier body armours being chosen.
 * @return          The armour_type of the armour to be generated.
 */
static int _acquirement_armour_subtype(bool divine, int & /*quantity*/,
                                       int /*agent*/)
{
    const equipment_type slot_type = _acquirement_armour_slot(divine);
    return _acquirement_armour_for_slot(slot_type, divine);
}

/**
 * Take a set of weighted elements and a filter, and return a random element
 * from those elements that fulfills the filter condition.
 *
 * @param weights       The elements to choose from.
 * @param filter        An optional filter; if present, only elements for which
 *                      the filter returns true may be chosen.
 * @return              A random element from the given list.
 */
template<class M>
M filtered_vector_select(vector<pair<M, int>> weights, function<bool(M)> filter)
{
    for (auto &weight : weights)
    {
        if (filter && !filter(weight.first))
            weight.second = 0;
        else
            weight.second = max(weight.second, 0); // cleanup
    }

    M *chosen_elem = random_choose_weighted(weights);
    ASSERT(chosen_elem);
    return *chosen_elem;
}

/**
 * Choose a random slot to acquire armour for.
 *
 * For most races, even odds for all armour slots when acquiring, or 50-50
 * split between body armour/aux armour when getting god gifts.
 *
 * Nagas and Armataurs get a high extra chance for bardings, especially if they haven't
 * seen any yet.
 *
 * Guaranteed to be wearable, in principle.
 *
 * @param divine    Whether the item is a god gift.
 * @return          A random equipment slot; e.g. EQ_SHIELD, EQ_BODY_ARMOUR...
 */
static equipment_type _acquirement_armour_slot(bool divine)
{
    if (you.wear_barding()
        && one_chance_in(you.seen_armour[ARM_BARDING] ? 4 : 2))
    {
        return EQ_BOOTS;
    }

    vector<pair<equipment_type, int>> weights = {
        { EQ_BODY_ARMOUR,   divine ? 5 : 1 },
        { EQ_SHIELD,        1 },
        { EQ_CLOAK,         1 },
        { EQ_HELMET,        1 },
        { EQ_GLOVES,        1 },
        { EQ_BOOTS,         1 },
    };

    return filtered_vector_select<equipment_type>(weights,
        [] (equipment_type etyp) {
            // return true if the player can wear at least something in this
            // equipment type
            return you_can_wear(etyp) != false;
        });
}


/**
 * Choose a random subtype of armour that will fit in the given equipment slot,
 * to generate through acquirement/divine gifts.
 *
 * Guaranteed to be usable by the player & weighted weakly by their skills;
 * heavy investment in armour skill, relative to dodging & spellcasting, makes
 * heavier armours more likely to be generated.
 *
 * @param divine    Whether the armour is a god gift.
 * @return          The armour_type of the armour to be generated.
 */
static armour_type _acquirement_armour_for_slot(equipment_type slot_type,
                                                bool divine)
{
    switch (slot_type)
    {
        case EQ_CLOAK:
            if (you_can_wear(EQ_CLOAK))
                return random_choose(ARM_CLOAK, ARM_SCARF);
            return ARM_SCARF;
        case EQ_GLOVES:
            return ARM_GLOVES;
        case EQ_BOOTS:
            if (you.wear_barding())
                return ARM_BARDING;
            else
                return ARM_BOOTS;
        case EQ_HELMET:
            if (you_can_wear(EQ_HELMET))
                return random_choose(ARM_HELMET, ARM_HAT);
            return ARM_HAT;
        case EQ_SHIELD:
            return _acquirement_shield_type();
        case EQ_BODY_ARMOUR:
            return _acquirement_body_armour(divine);
        default:
            die("Unknown armour slot %d!", slot_type);
    }
}

/**
 * Choose a random type of shield to be generated via acquirement or god gifts.
 *
 * Weighted by Shields skill & the secret racial shield bonus.
 *
 * Ratios by shields skill & player size (B = buckler, K = kite shield, T = tower shield)
 *
 *     Shields    0           5         10          15        20
 * Large:   {6B}/5K/4T  ~{1B}/1K/1T  ~{1B}/5K/7T  ~2K/3T     1K/2T
 * Med.:        2B/1K    6B/4K/1T      2B/2K/1T   4B/8K/3T   1K/1T
 * Small:      ~3B/1K     ~5B/2K      ~2B/1K     ~3B/2K     ~1B/1K
 *
 * XXX: possibly shield skill should count for more for non-med races?
 *
 * @return A potentially wearable type of shield.
 */
static armour_type _acquirement_shield_type()
{
    // Fixed chance for an orb.
    if (one_chance_in(4))
        return ARM_ORB;

    const int scale = 256;
    vector<pair<armour_type, int>> weights = {
        { ARM_BUCKLER,       (5 + player_shield_racial_factor()) * 4 * scale
                                - _skill_rdiv(SK_SHIELDS, scale) },
        { ARM_KITE_SHIELD,        10 * scale },
        { ARM_TOWER_SHIELD,  20 * scale
                             - (5 + player_shield_racial_factor()) * 4 * scale
                             + _skill_rdiv(SK_SHIELDS, scale / 2) },
    };

    return filtered_vector_select<armour_type>(weights, [] (armour_type shtyp) {
        return check_armour_size(shtyp,  you.body_size(PSIZE_TORSO, true));
    });
}

/**
 * Determine the weight (likelihood) to acquire a specific type of body armour.
 *
 * If divine is set, returns the base weight for the armour type.
 * Otherwise, if warrior is set, multiplies the base weight by the base ac^2.
 * Otherwise, uses the player's Armour skill to crudely guess how likely they
 * are to want the armour, based on its EVP.
 *
 * @param armour    The type of armour in question. (E.g. ARM_ROBE.)
 * @param divine    Whether the 'acquirement' is actually a god gift.
 * @param warrior   Whether we think the player only cares about AC.
 * @return          A weight for the armour.
 */
static int _body_acquirement_weight(armour_type armour,
                                    bool divine, bool warrior)
{
    const int base_weight = armour_acq_weight(armour);
    if (divine)
        return base_weight; // gods don't care about your skills, apparently

    if (warrior)
    {
        const int ac = armour_prop(armour, PARM_AC);
        return base_weight * ac * ac;
    }

    // highest chance when armour skill = (displayed) evp - 3
    const int evp = armour_prop(armour, PARM_EVASION);
    const int skill = min(27, _skill_rdiv(SK_ARMOUR) + 3);
    const int sk_diff = skill + evp / 10;
    const int inv_diff = max(1, 27 - sk_diff);
    // armour closest to ideal evp is 27^3 times as likely as the furthest away
    return base_weight * inv_diff * inv_diff * inv_diff;
}

/**
 * Choose a random type of body armour to be generated via acquirement or
 * god gifts.
 *
 * @param divine      Whether the armour is a god gift.
 * @return A potentially wearable type of body armour.
 */
static armour_type _acquirement_body_armour(bool divine)
{
    // Using an arbitrary legacy formula, do we think the player doesn't care
    // about armour EVP?
    int light_pref = _skill_rdiv(SK_SPELLCASTING, 3);
    light_pref += _skill_rdiv(SK_DODGING);
    light_pref = random2(light_pref);
    const bool warrior = light_pref < random2(_skill_rdiv(SK_ARMOUR, 2));

    vector<pair<armour_type, int>> weights;
    for (int i = ARM_FIRST_MUNDANE_BODY; i < NUM_ARMOURS; ++i)
    {
        const armour_type armour = (armour_type)i;
        if (get_armour_slot(armour) != EQ_BODY_ARMOUR)
            continue;

        if (!check_armour_size(armour, you.body_size(PSIZE_TORSO, true)))
            continue;

        const int weight = _body_acquirement_weight(armour, divine, warrior);

        if (weight)
        {
            const pair<armour_type, int> weight_pair = { armour, weight };
            weights.push_back(weight_pair);
        }
    }

    const armour_type* armour_ptr = random_choose_weighted(weights);
    ASSERT(armour_ptr);
    return *armour_ptr;
}

static armour_type _pick_unseen_armour()
{
    // Consider shields uninteresting always, since unlike with other slots
    // players might well prefer an empty slot to wearing one. We don't
    // want to try to guess at this by looking at their weapon's handedness
    // because this would encourage switching weapons or putting on a
    // shield right before reading acquirement in some cases. --elliptic
    // This affects only the "unfilled slot" special-case, not regular
    // acquirement which can always produce (wearable) shields.
    static const equipment_type armour_slots[] =
        {  EQ_CLOAK, EQ_HELMET, EQ_GLOVES, EQ_BOOTS  };

    armour_type picked = NUM_ARMOURS;
    int count = 0;
    for (auto &slot : armour_slots)
    {
        if (!you_can_wear(slot))
            continue;

        const armour_type sub_type = _acquirement_armour_for_slot(slot, false);
        ASSERT(sub_type != NUM_ARMOURS);

        if (!you.seen_armour[sub_type] && one_chance_in(++count))
            picked = sub_type;
    }

    return picked;
}

static int _acquirement_weapon_subtype(bool divine, int & /*quantity*/, int agent)
{
    // Now choose a subtype which uses that skill.
    int result = OBJ_RANDOM;
    int count = 0;
    item_def item_considered;
    item_considered.base_type = OBJ_WEAPONS;
    for (int i = 0; i < NUM_WEAPONS; ++i)
    {
        item_considered.sub_type = i;

        int acqweight = property(item_considered, PWPN_ACQ_WEIGHT);

        if (!acqweight)
            continue;

        const bool two_handed = you.hands_reqd(item_considered) == HANDS_TWO;

        if (two_handed && you.get_mutation_level(MUT_MISSING_HAND))
            continue;

        // reservoir sampling
        if (x_chance_in_y(acqweight, count += acqweight))
            result = i;
    }
    ASSERT(result != OBJ_RANDOM); // make sure loop ran at least once
    return result;
}

static int _acquirement_missile_subtype(bool /*divine*/, int & /*quantity*/,
                                        int /*agent*/)
{
    // Choose from among all usable missile types.
    vector<pair<missile_type, int> > missile_weights;

    missile_weights.emplace_back(MI_BOOMERANG, 1);
    missile_weights.emplace_back(MI_DART, 1);

    if (you.body_size() >= SIZE_MEDIUM)
        missile_weights.emplace_back(MI_JAVELIN, 1);

    if (you.can_throw_large_rocks())
        missile_weights.emplace_back(MI_LARGE_ROCK, 1);

    return *random_choose_weighted(missile_weights);
}

static int _acquirement_jewellery_subtype(bool /*divine*/, int & /*quantity*/,
                                          int /*agent*/)
{
    int result = 0;

    // Rings are (number of usable rings) times as common as amulets.
    const int ring_num = you.arm_count();

    // Try ten times to give something the player hasn't seen.
    for (int i = 0; i < 10; i++)
    {
        result = one_chance_in(ring_num + 1) ? get_random_amulet_type()
                                             : get_random_ring_type();

        // If we haven't seen this yet, we're done.
        if (!get_ident_type(OBJ_JEWELLERY, result))
            break;
    }

    return result;
}

static const vector<pair<misc_item_type, int> > _misc_base_weights()
{
    const bool no_allies = you.allies_forbidden();
    vector<pair<misc_item_type, int> > choices =
    {
        {MISC_BOX_OF_BEASTS,       (no_allies ? 0 : 20)},
        {MISC_SACK_OF_SPIDERS,     (no_allies ? 0 : 20)},
        {MISC_PHANTOM_MIRROR,      (no_allies ? 0 : 20)},
        // Tremorstones are better for heavily armoured characters.
        {MISC_TIN_OF_TREMORSTONES, 5 + _skill_rdiv(SK_ARMOUR) / 3},
        // everything else is evenly weighted
        {MISC_LIGHTNING_ROD,       20},
        {MISC_PHIAL_OF_FLOODS,     20},
        {MISC_CONDENSER_VANE,      20},
    };
    // The player never needs more than one of any of these.
    for (auto &p : choices)
        if (you.seen_misc[p.first])
            p.second = 0;
    return choices;
}

static bool _unided_acq_misc()
{
    const vector<pair<misc_item_type, int> > choices = _misc_base_weights();
    for (auto &p : choices)
        if (p.second > 0)
            return true;
    return false;
}

/**
 * Return a miscellaneous evokable item for acquirement.
 * @return   The item type chosen.
 */
static int _acquirement_misc_subtype(bool /*divine*/, int & /*quantity*/,
                                     int /*agent*/)
{
    const vector<pair<misc_item_type, int> > choices = _misc_base_weights();
    const misc_item_type * const choice = random_choose_weighted(choices);

    // Possible for everything to be 0 weight - if so just give a random spare.
    // should not be used in normal acquirement..
    if (choice == nullptr)
    {
        return random_choose(MISC_BOX_OF_BEASTS,
                             MISC_SACK_OF_SPIDERS,
                             MISC_PHANTOM_MIRROR,
                             MISC_TIN_OF_TREMORSTONES,
                             MISC_LIGHTNING_ROD,
                             MISC_PHIAL_OF_FLOODS,
                             MISC_CONDENSER_VANE);
    }

    return *choice;
}

static int _acquirement_book_subtype(bool /*divine*/, int & /*quantity*/,
                                     int /*agent*/)
{
    return BOOK_MINOR_MAGIC;
    //this gets overwritten later, but needs to be a sane value
    //or asserts will get set off
}

typedef int (*acquirement_subtype_finder)(bool divine, int &quantity, int agent);
static const acquirement_subtype_finder _subtype_finders[] =
{
    _acquirement_weapon_subtype,
    _acquirement_missile_subtype,
    _acquirement_armour_subtype,
    0,
#if TAG_MAJOR_VERSION == 34
    0, // no food
#endif
    0, // no scrolls
    _acquirement_jewellery_subtype,
    0, // no potions
    _acquirement_book_subtype,
    0,
    0, // no, you can't acquire the orb
    _acquirement_misc_subtype,
    0, // no corpses
    0, // gold handled elsewhere, and doesn't have subtypes anyway
#if TAG_MAJOR_VERSION == 34
    0, // no rods
#endif
    0, // no runes either
    0, // no talismans... for now. TODO: add talisman acquirement
};

static int _find_acquirement_subtype(object_class_type &class_wanted,
                                     int &quantity, bool divine,
                                     int agent)
{
    COMPILE_CHECK(ARRAYSZ(_subtype_finders) == NUM_OBJECT_CLASSES);
    ASSERT(class_wanted != OBJ_RANDOM);

    if (class_wanted == OBJ_ARMOUR && you.has_mutation(MUT_NO_ARMOUR)
        || class_wanted == OBJ_WEAPONS && you.has_mutation(MUT_NO_GRASPING)
        || you.species == SP_OCTOPODE && class_wanted == OBJ_ARMOUR
           && you.has_mutation(MUT_MISSING_HAND)
           && bool(!you_can_wear(EQ_HELMET)))
    {
        return OBJ_RANDOM;
    }

    int type_wanted = OBJ_RANDOM;

    int useless_count = 0;

    do
    {
        if (_subtype_finders[class_wanted])
        {
            type_wanted =
                (*_subtype_finders[class_wanted])(divine, quantity, agent);
        }

        // Double-check our subtype for weapons is valid
        ASSERT(class_wanted != OBJ_WEAPONS || type_wanted < get_max_subtype(class_wanted));

        item_def dummy;
        dummy.base_type = class_wanted;
        dummy.sub_type = type_wanted;
        dummy.plus = 1; // empty wands would be useless
        dummy.flags |= ISFLAG_IDENT_MASK;

        if (!is_useless_item(dummy, false) && !god_hates_item(dummy)
            && (agent >= NUM_GODS || god_likes_item_type(dummy,
                                                         (god_type)agent)))
        {
            break;
        }
    }
    while (useless_count++ < 200);

    return type_wanted;
}

// The weight of a spell takes into account its disciplines' skill levels
// and the spell difficulty.
static int _spell_weight(spell_type spell)
{
    ASSERT(spell != SPELL_NO_SPELL);

    int weight = 0;
    spschools_type disciplines = get_spell_disciplines(spell);
    int count = 0;
    for (const auto disc : spschools_type::range())
    {
        if (disciplines & disc)
        {
            weight += _skill_rdiv(spell_type2skill(disc));
            count++;
        }
    }
    ASSERT(count > 0);

    // Particularly difficult spells _reduce_ the overall weight.
    int leveldiff = 5 - spell_difficulty(spell);

    return max(0, 2 * weight/count + leveldiff);
}

// When randomly picking a book for acquirement, use the sum of the
// weights of all unknown spells in the book.
static int _book_weight(book_type book)
{
    ASSERT_RANGE(book, 0, NUM_BOOKS);
    ASSERT(book != BOOK_MANUAL);
    ASSERT(book != BOOK_RANDART_LEVEL);
    ASSERT(book != BOOK_RANDART_THEME);

    int total_weight = 0;
    for (spell_type stype : spellbook_template(book))
    {
        // Skip over spells already in library.
        if (you.spell_library[stype])
            continue;
        if (god_hates_spell(stype, you.religion))
            continue;

        total_weight += _spell_weight(stype);
    }

    return total_weight;
}

static bool _do_book_acquirement(item_def &book, int agent)
{
    // items() shouldn't make book a randart for acquirement items.
    ASSERT(!is_random_artefact(book));

    const int choice = random_choose_weighted(
                                    30, BOOK_RANDART_THEME,
       agent == GOD_SIF_MUNA ? 10 : 40, NUM_BOOKS, // normal books
                                     1, BOOK_RANDART_LEVEL);

    switch (choice)
    {
    default:
    case NUM_BOOKS:
    {
        int total_weights = 0;

        // Pick a random spellbook according to unknown spells contained.
        int weights[NUM_BOOKS] = { 0 };
        for (int bk = 0; bk < NUM_BOOKS; bk++)
        {
            const auto bkt = static_cast<book_type>(bk);

            if (!book_exists(bkt))
                continue;

            weights[bk]    = _book_weight(bkt);
            total_weights += weights[bk];
        }

        if (total_weights > 0)
        {
            book.sub_type = choose_random_weighted(weights, end(weights));
            break;
        }
        acquire_themed_randbook(book, agent);
        break;
    }

    case BOOK_RANDART_THEME:
        acquire_themed_randbook(book, agent);
        break;

    case BOOK_RANDART_LEVEL:
    {
        const int level = agent == GOD_XOM ?
            random_range(1, 9) :
            max(1, (_skill_rdiv(SK_SPELLCASTING) + 2) / 3);

        book.sub_type  = BOOK_RANDART_LEVEL;
        if (!make_book_level_randart(book, level, agent == GOD_SIF_MUNA))
            return false;
        break;
    }
    } // switch book choice

    set_ident_flags(book, ISFLAG_IDENT_MASK);

    return true;
}

static int _failed_acquirement(bool quiet)
{
    if (!quiet)
        mpr("The demon of the infinite void smiles upon you.");
    return NON_ITEM;
}

static bool _armour_slot_seen(armour_type arm)
{
    item_def item;
    item.base_type = OBJ_ARMOUR;
    item.quantity = 1;

    for (int i = 0; i < NUM_ARMOURS; i++)
    {
        if (get_armour_slot(arm) != get_armour_slot((armour_type)i))
            continue;
        item.sub_type = i;

        // having seen a helmet means nothing about your decision to go
        // bare-headed if you have horns
        if (!can_wear_armour(item, false, true))
            continue;

        if (you.seen_armour[i])
            return true;
    }
    return false;
}

static bool _is_armour_plain(const item_def &item)
{
    ASSERT(item.base_type == OBJ_ARMOUR);
    if (is_artefact(item))
        return false;

    if (armour_is_special(item))
    {
        // These are always interesting, even with no brand.
        // May still be redundant, but that has another check.
        return false;
    }

    return get_armour_ego_type(item) == SPARM_NORMAL;
}

// ugh
#define ITEM_LEVEL (divine ? ISPEC_GIFT : ISPEC_GOOD_ITEM)

/**
 * Take a newly-generated acquirement item, and adjust its brand if we don't
 * like it.
 *
 * Specifically, when any of:
 *   - The god doesn't like the brand (for divine gifts)
 *   - We think the brand is too weak (for non-divine gifts)
 *   - Sometimes if we've seen the brand before.
 *
 * @param item      The item which may have its brand adjusted. Not necessarily
 *                  a weapon or piece of armour.
 * @param divine    Whether the item is a god gift, rather than from
 *                  acquirement proper.
 * @param agent     The source of the acquirement. For god gifts, it's equal to
 *                  the god.
 */
static void _adjust_brand(item_def &item, bool divine, int agent)
{
    if (item.base_type != OBJ_WEAPONS && item.base_type != OBJ_ARMOUR)
        return; // don't reroll missile brands, I guess

    if (is_artefact(item))
        return; // their own kettle of fish

    // Trog has a restricted brand table.
    if (agent == GOD_TROG && item.base_type == OBJ_WEAPONS)
    {
        // 75% chance of a brand
        item.brand = random_choose(SPWPN_NORMAL, SPWPN_HEAVY,
                                   SPWPN_EXPLOSIVE, SPWPN_ANTIMAGIC);
        return;
    }
}

/**
 * Should the given item be rejected as an acquirement/god gift result &
 * rerolled? If so, why?
 *
 * @param item      The item in question.
 * @param agent     The entity creating the item; possibly a god.
 * @return          A reason why the item should be rejected, if it should be;
 *                  otherwise, the empty string.
 */
static string _why_reject(const item_def &item, int agent)
{
    if (agent != GOD_XOM
        && (item.base_type == OBJ_WEAPONS
                && !can_wield(&item, false, true)
            || item.base_type == OBJ_ARMOUR
                && !can_wear_armour(item, false, true)))
    {
        return "Destroying unusable weapon or armour!";
    }

    // Trog does not gift the Wrath of Trog.
    if (agent == GOD_TROG && is_unrandom_artefact(item, UNRAND_TROG))
        return "Destroying Trog-gifted Wrath of Trog!";

    // Pain brand is useless if you've sacrificed Necromacy.
    if (you.get_mutation_level(MUT_NO_NECROMANCY_MAGIC)
        && get_weapon_brand(item) == SPWPN_PAIN)
    {
        return "Destroying pain weapon after Necro sac!";
    }

    return ""; // all OK
}

int acquirement_create_item(object_class_type class_wanted,
                            int agent, bool quiet,
                            const coord_def &pos)
{
    ASSERT(class_wanted != OBJ_RANDOM);

    const bool divine = (agent == GOD_OKAWARU || agent == GOD_XOM
                         || agent == GOD_TROG);
    int thing_created = NON_ITEM;
    int quant = 1;
#define MAX_ACQ_TRIES 40
    for (int item_tries = 0; item_tries < MAX_ACQ_TRIES; item_tries++)
    {
        int type_wanted = -1;

        // This may clobber class_wanted (e.g. staves)
        type_wanted = _find_acquirement_subtype(class_wanted, quant,
                                                    divine, agent);
        ASSERT(type_wanted != -1);

        // Don't generate randart books in items(), we do that
        // ourselves.
        bool want_arts = (class_wanted != OBJ_BOOKS);

        thing_created = items(want_arts, class_wanted, type_wanted,
                              you.depth, 0, agent);

        if (thing_created == NON_ITEM)
        {
            if (!quiet)
                dprf("Failed to make thing!");
            continue;
        }

        item_def &acq_item(env.item[thing_created]);
        _adjust_brand(acq_item, divine, agent);

        // For plain armour, try to change the subtype to something
        // matching a currently unfilled equipment slot.
        if (acq_item.base_type == OBJ_ARMOUR && !is_artefact(acq_item))
        {
            if (agent != GOD_XOM
                && x_chance_in_y(MAX_ACQ_TRIES - item_tries, MAX_ACQ_TRIES + 5)
                || !divine
                && you.seen_armour[acq_item.sub_type]
                && !one_chance_in(3)
                && item_tries < 20)
            {
                // We have seen the exact item already, it's very unlikely
                // extras will do any good.
                // For scroll acquirement, prefer base items not seen before
                // as well, even if you didn't see the exact brand yet.
                destroy_item(thing_created, true);
                thing_created = NON_ITEM;
                if (!quiet)
                    dprf("Destroying already-seen item!");
                continue;
            }

            // Try to fill empty slots.
            if ((_is_armour_plain(acq_item)
                 || get_armour_slot(acq_item) == EQ_BODY_ARMOUR && coinflip())
                && _armour_slot_seen((armour_type)acq_item.sub_type))
            {
                armour_type at = _pick_unseen_armour();
                if (at != NUM_ARMOURS)
                {
                    destroy_item(thing_created, true);
                    thing_created = items(true, OBJ_ARMOUR, at,
                                          ITEM_LEVEL, 0, agent);
                }
                else if (agent != GOD_XOM && one_chance_in(3))
                {
                    // If the item is plain and there aren't any
                    // unfilled slots, we might want to roll again.
                    destroy_item(thing_created, true);
                    thing_created = NON_ITEM;
                    if (!quiet)
                        dprf("Destroying plain item!");
                    continue;
                }
            }
        }

        const string rejection_reason = _why_reject(acq_item, agent);
        if (!rejection_reason.empty())
        {
            if (!quiet)
                dprf("%s", rejection_reason.c_str());
            destroy_item(acq_item);
            thing_created = NON_ITEM;
            continue;
        }

        ASSERT(acq_item.is_valid());

        if (class_wanted == OBJ_WANDS)
            acq_item.plus = max(static_cast<int>(acq_item.plus), 3 + random2(3));
        else if (class_wanted == OBJ_GOLD)
            acq_item.quantity = random_range(200, 1400, 2);
        else if (class_wanted == OBJ_MISSILES)
        {
            // TODO: consider doubling the gift timeout instead of adjusting
            // gift quantity. That'd be an Oka nerf, but maybe it's fine?
            if (divine)
                acq_item.quantity = max(1, acq_item.quantity / 2);
            else
                acq_item.quantity *= 5;
        }
        else if (quant > 1)
            acq_item.quantity = quant;

        if (acq_item.base_type == OBJ_BOOKS)
        {
            if (!_do_book_acquirement(acq_item, agent))
            {
                destroy_item(acq_item, true);
                return _failed_acquirement(quiet);
            }
            // That might have changed the item's subtype.
            item_colour(acq_item);
        }
        else if (acq_item.base_type == OBJ_JEWELLERY
                 && !is_unrandom_artefact(acq_item))
        {
            switch (acq_item.sub_type)
            {
            case RING_EVASION:
                acq_item.plus = 5;
                break;
            case RING_PROTECTION:
            case RING_SLAYING:
                acq_item.plus = GOOD_RING_PLUS;
                break;

            default:
                break;
            }

            // bump jewel acq power up a bit
            if (one_chance_in(2) && !is_artefact(acq_item))
                make_item_randart(acq_item);
        }
        else if (acq_item.base_type == OBJ_WEAPONS
                 && !is_unrandom_artefact(acq_item))
        {
            // These can never get egos, and mundane versions are quite common,
            // so guarantee artefact status. Rarity is a bit low to compensate.
            // ...except actually, trog can give them antimagic brand, so...
            if (is_giant_club_type(acq_item.sub_type)
                && get_weapon_brand(acq_item) == SPWPN_NORMAL
                && !one_chance_in(25))
            {
                make_item_randart(acq_item, true);
            }

            if (agent == GOD_TROG)
                acq_item.plus += random2(3);
            // God gifts (except Xom's) never have a negative enchantment
            if (divine && agent != GOD_XOM)
                acq_item.plus = max(static_cast<int>(acq_item.plus), 0);
        }

        // Last check: don't acquire items your god hates.
        // Temporarily mark the type as ID'd for the purpose of checking if
        // it is a hated brand (this addresses, e.g., Elyvilon followers
        // immediately identifying evil weapons).
        // Note that Xom will happily give useless items!
        int oldflags = acq_item.flags;
        acq_item.flags |= ISFLAG_KNOW_TYPE | ISFLAG_KNOW_PROPERTIES;
        if ((is_useless_item(acq_item, false) && agent != GOD_XOM)
            || god_hates_item(acq_item))
        {
            if (!quiet)
                dprf("destroying useless item");
            destroy_item(thing_created);
            thing_created = NON_ITEM;
            continue;
        }
        acq_item.flags = oldflags;
        break;
    }

    if (thing_created == NON_ITEM)
        return _failed_acquirement(quiet);

    item_set_appearance(env.item[thing_created]); // cleanup

    if (thing_created != NON_ITEM)
    {
        ASSERT(env.item[thing_created].is_valid());
        env.item[thing_created].props[ACQUIRE_KEY].get_int() = agent;
    }

    ASSERT(!is_useless_item(env.item[thing_created], false) || agent == GOD_XOM);
    ASSERT(!god_hates_item(env.item[thing_created]));

    // If we have a zero coord_def, don't move the item to the grid. Used for
    // generating scroll of acquirement items.
    if (pos.origin())
        return thing_created;

    // Moving this above the move since it might not exist after falling.
    if (thing_created != NON_ITEM && !quiet)
        canned_msg(MSG_SOMETHING_APPEARS);

    // If a god wants to give you something but the floor doesn't want it,
    // it counts as a failed acquirement - no piety, etc cost.
    if (feat_destroys_items(env.grid(pos))
        && agent > GOD_NO_GOD
        && agent < NUM_GODS)
    {
        if (!quiet && agent == GOD_XOM)
            simple_god_message(" snickers.", GOD_XOM);
        else
            return _failed_acquirement(quiet);
    }

    move_item_to_grid(&thing_created, pos, quiet);

    return thing_created;
}

class AcquireMenu : public InvMenu
{
    friend class AcquireEntry;

    CrawlVector &acq_items;

    void init_entries();
    string get_keyhelp(bool unused) const override;
    bool examine_index(int i) override;
    bool skip_process_command(int keyin) override;
public:
    AcquireMenu(CrawlVector &aitems);
};

class AcquireEntry : public InvEntry
{
    string get_text() const override
    {
        const colour_t keycol = LIGHTCYAN;
        const string keystr = colour_to_str(keycol);
        const string itemstr =
            colour_to_str(menu_colour(text, item_prefix(*item), tag, false));
        const string gold_text = item->base_type == OBJ_GOLD
            ? make_stringf(" (you have %d gold)", you.gold) : "";
        return make_stringf(" <%s>%c %c </%s><%s>%s%s</%s>",
                            keystr.c_str(),
                            hotkeys[0],
                            selected() ? '+' : '-',
                            keystr.c_str(),
                            itemstr.c_str(),
                            text.c_str(),
                            gold_text.c_str(),
                            itemstr.c_str());
    }

public:
    AcquireEntry(const item_def& i) : InvEntry(i)
    {
        show_background = false;
    }
};

AcquireMenu::AcquireMenu(CrawlVector &aitems)
    : InvMenu(MF_SINGLESELECT | MF_QUIET_SELECT
              | MF_ALLOW_FORMATTING | MF_INIT_HOVER | MF_UNCANCEL),
      acq_items(aitems)
{
    menu_action = ACT_EXECUTE;
    action_cycle = CYCLE_TOGGLE;
    set_flags(get_flags() & ~MF_USE_TWO_COLUMNS);

    set_tag("acquirement");

    init_entries();

    set_title("Choose an item to acquire.");
}

static void _create_acquirement_item(item_def &item)
{
    auto &acq_items = you.props[ACQUIRE_ITEMS_KEY].get_vector();

    // Now that we have a selection, mark any generated unrands as not having
    // been generated, so they go back in circulation. Exclude the selected
    // item from this, if it's an unrand.
    for (item_def &aitem : acq_items)
    {
        if (is_unrandom_artefact(aitem)
            && (!is_unrandom_artefact(item)
                || !is_unrandom_artefact(aitem, item.unrand_idx)))
        {
            destroy_item(aitem, true);
        }
        // TODO: if we allow misc acquirement, also destroy unchosen miscs
    }

    take_note(Note(NOTE_ACQUIRE_ITEM, 0, 0, item.name(DESC_A),
              origin_desc(item)));
    item.flags |= (ISFLAG_NOTED_ID | ISFLAG_NOTED_GET);

    set_ident_type(item, true);

    if (move_item_to_inv(item))
        canned_msg(MSG_SOMETHING_APPEARS);
    else if (copy_item_to_grid(item, you.pos()))
        canned_msg(MSG_SOMETHING_APPEARS);
    else
        canned_msg(MSG_NOTHING_HAPPENS);

    acq_items.clear();
    you.props.erase(ACQUIRE_ITEMS_KEY);
}

void AcquireMenu::init_entries()
{
    menu_letter ckey = 'a';
    for (item_def& item : acq_items)
    {
        auto newentry = make_unique<AcquireEntry>(item);
        newentry->hotkeys.clear();
        newentry->add_hotkey(ckey++);
        add_entry(move(newentry));
    }

    on_single_selection = [this](const MenuEntry& item)
    {
        // update the more with a y/n prompt
        update_more();

        if (!yesno(nullptr, true, 'n', false, false, true))
        {
            deselect_all();
            update_more(); // go back to the regular more
            return true;
        }

        item_def &acq_item = *static_cast<item_def*>(item.data);
        _create_acquirement_item(acq_item);

        return false;
    };
}

string AcquireMenu::get_keyhelp(bool) const
{
    string help;
    vector<MenuEntry*> selected = selected_entries();
    if (selected.size() == 1 && menu_action == ACT_EXECUTE)
    {
        auto& entry = *selected[0];
        const string col = colour_to_str(channel_to_colour(MSGCH_PROMPT));
        help = make_stringf(
               "<%s>Acquire %s? (%s/N)</%s>\n",
               col.c_str(),
               entry.text.c_str(),
               Options.easy_confirm == easy_confirm_type::none ? "Y" : "y",
               col.c_str());
    }
    else
        help = "\n";
    // looks better with a margin:
    help += string(MIN_COLS, ' ') + '\n';

    help += make_stringf(
        //[!] acquire|examine item  [a-i] select item to acquire
        //[Esc/R-Click] exit
        "<lightgrey>%s%s  %s %s</lightgrey>",
        menu_keyhelp_cmd(CMD_MENU_CYCLE_MODE).c_str(),
        menu_action == ACT_EXECUTE ? " <w>acquire</w>|examine items" :
                                     " acquire|<w>examine</w> items",
        hyphenated_hotkey_letters(item_count(), 'a').c_str(),
        menu_action == ACT_EXECUTE ? "select item for acquirement"
                                   : "examine item");
    return pad_more_with(help, "");
}

bool AcquireMenu::examine_index(int i)
{
    ASSERT(i >= 0 && i < static_cast<int>(items.size()));
    // Use a copy to set flags that make the description better
    // See the similar code in shopping.cc for details about const
    // hygiene
    item_def &item = *static_cast<item_def*>(items[i]->data);

    item.flags |= (ISFLAG_IDENT_MASK | ISFLAG_NOTED_ID
                   | ISFLAG_NOTED_GET);
    describe_item_popup(item);
    deselect_all();

    return true;
}

bool AcquireMenu::skip_process_command(int keyin)
{
    // Bypass InvMenu::skip_process_command, which disables ! and ?
    return Menu::skip_process_command(keyin);
}

static item_def _acquirement_item_def(object_class_type item_type)
{
    item_def item;

    const int item_index = acquirement_create_item(item_type, AQ_SCROLL, true);

    if (item_index != NON_ITEM)
    {
        ASSERT(!god_hates_item(env.item[item_index]));

        // We make a copy of the item def, but we don't keep the real item.
        item = env.item[item_index];
        set_ident_flags(item,
                // Act as if we've received this item already to prevent notes.
                ISFLAG_IDENT_MASK | ISFLAG_NOTED_ID | ISFLAG_NOTED_GET);
        destroy_item(item_index);
    }

    return item;
}

vector<object_class_type> shuffled_acquirement_classes()
{
    vector<object_class_type> rand_classes;

    if (!you.has_mutation(MUT_NO_ARMOUR))
        rand_classes.emplace_back(OBJ_ARMOUR);

    if (!you.has_mutation(MUT_NO_GRASPING))
    {
        rand_classes.emplace_back(OBJ_WEAPONS);
    }

    rand_classes.emplace_back(OBJ_JEWELLERY);
    rand_classes.emplace_back(OBJ_BOOKS);

    if (_unided_acq_misc())
        rand_classes.emplace_back(OBJ_MISCELLANY);

    shuffle_array(rand_classes);
    return rand_classes;
}

void make_acquirement_items()
{
    vector<object_class_type> rand_classes = shuffled_acquirement_classes();
    const int num_wanted = min(3, (int) rand_classes.size());

    CrawlVector &acq_items = you.props[ACQUIRE_ITEMS_KEY].get_vector();
    acq_items.empty();

    // Generate item defs until we have enough, skipping any random classes
    // that fail to generate an item.
    for (auto obj_type : rand_classes)
    {
        if (acq_items.size() == num_wanted)
            break;

        auto item = _acquirement_item_def(obj_type);
        if (item.defined())
            acq_items.push_back(item);
    }
}

/*
 * Handle scroll of acquirement.
 *
 * Generate acquirement choices as items in a prop if these don't already exist
 * (because a scroll was read and canceled. Then either get the acquirement
 * choice from the c_choose_acquirement lua handler if one exists or present a
 * menu for the player to choose an item.
 *
 * returns True if the scroll was used, false if it was canceled.
*/
bool acquirement_menu()
{
    ASSERT(!crawl_state.game_is_arena());

    if (!you.props.exists(ACQUIRE_ITEMS_KEY))
        make_acquirement_items();

    auto &acq_items = you.props[ACQUIRE_ITEMS_KEY].get_vector();

    int index = 0;
    if (!clua.callfn("c_choose_acquirement", ">d", &index))
    {
        if (!clua.error.empty())
            mprf(MSGCH_ERROR, "Lua error: %s", clua.error.c_str());
    }
    else if (index >= 1 && index <= acq_items.size())
    {
        _create_acquirement_item(acq_items[index - 1]);
        return true;
    }

    AcquireMenu acq_menu(acq_items);
    acq_menu.show();

    return !you.props.exists(ACQUIRE_ITEMS_KEY);
}
