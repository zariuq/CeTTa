#define _GNU_SOURCE
#include "atom_wire.h"
#include "match.h"
#include "mm2_lower.h"
#include "native_sha256.h"
#include "native_journal.h"
#include "space.h"
#include "space_path_index.h"
#include "symbol.h"
#include "term_universe.h"
#include "mork_space_bridge_runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* Storage runs on one owner. Transport is nonblocking and bounded. There is
 * no evaluator entry point in this protocol: terms are stored and matched. */
enum { CLIENTS = 32, STORES = 32, SNAPSHOTS = 64, RECEIPTS = 256,
       RECEIPT_BYTES = 16 * 1024 * 1024, QUERY_BYTES = 8 * 1024 * 1024,
       SNAPSHOT_BYTES = 32 * 1024 * 1024, PAGE_ROWS = 1024,
       QUERY_ROWS = 65536, FACT_BYTES = 512 * 1024 };
/* Rights another account may hold on one store. Reading implies observing;
 * writing implies reading and unconstrained appends. The service owner holds
 * every right on every store, which is the trusted single-account profile. */
enum { RIGHT_OBSERVE = 1u, RIGHT_READ = 2u, RIGHT_APPEND = 4u, RIGHT_WRITE = 8u };
enum { REQUIRE_DELEGATE = 16u, REQUIRE_REVOKE = 32u };
static const char *const RIGHT_NAMES[] = {"observe","read","append","write"};

typedef struct { unsigned char *data; size_t size; } Bytes;
/* SO_PEERCRED supplies the account uid. The service supplies one globally
 * nonreused epoch; both fields together name an authority principal. */
typedef struct { uint32_t uid; uint64_t epoch; } Principal;
typedef struct { Principal current; } PrincipalBinding;
/* A grant's (grant-form SHAPE UNIQUE) keeps its exact wire bytes: one term,
 * so a variable shared by the shape and the uniqueness pattern stays one. */
typedef struct {
    uint64_t id, generation, parent_id, parent_generation;
    Principal grantee, issuer;
    unsigned rights, ticket_rights;
    bool delegable, revoked, direct_default;
    Bytes form;
} Grant;
typedef enum { TICKET_LIVE=1, TICKET_SPENT=2, TICKET_REVOKED=3 } TicketStatus;
/* A ticket is linear currency distinct from the copyable grant that permits
 * its use. Its store-local identity is never reused. */
typedef struct {
    uint64_t id, grant_id, grant_generation;
    Principal holder;
    unsigned right;
    TicketStatus status;
    char spent_id[129];
} Ticket;
typedef struct {
    uint64_t id, revision, grant_generation, ticket_generation;
    char name[129];
    SpaceEngine engine;
    bool ordered;
    Arena arena;
    TermUniverse universe;
    Space *space;
    CettaMorkSpaceHandle *backend;
    uint64_t live_bytes, churn;
    Grant *grants;
    size_t grant_count, grant_capacity;
    size_t *grant_slots;
    size_t grant_slot_capacity;
    Ticket *tickets;
    size_t ticket_count, ticket_capacity;
    bool grant_clock_restored, ticket_clock_restored;
} Store;
typedef struct {
    uint64_t id, source, revision, expires;
    uint64_t authority_grant, authority_generation;
    Bytes *rows;
    size_t count, size;
    Principal holder;
} Snapshot;
typedef struct { Bytes pattern; Snapshot rows; } ScopeView;
typedef struct {
    uint64_t id, source, revision, expires;
    uint64_t authority_grant, authority_generation;
    Principal holder;
    char creation[129];
    uint64_t creation_until;
    Bytes creation_request;
    ScopeView *views;
    size_t reads, writes, size;
} Observation;
typedef struct { Principal holder; char creation[129]; uint64_t source, until; } CancelledCreation;
typedef struct { char id[129]; Principal caller; Bytes request, reply; } Receipt;
typedef struct {
    char id[129];
    char origin[33], effect_digest[65];
    Principal caller;
    uint64_t source, grant, generation, ticket;
    unsigned required;
    bool root;
} Decision;
/* Fallible resources and derived values owned by one admitted publication.
 * The live commit only moves these prepared values after its journal record. */
typedef struct {
    Store *opened;
    Bytes grant_form;
    Atom *bundle, *origin_ref;
    char origin[33], effect_digest[65];
    PrincipalBinding principal;
    bool has_grant_form;
    bool has_principal;
} PreparedCommit;
typedef struct Client Client;
typedef struct {
    Store *stores[STORES];
    Snapshot snapshots[SNAPSHOTS];
    Observation observations[SNAPSHOTS];
    CancelledCreation cancelled[RECEIPTS];
    Receipt receipts[RECEIPTS];
    Decision *decisions;
    size_t decision_count, decision_capacity;
    size_t *decision_slots;
    size_t decision_slot_capacity;
    size_t receipt_count, receipt_bytes, snapshot_bytes, observation_bytes;
    PrincipalBinding *principals;
    size_t principal_count, principal_capacity;
    size_t *principal_slots;
    size_t principal_slot_capacity;
    uint64_t principal_generation;
    bool principal_clock_restored;
    uint64_t next_store, next_cursor, catalog_revision;
    uint64_t max_facts, max_live_bytes, max_journal_bytes, journal_bytes, checkpoint_bytes;
    uint64_t max_query_bytes, max_snapshot_bytes;
    uint64_t max_account_view_bytes;
    uint64_t max_decisions, max_tickets, max_grants, max_principals;
    unsigned query_ms;
    size_t watchers, max_watchers;
    size_t max_account_views, max_account_watchers;
    char uuid[33], boot[33];
    int journal, directory, lock;
    /* The process account wrote every record that names no account. */
    uint32_t owner_uid, process_uid;
    bool poisoned, restoring, shared_profile;
    /* Recovery is replaying the journal: recorded history is accepted under
     * the rule that admitted it, and live authority under the current one. */
    bool replaying;
    Client *clients;
    Observation *capturing;
} Service;
struct Client {
    int fd;
    unsigned char header[8];
    size_t header_read, input_read;
    Bytes input, output;
    size_t output_sent;
    uint64_t deadline;
    uint64_t watch_source, watch_revision, watch_due;
    uint64_t watch_authority_grant, watch_authority_generation;
    /* The kernel supplies principal.uid; the service pins principal.epoch. */
    Principal principal;
    char account[33];
    bool owner;
    bool watching;
    bool watch_dirty;
    Observation watched;
};

static volatile sig_atomic_t stopping;
static void stop(int signal_number) { (void)signal_number; stopping = 1; }
static uint64_t now_ms(void) {
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}
static void bytes_free(Bytes *b) { free(b->data); *b = (Bytes){0}; }
static bool bytes_copy(Bytes *out, const unsigned char *data, size_t size) {
    out->data = malloc(size ? size : 1);
    if (!out->data) return false;
    if (size) memcpy(out->data, data, size);
    out->size = size; return true;
}
static bool same_bytes(Bytes a, Bytes b) {
    return a.size == b.size && !memcmp(a.data, b.data, a.size);
}
static bool random_id(char out[33]) {
    unsigned char raw[16]; size_t pos = 0;
    while (pos < sizeof(raw)) {
        ssize_t n = getrandom(raw + pos, sizeof(raw) - pos, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        pos += (size_t)n;
    }
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 16; ++i) { out[2*i] = hex[raw[i] >> 4]; out[2*i+1] = hex[raw[i] & 15]; }
    out[32] = 0; return true;
}
static bool hex_text(const char *value,size_t length) {
    if (!value || strlen(value)!=length) return false;
    for (size_t i=0;i<length;++i)
        if (!((value[i]>='0' && value[i]<='9') || (value[i]>='a' && value[i]<='f')))
            return false;
    return true;
}
static bool text(Atom *a, const char **out) {
    if (!a || a->kind != ATOM_GROUNDED || a->ground.gkind != GV_STRING ||
        memchr(a->ground.sval, 0, atom_string_len(a))) return false;
    *out = a->ground.sval; return true;
}
static bool token(Atom *a, const char **out) {
    if (!text(a, out)) return false;
    size_t n = atom_string_len(a);
    if (!n || n > 128) return false;
    for (size_t i = 0; i < n; ++i) if ((unsigned char)(*out)[i] < 33 ||
        (unsigned char)(*out)[i] > 126) return false;
    return true;
}
static bool integer(Atom *a, uint64_t *out) {
    if (!a || a->kind != ATOM_GROUNDED || a->ground.gkind != GV_INT || a->ground.ival < 0)
        return false;
    *out = (uint64_t)a->ground.ival; return true;
}
static bool boolean_atom(Atom *a,bool *out) {
    if (atom_is_symbol(a,"true")) { *out=true; return true; }
    if (atom_is_symbol(a,"false")) { *out=false; return true; }
    return false;
}
static Atom *part(Atom *a, size_t i) {
    return a && a->kind == ATOM_EXPR && i < a->expr.len ? a->expr.elems[i] : NULL;
}
static bool shape(Atom *a, const char *head, size_t n) {
    return a && a->kind == ATOM_EXPR && a->expr.len == n && atom_is_symbol(part(a,0), head);
}
static Atom *form(Arena *a, const char *head, Atom **args, size_t count) {
    Atom **items = arena_alloc(a, (count + 1) * sizeof(*items));
    items[0] = atom_symbol(a, head);
    for (size_t i = 0; i < count; ++i) items[i+1] = args[i];
    return atom_expr(a, items, count+1);
}
static Atom *reply(Arena *a, Atom *request, bool ok, Atom *value) {
    Atom *args[] = {part(request,1), atom_symbol(a, ok ? "ok" : "error"), value};
    return form(a, "reply", args, 3);
}
static Atom *error(Arena *a, Atom *request, const char *reason) {
    return reply(a, request, false, atom_symbol(a, reason));
}
static bool wire(Atom *atom, Bytes *out) {
    return cetta_atom_wire_encode(atom, &out->data, &out->size) == ATOM_WIRE_OK;
}
static bool decode(Arena *arena, Bytes bytes, Atom **out) {
    return cetta_atom_wire_decode(arena, bytes.data, bytes.size, out) == ATOM_WIRE_OK;
}
static Store *find_store(Service *s, uint64_t id) {
    for (size_t i = 0; i < STORES; ++i) if (s->stores[i] && s->stores[i]->id == id) return s->stores[i];
    return NULL;
}
static Store *find_name(Service *s, const char *name) {
    for (size_t i = 0; i < STORES; ++i) if (s->stores[i] && !strcmp(s->stores[i]->name, name)) return s->stores[i];
    return NULL;
}
static bool principal_same(Principal left,Principal right) {
    return left.uid==right.uid && left.epoch==right.epoch;
}
static uint64_t principal_hash(uint32_t uid) {
    uint64_t hash=(uint64_t)uid+UINT64_C(0x9e3779b97f4a7c15);
    hash=(hash^(hash>>30))*UINT64_C(0xbf58476d1ce4e5b9);
    hash=(hash^(hash>>27))*UINT64_C(0x94d049bb133111eb);
    return hash^(hash>>31);
}
static bool principal_index_rehash(Service *s,size_t capacity) {
    size_t *slots=calloc(capacity,sizeof(*slots));
    if (!slots) return false;
    for (size_t i=0;i<s->principal_count;++i) {
        size_t slot=(size_t)principal_hash(s->principals[i].current.uid)&(capacity-1);
        while (slots[slot]) slot=(slot+1)&(capacity-1);
        slots[slot]=i+1;
    }
    free(s->principal_slots); s->principal_slots=slots;
    s->principal_slot_capacity=capacity; return true;
}
static void principal_index_insert(Service *s,size_t index) {
    Principal principal=s->principals[index].current;
    size_t slot=(size_t)principal_hash(principal.uid)&(s->principal_slot_capacity-1);
    while (s->principal_slots[slot]) slot=(slot+1)&(s->principal_slot_capacity-1);
    s->principal_slots[slot]=index+1;
}
static PrincipalBinding *find_principal(Service *s,uint32_t uid) {
    if (!s->principal_slot_capacity) return NULL;
    size_t slot=(size_t)principal_hash(uid)&(s->principal_slot_capacity-1);
    while (s->principal_slots[slot]) {
        PrincipalBinding *binding=&s->principals[s->principal_slots[slot]-1];
        if (binding->current.uid==uid) return binding;
        slot=(slot+1)&(s->principal_slot_capacity-1);
    }
    return NULL;
}
static PrincipalBinding *find_principal_epoch(Service *s,uint64_t epoch) {
    if (!epoch) return NULL;
    for (size_t i=0;i<s->principal_count;++i)
        if (s->principals[i].current.epoch==epoch) return &s->principals[i];
    return NULL;
}
static Principal current_principal(Service *s,uint32_t uid) {
    PrincipalBinding *binding=find_principal(s,uid);
    return binding ? binding->current : (Principal){.uid=uid};
}
static bool principal_is_current(Service *s,Principal principal) {
    Principal current=current_principal(s,principal.uid);
    return principal.epoch && principal_same(current,principal);
}
static bool principal_reserve(Service *s) {
    if (s->principal_count==s->principal_capacity) {
        size_t capacity=s->principal_capacity ? 2*s->principal_capacity : 8;
        if (capacity<s->principal_capacity || capacity>SIZE_MAX/sizeof(*s->principals))
            return false;
        PrincipalBinding *grown=realloc(s->principals,capacity*sizeof(*grown));
        if (!grown) return false;
        s->principals=grown; s->principal_capacity=capacity;
    }
    if (!s->principal_slot_capacity ||
        s->principal_count+1>s->principal_slot_capacity-(s->principal_slot_capacity/4)) {
        size_t capacity=s->principal_slot_capacity ? 2*s->principal_slot_capacity : 16;
        if (capacity<s->principal_slot_capacity ||
            capacity>SIZE_MAX/sizeof(*s->principal_slots)) return false;
        if (!principal_index_rehash(s,capacity)) return false;
    }
    return true;
}
/* Mint only during an already authorized, journal-bound operation. */
static const char *principal_prepare(Service *s,uint32_t uid,bool rotate,PreparedCommit *prepared) {
    PrincipalBinding *existing=find_principal(s,uid);
    if (existing && !rotate) {
        prepared->principal=*existing; return NULL;
    }
    if (s->principal_generation>=INT64_MAX ||
        (s->max_principals && (!existing && s->principal_count>=s->max_principals)))
        return "principal-capacity";
    if (!existing && !principal_reserve(s)) return "memory";
    prepared->principal.current=(Principal){.uid=uid,.epoch=s->principal_generation+1};
    prepared->has_principal=true; return NULL;
}
static Principal prepared_principal(Service *s,uint32_t uid,PreparedCommit *prepared) {
    if (prepared && prepared->has_principal && prepared->principal.current.uid==uid)
        return prepared->principal.current;
    return current_principal(s,uid);
}
static bool principal_install(Service *s,PreparedCommit *prepared) {
    if (!prepared || !prepared->has_principal) return true;
    Principal principal=prepared->principal.current;
    if (!principal.epoch || principal.epoch!=s->principal_generation+1) return false;
    PrincipalBinding *existing=find_principal(s,principal.uid);
    if (existing) *existing=prepared->principal;
    else {
        if (s->principal_count>=s->principal_capacity) return false;
        s->principals[s->principal_count]=prepared->principal;
        principal_index_insert(s,s->principal_count++);
    }
    s->principal_generation=principal.epoch;
    prepared->has_principal=false;
    return true;
}
/* Checkpoint rows restore exact service-issued identities.  Older journals
 * did not store an epoch; migrate each uid once to a fresh service-wide epoch
 * instead of treating the reusable uid itself as authority. */
static bool principal_restore(Service *s,Principal principal) {
    if (!principal.epoch || principal.epoch>s->principal_generation ||
        find_principal(s,principal.uid) || find_principal_epoch(s,principal.epoch) ||
        !principal_reserve(s)) return false;
    s->principals[s->principal_count]=(PrincipalBinding){.current=principal};
    principal_index_insert(s,s->principal_count++);
    return true;
}
static Principal legacy_principal(Service *s,uint32_t uid) {
    Principal current=current_principal(s,uid);
    if (current.epoch) return current;
    if (s->principal_generation>=INT64_MAX ||
        (s->max_principals && s->principal_count>=s->max_principals) ||
        !principal_reserve(s)) return (Principal){0};
    current=(Principal){.uid=uid,.epoch=++s->principal_generation};
    s->principals[s->principal_count]=(PrincipalBinding){.current=current};
    principal_index_insert(s,s->principal_count++);
    return current;
}
/* An unbound connection may pin the first incarnation minted after it
 * connected.  A connection that already holds an epoch never upgrades: a
 * rotation therefore invalidates every lingering socket and wait. */
static bool client_principal_current(Service *s,Client *client) {
    if (client->owner) {
        client->principal=(Principal){.uid=client->principal.uid};
        return true;
    }
    if (!client->principal.epoch) {
        Principal current=current_principal(s,client->principal.uid);
        if (!current.epoch) return false;
        client->principal=current;
    }
    return principal_is_current(s,client->principal);
}
static size_t store_slot(Service *s) {
    for (size_t i = 0; i < STORES; ++i) if (!s->stores[i]) return i;
    return STORES;
}
static unsigned effective_rights(unsigned rights) {
    if (rights & RIGHT_WRITE) rights |= RIGHT_READ | RIGHT_APPEND;
    if (rights & RIGHT_READ) rights |= RIGHT_OBSERVE;
    return rights;
}
/* A ticket requirement restricts a right, and restrictions are closed along
 * the implication of rights: requiring a ticket for each append while
 * holding write requires one for write, since a write batch adds rows too. */
static unsigned requirement_closure(unsigned rights,unsigned ticket_rights) {
    if ((ticket_rights&RIGHT_APPEND) && (effective_rights(rights)&RIGHT_WRITE))
        ticket_rights|=RIGHT_WRITE;
    return ticket_rights;
}
static bool same_form(Bytes left,Bytes right) {
    return left.size==right.size && (!left.size || !memcmp(left.data,right.data,left.size));
}
static uint64_t grant_hash(uint64_t id,uint64_t generation) {
    uint64_t hash=id^(generation+UINT64_C(0x9e3779b97f4a7c15)+(id<<6)+(id>>2));
    hash^=hash>>30; hash*=UINT64_C(0xbf58476d1ce4e5b9);
    hash^=hash>>27; hash*=UINT64_C(0x94d049bb133111eb);
    return hash^(hash>>31);
}
static bool grant_index_rehash(Store *st,size_t capacity) {
    size_t *slots=calloc(capacity,sizeof(*slots));
    if (!slots) return false;
    for (size_t i=0;i<st->grant_count;++i) {
        size_t slot=(size_t)grant_hash(st->grants[i].id,st->grants[i].generation)&(capacity-1);
        while (slots[slot]) slot=(slot+1)&(capacity-1);
        slots[slot]=i+1;
    }
    free(st->grant_slots); st->grant_slots=slots; st->grant_slot_capacity=capacity;
    return true;
}
static void grant_index_insert(Store *st,size_t index) {
    Grant *grant=&st->grants[index];
    size_t slot=(size_t)grant_hash(grant->id,grant->generation)&(st->grant_slot_capacity-1);
    while (st->grant_slots[slot]) slot=(slot+1)&(st->grant_slot_capacity-1);
    st->grant_slots[slot]=index+1;
}
static Grant *find_grant_ref(Store *st,uint64_t id,uint64_t generation) {
    if (!id || !generation || !st->grant_slot_capacity) return NULL;
    size_t slot=(size_t)grant_hash(id,generation)&(st->grant_slot_capacity-1);
    while (st->grant_slots[slot]) {
        Grant *grant=&st->grants[st->grant_slots[slot]-1];
        if (grant->id==id && grant->generation==generation) return grant;
        slot=(slot+1)&(st->grant_slot_capacity-1);
    }
    return NULL;
}
/* Legacy/direct operations resolve only the one live default root. Delegated
 * grants never become ambient merely because their grantee has the same uid. */
static Grant *find_grant(Store *st,Principal principal) {
    for (size_t i=0;i<st->grant_count;++i) {
        Grant *grant=&st->grants[i];
        if (grant->direct_default && !grant->revoked &&
            principal_same(grant->grantee,principal)) return grant;
    }
    return NULL;
}
typedef struct {
    unsigned rights;
    uint64_t grant, generation;
    bool delegable;
} AuthorityView;
static unsigned grant_requirements(const Grant *grant) {
    return requirement_closure(grant->rights,grant->ticket_rights);
}
/* Delegation never gives a child more than its parent can do: the child's
 * effective rights lie within the parent's, and the child keeps every
 * requirement its parent places on a right the child holds; adding
 * requirements only attenuates. The earlier rule compared requirements by
 * subset, which let a child drop one. A `historical` record admitted under
 * that rule is accepted as recorded, but live authority never follows it. */
static bool grant_edge_valid(const Grant *parent,const Grant *child,bool historical) {
    bool keeps=!(grant_requirements(parent)&effective_rights(child->rights)&
        ~grant_requirements(child));
    bool recorded=historical && !(child->ticket_rights&~parent->ticket_rights);
    return child->parent_id==parent->id &&
        child->parent_generation==parent->generation &&
        principal_same(child->issuer,parent->grantee) && parent->id<child->id &&
        parent->delegable &&
        !(effective_rights(child->rights)&~effective_rights(parent->rights)) &&
        (keeps || recorded) &&
        same_form(parent->form,child->form);
}
static bool grant_structure_valid(Store *st,Grant *grant) {
    if (!grant->id || grant->id!=grant->generation || !grant->grantee.epoch ||
        grant->generation>st->grant_generation || !grant->rights ||
        grant->rights>15 || (grant->ticket_rights&~(RIGHT_APPEND|RIGHT_WRITE)) ||
        (grant->ticket_rights&~effective_rights(grant->rights))) return false;
    if (!grant->parent_id && !grant->parent_generation)
        return !grant->issuer.uid && !grant->issuer.epoch;
    Grant *parent=find_grant_ref(st,grant->parent_id,grant->parent_generation);
    return parent && !grant->direct_default && grant_edge_valid(parent,grant,true);
}
static AuthorityView grant_authority(Service *s,Store *st,Grant *leaf,Principal principal) {
    if (!principal_is_current(s,principal) || !leaf ||
        !principal_same(leaf->grantee,principal) || leaf->revoked)
        return (AuthorityView){0};
    unsigned rights=effective_rights(leaf->rights);
    Grant *child=leaf;
    for (size_t depth=0;depth<=st->grant_count;++depth) {
        if (!child->parent_id && !child->parent_generation)
            return !child->issuer.uid && !child->issuer.epoch ?
                (AuthorityView){rights,leaf->id,leaf->generation,leaf->delegable} :
                (AuthorityView){0};
        Grant *parent=find_grant_ref(st,child->parent_id,child->parent_generation);
        if (!parent || parent->revoked || !principal_is_current(s,parent->grantee) ||
            !grant_edge_valid(parent,child,s->replaying))
            return (AuthorityView){0};
        rights&=effective_rights(parent->rights);
        child=parent;
    }
    return (AuthorityView){0};
}
static AuthorityView authority_of(Service *s,Client *c, Store *st) {
    if (c->owner) return (AuthorityView){.rights=~0u,.delegable=true};
    return grant_authority(s,st,find_grant(st,c->principal),c->principal);
}
/* Revalidate the exact authority captured by a retained observation.  Owner
 * observations use the distinguished zero stamp; restricted observations
 * always name one immutable grant record. */
static AuthorityView authority_at(Service *s,Client *c,Store *st,uint64_t grant,uint64_t generation) {
    if (c->owner)
        return !grant && !generation ?
            (AuthorityView){.rights=~0u,.delegable=true} : (AuthorityView){0};
    if (!grant || !generation) return (AuthorityView){0};
    return grant_authority(s,st,find_grant_ref(st,grant,generation),c->principal);
}
static unsigned rights_of(Service *s,Client *c, Store *st) {
    return authority_of(s,c,st).rights;
}
static bool grant_reserve(Store *st) {
    if (st->grant_count==st->grant_capacity) {
        size_t capacity=st->grant_capacity ? 2*st->grant_capacity : 4;
        if (capacity<st->grant_capacity || capacity>SIZE_MAX/sizeof(*st->grants)) return false;
        Grant *grown=realloc(st->grants,capacity*sizeof(*grown));
        if (!grown) return false;
        st->grants=grown; st->grant_capacity=capacity;
    }
    if (!st->grant_slot_capacity ||
        st->grant_count+1>st->grant_slot_capacity-(st->grant_slot_capacity/4)) {
        size_t capacity=st->grant_slot_capacity ? 2*st->grant_slot_capacity : 8;
        if (capacity<st->grant_slot_capacity || capacity>SIZE_MAX/sizeof(*st->grant_slots))
            return false;
        if (!grant_index_rehash(st,capacity)) return false;
    }
    return true;
}
static bool grants_consistent(Store *st) {
    for (size_t i=0;i<st->grant_count;++i) {
        Grant *grant=&st->grants[i];
        if (!grant_structure_valid(st,grant) ||
            find_grant_ref(st,grant->id,grant->generation)!=grant) return false;
        if (grant->direct_default && !grant->revoked &&
            find_grant(st,grant->grantee)!=grant) return false;
    }
    return true;
}
static bool grant_issue(Store *st,Principal grantee,Principal issuer,unsigned rights,
                        unsigned ticket_rights,bool delegable,bool direct_default,
                        uint64_t parent_id,uint64_t parent_generation,Bytes *form_bytes,
                        bool historical,uint64_t *issued) {
    if ((ticket_rights&~(RIGHT_APPEND|RIGHT_WRITE)) ||
        (ticket_rights&~effective_rights(rights)) || !rights ||
        st->grant_generation>=INT64_MAX || !grant_reserve(st)) {
        bytes_free(form_bytes); return false;
    }
    uint64_t identity=st->grant_generation+1;
    Grant candidate={.id=identity,.generation=identity,.parent_id=parent_id,
        .parent_generation=parent_generation,.grantee=grantee,.issuer=issuer,
        .rights=rights,.ticket_rights=ticket_rights,.delegable=delegable,
        .direct_default=direct_default,.form=*form_bytes};
    if (parent_id || parent_generation) {
        Grant *parent=find_grant_ref(st,parent_id,parent_generation);
        if (!parent || !grant_edge_valid(parent,&candidate,historical)) {
            bytes_free(form_bytes); return false;
        }
    } else if (issuer.uid || issuer.epoch) {
        bytes_free(form_bytes); return false;
    }
    st->grants[st->grant_count]=candidate;
    grant_index_insert(st,st->grant_count++);
    st->grant_generation=identity; *form_bytes=(Bytes){0};
    if (issued) *issued=identity;
    return true;
}
/* The compatibility path replaces only the ambient direct default. Historical
 * records remain for decisions and lineage evidence; no old reference revives. */
static bool grant_set(Store *st,Principal principal,unsigned rights,unsigned ticket_rights,
                      Bytes *form_bytes) {
    Grant *old=find_grant(st,principal);
    if ((ticket_rights&~(RIGHT_APPEND|RIGHT_WRITE)) ||
        (ticket_rights&~effective_rights(rights)) ||
        st->grant_generation>=INT64_MAX) { bytes_free(form_bytes); return false; }
    if (old) old->revoked=true;
    if (!rights) {
        bytes_free(form_bytes); ++st->grant_generation; return true;
    }
    return grant_issue(st,principal,(Principal){0},rights,ticket_rights,false,true,
        0,0,form_bytes,false,NULL);
}
static bool grant_revoke(Store *st,Grant *grant) {
    if (!grant || grant->revoked || st->grant_generation>=INT64_MAX) return false;
    grant->revoked=true; ++st->grant_generation; return true;
}
/* Restore an immutable historical grant below the checkpoint watermark.
 * Parent order is checked by the sealed checkpoint after all rows are read. */
static bool grant_restore(Store *st,const Grant *grant,Bytes *form_bytes) {
    if (!grant->id || grant->id!=grant->generation ||
        grant->generation>st->grant_generation ||
        !grant->rights || grant->rights>15 ||
        (grant->ticket_rights&~(RIGHT_APPEND|RIGHT_WRITE)) ||
        (grant->ticket_rights&~effective_rights(grant->rights)) ||
        find_grant_ref(st,grant->id,grant->generation)) {
        bytes_free(form_bytes); return false;
    }
    if (!grant_reserve(st)) { bytes_free(form_bytes); return false; }
    st->grants[st->grant_count]=*grant;
    st->grants[st->grant_count].form=*form_bytes;
    grant_index_insert(st,st->grant_count++);
    *form_bytes=(Bytes){0}; return true;
}
static Ticket *find_ticket(Store *st,uint64_t id) {
    if (!id) return NULL;
    for (size_t i=0;i<st->ticket_count;++i) if (st->tickets[i].id==id) return &st->tickets[i];
    return NULL;
}
static bool ticket_reserve(Store *st) {
    if (st->ticket_count<st->ticket_capacity) return true;
    size_t capacity=st->ticket_capacity ? 2*st->ticket_capacity : 4;
    if (capacity<st->ticket_capacity || capacity>SIZE_MAX/sizeof(*st->tickets)) return false;
    Ticket *grown=realloc(st->tickets,capacity*sizeof(*grown));
    if (!grown) return false;
    st->tickets=grown; st->ticket_capacity=capacity; return true;
}
static bool ticket_issue(Store *st,Principal holder,uint64_t grant_id,uint64_t grant_generation,
                         unsigned right,uint64_t *issued) {
    if (st->ticket_generation>=INT64_MAX ||
        (right!=RIGHT_APPEND && right!=RIGHT_WRITE) || !ticket_reserve(st)) return false;
    uint64_t id=st->ticket_generation+1;
    st->tickets[st->ticket_count++]=(Ticket){.id=id,.grant_id=grant_id,
        .grant_generation=grant_generation,.holder=holder,.right=right,.status=TICKET_LIVE};
    st->ticket_generation=id;
    if (issued) *issued=id;
    return true;
}
static bool ticket_restore(Store *st,const Ticket *ticket) {
    if (!ticket->id || ticket->id>st->ticket_generation || find_ticket(st,ticket->id) ||
        !ticket->grant_id || !ticket->grant_generation || !ticket->holder.epoch ||
        (ticket->right!=RIGHT_APPEND && ticket->right!=RIGHT_WRITE) ||
        (ticket->status!=TICKET_LIVE && ticket->status!=TICKET_SPENT &&
         ticket->status!=TICKET_REVOKED) ||
        (ticket->status==TICKET_SPENT) != (ticket->spent_id[0]!=0) || !ticket_reserve(st))
        return false;
    st->tickets[st->ticket_count++]=*ticket; return true;
}
static bool ticket_spend(Store *st,uint64_t id,Principal holder,uint64_t grant_id,
                         uint64_t grant_generation,unsigned right,const char *request_id) {
    Ticket *ticket=find_ticket(st,id);
    if (!ticket || ticket->status!=TICKET_LIVE || !principal_same(ticket->holder,holder) ||
        ticket->grant_id!=grant_id || ticket->grant_generation!=grant_generation ||
        ticket->right!=right) return false;
    ticket->status=TICKET_SPENT;
    snprintf(ticket->spent_id,sizeof(ticket->spent_id),"%s",request_id);
    return true;
}
static bool ticket_available(Store *st,uint64_t id,Principal holder,uint64_t grant_id,
                             uint64_t grant_generation,unsigned right) {
    Ticket *ticket=find_ticket(st,id);
    return ticket && ticket->status==TICKET_LIVE && principal_same(ticket->holder,holder) &&
        ticket->grant_id==grant_id && ticket->grant_generation==grant_generation &&
        ticket->right==right;
}
static bool ticket_revoke(Store *st,uint64_t id) {
    Ticket *ticket=find_ticket(st,id);
    if (!ticket || ticket->status!=TICKET_LIVE) return false;
    ticket->status=TICKET_REVOKED; return true;
}
static uint64_t ticket_total(Service *s) {
    uint64_t total=0;
    for (size_t i=0;i<STORES;++i) if (s->stores[i]) {
        if (UINT64_MAX-total<s->stores[i]->ticket_count) return UINT64_MAX;
        total+=s->stores[i]->ticket_count;
    }
    return total;
}
static uint64_t grant_total(Service *s) {
    uint64_t total=0;
    for (size_t i=0;i<STORES;++i) if (s->stores[i]) {
        if (UINT64_MAX-total<s->stores[i]->grant_count) return UINT64_MAX;
        total+=s->stores[i]->grant_count;
    }
    return total;
}
static bool grant_capacity_available(Service *s) {
    uint64_t total=grant_total(s);
    return total<INT64_MAX && (!s->max_grants || total<s->max_grants);
}
static bool rights_parse(Atom *list, unsigned *out) {
    if (!list || list->kind != ATOM_EXPR || !atom_is_symbol(part(list,0), "rights")) return false;
    unsigned rights = 0;
    for (size_t i = 1; i < list->expr.len; ++i) {
        size_t k = 0;
        while (k < 4 && !atom_is_symbol(part(list,i), RIGHT_NAMES[k])) ++k;
        if (k == 4) return false;
        rights |= 1u << k;
    }
    *out = rights; return true;
}
static Atom *rights_atom(Arena *a, unsigned rights) {
    Atom *names[4]; size_t count = 0;
    for (size_t k = 0; k < 4; ++k) if (rights & (1u << k)) names[count++] = atom_symbol(a, RIGHT_NAMES[k]);
    return form(a, "rights", names, count);
}
/* An account is named by uid; a login name may follow for readers. */
static bool account_uid(Atom *a, uint32_t *uid) {
    uint64_t n;
    if (!a || a->kind != ATOM_EXPR || a->expr.len < 2 || a->expr.len > 3 ||
        !atom_is_symbol(part(a,0), "account") || !integer(part(a,1), &n) || n > UINT32_MAX) return false;
    *uid = (uint32_t)n; return true;
}
static Atom *account_atom(Arena *a, Client *c) {
    Atom *args[] = {atom_int(a, c->principal.uid),
        c->account[0] ? atom_symbol(a, c->account) : NULL};
    return form(a, "account", args, c->account[0] ? 2 : 1);
}
static Atom *principal_ref_atom(Service *s,Arena *a,Principal principal) {
    Atom *args[]={atom_string(a,s->uuid),atom_int(a,principal.uid),atom_int(a,principal.epoch)};
    return form(a,"principal-ref",args,3);
}
static Atom *origin_ref_atom(Service *s,Arena *a,uint32_t uid,const char *request_id,
                             const char *origin) {
    Atom *args[]={atom_string(a,s->uuid),atom_int(a,uid),atom_string(a,request_id),
        atom_string(a,origin)};
    return form(a,"origin-ref",args,4);
}
static Atom *grant_ref_atom(Service *s,Arena *a,Store *st,Grant *grant) {
    Atom *args[]={atom_string(a,s->uuid),atom_int(a,st->id),
        atom_int(a,grant->id),atom_int(a,grant->generation)};
    return form(a,"grant-ref",args,4);
}
static Grant *grant_ref(Service *s,Store *st,Atom *reference) {
    const char *uuid=NULL; uint64_t store,id,generation;
    if (!shape(reference,"grant-ref",5) || !text(part(reference,1),&uuid) ||
        strcmp(uuid,s->uuid) || !integer(part(reference,2),&store) ||
        store!=st->id || !integer(part(reference,3),&id) ||
        !integer(part(reference,4),&generation)) return NULL;
    return find_grant_ref(st,id,generation);
}
static Atom *ticket_ref_atom(Service *s,Arena *a,Store *st,Ticket *ticket) {
    Atom *args[]={atom_string(a,s->uuid),atom_int(a,st->id),atom_int(a,ticket->id)};
    return form(a,"ticket-ref",args,3);
}
static bool ticket_ref(Service *s,Store *st,Atom *reference,uint64_t *id) {
    const char *uuid=NULL; uint64_t store;
    if (atom_is_symbol(reference,"none")) { *id=0; return true; }
    return shape(reference,"ticket-ref",4) && text(part(reference,1),&uuid) &&
        !strcmp(uuid,s->uuid) && integer(part(reference,2),&store) &&
        store==st->id && integer(part(reference,3),id) && *id;
}
/* Replace one zero-argument protocol marker without interpreting stored data. */
static Atom *with_marker(Arena *a,Atom *atom,const char *marker,Atom *replacement) {
    if (!atom || atom->kind != ATOM_EXPR) return atom;
    if (shape(atom,marker,1)) return replacement;
    Atom **items = NULL;
    for (CettaExprIndex i = 0; i < atom->expr.len; ++i) {
        Atom *next=with_marker(a,atom->expr.elems[i],marker,replacement);
        if (next != atom->expr.elems[i] && !items) {
            items = arena_alloc(a, atom->expr.len * sizeof(*items));
            for (CettaExprIndex j = 0; j < i; ++j) items[j] = atom->expr.elems[j];
        }
        if (items) items[i] = next;
    }
    return items ? atom_expr(a, items, atom->expr.len) : atom;
}
static bool has_marker(Atom *atom,const char *marker) {
    if (!atom || atom->kind!=ATOM_EXPR) return false;
    if (shape(atom,marker,1)) return true;
    for (CettaExprIndex i=0;i<atom->expr.len;++i)
        if (has_marker(atom->expr.elems[i],marker)) return true;
    return false;
}
static bool grant_form_bytes(Atom *pattern,Atom *unique,Bytes *out) {
    if (atom_is_symbol(pattern,"none") && atom_is_symbol(unique,"none")) return true;
    Arena scratch; arena_init_detached(&scratch);
    Atom *forms[]={pattern,unique};
    bool encoded=wire(form(&scratch,"grant-form",forms,2),out);
    arena_free(&scratch); return encoded;
}
static bool engine(Atom *a, SpaceEngine *out, bool *ordered) {
    *ordered = atom_is_symbol(a, "native-ordered");
    if (*ordered || atom_is_symbol(a, "native")) *out = SPACE_ENGINE_NATIVE;
    else if (atom_is_symbol(a, "pathmap")) *out = SPACE_ENGINE_PATHMAP;
    else if (atom_is_symbol(a, "mork")) *out = SPACE_ENGINE_MORK;
    else return false;
    return true;
}
static const char *engine_name(SpaceEngine e) {
    return e == SPACE_ENGINE_MORK ? "mork" : e == SPACE_ENGINE_PATHMAP ? "pathmap" : "native";
}
static const char *store_engine_name(const Store *st) {
    return st->ordered ? "native-ordered" : engine_name(st->engine);
}
static void store_free(Store *st) {
    if (!st) return;
    for (size_t i = 0; i < st->grant_count; ++i) bytes_free(&st->grants[i].form);
    free(st->grants); free(st->grant_slots); free(st->tickets);
    if (st->space) { space_free(st->space); free(st->space); }
    if (st->backend) cetta_mork_bridge_space_free(st->backend);
    term_universe_free(&st->universe); arena_free(&st->arena); free(st);
}
static void prepared_commit_free(PreparedCommit *prepared) {
    if (!prepared) return;
    store_free(prepared->opened); bytes_free(&prepared->grant_form);
    *prepared=(PreparedCommit){0};
}
static Store *store_new(uint64_t id, const char *name, SpaceEngine e, bool ordered, uint64_t revision) {
    Store *st = calloc(1, sizeof(*st));
    if (!st) return NULL;
    st->id = id; st->revision = revision; st->engine = e; st->ordered = ordered;
    snprintf(st->name, sizeof(st->name), "%s", name);
    arena_init_detached(&st->arena);
    term_universe_init(&st->universe);
    term_universe_set_persistent_arena(&st->universe, &st->arena);
    st->space = calloc(1, sizeof(*st->space));
    if (!st->space) { store_free(st); return NULL; }
    space_init_with_universe(st->space, &st->universe);
    /* Ordered native stores use the existing sequence-preserving removal
     * path. The original native bag remains unchanged for existing clients. */
    if (ordered) st->space->kind = SPACE_KIND_QUEUE;
    if (e != SPACE_ENGINE_NATIVE) {
        st->backend=e==SPACE_ENGINE_PATHMAP ? cetta_mork_bridge_space_new_pathmap()
                                           : cetta_mork_bridge_space_new();
        if (!st->backend) { store_free(st); return NULL; }
    }
    return st;
}
/* Rust storage holds lossless, ground portable keys. The native Space is the
 * maintained matching projection, never a text reconstruction of a Rust row.
 * The service exclusively owns both; every acknowledged mutation changes both.
 * This profile does not expose MM2 execution or Rust query acceleration. */
static bool backend_key(Bytes key,Bytes *encoded,bool packet) {
    Arena a; arena_init_detached(&a);
    /* Compact bridge symbols have a 63-byte limit. A canonical bounded-arity
     * rope keeps arbitrary bytes lossless without permanent symbol interning,
     * hashing collisions, hex expansion or a text parser. */
    size_t count=(key.size+62)/63;
    Atom **nodes=arena_alloc(&a,count*sizeof(*nodes));
    for (size_t i=0;i<count;++i) {
        size_t size=key.size-i*63; if (size>63) size=63;
        nodes[i]=atom_symbol_data_n(&a,(const char *)key.data+i*63,(uint32_t)size);
    }
    while (count>62) {
        size_t groups=(count+61)/62;
        Atom **next=arena_alloc(&a,groups*sizeof(*next));
        for (size_t i=0;i<groups;++i) {
            size_t size=count-i*62; if (size>62) size=62;
            next[i]=form(&a,"cav1:branch",nodes+i*62,size);
        }
        nodes=next; count=groups;
    }
    Atom *symbol=form(&a,"cav1:key",nodes,count);
    const char *reason=NULL;
    bool ok=packet
        ? cetta_mm2_atom_to_bridge_expr_packet(&a,symbol,&encoded->data,&encoded->size,&reason)
        : cetta_mm2_atom_to_bridge_expr_bytes(&a,symbol,&encoded->data,&encoded->size,&reason);
    arena_free(&a); return ok;
}
static bool backend_change(Store *st,Bytes key,bool add,uint64_t expected) {
    if (!st->backend) return true;
    Bytes encoded={0};
    if (!backend_key(key,&encoded,false)) return false;
    uint64_t changed=0;
    bool ok=add ? cetta_mork_bridge_space_add_expr_bytes(st->backend,encoded.data,encoded.size,&changed)
                : cetta_mork_bridge_space_remove_expr_bytes(st->backend,encoded.data,encoded.size,&changed);
    bytes_free(&encoded); return ok && changed==expected;
}
static bool store_consistent(Store *st) {
    uint64_t count=0;
    return !st->backend || (cetta_mork_bridge_space_size(st->backend,&count) &&
                           count==space_length64(st->space));
}
static int byte_order(const void *left,const void *right) {
    const Bytes *a=left,*b=right;
    size_t length=a->size<b->size ? a->size : b->size;
    int order=memcmp(a->data,b->data,length);
    return order ? order : a->size>b->size ? 1 : a->size<b->size ? -1 : 0;
}
/* Diagnostic comparison of complete key multisets, including multiplicities.
 * The dump's stable expr bytes are compared directly, without a text parser or
 * Rust-to-MeTTa projection. It is intentionally outside the mutation hot path. */
static bool verify_backend(Store *st) {
    if (!st->backend) return true;
    uint64_t count=space_length64(st->space),reported=0;
    Bytes packet={0};
    if (!cetta_mork_bridge_space_dump_expr_rows(st->backend,&packet.data,&packet.size,&reported))
        return false;
    Bytes *expected=calloc(count ? (size_t)count : 1,sizeof(*expected));
    Bytes *actual=calloc(count ? (size_t)count : 1,sizeof(*actual));
    bool ok=expected && actual && reported==count;
    size_t position=0;
    for (uint64_t i=0;ok && i<count;++i) {
        Bytes key={0};
        ok=wire(space_get_at64(st->space,i),&key) && backend_key(key,&expected[i],true);
        bytes_free(&key);
        if (!ok || packet.size-position<4) { ok=false; break; }
        size_t length=0;
        for (size_t j=0;j<4;++j) length=(length<<8)|packet.data[position++];
        if (length>packet.size-position) { ok=false; break; }
        actual[i]=(Bytes){packet.data+position,length}; position+=length;
    }
    if (ok) ok=position==packet.size;
    if (ok) {
        qsort(expected,(size_t)count,sizeof(*expected),byte_order);
        qsort(actual,(size_t)count,sizeof(*actual),byte_order);
        for (uint64_t i=0;ok && i<count;++i) ok=same_bytes(expected[i],actual[i]);
    }
    if (expected) for (uint64_t i=0;i<count;++i) bytes_free(&expected[i]);
    free(expected); free(actual);
    cetta_mork_bridge_bytes_free(packet.data,packet.size); return ok;
}
static bool atom_same(Atom *left, Atom *right, bool *equal) {
    Bytes a = {0}, b = {0};
    *equal=false;
    bool ok = wire(left, &a) && wire(right, &b);
    if (ok) *equal=same_bytes(a,b);
    bytes_free(&a); bytes_free(&b); return ok;
}
static bool has_nan(Atom *atom) {
    if (atom->kind == ATOM_GROUNDED)
        return atom->ground.gkind == GV_FLOAT && isnan(atom->ground.fval);
    if (atom->kind == ATOM_EXPR)
        for (CettaExprIndex i=0;i<atom->expr.len;++i)
            if (has_nan(atom->expr.elems[i])) return true;
    return false;
}
/* Alpha equality within a term. Ground membership uses the native index;
 * open terms and NaNs use canonical wire spelling, never query unification.
 * IEEE numeric equality cannot establish membership of a NaN's storage key.
 * Failed lookup is distinct from absence: it must never acknowledge a removal
 * as a successful no-op or admit a second occurrence to a set backend. */
static bool find_equal(Store *st, Atom *atom, Atom **found) {
    *found=NULL;
    if (!atom_has_vars(atom) && !has_nan(atom)) {
        /* Exact-index absence is authoritative only for indexable atoms.
         * Immutable tensor leaves use the retained-value fallback. */
        space_match_backend_clear_error();
        if (space_atom_is_exact_indexable(atom)) {
            bool present=space_contains_exact(st->space,atom);
            if (space_match_backend_last_error_code()!=SPACE_MATCH_BACKEND_ERROR_NONE) return false;
            if (!present) return true;
        }
        CettaIndex *indices = NULL;
        CettaIndex n = space_match_candidates64(st->space, atom, &indices);
        bool ok=space_match_backend_last_error_code()==SPACE_MATCH_BACKEND_ERROR_NONE && (!n || indices);
        for (CettaIndex i = 0; ok && i < n && !*found; ++i) {
            Atom *candidate = space_match_candidate_at64(st->space, indices[i]);
            bool equal=false;
            ok=candidate && atom_same(candidate,atom,&equal);
            if (ok && equal) *found=candidate;
        }
        free(indices); return ok;
    }
    CettaCount n = space_length64(st->space);
    for (CettaIndex i = 0; i < n; ++i) {
        Atom *candidate = space_get_at64(st->space, i);
        bool equal=false;
        if (!candidate || !atom_same(candidate,atom,&equal)) return false;
        if (equal) { *found=candidate; return true; }
    }
    return true;
}
static bool store_add(Store *st, Atom *atom) {
    Atom *present=NULL;
    if (st->engine==SPACE_ENGINE_MORK && !find_equal(st,atom,&present)) return false;
    Bytes b = {0};
    if (!wire(atom, &b)) return false;
    size_t size = b.size;
    /* Raw MORK reports one accepted upsert even when its set is unchanged. */
    bool ok=backend_change(st,b,true,1);
    bytes_free(&b); if (!ok) return false;
    if (present) return true;
    if (!space_admit_atom(st->space, &st->arena, atom)) return false;
    st->live_bytes += size; st->churn += size;
    return true;
}
static bool store_remove(Store *st, Atom *atom) {
    Atom *found=NULL;
    if (!find_equal(st,atom,&found)) return false;
    if (!found) return true;
    Bytes b = {0};
    if (!wire(found, &b)) return false;
    size_t size = b.size;
    bool ok=backend_change(st,b,false,1);
    bytes_free(&b); if (!ok) return false;
    AtomId identity=term_universe_lookup_atom_id(&st->universe,found);
    if (!space_remove_atom_id(st->space,identity)) return false;
    if (st->live_bytes < size) return false;
    st->live_bytes -= size; st->churn += size;
    return true;
}
/* Without the right to observe, revision and size read as zero. */
static Atom *describe_as(Service *s, Arena *a, Store *st, unsigned rights) {
    bool seen = rights & RIGHT_OBSERVE;
    Atom *args[] = {atom_string(a,s->uuid), atom_int(a,st->id), atom_string(a,st->name),
        atom_symbol(a,store_engine_name(st)), atom_symbol(a,st->engine == SPACE_ENGINE_MORK ? "set" : "bag"),
        atom_int(a,seen ? st->revision : 0), atom_int(a,seen ? space_length64(st->space) : 0)};
    return form(a,"space",args,7);
}
static Atom *describe(Service *s, Arena *a, Store *st) { return describe_as(s,a,st,~0u); }
static void snapshot_free(Service *s, Snapshot *q) {
    if (!q->id) return;
    for (size_t i = 0; i < q->count; ++i) bytes_free(&q->rows[i]);
    free(q->rows); s->snapshot_bytes -= q->size; *q = (Snapshot){0};
}
static void observation_free(Service *s, Observation *o) {
    if (!o->id) return;
    for (size_t i=0;i<o->reads+o->writes;++i) {
        ScopeView *view=&o->views[i];
        bytes_free(&view->pattern);
        for (size_t j=0;j<view->rows.count;++j) bytes_free(&view->rows.rows[j]);
        free(view->rows.rows);
    }
    free(o->views); bytes_free(&o->creation_request);
    s->observation_bytes-=o->size; *o=(Observation){0};
}
static void view_usage(Service *s,uint32_t uid,size_t *count,uint64_t *bytes,size_t *waits) {
    *count=0; *bytes=0; *waits=0;
    for (size_t i=0;i<SNAPSHOTS;++i) {
        Snapshot *q=&s->snapshots[i]; Observation *o=&s->observations[i];
        if (q->id && q->holder.uid==uid) { ++*count; *bytes+=q->size; }
        if (o->id && o->holder.uid==uid) { ++*count; *bytes+=o->size; }
    }
    if (s->capturing && s->capturing->holder.uid==uid) {
        ++*count; *bytes+=s->capturing->size;
    }
    if (s->clients) for (size_t i=0;i<CLIENTS;++i) {
        Client *c=&s->clients[i];
        if (c->watching && c->principal.uid==uid) ++*waits;
        if (c->watched.id && c->watched.holder.uid==uid) { ++*count; *bytes+=c->watched.size; }
    }
}
static const char *view_room(Service *s,uint32_t uid,size_t added,uint64_t *available) {
    size_t count,waits; uint64_t used;
    view_usage(s,uid,&count,&used,&waits);
    size_t limit=s->max_account_views ? s->max_account_views :
        s->shared_profile ? SNAPSHOTS/2 : SNAPSHOTS*2+CLIENTS;
    uint64_t budget=s->max_account_view_bytes ? s->max_account_view_bytes :
        s->shared_profile ? s->max_snapshot_bytes/2+s->max_snapshot_bytes%2 : s->max_snapshot_bytes;
    if (added>limit || count>limit-added) return "account-view-limit";
    if (used>budget) return "account-view-byte-limit";
    *available=budget-used;
    return NULL;
}
static const char *view_admit(Service *s,uint32_t uid,size_t added,uint64_t bytes) {
    uint64_t available=0;
    const char *failure=view_room(s,uid,added,&available);
    return failure ? failure : bytes>available ? "account-view-byte-limit" : NULL;
}
static bool account_watch_full(Service *s,uint32_t uid) {
    size_t count,waits; uint64_t bytes;
    view_usage(s,uid,&count,&bytes,&waits);
    size_t limit=s->max_account_watchers ? s->max_account_watchers :
        s->shared_profile ? (s->max_watchers+1)/2 : s->max_watchers;
    return waits>=limit;
}
static void expire(Service *s) {
    uint64_t now = now_ms();
    for (size_t i = 0; i < SNAPSHOTS; ++i)
        if (s->snapshots[i].id && s->snapshots[i].expires <= now) snapshot_free(s,&s->snapshots[i]);
    for (size_t i=0;i<SNAPSHOTS;++i)
        if (s->observations[i].id && s->observations[i].expires<=now)
            observation_free(s,&s->observations[i]);
    for (size_t i=0;i<RECEIPTS;++i)
        if (s->cancelled[i].until && s->cancelled[i].until<=now)
            s->cancelled[i]=(CancelledCreation){0};
}
static Snapshot *find_snapshot(Service *s, uint64_t id) {
    for (size_t i = 0; i < SNAPSHOTS; ++i) if (s->snapshots[i].id == id && id) return &s->snapshots[i];
    return NULL;
}
static Observation *find_observation(Service *s, uint64_t id) {
    for (size_t i=0;i<SNAPSHOTS;++i)
        if (id && s->observations[i].id==id) return &s->observations[i];
    return NULL;
}
static Observation *find_creation(Service *s,Principal holder,const char *id) {
    for (size_t i=0;i<SNAPSHOTS;++i) {
        Observation *o=&s->observations[i];
        if (o->id && principal_same(o->holder,holder) && !strcmp(o->creation,id)) return o;
    }
    return NULL;
}
static Observation *find_waiting_creation(Service *s,Principal holder,const char *id) {
    if (s->clients) for (size_t i=0;i<CLIENTS;++i) {
        Observation *o=&s->clients[i].watched;
        if (o->id && principal_same(o->holder,holder) && !strcmp(o->creation,id)) return o;
    }
    return NULL;
}
static bool creation_cancelled(Service *s,Principal holder,uint64_t source,const char *id,uint64_t until) {
    for (size_t i=0;i<RECEIPTS;++i) {
        CancelledCreation *c=&s->cancelled[i];
        if (c->until==until && principal_same(c->holder,holder) && c->source==source && !strcmp(c->creation,id)) return true;
    }
    return false;
}
static bool cancel_creation(Service *s,Principal holder,uint64_t source,const char *id,uint64_t until) {
    if (until<=now_ms() || creation_cancelled(s,holder,source,id,until)) return true;
    size_t slot=RECEIPTS,owned=0;
    for (size_t i=0;i<RECEIPTS;++i) {
        if (!s->cancelled[i].until && slot==RECEIPTS) slot=i;
        if (s->cancelled[i].until && s->cancelled[i].holder.uid==holder.uid) ++owned;
    }
    /* Unexpired cancellation fences are never evicted: a delayed creating
     * request must remain refused until its own deadline has passed. */
    if (slot==RECEIPTS || owned>=RECEIPTS/2) return false;
    s->cancelled[slot]=(CancelledCreation){.holder=holder,.source=source,.until=until};
    snprintf(s->cancelled[slot].creation,sizeof(s->cancelled[slot].creation),"%s",id);
    return true;
}
static void receipt_free(Receipt *r) {
    bytes_free(&r->request); bytes_free(&r->reply); *r = (Receipt){0};
}
/* Request identities belong to one account: another account's IDs neither
 * collide with nor reveal this one's. */
static Receipt *find_receipt(Service *s, Principal caller, const char *id) {
    for (size_t i = 0; i < s->receipt_count; ++i)
        if (principal_same(s->receipts[i].caller,caller) &&
            !strcmp(s->receipts[i].id,id)) return &s->receipts[i];
    return NULL;
}
static uint64_t decision_hash(Principal caller,const char *id) {
    uint64_t hash=symbol_hash_span((const uint8_t *)id,(uint32_t)strlen(id));
    hash^=(uint64_t)caller.uid*UINT64_C(0x9e3779b97f4a7c15);
    hash^=caller.epoch*UINT64_C(0xd6e8feb86659fd93);
    hash^=hash>>30; hash*=UINT64_C(0xbf58476d1ce4e5b9);
    hash^=hash>>27; hash*=UINT64_C(0x94d049bb133111eb);
    return hash^(hash>>31);
}
static Decision *find_decision(Service *s,Principal caller,const char *id) {
    if (!s->decision_slot_capacity) return NULL;
    size_t slot=(size_t)decision_hash(caller,id)&(s->decision_slot_capacity-1);
    while (s->decision_slots[slot]) {
        Decision *decision=&s->decisions[s->decision_slots[slot]-1];
        if (principal_same(decision->caller,caller) && !strcmp(decision->id,id)) return decision;
        slot=(slot+1)&(s->decision_slot_capacity-1);
    }
    return NULL;
}
static Decision *origin_reference(Service *s,Client *client,Atom *reference) {
    const char *uuid=NULL,*id=NULL,*origin=NULL; uint64_t uid;
    if (!shape(reference,"origin-ref",5) || !text(part(reference,1),&uuid) ||
        strcmp(uuid,s->uuid) || !integer(part(reference,2),&uid) || uid>UINT32_MAX ||
        !token(part(reference,3),&id) || !text(part(reference,4),&origin) ||
        !hex_text(origin,32)) return NULL;
    if (!client->owner) {
        if (!client->principal.epoch || client->principal.uid!=(uint32_t)uid) return NULL;
        Decision *decision=find_decision(s,client->principal,id);
        return decision && !strcmp(decision->origin,origin) ? decision : NULL;
    }
    for (size_t i=0;i<s->decision_count;++i) {
        Decision *decision=&s->decisions[i];
        if (decision->caller.uid==(uint32_t)uid && !strcmp(decision->id,id) &&
            !strcmp(decision->origin,origin)) return decision;
    }
    return NULL;
}
static bool decision_index_rehash(Service *s,size_t capacity) {
    size_t *slots=calloc(capacity,sizeof(*slots));
    if (!slots) return false;
    for (size_t i=0;i<s->decision_count;++i) {
        size_t slot=(size_t)decision_hash(s->decisions[i].caller,
            s->decisions[i].id)&(capacity-1);
        while (slots[slot]) slot=(slot+1)&(capacity-1);
        slots[slot]=i+1;
    }
    free(s->decision_slots); s->decision_slots=slots; s->decision_slot_capacity=capacity;
    return true;
}
static bool decision_reserve(Service *s) {
    if (s->decision_count==s->decision_capacity) {
        size_t capacity=s->decision_capacity ? 2*s->decision_capacity : 256;
        if (capacity<s->decision_capacity || capacity>SIZE_MAX/sizeof(*s->decisions)) return false;
        Decision *grown=realloc(s->decisions,capacity*sizeof(*grown));
        if (!grown) return false;
        s->decisions=grown; s->decision_capacity=capacity;
    }
    if (!s->decision_slot_capacity ||
        s->decision_count+1>s->decision_slot_capacity-(s->decision_slot_capacity/4)) {
        size_t capacity=s->decision_slot_capacity ? 2*s->decision_slot_capacity : 512;
        if (capacity<s->decision_slot_capacity || capacity>SIZE_MAX/sizeof(*s->decision_slots))
            return false;
        if (!decision_index_rehash(s,capacity)) return false;
    }
    return true;
}
static bool is_append_request(Atom *request) {
    return (atom_is_symbol(part(request,2),"append") && request->expr.len==6) ||
        (atom_is_symbol(part(request,2),"append-ticket") && request->expr.len==7) ||
        (atom_is_symbol(part(request,2),"append-authorized") && request->expr.len==8);
}
static bool is_batch_request(Atom *request) {
    return (atom_is_symbol(part(request,2),"batch") && request->expr.len==7) ||
        (atom_is_symbol(part(request,2),"batch-ticket") && request->expr.len==8) ||
        (atom_is_symbol(part(request,2),"batch-authorized") && request->expr.len==9);
}
static bool is_authorized_request(Atom *request) {
    return (atom_is_symbol(part(request,2),"append-authorized") && request->expr.len==8) ||
        (atom_is_symbol(part(request,2),"batch-authorized") && request->expr.len==9);
}
static bool is_ticketed_request(Atom *request) {
    return (atom_is_symbol(part(request,2),"append-ticket") && request->expr.len==7) ||
        (atom_is_symbol(part(request,2),"batch-ticket") && request->expr.len==8);
}
/* Read-like operations optionally carry one final `(authority GRANT-REF)`.
 * Their ordinary arguments keep the same positions, so legacy/direct and
 * explicit attenuating views enter one implementation. */
static size_t observation_request_base_length(Atom *request) {
    Atom *operation=part(request,2);
    if (atom_is_symbol(operation,"describe") || atom_is_symbol(operation,"verify")) return 5;
    if (atom_is_symbol(operation,"count")) return 6;
    if (atom_is_symbol(operation,"watch")) return 7;
    if (atom_is_symbol(operation,"query") || atom_is_symbol(operation,"query-first") ||
        atom_is_symbol(operation,"query-profile") ||
        atom_is_symbol(operation,"query-first-profile")) return 9;
    return 0;
}
static bool observation_request_shape(Atom *request) {
    size_t base=observation_request_base_length(request);
    return base && (request->expr.len==base ||
        (request->expr.len==base+1 && shape(part(request,base),"authority",2)));
}
static Atom *observation_authority_reference(Atom *request) {
    size_t base=observation_request_base_length(request);
    return base && request->expr.len==base+1 && shape(part(request,base),"authority",2) ?
        part(part(request,base),1) : NULL;
}
static Atom *request_bundle(Atom *request) {
    if (atom_is_symbol(part(request,2),"append-authorized")) return part(request,7);
    return part(request,atom_is_symbol(part(request,2),"append-ticket") ? 6 : 5);
}
static Atom *request_ops(Atom *request) {
    if (atom_is_symbol(part(request,2),"batch-authorized")) return part(request,8);
    return part(request,atom_is_symbol(part(request,2),"batch-ticket") ? 7 : 6);
}
static bool request_ticket(Atom *request,uint64_t *ticket) {
    if (!is_ticketed_request(request)) { *ticket=0; return true; }
    return integer(part(request,atom_is_symbol(part(request,2),"append-ticket") ? 5 : 6),ticket) && *ticket;
}
static unsigned decision_required(Atom *request) {
    if (is_append_request(request)) return RIGHT_APPEND;
    if (is_batch_request(request)) return RIGHT_WRITE;
    if (atom_is_symbol(part(request,2),"delegate-grant") && request->expr.len==10)
        return REQUIRE_DELEGATE;
    if (atom_is_symbol(part(request,2),"revoke-grant") && request->expr.len==6)
        return REQUIRE_REVOKE;
    return 0;
}
static Grant *request_authority_grant(Service *s,Client *client,Store *st,Atom *request) {
    Atom *observation=observation_authority_reference(request);
    if (observation) return grant_ref(s,st,observation);
    if (is_authorized_request(request))
        return grant_ref(s,st,part(request,
            atom_is_symbol(part(request,2),"append-authorized") ? 5 : 6));
    if (atom_is_symbol(part(request,2),"delegate-grant") && request->expr.len==10)
        return grant_ref(s,st,part(request,5));
    if (atom_is_symbol(part(request,2),"revoke-grant") && request->expr.len==6) {
        Grant *target=grant_ref(s,st,part(request,5));
        if (!target || !principal_same(target->issuer,client->principal)) return NULL;
        return find_grant_ref(st,target->parent_id,target->parent_generation);
    }
    return find_grant(st,client->principal);
}
/* Describe the authority consulted by one already admitted durable request.
 * Allocation and policy capacity are handled separately before publication. */
static const char *decision_describe(Service *s,Client *client,Atom *request,Decision *out) {
    const char *id=NULL;
    if (!token(part(request,1),&id)) return "invalid-id";
    *out=(Decision){.caller=client->principal,.root=client->owner};
    snprintf(out->id,sizeof(out->id),"%s",id);
    if (client->owner)
        return (is_ticketed_request(request) || is_authorized_request(request)) ?
            "ticket-unavailable" : NULL;
    uint64_t source; unsigned required=decision_required(request);
    if (!required || !integer(part(request,4),&source)) return "forbidden";
    Store *st=find_store(s,source);
    if (!st) return "forbidden";
    Grant *selected=request_authority_grant(s,client,st,request);
    AuthorityView current=grant_authority(s,st,selected,client->principal);
    bool permitted=required==REQUIRE_DELEGATE ? current.delegable :
        required==REQUIRE_REVOKE ? current.grant!=0 :
        (current.rights&required)==required;
    if (!current.generation || !permitted) return "forbidden";
    out->source=source; out->grant=current.grant;
    out->generation=current.generation; out->required=required;
    if (is_authorized_request(request)) {
        if (!ticket_ref(s,st,part(request,
                atom_is_symbol(part(request,2),"append-authorized") ? 6 : 7),
                &out->ticket)) return "invalid-ticket";
    } else if (!request_ticket(request,&out->ticket)) return "invalid-ticket";
    if (out->ticket && !ticket_available(st,out->ticket,client->principal,current.grant,
            current.generation,required))
        return "ticket-unavailable";
    return NULL;
}
static const char *decision_prepare(Service *s,Client *client,Atom *request,Decision *out) {
    const char *reason=decision_describe(s,client,request,out);
    if (reason) return reason;
    if (s->decision_count>=INT64_MAX ||
        (s->max_decisions && s->decision_count>=s->max_decisions)) return "decision-capacity";
    return decision_reserve(s) ? NULL : "memory";
}
static void remember_decision(Service *s,const Decision *decision) {
    size_t index=s->decision_count;
    s->decisions[index]=*decision;
    size_t slot=(size_t)decision_hash(decision->caller,decision->id)&
        (s->decision_slot_capacity-1);
    while (s->decision_slots[slot]) slot=(slot+1)&(s->decision_slot_capacity-1);
    s->decision_slots[slot]=index+1; ++s->decision_count;
}
static bool decision_current(Service *s,Client *client,const Decision *decision) {
    if (!decision || !principal_same(decision->caller,client->principal)) return false;
    if (decision->root) return client->owner;
    Store *st=find_store(s,decision->source);
    Grant *selected=st ? find_grant_ref(st,decision->grant,decision->generation) : NULL;
    AuthorityView current=st ? grant_authority(s,st,selected,client->principal) :
        (AuthorityView){0};
    bool permitted=decision->required==REQUIRE_DELEGATE ? current.delegable :
        decision->required==REQUIRE_REVOKE ? current.grant!=0 :
        (current.rights&decision->required)==decision->required;
    if (!st || current.grant!=decision->grant ||
        current.generation!=decision->generation || !permitted) return false;
    if (!decision->ticket) return true;
    Ticket *ticket=find_ticket(st,decision->ticket);
    return ticket && ticket->status==TICKET_SPENT &&
        principal_same(ticket->holder,decision->caller) &&
        ticket->grant_id==decision->grant &&
        ticket->grant_generation==decision->generation &&
        ticket->right==decision->required &&
        !strcmp(ticket->spent_id,decision->id);
}
static bool decision_same(const Decision *left,const Decision *right) {
    return principal_same(left->caller,right->caller) && left->source==right->source &&
        left->grant==right->grant && left->generation==right->generation &&
        left->ticket==right->ticket &&
        left->required==right->required &&
        left->root==right->root && !strcmp(left->id,right->id);
}
static bool decision_spend_ticket(Service *s,const Decision *decision) {
    if (!decision->ticket) return true;
    Store *st=find_store(s,decision->source);
    return st && ticket_spend(st,decision->ticket,decision->caller,decision->grant,
        decision->generation,decision->required,decision->id);
}
static bool decision_restore(Service *s,const Decision *decision) {
    if (find_decision(s,decision->caller,decision->id) || !decision_reserve(s)) return false;
    remember_decision(s,decision); return true;
}
/* Prepare owned messages before publication. Installation only moves ownership
 * and evicts old receipts; it cannot fail after the durable decision. Recovery
 * already owns its decoded messages and uses the same preparation boundary. */
static bool receipt_prepare(Receipt *r, Principal caller, const char *id,
                            Bytes *request, Bytes *answer) {
    if (request->size > RECEIPT_BYTES || answer->size > RECEIPT_BYTES-request->size)
        return false;
    r->caller=caller;
    snprintf(r->id,sizeof(r->id),"%s",id);
    r->request=*request; r->reply=*answer;
    *request=(Bytes){0}; *answer=(Bytes){0};
    return true;
}
static void remember(Service *s, Receipt *r) {
    size_t size=r->request.size+r->reply.size;
    while (s->receipt_count && (s->receipt_count == RECEIPTS ||
            s->receipt_bytes + size > RECEIPT_BYTES)) {
        s->receipt_bytes -= s->receipts[0].request.size + s->receipts[0].reply.size;
        receipt_free(&s->receipts[0]);
        memmove(s->receipts,s->receipts+1,(--s->receipt_count)*sizeof(*s->receipts));
    }
    s->receipts[s->receipt_count++]=*r;
    s->receipt_bytes+=size; *r=(Receipt){0};
}
/* Complete records have a bounded length and SHA-256. A short last record is
 * an unsynchronized tail. A complete invalid record fails recovery closed. */
static bool journal_bytes(int fd, Bytes b, bool synchronize, uint64_t *written) {
    return cetta_journal_append(fd,b.data,b.size,synchronize,written)==CETTA_JOURNAL_OK;
}
static bool journal_record(int fd, Atom *record, bool synchronize, uint64_t *written) {
    Bytes b={0};
    bool ok=wire(record,&b) && journal_bytes(fd,b,synchronize,written);
    bytes_free(&b); return ok;
}
/* Live open moves the backend prepared at admission. Replay creates it from
 * the journal. Other mutations still fail closed if application cannot finish. */
static bool apply(Service *s, Client *client, Atom *request, PreparedCommit *prepared) {
    if (atom_is_symbol(part(request,2),"rotate-principal") && request->expr.len==5) {
        uint32_t uid;
        Principal expected=prepared && prepared->has_principal ?
            prepared->principal.current : (Principal){0};
        return account_uid(part(request,4),&uid) && expected.uid==uid &&
            expected.epoch && principal_install(s,prepared);
    }
    if (atom_is_symbol(part(request,2),"open")) {
        const char *name = NULL; SpaceEngine e; bool ordered; uint64_t rev;
        if (!token(part(request,4),&name) || !engine(part(request,5),&e,&ordered) ||
            !integer(part(request,6),&rev) || rev != s->catalog_revision ||
            s->next_store >= INT64_MAX || find_name(s,name)) return false;
        size_t slot = store_slot(s);
        if (slot == STORES) return false;
        Store *st = prepared && prepared->opened ? prepared->opened
                                                  : store_new(s->next_store,name,e,ordered,0);
        if (!st) return false;
        if (prepared && prepared->opened && (st->id!=s->next_store || strcmp(st->name,name) ||
            st->engine!=e || st->ordered!=ordered || st->revision)) return false;
        s->stores[slot] = st; ++s->next_store; ++s->catalog_revision;
        if (prepared) prepared->opened=NULL;
        return true;
    }
    if (is_append_request(request)) {
        uint64_t id; Atom *bundle = prepared && prepared->bundle ? prepared->bundle
                                                                 : request_bundle(request);
        if (!integer(part(request,4),&id) || !bundle || bundle->kind != ATOM_EXPR) return false;
        Store *st = find_store(s,id);
        if (!st || st->revision >= INT64_MAX) return false;
        for (size_t i = 1; i < bundle->expr.len; ++i) if (!store_add(st,part(bundle,i))) return false;
        ++st->revision; return store_consistent(st);
    }
    if (atom_is_symbol(part(request,2),"issue-ticket") && request->expr.len==7) {
        uint64_t id; uint32_t uid; unsigned right;
        if (!integer(part(request,4),&id) || !account_uid(part(request,5),&uid) ||
            !rights_parse(part(request,6),&right) || (right!=RIGHT_APPEND && right!=RIGHT_WRITE))
            return false;
        Principal holder=current_principal(s,uid);
        Store *st=find_store(s,id); Grant *grant=st ? find_grant(st,holder) : NULL;
        if (!grant || !(effective_rights(grant->rights)&right) || !(grant_requirements(grant)&right))
            return false;
        return ticket_issue(st,holder,grant->id,grant->generation,right,NULL);
    }
    if (atom_is_symbol(part(request,2),"issue-ticket-v2") && request->expr.len==7) {
        uint64_t id; unsigned right;
        if (!integer(part(request,4),&id) || !rights_parse(part(request,6),&right) ||
            (right!=RIGHT_APPEND && right!=RIGHT_WRITE)) return false;
        Store *st=find_store(s,id);
        Grant *grant=st ? grant_ref(s,st,part(request,5)) : NULL;
        if (!grant || grant->revoked || !(effective_rights(grant->rights)&right) ||
            !(grant_requirements(grant)&right)) return false;
        return ticket_issue(st,grant->grantee,grant->id,grant->generation,right,NULL);
    }
    if (atom_is_symbol(part(request,2),"revoke-ticket") && request->expr.len==6) {
        uint64_t id,ticket;
        return integer(part(request,4),&id) && integer(part(request,5),&ticket) &&
            find_store(s,id) && ticket_revoke(find_store(s,id),ticket);
    }
    bool ticketedGrant=atom_is_symbol(part(request,2),"grant-ticketed") && request->expr.len==10;
    if ((atom_is_symbol(part(request,2),"grant") && request->expr.len==9) || ticketedGrant) {
        uint64_t id; uint32_t uid; unsigned rights,ticketRights=0;
        if (!integer(part(request,4),&id) || !account_uid(part(request,5),&uid) ||
            !rights_parse(part(request,6),&rights) ||
            (ticketedGrant && !rights_parse(part(request,7),&ticketRights))) return false;
        Store *st = find_store(s,id);
        if (!st) return false;
        Principal target=prepared_principal(s,uid,prepared);
        if (rights && (!target.epoch || !principal_install(s,prepared))) return false;
        Bytes local_form={0};
        Bytes *form_bytes=prepared && prepared->has_grant_form ? &prepared->grant_form : &local_form;
        if ((!prepared || !prepared->has_grant_form) && !grant_form_bytes(part(request,ticketedGrant ? 8 : 7),
                part(request,ticketedGrant ? 9 : 8),form_bytes)) return false;
        return grant_set(st,target,rights,ticketRights,form_bytes);
    }
    if (atom_is_symbol(part(request,2),"grant-root") && request->expr.len==11) {
        uint64_t id; uint32_t uid; unsigned rights,ticketRights; bool delegable;
        if (!integer(part(request,4),&id) || !account_uid(part(request,5),&uid) ||
            !rights_parse(part(request,6),&rights) ||
            !rights_parse(part(request,7),&ticketRights) ||
            !boolean_atom(part(request,8),&delegable)) return false;
        Store *st=find_store(s,id);
        Principal target=prepared_principal(s,uid,prepared);
        if (!st || !target.epoch) return false;
        Bytes local_form={0};
        Bytes *form_bytes=prepared && prepared->has_grant_form ? &prepared->grant_form : &local_form;
        if ((!prepared || !prepared->has_grant_form) &&
            !grant_form_bytes(part(request,9),part(request,10),form_bytes)) return false;
        if (!principal_install(s,prepared)) { bytes_free(&local_form); return false; }
        return grant_issue(st,target,(Principal){0},rights,ticketRights,delegable,false,0,0,
            form_bytes,false,NULL);
    }
    if (atom_is_symbol(part(request,2),"delegate-grant") && request->expr.len==10) {
        uint64_t id; uint32_t uid; unsigned rights,ticketRights; bool delegable;
        if (!integer(part(request,4),&id) || !account_uid(part(request,6),&uid) ||
            !rights_parse(part(request,7),&rights) ||
            !rights_parse(part(request,8),&ticketRights) ||
            !boolean_atom(part(request,9),&delegable)) return false;
        Store *st=find_store(s,id);
        Grant *parent=st ? grant_ref(s,st,part(request,5)) : NULL;
        Principal target=prepared_principal(s,uid,prepared);
        if (!parent || !target.epoch) return false;
        Bytes local_form={0};
        Bytes *inherited=prepared && prepared->has_grant_form ? &prepared->grant_form : &local_form;
        if ((!prepared || !prepared->has_grant_form) && parent->form.size &&
            !bytes_copy(inherited,parent->form.data,parent->form.size)) return false;
        if (!principal_install(s,prepared)) { bytes_free(&local_form); return false; }
        return grant_issue(st,target,client->principal,rights,ticketRights,delegable,false,
            parent->id,parent->generation,inherited,s->replaying,NULL);
    }
    if (atom_is_symbol(part(request,2),"revoke-grant") && request->expr.len==6) {
        uint64_t id;
        if (!integer(part(request,4),&id)) return false;
        Store *st=find_store(s,id);
        return st && grant_revoke(st,grant_ref(s,st,part(request,5)));
    }
    uint64_t id, rev;
    if (!integer(part(request,4),&id) || !integer(part(request,5),&rev)) return false;
    Store *st = find_store(s,id);
    if (!st || st->revision != rev || rev >= INT64_MAX) return false;
    if (atom_is_symbol(part(request,2),"drop")) {
        uint64_t catalog;
        if (!integer(part(request,6),&catalog) || catalog != s->catalog_revision) return false;
        for (size_t i = 0; i < STORES; ++i) if (s->stores[i] == st) {
            store_free(st); s->stores[i] = NULL; ++s->catalog_revision; return true;
        }
        return false;
    }
    if (!is_batch_request(request)) return false;
    Atom *ops = request_ops(request);
    if (!ops || ops->kind != ATOM_EXPR || !atom_is_symbol(part(ops,0),"ops")) return false;
    for (size_t i = 1; i < ops->expr.len; ++i) {
        Atom *op = part(ops,i);
        if (shape(op,"add",2)) { if (!store_add(st,part(op,1))) return false; }
        else if (shape(op,"remove",2)) { if (!store_remove(st,part(op,1))) return false; }
        else return false;
    }
    ++st->revision; return store_consistent(st);
}
static Atom *committed(Service *s, Arena *a, Atom *request, uint64_t id, uint64_t rev) {
    Atom *args[] = {atom_string(a,s->uuid),atom_int(a,id),atom_int(a,rev)};
    return reply(a,request,true,form(a,"committed",args,3));
}
static void affected_scope_watches(Service *s, Atom *application, Atom *bundle,
                                   bool affected[CLIENTS]);
static Atom *commit_resolved(Service *s, Arena *a, Client *client, Atom *request,
                             Atom *application, Bytes raw, Atom *answer,
                             PreparedCommit *prepared) {
    const char *id = NULL;
    Bytes input={0}, result={0}, record_bytes={0}; Receipt receipt={0};
    Decision decision={0};
    const char *reason="invalid-id";
    if (!token(part(request,1),&id)) goto refused;
    reason=decision_prepare(s,client,application,&decision);
    if (reason) goto refused;
    if (prepared && prepared->origin[0]) {
        snprintf(decision.origin,sizeof(decision.origin),"%s",prepared->origin);
        snprintf(decision.effect_digest,sizeof(decision.effect_digest),"%s",
            prepared->effect_digest);
    }
    reason="limit";
    if (!wire(answer,&result)) goto refused;
    reason="allocation";
    if (!bytes_copy(&input,raw.data,raw.size)) goto refused;
    reason="receipt-limit";
    if (!receipt_prepare(&receipt,client->principal,id,&input,&result)) goto refused;
    /* Persist the exact input bytes: number parsing can normalize spelling,
     * and decoding/re-encoding cannot reconstruct an idempotence key.  The
     * same frame retains the exact authority stamp used for this decision. */
    Principal pending=prepared && prepared->has_principal ? prepared->principal.current :
        (Principal){0};
    Atom *parts[] = {atom_int(a,decision.caller.uid),atom_int(a,decision.caller.epoch),
        atom_int(a,decision.root ? 1 : 0),
        atom_int(a,decision.source),atom_int(a,decision.grant),
        atom_int(a,decision.generation),atom_int(a,decision.required),atom_int(a,decision.ticket),
        decision.origin[0] ? atom_string(a,decision.origin) : atom_symbol(a,"none"),
        decision.origin[0] ? atom_string(a,decision.effect_digest) : atom_symbol(a,"none"),
        atom_int(a,pending.uid),atom_int(a,pending.epoch),
        atom_string_n(a,(const char *)receipt.request.data,receipt.request.size),
        atom_string_n(a,(const char *)receipt.reply.data,receipt.reply.size)};
    Atom *record = form(a,"event-authorized-v5",parts,14);
    if (application != request) {
        /* The original request contains the exact operations. Only its
         * resolved revision fence must be added: recovery must not depend
         * on a leased observation from the old process. No payload copy. */
        Atom *resolved[15];
        memcpy(resolved,parts,sizeof(parts)); resolved[14]=part(application,5);
        record=form(a,"event-authorized-observed-v1",resolved,15);
    }
    reason="limit";
    if (!wire(record,&record_bytes)) goto refused;
    /* Interest classification is preparation. Publication only flips bits;
     * notifying a watcher never adds an allocation after the durable write. */
    bool affected[CLIENTS]={0};
    affected_scope_watches(s,application,prepared ? prepared->bundle : NULL,affected);
    bool recorded=journal_bytes(s->journal,record_bytes,true,&s->journal_bytes);
    bytes_free(&record_bytes);
    if (!recorded || !apply(s,client,application,prepared) ||
        (prepared && prepared->has_principal) ||
        !decision_spend_ticket(s,&decision)) {
        s->poisoned=true;
        prepared_commit_free(prepared); receipt_free(&receipt);
        return NULL;
    }
    prepared_commit_free(prepared);
    if (s->clients) for (size_t i=0;i<CLIENTS;++i)
        if (affected[i]) s->clients[i].watch_dirty=true;
    remember_decision(s,&decision); remember(s,&receipt);
    return answer;
refused:
    bytes_free(&input); bytes_free(&result); bytes_free(&record_bytes);
    prepared_commit_free(prepared); receipt_free(&receipt);
    return error(a,request,reason);
}
static Atom *commit(Service *s, Arena *a, Client *client, Atom *request, Bytes raw,
                    Atom *answer, PreparedCommit *prepared) {
    return commit_resolved(s,a,client,request,request,raw,answer,prepared);
}
static bool service_matches(Service *s, Atom *a) {
    const char *uuid = NULL; return text(a,&uuid) && !strcmp(uuid,s->uuid);
}
static bool boot_matches(Service *s, Atom *a) {
    const char *boot = NULL; return text(a,&boot) && !strcmp(boot,s->boot);
}
static Atom *query(Service *s, Arena *a, Atom *request, Store *st, bool counting, Principal holder,
                   uint64_t authority_grant, uint64_t authority_generation,
                   bool original_records, uint64_t deadline) {
    bool profile=!counting && (atom_is_symbol(part(request,2),"query-profile") ||
                              atom_is_symbol(part(request,2),"query-first-profile"));
    bool first=!counting && (atom_is_symbol(part(request,2),"query-first") ||
                            atom_is_symbol(part(request,2),"query-first-profile"));
    uint64_t limit = 0, lease = 0;
    if (!counting && (!integer(part(request,7),&limit) || !integer(part(request,8),&lease) ||
        !limit || limit > QUERY_ROWS || lease < 1 || lease > 300000)) return error(a,request,"limit");
    expire(s);
    if (now_ms()>=deadline) return error(a,request,"query-time-limit");
    size_t slot = SNAPSHOTS;
    if (!counting) {
        for (size_t i = 0; i < SNAPSHOTS; ++i) if (!s->snapshots[i].id) { slot = i; break; }
        if (slot == SNAPSHOTS || s->next_cursor >= INT64_MAX) return error(a,request,"snapshot-limit");
    }
    Atom *pattern = part(request,5), *projection = counting ? pattern : part(request,6);
    SpaceOccurrenceCursor cursor;
    space_occurrence_cursor_init_empty(&cursor);
    if (!store_consistent(st) || !space_occurrence_cursor_init(st->space,pattern,&cursor))
        return error(a,request,"backend");
    Snapshot q = {.id=s->next_cursor,.source=st->id,.revision=st->revision,.holder=holder,
                  .authority_grant=authority_grant,
                  .authority_generation=authority_generation};
    uint32_t uid=holder.uid;
    uint64_t account_room=0,total_room=s->max_snapshot_bytes-s->snapshot_bytes-s->observation_bytes;
    size_t capacity=0;
    if (!counting) {
        /* The single owner cannot admit another query during this scan.
         * Capture its remaining allowance once, not once per returned row. */
        const char *refused=view_room(s,uid,original_records ? 0 : 1,&account_room);
        if (refused) { space_occurrence_cursor_release(&cursor); return error(a,request,refused); }
    }
    uint64_t matches = 0, examined = 0, payload_bytes=0;
    const char *failure = NULL;
    CettaIndex index;
    SpaceOccurrenceCursorStep step=SPACE_OCCURRENCE_CURSOR_END;
    for (;;) {
        step=space_occurrence_cursor_next(&cursor,&index,NULL);
        if (step!=SPACE_OCCURRENCE_CURSOR_ITEM) break;
        if ((examined++ & 255u) == 0 && now_ms() >= deadline) { failure="query-time-limit"; break; }
        Atom *source = space_occurrence_cursor_atom(&cursor,index);
        if (!source) { failure="backend"; break; }
        Arena scratch; arena_init_detached(&scratch);
        Atom *row = source;
        Bytes fresh = {0};
        if (atom_has_vars(source)) {
            if (!wire(source,&fresh) || !decode(&scratch,fresh,&row)) failure="backend";
            bytes_free(&fresh);
        }
        Bindings bindings; bindings_init(&bindings);
        if (!failure && match_atoms(pattern,row,&bindings,&scratch)) {
            ++matches;
            if (!counting) {
                if (q.count == limit) failure="query-row-limit";
                else {
                    Atom *value = original_records ? source : bindings_apply(&bindings,&scratch,projection);
                    Bytes b = {0};
                    if (!value || !wire(value,&b)) failure="query-value-limit";
                    else if (b.size>s->max_query_bytes-payload_bytes || b.size>total_room-q.size) {
                        bytes_free(&b); failure="query-byte-limit";
                    } else if (b.size>account_room-q.size) {
                        bytes_free(&b); failure="account-view-byte-limit";
                    } else {
                        if (q.count==capacity) {
                            uint64_t room=total_room<account_room ? total_room : account_room;
                            size_t extra=(size_t)((room-q.size-b.size)/sizeof(*q.rows));
                            size_t desired=capacity ? capacity : 1;
                            if (desired>limit-capacity) desired=(size_t)limit-capacity;
                            if (extra>desired) extra=desired;
                            if (!extra) failure=account_room<total_room ? "account-view-byte-limit" : "query-byte-limit";
                            else {
                                Bytes *rows=realloc(q.rows,(capacity+extra)*sizeof(*q.rows));
                                if (!rows) failure="memory";
                                else { q.rows=rows; capacity+=extra; q.size+=extra*sizeof(*q.rows); }
                            }
                        }
                        if (failure) bytes_free(&b);
                        else { q.rows[q.count++]=b; q.size+=b.size; payload_bytes+=b.size; }
                    }
                }
            }
        }
        bindings_free(&bindings); arena_free(&scratch);
        if (failure || (first && q.count)) break;
    }
    space_occurrence_cursor_release(&cursor);
    if (!failure && step != SPACE_OCCURRENCE_CURSOR_ITEM && step != SPACE_OCCURRENCE_CURSOR_END) failure="backend";
    if (failure) {
        for (size_t i = 0; i < q.count; ++i) bytes_free(&q.rows[i]);
        free(q.rows); return error(a,request,failure);
    }
    if (counting) return reply(a,request,true,atom_int(a,matches));
    if (q.count<capacity) {
        Bytes *rows=realloc(q.rows,q.count*sizeof(*q.rows));
        if (rows || !q.count) {
            q.rows=rows; q.size-=(capacity-q.count)*sizeof(*q.rows);
        }
        /* A failed shrink retains and charges its actual allocation. */
    }
    q.expires = now_ms()+lease;
    s->snapshots[slot] = q; s->snapshot_bytes += q.size; ++s->next_cursor;
    Atom *args[] = {atom_string(a,s->boot),atom_int(a,q.id),atom_int(a,q.source),
        atom_int(a,q.revision),atom_int(a,q.count),atom_int(a,lease)};
    /* A first-occurrence selection is explicitly different from an
     * exhaustive snapshot. It preserves the native occurrence order. */
    Atom *view=form(a,first ? "selection" : "snapshot",args,6);
    if (profile) {
        CettaSpacePathIndexInfo info={0};
        space_path_indexes_info(st->space,&info);
        Atom *metrics[]={atom_int(a,examined),atom_int(a,info.index_count),
            atom_int(a,info.source_rows),atom_int(a,info.keyed_rows),atom_int(a,info.uncertain_rows)};
        Atom *values[]={view,form(a,"query-work",metrics,5)};
        view=form(a,"profile",values,2);
    }
    return reply(a,request,true,view);
}

/* Captured views are complete records, including duplicates and original
 * variable sharing. A client projection never becomes validation authority. */
static Atom *observe_scopes(Service *s, Client *client, Arena *a, Atom *request, Store *st,
                            AuthorityView authority,uint64_t creation_until,Bytes raw) {
    uint64_t deadline=now_ms()+s->query_ms;
    if (creation_until && creation_until<deadline) deadline=creation_until;
    Atom *reads=part(request,5), *writes=part(request,6);
    uint64_t limit,lease;
    if (!reads || !writes || reads->kind!=ATOM_EXPR || writes->kind!=ATOM_EXPR ||
        !atom_is_symbol(part(reads,0),"reads") || !atom_is_symbol(part(writes,0),"writes") ||
        !integer(part(request,7),&limit) || !limit || limit>QUERY_ROWS ||
        !integer(part(request,8),&lease) || !lease || lease>300000)
        return error(a,request,"invalid-observation");
    expire(s);
    size_t slot=SNAPSHOTS;
    for (size_t i=0;i<SNAPSHOTS;++i) if (!s->observations[i].id) { slot=i; break; }
    if (slot==SNAPSHOTS || s->next_cursor>=INT64_MAX) return error(a,request,"observation-limit");
    size_t count=(size_t)(reads->expr.len-1)+(size_t)(writes->expr.len-1);
    size_t request_bytes=creation_until ? raw.size : 0;
    if (count>SIZE_MAX/sizeof(ScopeView) || request_bytes>SIZE_MAX-count*sizeof(ScopeView) ||
        count*sizeof(ScopeView)+request_bytes>
        s->max_snapshot_bytes-s->snapshot_bytes-s->observation_bytes)
        return error(a,request,"observation-byte-limit");
    const char *quota=view_admit(s,client->principal.uid,1,count*sizeof(ScopeView)+request_bytes);
    if (quota) return error(a,request,quota);
    Observation o={.id=s->next_cursor++,.source=st->id,.revision=st->revision,.holder=client->principal,
        .authority_grant=authority.grant,.authority_generation=authority.generation,
        .reads=reads->expr.len-1,.writes=writes->expr.len-1,.size=count*sizeof(ScopeView)+request_bytes};
    o.views=count ? calloc(count,sizeof(*o.views)) : NULL;
    if (count && !o.views) return error(a,request,"memory");
    if (creation_until) {
        const char *id=NULL;
        if (!token(part(request,1),&id)) { free(o.views); return error(a,request,"invalid-id"); }
        if (!bytes_copy(&o.creation_request,raw.data,raw.size)) { free(o.views); return error(a,request,"memory"); }
        snprintf(o.creation,sizeof(o.creation),"%s",id); o.creation_until=creation_until;
    }
    s->observation_bytes+=o.size;
    s->capturing=&o;
    const char *failure=NULL;
    for (size_t i=0;i<count;++i) {
        if (now_ms()>=deadline) { failure="query-time-limit"; break; }
        Atom *pattern=i<o.reads ? part(reads,i+1) : part(writes,i-o.reads+1);
        Bytes encoded={0};
        if (!wire(pattern,&encoded)) { failure="unsupported-pattern"; break; }
        if (encoded.size>s->max_snapshot_bytes-s->snapshot_bytes-s->observation_bytes) {
            bytes_free(&encoded); failure="observation-byte-limit"; break;
        }
        quota=view_admit(s,client->principal.uid,0,encoded.size);
        if (quota) { bytes_free(&encoded); failure=quota; break; }
        o.views[i].pattern=encoded; o.size+=encoded.size; s->observation_bytes+=encoded.size;
        Atom *args[]={part(request,1),atom_symbol(a,"query"),part(request,3),part(request,4),
            pattern,atom_unit(a),part(request,7),part(request,8)};
        Atom *answer=query(s,a,form(a,"request",args,8),st,false,client->principal,
            authority.grant,authority.generation,true,deadline);
        uint64_t cursor;
        if (!shape(answer,"reply",4) || !atom_is_symbol(part(answer,2),"ok") ||
            !integer(part(part(answer,3),2),&cursor)) {
            s->capturing=NULL; observation_free(s,&o); return answer;
        }
        Snapshot *snapshot=find_snapshot(s,cursor);
        if (!snapshot) { failure="observation-capture"; break; }
        /* Query ownership includes its pointer vector as well as row bytes. */
        o.views[i].rows=*snapshot;
        s->snapshot_bytes-=snapshot->size;
        o.size+=snapshot->size; s->observation_bytes+=snapshot->size;
        *snapshot=(Snapshot){0};
    }
    s->capturing=NULL;
    if (!failure && creation_until && now_ms()>=creation_until) failure="creation-expired";
    if (failure) { observation_free(s,&o); return error(a,request,failure); }
    o.expires=now_ms()+lease; s->observations[slot]=o;
    Atom *args[]={atom_string(a,s->boot),atom_int(a,o.id),atom_int(a,o.source),atom_int(a,o.revision),
        atom_int(a,o.reads),atom_int(a,o.writes),atom_int(a,lease)};
    return reply(a,request,true,form(a,"observation",args,7));
}

/* Compare against the current complete view without allocating another
 * materialized result. The cursor, transient bindings and wire row are freed
 * on every exit. A mismatch includes a newly present or missing occurrence. */
static const char *scope_current(Service *s, Store *st, ScopeView *view, uint64_t deadline) {
    Arena terms; arena_init_detached(&terms); Atom *pattern=NULL;
    SpaceOccurrenceCursor cursor; space_occurrence_cursor_init_empty(&cursor);
    const char *failure=NULL; size_t position=0; uint64_t examined=0;
    if (!decode(&terms,view->pattern,&pattern) ||
        !space_occurrence_cursor_init(st->space,pattern,&cursor)) failure="backend";
    CettaIndex index; SpaceOccurrenceCursorStep step=SPACE_OCCURRENCE_CURSOR_END;
    while (!failure && (step=space_occurrence_cursor_next(&cursor,&index,NULL))==SPACE_OCCURRENCE_CURSOR_ITEM) {
        if ((examined++ & 255u)==0 && now_ms()>=deadline) { failure="query-time-limit"; break; }
        Atom *source=space_occurrence_cursor_atom(&cursor,index);
        Arena scratch; arena_init_detached(&scratch);
        Bytes encoded={0}; Atom *row=source;
        if (!source) failure="backend";
        if (!failure && atom_has_vars(source) &&
            (!wire(source,&encoded) || !decode(&scratch,encoded,&row))) failure="backend";
        Bindings bindings; bindings_init(&bindings);
        if (!failure && match_atoms(pattern,row,&bindings,&scratch)) {
            if (!encoded.data && !wire(source,&encoded)) failure="backend";
            else if (position>=view->rows.count || !same_bytes(encoded,view->rows.rows[position]))
                failure="scope-conflict";
            ++position;
        }
        bindings_free(&bindings); bytes_free(&encoded); arena_free(&scratch);
    }
    if (!failure && step!=SPACE_OCCURRENCE_CURSOR_ITEM && step!=SPACE_OCCURRENCE_CURSOR_END) failure="backend";
    if (!failure && position!=view->rows.count) failure="scope-conflict";
    space_occurrence_cursor_release(&cursor); arena_free(&terms);
    (void)s;
    return failure;
}

static int scope_covers(Observation *o, Atom *value) {
    for (size_t i=o->reads;i<o->reads+o->writes;++i) {
        Arena scratch; arena_init_detached(&scratch); Atom *pattern=NULL;
        Bindings bindings; bindings_init(&bindings);
        bool decoded=decode(&scratch,o->views[i].pattern,&pattern);
        /* Coverage is one-way: the declared pattern may bind, but a stored
         * open value cannot acquire a narrower channel by instantiation. */
        bool covered=decoded && match_atoms(pattern,value,&bindings,&scratch);
        if (covered && atom_has_vars(value)) {
            Atom *instance=bindings_apply(&bindings,&scratch,value);
            if (!instance) { bindings_free(&bindings); arena_free(&scratch); return -1; }
            covered=atom_alpha_eq(value,instance);
        }
        bindings_free(&bindings); arena_free(&scratch);
        if (!decoded) return -1;
        if (covered) return 1;
    }
    return 0;
}

static const char *batch_admit(Service *s, Store *st, Atom *ops) {
    if (!ops || ops->kind!=ATOM_EXPR || !atom_is_symbol(part(ops,0),"ops") || ops->expr.len>4097)
        return "invalid-batch";
    uint64_t added=0,added_bytes=0;
    for (size_t i=1;i<ops->expr.len;++i) {
        Atom *op=part(ops,i); Bytes b={0};
        if (!(shape(op,"add",2) || shape(op,"remove",2))) return "invalid-batch";
        if (!wire(part(op,1),&b)) return "unsupported-value";
        size_t size=b.size; bytes_free(&b);
        if (size>FACT_BYTES) return "fact-limit";
        if (shape(op,"add",2)) { ++added; added_bytes+=size; }
    }
    return space_length64(st->space)+added>s->max_facts ||
        st->live_bytes+added_bytes>s->max_live_bytes ? "space-limit" : NULL;
}

static Atom *resolved_observed_batch(Arena *a, Atom *request, Atom *revision) {
    uint64_t number,source,observation; const char *boot=NULL;
    if (!shape(request,"request",8) || !atom_is_symbol(part(request,2),"batch-observed") ||
        !integer(revision,&number) || !integer(part(request,4),&source) ||
        !token(part(request,5),&boot) || !integer(part(request,6),&observation)) return NULL;
    Atom *args[]={part(request,1),atom_symbol(a,"batch"),part(request,3),part(request,4),revision,part(request,7)};
    return form(a,"request",args,6);
}

static void affected_scope_watches(Service *s, Atom *application, Atom *bundle,
                                   bool affected[CLIENTS]) {
    if (!s->clients) return;
    uint64_t source;
    if (!integer(part(application,4),&source)) return;
    Store *st=find_store(s,source);
    bool batch=is_batch_request(application);
    bool append=is_append_request(application);
    Atom *operations=batch ? request_ops(application) :
        append ? (bundle ? bundle : request_bundle(application)) : NULL;
    for (size_t i=0;i<CLIENTS;++i) {
        Client *client=&s->clients[i]; Observation *o=&client->watched;
        if (!client->watching || !o->id || o->source!=source || client->watch_dirty) continue;
        bool changed=!operations || operations->kind!=ATOM_EXPR;
        for (size_t j=1;!changed && j<operations->expr.len;++j) {
            Atom *op=part(operations,j),*value=batch ? part(op,1) : op;
            /* Swap removal in an unordered bag may reorder other scopes. */
            if (batch && atom_is_symbol(part(op,0),"remove") && (!st || !st->ordered)) {
                changed=true; break;
            }
            for (size_t k=0;!changed && k<o->reads+o->writes;++k) {
                Arena scratch; arena_init_detached(&scratch); Atom *pattern=NULL;
                Bindings bindings; bindings_init(&bindings);
                changed=!decode(&scratch,o->views[k].pattern,&pattern) ||
                    match_atoms(pattern,value,&bindings,&scratch);
                bindings_free(&bindings); arena_free(&scratch);
            }
        }
        if (changed) affected[i]=true;
    }
}
/* Whether some stored atom matches the pattern: 1, 0, or -1 on failure. This
 * is the service's own read; the requester needs no right to read. */
static int store_any(Store *st, Atom *pattern) {
    SpaceOccurrenceCursor cursor;
    space_occurrence_cursor_init_empty(&cursor);
    if (!store_consistent(st) || !space_occurrence_cursor_init(st->space,pattern,&cursor)) return -1;
    int found = 0; CettaIndex index;
    SpaceOccurrenceCursorStep step = SPACE_OCCURRENCE_CURSOR_END;
    while (!found && (step = space_occurrence_cursor_next(&cursor,&index,NULL)) == SPACE_OCCURRENCE_CURSOR_ITEM) {
        Atom *source = space_occurrence_cursor_atom(&cursor,index);
        if (!source) { found = -1; break; }
        Arena scratch; arena_init_detached(&scratch);
        Atom *row = source; Bytes fresh = {0};
        if (atom_has_vars(source) && (!wire(source,&fresh) || !decode(&scratch,fresh,&row))) found = -1;
        bytes_free(&fresh);
        Bindings bindings; bindings_init(&bindings);
        if (!found && match_atoms(pattern,row,&bindings,&scratch)) found = 1;
        bindings_free(&bindings); arena_free(&scratch);
    }
    if (!found && step != SPACE_OCCURRENCE_CURSOR_ITEM && step != SPACE_OCCURRENCE_CURSOR_END) found = -1;
    space_occurrence_cursor_release(&cursor);
    return found;
}
static const char *prepare_account_origin(Service *s,Arena *a,Client *client,Atom *request,
                                          const char *origin,PreparedCommit *prepared) {
    const char *request_id=NULL;
    if (!token(part(request,1),&request_id) || !hex_text(origin,32)) return "invalid-origin";
    Atom *raw=request_bundle(request);
    Atom *reference=origin_ref_atom(s,a,client->principal.uid,request_id,origin);
    Atom *materialized=with_marker(a,raw,"origin",reference);
    if (materialized==raw) return "origin-required";
    Bytes encoded={0};
    if (!wire(materialized,&encoded)) return "memory";
    char digest[65]; cetta_native_sha256_hex(encoded.data,encoded.size,digest);
    bytes_free(&encoded);
    prepared->bundle=materialized;
    prepared->origin_ref=reference;
    snprintf(prepared->origin,sizeof(prepared->origin),"%s",origin);
    snprintf(prepared->effect_digest,sizeof(prepared->effect_digest),"%s",digest);
    return NULL;
}
/* Appended values are ground and bounded. When a grant has a shape, the whole
 * bundle must be one instance of it, with (caller) bound to the authenticated
 * account: shared variables make correlated fields agree, and the arity rules
 * out missing, extra or forged records. A uniqueness pattern makes an
 * identical retry a duplicate and any other reuse a conflict. NULL admits. */
static Atom *append_admit(Service *s,Arena *a,Client *client,Store *st,Atom *request,
                          PreparedCommit *prepared) {
    Atom *bundle=request_bundle(request);
    if (!bundle || bundle->kind != ATOM_EXPR || !atom_is_symbol(part(bundle,0),"bundle") ||
        bundle->expr.len < 2 || bundle->expr.len > 4097) return error(a,request,"invalid-bundle");
    if (st->revision >= INT64_MAX) return error(a,request,"revision-limit");
    Grant *g=client->owner ? NULL : request_authority_grant(s,client,st,request);
    Atom *grant=NULL,*outcome=NULL;
    if (g && !(g->rights&RIGHT_WRITE) && g->form.size) {
        if (!decode(a,g->form,&grant) || !shape(grant,"grant-form",3))
            return error(a,request,"backend");
        if (has_marker(grant,"origin")) {
            char origin[33];
            if (!random_id(origin)) return error(a,request,"entropy");
            const char *reason=prepare_account_origin(s,a,client,request,origin,prepared);
            if (reason) return error(a,request,reason);
            bundle=prepared->bundle;
            grant=with_marker(a,grant,"origin",prepared->origin_ref);
        }
    }
    uint64_t added_bytes = 0;
    for (size_t i = 1; i < bundle->expr.len; ++i) {
        Atom *value = part(bundle,i); Bytes b = {0};
        if (atom_has_vars(value)) return error(a,request,"open-value");
        if (!wire(value,&b)) return error(a,request,"unsupported-value");
        size_t size = b.size; bytes_free(&b);
        if (size > FACT_BYTES) return error(a,request,"fact-limit");
        added_bytes += size;
    }
    if (space_length64(st->space) + (bundle->expr.len - 1) > s->max_facts ||
        st->live_bytes + added_bytes > s->max_live_bytes) return error(a,request,"space-limit");
    if (!grant) return NULL;
    grant=with_marker(a,grant,"caller",account_atom(a,client));
    Atom *pattern = part(grant,1), *unique = part(grant,2);
    Bindings bindings; bindings_init(&bindings);
    if (!atom_is_symbol(pattern,"none") && !match_atoms(pattern,bundle,&bindings,a))
        outcome = error(a,request,"shape-mismatch");
    if (!outcome && !atom_is_symbol(unique,"none")) {
        Atom *key = bindings_apply(&bindings,a,unique);
        int seen = key ? store_any(st,key) : -1;
        if (seen < 0) outcome = error(a,request,"backend");
        else if (seen) {
            /* Identity is the record the pattern names. Its other records may
             * already be consumed, as a pickup consumes a pending record, so
             * a retry is a duplicate exactly when the named record is identical. */
            bool named = false, same = true;
            for (size_t i = 1; same && i < bundle->expr.len; ++i) {
                Bindings probe; bindings_init(&probe);
                bool keyed = match_atoms(key,part(bundle,i),&probe,a);
                bindings_free(&probe);
                if (keyed) {
                    Atom *found=NULL;
                    named=true;
                    if (!find_equal(st,part(bundle,i),&found)) {
                        outcome=error(a,request,"backend"); break;
                    }
                    same=found!=NULL;
                }
            }
            if (!outcome)
                outcome = named && same ? reply(a,request,true,atom_symbol(a,"duplicate"))
                                        : error(a,request,"id-conflict");
        }
    }
    bindings_free(&bindings); return outcome;
}
static bool compact(Service *s);
static Atom *dispatch(Service *s, Client *client, Arena *a, Atom *request, Bytes raw) {
    const char *id = NULL;
    if (!request || request->kind != ATOM_EXPR || request->expr.len < 3 ||
        !atom_is_symbol(part(request,0),"request") || !token(part(request,1),&id)) return NULL;
    if (s->poisoned) return NULL;
    if (shape(request,"request",3) && atom_is_symbol(part(request,2),"hello")) {
        Atom *caps[] = {atom_symbol(a,"conditional-batches"),atom_symbol(a,"snapshot-pages"),
            atom_symbol(a,"bounded-receipts"),atom_symbol(a,"journal-checkpoint"),atom_symbol(a,"no-eval"),
            atom_symbol(a,"portable-value-keys"),atom_symbol(a,"native-match-index"),
            atom_symbol(a,"tensor-ctn1-native"),atom_symbol(a,"tensor-f32-native"),
            atom_symbol(a,"revision-waits"),atom_symbol(a,"first-occurrence"),
            atom_symbol(a,"peer-principals"),atom_symbol(a,"one-shot-tickets"),
            atom_symbol(a,"attenuating-grants"),atom_symbol(a,"account-origins"),
            atom_symbol(a,"explicit-read-grants"),atom_symbol(a,"stable-principals"),
            atom_symbol(a,"native-ordered"),atom_symbol(a,"scoped-observations-v1"),
            atom_symbol(a,"scope-waits-v1"),
            atom_symbol(a,"scoped-creation-v2"),atom_symbol(a,"account-view-budgets-v1")};
        /* Only the owner creates stores, so only it needs the catalog revision. */
        Atom *args[] = {atom_int(a,1),atom_string(a,s->uuid),atom_string(a,s->boot),
            atom_int(a,client->owner ? s->catalog_revision : 0),form(a,"capabilities",caps,sizeof(caps)/sizeof(caps[0]))};
        return reply(a,request,true,form(a,"service",args,5));
    }
    if (shape(request,"request",3) && atom_is_symbol(part(request,2),"whoami")) {
        Atom *args[] = {account_atom(a,client),atom_symbol(a,client->owner ? "owner" : "member")};
        return reply(a,request,true,form(a,"principal",args,2));
    }
    bool current=client_principal_current(s,client);
    /* Epoch zero on a nonowner is an unbound account, not the owner root.
     * It may observe opaque absence but never consult generation-zero retry
     * state. A nonzero stale epoch is a rotated connection and fails closed. */
    bool unbound=!current && !client->principal.epoch;
    if (!current && !unbound) return error(a,request,"authority-refused");
    Decision *known=unbound ? NULL : find_decision(s,client->principal,id);
    Receipt *old=unbound ? NULL : find_receipt(s,client->principal,id);
    if (known) {
        if (!decision_current(s,client,known))
            return error(a,request,"authority-refused");
        if (!old) return error(a,request,"unknown-receipt");
        if (!same_bytes(old->request,raw)) return error(a,request,"reused-id");
        Atom *answer = NULL;
        return decode(a,old->reply,&answer) ? answer : NULL;
    }
    if (old) return error(a,request,"backend");
    if (atom_is_symbol(part(request,2),"observe-fetch") || atom_is_symbol(part(request,2),"observe-close")) {
        bool closing=atom_is_symbol(part(request,2),"observe-close"); uint64_t identity;
        if (request->expr.len!=(closing ? 5u : 8u) || !boot_matches(s,part(request,3)) ||
            !integer(part(request,4),&identity)) return error(a,request,"observation-identity");
        Observation *o=find_observation(s,identity);
        /* Observations belong to their principal, as cursors do. */
        if (!o || !principal_same(o->holder,client->principal))
            return error(a,request,"expired-or-closed");
        if (o->expires<=now_ms()) { observation_free(s,o); return error(a,request,"expired-or-closed"); }
        /* An owner can release its buffers after revocation or source drop. */
        if (closing) { observation_free(s,o); return reply(a,request,true,atom_symbol(a,"closed")); }
        Store *source=find_store(s,o->source);
        /* A page needs the exact grant generation that captured the view. */
        AuthorityView current=source ? authority_at(s,client,source,
            o->authority_grant,o->authority_generation) : (AuthorityView){0};
        if (!source || !(current.rights&RIGHT_READ) ||
            current.grant!=o->authority_grant ||
            current.generation!=o->authority_generation)
            return error(a,request,"expired-or-closed");
        uint64_t scope,offset,count;
        if (!integer(part(request,5),&scope) || scope>=o->reads+o->writes ||
            !integer(part(request,6),&offset) || !integer(part(request,7),&count) ||
            !count || count>PAGE_ROWS) return error(a,request,"page-limit");
        Snapshot *view=&o->views[scope].rows;
        if (offset>view->count) return error(a,request,"page-limit");
        if (count>view->count-offset) count=view->count-offset;
        Atom **rows=arena_alloc(a,count*sizeof(*rows)); size_t bytes=0;
        for (size_t i=0;i<count;++i) {
            bytes+=view->rows[offset+i].size;
            if (bytes>CETTA_ATOM_WIRE_BYTES-1024) return error(a,request,"page-byte-limit");
            if (!decode(a,view->rows[offset+i],&rows[i])) return error(a,request,"corrupt");
        }
        Atom *args[]={atom_int(a,o->revision),atom_int(a,offset),form(a,"rows",rows,count),
            atom_int(a,offset+count),atom_bool(a,offset+count==view->count)};
        return reply(a,request,true,form(a,"page",args,5));
    }
    if (atom_is_symbol(part(request,2),"fetch") || atom_is_symbol(part(request,2),"close")) {
        bool closing = atom_is_symbol(part(request,2),"close"); uint64_t cursor;
        if (request->expr.len != (closing ? 5 : 7) || !boot_matches(s,part(request,3)) ||
            !integer(part(request,4),&cursor)) return error(a,request,"cursor-identity");
        Snapshot *q = find_snapshot(s,cursor);
        /* Cursors belong to their account and exact grant generation. Revoking
         * and later regranting the same rights cannot revive an old cursor. */
        if (q) {
            Store *source = find_store(s,q->source);
            AuthorityView current=source ? authority_at(s,client,source,
                q->authority_grant,q->authority_generation) : (AuthorityView){0};
            if (!principal_same(q->holder,client->principal) || !source ||
                !(current.rights & RIGHT_READ) ||
                current.grant!=q->authority_grant ||
                current.generation!=q->authority_generation) q = NULL;
        }
        if (!q) return error(a,request,"expired-or-closed");
        if (q->expires <= now_ms()) { snapshot_free(s,q); return error(a,request,"expired-or-closed"); }
        if (closing) { snapshot_free(s,q); return reply(a,request,true,atom_symbol(a,"closed")); }
        uint64_t offset,count;
        if (!integer(part(request,5),&offset) || !integer(part(request,6),&count) ||
            offset > q->count || !count || count > PAGE_ROWS) return error(a,request,"page-limit");
        if (count > q->count - offset) count = q->count - offset;
        Atom **rows = arena_alloc(a,count*sizeof(*rows));
        size_t total = 0;
        for (size_t i = 0; i < count; ++i) {
            total += q->rows[offset+i].size;
            if (total > CETTA_ATOM_WIRE_BYTES - 1024) return error(a,request,"page-byte-limit");
            if (!decode(a,q->rows[offset+i],&rows[i])) return error(a,request,"corrupt");
        }
        Atom *args[] = {atom_int(a,q->revision),atom_int(a,offset),form(a,"rows",rows,count),
            atom_int(a,offset+count),atom_bool(a,offset+count == q->count)};
        return reply(a,request,true,form(a,"page",args,5));
    }
    if (!service_matches(s,part(request,3))) return error(a,request,"service-identity");
    if (atom_is_symbol(part(request,2),"observe-abandon") && request->expr.len==8) {
        const char *creation=NULL; uint64_t source,until;
        if (!integer(part(request,4),&source) || !boot_matches(s,part(request,5)) ||
            !token(part(request,6),&creation) || !integer(part(request,7),&until))
            return error(a,request,"creation-identity");
        expire(s);
        if (find_waiting_creation(s,client->principal,creation)) return error(a,request,"creation-transferred");
        Observation *o=find_creation(s,client->principal,creation);
        if (o && (o->source!=source || o->creation_until!=until)) return error(a,request,"creation-identity");
        Store *owned_source=find_store(s,source);
        if (!o && (!owned_source || !(rights_of(s,client,owned_source)&RIGHT_READ)))
            return error(a,request,"missing-space");
        if (until>now_ms()+300000) return error(a,request,"invalid-creation-deadline");
        bool fenced=cancel_creation(s,client->principal,source,creation,until);
        if (o) observation_free(s,o);
        if (!fenced) return error(a,request,"creation-fence-limit");
        return reply(a,request,true,atom_symbol(a,"closed"));
    }
    if (atom_is_symbol(part(request,2),"principal-ref") && request->expr.len==4) {
        if (unbound) return error(a,request,"authority-refused");
        return reply(a,request,true,principal_ref_atom(s,a,client->principal));
    }
    if (atom_is_symbol(part(request,2),"list") && request->expr.len == 4) {
        Atom *spaces[STORES]; size_t count=0;
        for (size_t i=0;i<STORES;++i) if (s->stores[i]) {
            unsigned rights=rights_of(s,client,s->stores[i]);
            if (rights) spaces[count++]=describe_as(s,a,s->stores[i],rights);
        }
        Atom *args[]={atom_string(a,s->uuid),atom_int(a,client->owner ? s->catalog_revision : 0),
                      form(a,"spaces",spaces,count)};
        return reply(a,request,true,form(a,"catalog",args,3));
    }
    if (atom_is_symbol(part(request,2),"receipt") && request->expr.len == 5) {
        const char *target = NULL;
        if (!token(part(request,4),&target)) return error(a,request,"invalid-id");
        Decision *decision=unbound ? NULL : find_decision(s,client->principal,target);
        if (decision && !decision_current(s,client,decision))
            return error(a,request,"authority-refused");
        Receipt *r = decision ? find_receipt(s,client->principal,target) : NULL;
        Atom *value = NULL;
        if (r && !decode(a,r->reply,&value)) return NULL;
        return reply(a,request,true,r ? value : atom_symbol(a,"unknown-receipt"));
    }
    if (atom_is_symbol(part(request,2),"origin") && request->expr.len==5) {
        Decision *decision=unbound ? NULL : origin_reference(s,client,part(request,4));
        if (!decision || (!client->owner &&
            !principal_same(client->principal,decision->caller)))
            return error(a,request,"missing-origin");
        Atom *ticket=decision->ticket ? atom_int(a,decision->ticket) : atom_symbol(a,"none");
        Atom *authorityArgs[]={atom_int(a,decision->source),atom_int(a,decision->grant),
            atom_int(a,decision->generation),rights_atom(a,decision->required),ticket};
        Atom *args[]={part(request,4),principal_ref_atom(s,a,decision->caller),
            form(a,"origin-authority",authorityArgs,5),atom_string(a,decision->effect_digest)};
        return reply(a,request,true,form(a,"account-origin",args,4));
    }
    if (atom_is_symbol(part(request,2),"compact") && request->expr.len == 4) {
        if (!client->owner) return error(a,request,"forbidden");
        if (!compact(s)) { s->poisoned=true; return NULL; }
        return reply(a,request,true,atom_symbol(a,"compacted"));
    }
    if (atom_is_symbol(part(request,2),"rotate-principal") && request->expr.len==5) {
        uint32_t uid; PreparedCommit prepared={0};
        if (!client->owner) return error(a,request,"forbidden");
        if (!account_uid(part(request,4),&uid))
            return error(a,request,"invalid-principal");
        const char *reason=principal_prepare(s,uid,true,&prepared);
        if (reason) return error(a,request,reason);
        Atom *args[]={principal_ref_atom(s,a,prepared.principal.current)};
        return commit(s,a,client,request,raw,
            reply(a,request,true,form(a,"rotated-principal",args,1)),&prepared);
    }
    if (atom_is_symbol(part(request,2),"open") && request->expr.len == 7) {
        const char *name = NULL; SpaceEngine e; bool ordered; uint64_t revision;
        if (!token(part(request,4),&name) || !engine(part(request,5),&e,&ordered) ||
            !integer(part(request,6),&revision)) return error(a,request,"invalid-open");
        Store *existing = find_name(s,name);
        /* Other accounts open only stores they hold a right on; an existing
         * store without one answers exactly like a missing name. */
        if (!client->owner) {
            unsigned rights = existing ? rights_of(s,client,existing) : 0;
            if (!rights) return error(a,request,"missing-space");
            return existing->engine == e && existing->ordered == ordered ? reply(a,request,true,describe_as(s,a,existing,rights))
                                         : error(a,request,"backend-mismatch");
        }
        if (existing) return existing->engine == e && existing->ordered == ordered ? reply(a,request,true,describe(s,a,existing))
                                                  : error(a,request,"backend-mismatch");
        if (revision != s->catalog_revision) return error(a,request,"stale-catalog");
        if (store_slot(s) == STORES || s->next_store >= INT64_MAX || s->catalog_revision >= INT64_MAX)
            return error(a,request,"space-limit");
        if (e != SPACE_ENGINE_NATIVE && !cetta_mork_bridge_is_available())
            return error(a,request,"backend-unavailable");
        /* Allocate the empty backend before durably promising its creation. */
        Store *probe = store_new(s->next_store,name,e,ordered,0);
        if (!probe) return error(a,request,"backend");
        Atom *answer = reply(a,request,true,describe(s,a,probe));
        PreparedCommit prepared={.opened=probe};
        return commit(s,a,client,request,raw,answer,&prepared);
    }
    uint64_t store_id;
    if (!integer(part(request,4),&store_id)) return error(a,request,"invalid-space");
    Store *st = find_store(s,store_id);
    Grant *selected=st && !client->owner ?
        request_authority_grant(s,client,st,request) : NULL;
    AuthorityView authority=st ?
        (client->owner ? authority_of(s,client,st)
                       : grant_authority(s,st,selected,client->principal)) :
        (AuthorityView){0};
    unsigned rights = authority.rights;
    /* A store this account holds no right on is indistinguishable from none. */
    if (!rights) return error(a,request,"missing-space");
    if (atom_is_symbol(part(request,2),"observe") && request->expr.len==9) {
        if (!(rights&RIGHT_READ)) return error(a,request,"forbidden");
        return observe_scopes(s,client,a,request,st,authority,0,raw);
    }
    if (atom_is_symbol(part(request,2),"observe-until") && request->expr.len==11) {
        if (!(rights&RIGHT_READ)) return error(a,request,"forbidden");
        uint64_t until,now=now_ms();
        if (!boot_matches(s,part(request,9)) || !integer(part(request,10),&until))
            return error(a,request,"creation-identity");
        if (until<=now) return error(a,request,"creation-expired");
        if (until-now>300000) return error(a,request,"invalid-creation-deadline");
        expire(s);
        if (creation_cancelled(s,client->principal,store_id,id,until)) return error(a,request,"creation-closed");
        if (find_waiting_creation(s,client->principal,id)) return error(a,request,"creation-transferred");
        Observation *o=find_creation(s,client->principal,id);
        if (o) {
            if (!same_bytes(o->creation_request,raw)) return error(a,request,"reused-id");
            uint64_t current=now_ms();
            if (o->expires<=current) { observation_free(s,o); return error(a,request,"expired-or-closed"); }
            Atom *args[]={atom_string(a,s->boot),atom_int(a,o->id),atom_int(a,o->source),atom_int(a,o->revision),
                atom_int(a,o->reads),atom_int(a,o->writes),atom_int(a,o->expires-current)};
            return reply(a,request,true,form(a,"observation",args,7));
        }
        return observe_scopes(s,client,a,request,st,authority,until,raw);
    }
    if (atom_is_symbol(part(request,2),"observe-watch") && request->expr.len==8) {
        if (!(rights&RIGHT_READ)) return error(a,request,"forbidden");
        uint64_t identity,duration;
        if (!boot_matches(s,part(request,5)) || !integer(part(request,6),&identity) ||
            !integer(part(request,7),&duration) || duration>INT_MAX)
            return error(a,request,"invalid-watch");
        Observation *o=find_observation(s,identity);
        if (!o || !principal_same(o->holder,client->principal) || o->source!=st->id ||
            o->authority_grant!=authority.grant ||
            o->authority_generation!=authority.generation)
            return error(a,request,"expired-or-closed");
        if (o->expires<=now_ms()) { observation_free(s,o); return error(a,request,"expired-or-closed"); }
        const char *failure=NULL;
        if (st->revision!=o->revision) {
            uint64_t deadline=now_ms()+s->query_ms;
            for (size_t i=0;!failure && i<o->reads+o->writes;++i)
                failure=scope_current(s,st,&o->views[i],deadline);
        }
        if (failure || !duration) {
            observation_free(s,o);
            if (failure && strcmp(failure,"scope-conflict")) return error(a,request,failure);
            Atom *args[]={atom_string(a,s->uuid),atom_int(a,st->id),atom_int(a,st->revision)};
            return reply(a,request,true,form(a,failure ? "changed" : "unchanged",args,3));
        }
        if (s->watchers>=s->max_watchers) return error(a,request,"watch-capacity");
        if (account_watch_full(s,client->principal.uid)) return error(a,request,"account-watch-limit");
        /* Transfer ownership, extending the observation through this wait.
         * The old handle cannot also authorize a mutation or another wait. */
        client->watched=*o; *o=(Observation){0};
        client->watching=true; ++s->watchers;
        client->watch_source=st->id; client->watch_revision=st->revision;
        client->watch_authority_grant=authority.grant;
        client->watch_authority_generation=authority.generation;
        client->watch_due=now_ms()+duration; client->deadline=client->watch_due+30000;
        client->watch_dirty=false;
        return NULL;
    }
    if (atom_is_symbol(part(request,2),"batch-observed") && request->expr.len==8) {
        if (!(rights&RIGHT_WRITE)) return error(a,request,"forbidden");
        /* An observed batch carries no ticket. A grant whose writes spend
         * one-shot tickets keeps requiring them through the ticketed forms. */
        if (selected && (grant_requirements(selected)&RIGHT_WRITE))
            return error(a,request,"ticket-required");
        uint64_t identity;
        if (!boot_matches(s,part(request,5)) || !integer(part(request,6),&identity))
            return error(a,request,"observation-identity");
        Observation *o=find_observation(s,identity);
        if (!o || !principal_same(o->holder,client->principal) || o->source!=st->id ||
            o->authority_grant!=authority.grant ||
            o->authority_generation!=authority.generation)
            return error(a,request,"expired-or-closed");
        const char *failure=NULL;
        if (o->expires<=now_ms()) failure="expired-or-closed";
        if (!failure && st->revision>=INT64_MAX) failure="revision-limit";
        Atom *ops=part(request,7);
        if (!failure) failure=batch_admit(s,st,ops);
        for (size_t i=1;!failure && i<ops->expr.len;++i) {
            int allowed=scope_covers(o,part(part(ops,i),1));
            if (allowed!=1) failure=allowed<0 ? "backend" : "undeclared-write";
        }
        if (!failure && st->revision!=o->revision) {
            uint64_t deadline=now_ms()+s->query_ms;
            for (size_t i=0;!failure && i<o->reads+o->writes;++i)
                failure=scope_current(s,st,&o->views[i],deadline);
        }
        Atom *application=NULL;
        if (!failure) application=resolved_observed_batch(a,request,atom_int(a,st->revision));
        if (!failure && !application) failure="invalid-observation";
        /* An observation is one-use even for a no-op or rejected proposal. The
         * durable request/reply receipt, not this lease, handles exact retry. */
        observation_free(s,o);
        if (failure) return error(a,request,failure);
        return commit_resolved(s,a,client,request,application,raw,
            committed(s,a,request,st->id,st->revision+1),NULL);
    }
    if (atom_is_symbol(part(request,2),"watch") && observation_request_shape(request)) {
        if (!(rights & RIGHT_OBSERVE)) return error(a,request,"forbidden");
        uint64_t revision, duration;
        if (!integer(part(request,5),&revision) || revision > st->revision ||
            !integer(part(request,6),&duration) || duration > INT_MAX)
            return error(a,request,"invalid-watch");
        if (revision != st->revision || !duration) {
            Atom *args[]={atom_string(a,s->uuid),atom_int(a,st->id),atom_int(a,st->revision)};
            return reply(a,request,true,form(a,revision != st->revision ? "changed" : "unchanged",args,3));
        }
        /* Registration and revision comparison run on the storage owner.
         * No commit can occur between the check and the saved wait. This is
         * a read notification, never an application receive or a receipt. */
        if (s->watchers >= s->max_watchers) return error(a,request,"watch-capacity");
        if (account_watch_full(s,client->principal.uid)) return error(a,request,"account-watch-limit");
        client->watching=true; ++s->watchers;
        client->watch_source=st->id; client->watch_revision=revision;
        client->watch_authority_grant=authority.grant;
        client->watch_authority_generation=authority.generation;
        client->watch_due=now_ms()+duration;
        client->deadline=client->watch_due+30000;
        return NULL;
    }
    if (atom_is_symbol(part(request,2),"describe") && observation_request_shape(request)) {
        if (!(rights & RIGHT_OBSERVE)) return error(a,request,"forbidden");
        return reply(a,request,true,describe(s,a,st));
    }
    if (atom_is_symbol(part(request,2),"verify") && observation_request_shape(request)) {
        if (!(rights & RIGHT_READ)) return error(a,request,"forbidden");
        if (space_length64(st->space)>QUERY_ROWS || st->live_bytes>QUERY_BYTES)
            return error(a,request,"verification-limit");
        if (!store_consistent(st) || !verify_backend(st))
            return error(a,request,"backend-divergence");
        Atom *args[]={atom_string(a,s->uuid),atom_int(a,st->id),
                      atom_int(a,st->revision),atom_int(a,space_length64(st->space))};
        return reply(a,request,true,form(a,"verified",args,4));
    }
    if ((atom_is_symbol(part(request,2),"query") || atom_is_symbol(part(request,2),"query-first") ||
         atom_is_symbol(part(request,2),"query-profile") || atom_is_symbol(part(request,2),"query-first-profile")) &&
        observation_request_shape(request)) {
        if (!(rights & RIGHT_READ)) return error(a,request,"forbidden");
        return query(s,a,request,st,false,client->principal,authority.grant,authority.generation,
                     false,now_ms()+s->query_ms);
    }
    if (atom_is_symbol(part(request,2),"count") && observation_request_shape(request)) {
        if (!(rights & RIGHT_READ)) return error(a,request,"forbidden");
        return query(s,a,request,st,true,client->principal,authority.grant,authority.generation,
                     false,now_ms()+s->query_ms);
    }
    if (is_append_request(request)) {
        if ((is_ticketed_request(request) || is_authorized_request(request)) && client->owner)
            return error(a,request,"ticket-unavailable");
        if (!(rights & RIGHT_APPEND)) return error(a,request,"forbidden");
        Grant *grant=client->owner ? NULL : selected;
        uint64_t ticketId=0;
        if (is_authorized_request(request)) {
            if (!ticket_ref(s,st,part(request,6),&ticketId))
                return error(a,request,"invalid-ticket");
        } else if (!request_ticket(request,&ticketId)) {
            return error(a,request,"invalid-ticket");
        }
        if (!ticketId && grant && (grant_requirements(grant)&RIGHT_APPEND))
            return error(a,request,"ticket-required");
        if (ticketId && !ticket_available(st,ticketId,client->principal,authority.grant,
                authority.generation,RIGHT_APPEND))
            return error(a,request,"ticket-unavailable");
        PreparedCommit prepared={0};
        Atom *refused=append_admit(s,a,client,st,request,&prepared);
        if (refused) return refused;
        return commit(s,a,client,request,raw,reply(a,request,true,atom_symbol(a,"appended")),
            prepared.bundle ? &prepared : NULL);
    }
    bool ticketedGrant=atom_is_symbol(part(request,2),"grant-ticketed") && request->expr.len==10;
    if ((atom_is_symbol(part(request,2),"grant") && request->expr.len == 9) || ticketedGrant) {
        if (!client->owner) return error(a,request,"forbidden");
        uint32_t uid; unsigned granted,ticketRights=0;
        Atom *pattern = part(request,ticketedGrant ? 8 : 7);
        Atom *unique = part(request,ticketedGrant ? 9 : 8);
        bool bundle = pattern && pattern->kind == ATOM_EXPR && atom_is_symbol(part(pattern,0),"bundle") &&
            pattern->expr.len >= 2 && pattern->expr.len <= 4097;
        bool stampsOrigin=has_marker(pattern,"origin");
        if (!account_uid(part(request,5),&uid) || !rights_parse(part(request,6),&granted) ||
            (ticketedGrant && !rights_parse(part(request,7),&ticketRights)) ||
            (ticketRights&~(RIGHT_APPEND|RIGHT_WRITE)) ||
            (ticketRights&~effective_rights(granted)) ||
            !(bundle || atom_is_symbol(pattern,"none")) ||
            !unique || (has_marker(unique,"origin") && !stampsOrigin) ||
            (stampsOrigin && (granted&RIGHT_WRITE)))
            return error(a,request,"invalid-grant");
        if (st->grant_generation>=INT64_MAX) return error(a,request,"generation-limit");
        if (granted && !grant_capacity_available(s)) return error(a,request,"grant-capacity");
        if (granted && !grant_reserve(st)) return error(a,request,"memory");
        PreparedCommit prepared={.has_grant_form=true};
        if (!grant_form_bytes(pattern,unique,&prepared.grant_form))
            return error(a,request,"memory");
        if (granted) {
            const char *reason=principal_prepare(s,uid,false,&prepared);
            if (reason) { prepared_commit_free(&prepared); return error(a,request,reason); }
        }
        Atom *args[] = {part(request,5),rights_atom(a,granted),rights_atom(a,ticketRights)};
        return commit(s,a,client,request,raw,
            reply(a,request,true,form(a,"granted",args,ticketedGrant ? 3 : 2)),&prepared);
    }
    if (atom_is_symbol(part(request,2),"grant-root") && request->expr.len==11) {
        if (!client->owner) return error(a,request,"forbidden");
        uint32_t uid; unsigned granted,ticketRights; bool delegable;
        Atom *pattern=part(request,9), *unique=part(request,10);
        bool bundle=pattern && pattern->kind==ATOM_EXPR &&
            atom_is_symbol(part(pattern,0),"bundle") &&
            pattern->expr.len>=2 && pattern->expr.len<=4097;
        if (!account_uid(part(request,5),&uid) ||
            !rights_parse(part(request,6),&granted) || !granted ||
            !rights_parse(part(request,7),&ticketRights) ||
            !boolean_atom(part(request,8),&delegable) ||
            (ticketRights&~(RIGHT_APPEND|RIGHT_WRITE)) ||
            (ticketRights&~effective_rights(granted)) ||
            !(bundle || atom_is_symbol(pattern,"none")) || !unique ||
            (has_marker(unique,"origin") && !has_marker(pattern,"origin")) ||
            (has_marker(pattern,"origin") && (granted&RIGHT_WRITE)))
            return error(a,request,"invalid-grant");
        if (st->grant_generation>=INT64_MAX) return error(a,request,"generation-limit");
        if (!grant_capacity_available(s)) return error(a,request,"grant-capacity");
        if (!grant_reserve(st)) return error(a,request,"memory");
        PreparedCommit prepared={.has_grant_form=true};
        if (!grant_form_bytes(pattern,unique,&prepared.grant_form)) return error(a,request,"memory");
        const char *reason=principal_prepare(s,uid,false,&prepared);
        if (reason) { prepared_commit_free(&prepared); return error(a,request,reason); }
        Grant predicted={.id=st->grant_generation+1,.generation=st->grant_generation+1};
        Atom *args[]={grant_ref_atom(s,a,st,&predicted),part(request,5),
            rights_atom(a,granted),rights_atom(a,ticketRights),
            atom_symbol(a,delegable ? "true" : "false")};
        return commit(s,a,client,request,raw,
            reply(a,request,true,form(a,"granted-root",args,5)),&prepared);
    }
    if (atom_is_symbol(part(request,2),"delegate-grant") && request->expr.len==10) {
        if (client->owner || !selected || !authority.delegable)
            return error(a,request,"forbidden");
        uint32_t uid; unsigned granted,ticketRights; bool delegable;
        if (!account_uid(part(request,6),&uid) || uid==client->principal.uid ||
            !rights_parse(part(request,7),&granted) || !granted ||
            !rights_parse(part(request,8),&ticketRights) ||
            !boolean_atom(part(request,9),&delegable))
            return error(a,request,"invalid-delegation");
        if ((ticketRights&~(RIGHT_APPEND|RIGHT_WRITE)) ||
            (ticketRights&~effective_rights(granted)))
            return error(a,request,"invalid-delegation");
        /* The child keeps every requirement the parent places on a right it
         * holds; it may add requirements, never drop one. */
        if ((effective_rights(granted)&~authority.rights) ||
            (grant_requirements(selected)&effective_rights(granted)&
                ~requirement_closure(granted,ticketRights)) ||
            (delegable && !selected->delegable))
            return error(a,request,"attenuation");
        if (st->grant_generation>=INT64_MAX) return error(a,request,"generation-limit");
        if (!grant_capacity_available(s)) return error(a,request,"grant-capacity");
        PreparedCommit prepared={.has_grant_form=true};
        if (selected->form.size &&
            !bytes_copy(&prepared.grant_form,selected->form.data,selected->form.size))
            return error(a,request,"memory");
        if (!grant_reserve(st)) {
            prepared_commit_free(&prepared); return error(a,request,"memory");
        }
        const char *reason=principal_prepare(s,uid,false,&prepared);
        if (reason) { prepared_commit_free(&prepared); return error(a,request,reason); }
        Grant predicted={.id=st->grant_generation+1,.generation=st->grant_generation+1};
        Atom *args[]={grant_ref_atom(s,a,st,&predicted),part(request,6),
            rights_atom(a,granted),rights_atom(a,ticketRights),
            atom_symbol(a,delegable ? "true" : "false")};
        return commit(s,a,client,request,raw,
            reply(a,request,true,form(a,"delegated",args,5)),&prepared);
    }
    if (atom_is_symbol(part(request,2),"revoke-grant") && request->expr.len==6) {
        Grant *target=grant_ref(s,st,part(request,5));
        if (!target || target->revoked) return error(a,request,"missing-grant");
        if (!client->owner && (!selected || !authority.delegable ||
            !principal_same(target->issuer,client->principal)))
            return error(a,request,"forbidden");
        if (st->grant_generation>=INT64_MAX) return error(a,request,"generation-limit");
        Atom *args[]={part(request,5)};
        return commit(s,a,client,request,raw,
            reply(a,request,true,form(a,"revoked",args,1)),NULL);
    }
    if (atom_is_symbol(part(request,2),"issue-ticket") && request->expr.len==7) {
        if (!client->owner) return error(a,request,"forbidden");
        uint32_t uid; unsigned right;
        if (!account_uid(part(request,5),&uid) || !rights_parse(part(request,6),&right) ||
            (right!=RIGHT_APPEND && right!=RIGHT_WRITE)) return error(a,request,"invalid-ticket");
        Principal holder=current_principal(s,uid);
        Grant *grant=find_grant(st,holder);
        AuthorityView granted=grant_authority(s,st,grant,holder);
        if (!grant || !(granted.rights&right) || !(grant_requirements(grant)&right))
            return error(a,request,"forbidden");
        if (st->ticket_generation>=INT64_MAX) return error(a,request,"generation-limit");
        uint64_t total=ticket_total(s);
        bool full=total>=INT64_MAX || (s->max_tickets && total>=s->max_tickets);
        if (full || !ticket_reserve(st)) return error(a,request,full ? "ticket-capacity" : "memory");
        uint64_t issued=st->ticket_generation+1;
        Atom *args[]={atom_string(a,s->uuid),atom_int(a,st->id),atom_int(a,issued),
            atom_int(a,grant->generation),atom_symbol(a,right==RIGHT_APPEND ? "append" : "write")};
        return commit(s,a,client,request,raw,reply(a,request,true,form(a,"ticket",args,5)),NULL);
    }
    if (atom_is_symbol(part(request,2),"issue-ticket-v2") && request->expr.len==7) {
        if (!client->owner) return error(a,request,"forbidden");
        unsigned right;
        Grant *grant=grant_ref(s,st,part(request,5));
        AuthorityView granted=grant ? grant_authority(s,st,grant,grant->grantee) :
            (AuthorityView){0};
        if (!grant || !rights_parse(part(request,6),&right) ||
            (right!=RIGHT_APPEND && right!=RIGHT_WRITE))
            return error(a,request,"invalid-ticket");
        if (!(granted.rights&right) || !(grant_requirements(grant)&right))
            return error(a,request,"forbidden");
        if (st->ticket_generation>=INT64_MAX) return error(a,request,"generation-limit");
        uint64_t total=ticket_total(s);
        bool full=total>=INT64_MAX || (s->max_tickets && total>=s->max_tickets);
        if (full || !ticket_reserve(st)) return error(a,request,full ? "ticket-capacity" : "memory");
        Ticket predicted={.id=st->ticket_generation+1};
        Atom *args[]={part(request,5),ticket_ref_atom(s,a,st,&predicted),
            atom_symbol(a,right==RIGHT_APPEND ? "append" : "write")};
        return commit(s,a,client,request,raw,
            reply(a,request,true,form(a,"ticket-v2",args,3)),NULL);
    }
    if (atom_is_symbol(part(request,2),"revoke-ticket") && request->expr.len==6) {
        if (!client->owner) return error(a,request,"forbidden");
        uint64_t ticketId;
        Ticket *ticket=integer(part(request,5),&ticketId) ? find_ticket(st,ticketId) : NULL;
        if (!ticket || ticket->status!=TICKET_LIVE) return error(a,request,"ticket-unavailable");
        return commit(s,a,client,request,raw,reply(a,request,true,atom_symbol(a,"ticket-revoked")),NULL);
    }
    /* Another account's mutations are checked against its rights before the
     * revision, so a refused account learns nothing about the store. */
    if (!client->owner) {
        bool dropping = atom_is_symbol(part(request,2),"drop") && request->expr.len == 7;
        bool batching = is_batch_request(request);
        if (!dropping && !batching) return error(a,request,"unsupported-operation");
        if (dropping || !(rights & RIGHT_WRITE)) return error(a,request,"forbidden");
    }
    uint64_t revision;
    if (!integer(part(request,5),&revision) || revision != st->revision)
        return error(a,request,"stale-revision");
    if (st->revision >= INT64_MAX) return error(a,request,"revision-limit");
    if (atom_is_symbol(part(request,2),"drop") && request->expr.len == 7) {
        uint64_t catalog;
        if (!integer(part(request,6),&catalog) || catalog != s->catalog_revision)
            return error(a,request,"stale-catalog");
        if (catalog >= INT64_MAX) return error(a,request,"revision-limit");
        return commit(s,a,client,request,raw,reply(a,request,true,atom_symbol(a,"dropped")),NULL);
    }
    if (!is_batch_request(request))
        return error(a,request,"unsupported-operation");
    if ((is_ticketed_request(request) || is_authorized_request(request)) && client->owner)
        return error(a,request,"ticket-unavailable");
    Grant *grant=client->owner ? NULL : selected;
    uint64_t ticketId=0;
    if (is_authorized_request(request)) {
        if (!ticket_ref(s,st,part(request,7),&ticketId))
            return error(a,request,"invalid-ticket");
    } else if (!request_ticket(request,&ticketId)) {
        return error(a,request,"invalid-ticket");
    }
    if (!ticketId && grant && (grant_requirements(grant)&RIGHT_WRITE))
        return error(a,request,"ticket-required");
    if (ticketId && !ticket_available(st,ticketId,client->principal,authority.grant,
            authority.generation,RIGHT_WRITE))
        return error(a,request,"ticket-unavailable");
    Atom *ops = request_ops(request);
    const char *failure=batch_admit(s,st,ops);
    if (failure) return error(a,request,failure);
    /* Admission is conservative: a batch at the limit may remove first and
     * add later, but no effects are attempted until the whole batch fits. */
    return commit(s,a,client,request,raw,committed(s,a,request,st->id,st->revision+1),NULL);
}

static bool record_messages(Arena *a,Atom *record,bool raw,Bytes *req,Bytes *ans,
                            Atom **request,Atom **answer) {
    if (raw) {
        Atom *r=part(record,1),*v=part(record,2);
        if (!r || !v || r->kind!=ATOM_GROUNDED || r->ground.gkind!=GV_STRING ||
            v->kind!=ATOM_GROUNDED || v->ground.gkind!=GV_STRING ||
            !bytes_copy(req,(const unsigned char *)r->ground.sval,atom_string_len(r)) ||
            !bytes_copy(ans,(const unsigned char *)v->ground.sval,atom_string_len(v)) ||
            !decode(a,*req,request) || !decode(a,*ans,answer)) return false;
    } else {
        /* Read the initial syntax-record prototype without changing its values.
         * New journals/checkpoints use exact byte messages exclusively. */
        *request=part(record,1); *answer=part(record,2);
        if (!wire(*request,req) || !wire(*answer,ans)) return false;
    }
    bool same=false;
    return *request && (*request)->kind==ATOM_EXPR && (*request)->expr.len>=3 &&
        atom_is_symbol(part(*request,0),"request") && shape(*answer,"reply",4) &&
        atom_is_symbol(part(*answer,2),"ok") &&
        atom_same(part(*request,1),part(*answer,1),&same) && same;
}
static bool decision_stored_valid(const Decision *decision) {
    if (decision->root ? decision->caller.epoch!=0 : decision->caller.epoch==0)
        return false;
    if (decision->root)
        return !decision->source && !decision->grant && !decision->generation &&
            !decision->required && !decision->ticket && !decision->origin[0] &&
            !decision->effect_digest[0];
    bool origin_valid=(!decision->origin[0] && !decision->effect_digest[0]) ||
        (hex_text(decision->origin,32) && hex_text(decision->effect_digest,64) &&
         decision->required==RIGHT_APPEND);
    return origin_valid && decision->source && decision->grant==decision->generation &&
        (decision->required==RIGHT_OBSERVE || decision->required==RIGHT_READ ||
         decision->required==RIGHT_APPEND || decision->required==RIGHT_WRITE ||
         decision->required==REQUIRE_DELEGATE || decision->required==REQUIRE_REVOKE) &&
        (!decision->ticket || decision->required==RIGHT_APPEND || decision->required==RIGHT_WRITE);
}
static bool decision_valid(const Decision *decision) {
    return decision_stored_valid(decision) && (decision->root || decision->generation);
}
static bool legacy_request_target(Service *s,Atom *request) {
    size_t account=0; unsigned rights=1;
    if ((atom_is_symbol(part(request,2),"grant") && request->expr.len==9) ||
        (atom_is_symbol(part(request,2),"grant-ticketed") && request->expr.len==10) ||
        (atom_is_symbol(part(request,2),"grant-root") && request->expr.len==11))
        account=5;
    else if (atom_is_symbol(part(request,2),"delegate-grant") && request->expr.len==10)
        account=6;
    else return true;
    uint32_t uid;
    if (!account_uid(part(request,account),&uid)) return false;
    if (account==5 &&
        (atom_is_symbol(part(request,2),"grant") ||
         atom_is_symbol(part(request,2),"grant-ticketed")) &&
        !rights_parse(part(request,6),&rights)) return false;
    if (!rights) return true;
    return legacy_principal(s,uid).epoch!=0;
}
/* A legacy checkpoint retained reply bytes but not an explicit authority
 * stamp. Records without an account were written for the process root; keep
 * that historical stamp even if today's configured owner differs. Observation
 * still requires the client to be today's owner. A member's old reply is
 * retained as deliberately stale generation zero rather than guessed current
 * authority. */
static bool legacy_checkpoint_decision(Service *s,uint32_t uid,bool explicit_member,Atom *request,
                                       Decision *decision) {
    const char *id=NULL;
    if (!token(part(request,1),&id)) return false;
    bool root=!explicit_member;
    Principal caller=root ? (Principal){.uid=uid} : legacy_principal(s,uid);
    if (!root && !caller.epoch) return false;
    *decision=(Decision){.caller=caller,.root=root};
    snprintf(decision->id,sizeof(decision->id),"%s",id);
    if (root) return true;
    if (!integer(part(request,4),&decision->source) ||
        !(decision->required=decision_required(request))) return false;
    /* Generation zero never equals a live restricted grant generation. */
    return true;
}
static bool restore_authorized_event(Service *s,Arena *a,Atom *record,unsigned version,
                                     bool observed) {
    if (s->restoring) return false;
    uint64_t uid,epoch=0,root,source,grant=0,generation,required,ticket=0;
    uint64_t pending_uid=0,pending_epoch=0;
    size_t message=version>=5 ? 13 : version>=4 ? 10 : version>=3 ? 8 : version==2 ? 7 : 6;
    size_t root_pos=version>=5 ? 3 : 2;
    size_t source_pos=version>=5 ? 4 : 3;
    if (!integer(part(record,1),&uid) || uid>UINT32_MAX ||
        (version>=5 && !integer(part(record,2),&epoch)) ||
        !integer(part(record,root_pos),&root) || root>1 ||
        !integer(part(record,source_pos),&source)) return false;
    if (version>=5) {
        if (!integer(part(record,5),&grant) ||
            !integer(part(record,6),&generation) ||
            !integer(part(record,7),&required) || required>UINT_MAX ||
            !integer(part(record,8),&ticket) ||
            !integer(part(record,11),&pending_uid) || pending_uid>UINT32_MAX ||
            !integer(part(record,12),&pending_epoch)) return false;
    } else if (version>=3) {
        if (!integer(part(record,4),&grant) ||
            !integer(part(record,5),&generation) ||
            !integer(part(record,6),&required) || required>UINT_MAX ||
            !integer(part(record,7),&ticket)) return false;
    } else {
        if (!integer(part(record,4),&generation) ||
            !integer(part(record,5),&required) || required>UINT_MAX ||
            (version==2 && !integer(part(record,6),&ticket))) return false;
        grant=generation;
    }
    Atom *messages[]={part(record,message),part(record,message+1)};
    Atom *raw=form(a,"event-bytes",messages,2), *request=NULL,*answer=NULL;
    Bytes req={0},ans={0}; const char *id=NULL;
    Principal caller=root ? (Principal){.uid=(uint32_t)uid} :
        version>=5 ? (Principal){.uid=(uint32_t)uid,.epoch=epoch} :
        legacy_principal(s,(uint32_t)uid);
    if ((!root && !caller.epoch) || (root && epoch)) return false;
    Decision decision={.caller=caller,.root=root!=0,.source=source,
        .grant=grant,.generation=generation,.ticket=ticket,.required=(unsigned)required};
    if (version>=4) {
        const char *origin=NULL,*digest=NULL;
        size_t origin_pos=version>=5 ? 9 : 8;
        bool absent=atom_is_symbol(part(record,origin_pos),"none") &&
            atom_is_symbol(part(record,origin_pos+1),"none");
        bool present=text(part(record,origin_pos),&origin) &&
            text(part(record,origin_pos+1),&digest) &&
            hex_text(origin,32) && hex_text(digest,64);
        if (!absent && !present) return false;
        if (present) {
            snprintf(decision.origin,sizeof(decision.origin),"%s",origin);
            snprintf(decision.effect_digest,sizeof(decision.effect_digest),"%s",digest);
        }
    }
    Receipt receipt={0}; Client client={.fd=-1,.principal=caller,.owner=root!=0};
    Decision expected={0}; PreparedCommit prepared={0};
    bool ok=record_messages(a,raw,true,&req,&ans,&request,&answer) && token(part(request,1),&id);
    /* An observed batch is replayed as the batch its revision fence resolved. */
    Atom *application=!ok ? NULL : observed ?
        resolved_observed_batch(a,request,part(record,message+2)) : request;
    ok=ok && application;
    if (ok && version<5) ok=legacy_request_target(s,request);
    if (ok && version>=5 && pending_epoch) {
        ok=pending_epoch==s->principal_generation+1 && pending_uid<=UINT32_MAX &&
            (!find_principal(s,(uint32_t)pending_uid) ||
             atom_is_symbol(part(request,2),"rotate-principal"));
        if (ok) {
            ok=principal_reserve(s);
            prepared.principal.current=(Principal){.uid=(uint32_t)pending_uid,
                .epoch=pending_epoch};
            prepared.has_principal=ok;
        }
    } else if (ok && version>=5 && pending_uid) ok=false;
    if (ok) snprintf(decision.id,sizeof(decision.id),"%s",id);
    ok=ok && decision_valid(&decision) && service_matches(s,part(request,3)) &&
        !find_decision(s,caller,id) && !find_receipt(s,caller,id) &&
        !decision_describe(s,&client,application,&expected) && decision_same(&decision,&expected) &&
        decision_reserve(s) && receipt_prepare(&receipt,caller,id,&req,&ans);
    if (ok && decision.origin[0]) {
        const char *reason=prepare_account_origin(s,a,&client,request,decision.origin,&prepared);
        ok=!reason && !strcmp(prepared.effect_digest,decision.effect_digest);
    }
    if (ok) {
        ok=apply(s,&client,application,
            (prepared.bundle || prepared.has_principal) ? &prepared : NULL) &&
            !prepared.has_principal && decision_spend_ticket(s,&decision);
    }
    if (ok) { remember_decision(s,&decision); remember(s,&receipt); }
    else receipt_free(&receipt);
    prepared_commit_free(&prepared);
    bytes_free(&req); bytes_free(&ans); return ok;
}
static bool restore_record(Service *s, Arena *a, Atom *record, bool first) {
    if (first) {
        const char *uuid = NULL;
        if (!shape(record,"header",3) || !text(part(record,1),&uuid) || strlen(uuid)!=32 ||
            !part(record,2) || part(record,2)->kind!=ATOM_GROUNDED ||
            part(record,2)->ground.gkind!=GV_INT || part(record,2)->ground.ival!=1) return false;
        for (size_t i=0;i<32;++i) if (!strchr("0123456789abcdef",uuid[i])) return false;
        memcpy(s->uuid,uuid,33); s->next_store=1; return true;
    }
    if (shape(record,"catalog",3)) {
        if (!integer(part(record,1),&s->next_store) || !s->next_store ||
            !integer(part(record,2),&s->catalog_revision)) return false;
        s->restoring=true; return true;
    }
    if (shape(record,"restore-space",5) && s->restoring) {
        uint64_t id,rev; const char *name=NULL; SpaceEngine e; bool ordered;
        if (!integer(part(record,1),&id) || !id || id>=s->next_store || find_store(s,id) ||
            !token(part(record,2),&name) || find_name(s,name) || !engine(part(record,3),&e,&ordered) ||
            !integer(part(record,4),&rev)) return false;
        size_t slot=store_slot(s); if (slot==STORES) return false;
        s->stores[slot]=store_new(id,name,e,ordered,rev); return s->stores[slot]!=NULL;
    }
    if (shape(record,"restore-rows",3) && s->restoring) {
        uint64_t id; Atom *rows=part(record,2);
        if (!integer(part(record,1),&id) || !rows || rows->kind!=ATOM_EXPR ||
            !atom_is_symbol(part(rows,0),"rows")) return false;
        Store *st=find_store(s,id); if (!st) return false;
        for (size_t i=1;i<rows->expr.len;++i) if (!store_add(st,part(rows,i))) return false;
        return store_consistent(st) && space_length64(st->space)<=s->max_facts && st->live_bytes<=s->max_live_bytes;
    }
    if (shape(record,"restore-principal-clock",2) && s->restoring) {
        uint64_t generation;
        if (!integer(part(record,1),&generation) || generation>INT64_MAX ||
            s->principal_clock_restored || s->principal_count) return false;
        s->principal_generation=generation;
        s->principal_clock_restored=true;
        return true;
    }
    if (shape(record,"restore-principal",3) && s->restoring) {
        uint64_t uid,epoch;
        return s->principal_clock_restored && integer(part(record,1),&uid) &&
            uid<=UINT32_MAX && integer(part(record,2),&epoch) &&
            principal_restore(s,(Principal){.uid=(uint32_t)uid,.epoch=epoch});
    }
    if (shape(record,"restore-grant-clock",3) && s->restoring) {
        uint64_t id,generation;
        if (!integer(part(record,1),&id) || !integer(part(record,2),&generation) ||
            generation>INT64_MAX) return false;
        Store *st=find_store(s,id);
        if (!st || st->grant_clock_restored || st->grant_count) return false;
        st->grant_generation=generation; st->grant_clock_restored=true; return true;
    }
    if (shape(record,"restore-grant-v5",16) && s->restoring &&
        s->principal_clock_restored) {
        uint64_t store,id,generation,issuerUid,issuerEpoch,uid,epoch,rights,
            ticketRights,parentId,parentGeneration;
        bool delegable,directDefault,revoked; Atom *text_form=part(record,15);
        if (!integer(part(record,1),&store) || !integer(part(record,2),&id) ||
            !integer(part(record,3),&generation) ||
            !integer(part(record,4),&issuerUid) || issuerUid>UINT32_MAX ||
            !integer(part(record,5),&issuerEpoch) ||
            !integer(part(record,6),&uid) || uid>UINT32_MAX ||
            !integer(part(record,7),&epoch) ||
            !integer(part(record,8),&rights) || !rights || rights>15 ||
            !integer(part(record,9),&ticketRights) || ticketRights>15 ||
            !boolean_atom(part(record,10),&delegable) ||
            !integer(part(record,11),&parentId) ||
            !integer(part(record,12),&parentGeneration) ||
            !boolean_atom(part(record,13),&directDefault) ||
            !boolean_atom(part(record,14),&revoked) ||
            !text_form || text_form->kind!=ATOM_GROUNDED ||
            text_form->ground.gkind!=GV_STRING) return false;
        Store *st=find_store(s,store);
        if (!st || !st->grant_clock_restored || !epoch ||
            epoch>s->principal_generation || issuerEpoch>s->principal_generation ||
            (!issuerEpoch && issuerUid)) return false;
        Bytes form_bytes={0};
        if (atom_string_len(text_form) && !bytes_copy(&form_bytes,
                (const unsigned char *)text_form->ground.sval,atom_string_len(text_form)))
            return false;
        Grant grant={.id=id,.generation=generation,.parent_id=parentId,
            .parent_generation=parentGeneration,
            .grantee={.uid=(uint32_t)uid,.epoch=epoch},
            .issuer={.uid=(uint32_t)issuerUid,.epoch=issuerEpoch},
            .rights=(unsigned)rights,.ticket_rights=(unsigned)ticketRights,
            .delegable=delegable,.revoked=revoked,.direct_default=directDefault};
        return grant_restore(st,&grant,&form_bytes);
    }
    if (shape(record,"restore-grant-v4",14) && s->restoring &&
        !s->principal_clock_restored) {
        uint64_t store,id,generation,issuer,uid,rights,ticketRights,parentId,parentGeneration;
        bool delegable,directDefault,revoked; Atom *text_form=part(record,13);
        if (!integer(part(record,1),&store) || !integer(part(record,2),&id) ||
            !integer(part(record,3),&generation) ||
            !integer(part(record,4),&issuer) || issuer>UINT32_MAX ||
            !integer(part(record,5),&uid) || uid>UINT32_MAX ||
            !integer(part(record,6),&rights) || !rights || rights>15 ||
            !integer(part(record,7),&ticketRights) || ticketRights>15 ||
            !boolean_atom(part(record,8),&delegable) ||
            !integer(part(record,9),&parentId) ||
            !integer(part(record,10),&parentGeneration) ||
            !boolean_atom(part(record,11),&directDefault) ||
            !boolean_atom(part(record,12),&revoked) ||
            !text_form || text_form->kind!=ATOM_GROUNDED ||
            text_form->ground.gkind!=GV_STRING) return false;
        Store *st=find_store(s,store);
        if (!st || !st->grant_clock_restored) return false;
        Bytes form_bytes={0};
        if (atom_string_len(text_form) && !bytes_copy(&form_bytes,
                (const unsigned char *)text_form->ground.sval,atom_string_len(text_form)))
            return false;
        Principal grantee=legacy_principal(s,(uint32_t)uid);
        Principal source=issuer ? legacy_principal(s,(uint32_t)issuer) : (Principal){0};
        if (!grantee.epoch || (issuer && !source.epoch)) { bytes_free(&form_bytes); return false; }
        Grant grant={.id=id,.generation=generation,.parent_id=parentId,
            .parent_generation=parentGeneration,.grantee=grantee,
            .issuer=source,.rights=(unsigned)rights,
            .ticket_rights=(unsigned)ticketRights,.delegable=delegable,
            .revoked=revoked,.direct_default=directDefault};
        return grant_restore(st,&grant,&form_bytes);
    }
    if (shape(record,"restore-grant-v3",7) && s->restoring &&
        !s->principal_clock_restored) {
        uint64_t id,uid,rights,ticketRights,generation; Atom *text_form=part(record,6);
        if (!integer(part(record,1),&id) || !integer(part(record,2),&uid) || uid>UINT32_MAX ||
            !integer(part(record,3),&rights) || !rights || rights>15 ||
            !integer(part(record,4),&ticketRights) || ticketRights>15 ||
            !integer(part(record,5),&generation) || !generation || generation>INT64_MAX ||
            !text_form || text_form->kind!=ATOM_GROUNDED || text_form->ground.gkind!=GV_STRING)
            return false;
        Store *st=find_store(s,id);
        if (!st || !st->grant_clock_restored) return false;
        Bytes form_bytes={0};
        if (atom_string_len(text_form) && !bytes_copy(&form_bytes,
                (const unsigned char *)text_form->ground.sval,atom_string_len(text_form))) return false;
        Principal grantee=legacy_principal(s,(uint32_t)uid);
        if (!grantee.epoch) { bytes_free(&form_bytes); return false; }
        Grant grant={.id=generation,.generation=generation,.grantee=grantee,
            .rights=(unsigned)rights,.ticket_rights=(unsigned)ticketRights,
            .direct_default=true};
        return grant_restore(st,&grant,&form_bytes);
    }
    if (shape(record,"restore-grant-v2",6) && s->restoring &&
        !s->principal_clock_restored) {
        uint64_t id,uid,rights,generation; Atom *text_form=part(record,5);
        if (!integer(part(record,1),&id) || !integer(part(record,2),&uid) || uid>UINT32_MAX ||
            !integer(part(record,3),&rights) || !rights || rights>15 ||
            !integer(part(record,4),&generation) || !generation || generation>INT64_MAX ||
            !text_form || text_form->kind!=ATOM_GROUNDED || text_form->ground.gkind!=GV_STRING)
            return false;
        Store *st=find_store(s,id);
        if (!st || !st->grant_clock_restored) return false;
        Bytes form_bytes={0};
        if (atom_string_len(text_form) && !bytes_copy(&form_bytes,
                (const unsigned char *)text_form->ground.sval,atom_string_len(text_form))) return false;
        Principal grantee=legacy_principal(s,(uint32_t)uid);
        if (!grantee.epoch) { bytes_free(&form_bytes); return false; }
        Grant grant={.id=generation,.generation=generation,.grantee=grantee,
            .rights=(unsigned)rights,.direct_default=true};
        return grant_restore(st,&grant,&form_bytes);
    }
    /* Backward-compatible checkpoints had grants but no generation watermark.
     * Reconstruct a fresh monotone ordering while reading that old checkpoint. */
    if (shape(record,"restore-grant",5) && s->restoring &&
        !s->principal_clock_restored) {
        uint64_t id,uid,rights; Atom *text_form=part(record,4);
        if (!integer(part(record,1),&id) || !integer(part(record,2),&uid) || uid>UINT32_MAX ||
            !integer(part(record,3),&rights) || !rights || rights>15 ||
            text_form->kind!=ATOM_GROUNDED || text_form->ground.gkind!=GV_STRING) return false;
        Store *st=find_store(s,id);
        Principal grantee=legacy_principal(s,(uint32_t)uid);
        if (!st || st->grant_clock_restored || !grantee.epoch || find_grant(st,grantee)) return false;
        Bytes form_bytes={0};
        if (atom_string_len(text_form) && !bytes_copy(&form_bytes,
                (const unsigned char *)text_form->ground.sval,atom_string_len(text_form))) return false;
        return grant_set(st,grantee,(unsigned)rights,0,&form_bytes);
    }
    if (shape(record,"restore-ticket-clock",3) && s->restoring) {
        uint64_t id,generation;
        if (!integer(part(record,1),&id) || !integer(part(record,2),&generation) ||
            generation>INT64_MAX) return false;
        Store *st=find_store(s,id);
        if (!st || st->ticket_clock_restored || st->ticket_count) return false;
        st->ticket_generation=generation; st->ticket_clock_restored=true; return true;
    }
    if (shape(record,"restore-ticket-v3",10) && s->restoring &&
        s->principal_clock_restored) {
        uint64_t store,ticketId,grantId,grantGeneration,uid,epoch,right,status;
        const char *spent=NULL; Atom *spentAtom=part(record,9);
        if (!integer(part(record,1),&store) ||
            !integer(part(record,2),&ticketId) ||
            !integer(part(record,3),&grantId) ||
            !integer(part(record,4),&grantGeneration) ||
            !integer(part(record,5),&uid) || uid>UINT32_MAX ||
            !integer(part(record,6),&epoch) || !epoch || epoch>s->principal_generation ||
            !integer(part(record,7),&right) || right>UINT_MAX ||
            !integer(part(record,8),&status) ||
            status<TICKET_LIVE || status>TICKET_REVOKED ||
            !((status==TICKET_SPENT && token(spentAtom,&spent)) ||
              (status!=TICKET_SPENT && atom_is_symbol(spentAtom,"none")))) return false;
        Store *st=find_store(s,store);
        if (!st || !st->ticket_clock_restored) return false;
        Ticket ticket={.id=ticketId,.grant_id=grantId,
            .grant_generation=grantGeneration,
            .holder={.uid=(uint32_t)uid,.epoch=epoch},
            .right=(unsigned)right,.status=(TicketStatus)status};
        if (spent) snprintf(ticket.spent_id,sizeof(ticket.spent_id),"%s",spent);
        return ticket_restore(st,&ticket);
    }
    if (shape(record,"restore-ticket-v2",9) && s->restoring &&
        !s->principal_clock_restored) {
        uint64_t store,ticketId,grantId,grantGeneration,uid,right,status;
        const char *spent=NULL; Atom *spentAtom=part(record,8);
        if (!integer(part(record,1),&store) ||
            !integer(part(record,2),&ticketId) ||
            !integer(part(record,3),&grantId) ||
            !integer(part(record,4),&grantGeneration) ||
            !integer(part(record,5),&uid) || uid>UINT32_MAX ||
            !integer(part(record,6),&right) || right>UINT_MAX ||
            !integer(part(record,7),&status) ||
            status<TICKET_LIVE || status>TICKET_REVOKED ||
            !((status==TICKET_SPENT && token(spentAtom,&spent)) ||
              (status!=TICKET_SPENT && atom_is_symbol(spentAtom,"none")))) return false;
        Store *st=find_store(s,store);
        if (!st || !st->ticket_clock_restored) return false;
        Principal holder=legacy_principal(s,(uint32_t)uid);
        if (!holder.epoch) return false;
        Ticket ticket={.id=ticketId,.grant_id=grantId,
            .grant_generation=grantGeneration,.holder=holder,
            .right=(unsigned)right,.status=(TicketStatus)status};
        if (spent) snprintf(ticket.spent_id,sizeof(ticket.spent_id),"%s",spent);
        return ticket_restore(st,&ticket);
    }
    if (shape(record,"restore-ticket",8) && s->restoring &&
        !s->principal_clock_restored) {
        uint64_t id,ticketId,uid,grantGeneration,right,status; const char *spent=NULL;
        Atom *spentAtom=part(record,7);
        if (!integer(part(record,1),&id) || !integer(part(record,2),&ticketId) ||
            !integer(part(record,3),&uid) || uid>UINT32_MAX ||
            !integer(part(record,4),&grantGeneration) ||
            !integer(part(record,5),&right) || right>UINT_MAX ||
            !integer(part(record,6),&status) || status<TICKET_LIVE || status>TICKET_REVOKED ||
            !((status==TICKET_SPENT && token(spentAtom,&spent)) ||
              (status!=TICKET_SPENT && atom_is_symbol(spentAtom,"none")))) return false;
        Store *st=find_store(s,id);
        if (!st || !st->ticket_clock_restored) return false;
        Principal holder=legacy_principal(s,(uint32_t)uid);
        if (!holder.epoch) return false;
        Ticket ticket={.id=ticketId,.grant_id=grantGeneration,
            .grant_generation=grantGeneration,.holder=holder,
            .right=(unsigned)right,.status=(TicketStatus)status};
        if (spent) snprintf(ticket.spent_id,sizeof(ticket.spent_id),"%s",spent);
        return ticket_restore(st,&ticket);
    }
    if (shape(record,"restore-decision-v2",8) && s->restoring &&
        !s->principal_clock_restored) {
        uint64_t uid,root,source,generation,required,ticket; const char *id=NULL;
        if (!integer(part(record,1),&uid) || uid>UINT32_MAX || !token(part(record,2),&id) ||
            !integer(part(record,3),&root) || root>1 ||
            !integer(part(record,4),&source) || !integer(part(record,5),&generation) ||
            !integer(part(record,6),&required) || required>UINT_MAX ||
            !integer(part(record,7),&ticket)) return false;
        Principal caller=root ? (Principal){.uid=(uint32_t)uid} :
            legacy_principal(s,(uint32_t)uid);
        if (!root && !caller.epoch) return false;
        Decision decision={.caller=caller,.root=root!=0,.source=source,
            .grant=generation,.generation=generation,.ticket=ticket,
            .required=(unsigned)required};
        snprintf(decision.id,sizeof(decision.id),"%s",id);
        return decision_stored_valid(&decision) && decision_restore(s,&decision);
    }
    if (shape(record,"restore-decision",7) && s->restoring &&
        !s->principal_clock_restored) {
        uint64_t uid,root,source,generation,required; const char *id=NULL;
        if (!integer(part(record,1),&uid) || uid>UINT32_MAX || !token(part(record,2),&id) ||
            !integer(part(record,3),&root) || root>1 ||
            !integer(part(record,4),&source) || !integer(part(record,5),&generation) ||
            !integer(part(record,6),&required) || required>UINT_MAX) return false;
        Principal caller=root ? (Principal){.uid=(uint32_t)uid} :
            legacy_principal(s,(uint32_t)uid);
        if (!root && !caller.epoch) return false;
        Decision decision={.caller=caller,.root=root!=0,.source=source,
            .grant=generation,.generation=generation,.required=(unsigned)required};
        snprintf(decision.id,sizeof(decision.id),"%s",id);
        return decision_stored_valid(&decision) && decision_restore(s,&decision);
    }
    if (shape(record,"restore-decision-v3",9) && s->restoring &&
        !s->principal_clock_restored) {
        uint64_t uid,root,source,grant,generation,required,ticket; const char *id=NULL;
        if (!integer(part(record,1),&uid) || uid>UINT32_MAX ||
            !token(part(record,2),&id) || !integer(part(record,3),&root) || root>1 ||
            !integer(part(record,4),&source) || !integer(part(record,5),&grant) ||
            !integer(part(record,6),&generation) ||
            !integer(part(record,7),&required) || required>UINT_MAX ||
            !integer(part(record,8),&ticket)) return false;
        Principal caller=root ? (Principal){.uid=(uint32_t)uid} :
            legacy_principal(s,(uint32_t)uid);
        if (!root && !caller.epoch) return false;
        Decision decision={.caller=caller,.root=root!=0,.source=source,
            .grant=grant,.generation=generation,.ticket=ticket,
            .required=(unsigned)required};
        snprintf(decision.id,sizeof(decision.id),"%s",id);
        return decision_stored_valid(&decision) && decision_restore(s,&decision);
    }
    if (shape(record,"restore-decision-v4",11) && s->restoring &&
        !s->principal_clock_restored) {
        uint64_t uid,root,source,grant,generation,required,ticket; const char *id=NULL;
        const char *origin=NULL,*digest=NULL;
        if (!integer(part(record,1),&uid) || uid>UINT32_MAX ||
            !token(part(record,2),&id) || !integer(part(record,3),&root) || root>1 ||
            !integer(part(record,4),&source) || !integer(part(record,5),&grant) ||
            !integer(part(record,6),&generation) ||
            !integer(part(record,7),&required) || required>UINT_MAX ||
            !integer(part(record,8),&ticket)) return false;
        bool absent=atom_is_symbol(part(record,9),"none") &&
            atom_is_symbol(part(record,10),"none");
        bool present=text(part(record,9),&origin) && text(part(record,10),&digest) &&
            hex_text(origin,32) && hex_text(digest,64);
        if (!absent && !present) return false;
        Principal caller=root ? (Principal){.uid=(uint32_t)uid} :
            legacy_principal(s,(uint32_t)uid);
        if (!root && !caller.epoch) return false;
        Decision decision={.caller=caller,.root=root!=0,.source=source,
            .grant=grant,.generation=generation,.ticket=ticket,
            .required=(unsigned)required};
        snprintf(decision.id,sizeof(decision.id),"%s",id);
        if (present) {
            snprintf(decision.origin,sizeof(decision.origin),"%s",origin);
            snprintf(decision.effect_digest,sizeof(decision.effect_digest),"%s",digest);
        }
        return decision_stored_valid(&decision) && decision_restore(s,&decision);
    }
    if (shape(record,"restore-decision-v5",12) && s->restoring &&
        s->principal_clock_restored) {
        uint64_t uid,epoch,root,source,grant,generation,required,ticket;
        const char *id=NULL,*origin=NULL,*digest=NULL;
        if (!integer(part(record,1),&uid) || uid>UINT32_MAX ||
            !integer(part(record,2),&epoch) ||
            !token(part(record,3),&id) || !integer(part(record,4),&root) || root>1 ||
            !integer(part(record,5),&source) || !integer(part(record,6),&grant) ||
            !integer(part(record,7),&generation) ||
            !integer(part(record,8),&required) || required>UINT_MAX ||
            !integer(part(record,9),&ticket)) return false;
        bool absent=atom_is_symbol(part(record,10),"none") &&
            atom_is_symbol(part(record,11),"none");
        bool present=text(part(record,10),&origin) && text(part(record,11),&digest) &&
            hex_text(origin,32) && hex_text(digest,64);
        if ((!root && (!epoch || epoch>s->principal_generation)) ||
            (root && epoch) || (!absent && !present)) return false;
        Decision decision={.caller={.uid=(uint32_t)uid,.epoch=epoch},
            .root=root!=0,.source=source,.grant=grant,.generation=generation,
            .ticket=ticket,.required=(unsigned)required};
        snprintf(decision.id,sizeof(decision.id),"%s",id);
        if (present) {
            snprintf(decision.origin,sizeof(decision.origin),"%s",origin);
            snprintf(decision.effect_digest,sizeof(decision.effect_digest),"%s",digest);
        }
        return decision_stored_valid(&decision) && decision_restore(s,&decision);
    }
    if (shape(record,"event-authorized-observed-v1",16))
        return restore_authorized_event(s,a,record,5,true);
    if (shape(record,"event-authorized-v5",15)) return restore_authorized_event(s,a,record,5,false);
    if (shape(record,"event-authorized-v4",12)) return restore_authorized_event(s,a,record,4,false);
    if (shape(record,"event-authorized-v3",10)) return restore_authorized_event(s,a,record,3,false);
    if (shape(record,"event-authorized-v2",9)) return restore_authorized_event(s,a,record,2,false);
    if (shape(record,"event-authorized",8)) return restore_authorized_event(s,a,record,1,false);
    /* Records that name an account carry its uid before the two messages;
     * all others were written for the process account. */
    uint64_t uid=s->process_uid,epoch=0;
    bool stable_receipt=shape(record,"receipt-from-v2",5);
    if (stable_receipt && (!s->restoring || !s->principal_clock_restored)) return false;
    Atom *resolved_revision=NULL;
    if (shape(record,"event-observed-v1",5)) {
        uint64_t revision;
        if (!integer(part(record,1),&uid) || uid>UINT32_MAX ||
            !integer(part(record,4),&revision)) return false;
        resolved_revision=part(record,4);
        /* Its writer recorded the process account in this field as well; the
         * other event forms of that line name no account for it. */
        Atom *messages[]={part(record,1),part(record,2),part(record,3)};
        record=uid==s->process_uid ? form(a,"event-bytes",messages+1,2)
                                   : form(a,"event-from",messages,3);
    }
    bool owned=shape(record,"receipt-from",4) || shape(record,"event-from",4);
    if (stable_receipt) {
        if (!integer(part(record,1),&uid) || uid>UINT32_MAX ||
            !integer(part(record,2),&epoch) ||
            (epoch && epoch>s->principal_generation)) return false;
        Atom *messages[]={part(record,3),part(record,4)};
        record=form(a,"receipt-bytes",messages,2);
    } else if (owned) {
        if (!integer(part(record,1),&uid) || uid>UINT32_MAX) return false;
        Atom *messages[]={part(record,2),part(record,3)};
        record=form(a,shape(record,"receipt-from",4) ? "receipt-bytes" : "event-bytes",messages,2);
    }
    Principal caller=stable_receipt ? (Principal){.uid=(uint32_t)uid,.epoch=epoch} :
        owned ? legacy_principal(s,(uint32_t)uid) : (Principal){.uid=(uint32_t)uid};
    if (owned && !caller.epoch) return false;
    bool raw_receipt=shape(record,"receipt-bytes",3);
    if ((raw_receipt || shape(record,"receipt",3)) && s->restoring) {
        const char *id=NULL; Bytes req={0},ans={0}; Atom *request=NULL,*answer=NULL;
        Receipt receipt={0}; Decision legacy={0};
        bool ok=record_messages(a,record,raw_receipt,&req,&ans,&request,&answer) &&
            token(part(request,1),&id) && !find_receipt(s,caller,id);
        bool needsDecision=ok && !find_decision(s,caller,id);
        if (stable_receipt && needsDecision) ok=false;
        else if (needsDecision)
            ok=legacy_checkpoint_decision(s,(uint32_t)uid,owned,request,&legacy) &&
                decision_restore(s,&legacy);
        ok=ok &&
            receipt_prepare(&receipt,caller,id,&req,&ans);
        if (ok) remember(s,&receipt);
        bytes_free(&req); bytes_free(&ans); return ok;
    }
    if (shape(record,"sealed-v2",5) && s->restoring &&
        !s->principal_clock_restored) {
        uint64_t spaces=0,rows=0,expected_spaces,expected_rows,expected_receipts,expected_decisions;
        for (size_t i=0;i<STORES;++i) if (s->stores[i]) {
            ++spaces; rows+=space_length64(s->stores[i]->space);
        }
        if (!integer(part(record,1),&expected_spaces) ||
            !integer(part(record,2),&expected_rows) ||
            !integer(part(record,3),&expected_receipts) ||
            !integer(part(record,4),&expected_decisions) || spaces!=expected_spaces ||
            rows!=expected_rows || s->receipt_count!=expected_receipts ||
            s->decision_count!=expected_decisions) return false;
        for (size_t i=0;i<STORES;++i)
            if (s->stores[i] && !grants_consistent(s->stores[i])) return false;
        s->restoring=false; return true;
    }
    if (shape(record,"sealed-v3",6) && s->restoring &&
        !s->principal_clock_restored) {
        uint64_t spaces=0,rows=0,expected_spaces,expected_rows,expected_receipts,
            expected_decisions,expected_tickets;
        for (size_t i=0;i<STORES;++i) if (s->stores[i]) {
            ++spaces; rows+=space_length64(s->stores[i]->space);
        }
        if (!integer(part(record,1),&expected_spaces) ||
            !integer(part(record,2),&expected_rows) ||
            !integer(part(record,3),&expected_receipts) ||
            !integer(part(record,4),&expected_decisions) ||
            !integer(part(record,5),&expected_tickets) || spaces!=expected_spaces ||
            rows!=expected_rows || s->receipt_count!=expected_receipts ||
            s->decision_count!=expected_decisions || ticket_total(s)!=expected_tickets) return false;
        for (size_t i=0;i<STORES;++i)
            if (s->stores[i] && !grants_consistent(s->stores[i])) return false;
        s->restoring=false; return true;
    }
    if ((shape(record,"sealed-v4",7) || shape(record,"sealed-v5",7)) &&
        s->restoring && !s->principal_clock_restored) {
        uint64_t spaces=0,rows=0,expected_spaces,expected_rows,expected_receipts,
            expected_decisions,expected_tickets,expected_grants;
        for (size_t i=0;i<STORES;++i) if (s->stores[i]) {
            ++spaces; rows+=space_length64(s->stores[i]->space);
        }
        if (!integer(part(record,1),&expected_spaces) ||
            !integer(part(record,2),&expected_rows) ||
            !integer(part(record,3),&expected_receipts) ||
            !integer(part(record,4),&expected_decisions) ||
            !integer(part(record,5),&expected_tickets) ||
            !integer(part(record,6),&expected_grants) ||
            spaces!=expected_spaces || rows!=expected_rows ||
            s->receipt_count!=expected_receipts ||
            s->decision_count!=expected_decisions ||
            ticket_total(s)!=expected_tickets ||
            grant_total(s)!=expected_grants) return false;
        for (size_t i=0;i<STORES;++i)
            if (s->stores[i] && !grants_consistent(s->stores[i])) return false;
        s->restoring=false; return true;
    }
    if (shape(record,"sealed-v6",9) && s->restoring) {
        uint64_t spaces=0,rows=0,expected_spaces,expected_rows,expected_receipts,
            expected_decisions,expected_tickets,expected_grants,expected_principals,
            expected_principal_generation;
        for (size_t i=0;i<STORES;++i) if (s->stores[i]) {
            ++spaces; rows+=space_length64(s->stores[i]->space);
        }
        if (!integer(part(record,1),&expected_spaces) ||
            !integer(part(record,2),&expected_rows) ||
            !integer(part(record,3),&expected_receipts) ||
            !integer(part(record,4),&expected_decisions) ||
            !integer(part(record,5),&expected_tickets) ||
            !integer(part(record,6),&expected_grants) ||
            !integer(part(record,7),&expected_principals) ||
            !integer(part(record,8),&expected_principal_generation) ||
            !s->principal_clock_restored || spaces!=expected_spaces ||
            rows!=expected_rows || s->receipt_count!=expected_receipts ||
            s->decision_count!=expected_decisions ||
            ticket_total(s)!=expected_tickets || grant_total(s)!=expected_grants ||
            s->principal_count!=expected_principals ||
            s->principal_generation!=expected_principal_generation) return false;
        for (size_t i=0;i<STORES;++i)
            if (s->stores[i] && !grants_consistent(s->stores[i])) return false;
        s->restoring=false; return true;
    }
    if (shape(record,"sealed",4) && s->restoring &&
        !s->principal_clock_restored) {
        uint64_t spaces=0,rows=0,expected_spaces,expected_rows,expected_receipts;
        for (size_t i=0;i<STORES;++i) if (s->stores[i]) {
            ++spaces; rows+=space_length64(s->stores[i]->space);
        }
        if (!integer(part(record,1),&expected_spaces) ||
            !integer(part(record,2),&expected_rows) ||
            !integer(part(record,3),&expected_receipts) || spaces!=expected_spaces ||
            rows!=expected_rows || s->receipt_count!=expected_receipts) return false;
        for (size_t i=0;i<STORES;++i)
            if (s->stores[i] && !grants_consistent(s->stores[i])) return false;
        s->restoring=false; return true;
    }
    if (s->restoring) return false;
    bool raw_event=shape(record,"event-bytes",3);
    if (!raw_event && !shape(record,"event",3)) return false;
    s->restoring=false;
    Atom *request=NULL,*answer=NULL;
    const char *id=NULL; Bytes req={0},ans={0};
    Decision decision={0}; Receipt receipt={0};
    /* An old event without an account was authorized as the process root when
     * it was written. Replay preserves that historical fact; live observation
     * separately checks the current owner configuration. */
    Client client={.fd=-1,.principal=caller,.owner=!owned};
    bool ok=record_messages(a,record,raw_event,&req,&ans,&request,&answer);
    Atom *application=!ok ? NULL : resolved_revision ?
        resolved_observed_batch(a,request,resolved_revision) : request;
    ok=ok && application &&
        token(part(request,1),&id) && service_matches(s,part(request,3)) &&
        legacy_request_target(s,application) &&
        !find_decision(s,caller,id) && !find_receipt(s,caller,id) &&
        !decision_describe(s,&client,application,&decision) && decision_reserve(s) &&
        receipt_prepare(&receipt,caller,id,&req,&ans);
    if (ok) ok=apply(s,&client,application,NULL);
    if (!ok) {
        receipt_free(&receipt);
        bytes_free(&req); bytes_free(&ans); return false;
    }
    remember_decision(s,&decision); remember(s,&receipt);
    bytes_free(&req); bytes_free(&ans); return true;
}
static bool recover(Service *s) {
    struct stat st; if (fstat(s->journal,&st)) return false;
    if (!st.st_size) {
        if (!random_id(s->uuid)) return false;
        Arena a; arena_init_detached(&a);
        Atom *args[]={atom_string(&a,s->uuid),atom_int(&a,1)};
        bool ok=journal_record(s->journal,form(&a,"header",args,2),true,&s->journal_bytes) && fsync(s->directory)==0;
        arena_free(&a); s->next_store=1; return ok;
    }
    uint64_t position=0; bool first=true;
    s->replaying=true;
    while (position<(uint64_t)st.st_size) {
        CettaJournalRecord frame={0};
        CettaJournalStatus status=cetta_journal_read(s->journal,position,
            (uint64_t)st.st_size,CETTA_ATOM_WIRE_BYTES,&frame);
        if (status==CETTA_JOURNAL_TORN) break;
        if (status!=CETTA_JOURNAL_OK || frame.size<4u) {
            cetta_journal_record_free(&frame); s->replaying=false; return false;
        }
        Bytes b={frame.data,frame.size};
        Arena a; arena_init_detached(&a); Atom *record=NULL;
        bool ok=decode(&a,b,&record) && restore_record(s,&a,record,first);
        if (ok && (shape(record,"sealed",4) || shape(record,"sealed-v2",5) ||
                   shape(record,"sealed-v3",6) || shape(record,"sealed-v4",7) ||
                   shape(record,"sealed-v5",7) || shape(record,"sealed-v6",9)))
            s->checkpoint_bytes=frame.next;
        position=frame.next;
        arena_free(&a); cetta_journal_record_free(&frame);
        if (!ok) { s->replaying=false; return false; }
        first=false;
    }
    s->replaying=false;
    if (first || s->restoring) return false;
    if (position!=(uint64_t)st.st_size) {
        fprintf(stderr,"space-service: discarding incomplete journal tail\n");
        if (cetta_journal_trim_tail(s->journal,position)!=CETTA_JOURNAL_OK) return false;
    }
    if (lseek(s->journal,(off_t)position,SEEK_SET)<0) return false;
    s->journal_bytes=position; s->restoring=false; return true;
}

static bool write_checkpoint(Service *s,int fd,uint64_t *size) {
    Arena a; arena_init_detached(&a);
    Atom *header[]={atom_string(&a,s->uuid),atom_int(&a,1)};
    Atom *catalog[]={atom_int(&a,s->next_store),atom_int(&a,s->catalog_revision)};
    bool ok=journal_record(fd,form(&a,"header",header,2),false,size) &&
        journal_record(fd,form(&a,"catalog",catalog,2),false,size);
    arena_free(&a);
    if (ok) {
        arena_init_detached(&a);
        Atom *parts[]={atom_int(&a,s->principal_generation)};
        ok=journal_record(fd,form(&a,"restore-principal-clock",parts,1),false,size);
        arena_free(&a);
    }
    for (size_t i=0;ok && i<s->principal_count;++i) {
        arena_init_detached(&a);
        Principal principal=s->principals[i].current;
        Atom *parts[]={atom_int(&a,principal.uid),atom_int(&a,principal.epoch)};
        ok=journal_record(fd,form(&a,"restore-principal",parts,2),false,size);
        arena_free(&a);
    }
    for (size_t i=0;ok && i<STORES;++i) {
        Store *st=s->stores[i]; if (!st) continue;
        arena_init_detached(&a);
        Atom *args[]={atom_int(&a,st->id),atom_string(&a,st->name),
            atom_symbol(&a,store_engine_name(st)),atom_int(&a,st->revision)};
        ok=journal_record(fd,form(&a,"restore-space",args,4),false,size);
        arena_free(&a);
        CettaCount count=space_length64(st->space);
        for (CettaIndex row=0;ok && row<count;) {
            Atom *rows[128]; size_t used=0,bytes=0;
            while (row<count && used<128) {
                Atom *value=space_get_at64(st->space,row); Bytes b={0};
                if (!value || !wire(value,&b)) { ok=false; break; }
                size_t n=b.size; bytes_free(&b);
                if (used && (bytes+n>32768 || atom_has_vars(value) ||
                             atom_has_vars(rows[0]))) break;
                rows[used++]=value; bytes+=n; ++row;
            }
            if (!ok) break;
            arena_init_detached(&a);
            Atom *parts[]={atom_int(&a,st->id),form(&a,"rows",rows,used)};
            ok=journal_record(fd,form(&a,"restore-rows",parts,2),false,size);
            arena_free(&a);
        }
        if (ok) {
            arena_init_detached(&a);
            Atom *parts[]={atom_int(&a,st->id),atom_int(&a,st->grant_generation)};
            ok=journal_record(fd,form(&a,"restore-grant-clock",parts,2),false,size);
            arena_free(&a);
        }
        for (size_t g=0;ok && g<st->grant_count;++g) {
            Grant *grant=&st->grants[g];
            arena_init_detached(&a);
            Atom *parts[]={atom_int(&a,st->id),atom_int(&a,grant->id),
                atom_int(&a,grant->generation),atom_int(&a,grant->issuer.uid),
                atom_int(&a,grant->issuer.epoch),atom_int(&a,grant->grantee.uid),
                atom_int(&a,grant->grantee.epoch),atom_int(&a,grant->rights),
                atom_int(&a,grant->ticket_rights),
                atom_symbol(&a,grant->delegable ? "true" : "false"),
                atom_int(&a,grant->parent_id),atom_int(&a,grant->parent_generation),
                atom_symbol(&a,grant->direct_default ? "true" : "false"),
                atom_symbol(&a,grant->revoked ? "true" : "false"),
                atom_string_n(&a,grant->form.size ? (const char *)grant->form.data : "",grant->form.size)};
            ok=journal_record(fd,form(&a,"restore-grant-v5",parts,15),false,size);
            arena_free(&a);
        }
        if (ok) {
            arena_init_detached(&a);
            Atom *parts[]={atom_int(&a,st->id),atom_int(&a,st->ticket_generation)};
            ok=journal_record(fd,form(&a,"restore-ticket-clock",parts,2),false,size);
            arena_free(&a);
        }
        for (size_t t=0;ok && t<st->ticket_count;++t) {
            Ticket *ticket=&st->tickets[t];
            arena_init_detached(&a);
            Atom *parts[]={atom_int(&a,st->id),atom_int(&a,ticket->id),
                atom_int(&a,ticket->grant_id),atom_int(&a,ticket->grant_generation),
                atom_int(&a,ticket->holder.uid),atom_int(&a,ticket->holder.epoch),
                atom_int(&a,ticket->right),
                atom_int(&a,ticket->status),ticket->status==TICKET_SPENT ?
                    atom_string(&a,ticket->spent_id) : atom_symbol(&a,"none")};
            ok=journal_record(fd,form(&a,"restore-ticket-v3",parts,9),false,size);
            arena_free(&a);
        }
    }
    for (size_t i=0;ok && i<s->decision_count;++i) {
        arena_init_detached(&a);
        Decision *decision=&s->decisions[i];
        Atom *parts[]={atom_int(&a,decision->caller.uid),atom_int(&a,decision->caller.epoch),
            atom_string(&a,decision->id),
            atom_int(&a,decision->root ? 1 : 0),atom_int(&a,decision->source),
            atom_int(&a,decision->grant),atom_int(&a,decision->generation),
            atom_int(&a,decision->required),atom_int(&a,decision->ticket),
            decision->origin[0] ? atom_string(&a,decision->origin) : atom_symbol(&a,"none"),
            decision->origin[0] ? atom_string(&a,decision->effect_digest) : atom_symbol(&a,"none")};
        ok=journal_record(fd,form(&a,"restore-decision-v5",parts,11),false,size);
        arena_free(&a);
    }
    for (size_t i=0;ok && i<s->receipt_count;++i) {
        arena_init_detached(&a);
        Receipt *r=&s->receipts[i];
        Atom *parts[]={atom_int(&a,r->caller.uid),atom_int(&a,r->caller.epoch),
                      atom_string_n(&a,(const char *)r->request.data,r->request.size),
                      atom_string_n(&a,(const char *)r->reply.data,r->reply.size)};
        ok=journal_record(fd,form(&a,"receipt-from-v2",parts,4),false,size);
        arena_free(&a);
    }
    if (ok) {
        uint64_t spaces=0,rows=0;
        for (size_t i=0;i<STORES;++i) if (s->stores[i]) {
            ++spaces; rows+=space_length64(s->stores[i]->space);
        }
        uint64_t tickets=ticket_total(s),grants=grant_total(s);
        if (tickets>INT64_MAX || grants>INT64_MAX) return false;
        arena_init_detached(&a);
        Atom *parts[]={atom_int(&a,spaces),atom_int(&a,rows),atom_int(&a,s->receipt_count),
                      atom_int(&a,s->decision_count),atom_int(&a,tickets),
                      atom_int(&a,grants),atom_int(&a,s->principal_count),
                      atom_int(&a,s->principal_generation)};
        ok=journal_record(fd,form(&a,"sealed-v6",parts,8),false,size);
        arena_free(&a);
    }
    return ok;
}
static bool checkpoint_records(int fd,uint64_t *size,void *context) {
    return write_checkpoint(context,fd,size);
}
static bool compact(Service *s) {
    /* Rebuild ownership from live terms. Query rows are independently owned,
     * so removing the old universe releases removed terms and old frontiers. */
    for (size_t i=0;i<STORES;++i) {
        Store *old=s->stores[i]; if (!old || !old->churn) continue;
        Store *fresh=store_new(old->id,old->name,old->engine,old->ordered,old->revision);
        if (!fresh) return false;
        bool ok=true;
        CettaCount count=space_length64(old->space);
        for (CettaIndex row=0;ok && row<count;++row) {
            Arena a; arena_init_detached(&a); Bytes b={0}; Atom *value=NULL;
            ok=wire(space_get_at64(old->space,row),&b) && decode(&a,b,&value) && store_add(fresh,value);
            bytes_free(&b); arena_free(&a);
        }
        if (!ok) { store_free(fresh); return false; }
        fresh->grants=old->grants; fresh->grant_count=old->grant_count; fresh->grant_capacity=old->grant_capacity;
        fresh->grant_slots=old->grant_slots; fresh->grant_slot_capacity=old->grant_slot_capacity;
        fresh->grant_generation=old->grant_generation;
        fresh->grant_clock_restored=old->grant_clock_restored;
        fresh->tickets=old->tickets; fresh->ticket_count=old->ticket_count;
        fresh->ticket_capacity=old->ticket_capacity;
        fresh->ticket_generation=old->ticket_generation;
        fresh->ticket_clock_restored=old->ticket_clock_restored;
        old->grants=NULL; old->grant_count=old->grant_capacity=0;
        old->grant_slots=NULL; old->grant_slot_capacity=0;
        old->tickets=NULL; old->ticket_count=old->ticket_capacity=0;
        fresh->churn=0; s->stores[i]=fresh; store_free(old);
    }
    uint64_t size=0;
    bool ok=cetta_journal_checkpoint(s->directory,"journal.csl","checkpoint.new",
        checkpoint_records,s,&s->journal,&size)==CETTA_JOURNAL_OK;
    if (ok) { s->journal_bytes=size; s->checkpoint_bytes=size; }
    return ok;
}
static void client_close(Service *s,Client *c) {
    if (c->watching) --s->watchers;
    observation_free(s,&c->watched);
    if (c->fd>=0) close(c->fd);
    bytes_free(&c->input); bytes_free(&c->output);
    *c=(Client){.fd=-1};
}
static bool client_reply(Service *s,Client *c,Arena *a,Atom *request,Atom *answer) {
    Bytes b={0}; bool ok=answer && wire(answer,&b);
    if (!ok && answer && !s->poisoned) ok=wire(error(a,request,"reply-limit"),&b);
    if (ok) {
        c->output.size=b.size+8; c->output.data=malloc(c->output.size);
        if (!c->output.data) ok=false;
        else {
            memcpy(c->output.data,"CSP1",4);
            for (size_t i=0;i<4;++i) c->output.data[4+i]=(unsigned char)(b.size>>(24-8*i));
            memcpy(c->output.data+8,b.data,b.size);
            c->deadline=now_ms()+30000;
        }
    }
    bytes_free(&b); return ok;
}
static bool respond(Service *s,Client *c) {
    Arena a; arena_init_detached(&a); Atom *request=NULL;
    bool ok=decode(&a,c->input,&request);
    Atom *answer=ok ? dispatch(s,c,&a,request,c->input) : NULL;
    if (c->watching) ok=true;
    else ok=client_reply(s,c,&a,request,answer);
    arena_free(&a); return ok;
}
static bool client_wake(Service *s,Client *c) {
    if (!c->watching) return true;
    Store *st=client_principal_current(s,c) ? find_store(s,c->watch_source) : NULL;
    /* Losing or replacing the exact grant ends a wait as if the store were
     * gone. A revoke followed by a regrant cannot revive retained work.
     * Scoped waits reveal matching data, so they retain a read requirement. */
    unsigned required=c->watched.id ? RIGHT_READ : RIGHT_OBSERVE;
    AuthorityView current=st ? authority_at(s,c,st,c->watch_authority_grant,
        c->watch_authority_generation) : (AuthorityView){0};
    if (st && (!(current.rights & required) ||
        current.grant!=c->watch_authority_grant ||
        current.generation!=c->watch_authority_generation)) st=NULL;
    bool changed=st && st->revision!=c->watch_revision;
    const char *failure=NULL;
    if (st && c->watched.id) {
        changed=false;
        if (c->watch_dirty) {
            uint64_t deadline=now_ms()+s->query_ms;
            for (size_t i=0;!failure && i<c->watched.reads+c->watched.writes;++i)
                failure=scope_current(s,st,&c->watched.views[i],deadline);
            changed=failure && !strcmp(failure,"scope-conflict");
            if (changed) failure=NULL;
            c->watch_dirty=false; c->watch_revision=st->revision;
        }
    }
    if (st && !changed && !failure && now_ms()<c->watch_due) return true;
    Arena a; arena_init_detached(&a); Atom *request=NULL,*answer=NULL;
    bool ok=decode(&a,c->input,&request);
    if (ok && !st) answer=error(&a,request,"missing-space");
    else if (ok && failure) answer=error(&a,request,failure);
    else if (ok) {
        Atom *args[]={atom_string(&a,s->uuid),atom_int(&a,st->id),atom_int(&a,st->revision)};
        answer=reply(&a,request,true,form(&a,changed ? "changed" : "unchanged",args,3));
    }
    c->watching=false; --s->watchers;
    observation_free(s,&c->watched);
    ok=ok && client_reply(s,c,&a,request,answer);
    arena_free(&a); return ok;
}
static bool client_read(Service *s,Client *c) {
    while (c->header_read<8) {
        ssize_t n=recv(c->fd,c->header+c->header_read,8-c->header_read,0);
        if (n>0) c->header_read+=(size_t)n;
        else if (n<0 && errno==EINTR) continue;
        else return n<0 && (errno==EAGAIN || errno==EWOULDBLOCK);
    }
    if (!c->input.data) {
        if (memcmp(c->header,"CSP1",4)) return false;
        size_t size=0; for (size_t i=4;i<8;++i) size=(size<<8)|c->header[i];
        if (size<4 || size>CETTA_ATOM_WIRE_BYTES) return false;
        c->input=(Bytes){malloc(size),size}; if (!c->input.data) return false;
    }
    while (c->input_read<c->input.size) {
        ssize_t n=recv(c->fd,c->input.data+c->input_read,c->input.size-c->input_read,0);
        if (n>0) c->input_read+=(size_t)n;
        else if (n<0 && errno==EINTR) continue;
        else return n<0 && (errno==EAGAIN || errno==EWOULDBLOCK);
    }
    return respond(s,c);
}
static bool client_write(Client *c) {
    while (c->output_sent<c->output.size) {
        ssize_t n=send(c->fd,c->output.data+c->output_sent,c->output.size-c->output_sent,MSG_NOSIGNAL);
        if (n>0) c->output_sent+=(size_t)n;
        else if (n<0 && errno==EINTR) continue;
        else return n<0 && (errno==EAGAIN || errno==EWOULDBLOCK);
    }
    bytes_free(&c->input); bytes_free(&c->output);
    c->header_read=c->input_read=c->output_sent=0; c->deadline=now_ms()+30000;
    return true;
}
static int listen_socket(const char *path, mode_t mode, gid_t group) {
    struct sockaddr_un address={.sun_family=AF_UNIX};
    size_t n=strlen(path);
    if (path[0]!='/' || n>=sizeof(address.sun_path)) return -1;
    memcpy(address.sun_path,path,n+1);
    struct stat st;
    if (!lstat(path,&st)) {
        if (!S_ISSOCK(st.st_mode) || st.st_uid!=geteuid()) return -1;
        int probe=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
        if (probe<0) return -1;
        int result=connect(probe,(struct sockaddr *)&address,sizeof(address));
        int saved=errno; close(probe);
        if (!result || (saved!=ECONNREFUSED && saved!=ENOENT)) return -1;
        if (unlink(path)) return -1;
    } else if (errno!=ENOENT) return -1;
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
    if (fd<0) return -1;
    if (bind(fd,(struct sockaddr *)&address,sizeof(address)) ||
        (group != (gid_t)-1 && chown(path,(uid_t)-1,group)) || chmod(path,mode) || listen(fd,CLIENTS)) {
        close(fd); return -1;
    }
    return fd;
}
/* Identity is the kernel's record of the connecting process: never the
 * listener it arrived on, a name, or anything the peer sends. */
static void client_accept(Service *s,Client *clients,int listener) {
    int fd=accept4(listener,NULL,NULL,SOCK_CLOEXEC|SOCK_NONBLOCK);
    if (fd<0) return;
    size_t slot=CLIENTS;
    for (size_t i=0;i<CLIENTS;++i) if (clients[i].fd<0) {slot=i;break;}
    struct ucred peer; socklen_t length=sizeof(peer);
    if (slot==CLIENTS || getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&length) || length!=sizeof(peer)) {
        close(fd); return;
    }
    Client *c=&clients[slot];
    bool owner=peer.uid==s->owner_uid;
    Principal principal=owner ? (Principal){.uid=(uint32_t)peer.uid} :
        current_principal(s,(uint32_t)peer.uid);
    *c=(Client){.fd=fd,.deadline=now_ms()+30000,.principal=principal,.owner=owner};
    /* A login name is only for readers, so keep it only when it is a plain token. */
    struct passwd entry,*found=NULL; char buffer[2048];
    if (!getpwuid_r(peer.uid,&entry,buffer,sizeof(buffer),&found) && found &&
        strlen(found->pw_name)<sizeof(c->account) &&
        strspn(found->pw_name,"abcdefghijklmnopqrstuvwxyz0123456789_-")==strlen(found->pw_name) &&
        found->pw_name[0]>='a' && found->pw_name[0]<='z')
        snprintf(c->account,sizeof(c->account),"%s",found->pw_name);
}
static bool directory_open(Service *s,const char *path) {
    if (mkdir(path,0700) && errno!=EEXIST) return false;
    s->directory=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if (s->directory<0) return false;
    struct stat st;
    if (fstat(s->directory,&st) || st.st_uid!=geteuid() || (st.st_mode&077)) return false;
    s->lock=openat(s->directory,"writer.lock",O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    if (s->lock<0 || flock(s->lock,LOCK_EX|LOCK_NB)) return false;
    s->journal=openat(s->directory,"journal.csl",O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    return s->journal>=0;
}
static bool argument_number(const char *text,uint64_t *value) {
    char *end=NULL; errno=0;
    unsigned long long n=strtoull(text,&end,10);
    if (!*text || text[0]=='-' || errno || *end || !n || n>INT64_MAX) return false;
    *value=(uint64_t)n; return true;
}
int main(int argc,char **argv) {
    const char *path=NULL,*directory=NULL,*shared_path=NULL,*shared_group=NULL;
    Service *s=calloc(1,sizeof(*s)); if (!s) return 1;
    s->journal=s->directory=s->lock=-1;
    s->next_cursor=1;
    s->process_uid=s->owner_uid=(uint32_t)geteuid();
    s->max_facts=1000000; s->max_live_bytes=64*1024*1024;
    s->max_query_bytes=QUERY_BYTES; s->max_snapshot_bytes=SNAPSHOT_BYTES;
    s->max_journal_bytes=32*1024*1024; s->query_ms=5000;
    s->max_watchers=CLIENTS/2;
    for (int i=1;i<argc;++i) {
        if (!strcmp(argv[i],"--help")) {
            puts("cetta-space-service --socket ABSOLUTE_PATH --data PRIVATE_DIRECTORY\n"
                 "  [--shared-socket ABSOLUTE_PATH --shared-group GROUP] [--owner-uid N]\n"
                 "  [--max-facts N] [--max-live-bytes N] [--max-journal-bytes N] [--query-ms N]\n"
                 "  [--max-query-bytes N] [--max-snapshot-bytes N] [--max-watchers N]\n"
                 "  [--max-account-view-bytes N] [--max-account-views N] [--max-account-watchers N]\n"
                 "  [--max-decisions N] (default: unlimited, refusal instead of eviction)\n"
                 "  [--max-tickets N] (default: unlimited, refusal instead of reuse)\n"
                 "  [--max-grants N] (default: unlimited, immutable history is retained)\n"
                 "  [--max-principals N] (default: unlimited, one current binding per uid)\n"
                 "The owner (default: this account) holds every right; other accounts reach\n"
                 "the shared socket and hold only what the owner grants them.");
            free(s); return 0;
        }
        if (i+1>=argc) goto invalid;
        const char *option=argv[i],*value=argv[++i];
        if (!strcmp(option,"--socket")) path=value;
        else if (!strcmp(option,"--data")) directory=value;
        else if (!strcmp(option,"--shared-socket")) shared_path=value;
        else if (!strcmp(option,"--shared-group")) shared_group=value;
        else {
            uint64_t n; if (!argument_number(value,&n)) goto invalid;
            if (!strcmp(option,"--owner-uid") && n<UINT32_MAX) s->owner_uid=(uint32_t)n;
            else if (!strcmp(option,"--max-facts")) s->max_facts=n;
            else if (!strcmp(option,"--max-live-bytes")) s->max_live_bytes=n;
            else if (!strcmp(option,"--max-journal-bytes")) s->max_journal_bytes=n;
            else if (!strcmp(option,"--max-query-bytes")) s->max_query_bytes=n;
            else if (!strcmp(option,"--max-snapshot-bytes")) s->max_snapshot_bytes=n;
            else if (!strcmp(option,"--max-account-view-bytes")) s->max_account_view_bytes=n;
            else if (!strcmp(option,"--max-account-views") && n<=SNAPSHOTS*2+CLIENTS) s->max_account_views=(size_t)n;
            else if (!strcmp(option,"--max-account-watchers") && n<CLIENTS) s->max_account_watchers=(size_t)n;
            else if (!strcmp(option,"--max-decisions")) s->max_decisions=n;
            else if (!strcmp(option,"--max-tickets")) s->max_tickets=n;
            else if (!strcmp(option,"--max-grants")) s->max_grants=n;
            else if (!strcmp(option,"--max-principals")) s->max_principals=n;
            else if (!strcmp(option,"--query-ms") && n<=60000) s->query_ms=(unsigned)n;
            else if (!strcmp(option,"--max-watchers") && n<CLIENTS) s->max_watchers=(size_t)n;
            else goto invalid;
        }
    }
    if (!path || !directory || directory[0]!='/' || !shared_path != !shared_group) goto invalid;
    s->shared_profile=shared_path!=NULL;
    umask(0077);
    SymbolTable symbols; symbol_table_init(&symbols); g_symbols=&symbols;
    symbol_table_init_builtins(&symbols,&g_builtin_syms);
    int listener=-1, shared=-1, result=1;
    struct stat socket_identity={0}, shared_identity={0};
    if (!random_id(s->boot) || !directory_open(s,directory) || !recover(s)) {
        fprintf(stderr,"space-service: storage unavailable or corrupt\n"); goto done;
    }
    listener=listen_socket(path,0600,(gid_t)-1);
    if (listener<0) { fprintf(stderr,"space-service: socket unavailable\n"); goto done; }
    if (lstat(path,&socket_identity)) goto done;
    if (shared_path) {
        struct group entry,*found=NULL; char buffer[4096];
        if (getgrnam_r(shared_group,&entry,buffer,sizeof(buffer),&found) || !found) {
            fprintf(stderr,"space-service: unknown shared group\n"); goto done;
        }
        shared=listen_socket(shared_path,0660,found->gr_gid);
        if (shared<0 || lstat(shared_path,&shared_identity)) {
            fprintf(stderr,"space-service: shared socket unavailable\n"); goto done;
        }
    }
    struct sigaction action={.sa_handler=stop}; sigemptyset(&action.sa_mask);
    sigaction(SIGINT,&action,NULL); sigaction(SIGTERM,&action,NULL);
    signal(SIGPIPE,SIG_IGN);
    /* Check the stop flag with these signals blocked, then unblock them
     * atomically in ppoll. A stop between the check and poll must not wait
     * for the ordinary 30-second idle deadline. */
    sigset_t stop_signals, wait_signals;
    sigemptyset(&stop_signals);
    sigaddset(&stop_signals,SIGINT); sigaddset(&stop_signals,SIGTERM);
    if (sigprocmask(SIG_BLOCK,&stop_signals,&wait_signals)) goto done;
    Client clients[CLIENTS]; for (size_t i=0;i<CLIENTS;++i) clients[i]=(Client){.fd=-1};
    s->clients=clients;
    fprintf(stderr,"space-service: ready\n");
    while (!stopping && !s->poisoned) {
        /* poll ignores the negative descriptor of an absent shared listener. */
        struct pollfd pollfds[CLIENTS+2]={{listener,POLLIN,0},{shared,POLLIN,0}};
        uint64_t current=now_ms(), deadline=UINT64_MAX;
        for (size_t i=0;i<CLIENTS;++i) {
            Client *c=&clients[i];
            if (c->fd>=0 && !client_wake(s,c)) client_close(s,c);
            pollfds[i+2]=(struct pollfd){c->fd,c->output.data ? POLLOUT : POLLIN,0};
            if (c->fd>=0 && c->deadline<deadline) deadline=c->deadline;
            if (c->watching && c->watch_due<deadline) deadline=c->watch_due;
        }
        for (size_t i=0;i<SNAPSHOTS;++i)
            if (s->snapshots[i].id && s->snapshots[i].expires<deadline) deadline=s->snapshots[i].expires;
        for (size_t i=0;i<SNAPSHOTS;++i)
            if (s->observations[i].id && s->observations[i].expires<deadline) deadline=s->observations[i].expires;
        for (size_t i=0;i<RECEIPTS;++i)
            if (s->cancelled[i].until && s->cancelled[i].until<deadline) deadline=s->cancelled[i].until;
        current=now_ms();
        uint64_t timeout=deadline>current ? deadline-current : 0;
        struct timespec wait_time={(time_t)(timeout/1000),(long)(timeout%1000)*1000000L};
        int n=ppoll(pollfds,CLIENTS+2,deadline==UINT64_MAX ? NULL : &wait_time,&wait_signals);
        if (n<0 && errno==EINTR) continue;
        if (n<0) { s->poisoned=true; break; }
        if (pollfds[0].revents&POLLIN) client_accept(s,clients,listener);
        if (pollfds[1].revents&POLLIN) client_accept(s,clients,shared);
        for (size_t i=0;i<CLIENTS && !s->poisoned;++i) {
            Client *c=&clients[i]; if (c->fd<0) continue;
            short events=pollfds[i+2].revents;
            bool ok=true;
            if (events&POLLOUT) ok=client_write(c);
            else if (events&POLLIN) {
                if (c->watching) {
                    /* A watch occupies this RPC connection until its reply.
                     * Detect a closed peer and refuse pipelined requests. */
                    char byte; ssize_t got=recv(c->fd,&byte,1,MSG_PEEK);
                    ok=got<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR);
                } else ok=client_read(s,c);
            }
            if (!ok || (events&(POLLERR|POLLNVAL)) ||
                ((events&POLLHUP) && !(events&POLLIN)) || now_ms()>=c->deadline) client_close(s,c);
        }
        expire(s);
        if (!s->poisoned && s->journal_bytes-s->checkpoint_bytes>s->max_journal_bytes && !compact(s))
            s->poisoned=true;
    }
    for (size_t i=0;i<CLIENTS;++i) client_close(s,&clients[i]);
    (void)sigprocmask(SIG_SETMASK,&wait_signals,NULL);
    struct stat current;
    if (!lstat(path,&current) && current.st_dev==socket_identity.st_dev &&
        current.st_ino==socket_identity.st_ino) unlink(path);
    if (shared_path && !lstat(shared_path,&current) && current.st_dev==shared_identity.st_dev &&
        current.st_ino==shared_identity.st_ino) unlink(shared_path);
    result=s->poisoned ? 1 : 0;
    if (s->poisoned) fprintf(stderr,"space-service: publication or checkpoint failed; recovery required\n");
done:
    if (listener>=0) close(listener);
    if (shared>=0) close(shared);
    for (size_t i=0;i<STORES;++i) store_free(s->stores[i]);
    for (size_t i=0;i<SNAPSHOTS;++i) snapshot_free(s,&s->snapshots[i]);
    for (size_t i=0;i<SNAPSHOTS;++i) observation_free(s,&s->observations[i]);
    for (size_t i=0;i<s->receipt_count;++i) receipt_free(&s->receipts[i]);
    free(s->decisions); free(s->decision_slots);
    free(s->principals); free(s->principal_slots);
    if (s->journal>=0) close(s->journal);
    if (s->lock>=0) close(s->lock);
    if (s->directory>=0) close(s->directory);
    symbol_table_free(&symbols); free(s); return result;
invalid:
    fprintf(stderr,"space-service: invalid arguments; use --help\n"); free(s); return 2;
}
