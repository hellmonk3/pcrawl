#pragma once

#include <unordered_map>
#include <string>

#include "item-prop-enum.h"

// Used in spl-damage.cc for lightning rod damage calculations
const int LIGHTNING_CHARGE_MULT = 100;
const int LIGHTNING_MAX_CHARGE = 4;

struct recharge_messages
{
    string noisy;
    string silent;
};

struct evoker_data
{
    const char * key;
    int charge_xp_debt;
    int max_charges;
    recharge_messages recharge_msg;
};

static const unordered_map<misc_item_type, evoker_data, std::hash<int>> xp_evoker_data = {
    { MISC_PHIAL_OF_FLOODS, { "phial_debt", 2, 1 ,
        { "You hear a faint sloshing from %s as it returns to readiness.",
          "Water glimmers in %s, now refilled and ready to use.", },
    }},
    { MISC_HORN_OF_GERYON, { "horn_debt", 1, 1 } },
    { MISC_LIGHTNING_ROD,  { "rod_debt", 1, LIGHTNING_MAX_CHARGE } },
    { MISC_TIN_OF_TREMORSTONES, { "tin_debt", 2, 2 } },
    { MISC_PHANTOM_MIRROR, { "mirror_debt", 3, 1 } },
    { MISC_BOX_OF_BEASTS, { "box_debt", 1, 1 } },
    { MISC_SACK_OF_SPIDERS, { "sack_debt", 1, 1,
        { "You hear chittering from %s. It's ready.",
          "%s twitches, refilled and ready to use.", },
    }},
    { MISC_CONDENSER_VANE, { "condenser_debt", 1, 1 } },
    { MISC_DUNGEON_ATLAS, { "atlas_debt", 3, 1 } },
    { MISC_HARP_OF_HEALING, { "harp_debt", 2, 1 } },
    { MISC_MAGES_CHALICE, { "chalice_debt", 1, 1 } },
    { MISC_BUTTERFLY_JAR, { "jar_debt", 1, 1 } },
    { MISC_PURPLE_STATUETTE, { "purple_debt", 2, 1 } },
    { MISC_MAGNET, { "magnet_debt", 1, 3 } },
    { MISC_LANTERN_OF_SHADOWS, { "lantern_debt", 1, 1 } },
};
