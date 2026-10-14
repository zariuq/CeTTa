#include "resource_wave.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define ID(K,O,I) ((CettaResourceIdentity){K,O,I})
static size_t checks;
#define CHECK(P) do { assert(P); checks++; } while (0)

static void small_controls(void) {
    CettaResourceSupply supply[] = {{ID(0,1,1),2u},{ID(0,1,2),1u}};
    CettaResourceClaim first[] = {{ID(0,1,1),1u,0u}};
    CettaResourceClaim read[] = {{ID(0,1,2),0u,1u}};
    CettaResourceClaim consume[] = {{ID(0,1,2),1u,0u}};
    CettaResourceFootprint work[] = {
        {first,1u,true},{first,1u,true},{first,1u,true},
        {read,1u,true},{read,1u,true},{consume,1u,true},
        {NULL,0u,false},{NULL,0u,true},
    };
    CettaResourceWave wave = {0};
    CHECK(cetta_resource_wave_select(supply,2u,work,8u,&wave));
    CHECK(wave.admitted == 5u && wave.deferred == 3u && wave.length == 8u);
    CHECK(wave.decisions[0] == CETTA_WAVE_ADMITTED);
    CHECK(wave.decisions[1] == CETTA_WAVE_ADMITTED);
    CHECK(wave.decisions[2] == CETTA_WAVE_DEFERRED_CONFLICT);
    CHECK(wave.decisions[3] == CETTA_WAVE_ADMITTED);
    CHECK(wave.decisions[4] == CETTA_WAVE_ADMITTED);
    CHECK(wave.decisions[5] == CETTA_WAVE_DEFERRED_CONFLICT);
    CHECK(wave.decisions[6] == CETTA_WAVE_DEFERRED_UNKNOWN);
    CHECK(wave.decisions[7] == CETTA_WAVE_ADMITTED);
    CHECK(wave.consumed[0] == 2u && wave.read[1] == 1u);
    cetta_resource_wave_free(&wave);
    CettaResourceClaim missing[] = {{ID(1,1,1),0u,1u}};
    CettaResourceFootprint unknown = {missing,1u,true};
    CHECK(cetta_resource_wave_select(supply,2u,&unknown,1u,&wave));
    CHECK(wave.decisions[0] == CETTA_WAVE_DEFERRED_UNKNOWN);
    cetta_resource_wave_free(&wave);
    CettaResourceSupply malformed[] = {supply[1],supply[0]};
    CHECK(!cetta_resource_wave_select(malformed,2u,work,8u,&wave));
    CettaResourceClaim duplicate[] = {first[0],first[0]};
    CettaResourceFootprint repeated = {duplicate,2u,true};
    CHECK(!cetta_resource_wave_select(supply,2u,&repeated,1u,&wave));
    CettaResourceSupply maximum = {ID(0,0,0),UINT64_MAX};
    CettaResourceClaim huge[] = {{ID(0,0,0),UINT64_MAX,0u},{ID(0,0,0),1u,0u}};
    CettaResourceFootprint overflow[] = {{huge,1u,true},{huge+1,1u,true}};
    CHECK(cetta_resource_wave_select(&maximum,1u,overflow,2u,&wave));
    CHECK(wave.decisions[0] == CETTA_WAVE_ADMITTED);
    CHECK(wave.decisions[1] == CETTA_WAVE_DEFERRED_CONFLICT);
    CHECK(wave.consumed[0] == UINT64_MAX);
    cetta_resource_wave_free(&wave);
}

/* Independent bounded reference: sum selected consumes and maximum selected
 * read at every resource using 128-bit arithmetic. It also checks maximality
 * of the result, not only equality with the greedy scan's prefix. */
static bool feasible(const CettaResourceSupply *supply,
    CettaResourceClaim claims[8][4], const bool chosen[8], size_t extra) {
    for (size_t r = 0u; r < 4u; r++) {
        __uint128_t consume = 0u;
        uint64_t read = 0u;
        for (size_t i = 0u; i < 8u; i++) if (chosen[i] || i == extra) {
            consume += claims[i][r].consume;
            if (claims[i][r].read > read) read = claims[i][r].read;
        }
        if (consume + read > supply[r].available) return false;
    }
    return true;
}

static void generated_controls(void) {
    uint32_t random = 0x81234765u;
    for (size_t trial = 0u; trial < 4096u; trial++) {
        CettaResourceSupply supply[4];
        CettaResourceClaim claims[8][4];
        CettaResourceFootprint work[8];
        for (size_t r = 0u; r < 4u; r++) {
            random = random * 1664525u + 1013904223u;
            supply[r] = (CettaResourceSupply){ID(2,11,r), random % 9u};
        }
        for (size_t i = 0u; i < 8u; i++) {
            work[i] = (CettaResourceFootprint){claims[i],4u,true};
            for (size_t r = 0u; r < 4u; r++) {
                random = random * 1664525u + 1013904223u;
                uint64_t consume = (random >> 8u) % 3u;
                random = random * 1664525u + 1013904223u;
                claims[i][r] = (CettaResourceClaim){ID(2,11,r),consume,(random >> 8u)%3u};
            }
        }
        CettaResourceWave wave = {0};
        CHECK(cetta_resource_wave_select(supply,4u,work,8u,&wave));
        bool selected[8] = {false};
        for (size_t i = 0u; i < 8u; i++) {
            bool accept = feasible(supply,claims,selected,i);
            CHECK(accept == (wave.decisions[i] == CETTA_WAVE_ADMITTED));
            selected[i] = accept;
        }
        CHECK(feasible(supply,claims,selected,8u));
        CHECK(wave.admitted + wave.deferred == 8u);
        for (size_t i = 0u; i < 8u; i++)
            if (!selected[i]) CHECK(!feasible(supply,claims,selected,i));
        cetta_resource_wave_free(&wave);
    }
}
int main(void) {
    small_controls();
    generated_controls();
    printf("Resource waves: %zu checks passed\n",checks);
    return 0;
}
