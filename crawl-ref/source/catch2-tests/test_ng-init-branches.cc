#include "catch_amalgamated.hpp"

#include "AppHdr.h"
#include "branch.h"
#include "ng-init-branches.h"

TEST_CASE( "create_brentry_validity", "[single-file]" ) {
    FixedVector<level_id, NUM_BRANCHES> candidate_brentry;
    candidate_brentry = create_brentry();


    for (branch_iterator it; it; ++it)
    {
        bool correct = candidate_brentry[it->id].is_valid()
                        || skip_branch_brentry(it);

        CAPTURE(it->id);

        REQUIRE(correct);
    }
}
