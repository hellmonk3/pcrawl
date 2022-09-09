#pragma once

#include "tag-version.h"

enum cloud_type
{
    CLOUD_NONE,
    CLOUD_FIRE,
    CLOUD_MEPHITIC,
    CLOUD_COLD,
    CLOUD_POISON,
    CLOUD_BLACK_SMOKE,
    CLOUD_GREY_SMOKE,
    CLOUD_BLUE_SMOKE,
    CLOUD_PURPLE_SMOKE,
    CLOUD_TLOC_ENERGY,
    CLOUD_FOREST_FIRE,
    CLOUD_STEAM,
#if TAG_MAJOR_VERSION == 34
    CLOUD_GLOOM,
#endif
    CLOUD_INK,
    CLOUD_PETRIFY,
    CLOUD_HOLY,
    CLOUD_MIASMA,
    CLOUD_MIST,
    CLOUD_CHAOS,
    CLOUD_RAIN,
    CLOUD_MUTAGENIC,
    CLOUD_MAGIC_TRAIL,
    CLOUD_VORTEX,
    CLOUD_DUST,
    CLOUD_SPECTRAL,
    CLOUD_ACID,
    CLOUD_STORM,
    CLOUD_NEGATIVE_ENERGY,
    CLOUD_FLUFFY,
    CLOUD_XOM_TRAIL,
    CLOUD_SALT,
    CLOUD_GOLD_DUST,
#if TAG_MAJOR_VERSION == 34
    CLOUD_EMBERS,
#endif
    CLOUD_FLAME,
    CLOUD_DEGENERATION,
    CLOUD_BLASTSPARKS,
    NUM_CLOUD_TYPES,

    // Random per-square.
    CLOUD_RANDOM_SMOKE = 97,
    CLOUD_RANDOM,
    CLOUD_DEBUGGING,
};
