#pragma once

#include "tag-version.h"

// Be sure to update artp_data[] in artefact.cc. Also, _randart_propnames() in
// describe.cc, but order doesn't matter there. Also util/art-data.pl.
// Also wiz-item.cc rap_names.
enum artefact_prop_type
{
    ARTP_BRAND,
    ARTP_AC,
    ARTP_EVASION,
#if TAG_MAJOR_VERSION == 34
    ARTP_STRENGTH,
    ARTP_INTELLIGENCE,
    ARTP_DEXTERITY,
#endif
    ARTP_FIRE,
    ARTP_COLD,
    ARTP_ELECTRICITY,
#if TAG_MAJOR_VERSION == 34
    ARTP_POISON,
    ARTP_NEGATIVE_ENERGY,
#endif
    ARTP_WILLPOWER,
#if TAG_MAJOR_VERSION == 34
    ARTP_SEE_INVISIBLE,
#endif
    ARTP_INVISIBLE,
    ARTP_FLY,
    ARTP_BLINK,
#if TAG_MAJOR_VERSION == 34
    ARTP_BERSERK,
#endif
    ARTP_NOISE,
    ARTP_INHIBIT_SPELLCASTING,
#if TAG_MAJOR_VERSION == 34
    ARTP_CAUSE_TELEPORTATION,
#endif
    ARTP_PREVENT_TELEPORTATION,
    ARTP_ANGRY,
#if TAG_MAJOR_VERSION == 34
    ARTP_METABOLISM,
#endif
    ARTP_CONTAM,
#if TAG_MAJOR_VERSION == 34
    ARTP_ACCURACY,
#endif
    ARTP_SLAYING,
#if TAG_MAJOR_VERSION == 34
    ARTP_CURSE,
#endif
    ARTP_STEALTH,
    ARTP_MAGICAL_POWER,
    ARTP_BASE_DELAY,
    ARTP_HP,
    ARTP_CLARITY,
    ARTP_BASE_ACC,
    ARTP_BASE_DAM,
    ARTP_RMSL,
#if TAG_MAJOR_VERSION == 34
    ARTP_FOG,
#endif
    ARTP_REGENERATION,
#if TAG_MAJOR_VERSION == 34
    ARTP_SUSTAT,
#endif
    ARTP_NO_UPGRADE,
#if TAG_MAJOR_VERSION == 34
    ARTP_RCORR,
#endif
    ARTP_RMUT,
#if TAG_MAJOR_VERSION == 34
    ARTP_TWISTER,
#endif
    ARTP_CORRODE,
    ARTP_DRAIN,
    ARTP_SLOW,
#if TAG_MAJOR_VERSION == 34
    ARTP_FRAGILE,
#endif
    ARTP_SHIELDING,
    ARTP_HARM,
    ARTP_RAMPAGING,
    ARTP_ARCHMAGI,
    ARTP_ENHANCE_CONJ,
    ARTP_ENHANCE_HEXES,
    ARTP_ENHANCE_SUMM,
    ARTP_ENHANCE_NECRO,
    ARTP_ENHANCE_TLOC,
    ARTP_ENHANCE_TMUT,
    ARTP_ENHANCE_FIRE,
    ARTP_ENHANCE_ICE,
    ARTP_ENHANCE_AIR,
    ARTP_ENHANCE_EARTH,
#if TAG_MAJOR_VERSION == 34
    ARTP_ENHANCE_POISON,
#endif
    ARTP_NUM_PROPERTIES
};
