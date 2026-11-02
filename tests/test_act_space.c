#define _POSIX_C_SOURCE 200809L
#include "act_space.h"
#include "atom_wire.h"
#include "native_sha256.h"
#include "parser.h"
#include "petta_search_machine.h"
#include "space_source_overlay.h"
#include "space_source_snapshot.h"
#include "storage_census.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static double seconds(void) {
  struct timespec t;
  assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static Atom *parse(Arena *a, const char *text) {
  Atom **forms = NULL;
  assert(parse_metta_text(text, a, &forms) == 1);
  Atom *result = forms[0];
  free(forms);
  return result;
}
static void fill(Space *s, Arena *a) {
  const char *facts[] = {
      "(edge a b)",     "(edge a b)",
      "(edge b c)",     "(rel $x $x)",
      "(edge $from z)", "(= (inert) (print! never-execute))",
      "$anything",      "(number 1)",
      "(number 1.0)",   "(number 123456789012345678901234567890)",
      "(number 17/23)"};
  for (size_t i = 0; i < sizeof(facts) / sizeof(*facts); i++)
    assert(space_admit_atom(s, a, parse(a, facts[i])));
  char *large = malloc(131072);
  assert(large);
  memset(large, 'x', 131072);
  large[23] = 0;
  large[24] = (char)255;
  assert(space_admit_atom(s, a, atom_symbol_data_n(a, large, 131072)));
  assert(space_admit_atom(s, a, atom_string_n(a, large, 131072)));
  free(large);
  char many[4096] = "(many";
  size_t at = strlen(many);
  for (unsigned i = 0; i < 100; i++)
    at += (size_t)snprintf(many + at, sizeof(many) - at, " $v%u", i);
  strcpy(many + at, ")");
  assert(space_admit_atom(s, a, parse(a, many)));
  const uint64_t bits[] = {0, UINT64_C(0x8000000000000000),
                           UINT64_C(0x7ff800000000002a)};
  for (size_t i = 0; i < 3; i++) {
    double number;
    memcpy(&number, &bits[i], sizeof(number));
    assert(space_admit_atom(s, a, atom_float(a, number)));
  }
}
static void equal_wire(Atom *left, Atom *right) {
  unsigned char *a = NULL, *b = NULL;
  size_t na = 0, nb = 0;
  assert(cetta_atom_wire_encode(left, &a, &na) == ATOM_WIRE_OK);
  assert(cetta_atom_wire_encode(right, &b, &nb) == ATOM_WIRE_OK);
  assert(na == nb && !memcmp(a, b, na));
  free(a);
  free(b);
}
static size_t equal_spaces(Space *left, Space *right, bool identities) {
  SpaceOccurrenceCursor a, b;
  assert(space_occurrence_cursor_init_scan(left, &a));
  assert(space_occurrence_cursor_init_scan(right, &b));
  CettaIndex ia, ib;
  Atom *va, *vb;
  SpaceOccurrenceCursorStep sa, sb;
  size_t count = 0;
  Arena scratch;
  arena_init_detached(&scratch);
  ArenaMark empty = arena_mark(&scratch);
  for (;;) {
    sa = space_occurrence_cursor_next(&a, &ia, &va);
    sb = space_occurrence_cursor_next(&b, &ib, &vb);
    assert(sa == sb);
    if (sa != SPACE_OCCURRENCE_CURSOR_ITEM)
      break;
    equal_wire(va, vb);
    if (identities)
      equal_wire(space_occurrence_cursor_identity(&a, ia, &scratch),
                 space_occurrence_cursor_identity(&b, ib, &scratch));
    arena_reset(&scratch, empty);
    count++;
  }
  assert(sa == SPACE_OCCURRENCE_CURSOR_END);
  space_occurrence_cursor_release(&a);
  space_occurrence_cursor_release(&b);
  arena_free(&scratch);
  assert(space_length64(left) == count && space_length64(right) == count);
  return count;
}
static PettaMachineHostMode classify(void *ctx, Space *space, Atom *call) {
  (void)ctx;
  (void)space;
  return atom_head_symbol_id(call) == g_builtin_syms.pat_query
             ? PETTA_MACHINE_HOST_PATTERN_QUERY
             : PETTA_MACHINE_HOST_NONE;
}
static Space *resolve(void *ctx, Space *space, Arena *a, Atom *reference) {
  (void)ctx;
  (void)a;
  return atom_is_symbol(reference, "&self") ? space : NULL;
}
static size_t query(Space *s, Arena *a, const char *text) {
  PettaMachineHost host = {.classify = classify, .resolve_space = resolve};
  PettaMachine machine;
  assert(petta_machine_init(&machine, s, a, parse(a, text), NULL, &host));
  Atom *answer = NULL;
  PettaMachineStep step;
  size_t count = 0;
  do {
    Bindings bindings;
    step = petta_machine_next(&machine, &answer, &bindings);
    bindings_free(&bindings);
    if (step == PETTA_MACHINE_STEP_ANSWER)
      count++;
  } while (step == PETTA_MACHINE_STEP_ANSWER ||
           step == PETTA_MACHINE_STEP_SUSPENDED);
  assert(step == PETTA_MACHINE_STEP_EXHAUSTED);
  petta_machine_destroy(&machine);
  return count;
}
static CettaMatchCompletion edge_a(Atom *value, Atom *unused) {
  (void)unused;
  return value && value->kind == ATOM_EXPR && value->expr.len == 3 &&
                 atom_is_symbol(value->expr.elems[0], "edge") &&
                 atom_is_symbol(value->expr.elems[1], "a")
             ? CETTA_MATCH_MATCHED
             : CETTA_MATCH_REFUTED;
}
typedef struct { Arena arena;Atom *row;CettaIndex next; } PositionalRows;
static void positional_free(void *raw) {
  PositionalRows *p=raw;if (p) {arena_free(&p->arena);free(p);}
}
static bool positional_copy(Atom *row,CettaIndex next,void **out) {
  PositionalRows *p=calloc(1,sizeof(*p));if (!p) return false;
  arena_init_detached(&p->arena);p->row=atom_deep_copy(&p->arena,row);p->next=next;
  if (!p->row) {positional_free(p);return false;}*out=p;return true;
}
static bool positional_clone(const void *raw,bool rewind,void **out) {
  const PositionalRows *p=raw;return positional_copy(p->row,rewind ? 0 : p->next,out);
}
static bool positional_open(Space *space,Atom *pattern,void **out,SpaceOccurrenceCursorStep *failure) {
  (void)pattern;(void)failure;return positional_copy(space->match_backend.source,0,out);
}
static SpaceOccurrenceCursorStep positional_next(void *raw,CettaIndex *index) {
  PositionalRows *p=raw;if(p->next==2)return SPACE_OCCURRENCE_CURSOR_END;
  *index=p->next++;return SPACE_OCCURRENCE_CURSOR_ITEM;
}
static Atom *positional_atom(const void *raw,CettaIndex index) {
  const PositionalRows *p=raw;return p->next && index==p->next-1 ? p->row : NULL;
}
static uint64_t positional_count(const Space *space) {(void)space;return 2;}
static const SpaceSourceCursorOps positional_rows={.clone=positional_clone,.next=positional_next,
  .atom=positional_atom,.free=positional_free};
static const SpaceMatchBackendOps positional_source={.name="test-positional-rows",.occurrence_ops=&positional_rows,
  .occurrence_open=positional_open,.logical_len=positional_count};
static void positional_snapshot(const char *path,TermUniverse *universe,Arena *data) {
  Space source,target;space_init_with_universe(&source,universe);space_init_with_universe(&target,universe);
  source.match_backend.kind=SPACE_ENGINE_SOURCE;source.match_backend.ops=&positional_source;
  source.match_backend.source=parse(data,"(positional duplicate)");
  size_t n=strlen(path)+17;char *copy=malloc(n);assert(copy);snprintf(copy,n,"%s.positional",path);
  assert(cetta_act_space_dump(&source,copy,false,CETTA_ATOM_WIRE_BYTES)==CETTA_ACT_OK);
  assert(cetta_act_space_capture(&target,copy,CETTA_ATOM_WIRE_BYTES)==CETTA_ACT_OK);
  SpaceOccurrenceCursor cursor;assert(space_occurrence_cursor_init_scan(&target,&cursor));CettaIndex index;
  Arena ids;arena_init_detached(&ids);
  for (unsigned i=0;i<2;i++) {
    assert(space_occurrence_cursor_next(&cursor,&index,NULL)==SPACE_OCCURRENCE_CURSOR_ITEM);
    Atom *id=space_occurrence_cursor_identity(&cursor,index,&ids);
    assert(id && id->kind==ATOM_EXPR && id->expr.len==3 && atom_is_symbol(id->expr.elems[0],"SpaceOccurrenceIdV1"));
    assert(id->expr.elems[2]->ground.ival==(int64_t)i);
  }
  space_occurrence_cursor_release(&cursor);arena_free(&ids);space_free(&source);space_free(&target);assert(!unlink(copy));free(copy);
}
static void wide_snapshot(const char *path,TermUniverse *universe,Arena *data) {
  Space source,snapshot,workspace,nested,second;
  space_init_with_universe(&source,universe);space_init_with_universe(&snapshot,universe);
  space_init_with_universe(&workspace,universe);space_init_with_universe(&nested,universe);
  space_init_with_universe(&second,universe);
  assert(cetta_act_space_capture(&source,path,2u*1024u*1024u)==CETTA_ACT_OK);
  uint64_t base=space_length64(&source);
  assert(space_source_snapshot_init(&snapshot,&source));
  assert(space_init_overlay_local(&workspace,&snapshot));
  space_free(&source);space_free(&snapshot);
  size_t n=1024u*1024u+17;char *bytes=malloc(n);assert(bytes);memset(bytes,'w',n);
  Atom *large=atom_string_n(data,bytes,n);free(bytes);
  assert(space_admit_atom(&workspace,data,large) && space_length64(&workspace)==base+1);
  assert(space_source_snapshot_init(&nested,&workspace));
  assert(space_init_overlay_local(&second,&nested));
  space_free(&nested);space_free(&workspace);
  assert(space_admit_atom(&second,data,large) && space_length64(&second)==base+2);
  space_free(&second);
}
int main(int argc, char **argv) {
  assert(argc == 3 || argc == 4);
  SymbolTable symbols;
  symbol_table_init(&symbols);
  symbol_table_init_builtins(&symbols, &g_builtin_syms);
  g_symbols = &symbols;
  Arena data, answers;
  arena_init_detached(&data);
  arena_init_detached(&answers);
  TermUniverse universe;
  term_universe_init(&universe);
  term_universe_set_persistent_arena(&universe, &data);
  Space expected, actual;
  space_init_with_universe(&expected, &universe);
  space_init_with_universe(&actual, &universe);
  bool actual_freed = false;
  CettaActStatus status;
  if (!strcmp(argv[1], "bench")) {
    size_t count = argc == 4 ? (size_t)strtoull(argv[3], NULL, 10) : 65536;
    assert(count && count <= INT64_MAX);
    Atom *head = atom_symbol(&data, "row");
    for (size_t i = 0; i < count; i++) {
      char key[64];
      snprintf(key, sizeof(key), "id-%016llx", (unsigned long long)i);
      assert(space_admit_atom(&expected, &data,
                              atom_expr3(&data, head, atom_string(&data, key),
                                         atom_int(&data, i))));
    }
    double began = seconds();
    assert(cetta_act_space_dump(&expected, argv[2], false,
                                CETTA_ATOM_WIRE_BYTES) == CETTA_ACT_OK);
    double dumped = seconds();
    assert(cetta_act_space_capture(&actual, argv[2], CETTA_ATOM_WIRE_BYTES) ==
           CETTA_ACT_OK);
    double opened = seconds();
    const size_t requests = 1000;
    ArenaMark mark = arena_mark(&answers);
    for (unsigned backend = 0; backend < 2; backend++) {
      double start = seconds();
      for (size_t i = 0; i < requests; i++) {
        char text[160];
        size_t row = (i * 7919u) % count;
        snprintf(text, sizeof(text),
                 "(match &self (row \"id-%016llx\" $value) $value)",
                 (unsigned long long)row);
        assert(query(backend ? &actual : &expected, &answers, text) == 1);
        arena_reset(&answers, mark);
      }
      printf("%s_mean_query_us=%.3f\n", backend ? "act" : "memory",
             (seconds() - start) * 1e6 / requests);
    }
    struct stat st;
    assert(stat(argv[2], &st) == 0);
    printf("rows=%zu file_bytes=%llu bytes_per_row=%.3f dump_s=%.3f "
           "validate_open_s=%.3f\n",
           count, (unsigned long long)st.st_size, (double)st.st_size / count,
           dumped - began, opened - dumped);
  } else if (!strcmp(argv[1], "stream")) {
    size_t count=argc==4 ? (size_t)strtoull(argv[3],NULL,10) : 12000;
    fill(&expected,&data);
    for (size_t i=0;i<count;i++) {
      char key[64];snprintf(key,sizeof(key),"shared-prefix-row-%016llx",(unsigned long long)(i%997));
      assert(space_admit_atom(&expected,&data,atom_expr3(&data,atom_symbol(&data,"row"),
          atom_string(&data,key),atom_int(&data,(int64_t)(i%127)))));
    }
    assert(cetta_act_space_dump(&expected,argv[2],false,CETTA_ATOM_WIRE_BYTES)==CETTA_ACT_OK);
    assert(cetta_act_space_capture(&actual,argv[2],CETTA_ATOM_WIRE_BYTES)==CETTA_ACT_OK);
    Space saved;space_init_with_universe(&saved,&universe);
    assert(space_source_snapshot_init(&saved,&actual));
    CettaActSpaceExport *job=NULL;
    assert(cetta_act_space_export_open(&actual,argv[2],false,CETTA_ATOM_WIRE_BYTES,1024*1024,&job)==CETTA_ACT_OK);
    space_free(&actual);actual_freed=true;
    assert(cetta_act_space_export_step(job,0)==CETTA_ACT_PROGRESS);
    size_t steps=0,peak=0;
    do {
      status=cetta_act_space_export_step(job,4096);
      CettaActSpaceExportInfo info;assert(cetta_act_space_export_info(job,&info));
      if (info.heap_bytes>peak) peak=info.heap_bytes;
      assert(++steps<10000000);
    } while(status==CETTA_ACT_PROGRESS);
    assert(status==CETTA_ACT_OK && peak<2*1024*1024);
    cetta_act_space_export_close(job);
    space_init_with_universe(&actual,&universe);actual_freed=false;
    assert(cetta_act_space_capture(&actual,argv[2],CETTA_ATOM_WIRE_BYTES)==CETTA_ACT_OK);
    assert(equal_spaces(&saved,&actual,true)==count+17);
    assert(equal_spaces(&expected,&actual,false)==count+17);
    Space workspace;space_init_with_universe(&workspace,&universe);
    assert(space_init_overlay_local(&workspace,&saved));
    assert(space_admit_atom(&workspace,&data,parse(&data,"(snapshot local)")));
    assert(space_length64(&saved)==count+17 && space_length64(&workspace)==count+18);
    space_free(&workspace);space_free(&saved);
    fprintf(stderr,"stream export: rows=%zu steps=%zu auxiliary_peak=%zu\n",count+17,steps,peak);
  } else if (!strcmp(argv[1], "reject")) {
    status = cetta_act_space_capture(&actual, argv[2], CETTA_ATOM_WIRE_BYTES);
    assert(status == CETTA_ACT_CORRUPT);
    assert(space_length64(&actual) == 0 &&
           actual.match_backend.kind == SPACE_ENGINE_NATIVE);
    assert(space_admit_atom(&actual, &data, parse(&data, "(seed keep)")));
    assert(cetta_act_space_load(&actual, &data, argv[2],
                                CETTA_ATOM_WIRE_BYTES) == CETTA_ACT_CORRUPT);
    assert(space_length64(&actual) == 1);
  } else if (!strcmp(argv[1], "write")) {
    fill(&expected, &data);
    assert(cetta_act_space_dump(&expected, argv[2], false,
                                CETTA_ATOM_WIRE_BYTES) == CETTA_ACT_OK);
    wide_snapshot(argv[2],&universe,&data);
    positional_snapshot(argv[2],&universe,&data);
  } else {
    /* Reopening with a different symbol-table history must preserve bytes. */
    for (unsigned i = 0; i < 100; i++) {
      char name[64];
      snprintf(name, sizeof(name), "unrelated-restart-%u", i);
      atom_symbol(&data, name);
    }
    fill(&expected, &data);
    assert(cetta_act_space_capture(&actual, argv[2], CETTA_ATOM_WIRE_BYTES) ==
           CETTA_ACT_OK);
    size_t rows = equal_spaces(&expected, &actual, false);
    /* A strict MORK export cannot represent these large typed values. Its
     * refusal must leave an existing valid destination intact. */
    assert(cetta_act_space_dump(&expected, argv[2], true,
                                CETTA_ATOM_WIRE_BYTES) == CETTA_ACT_LIMIT);
    Space still_valid;
    space_init_with_universe(&still_valid, &universe);
    assert(cetta_act_space_capture(&still_valid, argv[2],
                                   CETTA_ATOM_WIRE_BYTES) == CETTA_ACT_OK);
    assert(equal_spaces(&actual, &still_valid, true) == rows);
    space_free(&still_valid);
    Space imported;
    space_init_with_universe(&imported, &universe);
    assert(space_match_backend_try_set(&imported, SPACE_ENGINE_NATIVE_CANDIDATE_EXACT));
    assert(space_admit_atom(&imported, &data, parse(&data, "(seed retained)")));
    assert(cetta_act_space_load(&imported, &data, argv[2],
                                CETTA_ATOM_WIRE_BYTES) == CETTA_ACT_OK);
    assert(space_length64(&imported) == rows + 1);
    space_free(&imported);
    const char *queries[] = {
        "(match &self (edge a $to) $to)",
        "(match &self (edge $from z) $from)",
        "(match &self (rel a b) yes)",
        "(match &self (rel a a) yes)",
        "(match &self (number 1.0) yes)",
        "(match &self (, (edge a $mid) (edge $mid c)) yes)",
        "(match &self (= (inert) $program) $program)",
        "(match &self (absent x) yes)"};
    for (size_t i = 0; i < sizeof(queries) / sizeof(*queries); i++)
      assert(query(&actual, &answers, queries[i]) ==
             query(&expected, &answers, queries[i]));
    CettaStorageCensus census = {0};
    actual.match_backend.ops->storage(&actual, &census);
    cetta_storage_finish(&census);
    assert(!census.incomplete && census.bytes < 16384);
    cetta_storage_census_free(&census);
    SpaceOccurrenceCursor cursor, clone;
    assert(space_occurrence_cursor_init_scan(&actual, &cursor));
    CettaIndex index;
    Atom *row;
    for (unsigned i = 0; i < 4; i++)
      assert(space_occurrence_cursor_next(&cursor, &index, &row) ==
             SPACE_OCCURRENCE_CURSOR_ITEM);
    assert(space_occurrence_cursor_clone(&cursor, &clone));
    Atom *copied = space_occurrence_cursor_atom(&clone, index);
    assert(
        var_id_eq(row->expr.elems[1]->var_id, copied->expr.elems[1]->var_id));
    assert(var_id_eq(copied->expr.elems[1]->var_id,
                     copied->expr.elems[2]->var_id));
    if (!strcmp(argv[1], "hold")) {
      puts("CAPTURED");
      fflush(stdout);
      assert(getchar() == '!');
    } else {
      size_t n = strlen(argv[2]);
      char *copy_path = malloc(n + 6);
      assert(copy_path);
      snprintf(copy_path, n + 6, "%s.copy", argv[2]);
      assert(cetta_act_space_dump(&actual, copy_path, false,
                                  CETTA_ATOM_WIRE_BYTES) == CETTA_ACT_OK);
      Space copy, workspace;
      space_init_with_universe(&copy, &universe);
      assert(cetta_act_space_capture(&copy, copy_path, CETTA_ATOM_WIRE_BYTES) ==
             CETTA_ACT_OK);
      assert(equal_spaces(&actual, &copy, true) == rows);
      assert(space_init_overlay_local(&workspace, &copy));
      uint64_t removed = 0;
      assert(space_remove_all_compared(&workspace, parse(&data, "filter"),
                                       edge_a,
                                       &removed) == CETTA_MATCH_MATCHED);
      assert(removed == 2 && space_length64(&workspace) == rows - 2);
      assert(space_admit_atom(&workspace, &data, parse(&data, "(local edit)")));
      assert(space_length64(&workspace) == rows - 1);
      assert(space_length64(&copy) == rows);
      space_free(&workspace);
      space_free(&copy);
      unlink(copy_path);
      free(copy_path);
    }
    space_free(&actual);
    actual_freed = true;
    size_t remaining = 0;
    SpaceOccurrenceCursorStep step;
    while ((step = space_occurrence_cursor_next(&cursor, &index, NULL)) ==
           SPACE_OCCURRENCE_CURSOR_ITEM)
      remaining++;
    assert(step == SPACE_OCCURRENCE_CURSOR_END && remaining == rows - 4);
    size_t other = 0;
    while ((step = space_occurrence_cursor_next(&clone, &index, NULL)) ==
           SPACE_OCCURRENCE_CURSOR_ITEM)
      other++;
    assert(step == SPACE_OCCURRENCE_CURSOR_END && other == remaining);
    space_occurrence_cursor_release(&cursor);
    space_occurrence_cursor_release(&clone);
  }
  if (!actual_freed)
    space_free(&actual);
  space_free(&expected);
  term_universe_free(&universe);
  arena_free(&answers);
  arena_free(&data);
  symbol_table_free(&symbols);
  g_symbols = NULL;
  puts("ACT_SPACE_OK");
  return 0;
}
