#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "symbol.h"

typedef struct {
    SymbolTable *table;
    bool owner;
} Worker;

typedef struct {
    SymbolTable *table;
    SymbolId id;
    void *candidate,*published;
} CanonicalWorker;

static void *canonical_worker(void *context) {
    CanonicalWorker *worker=context;
    worker->published=symbol_publish_canonical_atom(
        worker->table,worker->id,worker->candidate);
    if (worker->published!=worker->candidate) free(worker->candidate);
    return NULL;
}

typedef struct {void *expected;uint32_t seen;} CanonicalVisit;
static bool canonical_visit(void *atom,void *context) {
    CanonicalVisit *visit=context;
    visit->seen++;
    return !visit->expected || atom==visit->expected;
}

enum { CANONICAL_CATALOGUE_RACE_COUNT=300 };
typedef struct {
    SymbolTable *table;
    uintptr_t **tokens;
    _Atomic bool start,zero_ready,done;
    uint32_t zero_visits,successful_visits,max_count;
} CanonicalCatalogueRace;

typedef struct {
    uintptr_t **tokens;
    uint32_t seen,limit;
} CanonicalPrefixVisit;

static bool canonical_prefix_visit(void *atom,void *context) {
    CanonicalPrefixVisit *visit=context;
    assert(visit->seen<visit->limit);
    return atom==visit->tokens[visit->seen++];
}

static void *canonical_catalogue_publisher(void *context) {
    CanonicalCatalogueRace *race=context;
    while(!atomic_load_explicit(&race->start,memory_order_acquire)) sched_yield();
    while(!atomic_load_explicit(&race->zero_ready,memory_order_acquire)) sched_yield();
    for(uint32_t i=0u;i<CANONICAL_CATALOGUE_RACE_COUNT;i++) {
        char text[48];
        snprintf(text,sizeof(text),"canonical-catalogue-%u",i);
        SymbolId id=symbol_intern_cstr(race->table,text);
        assert(id!=SYMBOL_ID_NONE);
        assert(symbol_publish_canonical_atom(race->table,id,race->tokens[i])==
            race->tokens[i]);
        sched_yield();
    }
    atomic_store_explicit(&race->done,true,memory_order_release);
    return NULL;
}

static void *canonical_catalogue_visitor(void *context) {
    CanonicalCatalogueRace *race=context;
    uint64_t instance=symbol_table_instance_id(race->table);
    while(!atomic_load_explicit(&race->start,memory_order_acquire)) sched_yield();
    do {
        uint32_t count=symbol_canonical_count(race->table);
        CanonicalPrefixVisit visit={.tokens=race->tokens,.limit=count};
        bool exact=symbol_canonical_visit(race->table,instance,count,
            count?canonical_prefix_visit:NULL,&visit);
        if(exact) {
            assert(visit.seen==count);
            race->successful_visits++;
            if(count>race->max_count) race->max_count=count;
            if(!count && race->zero_visits<1024u) {
                race->zero_visits++;
                if(race->zero_visits==1024u)
                    atomic_store_explicit(&race->zero_ready,true,memory_order_release);
            }
        }
        sched_yield();
    } while(!atomic_load_explicit(&race->done,memory_order_acquire) ||
        symbol_canonical_count(race->table)<CANONICAL_CATALOGUE_RACE_COUNT);
    return NULL;
}

static void *intern_worker(void *context) {
    Worker *worker = context;
    for (unsigned i = 0u; i < 2000u; i++) {
        SymbolId id = worker->owner
            ? symbol_intern_cstr(worker->table, "concurrent-promotion")
            : symbol_intern_data_cstr(worker->table, "concurrent-promotion");
        assert(id != SYMBOL_ID_NONE);
        assert(symbol_eq_cstr(worker->table, id, "concurrent-promotion"));
        (void)symbol_flags(worker->table, id);
    }
    return NULL;
}

int main(void) {
    SymbolTable table;
    SymbolTableStats before, after;
    symbol_table_init(&table);
    symbol_table_stats_snapshot(&table, &before);
    assert(before.owned_count == 0u && before.data_count == 0u);
    assert(before.metadata_bytes >= sizeof(SymbolEntry) * SYMBOL_ENTRY_CHUNK_SIZE);

    SymbolId data = symbol_intern_data_cstr(&table, "temporary");
    assert(data != SYMBOL_ID_NONE);
    uint64_t hash = symbol_hash_value(&table, data);
    assert(symbol_lookup_cstr(&table, "temporary") == data);
    assert(symbol_flags(&table, data) & CETTA_SYMBOL_FLAG_RUNTIME_DATA);
    assert(symbol_intern_data_cstr(&table, "temporary") == data);
    symbol_table_stats_snapshot(&table, &after);
    assert(after.data_count == 1u && after.data_bytes == strlen("temporary") + 1u);
    assert(after.data_allocated_bytes == after.data_bytes);
    assert(after.owned_count == 0u && after.promotions == 0u);

    /* Owning the existing spelling preserves both identity and hashing. */
    assert(symbol_intern_span_hashed(&table, (const uint8_t *)"temporary",
                                     strlen("temporary"), hash) == data);
    assert(!(symbol_flags(&table, data) & CETTA_SYMBOL_FLAG_RUNTIME_DATA));
    assert(symbol_intern_data_cstr(&table, "temporary") == data);
    symbol_table_stats_snapshot(&table, &after);
    assert(after.data_count == 0u && after.data_bytes == 0u);
    assert(after.owned_count == 1u && after.promotions == 1u);
    assert(after.owned_bytes == strlen("temporary") + 1u);

    SymbolId pinned = symbol_intern_data_cstr(&table, "registered");
    assert(symbol_pin(&table, pinned));
    assert(symbol_pin(&table, pinned));
    assert(!symbol_pin(&table, SYMBOL_ID_NONE));
    assert(!symbol_pin(&table, UINT32_MAX));
    assert(symbol_intern_data_cstr(&table, "registered") == pinned);
    assert(!(symbol_flags(&table, pinned) & CETTA_SYMBOL_FLAG_RUNTIME_DATA));
    assert(symbol_intern_data_cstr(&table, "") == SYMBOL_ID_NONE);
    assert(symbol_intern_data_bytes(&table, NULL, 4u) == SYMBOL_ID_NONE);
    assert(!symbol_eq_cstr(&table, SYMBOL_ID_NONE, ""));

    /* Resizing the weak lookup index preserves both lifetime classes. */
    for (unsigned i = 0u; i < 20000u; i++) {
        char text[48];
        snprintf(text, sizeof(text), "runtime-symbol-%u", i);
        SymbolId id = symbol_intern_data_cstr(&table, text);
        assert(symbol_eq_cstr(&table, id, text));
        if (i == 2999u) {
            /* First index rebuild overlaps its 4096 old slots with the
             * new array, before another entry chunk has been allocated. */
            symbol_table_stats_snapshot(&table, &after);
            assert(after.peak_metadata_bytes >= after.metadata_bytes +
                   4096u * sizeof(SymbolSlot));
        }
    }
    assert(symbol_lookup_cstr(&table, "temporary") == data);
    assert(symbol_lookup_cstr(&table, "registered") == pinned);
    symbol_table_stats_snapshot(&table, &after);
    assert(after.data_count == 20000u);
    assert(after.owned_count == 2u && after.promotions == 2u);
    assert(after.peak_payload_bytes == after.data_bytes + after.owned_bytes);
    assert(after.metadata_bytes > before.metadata_bytes);
    assert(after.peak_metadata_bytes >= after.metadata_bytes);
    assert(after.retired_count == 0u);

    symbol_intern_data_cstr(&table, "concurrent-promotion");
    pthread_t threads[4];
    Worker workers[4];
    for (unsigned i = 0u; i < 4u; i++) {
        workers[i] = (Worker){.table = &table, .owner = i == 0u};
        assert(pthread_create(&threads[i], NULL, intern_worker, &workers[i]) == 0);
    }
    for (unsigned i = 0u; i < 4u; i++)
        assert(pthread_join(threads[i], NULL) == 0);
    symbol_table_stats_snapshot(&table, &after);
    assert(after.data_count == 20000u && after.owned_count == 3u);
    assert(after.promotions == 3u);

    SymbolId canonical_id=symbol_intern_cstr(&table,"concurrent-canonical");
    CanonicalWorker canonical_workers[4];
    for (unsigned i=0u;i<4u;i++) {
        canonical_workers[i]=(CanonicalWorker){.table=&table,.id=canonical_id,
            .candidate=malloc(32u)};
        assert(canonical_workers[i].candidate);
        assert(pthread_create(&threads[i],NULL,canonical_worker,&canonical_workers[i])==0);
    }
    for (unsigned i=0u;i<4u;i++) assert(pthread_join(threads[i],NULL)==0);
    void *canonical=symbol_canonical_atom(&table,canonical_id);
    assert(canonical && symbol_canonical_count(&table)==1u);
    for (unsigned i=0u;i<4u;i++) assert(canonical_workers[i].published==canonical);
    CanonicalVisit visit={.expected=canonical};
    assert(symbol_canonical_visit(&table,symbol_table_instance_id(&table),1u,
        canonical_visit,&visit) && visit.seen==1u);
    assert(!symbol_canonical_visit(&table,symbol_table_instance_id(&table)+1u,1u,
        canonical_visit,&visit));

    /* The acquire count publishes both the first chunk and every later next
     * link. Exercise the zero-to-one edge while a reader is visiting zero,
     * then cross the 256-entry chunk boundary under the same visitor. */
    SymbolTable catalogue;symbol_table_init(&catalogue);
    uintptr_t *catalogue_tokens[CANONICAL_CATALOGUE_RACE_COUNT];
    for(uint32_t i=0u;i<CANONICAL_CATALOGUE_RACE_COUNT;i++) {
        catalogue_tokens[i]=malloc(sizeof(*catalogue_tokens[i]));
        assert(catalogue_tokens[i]);
        *catalogue_tokens[i]=i;
    }
    CanonicalCatalogueRace race={.table=&catalogue,.tokens=catalogue_tokens};
    atomic_init(&race.start,false);atomic_init(&race.zero_ready,false);
    atomic_init(&race.done,false);
    pthread_t publisher,visitor_thread;
    assert(pthread_create(&visitor_thread,NULL,canonical_catalogue_visitor,&race)==0);
    assert(pthread_create(&publisher,NULL,canonical_catalogue_publisher,&race)==0);
    atomic_store_explicit(&race.start,true,memory_order_release);
    assert(pthread_join(publisher,NULL)==0);
    assert(pthread_join(visitor_thread,NULL)==0);
    CanonicalPrefixVisit final_visit={.tokens=catalogue_tokens,
        .limit=CANONICAL_CATALOGUE_RACE_COUNT};
    assert(race.zero_visits==1024u && race.successful_visits>=1025u &&
        symbol_canonical_count(&catalogue)==CANONICAL_CATALOGUE_RACE_COUNT &&
        symbol_canonical_visit(&catalogue,symbol_table_instance_id(&catalogue),
            CANONICAL_CATALOGUE_RACE_COUNT,canonical_prefix_visit,&final_visit) &&
        final_visit.seen==CANONICAL_CATALOGUE_RACE_COUNT);
    symbol_table_free(&catalogue);

    /* Shadow marking is an identity on payloads and IDs, even for unmarked
     * values. Pinning after the snapshot removes an entry from candidates;
     * births after it cannot be mistaken for unreachable data. */
    SymbolId held = symbol_intern_data_cstr(&table, "held-for-shadow");
    SymbolId unmarked = symbol_intern_data_cstr(&table, "unmarked-for-shadow");
    SymbolShadowScan scan;
    SymbolShadowResult result;
    assert(symbol_shadow_begin(&table, &scan));
    assert(symbol_shadow_mark(&scan, held));
    assert(symbol_shadow_mark(&scan, held));
    assert(symbol_shadow_mark(&scan, SYMBOL_ID_NONE));
    assert(symbol_shadow_finish(&scan, &result));
    assert(result.marked_data_count == 1u);
    assert(result.marked_data_bytes == strlen("held-for-shadow") + 1u);
    assert(result.unmarked_data_count == 20001u);
    assert(!result.snapshot_outdated);
    assert(symbol_eq_cstr(&table, unmarked, "unmarked-for-shadow"));
    assert(symbol_intern_data_cstr(&table, "unmarked-for-shadow") == unmarked);
    assert(symbol_pin(&table, held));
    assert(symbol_shadow_finish(&scan, &result));
    assert(result.marked_data_count == 0u);
    SymbolId late = symbol_intern_data_cstr(&table, "late-for-shadow");
    assert(!symbol_shadow_mark(&scan, late));
    assert(symbol_shadow_finish(&scan, &result));
    assert(result.snapshot_outdated && result.late_births == 1u);
    symbol_shadow_destroy(&scan);
    symbol_shadow_destroy(&scan);

    BuiltinSyms builtins;
    symbol_table_init_builtins(&table, &builtins);
#define CHECK_OWNED(field, text) \
    assert(!(symbol_flags(&table, builtins.field) & CETTA_SYMBOL_FLAG_RUNTIME_DATA));
    CETTA_BUILTIN_SYMBOLS(CHECK_OWNED)
#undef CHECK_OWNED
    symbol_table_free(&table);
    symbol_table_init(&table);
    symbol_table_stats_snapshot(&table, &after);
    assert(after.owned_count == 0u && after.data_count == 0u);
    symbol_table_free(&table);
    puts("test_symbol_lifetimes: ok");
    return 0;
}
