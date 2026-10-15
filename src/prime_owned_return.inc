static void prime_native_return_carrier_retain(void *raw) {
    PrimeNativeReturnCarrier *carrier = raw;
    size_t count = atomic_load_explicit(&carrier->references, memory_order_relaxed);
    do {
        if (!count || count == SIZE_MAX) abort();
    } while (!atomic_compare_exchange_weak_explicit(&carrier->references,
        &count, count + 1u, memory_order_relaxed, memory_order_relaxed));
}

static void prime_native_return_carrier_release(void *raw) {
    PrimeNativeReturnCarrier *carrier = raw;
    size_t count = atomic_fetch_sub_explicit(&carrier->references, 1u, memory_order_acq_rel);
    if (!count) abort();
    if (count != 1u) return;
    /* The origin may itself own an older carrier. Retire that exact edge
     * before dropping the final use; no destructor runs under the registry
     * mutex, and a detached owner never follows its old enclosing context. */
    arena_free(&carrier->value_storage);
    arena_free(&carrier->origin_storage);
    if (carrier->lifetime_ready) {
        bool released = cetta_native_handle_use_release(&carrier->lifetime);
        assert(released); (void)released;
    }
    free(carrier);
}

static void prime_native_return_carrier_storage(const void *raw,
    CettaStorageCensus *census) {
    const PrimeNativeReturnCarrier *carrier = raw;
    if (!carrier || !cetta_storage_object(census, carrier, sizeof(*carrier), CETTA_STORAGE_BLOCK))
        return;
    arena_storage_census(&carrier->origin_storage, census);
    arena_storage_census(&carrier->value_storage, census);
    if (carrier->lifetime_ready) cetta_native_handle_use_storage(&carrier->lifetime, census);
}

static void prime_native_acquired_operation_storage(const PrimeNativeAcquiredOperation *operation,
    CettaStorageCensus *census) {
    if (!operation || !cetta_storage_object(census, operation, sizeof(*operation), CETTA_STORAGE_BLOCK))
        return;
    cetta_native_handle_use_storage(&operation->use, census);
    prime_native_return_carrier_storage(operation->carrier, census);
    cetta_storage_array(census, operation->value_items, operation->value_capacity,
        sizeof(*operation->value_items), 0u);
    atom_deep_copy_session_storage_census(operation->value_copy, census);
    if (operation->pending_ready) outcome_set_storage_census(&operation->pending, census);
}

static void prime_native_acquired_operation_free(PrimeNativeAcquiredOperation *operation) {
    if (!operation) return;
    PrimeNativeControl *control = cetta_native_handle_use_resource(&operation->use);
    PrimeNativeRegionScope scope = {0};
    bool selected = prime_native_region_scope_enter(
        control ? control->owned_top_episode : NULL, &scope);
    assert(selected); (void)selected;
    if (operation->pending_ready) outcome_set_free(&operation->pending);
    atom_deep_copy_session_free(operation->value_copy);
    free(operation->value_items);
    if (operation->carrier) prime_native_return_carrier_release(operation->carrier);
    operation->carrier = NULL;
    if (control && operation->owns_lease) control->leased = false;
    prime_native_region_scope_leave(&scope);
    if (control) {
        bool released = cetta_native_handle_use_release(&operation->use);
        assert(released); (void)released;
    }
    free(operation);
}

static PrimeNativeAcquiredOperation *prime_native_acquired_operation_adopt(
    CettaNativeHandleUse *held, Atom *origin, Arena *receiver, uint64_t allowance) {
    if (!held || !cetta_native_handle_use_resource(held) || !origin || !receiver) return NULL;
    PrimeNativeAcquiredOperation *operation = calloc(1u, sizeof(*operation));
    if (!operation) return NULL;
    PrimeNativeReturnCarrier *carrier = calloc(1u, sizeof(*carrier));
    if (!carrier) { free(operation); return NULL; }
    atomic_init(&carrier->references, 1u);
    arena_init_detached(&carrier->origin_storage);
    arena_init_detached(&carrier->value_storage);
    arena_set_scalar_cache(&carrier->value_storage, false);
    carrier->origin = atom_deep_copy(&carrier->origin_storage, origin);
    operation->carrier = carrier;
    operation->receiver = receiver;
    operation->remaining = allowance;
    PrimeNativeControl *control = cetta_native_handle_use_resource(held);
    operation->output_first = operation->value_cursor = control->published;
    operation->accept_cursor = control->published;
    operation->delivered_first = control->delivered_count;
    outcome_set_init_with_owner(&operation->pending, receiver);
    operation->pending_ready = true;
    /* The caller acquired this exact origin and keeps the original Atom
     * live through its complete copy. Move repairs the receipt's intrusive
     * neighbors; it does not renew the public slot after a racing close. */
    if (!carrier->origin || !cetta_native_handle_use_move(&operation->use,held)) {
        prime_native_acquired_operation_free(operation);
        return NULL;
    }
    return operation;
}

static PrimeNativeAcquiredOperation *prime_native_acquired_operation_new(
    CettaLibraryContext *authority, Atom *origin, Arena *receiver, uint64_t allowance) {
    CettaNativeHandleUse held={0};
    if (!authority || !origin || !receiver || !cetta_native_handle_acquire_owned(authority,
            "prime-search",origin,CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&held)) return NULL;
    PrimeNativeAcquiredOperation *operation=prime_native_acquired_operation_adopt(
        &held,origin,receiver,allowance);
    if (!operation) {
        bool released=cetta_native_handle_use_release(&held);
        assert(released); (void)released;
    }
    return operation;
}

static Atom *prime_native_acquired_return_holder(PrimeNativeAcquiredOperation *operation) {
    if (!operation || !operation->carrier) return NULL;
    if (operation->holder) return operation->holder;
    PrimeNativeControl *control = cetta_native_handle_use_resource(&operation->use);
    if (!control || !prime_native_acquired_receiver_admitted(operation, control))
        return NULL;
    PrimeNativeReturnCarrier *carrier = operation->carrier;
    if (!carrier->lifetime_ready) {
        if (!cetta_native_handle_use_fork_lifetime(&carrier->lifetime, &operation->use))
            return NULL;
        carrier->lifetime_ready = true;
    }
    Atom *origin = carrier->origin;
    uint64_t id;
    if (!cetta_native_handle_arg(origin, "prime-search", &id) || id != control->handle_id)
        return NULL;
    if (!operation->identifier)
        operation->identifier = atom_native_handle_identifier_with_carrier(operation->receiver,
            origin->expr.elems[2], carrier, prime_native_return_carrier_retain,
            prime_native_return_carrier_release, prime_native_return_carrier_storage);
    if (!operation->identifier) return NULL;
    Atom *head = atom_deep_copy(operation->receiver, origin->expr.elems[0]);
    Atom *kind = atom_deep_copy(operation->receiver, origin->expr.elems[1]);
    if (!head || !kind) return NULL;
    operation->holder = atom_expr(operation->receiver,
        (Atom *[]){head, kind, operation->identifier}, 3u);
    return operation->holder;
}
