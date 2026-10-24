#include "weight_ledger.h"

#include <stdlib.h>

const CettaWeightFactor **cetta_weight_ledger_ordered(
    const CettaWeightLedger *ledger) {
    if (!ledger || ledger->length == 0u)
        return NULL;
    if (ledger->length > SIZE_MAX / sizeof(CettaWeightFactor *))
        return NULL;
    const CettaWeightFactor **items = cetta_malloc(
        ledger->length * sizeof(*items));
    const CettaWeightFactor *node = ledger->last;
    for (size_t index = ledger->length; index > 0u; index--) {
        if (!node) {
            free(items);
            return NULL;
        }
        items[index - 1u] = node;
        node = node->previous;
    }
    if (node) {
        free(items);
        return NULL;
    }
    return items;
}

static bool factor_equal(const CettaWeightFactor *left,
                         const CettaWeightFactor *right) {
    return left->occurrence == right->occurrence &&
        left->dependency == right->dependency &&
        atom_eq(left->value, right->value);
}

uint64_t cetta_weight_ledger_handler(const CettaWeightLedger *ledger,
                                     uint64_t occurrence) {
    for (const CettaWeightClaim *claim = ledger ? ledger->claims : NULL;
         claim; claim = claim->previous)
        if (claim->occurrence == occurrence)
            return claim->handler;
    return 0u;
}

bool cetta_weight_ledger_claims_conflict(const CettaWeightLedger *left,
                                         const CettaWeightLedger *right) {
    if (!left || !right || !left->claims || !right->claims)
        return false;
    /* Immutable shared claims denote the same ownership map, even when
     * either production path has gained fresh, still-unclaimed factors. */
    if (left->claims == right->claims)
        return false;
    for (const CettaWeightClaim *claim = left->claims; claim; claim = claim->previous) {
        uint64_t handler = cetta_weight_ledger_handler(right, claim->occurrence);
        if (handler && handler != claim->handler)
            return true;
    }
    return false;
}

static bool ordered_extends(const CettaWeightLedger *base,
    const CettaWeightLedger *prefix, const CettaWeightFactor **items,
    const CettaWeightFactor **before) {
    if (prefix->length > base->length || (base->length && !items) ||
        (prefix->length && !before))
        return false;
    for (size_t i = 0u; i < prefix->length; i++)
        if (!factor_equal(items[i], before[i]))
            return false;
    for (const CettaWeightClaim *claim = prefix->claims; claim; claim = claim->previous)
        if (cetta_weight_ledger_handler(base, claim->occurrence) != claim->handler)
            return false;
    return true;
}

bool cetta_weight_ledger_extends(const CettaWeightLedger *base,
                                  const CettaWeightLedger *prefix) {
    if (!base || !prefix || prefix->length > base->length)
        return false;
    const CettaWeightFactor **items = cetta_weight_ledger_ordered(base);
    const CettaWeightFactor **before = cetta_weight_ledger_ordered(prefix);
    bool ok = ordered_extends(base, prefix, items, before);
    free(items);
    free(before);
    return ok;
}

static bool claims_equal(const CettaWeightLedger *left,
                          const CettaWeightLedger *right) {
    if (left->claim_count != right->claim_count)
        return false;
    if (left->claims == right->claims)
        return true;
    for (const CettaWeightClaim *claim = left->claims; claim; claim = claim->previous)
        if (cetta_weight_ledger_handler(right, claim->occurrence) != claim->handler)
            return false;
    return true;
}

bool cetta_weight_ledger_equal(const CettaWeightLedger *left,
                               const CettaWeightLedger *right) {
    if (!left || !right || left->length != right->length || !claims_equal(left, right))
        return false;
    const CettaWeightFactor *a = left->last, *b = right->last;
    while (a && b) {
        if (a == b)
            return true;
        if (!factor_equal(a, b))
            return false;
        a = a->previous;
        b = b->previous;
    }
    return !a && !b;
}

static bool append_owned_with_session(CettaWeightLedger *ledger, Arena *owner,
    uint64_t occurrence, uint64_t dependency, Atom *value,
    AtomDeepCopySession *copy_session) {
    if (ledger->length == SIZE_MAX)
        return false;
    Atom *owned = copy_session
        ? atom_deep_copy_session_copy(copy_session, value)
        : atom_deep_copy(owner, value);
    if (!owned)
        return false;
    CettaWeightFactor *node = arena_alloc(owner, sizeof(*node));
    *node = (CettaWeightFactor){
        .previous = ledger->last,
        .occurrence = occurrence,
        .dependency = dependency,
        .value = owned,
    };
    ledger->last = node;
    ledger->length++;
    ledger->owner = owner;
    return true;
}

static bool append_owned(CettaWeightLedger *ledger, Arena *owner,
    uint64_t occurrence, uint64_t dependency, Atom *value) {
    return append_owned_with_session(ledger, owner, occurrence, dependency,
                                      value, NULL);
}

static bool claim_fresh(CettaWeightLedger *ledger, Arena *owner,
                        uint64_t occurrence, uint64_t handler) {
    if (!occurrence || !handler || ledger->claim_count == SIZE_MAX)
        return false;
    CettaWeightClaim *claim = arena_alloc(owner, sizeof(*claim));
    *claim = (CettaWeightClaim){ledger->claims, occurrence, handler};
    ledger->claims = claim;
    ledger->claim_count++;
    ledger->owner = owner;
    return true;
}

static bool claim_owned(CettaWeightLedger *ledger, Arena *owner,
                         uint64_t occurrence, uint64_t handler) {
    uint64_t known = cetta_weight_ledger_handler(ledger, occurrence);
    return known ? known == handler : claim_fresh(ledger, owner, occurrence, handler);
}

bool cetta_weight_ledger_promote(Arena *owner, CettaWeightLedger *ledger) {
    if (!owner || !ledger)
        return false;
    if (!ledger->length || ledger->owner == owner)
        return true;
    AtomDeepCopySession *session = atom_deep_copy_session_new(owner);
    if (!session)
        return false;
    bool ok = cetta_weight_ledger_promote_with_session(owner, session, ledger);
    atom_deep_copy_session_free(session);
    return ok;
}

bool cetta_weight_ledger_promote_with_session(
    Arena *owner, AtomDeepCopySession *copy_session, CettaWeightLedger *ledger) {
    if (!ledger || !atom_deep_copy_session_targets(copy_session, owner))
        return false;
    if (!ledger->length || ledger->owner == owner)
        return true;
    const CettaWeightFactor **items = cetta_weight_ledger_ordered(ledger);
    if (!items)
        return false;
    CettaWeightLedger promoted = {0};
    bool ok = true;
    for (size_t i = 0u; ok && i < ledger->length; i++)
        ok = append_owned_with_session(&promoted, owner, items[i]->occurrence,
                          items[i]->dependency, items[i]->value, copy_session);
    /* Claims are immutable map entries, but their retained path is part of
     * a checkpoint too. Copy in the same order instead of appending the
     * newest-first traversal as a reversed chain. */
    CettaWeightClaim *tail = NULL;
    for (const CettaWeightClaim *claim = ledger->claims; ok && claim;
         claim = claim->previous) {
        if (promoted.claim_count == ledger->claim_count ||
            !claim->occurrence || !claim->handler) {
            ok = false;
            break;
        }
        CettaWeightClaim *copy = arena_alloc(owner, sizeof(*copy));
        *copy = (CettaWeightClaim){NULL, claim->occurrence, claim->handler};
        if (tail)
            tail->previous = copy;
        else
            promoted.claims = copy;
        tail = copy;
        promoted.claim_count++;
    }
    ok = ok && promoted.claim_count == ledger->claim_count;
    free(items);
    if (ok)
        *ledger = promoted;
    return ok;
}

static const char weight_factor_copy_kind;
static const char weight_claim_copy_kind;

bool cetta_weight_ledger_copy_with_session(Arena *owner,
    AtomDeepCopySession *session, CettaWeightLedger *ledger) {
    if (!ledger || !atom_deep_copy_session_targets(session, owner)) return false;
    CettaWeightLedger image = *ledger;
    if (ledger->owner &&
        (!atom_deep_copy_session_destination_for_owner(session, ledger->owner,
            &image.owner) || image.owner == ledger->owner)) return false;
    if (!ledger->length) {
        if (ledger->last || ledger->claims || ledger->claim_count) return false;
        *ledger = image;
        return true;
    }
    if (!ledger->owner || ledger->claim_count > ledger->length ||
        ledger->claim_count > SIZE_MAX / sizeof(const CettaWeightClaim *)) return false;
    const CettaWeightFactor **factors = cetta_weight_ledger_ordered(ledger);
    const CettaWeightClaim **claims = ledger->claim_count
        ? malloc(ledger->claim_count * sizeof(*claims)) : NULL;
    bool ok = false;
    if (!factors || (ledger->claim_count && !claims)) goto done;
    const CettaWeightClaim *claim = ledger->claims;
    for (size_t i = ledger->claim_count; i > 0u; i--) {
        if (!claim || !claim->occurrence || !claim->handler) goto done;
        claims[i - 1u] = claim;
        claim = claim->previous;
    }
    if (claim) goto done;
    const CettaWeightFactor *previous = NULL;
    for (size_t i = 0u; i < ledger->length; i++) {
        const CettaWeightFactor *source = factors[i];
        const Arena *source_owner = NULL;
        Arena *image_owner = NULL;
        if (!source->occurrence || !source->value ||
            !atom_deep_copy_session_owner_of_span(session, source, sizeof(*source),
                ledger->owner, &source_owner, &image_owner) || source_owner == image_owner ||
            (source == ledger->last && source_owner != ledger->owner)) goto done;
        Atom *value = atom_deep_copy_session_copy(session, source->value);
        if (!value) goto done;
        CettaWeightFactor *copy = atom_deep_copy_session_object_forwarded(session,
            &weight_factor_copy_kind, source, sizeof(*source));
        if (copy) {
            if (!arena_owns_ptr(image_owner, copy) || copy->previous != previous ||
                copy->occurrence != source->occurrence || copy->dependency != source->dependency ||
                copy->value != value) goto done;
        } else {
            copy = arena_alloc(image_owner, sizeof(*copy));
            if (!copy) goto done;
            *copy = (CettaWeightFactor){previous,source->occurrence,source->dependency,value};
            if (!atom_deep_copy_session_object_record(session, &weight_factor_copy_kind,
                    source, copy, sizeof(*source))) goto done;
        }
        previous = copy;
    }
    image.last = previous;
    const CettaWeightClaim *previous_claim = NULL;
    for (size_t i = 0u; i < ledger->claim_count; i++) {
        const CettaWeightClaim *source = claims[i];
        const Arena *source_owner = NULL;
        Arena *image_owner = NULL;
        if (!atom_deep_copy_session_owner_of_span(session, source, sizeof(*source),
                ledger->owner, &source_owner, &image_owner) || source_owner == image_owner)
            goto done;
        CettaWeightClaim *copy = atom_deep_copy_session_object_forwarded(session,
            &weight_claim_copy_kind, source, sizeof(*source));
        if (copy) {
            if (!arena_owns_ptr(image_owner, copy) || copy->previous != previous_claim ||
                copy->occurrence != source->occurrence || copy->handler != source->handler)
                goto done;
        } else {
            copy = arena_alloc(image_owner, sizeof(*copy));
            if (!copy) goto done;
            *copy = (CettaWeightClaim){previous_claim,source->occurrence,source->handler};
            if (!atom_deep_copy_session_object_record(session, &weight_claim_copy_kind,
                    source, copy, sizeof(*source))) goto done;
        }
        previous_claim = copy;
    }
    image.claims = previous_claim;
    *ledger = image;
    ok = true;
done:
    free(factors);
    free(claims);
    return ok;
}

bool cetta_weight_ledger_append(Arena *owner, const CettaWeightLedger *base,
    uint64_t occurrence, uint64_t dependency, Atom *value,
    CettaWeightLedger *out) {
    if (!owner || !base || !out || !occurrence || !value)
        return false;
    for (const CettaWeightFactor *node = base->last; node; node = node->previous)
        if (node->occurrence == occurrence)
            return false;
    CettaWeightLedger next = *base;
    if (!cetta_weight_ledger_promote(owner, &next) ||
        !append_owned(&next, owner, occurrence, dependency, value))
        return false;
    *out = next;
    return true;
}

bool cetta_weight_ledger_handle(Arena *owner, const CettaWeightLedger *base,
    const CettaWeightLedger *prefix, uint64_t handler,
    CettaWeightLedger *out, CettaWeightLedger *selected) {
    if (!owner || !base || !prefix || !handler || !out || !selected ||
        out == selected || prefix->length > base->length)
        return false;
    const CettaWeightFactor **items = cetta_weight_ledger_ordered(base);
    const CettaWeightFactor **before = cetta_weight_ledger_ordered(prefix);
    bool ok = ordered_extends(base, prefix, items, before);
    CettaWeightLedger world = *base, factors = {0};
    if (ok)
        ok = cetta_weight_ledger_promote(owner, &world);
    if (!prefix->length && !base->claims && !base->claim_count) {
        /* Every admitted factor is selected once, in source order. The
         * immutable path is already owned here after promotion; the read
         * view shares it without sharing the world's new claim map. Its
         * unique occurrence IDs need no search through claims just added. */
        factors = (CettaWeightLedger){
            .last = world.last, .length = world.length, .owner = world.owner,
        };
        for (size_t i = 0u; ok && i < base->length; i++)
            ok = claim_fresh(&world, owner, items[i]->occurrence, handler);
    } else {
        for (size_t i = prefix->length; ok && i < base->length; i++) {
            const CettaWeightFactor *factor = items[i];
            if (cetta_weight_ledger_handler(&world, factor->occurrence))
                continue;
            ok = append_owned(&factors, owner, factor->occurrence,
                              factor->dependency, factor->value) &&
                claim_owned(&world, owner, factor->occurrence, handler);
        }
    }
    free(items);
    free(before);
    if (ok) {
        *out = world;
        *selected = factors;
    }
    return ok;
}

bool cetta_weight_ledger_merge(CettaWeightLedger *destination,
                               const CettaWeightLedger *right) {
    if (!destination || !right)
        return false;
    if (!right->length)
        return true;
    if (!destination->length) {
        *destination = *right;
        return true;
    }
    if (destination->last == right->last && destination->claims == right->claims)
        return destination->length == right->length;
    const CettaWeightFactor **left_items = cetta_weight_ledger_ordered(destination);
    const CettaWeightFactor **right_items = cetta_weight_ledger_ordered(right);
    if (!left_items || !right_items) {
        free(left_items);
        free(right_items);
        return false;
    }
    size_t shared = 0u;
    bool ok = true;
    while (shared < destination->length && shared < right->length &&
           left_items[shared]->occurrence == right_items[shared]->occurrence) {
        if (!factor_equal(left_items[shared], right_items[shared])) {
            ok = false;
            break;
        }
        shared++;
    }
    for (size_t i = shared; ok && i < destination->length; i++)
        for (size_t j = shared; ok && j < right->length; j++)
            ok = left_items[i]->occurrence != right_items[j]->occurrence;
    CettaWeightLedger merged = *destination;
    for (size_t j = shared; ok && j < right->length; j++)
        ok = append_owned(&merged, destination->owner,
                          right_items[j]->occurrence,
                          right_items[j]->dependency, right_items[j]->value);
    for (const CettaWeightClaim *claim = right->claims; ok && claim; claim = claim->previous)
        ok = claim_owned(&merged, destination->owner, claim->occurrence, claim->handler);
    free(left_items);
    free(right_items);
    if (ok)
        *destination = merged;
    return ok;
}

static size_t factor_position(const CettaWeightFactor *const *items,
    size_t count, uint64_t occurrence) {
    for (size_t i = 0u; i < count; i++)
        if (items[i]->occurrence == occurrence) return i;
    return count;
}

static bool captured_factor_equal(const CettaWeightFactor *left,
    const CettaWeightFactor *right) {
    return left->occurrence == right->occurrence &&
        left->dependency == right->dependency &&
        atom_capture_identical(left->value, right->value);
}

bool cetta_weight_ledger_import_suffix(Arena *owner,
    const CettaWeightLedger *entrance, const CettaWeightLedger *production,
    const CettaWeightLedger *recipient, CettaWeightLedger *out) {
    if (!owner || !entrance || !production || !recipient || !out ||
        entrance->length > production->length ||
        production->claim_count > SIZE_MAX / sizeof(CettaWeightClaim *)) return false;
    const CettaWeightFactor **before = cetta_weight_ledger_ordered(entrance);
    const CettaWeightFactor **earned = cetta_weight_ledger_ordered(production);
    const CettaWeightFactor **held = cetta_weight_ledger_ordered(recipient);
    const CettaWeightClaim **claims = production->claim_count
        ? malloc(production->claim_count * sizeof(*claims)) : NULL;
    bool valid = (!recipient->length || held) &&
        (!production->claim_count || claims) &&
        ordered_extends(production, entrance, earned, before);
    for (size_t i = 0u; valid && i < entrance->length; i++)
        valid = captured_factor_equal(before[i], earned[i]);

    size_t first_missing = production->length;
    size_t previous_position = 0u;
    bool reused = false;
    for (size_t i = entrance->length; valid && i < production->length; i++) {
        size_t position = factor_position(held, recipient->length, earned[i]->occurrence);
        if (position == recipient->length) {
            if (first_missing == production->length) first_missing = i;
        } else {
            valid = first_missing == production->length &&
                (!reused || position > previous_position) &&
                captured_factor_equal(earned[i], held[position]);
            previous_position = position;
            reused = true;
        }
    }
    size_t claim_count = 0u;
    for (const CettaWeightClaim *claim = production->claims; valid && claim;
         claim = claim->previous) {
        if (claim_count == production->claim_count || !claim->occurrence || !claim->handler) {
            valid = false;
            break;
        }
        claims[claim_count++] = claim;
        if (cetta_weight_ledger_handler(entrance, claim->occurrence) == claim->handler)
            continue;
        size_t position = factor_position(earned, production->length, claim->occurrence);
        size_t receiver = factor_position(held, recipient->length, claim->occurrence);
        uint64_t handler = cetta_weight_ledger_handler(recipient, claim->occurrence);
        valid = position < production->length && (!handler || handler == claim->handler) &&
            (position >= entrance->length ||
             (receiver < recipient->length && captured_factor_equal(earned[position], held[receiver])));
    }
    valid = valid && claim_count == production->claim_count;

    CettaWeightLedger imported = *recipient;
    if (valid) valid = cetta_weight_ledger_promote(owner, &imported);
    for (size_t i = first_missing; valid && i < production->length; i++)
        valid = append_owned(&imported, owner, earned[i]->occurrence,
            earned[i]->dependency, earned[i]->value);
    /* Retain the authored claim path order as well as its ownership map. */
    for (size_t i = claim_count; valid && i > 0u; i--) {
        const CettaWeightClaim *claim = claims[i - 1u];
        if (cetta_weight_ledger_handler(entrance, claim->occurrence) != claim->handler)
            valid = claim_owned(&imported, owner, claim->occurrence, claim->handler);
    }
    free(before); free(earned); free(held); free(claims);
    if (valid) *out = imported;
    return valid;
}

bool cetta_weight_ledger_compare(const CettaWeightLedger *left,
    const CettaWeightLedger *right, const CettaCheckpointCompare *read) {
    if (!left || !right || !cetta_checkpoint_compare_valid(read)) return false;
#define WC_S(n,a,b) do { if (!read->scalar(read->context,n,(uint64_t)(a),(uint64_t)(b))) return false; } while (0)
#define WC_P(n,a,b) do { if (!read->address(read->context,n,a,b)) return false; } while (0)
#define WC_I(n,r,a,b) do { if (!read->identity(read->context,n,r,(uint64_t)(a),(uint64_t)(b))) return false; } while (0)
    WC_S("weight.length",left->length,right->length);
    WC_S("weight.claim_count",left->claim_count,right->claim_count);
    if (left->length != right->length || left->claim_count != right->claim_count)
        return false;
    if(!read->arena(read->context,"weight.owner",left->owner,right->owner))return false;
    const CettaWeightFactor *a=left->last,*b=right->last;
    for (size_t i=0u;i<left->length;i++) {
        if (!a || !b) return false;
        WC_S("weight.factor_index",i,i);
        WC_P("weight.factor",a,b);
        WC_P("weight.previous",a->previous,b->previous);
        WC_I("weight.occurrence",CETTA_CHECKPOINT_WEIGHT_OCCURRENCE,a->occurrence,b->occurrence);
        WC_I("weight.dependency",CETTA_CHECKPOINT_SOURCE_OCCURRENCE,a->dependency,b->dependency);
        if (!read->atom(read->context,"weight.value",a->value,b->value)) return false;
        a=a->previous;b=b->previous;
    }
    if (a || b) return false;
    const CettaWeightClaim *x=left->claims,*y=right->claims;
    for (size_t i=0u;i<left->claim_count;i++) {
        if (!x || !y) return false;
        WC_S("weight.claim_index",i,i);
        WC_P("weight.claim",x,y);WC_P("weight.claim_previous",x->previous,y->previous);
        WC_I("weight.claim_occurrence",CETTA_CHECKPOINT_WEIGHT_OCCURRENCE,x->occurrence,y->occurrence);
        WC_I("weight.handler",CETTA_CHECKPOINT_WEIGHT_HANDLER,x->handler,y->handler);
        x=x->previous;y=y->previous;
    }
    return !x && !y;
#undef WC_S
#undef WC_P
#undef WC_I
}
