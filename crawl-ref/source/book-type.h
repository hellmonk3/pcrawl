#pragma once

#include "tag-version.h"

enum book_type
{
    BOOK_FIREBALL,
    BOOK_INNER_FLAME,
    BOOK_FOXFIRE,
    BOOK_SCORCH,
    BOOK_BLASTMOTES,
    BOOK_UNRAVELLING,
    BOOK_STEAM_BURST,
    BOOK_AMBULATORY_BOMB,
    BOOK_PLASMA_BEAM,
    BOOK_FLAME_LANCE,
    BOOK_FLAME_WAVE,
    BOOK_MAGMA_JET,
    BOOK_BLOOD_EXPLOSION,
    BOOK_ARCANE_NOVA,
    BOOK_DRAGON_CALL,
    BOOK_IGNITION,
    BOOK_SANDBLAST,
    BOOK_FORCE_QUAKE,
    BOOK_PASSWALL,
    BOOK_PETRIFY,
    BOOK_SUMMON_FOREST,
    BOOK_NOXIOUS_BOG,
    BOOK_VILE_CLUTCH,
    BOOK_PERMAFROST,
    BOOK_WARP_GRAVITY,
    BOOK_BOULDER,
    BOOK_DOROKLOHE,
    BOOK_IOOD,
    BOOK_DIG,
    BOOK_SHATTER,
    BOOK_FRIGID_HALO,
    BOOK_OZOCUBU_ARMOUR,
    BOOK_HIBERNATION,
    BOOK_CONDENSATION_SHIELD,
    BOOK_ICE_STATUE,
    BOOK_HAILSTORM,
    BOOK_ENGLACIATION,
    BOOK_FREEZING_CLOUD,
    BOOK_RIMEBLIGHT,
    BOOK_RAMPARTS,

    BOOK_WINTERS_EMBRACE,
    BOOK_POLAR_VORTEX,
    BOOK_ELECTRIC_CHARGE,

    MAX_FIXED_BOOK = BOOK_ELECTRIC_CHARGE,

    BOOK_RANDART_LEVEL,
    BOOK_RANDART_THEME,

    BOOK_MANUAL,

    BOOK_STATIC_DISCHARGE,
    BOOK_MEPHITIC_CLOUD,
    BOOK_CREEPING_ROT,
    BOOK_SWIFTNESS,
    BOOK_SILENCE,
    BOOK_DEFLECT_MISSILES,
    BOOK_ADB,
    BOOK_CBL,
    BOOK_SUPER_THUNDERBOLT,
    BOOK_MCC,
    BOOK_AGONY,
    BOOK_ANIMATE_DEAD,
    BOOK_SUBLIMATION,
    BOOK_ANGUISH,
    BOOK_KISS_OF_DEATH,
    BOOK_INFESTATION,
    BOOK_DEATH_CHANNEL,
    BOOK_VAMPIRIC_DRAINING,
    BOOK_GHOSTLY_LEGION,
    BOOK_DEATHS_DOOR,
    BOOK_REVIVIFICATION,
    BOOK_CALL_IMP,
    BOOK_LIGHTNING_SPIRE,
    BOOK_MANA_VIPER,
    BOOK_SPELLFORGED_SERVITOR,
    BOOK_FORCEFUL_DISMISSAL,
    BOOK_SUMMON_ELEMENTAL,
    BOOK_MALIGN_GATEWAY,
    BOOK_SUMMON_CACTUS,
    BOOK_ELDRITCH_ICHOR,
    BOOK_MANIFOLD_ASSAULT,
    BOOK_DIMENSIONAL_BULLSEYE,
    BOOK_LESSER_BECKONING,
    BOOK_BLINK,
    BOOK_PHASE_SHIFT,
    BOOK_PASSAGE_OF_GOLUBRIA,
    BOOK_DISPERSAL,
    BOOK_DISJUNCTION,
    BOOK_CONTROLLED_BLINK,
    BOOK_CONFUSING_TOUCH,
    BOOK_ENFEEBLE,
    BOOK_INTOXICATION,
    BOOK_DISCORD,
    BOOK_SONG_OF_SLAYING,
    BOOK_PIERCING_SHOTS,
    BOOK_SCRYING,
    BOOK_HASTE,
    BOOK_ANIMATE_ARMOUR,
    NUM_BOOKS
};

#define NUM_FIXED_BOOKS      (MAX_FIXED_BOOK + 1)
