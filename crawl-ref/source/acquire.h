/**
 * @file
 * @brief Acquirement and Trog/Oka/Sif gifts.
**/

#pragma once

#include "item-prop-enum.h"

bool acquirement_menu();
void make_acquirement_items();

item_def item_based_on_equip();

int acquirement_create_item(object_class_type class_wanted, int agent,
                            bool quiet, const coord_def &pos = coord_def());

vector<object_class_type> shuffled_acquirement_classes();
