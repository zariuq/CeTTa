#include "atom.h"
#include "parser.h"
#include "space.h"
#include "eval.h"
#include "lang.h"
#include "compile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool g_count_only = false;

static void print_results(ResultSet *rs) {
    if (rs->len == 0) return;  /* HE prints nothing for empty result sets */
    if (g_count_only) {
        printf("%u\n", rs->len);
        return;
    }
    printf("[");
    for (uint32_t i = 0; i < rs->len; i++) {
        if (i > 0) printf(", ");
        atom_print(rs->items[i], stdout);
    }
    printf("]\n");
}

static bool result_set_has_error(ResultSet *rs) {
    for (uint32_t i = 0; i < rs->len; i++) {
        if (atom_is_error(rs->items[i])) return true;
    }
    return false;
}

static void print_usage(FILE *out) {
    fputs("usage: cetta [--lang <name>] <file.metta>\n", out);
    fputs("       cetta --compile <file.metta>     # emit LLVM IR to stdout\n", out);
    fputs("       cetta --count-only <file.metta>  # print result counts only\n", out);
    fputs("       cetta --space-match-backend <name> <file.metta>\n", out);
    fputs("       cetta --list-space-match-backends\n", out);
    fputs("       cetta --list-languages\n", out);
}

int main(int argc, char **argv) {
    const char *lang_name = "he";
    const char *filename = NULL;
    bool compile_mode = false;
    bool count_only = false;
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
        if (strcmp(argv[i], "--compile") == 0) {
            compile_mode = true;
            continue;
        }
        if (strcmp(argv[i], "--count-only") == 0) {
            count_only = true;
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
        if (!filename) {
            filename = argv[i];
            continue;
        }
        print_usage(stderr);
        return 1;
    }

    if (!filename) {
        print_usage(stderr);
        return 1;
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

    Arena arena;       /* persistent: parsed atoms, space content, type decls */
    Arena eval_arena;  /* ephemeral: intermediate eval results, reset per ! */
    arena_init(&arena);
    arena_init(&eval_arena);

    /* Symbol interning for O(1) symbol comparison */
    InternTable intern_table;
    intern_init(&intern_table);
    g_intern = &intern_table;

    /* Hash-consing for structural sharing (reduces memory for large derivations) */
    HashConsTable hashcons_table;
    hashcons_init(&hashcons_table);
    g_hashcons = &hashcons_table;

    Atom **atoms = NULL;
    int n = parse_metta_file(filename, &arena, &atoms);
    if (n < 0) {
        fprintf(stderr, "error: could not read %s\n", filename);
        return 1;
    }

    Space space;
    space_init(&space);
    if (!space_match_backend_try_set(&space, match_backend_kind)) {
        fprintf(stderr, "error: space match backend '%s' is recognized but not implemented yet\n",
                space_match_backend_kind_name(match_backend_kind));
        return 2;
    }

    Registry registry;
    registry_init(&registry);
    registry_bind(&registry, "&self", atom_space(&arena, &space));

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
        g_intern = NULL;
        intern_free(&intern_table);
        return 0;
    }

    /* Process top-level atoms */
    int i = 0;
    while (i < n) {
        Atom *at = atoms[i];

        /* ! prefix → evaluate and print */
        if (at->kind == ATOM_SYMBOL && strcmp(at->name, "!") == 0 && i + 1 < n) {
            Atom *expr = atoms[i + 1];
            ResultSet rs;
            result_set_init(&rs);
            eval_top_with_registry(&space, &eval_arena, &arena, &registry, expr, &rs);
            print_results(&rs);
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

    space_free(&space);
    arena_free(&eval_arena);
    arena_free(&arena);
    g_intern = NULL;
    intern_free(&intern_table);
    g_hashcons = NULL;
    hashcons_free(&hashcons_table);
    return 0;
}
