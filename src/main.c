#define _GNU_SOURCE
#include "atom.h"
#include "parser.h"
#include "space.h"
#include "eval.h"
#include "library.h"
#include "lang.h"
#include "compile.h"
#include "cetta_stdlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static char alt_stack_buf[16384];  /* alternate signal stack for SIGSEGV handler */

static void handle_sigsegv(int sig) {
    (void)sig;
    const char msg[] = "\nStack overflow. Use tail recursion or increase stack: ulimit -s unlimited\n";
    ssize_t r = write(STDERR_FILENO, msg, sizeof(msg) - 1);
    (void)r;
    _exit(2);
}

static bool g_count_only = false;

static void write_results(FILE *out, ResultSet *rs) {
    if (rs->len == 0) return;  /* HE prints nothing for empty result sets */
    if (g_count_only) {
        if (rs->len == 1 &&
            rs->items[0]->kind == ATOM_GROUNDED &&
            rs->items[0]->ground.gkind == GV_INT) {
            fprintf(out, "%lld\n", (long long)rs->items[0]->ground.ival);
            return;
        }
        fprintf(out, "%u\n", rs->len);
        return;
    }
    fprintf(out, "[");
    for (uint32_t i = 0; i < rs->len; i++) {
        if (i > 0) fprintf(out, ", ");
        atom_print(rs->items[i], out);
    }
    fprintf(out, "]\n");
}

static bool result_set_has_error(ResultSet *rs) {
    for (uint32_t i = 0; i < rs->len; i++) {
        if (atom_is_error(rs->items[i])) return true;
    }
    return false;
}

static void print_usage(FILE *out) {
    fputs("usage: cetta [--lang <name>] <file.metta>\n", out);
    fputs("       cetta [--profile <he_compat|he_extended|he_prime>] <file.metta>\n", out);
    fputs("       cetta --compile <file.metta>           # emit LLVM IR to stdout\n", out);
    fputs("       cetta --compile-stdlib <file.metta>     # emit precompiled stdlib blob to stdout\n", out);
    fputs("       cetta --count-only <file.metta>        # print result counts only\n", out);
    fputs("       cetta --fuel <n> <file.metta>          # override evaluator fuel budget\n", out);
    fputs("       cetta --space-match-backend <name> <file.metta>\n", out);
    fputs("       cetta --list-profiles\n", out);
    fputs("       cetta --list-space-match-backends\n", out);
    fputs("       cetta --list-languages\n", out);
}

static const char *find_profile_blocked_surface(const CettaProfile *profile, Atom *atom) {
    if (!profile || !atom) return NULL;
    if (atom->kind != ATOM_EXPR || atom->expr.len == 0) return NULL;

    Atom *head = atom->expr.elems[0];
    if (head->kind == ATOM_SYMBOL) {
        if (strcmp(head->name, "quote") == 0) {
            return NULL;
        }
        if (!cetta_profile_allows_surface(profile, head->name)) {
            return head->name;
        }
        if (strcmp(head->name, ":") == 0 && atom->expr.len >= 2 &&
            atom->expr.elems[1]->kind == ATOM_SYMBOL &&
            !cetta_profile_allows_surface(profile, atom->expr.elems[1]->name)) {
            return atom->expr.elems[1]->name;
        }
    }

    for (uint32_t i = 1; i < atom->expr.len; i++) {
        const char *blocked = find_profile_blocked_surface(profile, atom->expr.elems[i]);
        if (blocked) return blocked;
    }
    return NULL;
}

static bool compile_profile_guard_ok(const CettaProfile *profile, Atom **atoms, int n) {
    for (int i = 0; i < n; i++) {
        Atom *at = atoms[i];
        if (at->kind == ATOM_SYMBOL && strcmp(at->name, "!") == 0) {
            i++;
            continue;
        }
        const char *blocked = find_profile_blocked_surface(profile, at);
        if (blocked) {
            const CettaSurfacePolicy *policy = cetta_surface_policy_lookup(blocked);
            fprintf(stderr, "error: surface '%s' is unavailable in profile '%s'",
                    blocked, profile ? profile->name : "unknown");
            if (policy && policy->surface_classification) {
                fprintf(stderr, " (%s)", policy->surface_classification);
            }
            fputc('\n', stderr);
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv) {
    /* Install SIGSEGV handler on alternate stack so it works during stack overflow */
    {
        stack_t ss;
        ss.ss_sp = alt_stack_buf;
        ss.ss_size = sizeof(alt_stack_buf);
        ss.ss_flags = 0;
        sigaltstack(&ss, NULL);

        struct sigaction sa;
        sa.sa_handler = handle_sigsegv;
        sa.sa_flags = SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, NULL);
    }

    const char *lang_name = "he";
    const CettaProfile *profile = cetta_profile_he_extended();
    const char *filename = NULL;
    int script_arg_start = -1;
    bool compile_mode = false;
    bool compile_stdlib_mode = false;
    bool count_only = false;
    int fuel_override = -1;
    SpaceMatchBackendKind match_backend_kind = SPACE_MATCH_BACKEND_NATIVE;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list-languages") == 0) {
            cetta_language_print_inventory(stdout);
            return 0;
        }
        if (strcmp(argv[i], "--list-space-match-backends") == 0) {
            space_match_backend_print_inventory(stdout);
            return 0;
        }
        if (strcmp(argv[i], "--list-profiles") == 0) {
            cetta_profile_print_inventory(stdout);
            return 0;
        }
        if (strcmp(argv[i], "--compile") == 0) {
            compile_mode = true;
            continue;
        }
        if (strcmp(argv[i], "--compile-stdlib") == 0) {
            compile_stdlib_mode = true;
            continue;
        }
        if (strcmp(argv[i], "--count-only") == 0) {
            count_only = true;
            continue;
        }
        if (strcmp(argv[i], "--fuel") == 0) {
            char *endp = NULL;
            long parsed;
            if (i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            parsed = strtol(argv[++i], &endp, 10);
            if (!endp || *endp != '\0' || parsed <= 0 || parsed > 100000000L) {
                fprintf(stderr, "error: invalid fuel '%s'\n", argv[i]);
                return 2;
            }
            fuel_override = (int)parsed;
            continue;
        }
        if (strcmp(argv[i], "--space-match-backend") == 0) {
            if (i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            if (!space_match_backend_kind_from_name(argv[++i], &match_backend_kind)) {
                fprintf(stderr, "error: unknown space match backend '%s'\n", argv[i]);
                space_match_backend_print_inventory(stderr);
                return 2;
            }
            continue;
        }
        if (strcmp(argv[i], "--lang") == 0) {
            if (i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            lang_name = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--profile") == 0) {
            if (i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            profile = cetta_profile_from_name(argv[++i]);
            if (!profile) {
                fprintf(stderr, "error: unknown profile '%s'\n", argv[i]);
                cetta_profile_print_inventory(stderr);
                return 2;
            }
            continue;
        }
        if (!filename) {
            filename = argv[i];
            script_arg_start = i + 1;
            break;
        }
    }

    if (!filename) {
        print_usage(stderr);
        return 1;
    }

    /* --compile-stdlib: parse .metta file, emit C blob header, exit */
    if (compile_stdlib_mode) {
        Arena tmp_arena;
        arena_init(&tmp_arena);
        InternTable tmp_intern;
        intern_init(&tmp_intern);
        VarInternTable tmp_var_intern;
        var_intern_init(&tmp_var_intern);
        g_intern = &tmp_intern;
        g_var_intern = &tmp_var_intern;
        int rc = stdlib_compile(filename, &tmp_arena, stdout);
        arena_free(&tmp_arena);
        g_intern = NULL;
        g_var_intern = NULL;
        var_intern_free(&tmp_var_intern);
        intern_free(&tmp_intern);
        return rc < 0 ? 1 : 0;
    }

    const CettaLanguageSpec *lang = cetta_language_lookup(lang_name);
    if (!lang) {
        fprintf(stderr, "error: unknown language '%s'\n", lang_name);
        cetta_language_print_inventory(stderr);
        return 2;
    }
    if (!lang->implemented) {
        fprintf(
            stderr,
            "error: language '%s' is recognized but not implemented in cetta yet\n",
            lang_name
        );
        fprintf(stderr, "note: %s\n", lang->note);
        return 2;
    }

    g_count_only = count_only;
    if (fuel_override > 0) eval_set_default_fuel(fuel_override);

    Arena arena;       /* persistent: parsed atoms, space content, type decls */
    Arena eval_arena;  /* ephemeral: intermediate eval results, reset per ! */
    arena_init(&arena);
    arena_init(&eval_arena);

    /* Symbol interning for O(1) symbol comparison */
    InternTable intern_table;
    intern_init(&intern_table);
    g_intern = &intern_table;
    VarInternTable var_intern_table;
    var_intern_init(&var_intern_table);
    g_var_intern = &var_intern_table;

    /* Hash-consing for structural sharing (reduces memory for large derivations) */
    HashConsTable hashcons_table;
    hashcons_init(&hashcons_table);
    g_hashcons = &hashcons_table;

    Atom **atoms = NULL;
    int n = parse_metta_file(filename, &arena, &atoms);
    if (n < 0) {
        fprintf(stderr, "error: could not read %s\n", filename);
        arena_free(&eval_arena);
        arena_free(&arena);
        g_var_intern = NULL;
        var_intern_free(&var_intern_table);
        g_intern = NULL;
        intern_free(&intern_table);
        g_hashcons = NULL;
        hashcons_free(&hashcons_table);
        return 1;
    }

    Space space;
    space_init(&space);
    if (!space_match_backend_try_set(&space, match_backend_kind)) {
        fprintf(stderr, "error: space match backend '%s' is recognized but not implemented yet\n",
                space_match_backend_kind_name(match_backend_kind));
        free(atoms);
        arena_free(&eval_arena);
        arena_free(&arena);
        g_var_intern = NULL;
        var_intern_free(&var_intern_table);
        g_intern = NULL;
        intern_free(&intern_table);
        g_hashcons = NULL;
        hashcons_free(&hashcons_table);
        return 2;
    }

    Registry registry;
    registry_init(&registry);
    registry_bind(&registry, "&self", atom_space(&arena, &space));

    CettaLibraryContext libraries;
    cetta_library_context_init_with_profile(&libraries, profile);
    cetta_library_context_set_exec_path(&libraries, argv[0]);
    cetta_library_context_set_script_path(&libraries, filename);
    cetta_library_context_set_cli_args(&libraries, argc, argv, script_arg_start);
    eval_set_library_context(&libraries);

    /* Load precompiled stdlib equations into the space */
    stdlib_load(&space, &arena);

    /* Add grounded op type declarations (HE stdlib implicit types) */
    {
        Atom *num = atom_symbol(&arena, "Number");
        Atom *arrow_nnn = atom_expr(&arena, (Atom*[]){
            atom_symbol(&arena, "->"), num, num, num}, 4);
        Atom *bool_t = atom_symbol(&arena, "Bool");
        Atom *arrow_nnb = atom_expr(&arena, (Atom*[]){
            atom_symbol(&arena, "->"), num, num, bool_t}, 4);
        const char *arith_ops[] = {"+", "-", "*", "/", "%", NULL};
        const char *cmp_ops[] = {"<", ">", "<=", ">=", NULL};
        for (const char **op = arith_ops; *op; op++)
            space_add(&space, atom_expr3(&arena, atom_symbol(&arena, ":"),
                atom_symbol(&arena, *op), arrow_nnn));
        for (const char **op = cmp_ops; *op; op++)
            space_add(&space, atom_expr3(&arena, atom_symbol(&arena, ":"),
                atom_symbol(&arena, *op), arrow_nnb));
        /* (: = (-> $t $t %Undefined%)) — equality requires same type on both sides */
        Atom *t_var = atom_var(&arena, "t");
        Atom *arrow_eq = atom_expr(&arena, (Atom*[]){
            atom_symbol(&arena, "->"), t_var, t_var, atom_undefined_type(&arena)}, 4);
        space_add(&space, atom_expr3(&arena, atom_symbol(&arena, ":"),
            atom_symbol(&arena, "="), arrow_eq));
        /* (: == (-> $t $t Bool)) — structural equality */
        Atom *arrow_eqeq = atom_expr(&arena, (Atom*[]){
            atom_symbol(&arena, "->"), t_var, t_var, bool_t}, 4);
        space_add(&space, atom_expr3(&arena, atom_symbol(&arena, ":"),
            atom_symbol(&arena, "=="), arrow_eqeq));
    }

    /* Compile mode: load all atoms into space, emit LLVM IR, exit */
    if (compile_mode) {
        if (!compile_profile_guard_ok(profile, atoms, n)) {
            free(atoms);
            cetta_library_context_free(&libraries);
            space_free(&space);
            arena_free(&eval_arena);
            arena_free(&arena);
            g_var_intern = NULL;
            var_intern_free(&var_intern_table);
            g_intern = NULL;
            intern_free(&intern_table);
            g_hashcons = NULL;
            hashcons_free(&hashcons_table);
            return 2;
        }
        for (int pi = 0; pi < n; pi++) {
            Atom *at = atoms[pi];
            if (at->kind == ATOM_SYMBOL && strcmp(at->name, "!") == 0) {
                pi++;
                continue;
            }
            space_add(&space, at);
        }
        compile_space_to_llvm(&space, &arena, stdout);
        free(atoms);
        space_free(&space);
        arena_free(&eval_arena);
        arena_free(&arena);
        g_var_intern = NULL;
        var_intern_free(&var_intern_table);
        g_intern = NULL;
        intern_free(&intern_table);
        g_hashcons = NULL;
        hashcons_free(&hashcons_table);
        return 0;
    }

    /* Process top-level atoms */
    int i = 0;
    FILE *output_spool = tmpfile();
    if (!output_spool) {
        fprintf(stderr, "error: could not create output spool\n");
        free(atoms);
        cetta_library_context_free(&libraries);
        space_free(&space);
        arena_free(&eval_arena);
        arena_free(&arena);
        g_var_intern = NULL;
        var_intern_free(&var_intern_table);
        g_intern = NULL;
        intern_free(&intern_table);
        g_hashcons = NULL;
        hashcons_free(&hashcons_table);
        return 1;
    }
    while (i < n) {
        Atom *at = atoms[i];

        /* ! prefix → evaluate and print */
        if (at->kind == ATOM_SYMBOL && strcmp(at->name, "!") == 0 && i + 1 < n) {
            Atom *expr = atoms[i + 1];
            ResultSet rs;
            result_set_init(&rs);
            eval_top_with_registry(&space, &eval_arena, &arena, &registry, expr, &rs);
            write_results(output_spool, &rs);
            if (fflush(output_spool) != 0) {
                fprintf(stderr, "error: could not write output spool\n");
                free(rs.items);
                fclose(output_spool);
                free(atoms);
                space_free(&space);
                arena_free(&eval_arena);
                arena_free(&arena);
                g_var_intern = NULL;
                var_intern_free(&var_intern_table);
                g_intern = NULL;
                intern_free(&intern_table);
                g_hashcons = NULL;
                hashcons_free(&hashcons_table);
                return 1;
            }
            bool stop_after_error = result_set_has_error(&rs);
            free(rs.items);
            eval_release_temporary_spaces();
            /* Reset ephemeral arena — frees all intermediate eval atoms.
               This makes CeTTa safe for unlimited chaining iterations. */
            arena_free(&eval_arena);
            arena_init(&eval_arena);
            if (stop_after_error) break;
            i += 2;
            continue;
        }

        /* Otherwise: add to space */
        space_add(&space, at);
        i++;
    }

    if (fseek(output_spool, 0, SEEK_SET) != 0) {
        fprintf(stderr, "error: could not rewind output spool\n");
        fclose(output_spool);
        free(atoms);
        cetta_library_context_free(&libraries);
        space_free(&space);
        arena_free(&eval_arena);
        arena_free(&arena);
        g_var_intern = NULL;
        var_intern_free(&var_intern_table);
        g_intern = NULL;
        intern_free(&intern_table);
        g_hashcons = NULL;
        hashcons_free(&hashcons_table);
        return 1;
    }
    {
        char io_buf[8192];
        size_t nread;
        while ((nread = fread(io_buf, 1, sizeof(io_buf), output_spool)) > 0) {
            if (fwrite(io_buf, 1, nread, stdout) != nread) {
                fprintf(stderr, "error: could not flush output spool to stdout\n");
                fclose(output_spool);
                free(atoms);
                cetta_library_context_free(&libraries);
                space_free(&space);
                arena_free(&eval_arena);
                arena_free(&arena);
                g_var_intern = NULL;
                var_intern_free(&var_intern_table);
                g_intern = NULL;
                intern_free(&intern_table);
                g_hashcons = NULL;
                hashcons_free(&hashcons_table);
                return 1;
            }
        }
    }
    fclose(output_spool);

    free(atoms);

    /* Free registry-owned space contents. Space structs themselves may live
       in the persistent arena, so cleanup must not free the struct pointer. */
    for (uint32_t ri = 0; ri < registry.len; ri++) {
        Atom *val = registry.entries[ri].value;
        if (!val) continue;
        if (val->kind == ATOM_GROUNDED && val->ground.gkind == GV_SPACE) {
            Space *sp = (Space *)val->ground.ptr;
            if (sp != &space) {
                space_free(sp);
            }
        }
    }

    cetta_library_context_free(&libraries);
    space_free(&space);
    arena_free(&eval_arena);
    arena_free(&arena);
    g_var_intern = NULL;
    var_intern_free(&var_intern_table);
    g_intern = NULL;
    intern_free(&intern_table);
    g_hashcons = NULL;
    hashcons_free(&hashcons_table);
    return 0;
}
