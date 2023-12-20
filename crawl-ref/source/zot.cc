/**
 * @file
 * @brief Zot clock functions.
**/

#include "AppHdr.h"

#include "zot.h"

#include "activity-interrupt-type.h" // for zot clock key
#include "branch.h" // for zot clock key
#include "database.h" // getSpeakString
#include "delay.h" // interrupt_activity
#include "god-passive.h"
#include "message.h"
#include "mon-place.h"
#include "notes.h" // NOTE_MESSAGE
#include "options.h" // fear_zot
#include "state.h"

// Returns -1 if the player hasn't been in this branch before.
static int& _zot_clock_for(branch_type br)
{
    CrawlHashTable &branch_clock = you.props["ZOT_AUTS"];
    const string branch_name = is_hell_branch(br) ? "Hells" : branches[br].abbrevname;
    // When entering a new branch, start with an empty clock.
    // (You'll get the usual time when you finish entering.)
    if (!branch_clock.exists(branch_name))
    {
            branch_clock[branch_name].get_int() = -1;
    }
    return branch_clock[branch_name].get_int();
}

static int& _zot_clock()
{
    return _zot_clock_for(you.where_are_you);
}

static bool _zot_clock_active_in(branch_type br)
{
    return br != BRANCH_ABYSS && !zot_immune() && !crawl_state.game_is_sprint();
}

// Is the zot clock running, or is it paused or stopped altogether?
bool zot_clock_active()
{
    return _zot_clock_active_in(you.where_are_you);
}

// Has the player stopped the zot clock?
bool zot_immune()
{
    return player_has_orb() || you.zigs_completed;
}

int turns_until_zot_in(branch_type br)
{
    const int aut = (MAX_ZOT_CLOCK - _zot_clock_for(br));
    if (have_passive(passive_t::slow_zot))
        return aut * 3 / (2 * BASELINE_DELAY);
    return aut / BASELINE_DELAY;
}

// How many turns (deca-auts) does the player have until Zot finds them?
int turns_until_zot()
{
    return turns_until_zot_in(you.where_are_you);
}

// A scale from 0 to 3 of how much danger the player is in of
// reaching the end of the zot clock. 0 is no danger, 3 is almost dead.
int bezotting_level_in(branch_type br)
{
    if (!_zot_clock_active_in(br))
        return 0;

    const int remaining_turns = turns_until_zot_in(br);
    if (remaining_turns < 50)
        return 3;
    if (remaining_turns < 100)
        return 2;
    if (remaining_turns < 250)
        return 1;
    return 0;
}

// A scale from 0 to 4 of how much danger the player is in of
// reaching the end of the zot clock in their current branch.
int bezotting_level()
{
    return bezotting_level_in(you.where_are_you);
}

// If the player was in the given branch, would they see warnings for
// nearing the end of the zot clock?
bool bezotted_in(branch_type br)
{
    return bezotting_level_in(br) > 0;
}

// Is the player seeing warnings about nearing the end of the zot clock?
bool bezotted()
{
    return bezotted_in(you.where_are_you);
}

bool should_fear_zot()
{
    return bezotting_level_in(you.where_are_you) == 3;
}

// Reset the zot clock when the player enters a new level.
void reset_zot_clock()
{
    if (!zot_clock_active())
        return;

    int &zot = _zot_clock();

    zot = 0;
}

static int _added_zot_time()
{
    if (have_passive(passive_t::slow_zot))
        return div_rand_round(you.time_taken * 2, 3);
    return you.time_taken;
}

void incr_zot_clock()
{
    if (!zot_clock_active())
        return;

    const int old_lvl = bezotting_level();
    _zot_clock() += _added_zot_time();
    if (!bezotted())
        return;

    if (_zot_clock() >= MAX_ZOT_CLOCK)
    {
        mpr("Time's up! Summoning administrators.");
        spawn_administrators();
        interrupt_activity(activity_interrupt::force);

        set_turns_until_zot(100);
    }

    const int lvl = bezotting_level();
    if (old_lvl >= lvl)
        return;

    if (lvl == 3)
        mprf("Time is running out! Please proceed to the next floor.");

    interrupt_activity(activity_interrupt::force);
}

void set_turns_until_zot(int turns_left)
{
    if (turns_left < 0 || turns_left > MAX_ZOT_CLOCK / BASELINE_DELAY)
        return;

    int &clock = _zot_clock();
    clock = MAX_ZOT_CLOCK - turns_left * BASELINE_DELAY;
}
