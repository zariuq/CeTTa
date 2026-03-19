#include "atom.h"
#include "parser.h"
#include "space.h"
#include "eval.h"
#include "lang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_results(ResultSet *rs) {
    if (rs->len == 0) return;  /* HE prints nothing for empty result sets */
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
    fputs("       cetta --list-languages\n", out);
}

int main(int argc, char **argv) {
    const char *lang_name = "he";
    const char *filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list-languages") == 0) {
            cetta_language_print_inventory(stdout);
            return 0;
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

    Arena arena;
    arena_init(&arena);

    Atom **atoms = NULL;
    int n = parse_metta_file(filename, &arena, &atoms);
    if (n < 0) {
        fprintf(stderr, "error: could not read %s\n", filename);
        return 1;
    }

    Space space;
    space_init(&space);

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

    /* Process top-level atoms */
    int i = 0;
    while (i < n) {
        Atom *at = atoms[i];

        /* ! prefix → evaluate and print */
        if (at->kind == ATOM_SYMBOL && strcmp(at->name, "!") == 0 && i + 1 < n) {
            Atom *expr = atoms[i + 1];
            ResultSet rs;
            result_set_init(&rs);
            eval_top_with_registry(&space, &arena, &registry, expr, &rs);
            print_results(&rs);
            bool stop_after_error = result_set_has_error(&rs);
            free(rs.items);
            if (stop_after_error) break;
            i += 2;
            continue;
        }

        /* Otherwise: add to space */
        space_add(&space, at);
        i++;
    }

    free(atoms);
    free(space.atoms);
    arena_free(&arena);
    return 0;
}
