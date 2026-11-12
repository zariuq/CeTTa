#define _POSIX_C_SOURCE 200809L
#include "link.h"
#include "ownership_signature.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* One (define ...) or (assume ...) of a unit's link record. */
typedef struct {
    const CeAtom *unit, *contract, *at, *obligation;
    CeText name;
    bool checked, external;
} Entry;
typedef struct { Entry *items; size_t len, capacity; } Entries;

static bool head(const CeAtom *a, const char *word) {
    return a && a->kind == CE_ATOM_LIST && a->len && a->items[0]->kind == CE_ATOM_SYMBOL && ce_is(a->items[0]->text,word);
}
static bool symbol(const CeAtom *a, const char *word) { return a && a->kind == CE_ATOM_SYMBOL && ce_is(a->text,word); }
static const CeAtom *part(const CeAtom *list, const char *word) {
    for (size_t i = 1; list && list->kind == CE_ATOM_LIST && i < list->len; ++i)
        if (head(list->items[i],word)) return list->items[i];
    return NULL;
}
static void add(Entries *list, Entry entry) {
    if (list->len == list->capacity) {
        list->capacity = list->capacity ? 2*list->capacity : 64;
        list->items = ce_resize(list->items,list->capacity,sizeof(*list->items));
    }
    list->items[list->len++] = entry;
}
static void text_out(FILE *file, CeText text) {
    CeAtom atom = {.kind=CE_ATOM_TEXT,.text=text}; ce_atom_write(&atom,file);
}

/* (at "FILE" LINE) as FILE:LINE. */
static void place_out(FILE *file, const CeAtom *at) {
    if (!at || at->len != 3) { fputs("?",file); return; }
    fprintf(file,"%.*s:%.*s",(int)at->items[1]->text.len,(const char *)at->items[1]->text.data,
            (int)at->items[2]->text.len,(const char *)at->items[2]->text.data);
}

static bool place_valid(const CeAtom *at) {
    return head(at,"at") && at->len == 3 && at->items[1]->kind == CE_ATOM_TEXT && at->items[2]->kind == CE_ATOM_SYMBOL;
}

static bool obligation_valid(const CeAtom *o) {
    return head(o,"obligation") && o->len == 2 && o->items[1]->kind == CE_ATOM_TEXT;
}

static bool contract_valid(const CeAtom *c) {
    if (!head(c,"contract") || !part(c,"signature") || !part(c,"type") || !part(c,"records")) return false;
    const CeAtom *callbacks = part(c,"callbacks");
    if (!callbacks) return false;
    for (size_t i = 1; i < callbacks->len; ++i) {
        const CeAtom *group = callbacks->items[i];
        if (!head(group,"callback") || group->len != 5 || group->items[1]->kind != CE_ATOM_TEXT ||
            !head(group->items[3],"signature") || !head(group->items[4],"type")) return false;
        const CeAtom *scope = group->items[2];
        if (head(scope,"open") && scope->len == 1) continue;
        if (!head(scope,"targets") || scope->len < 2) return false;
        for (size_t j = 1; j < scope->len; ++j) {
            const CeAtom *target = scope->items[j];
            if (head(target,"external") && target->len == 2 && target->items[1]->kind == CE_ATOM_TEXT) continue;
            if (head(target,"internal") && target->len == 3 && target->items[1]->kind == CE_ATOM_TEXT &&
                target->items[2]->kind == CE_ATOM_TEXT) continue;
            return false;
        }
    }
    return true;
}

/* Reads one record file and appends its units' definitions and assumptions. */
static bool read_record(CeArena *arena, const char *path, Entries *defines, Entries *assumes,
                        const CeAtom ***units, size_t *nunits, size_t *capacity, CeError *error) {
    CeText text; CeAtom *document; CeError local = {{0},0};
    if (!ce_read_file(arena,path,&text,&local)) return ce_error(error,0,"cannot read link record %s",path);
    if (!ce_atom_read(arena,text,&document,&local))
        return ce_error(error,0,"%s: malformed link record: %s",path,local.message);
    if (!head(document,"cesta") || document->len < 2 || !symbol(document->items[1],"link-contracts/1"))
        return ce_error(error,0,"%s: expected (cesta link-contracts/1 UNIT...)",path);
    for (size_t i = 2; i < document->len; ++i) {
        const CeAtom *unit = document->items[i];
        if (!head(unit,"unit") || unit->len < 4 || unit->items[1]->kind != CE_ATOM_TEXT ||
            !head(unit->items[2],"abi") || !head(unit->items[3],"char-signed"))
            return ce_error(error,0,"%s: expected (unit IDENTITY (abi NAME) (char-signed N) ...)",path);
        if (*nunits == *capacity) {
            *capacity = *capacity ? 2 * *capacity : 16;
            *units = ce_resize(*units,*capacity,sizeof(**units));
        }
        (*units)[(*nunits)++] = unit;
        for (size_t j = 4; j < unit->len; ++j) {
            const CeAtom *item = unit->items[j];
            bool define = head(item,"define");
            if (define && item->len >= 5 && item->items[1]->kind == CE_ATOM_SYMBOL &&
                (symbol(item->items[2],"checked") || symbol(item->items[2],"unchecked")) &&
                (symbol(item->items[3],"external") || symbol(item->items[3],"internal")) && place_valid(item->items[4])) {
                Entry e = {.unit=unit,.name=item->items[1]->text,.checked=symbol(item->items[2],"checked"),
                           .external=symbol(item->items[3],"external"),.at=item->items[4],
                           .obligation=part(item,"obligation"),.contract=part(item,"contract")};
                if ((e.obligation && !obligation_valid(e.obligation)) || (!e.checked && !e.obligation) ||
                    (e.external && !contract_valid(e.contract)))
                    return ce_error(error,0,"%s: malformed definition of %.*s",path,(int)e.name.len,(const char *)e.name.data);
                add(defines,e);
            } else if (head(item,"assume") && item->len == 5 && item->items[1]->kind == CE_ATOM_SYMBOL &&
                       place_valid(item->items[2]) && obligation_valid(item->items[3]) && contract_valid(item->items[4])) {
                add(assumes,(Entry){.unit=unit,.name=item->items[1]->text,.at=item->items[2],
                                    .obligation=item->items[3],.contract=item->items[4],.external=true});
            } else return ce_error(error,0,"%s: expected (define ...) or (assume ...) in a unit",path);
        }
    }
    return true;
}

/* Descends to the innermost list in which two atoms differ, so that the
   difference is shown with its context. */
static void difference(const CeAtom *a, const CeAtom *b, const CeAtom **here, const CeAtom **there) {
    while (a && b && a->kind == CE_ATOM_LIST && b->kind == CE_ATOM_LIST && a->len == b->len) {
        size_t i = 0;
        while (i < a->len && ce_atom_equal(a->items[i],b->items[i])) ++i;
        if (i == a->len || a->items[i]->kind != CE_ATOM_LIST || b->items[i]->kind != CE_ATOM_LIST) break;
        a = a->items[i]; b = b->items[i];
    }
    *here = a; *there = b;
}

/* The provider meets the import: compatible ownership guarantees, equal C
   type and callback contracts, and equal shared complete record layouts. A
   unit that saw a record only as an incomplete type did not rely on its
   layout. */
static bool private_callbacks(const CeAtom *callbacks) {
    for (size_t i = 1; callbacks && i < callbacks->len; ++i) {
        const CeAtom *targets = part(callbacks->items[i],"targets");
        for (size_t j = 1; targets && j < targets->len; ++j)
            if (head(targets->items[j],"internal")) return true;
    }
    return false;
}

static bool satisfies_contract(const CeAtom *a, const CeAtom *b, bool same_unit, const char **what,
                          const CeAtom **here, const CeAtom **there) {
    static const char *const parts[] = {"signature","type","callbacks"};
    static const char *const names[] = {"ownership contract","C type","callback contract"};
    for (size_t k = 0; k < 3; ++k) {
        const CeAtom *x = part(a,parts[k]), *y = part(b,parts[k]);
        if (k == 2 && !same_unit && (private_callbacks(x) || private_callbacks(y))) {
            *what = "callback contract (internal target crosses units)"; *here = x; *there = y; return false;
        }
        bool accepted = k == 0 ? ce_ownership_signature_refines(y,x) : ce_atom_equal(x,y);
        if (!accepted) { *what = names[k]; difference(x,y,here,there); return false; }
    }
    const CeAtom *ra = part(a,"records"), *rb = part(b,"records");
    for (size_t i = 1; i < ra->len; ++i) for (size_t j = 1; j < rb->len; ++j) {
        const CeAtom *r = ra->items[i], *s = rb->items[j];
        if (r->kind != CE_ATOM_LIST || s->kind != CE_ATOM_LIST || r->len < 2 || s->len < 2 ||
            !ce_atom_equal(r->items[0],s->items[0]) || !ce_atom_equal(r->items[1],s->items[1])) continue;
        if (!ce_atom_equal(r,s)) { *what = "record layout"; difference(r,s,here,there); return false; }
    }
    return true;
}

static void atom_line(FILE *file, const char *label, const CeAtom *atom) {
    char *buffer = NULL; size_t length = 0; FILE *text = open_memstream(&buffer,&length);
    if (text) {
        if (atom) ce_atom_write(atom,text); else fputs("(absent)",text);
        fclose(text);
    }
    if (buffer && length > 240) { buffer[237] = '.'; buffer[238] = '.'; buffer[239] = '.'; length = 240; }
    fprintf(file,"  %-12s%.*s\n",label,buffer ? (int)length : 0,buffer ? buffer : "");
    free(buffer);
}

static const Entry *definition(const Entries *defines, CeText name) {
    for (size_t i = 0; i < defines->len; ++i)
        if (defines->items[i].external && ce_equal(defines->items[i].name,name)) return &defines->items[i];
    return NULL;
}

static void obligation_item(FILE *file, const char *kind, const Entry *e, const char *differs) {
    fprintf(file,"  (%s ",kind); text_out(file,e->at->items[1]->text);
    fprintf(file," %.*s ",(int)e->at->items[2]->text.len,(const char *)e->at->items[2]->text.data);
    text_out(file,e->obligation ? e->obligation->items[1]->text : (CeText){0});
    if (differs) { fputs(" (differs ",file); text_out(file,(CeText){(unsigned char *)differs,strlen(differs)}); fputc(')',file); }
    fputc(')',file);
}

static bool same_place(const Entry *a, const Entry *b) {
    return ce_atom_equal(a->at,b->at) && ce_atom_equal(a->obligation,b->obligation);
}

/* (cesta link-obligations/1 (program NAME) (units N) (checked-calls N)
     (native NAME (assumed FILE LINE OBLIGATION)...)
     (unchecked NAME (defined FILE LINE OBLIGATION)...
                     (assumed FILE LINE OBLIGATION [(differs WHAT)])...))
   A call that assumes another contract than the unchecked definition states
   rests on its own obligation; differs names what differs. */
static bool write_obligations(const char *path, const char *program, const CeLinkSummary *s,
                              const Entries *defines, const Entries *assumes, const bool *resolved,
                              const char *const *differs, CeError *error) {
    size_t length = strlen(path);
    char *temporary = malloc(length+8);
    if (!temporary) return ce_error(error,0,"out of memory");
    memcpy(temporary,path,length); memcpy(temporary+length,".XXXXXX",8);
    int fd = mkstemp(temporary);
    FILE *file = fd < 0 ? NULL : fdopen(fd,"w");
    if (!file) {
        if (fd >= 0) { close(fd); unlink(temporary); }
        free(temporary); return ce_error(error,0,"cannot write %s: %s",path,strerror(errno));
    }
    fputs("(cesta link-obligations/1\n (program ",file);
    text_out(file,(CeText){(unsigned char *)program,strlen(program)});
    fprintf(file,")\n (units %zu) (checked-calls %zu)\n",s->units,s->checked);
    /* Calls into functions no unit of the program defines. */
    for (size_t i = 0; i < assumes->len; ++i) {
        const Entry *a = &assumes->items[i];
        if (resolved[i] || definition(defines,a->name)) continue;
        bool first = true;
        for (size_t k = 0; k < i; ++k)
            if (ce_equal(assumes->items[k].name,a->name)) { first = false; break; }
        if (!first) continue;
        fprintf(file," (native %.*s\n",(int)a->name.len,(const char *)a->name.data);
        for (size_t k = i; k < assumes->len; ++k)
            if (ce_equal(assumes->items[k].name,a->name)) { obligation_item(file,"assumed",&assumes->items[k],NULL); fputc('\n',file); }
        fputs(" )\n",file);
    }
    /* Definitions left unchecked, and the calls that reach them. */
    for (size_t i = 0; i < defines->len; ++i) {
        const Entry *d = &defines->items[i];
        if (d->checked) continue;
        bool first = true;
        for (size_t k = 0; k < i; ++k)
            if (!defines->items[k].checked && ce_equal(defines->items[k].name,d->name)) { first = false; break; }
        if (!first) continue;
        fprintf(file," (unchecked %.*s\n",(int)d->name.len,(const char *)d->name.data);
        for (size_t k = i; k < defines->len; ++k) {
            const Entry *e = &defines->items[k];
            if (e->checked || !ce_equal(e->name,d->name)) continue;
            bool repeated = false;
            for (size_t m = i; m < k; ++m)
                if (!defines->items[m].checked && ce_equal(defines->items[m].name,d->name) && same_place(&defines->items[m],e))
                    repeated = true;
            if (!repeated) { obligation_item(file,"defined",e,NULL); fputc('\n',file); }
        }
        const Entry *linked = definition(defines,d->name);
        if (linked && !linked->checked) for (size_t k = 0; k < assumes->len; ++k)
            if (resolved[k] && ce_equal(assumes->items[k].name,d->name)) {
                obligation_item(file,"assumed",&assumes->items[k],differs[k]); fputc('\n',file);
            }
        fputs(" )\n",file);
    }
    fputs(")\n",file);
    bool ok = !ferror(file) && !fchmod(fd,0644);
    ok = fclose(file) == 0 && ok;
    if (ok && rename(temporary,path)) ok = false;
    if (!ok) { unlink(temporary); ce_error(error,0,"cannot write %s: %s",path,strerror(errno)); }
    free(temporary);
    return ok;
}

enum { SHOWN_DIFFERENCES = 10 };

bool ce_link_check(CeArena *arena, const char *program, char **records, size_t nrecords,
                   const char *obligations, CeLinkSummary *summary, CeError *error) {
    *summary = (CeLinkSummary){0};
    Entries defines = {0}, assumes = {0};
    const CeAtom **units = NULL; size_t nunits = 0, capacity = 0;
    bool *resolved = NULL, ok = true; const char **differs = NULL;
    for (size_t i = 0; ok && i < nrecords; ++i)
        ok = read_record(arena,records[i],&defines,&assumes,&units,&nunits,&capacity,error);
    if (!ok) goto done;
    summary->units = nunits;
    /* One program, one target: the same ABI model and plain char. */
    for (size_t i = 1; i < nunits; ++i)
        if (!ce_atom_equal(units[i]->items[2],units[0]->items[2]) || !ce_atom_equal(units[i]->items[3],units[0]->items[3])) {
            fprintf(stderr,"%.*s: error: compiled for another target than %.*s\n",
                    (int)units[i]->items[1]->text.len,(const char *)units[i]->items[1]->text.data,
                    (int)units[0]->items[1]->text.len,(const char *)units[0]->items[1]->text.data);
            for (size_t k = 2; k < 4; ++k) {
                atom_line(stderr,"this unit:",units[i]->items[k]); atom_line(stderr,"the other:",units[0]->items[k]);
            }
            ++summary->mismatches;
        }
    resolved = calloc(assumes.len ? assumes.len : 1,sizeof(*resolved));
    differs = calloc(assumes.len ? assumes.len : 1,sizeof(*differs));
    if (!resolved || !differs) { ok = ce_error(error,0,"out of memory"); goto done; }
    for (size_t i = 0; i < assumes.len; ++i) {
        const Entry *a = &assumes.items[i], *d = definition(&defines,a->name);
        if (!d) continue;
        const char *what; const CeAtom *here = NULL, *there = NULL;
        if (!satisfies_contract(a->contract,d->contract,a->unit == d->unit,&what,&here,&there)) {
            /* A checked definition establishes its contract, and the call
               relies on another: an error. An unchecked definition only
               states one: the call rests on its own obligation, and the
               difference is reported and listed. */
            /* Every error is shown; the first warnings are, and the rest are
               counted and listed with the obligations. */
            if (d->checked || summary->differing < SHOWN_DIFFERENCES) {
                place_out(stderr,a->at);
                fprintf(stderr,": %s: %.*s is called under another %s than its %s definition at ",
                        d->checked ? "error" : "warning",(int)a->name.len,(const char *)a->name.data,what,
                        d->checked ? "checked" : "unchecked");
                place_out(stderr,d->at);
                fputs(d->checked ? "\n" : "; the call rests on its own obligation\n",stderr);
                atom_line(stderr,"call:",here); atom_line(stderr,"definition:",there);
            }
            if (d->checked) { ++summary->mismatches; continue; }
            ++summary->differing; differs[i] = what;
        }
        resolved[i] = true;
        if (d->checked) ++summary->checked;
    }
    if (summary->differing > SHOWN_DIFFERENCES)
        fprintf(stderr,"%s: warning: %zu more calls differ from their unchecked definitions%s%s\n",program,
                summary->differing-SHOWN_DIFFERENCES,obligations ? "; all are listed in " : "",obligations ? obligations : "");
    for (size_t i = 0; i < assumes.len; ++i) {
        if (definition(&defines,assumes.items[i].name)) continue;
        bool first = true;
        for (size_t k = 0; k < i; ++k) first &= !ce_equal(assumes.items[k].name,assumes.items[i].name);
        summary->native += first;
    }
    for (size_t i = 0; i < defines.len; ++i) {
        if (defines.items[i].checked) continue;
        bool first = true;
        for (size_t k = 0; k < i; ++k)
            first &= defines.items[k].checked || !ce_equal(defines.items[k].name,defines.items[i].name);
        summary->unchecked += first;
    }
    if (summary->mismatches) { ok = ce_error(error,0,"%zu call%s disagree%s with the definitions this program links",
                                             summary->mismatches,summary->mismatches == 1 ? "" : "s",
                                             summary->mismatches == 1 ? "s" : ""); goto done; }
    if (obligations) ok = write_obligations(obligations,program,summary,&defines,&assumes,resolved,differs,error);
done:
    free(defines.items); free(assumes.items); free(units); free(resolved); free(differs);
    return ok;
}

bool ce_link_merge(CeArena *arena, char **records, size_t nrecords, const char *path, CeError *error) {
    if (!nrecords) {
        if (unlink(path) && errno != ENOENT) return ce_error(error,0,"cannot remove %s: %s",path,strerror(errno));
        return true;
    }
    size_t length = strlen(path);
    char *temporary = malloc(length+8);
    if (!temporary) return ce_error(error,0,"out of memory");
    memcpy(temporary,path,length); memcpy(temporary+length,".XXXXXX",8);
    int fd = mkstemp(temporary);
    FILE *file = fd < 0 ? NULL : fdopen(fd,"w");
    if (!file) {
        if (fd >= 0) { close(fd); unlink(temporary); }
        free(temporary); return ce_error(error,0,"cannot write %s: %s",path,strerror(errno));
    }
    bool ok = true;
    fputs("(cesta link-contracts/1\n",file);
    for (size_t i = 0; ok && i < nrecords; ++i) {
        CeText text; CeAtom *document; CeError local = {{0},0};
        ok = ce_read_file(arena,records[i],&text,&local) && ce_atom_read(arena,text,&document,&local) &&
             head(document,"cesta") && document->len >= 2 && symbol(document->items[1],"link-contracts/1");
        if (!ok) { ce_error(error,0,"cannot read link record %s",records[i]); break; }
        for (size_t k = 2; k < document->len; ++k) { ce_atom_write(document->items[k],file); fputc('\n',file); }
    }
    fputs(")\n",file);
    ok = ok && !ferror(file) && !fchmod(fd,0644);
    ok = fclose(file) == 0 && ok;
    if (ok && rename(temporary,path)) ok = ce_error(error,0,"cannot write %s: %s",path,strerror(errno));
    if (!ok) unlink(temporary);
    free(temporary);
    return ok;
}

int ce_link_command(int argc, char **argv) {
    const char *obligations = NULL, *program = "program";
    int i = 2;
    for (; i < argc; ++i) {
        if (!strcmp(argv[i],"--obligations") && i+1 < argc) obligations = argv[++i];
        else if (!strcmp(argv[i],"--program") && i+1 < argc) program = argv[++i];
        else if (!strcmp(argv[i],"--")) { ++i; break; }
        else if (argv[i][0] == '-') { i = argc; break; }
        else break;
    }
    if (i >= argc) {
        fputs("usage: cesta link-check [--obligations FILE] [--program NAME] RECORD...\n",stderr);
        return 2;
    }
    CeArena arena = {0}; CeError error = {{0},0}; CeLinkSummary summary;
    bool ok = ce_link_check(&arena,program,argv+i,(size_t)(argc-i),obligations,&summary,&error);
    if (!ok) fprintf(stderr,"cesta link-check: %s\n",error.message);
    else fprintf(stderr,"cesta link-check: %s: %zu units; %zu cross-unit calls meet checked definitions; "
                 "%zu native and %zu unchecked functions remain obligations; %zu calls differ from their unchecked definitions\n",
                 program,summary.units,summary.checked,summary.native,summary.unchecked,summary.differing);
    ce_arena_free(&arena);
    return ok ? 0 : 1;
}
