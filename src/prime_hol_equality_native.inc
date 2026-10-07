/* Structural HOL equality rules lower through the explicitly admitted
 * reflexivity and transport constants. The regular kernel checks the
 * resulting dependent term and its complete declaration context. */
static Atom *sj_native_equality_instance(SjNativeCompiler *compiler, Atom *carrier,
                                        Atom *left, Atom *right) {
    Atom *equation = sj_hol_equation(compiler->proof, carrier, left, right);
    Atom *code = equation ? sj_native_code(compiler, equation) : NULL;
    return code ? sj_native_code_instance(code, "eq", NULL) : NULL;
}

static Atom *sj_native_reflexivity(SjNativeCompiler *compiler, Atom *carrier,
                                  Atom *canonical_term, Atom *term_code) {
    Arena *a = compiler->proof->arena;
    Atom *instance = sj_native_equality_instance(compiler, carrier, canonical_term, canonical_term);
    Atom *name = sj_native_reflexivity_name(a, instance);
    if (!name || !term_code) return NULL;
    bool noted = false;
    for (size_t i = 0u; i < compiler->reflexive_count; i++)
        noted |= atom_eq(compiler->reflexive[i], instance);
    if (!noted) {
        if (!sj_grow_pair(a, &compiler->reflexive, NULL,
                          compiler->reflexive_count, &compiler->reflexive_cap)) {
            sj_fail(compiler->proof, false, sj_expr1(a, "set:native-equality-storage"));
            return NULL;
        }
        compiler->reflexive[compiler->reflexive_count++] = instance;
    }
    return sj_expr3(a, "App", sj_expr2(a, "DeclConst", name), term_code);
}

static Atom *sj_native_transport(SjNativeCompiler *compiler, Atom *instance,
                                 Atom *predicate, Atom *left, Atom *right,
                                 Atom *major, Atom *minor) {
    if (!predicate || !left || !right || !major || !minor ||
        !sj_native_note_substitution(compiler, instance)) return NULL;
    Arena *a = compiler->proof->arena;
    Atom *rule = sj_native_rule_name(compiler, SJ_NATIVE_EQ_SUBST, instance);
    if (!rule) return NULL;
    Atom *applied = sj_expr2(a, "DeclConst", rule);
    Atom *arguments[5] = {predicate, left, right, major, minor};
    for (size_t i = 0u; i < 5u; i++) applied = sj_expr3(a, "App", applied, arguments[i]);
    return applied;
}

static Atom *sj_native_kernel_equation(Arena *a, Atom *instance, Atom *left, Atom *right) {
    return sj_expr3(a, "App", sj_expr3(a, "App", sj_expr2(a, "DeclConst", instance), left), right);
}

static Atom *sj_native_compile_equality(SjNativeCompiler *compiler, Atom *proof,
                                        Atom **proposition_out) {
    SjProofState *st = compiler->proof;
    Arena *a = st->arena;
    SjHolEqualityParts parts;
    if (!sj_hol_equality_parts(st, proof, &parts)) return NULL;
    *proposition_out = parts.conclusion;
    Atom *left = sj_native_code(compiler, parts.left);
    Atom *right = sj_native_code(compiler, parts.right);
    if (!left || !right) return NULL;
    if (parts.kind == SJ_HOL_EQ_REFL || parts.kind == SJ_HOL_EQ_BETA)
        return sj_native_reflexivity(compiler, parts.carrier, parts.left, left);

    Atom *major_prop = NULL;
    Atom *major = sj_native_compile_synth(compiler, parts.first_proof, &major_prop);
    Atom *instance = sj_native_equality_instance(compiler, parts.carrier, parts.left, parts.right);
    Atom *carrier = sj_to_kernel(a, parts.carrier);
    if (!major || !instance || !carrier) return NULL;
    Atom *index = sj_expr2(a, "idx", atom_int(a, 0));
    Atom *predicate = NULL, *minor = NULL;
    if (parts.kind == SJ_HOL_EQ_SYMM) {
        predicate = sj_expr3(a, "Lam", carrier, sj_native_kernel_equation(a, instance,
            index, sj_shift(a, left, 0u, 1)));
        minor = sj_native_reflexivity(compiler, parts.carrier, parts.left, left);
        return minor ? sj_native_transport(compiler, instance, predicate, left, right, major, minor) : NULL;
    }
    if (parts.kind == SJ_HOL_EQ_TRANS) {
        Atom *last = sj_native_code(compiler, parts.argument);
        Atom *second_prop = NULL;
        Atom *second = sj_native_compile_synth(compiler, parts.second_proof, &second_prop);
        predicate = sj_expr3(a, "Lam", carrier, sj_native_kernel_equation(a, instance,
            sj_shift(a, left, 0u, 1), index));
        return last && second ? sj_native_transport(compiler, instance, predicate, right, last, second, major) : NULL;
    }
    if (parts.kind == SJ_HOL_EQ_APP || parts.kind == SJ_HOL_EQ_ARG) {
        Atom *first_value = parts.kind == SJ_HOL_EQ_APP
            ? sj_expr3(a, "App", parts.left, parts.argument)
            : sj_expr3(a, "App", parts.function, parts.left);
        Atom *first_code = sj_native_code(compiler, first_value);
        Atom *result_instance = sj_native_equality_instance(compiler, parts.codomain, first_value, first_value);
        Atom *argument = sj_native_code(compiler,
            parts.kind == SJ_HOL_EQ_APP ? parts.argument : parts.function);
        if (!first_code || !result_instance || !argument) return NULL;
        Atom *varying = parts.kind == SJ_HOL_EQ_APP
            ? sj_expr3(a, "App", index, sj_shift(a, argument, 0u, 1))
            : sj_expr3(a, "App", sj_shift(a, argument, 0u, 1), index);
        predicate = sj_expr3(a, "Lam", carrier, sj_native_kernel_equation(a, result_instance,
            sj_shift(a, first_code, 0u, 1), varying));
        minor = sj_native_reflexivity(compiler, parts.codomain, first_value, first_code);
        return minor ? sj_native_transport(compiler, instance, predicate, left, right, major, minor) : NULL;
    }
    /* Equality of propositions eliminates to their implication. The
     * equation proof is shifted past the new proof binder, not re-read as
     * a source hypothesis at the wrong position. */
    Atom *prop = sj_expr2(a, "DeclConst", sj_sym(a, "prop"));
    Atom *identity = sj_expr3(a, "Lam", prop, index);
    Atom *transport = sj_native_transport(compiler, instance, identity,
        sj_shift(a, left, 0u, 1), sj_shift(a, right, 0u, 1),
        sj_shift(a, major, 0u, 1), index);
    if (!transport) return NULL;
    Atom *holds_left = sj_expr3(a, "App", sj_expr2(a, "DeclConst", compiler->family_name), left);
    Atom *abstraction = sj_expr3(a, "Lam", holds_left, transport);
    if (!compiler->rule_constants) return abstraction;
    Atom *intro = sj_native_rule_name(compiler, SJ_NATIVE_IMP_INTRO, NULL);
    if (!intro) return NULL;
    return sj_expr3(a, "App", sj_expr3(a, "App", sj_expr3(a, "App",
        sj_expr2(a, "DeclConst", intro), left), right), abstraction);
}
