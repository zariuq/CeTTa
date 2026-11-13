/* This entry is added only to the private qualification source capture. */
static bool cesta_call_source_control_audit(const PrimeNeedCallSources *hub,
                                          const Arena *eval, const Arena *survivor) {
    PrimeNeedArenaAudit *first = prime_need_arena_audit_new(eval);
    PrimeNeedArenaAudit *second = prime_need_arena_audit_new(survivor);
    bool valid = first && second && prime_eval_stack_audit_call_hub(first, second, hub);
    prime_need_arena_audit_free(first);
    prime_need_arena_audit_free(second);
    return valid;
}
int cesta_call_source_controls(void) {
    Arena owners[3];
    for (unsigned i=0; i<3; ++i) arena_init_detached(&owners[i]);
    PrimeEvalStackDriver driver = {.arena=&owners[0]};
    PrimeEvalStackDriver *previous = g_prime_eval_stack_driver;
    g_prime_eval_stack_driver = &driver;
    PrimeNeedCallSourceCell cell = {0}, tail = {0};
    bindings_init(&cell.common);
    bindings_init(&tail.common);
    PrimeNeedCallSources hub = {.cells=&cell};
    Atom *atoms[3];
    int status=0;
    for (unsigned i=0; i<3; ++i) {
        atoms[i]=atom_int(&owners[i],INT64_C(12345678901));
        if (!atoms[i] || !arena_owns_atom(&owners[i],atoms[i])) {status=1;goto finish;}
    }
    cell.reference=atoms[2];
    if (!cesta_call_source_control_audit(&hub,&owners[0],&owners[1])) {status=2;goto finish;}
    for (unsigned i=0; i<3; ++i) {
        cell.reference=atoms[i];
        if (cesta_call_source_control_audit(&hub,&owners[0],&owners[1]) != (i==2)) {status=3;goto finish;}
    }
    cell.reference=atoms[2];
    for (unsigned i=0; i<3; ++i) {
        bindings_free(&cell.common);
        bindings_init(&cell.common);
        if (!bindings_add_id(&cell.common,var_id_from_base(1),SYMBOL_ID_NONE,atoms[i])) {status=4;goto finish;}
        if (cesta_call_source_control_audit(&hub,&owners[0],&owners[1]) != (i==2)) {status=5;goto finish;}
    }
    cell.next=&tail;
    tail.reference=atoms[0];
    if (cesta_call_source_control_audit(&hub,&owners[0],&owners[1])) {status=6;goto finish;}
    tail.reference=atoms[2];
    if (!cesta_call_source_control_audit(&hub,&owners[0],&owners[1])) {status=7;goto finish;}
finish:
    bindings_free(&tail.common);
    bindings_free(&cell.common);
    g_prime_eval_stack_driver=previous;
    for (unsigned i=0; i<3; ++i) arena_free(&owners[i]);
    return status;
}
