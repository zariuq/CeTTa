/* Prints a .mg file's items, as the evaluator receives them (mg_source.h),
 * in the format of Megalodon's own parser oracle
 * (tests/support/megalodon_reference_parse_oracle.ml), so the reading can be
 * compared with Megalodon's line for line. */

#include "../../native/megalodon/mg_gslt_reader.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* OCaml's %S: String.escaped between double quotes. */
static void print_quoted(FILE *out, const char *text) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        switch (*p) {
        case '"': fputs("\\\"", out); break;
        case '\\': fputs("\\\\", out); break;
        case '\n': fputs("\\n", out); break;
        case '\t': fputs("\\t", out); break;
        case '\r': fputs("\\r", out); break;
        case '\b': fputs("\\b", out); break;
        default:
            if (*p >= 32 && *p <= 126)
                fputc(*p, out);
            else
                fprintf(out, "\\%03u", (unsigned)*p);
        }
    }
    fputc('"', out);
}

static void print_opt_quoted(FILE *out, const char *text) {
    if (!text) {
        fputs("None", out);
        return;
    }
    fputs("(Some ", out);
    print_quoted(out, text);
    fputc(')', out);
}

static void print_bool(FILE *out, bool value) { fputs(value ? "True" : "False", out); }

static void print_names(FILE *out, const char *const *names, uint32_t len) {
    fputs("(List", out);
    for (uint32_t i = 0u; i < len; i++) {
        fputc(' ', out);
        print_quoted(out, names[i]);
    }
    fputc(')', out);
}

static const char *asckind_text(MgAscKind kind) {
    return kind == MG_ASC_TP ? "AscTp" : kind == MG_ASC_SET ? "AscSet" : "AscSubeq";
}

static const char *setinfix_text(MgSetInfix op) { return op == MG_SET_MEM ? "InfMem" : "InfSubq"; }

static void print_l(FILE *out, const MgL *l);

static void print_l_list(FILE *out, MgL *const *items, uint32_t len) {
    fputs("(List", out);
    for (uint32_t i = 0u; i < len; i++) {
        fputc(' ', out);
        print_l(out, items[i]);
    }
    fputc(')', out);
}

static void print_opt_l(FILE *out, const MgL *l) {
    if (!l) {
        fputs("None", out);
        return;
    }
    fputs("(Some ", out);
    print_l(out, l);
    fputc(')', out);
}

static void print_opt_asc(FILE *out, const MgLAsc *asc) {
    if (!asc) {
        fputs("None", out);
        return;
    }
    fprintf(out, "(Some (%s ", asckind_text(asc->kind));
    print_l(out, asc->type);
    fputs("))", out);
}

static void print_l(FILE *out, const MgL *l) {
    switch (l->kind) {
    case MG_L_BYTE: fprintf(out, "(ByteL %d)", l->byte); return;
    case MG_L_STRING: fputs("(StringL ", out); print_quoted(out, l->text); fputc(')', out); return;
    case MG_L_QSTRING: fputs("(QStringL ", out); print_quoted(out, l->text); fputc(')', out); return;
    case MG_L_NAME: fputs("(NaL ", out); print_quoted(out, l->text); fputc(')', out); return;
    case MG_L_NUM:
        fputs("(NuL ", out); print_bool(out, l->negative); fputc(' ', out);
        print_quoted(out, l->text); fputc(' ', out);
        print_opt_quoted(out, l->text2); fputc(' ', out);
        print_opt_quoted(out, l->text3); fputc(')', out);
        return;
    case MG_L_LET:
        fputs("(LeL ", out); print_quoted(out, l->text); fputc(' ', out);
        print_opt_asc(out, l->asc); fputc(' ', out);
        print_l(out, l->a); fputc(' ', out); print_l(out, l->b); fputc(')', out);
        return;
    case MG_L_LETM:
        fputs("(LeML ", out); print_quoted(out, l->text); fputc(' ', out);
        print_names(out, l->names, l->names_len); fputc(' ', out);
        print_l(out, l->a); fputc(' ', out); print_l(out, l->b); fputc(')', out);
        return;
    case MG_L_BINDER:
        fputs("(BiL ", out); print_quoted(out, l->text); fputc(' ', out);
        print_quoted(out, l->text2); fputs(" (List", out);
        for (uint32_t i = 0u; i < l->group_count; i++) {
            fputs(" (", out);
            print_names(out, l->groups[i].names, l->groups[i].name_count);
            fputc(' ', out);
            print_opt_asc(out, l->groups[i].asc);
            fputc(')', out);
        }
        fputs(") ", out); print_l(out, l->a); fputc(')', out);
        return;
    case MG_L_PREFIX: fputs("(PreoL ", out); print_quoted(out, l->text); fputc(' ', out); print_l(out, l->a); fputc(')', out); return;
    case MG_L_POSTFIX: fputs("(PostoL ", out); print_quoted(out, l->text); fputc(' ', out); print_l(out, l->a); fputc(')', out); return;
    case MG_L_INFIX:
        fputs("(InfoL ", out);
        if (l->set_infix) {
            fprintf(out, "(InfSet %s)", setinfix_text(l->set_op));
        } else {
            fputs("(InfNam ", out); print_quoted(out, l->text); fputc(')', out);
        }
        fputc(' ', out); print_l(out, l->a); fputc(' ', out); print_l(out, l->b); fputc(')', out);
        return;
    case MG_L_IMPLICIT: fputs("(ImplopL ", out); print_l(out, l->a); fputc(' ', out); print_l(out, l->b); fputc(')', out); return;
    case MG_L_SEP: case MG_L_REP:
        fputs(l->kind == MG_L_SEP ? "(SepL " : "(RepL ", out); print_quoted(out, l->text);
        fprintf(out, " %s ", setinfix_text(l->set_op));
        print_l(out, l->a); fputc(' ', out); print_l(out, l->b); fputc(')', out);
        return;
    case MG_L_SEPREP:
        fputs("(SepRepL ", out); print_quoted(out, l->text);
        fprintf(out, " %s ", setinfix_text(l->set_op));
        print_l(out, l->a); fputc(' ', out); print_l(out, l->b); fputc(' ', out);
        print_l(out, l->c); fputc(')', out);
        return;
    case MG_L_SETENUM: fputs("(SetEnumL ", out); print_l_list(out, l->list, l->list_len); fputc(')', out); return;
    case MG_L_MTUPLE: fputs("(MTupleL ", out); print_l(out, l->a); fputc(' ', out); print_l_list(out, l->list, l->list_len); fputc(')', out); return;
    case MG_L_PAREN: fputs("(ParenL ", out); print_l(out, l->a); fputc(' ', out); print_l_list(out, l->list, l->list_len); fputc(')', out); return;
    case MG_L_IF: fputs("(IfThenElseL ", out); print_l(out, l->a); fputc(' ', out); print_l(out, l->b); fputc(' ', out); print_l(out, l->c); fputc(')', out); return;
    }
}

static const char *picase_text(MgPicase pic) {
    switch (pic) {
    case MG_PIC_POSTFIX: return "Postfix";
    case MG_PIC_INFIX_NONE: return "InfixNone";
    case MG_PIC_INFIX_LEFT: return "InfixLeft";
    default: return "InfixRight";
    }
}

static void print_docitem(FILE *out, const MgSourceItem *it) {
    switch ((MgDocKind)it->kind) {
    case MG_DOC_AUTHOR:
        fputs("(Author ", out); print_quoted(out, it->name); fputc(' ', out);
        print_names(out, it->names, it->names_len); fputc(')', out); return;
    case MG_DOC_TITLE: fputs("(Title ", out); print_quoted(out, it->name); fputc(')', out); return;
    case MG_DOC_SALT: fputs("(Salt ", out); print_quoted(out, it->name); fputc(')', out); return;
    case MG_DOC_OPAQUE: fputs("(Opaque ", out); print_names(out, it->names, it->names_len); fputc(')', out); return;
    case MG_DOC_TRANSPARENT: fputs("(Transparent ", out); print_names(out, it->names, it->names_len); fputc(')', out); return;
    case MG_DOC_TREASURE: fputs("(Treasure ", out); print_quoted(out, it->name); fputc(')', out); return;
    case MG_DOC_SHOWPROOFTERMS: fputs("(ShowProofTerms ", out); print_bool(out, it->flag); fputc(')', out); return;
    case MG_DOC_SECTION: fputs("(Section ", out); print_quoted(out, it->name); fputc(')', out); return;
    case MG_DOC_END: fputs("(End ", out); print_quoted(out, it->name); fputc(')', out); return;
    case MG_DOC_VAR:
        fputs("(VarDecl ", out); print_names(out, it->names, it->names_len);
        fprintf(out, " %s ", asckind_text(it->asc_kind)); print_l(out, it->a); fputc(')', out); return;
    case MG_DOC_LET:
        fputs("(LetDecl ", out); print_quoted(out, it->name); fputc(' ', out);
        print_opt_asc(out, it->asc); fputc(' ', out); print_l(out, it->a); fputc(')', out); return;
    case MG_DOC_HYP: fputs("(HypDecl ", out); print_quoted(out, it->name); fputc(' ', out); print_l(out, it->a); fputc(')', out); return;
    case MG_DOC_POSTINFIX:
        fputs("(PostInfixDecl ", out); print_quoted(out, it->name); fputc(' ', out);
        print_l(out, it->a); fprintf(out, " %d %s)", it->priority, picase_text(it->picase)); return;
    case MG_DOC_PREFIX:
        fputs("(PrefixDecl ", out); print_quoted(out, it->name); fputc(' ', out);
        print_l(out, it->a); fprintf(out, " %d)", it->priority); return;
    case MG_DOC_BINDER:
        fputs("(BinderDecl ", out); print_bool(out, it->flag); fputc(' ', out);
        print_bool(out, it->flag2); fputc(' ', out); print_quoted(out, it->name); fputc(' ', out);
        print_l(out, it->a); fputc(' ', out); print_opt_l(out, it->b); fputc(')', out); return;
    case MG_DOC_NOTATION: case MG_DOC_UNICODE:
        fputs(it->kind == MG_DOC_NOTATION ? "(NotationDecl " : "(UnicodeDecl ", out); print_quoted(out, it->name);
        fputc(' ', out); print_names(out, it->names, it->names_len); fputc(')', out); return;
    case MG_DOC_SUBSCRIPT: fputs("(SubscriptDecl ", out); print_quoted(out, it->name); fputc(')', out); return;
    case MG_DOC_SUPERSCRIPT: fputs("(SuperscriptDecl ", out); print_quoted(out, it->name); fputc(')', out); return;
    case MG_DOC_PARAM: fputs("(ParamDecl ", out); print_quoted(out, it->name); fputc(' ', out); print_l(out, it->a); fputc(')', out); return;
    case MG_DOC_DEF:
        fputs("(DefDecl ", out); print_quoted(out, it->name); fputc(' ', out);
        print_opt_l(out, it->a); fputc(' ', out); print_l(out, it->b); fputc(')', out); return;
    case MG_DOC_PARAMHASH:
        fputs("(ParamHash ", out); print_quoted(out, it->name); fputc(' ', out);
        print_quoted(out, it->name2); fputc(' ', out); print_opt_quoted(out, it->name3); fputc(')', out); return;
    case MG_DOC_AXIOM: fputs("(AxDecl ", out); print_quoted(out, it->name); fputc(' ', out); print_l(out, it->a); fputc(')', out); return;
    case MG_DOC_THEOREM:
        fputs("(ThmDecl ", out); print_quoted(out, it->name2); fputc(' ', out);
        print_quoted(out, it->name); fputc(' ', out); print_l(out, it->a); fputc(')', out); return;
    }
}

static void print_pftacitem(FILE *out, const MgSourceItem *it) {
    switch ((MgTacKind)it->kind) {
    case MG_TAC_STRUCT: fprintf(out, "(PfStruct %d)", it->depth); return;
    case MG_TAC_EXACT: fputs("(Exact ", out); print_l(out, it->a); fputc(')', out); return;
    case MG_TAC_LET: case MG_TAC_ASSUME:
        fputs(it->kind == MG_TAC_LET ? "(LetTac " : "(AssumeTac ", out);
        print_names(out, it->names, it->names_len); fputc(' ', out);
        print_opt_l(out, it->a); fputc(')', out); return;
    case MG_TAC_SET:
        fputs("(SetTac ", out); print_quoted(out, it->name); fputc(' ', out);
        print_opt_l(out, it->a); fputc(' ', out); print_l(out, it->b); fputc(')', out); return;
    case MG_TAC_APPLY: fputs("(ApplyTac ", out); print_l(out, it->a); fputc(')', out); return;
    case MG_TAC_CLAIM: fputs("(ClaimTac ", out); print_quoted(out, it->name); fputc(' ', out); print_l(out, it->a); fputc(')', out); return;
    case MG_TAC_PROVE:
        fputs("(ProveTac ", out); print_l(out, it->a); fputc(' ', out);
        print_l_list(out, it->terms, it->terms_len); fputc(')', out); return;
    case MG_TAC_CASES:
        fputs("(CasesTac ", out); print_l(out, it->a); fputs(" (List", out);
        for (uint32_t g = 0u; g < it->case_group_count; g++) {
            fputs(" (List", out);
            for (uint32_t i = 0u; i < it->case_groups[g].count; i++) {
                fputs(" (", out);
                print_quoted(out, it->case_groups[g].cases[i].name);
                fputc(' ', out);
                print_l(out, it->case_groups[g].cases[i].term);
                fputc(')', out);
            }
            fputc(')', out);
        }
        fputs("))", out); return;
    case MG_TAC_WITNESS: fputs("(WitnessTac ", out); print_l(out, it->a); fputc(')', out); return;
    case MG_TAC_REWRITE:
        fputs("(RewriteTac ", out); print_bool(out, it->flag); fputc(' ', out);
        print_l(out, it->a); fputs(" (List", out);
        for (uint32_t i = 0u; i < it->positions_len; i++)
            fprintf(out, " %lld", (long long)it->positions[i]);
        fputs("))", out); return;
    case MG_TAC_SPECIAL:
        fputs("(SpecialTac ", out); print_quoted(out, it->name); fputc(' ', out);
        print_l_list(out, it->terms, it->terms_len); fputc(')', out); return;
    case MG_TAC_QED: fputs("Qed", out); return;
    case MG_TAC_ADMITTED: fputs("Admitted", out); return;
    case MG_TAC_ADMIT: fputs("Admit", out); return;
    case MG_TAC_ABY: fputs("(Aby ", out); print_names(out, it->names, it->names_len); fputc(')', out); return;
    }
}

static void print_item(FILE *out, const MgSourceItem *item) {
    if (item->tactic) {
        fputs("(PfTac ", out);
        print_pftacitem(out, item);
    } else {
        fputs("(Doc ", out);
        print_docitem(out, item);
    }
    fputs(")\n", out);
}

int main(int argc, char **argv) {
    bool items = argc >= 3 && strcmp(argv[1], "--items") == 0;
    if (!items || (argc - 3) % 2 != 0) {
        fprintf(stderr, "usage: mg_source_dump --items [-I SIGNATURE.mgs]... FILE.mg\n");
        return 2;
    }
    int file_count = (argc - 1) / 2;
    char **files = malloc(sizeof(*files) * (size_t)file_count);
    if (!files)
        return 2;
    for (int i = 0; i < file_count - 1; i++) {
        if (strcmp(argv[2 + 2 * i], "-I") != 0) {
            fprintf(stderr, "expected -I before %s\n", argv[3 + 2 * i]);
            free(files);
            return 2;
        }
        files[i] = argv[3 + 2 * i];
    }
    files[file_count - 1] = argv[argc - 1];
    MgSource *source = mg_source_new(NULL, 0u);
    if (!source) {
        free(files);
        return 2;
    }
    int status = 0;
    char **held = calloc((size_t)file_count, sizeof(*held));
    for (int f = 0; f < file_count && status == 0 && held; f++) {
        size_t len = 0u;
        char *bytes = mg_gslt_file_bytes(files[f], &len);
        if (!bytes) {
            fprintf(stderr, "cannot read %s\n", files[f]);
            status = 2;
            break;
        }
        held[f] = bytes;
        mg_source_set_input(source, bytes, len);
        bool main_file = f == file_count - 1;
        int r;
        const MgSourceItem *item;
        while ((r = mg_source_next(source, &item)) == 1)
            if (main_file)
                print_item(stdout, item);
        if (r < 0) {
            fprintf(stderr, "%s%s%s\n", main_file ? "" : files[f],
                    main_file ? "" : ": ", mg_source_error(source));
            status = 1;
        }
    }
    mg_source_free(source);
    for (int f = 0; held && f < file_count; f++)
        free(held[f]);
    free(held);
    free(files);
    return status;
}
