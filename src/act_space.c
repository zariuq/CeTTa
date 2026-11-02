#define _POSIX_C_SOURCE 200809L
#include "act_space.h"
#include "atom_wire.h"
#include "mm2_lower.h"
#include "native_sha256.h"
#include "space_source_overlay.h"
#include "storage_census.h"
#include "byte_run_sort.h"
#include "space_source_snapshot.h"
#include "shared_transition.h"
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* CACT0001: a lossless occurrence profile around an ordinary ACTree03 trie.
 * All offsets and section sizes are little-endian; trie keys use big-endian
 * ordinal suffixes. The final SHA-256 binds header, trie and payload section.
 * Namespaces: 1 occurrence, 3 scalar projection, 4 open root,
 * 5 identity. CAV1 payloads preserve types and within-fact variable sharing. */
enum { HEADER = 64, DIGEST = 32, OCC = 1, POS = 3, OPEN = 4, ID = 5 };
typedef struct {
  unsigned char *data;
  size_t len, cap;
} Bytes;
typedef struct {
  atomic_size_t refs;
  CettaActTree *mapping, *trie;
  size_t payload_at, payload_size, maximum;
  uint64_t count;
  bool cetta;
} Source;
typedef struct {
  Source *source;
  CettaActCursor *streams[3];
  uint64_t row_ids[3], records[3];
  bool peeked[3], ended[3], selected;
  size_t stream_count;
  uint64_t seen;
  CettaIndex last;
  Arena arena;
  ArenaMark empty;
  Atom *atom, *identity;
  SpaceOccurrenceCursorStep terminal;
} Cursor;
static uint64_t le64(const unsigned char *p) {
  uint64_t v = 0;
  for (unsigned i = 0; i < 8; i++)
    v |= (uint64_t)p[i] << (8 * i);
  return v;
}
static uint64_t be64(const unsigned char *p) {
  uint64_t v = 0;
  for (unsigned i = 0; i < 8; i++)
    v = (v << 8) | p[i];
  return v;
}
static void setle(unsigned char *p, uint64_t v) {
  for (unsigned i = 0; i < 8; i++)
    p[i] = (unsigned char)(v >> (8 * i));
}
static void setbe(unsigned char *p, uint64_t v) {
  for (unsigned i = 0; i < 8; i++)
    p[7 - i] = (unsigned char)(v >> (8 * i));
}
static bool grow(void **ptr, size_t *cap, size_t need, size_t width) {
  if (need <= *cap)
    return true;
  if (need > SIZE_MAX / width)
    return false;
  size_t n = *cap ? *cap : 16;
  while (n < need) {
    if (n > SIZE_MAX / 2) {
      n = need;
      break;
    }
    n *= 2;
  }
  if (n > SIZE_MAX / width)
    n = need;
  void *p = realloc(*ptr, n * width);
  if (!p)
    return false;
  *ptr = p;
  *cap = n;
  return true;
}
static bool add(Bytes *b, const void *p, size_t n) {
  if (n > SIZE_MAX - b->len || !grow((void **)&b->data, &b->cap, b->len + n, 1))
    return false;
  if (n)
    memcpy(b->data + b->len, p, n);
  b->len += n;
  return true;
}
static CettaActBytes view(const Bytes *b) {
  return (CettaActBytes){b->data, b->len};
}
static void retain(Source *s) { atomic_fetch_add(&s->refs, 1); }
static void release(Source *s) {
  if (s && atomic_fetch_sub(&s->refs, 1) == 1) {
    cetta_act_release(s->trie);
    cetta_act_release(s->mapping);
    free(s);
  }
}
static SpaceOccurrenceCursorStep failure(CettaActStatus s) {
  return s == CETTA_ACT_NOMEM || s == CETTA_ACT_LIMIT
             ? SPACE_OCCURRENCE_CURSOR_CAPACITY
             : SPACE_OCCURRENCE_CURSOR_INCOMPLETE;
}
static CettaActStatus wire_status(CettaAtomWireStatus s) {
  if (s == ATOM_WIRE_UNSUPPORTED)
    return CETTA_ACT_UNSUPPORTED;
  return s == ATOM_WIRE_OK      ? CETTA_ACT_OK
         : s == ATOM_WIRE_NOMEM ? CETTA_ACT_NOMEM
         : s == ATOM_WIRE_LIMIT ? CETTA_ACT_LIMIT
                                : CETTA_ACT_CORRUPT;
}
static bool exact(const Atom *a) {
  return a && (a->kind == ATOM_SYMBOL ||
               (a->kind == ATOM_GROUNDED && a->ground.gkind == GV_STRING));
}
static bool field_prefix(Bytes *b, uint64_t arity, uint64_t position,
                         const Atom *value, size_t maximum) {
  unsigned char head[18] = {POS};
  setbe(head + 1, arity);
  setbe(head + 9, position);
  head[17] = exact(value) ? 1 : 0;
  if (!add(b, head, sizeof(head)))
    return false;
  if (!head[17])
    return true;
  unsigned char *wire = NULL;
  size_t n = 0;
  if (cetta_atom_wire_encode_bounded(value, maximum, &wire, &n) != ATOM_WIRE_OK)
    return false;
  bool ok = add(b, wire, n);
  free(wire);
  return ok;
}
static bool payload_bytes(const Source *s, uint64_t offset,
                          CettaActBytes *out) {
  if (offset >= s->payload_size)
    return false;
  CettaActBytes image = cetta_act_image(s->mapping);
  size_t at = (size_t)offset, used;
  uint64_t n;
  const unsigned char *p = image.data + s->payload_at;
  if (!cetta_act_varint(p + at, s->payload_size - at, &n, &used))
    return false;
  at += used;
  if (n > s->maximum || n > s->payload_size - at)
    return false;
  *out = (CettaActBytes){p + at, (size_t)n};
  return true;
}
static bool payload_number(const Source *s, const unsigned char *p, size_t *at,
                           uint64_t *value) {
  size_t used;
  if (*at >= s->payload_size ||
      !cetta_act_varint(p + *at, s->payload_size - *at, value, &used))
    return false;
  *at += used;
  return true;
}
static bool record_bytes(const Source *s, uint64_t offset,
                         CettaActBytes *identity, CettaActBytes *payload,
                         bool *compact, uint64_t *ordinal) {
  if (offset >= s->payload_size)
    return false;
  const unsigned char *p = cetta_act_image(s->mapping).data + s->payload_at;
  size_t at = (size_t)offset;
  uint64_t n, body;
  if (!payload_number(s, p, &at, &n))
    return false;
  *compact = n == 0;
  if (*compact) {
    uint64_t domain;
    if (!payload_number(s, p, &at, &domain) ||
        !payload_number(s, p, &at, ordinal) || *ordinal > INT64_MAX ||
        !payload_number(s, p, &at, &body) ||
        !payload_bytes(s, domain, identity) || identity->size != 16)
      return false;
  } else {
    if (n > s->maximum || n > s->payload_size - at)
      return false;
    *identity = (CettaActBytes){p + at, (size_t)n};
    at += (size_t)n;
    if (s->payload_size - at < 8)
      return false;
    body = le64(p + at);
  }
  return payload_bytes(s, body, payload);
}
static CettaActStatus decode_record(Source *s, uint64_t record, Arena *a,
                                    Atom **atom, Atom **identity) {
  CettaActBytes id, body;
  bool compact;
  uint64_t ordinal = 0;
  if (!record_bytes(s, record, &id, &body, &compact, &ordinal))
    return CETTA_ACT_CORRUPT;
  CettaAtomWireStatus w =
      cetta_atom_wire_decode_bounded(a, body.data, body.size, s->maximum, atom);
  if (w != ATOM_WIRE_OK)
    return wire_status(w);
  if (compact) {
    char domain[33];
    for (unsigned i = 0; i < 16; i++)
      snprintf(domain + 2 * i, 3, "%02x", id.data[i]);
    *identity =
        atom_expr3(a, atom_symbol(a, "SpaceOccurrenceIdV1"),
                   atom_string(a, domain), atom_int(a, (int64_t)ordinal));
  } else {
    w = cetta_atom_wire_decode_bounded(a, id.data, id.size, s->maximum,
                                       identity);
    if (w != ATOM_WIRE_OK)
      return wire_status(w);
  }
  return !*identity || atom_has_vars(*identity) ? CETTA_ACT_CORRUPT
                                                : CETTA_ACT_OK;
}
static void cursor_free(void *raw) {
  Cursor *c = raw;
  if (!c)
    return;
  for (size_t i = 0; i < 3; i++)
    cetta_act_cursor_close(c->streams[i]);
  arena_free(&c->arena);
  release(c->source);
  free(c);
}
static CettaActStatus cursor_new(Source *s, Atom *pattern, Cursor **out) {
  *out = NULL;
  Cursor *c = calloc(1, sizeof(*c));
  if (!c)
    return CETTA_ACT_NOMEM;
  c->source = s;
  retain(s);
  arena_init_detached(&c->arena);
  c->empty = arena_mark(&c->arena);
  CettaActStatus status = CETTA_ACT_OK;
  if (s->cetta && pattern && pattern->kind == ATOM_EXPR) {
    size_t position = SIZE_MAX;
    for (size_t i = 0; i < pattern->expr.len && i < 3; i++)
      if (exact(pattern->expr.elems[i]))
        position = i;
    if (position != SIZE_MAX) {
      Bytes prefix = {0};
      c->selected = true;
      c->stream_count = 3;
      if (!field_prefix(&prefix, pattern->expr.len, position,
                        pattern->expr.elems[position], s->maximum)) {
        status = CETTA_ACT_NOMEM;
        goto failed;
      }
      status = cetta_act_cursor_prefix(s->trie, view(&prefix), &c->streams[0]);
      free(prefix.data);
      if (status != CETTA_ACT_OK)
        goto failed;
      unsigned char any[18] = {POS};
      setbe(any + 1, pattern->expr.len);
      setbe(any + 9, position);
      status = cetta_act_cursor_prefix(
          s->trie, (CettaActBytes){any, sizeof(any)}, &c->streams[1]);
      if (status != CETTA_ACT_OK)
        goto failed;
      unsigned char open = OPEN;
      status = cetta_act_cursor_prefix(s->trie, (CettaActBytes){&open, 1},
                                       &c->streams[2]);
      if (status != CETTA_ACT_OK)
        goto failed;
    }
  }
  if (!c->stream_count) {
    c->stream_count = 1;
    unsigned char prefix = OCC;
    status = s->cetta
                 ? cetta_act_cursor_prefix(s->trie, (CettaActBytes){&prefix, 1},
                                           &c->streams[0])
                 : cetta_act_cursor_open(s->trie, NULL, NULL, &c->streams[0]);
    if (status != CETTA_ACT_OK)
      goto failed;
  }
  *out = c;
  return CETTA_ACT_OK;
failed:
  cursor_free(c);
  return status;
}
static SpaceOccurrenceCursorStep cursor_next(void *raw, CettaIndex *index) {
  Cursor *c = raw;
  if (c->terminal)
    return c->terminal;
  arena_reset(&c->arena, c->empty);
  c->atom = c->identity = NULL;
  CettaActStatus status = CETTA_ACT_OK;
  if (!c->source->cetta) {
    CettaActBytes key;
    uint64_t v;
    status = cetta_act_cursor_next(c->streams[0], &key, &v);
    if (status == CETTA_ACT_END) {
      c->terminal = SPACE_OCCURRENCE_CURSOR_END;
      return c->terminal;
    }
    const char *error = NULL;
    if (status != CETTA_ACT_OK || key.size > c->source->maximum ||
        !cetta_mm2_bridge_expr_bytes_to_atom(&c->arena, key.data, key.size,
                                             &c->atom, &error)) {
      c->terminal =
          failure(status == CETTA_ACT_OK ? CETTA_ACT_CORRUPT : status);
      return c->terminal;
    }
    char digest[65];
    cetta_native_sha256_hex(key.data, key.size, digest);
    c->identity =
        atom_expr2(&c->arena, atom_symbol(&c->arena, "MorkOccurrenceV1"),
                   atom_string(&c->arena, digest));
  } else {
    size_t chosen = SIZE_MAX;
    for (size_t i = 0; i < c->stream_count; i++) {
      if (!c->peeked[i] && !c->ended[i]) {
        CettaActBytes key;
        status = cetta_act_cursor_next(c->streams[i], &key, &c->records[i]);
        if (status == CETTA_ACT_END)
          c->ended[i] = true;
        else if (status != CETTA_ACT_OK || key.size < 9) {
          c->terminal =
              failure(status == CETTA_ACT_OK ? CETTA_ACT_CORRUPT : status);
          return c->terminal;
        } else {
          c->row_ids[i] = be64(key.data + key.size - 8);
          c->peeked[i] = true;
        }
      }
      if (c->peeked[i] &&
          (chosen == SIZE_MAX || c->row_ids[i] < c->row_ids[chosen]))
        chosen = i;
    }
    if (chosen == SIZE_MAX) {
      if (!c->selected && c->seen != c->source->count) {
        c->terminal = SPACE_OCCURRENCE_CURSOR_INCOMPLETE;
        return c->terminal;
      }
      c->terminal = SPACE_OCCURRENCE_CURSOR_END;
      return c->terminal;
    }
    uint64_t row = c->row_ids[chosen], record = c->records[chosen];
    if (row >= c->source->count || (!c->selected && row != c->seen)) {
      c->terminal = SPACE_OCCURRENCE_CURSOR_INCOMPLETE;
      return c->terminal;
    }
    unsigned char occurrence[9] = {OCC};
    setbe(occurrence + 1, row);
    uint64_t canonical;
    if (cetta_act_get(c->source->trie, (CettaActBytes){occurrence, 9},
                      &canonical) != CETTA_ACT_OK ||
        canonical != record) {
      c->terminal = SPACE_OCCURRENCE_CURSOR_INCOMPLETE;
      return c->terminal;
    }
    for (size_t i = 0; i < c->stream_count; i++)
      if (c->peeked[i] && c->row_ids[i] == row)
        c->peeked[i] = false;
    status =
        decode_record(c->source, record, &c->arena, &c->atom, &c->identity);
    if (status != CETTA_ACT_OK) {
      c->terminal = failure(status);
      return c->terminal;
    }
  }
  if (c->seen == UINT64_MAX) {
    c->terminal = SPACE_OCCURRENCE_CURSOR_CAPACITY;
    return c->terminal;
  }
  c->last = (CettaIndex)c->seen++;
  *index = c->last;
  return SPACE_OCCURRENCE_CURSOR_ITEM;
}
static Atom *cursor_atom(const void *raw, CettaIndex i) {
  const Cursor *c = raw;
  return c && c->seen && c->last == i ? c->atom : NULL;
}
static Atom *cursor_identity(const void *raw, CettaIndex i) {
  const Cursor *c = raw;
  return c && c->seen && c->last == i ? c->identity : NULL;
}
static bool cursor_clone(const void *raw, bool rewind, void **out) {
  const Cursor *c = raw;
  *out = NULL;
  Cursor *copy = calloc(1, sizeof(*copy));
  if (!copy)
    return false;
  copy->source = c->source;
  retain(copy->source);
  arena_init_detached(&copy->arena);
  copy->empty = arena_mark(&copy->arena);
  copy->stream_count = c->stream_count;
  copy->selected = c->selected;
  for (size_t i = 0; i < c->stream_count; i++)
    if (cetta_act_cursor_clone(c->streams[i], rewind, &copy->streams[i]) !=
        CETTA_ACT_OK) {
      cursor_free(copy);
      return false;
    }
  if (!rewind) {
    copy->seen = c->seen;
    copy->last = c->last;
    copy->terminal = c->terminal;
    memcpy(copy->row_ids, c->row_ids, sizeof(copy->row_ids));
    memcpy(copy->records, c->records, sizeof(copy->records));
    memcpy(copy->peeked, c->peeked, sizeof(copy->peeked));
    memcpy(copy->ended, c->ended, sizeof(copy->ended));
    if (c->atom)
      copy->atom = atom_deep_copy(&copy->arena, c->atom);
    if (c->identity)
      copy->identity = atom_deep_copy(&copy->arena, c->identity);
    if ((c->atom && !copy->atom) || (c->identity && !copy->identity)) {
      cursor_free(copy);
      return false;
    }
  }
  *out = copy;
  return true;
}
static bool cursor_select(const void *raw, Atom *pattern, void **out,
                          SpaceOccurrenceCursorStep *fail) {
  Cursor *c = NULL;
  CettaActStatus s = cursor_new(((const Cursor *)raw)->source, pattern, &c);
  *out = c;
  if (s != CETTA_ACT_OK && fail)
    *fail = failure(s);
  return s == CETTA_ACT_OK;
}
static bool source_open(Space *space, Atom *pattern, void **out,
                        SpaceOccurrenceCursorStep *fail) {
  Cursor *c = NULL;
  CettaActStatus s = cursor_new(space->match_backend.source, pattern, &c);
  *out = c;
  if (s != CETTA_ACT_OK && fail)
    *fail = failure(s);
  return s == CETTA_ACT_OK;
}
static uint64_t source_count(const Space *space) {
  return ((const Source *)space->match_backend.source)->count;
}
static void source_free(Space *space) {
  release(space->match_backend.source);
  space->match_backend.source = NULL;
}
static void retained_storage(const Source *s, CettaStorageCensus *census) {
  if (!s || !cetta_storage_object(census, s, sizeof(*s), CETTA_STORAGE_BLOCK))
    return;
  cetta_act_storage(s->trie, census);
  cetta_act_storage(s->mapping, census);
}
static void cursor_storage(const void *raw, CettaStorageCensus *census) {
  const Cursor *c = raw;
  if (!c || !cetta_storage_object(census, c, sizeof(*c), CETTA_STORAGE_BLOCK))
    return;
  retained_storage(c->source, census);
  arena_storage_census(&c->arena, census);
  for (size_t i = 0; i < c->stream_count; i++)
    cetta_act_cursor_storage(c->streams[i], census);
}
static void source_storage(const Space *space, CettaStorageCensus *census) {
  retained_storage(space->match_backend.source, census);
}
static bool source_overlay(Space *target, const Space *base);
static size_t cursor_value_limit(const void *raw) { return ((const Cursor *)raw)->source->maximum; }
static const SpaceSourceCursorOps cursor_ops = {.clone = cursor_clone,
                                                .select = cursor_select,
                                                .next = cursor_next,
                                                .atom = cursor_atom,
                                                .identity = cursor_identity,
                                                .value_byte_limit = cursor_value_limit,
                                                .storage = cursor_storage,
                                                .free = cursor_free};
static const SpaceMatchBackendOps source_ops = {.name = "native-act",
                                                .occurrence_ops = &cursor_ops,
                                                .occurrence_open = source_open,
                                                .logical_len = source_count,
                                                .free = source_free,
                                                .storage = source_storage,
                                                .overlay_local =
                                                    source_overlay};
static bool source_overlay(Space *target, const Space *base) {
  Space *copy = malloc(sizeof(*copy));
  if (!copy)
    return false;
  space_init_with_universe(copy, target->native.universe);
  Source *s = base->match_backend.source;
  retain(s);
  copy->match_backend.kind = SPACE_ENGINE_SOURCE;
  copy->match_backend.ops = &source_ops;
  copy->match_backend.source = s;
  if (space_source_overlay_init_owned(target, copy, s->maximum))
    return true;
  space_free(copy);
  free(copy);
  return false;
}
static bool sidecar_exists(const char *path) {
  size_t n = strlen(path);
  if (n > SIZE_MAX - 8)
    return true;
  char *p = malloc(n + 8);
  if (!p)
    return true;
  memcpy(p, path, n);
  memcpy(p + n, ".copies", 8);
  struct stat st;
  int fd = open(p, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
  bool exists = fd >= 0
                    ? fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size != 0
                    : errno != ENOENT;
  if (fd >= 0)
    close(fd);
  free(p);
  return exists;
}
static CettaActStatus check_index(Source *s, uint64_t row, uint64_t record,
                                  Atom *atom, Atom *identity) {
  Bytes key = {0};
  unsigned char ordinal[8];
  setbe(ordinal, row);
  uint64_t got;
  unsigned char *wire = NULL;
  size_t size = 0;
  CettaAtomWireStatus w =
      cetta_atom_wire_encode_bounded(identity, s->maximum, &wire, &size);
  unsigned char ns = ID;
  bool ok = w == ATOM_WIRE_OK && add(&key, &ns, 1) && add(&key, wire, size);
  free(wire);
  if (!ok) {
    free(key.data);
    return w == ATOM_WIRE_NOMEM ? CETTA_ACT_NOMEM : CETTA_ACT_CORRUPT;
  }
  bool found =
      cetta_act_get(s->trie, view(&key), &got) == CETTA_ACT_OK && got == row;
  free(key.data);
  if (!found)
    return CETTA_ACT_CORRUPT;
  if (atom->kind == ATOM_VAR) {
    unsigned char k[9] = {OPEN};
    memcpy(k + 1, ordinal, 8);
    return cetta_act_get(s->trie, (CettaActBytes){k, 9}, &got) ==
                       CETTA_ACT_OK &&
                   got == record
               ? CETTA_ACT_OK
               : CETTA_ACT_CORRUPT;
  }
  if (atom->kind != ATOM_EXPR)
    return CETTA_ACT_OK;
  for (size_t i = 0; i < atom->expr.len && i < 3; i++) {
    key = (Bytes){0};
    ok = field_prefix(&key, atom->expr.len, i, atom->expr.elems[i],
                      s->maximum) &&
         add(&key, ordinal, 8);
    found = ok && cetta_act_get(s->trie, view(&key), &got) == CETTA_ACT_OK &&
            got == record;
    free(key.data);
    if (!found)
      return CETTA_ACT_CORRUPT;
  }
  return CETTA_ACT_OK;
}
typedef struct {
  Source *source;
  CettaActCursor *structure;
  Cursor *rows;
  CettaNativeSha256 sha;
  size_t hash_at;
  Bytes previous;
  bool have_previous;
  unsigned phase;
} SourceValidation;
static void validation_close(SourceValidation *v) {
  if (!v) return;
  cursor_free(v->rows);cetta_act_cursor_close(v->structure);
  release(v->source);free(v->previous.data);free(v);
}
static void validation_storage(const SourceValidation *v,CettaStorageCensus *census) {
  if (!v || !cetta_storage_object(census,v,sizeof(*v),CETTA_STORAGE_BLOCK)) return;
  cetta_storage_array(census,v->previous.data,v->previous.cap,1,0);
  cursor_storage(v->rows,census);cetta_act_cursor_storage(v->structure,census);
  retained_storage(v->source,census);
}
static CettaActStatus validation_open(int fd,size_t maximum,SourceValidation **out) {
  *out=NULL;
  SourceValidation *v=calloc(1,sizeof(*v));if (!v) return CETTA_ACT_NOMEM;
  v->source=calloc(1,sizeof(*v->source));if (!v->source) {free(v);return CETTA_ACT_NOMEM;}
  Source *s=v->source;atomic_init(&s->refs,1);s->maximum=maximum;
  CettaActStatus st=cetta_act_map_fd(fd,&s->mapping);
  if (st!=CETTA_ACT_OK) goto failed;
  CettaActBytes image=cetta_act_image(s->mapping);
  if (!memcmp(image.data,"ACTree03",8)) {
    st=cetta_act_view(s->mapping,0,image.size,&s->trie);v->phase=1;
  } else if (image.size>=HEADER+DIGEST && !memcmp(image.data,"CACT0001",8)) {
    uint64_t at=le64(image.data+16),len=le64(image.data+24),body=le64(image.data+32),blen=le64(image.data+40);
    if (le64(image.data+8)!=HEADER || le64(image.data+56)!=1 || at!=HEADER ||
        len>SIZE_MAX-at || body!=at+len || body>image.size-DIGEST || blen!=image.size-DIGEST-body) {
      st=CETTA_ACT_CORRUPT;goto failed;
    }
    s->cetta=true;s->payload_at=(size_t)body;s->payload_size=(size_t)blen;s->count=le64(image.data+48);
    st=cetta_act_view(s->mapping,(size_t)at,(size_t)len,&s->trie);
    cetta_native_sha256_init(&v->sha);
  } else {st=CETTA_ACT_CORRUPT;goto failed;}
  if (st!=CETTA_ACT_OK) goto failed;
  st=cetta_act_cursor_open(s->trie,NULL,NULL,&v->structure);
  if (st!=CETTA_ACT_OK) goto failed;
  if (maximum>SIZE_MAX-32 || !cetta_act_cursor_key_limit(v->structure,maximum+32)) {st=CETTA_ACT_LIMIT;goto failed;}
  *out=v;return CETTA_ACT_OK;
failed:
  validation_close(v);return st;
}
static CettaActStatus validation_step(SourceValidation *v,uint64_t work) {
  if (!v || !work) return CETTA_ACT_PROGRESS;
  Source *s=v->source;
  if (!v->phase) {
    CettaActBytes image=cetta_act_image(s->mapping);size_t length=image.size-DIGEST;
    size_t n=length-v->hash_at<65536 ? length-v->hash_at : 65536;if (n>work) n=(size_t)work;
    cetta_native_sha256_update(&v->sha,image.data+v->hash_at,n);v->hash_at+=n;
    if (v->hash_at==length) {
      unsigned char digest[32];cetta_native_sha256_finish(&v->sha,digest);
      if (memcmp(digest,image.data+length,32)) return CETTA_ACT_CORRUPT;
      v->phase=1;
    }
    return CETTA_ACT_PROGRESS;
  }
  if (v->phase==1) {
    CettaActBytes key;uint64_t value;
    CettaActStatus st=cetta_act_cursor_next_bounded(v->structure,work,&key,&value);
    if (st==CETTA_ACT_PROGRESS) return st;
    if (st==CETTA_ACT_END) {
      st=cursor_new(s,NULL,&v->rows);if (st!=CETTA_ACT_OK) return st;
      v->phase=2;return CETTA_ACT_PROGRESS;
    }
    if (st!=CETTA_ACT_OK) return st;
    size_t n=v->previous.len<key.size ? v->previous.len : key.size;
    int order=n ? memcmp(v->previous.data,key.data,n) : 0;
    if (!order) order=(v->previous.len>key.size)-(v->previous.len<key.size);
    if (v->have_previous && order>=0) return CETTA_ACT_CORRUPT;
    v->previous.len=0;
    if (!add(&v->previous,key.data,key.size)) return CETTA_ACT_NOMEM;
    v->have_previous=true;return CETTA_ACT_PROGRESS;
  }
  if (v->phase==2) {
    CettaIndex index;SpaceOccurrenceCursorStep step=cursor_next(v->rows,&index);
    if (step==SPACE_OCCURRENCE_CURSOR_PROGRESS) return CETTA_ACT_PROGRESS;
    if (step==SPACE_OCCURRENCE_CURSOR_END) {
      if (!s->cetta) s->count=v->rows->seen;
      v->phase=3;return CETTA_ACT_OK;
    }
    if (step!=SPACE_OCCURRENCE_CURSOR_ITEM)
      return step==SPACE_OCCURRENCE_CURSOR_CAPACITY ? CETTA_ACT_NOMEM : CETTA_ACT_CORRUPT;
    if (s->cetta) {
      unsigned char key[9]={OCC};setbe(key+1,index);uint64_t record;
      CettaActStatus st=cetta_act_get(s->trie,(CettaActBytes){key,9},&record);
      if (st!=CETTA_ACT_OK) return st;
      st=check_index(s,index,record,v->rows->atom,v->rows->identity);
      if (st!=CETTA_ACT_OK) return st;
    }
    return CETTA_ACT_PROGRESS;
  }
  return CETTA_ACT_OK;
}
static CettaActStatus source_read_descriptor(int fd,size_t maximum,Source **out) {
  *out=NULL;SourceValidation *v=NULL;
  CettaActStatus status=validation_open(fd,maximum,&v);
  if (status!=CETTA_ACT_OK) return status;
  do status=validation_step(v,65536);while (status==CETTA_ACT_PROGRESS);
  if (status==CETTA_ACT_OK) {*out=v->source;retain(*out);}
  validation_close(v);return status;
}
static CettaActStatus source_read(const char *path,size_t maximum,Source **out) {
  *out=NULL;
  if (!path || !*path || sidecar_exists(path)) return CETTA_ACT_CORRUPT;
  int fd=open(path,O_RDONLY|O_CLOEXEC|O_NONBLOCK);
  if (fd<0) return CETTA_ACT_IO;
  CettaActStatus status=source_read_descriptor(fd,maximum,out);close(fd);return status;
}
CettaActStatus cetta_act_space_capture(Space *target, const char *path,
                                       size_t maximum) {
  if (!target || space_length64(target) ||
      target->match_backend.kind != SPACE_ENGINE_NATIVE ||
      target->overlay_base || space_has_dependencies(target))
    return CETTA_ACT_CORRUPT;
  Source *s = NULL;
  CettaActStatus status = source_read(path, maximum, &s);
  if (status != CETTA_ACT_OK)
    return status;
  target->match_backend.kind = SPACE_ENGINE_SOURCE;
  target->match_backend.ops = &source_ops;
  target->match_backend.source = s;
  space_read_view_invalidate(target);
  return CETTA_ACT_OK;
}

static bool native_identity(const Atom *id, unsigned char domain[16],
                            uint64_t *ordinal) {
  if (!id || id->kind != ATOM_EXPR || id->expr.len != 3 ||
      !atom_is_symbol(id->expr.elems[0], "SpaceOccurrenceIdV1"))
    return false;
  const Atom *name = id->expr.elems[1], *number = id->expr.elems[2];
  if (!name || name->kind != ATOM_GROUNDED || name->ground.gkind != GV_STRING ||
      atom_string_len(name) != 32 || !number || number->kind != ATOM_GROUNDED ||
      number->ground.gkind != GV_INT || number->ground.ival < 0)
    return false;
  for (size_t i = 0; i < 16; i++) {
    unsigned value = 0;
    for (size_t j = 0; j < 2; j++) {
      unsigned char c = (unsigned char)name->ground.sval[2 * i + j];
      if (c >= '0' && c <= '9')
        value = value * 16u + c - '0';
      else if (c >= 'a' && c <= 'f')
        value = value * 16u + c - 'a' + 10u;
      else
        return false;
    }
    domain[i] = (unsigned char)value;
  }
  *ordinal = (uint64_t)number->ground.ival;
  return true;
}
typedef struct {
  CettaByteSpan identity;
  uint64_t body,domain,native_ordinal,postings,compact;
} ExportOccurrence;
typedef struct { CettaByteSpan key; uint64_t ordinal,identity; } ExportPosting;
enum { EXPORT_CAPTURE,EXPORT_BODY_SORT,EXPORT_BODY_NEXT,EXPORT_BODY_COMPARE,
  EXPORT_BODY_COPY,EXPORT_MAP_ADD,EXPORT_MAP_SORT,EXPORT_OCCURRENCE,
  EXPORT_ID_COPY,EXPORT_POSTINGS,EXPORT_KEY_SORT,EXPORT_KEY_NEXT,
  EXPORT_KEY_COMPARE,EXPORT_KEY_BUILD,EXPORT_TRIE_FINISH,EXPORT_ASSEMBLE,
  EXPORT_VALIDATE,EXPORT_PUBLISH,EXPORT_COMPLETE };
struct CettaActSpaceExport {
  Space captured;
  bool has_capture,mork,failed,row_pending,body_added,domain_added,have_previous;
  Arena retention,scratch;
  ArenaMark empty;
  SpaceOccurrenceCursor cursor;
  CettaActPublication *publication;
  CettaByteRunSort *bodies,*offsets,*keys;
  CettaActBuilder *builder;
  SourceValidation *validation;
  int payload,occurrences,postings,trie;
  size_t maximum,memory;
  unsigned phase;
  CettaActStatus failure;
  uint64_t count,payload_end,occurrence_at,posting_at,posting_count,trie_size,copy_at,copy_end;
  uint64_t unique_offset,body_offset,domain_offset,record_offset,postings_left;
  uint64_t assembly_at,assembly_size;
  unsigned assembly_part;
  char domain[33];
  unsigned char native_domain[16],io[32768],header[HEADER];
  unsigned char *wire,*identity_wire;
  size_t wire_size,identity_size;
  CettaByteSpan body_span,domain_span,previous;
  CettaByteSortItem current;
  ExportOccurrence occurrence;
  ExportPosting posting;
  CettaNativeSha256 sha;
};
static bool export_io(int fd,void *raw,size_t n,uint64_t at,bool write) {
  if (at>INT64_MAX || n>(uint64_t)INT64_MAX-at) return false;
  unsigned char *p=raw;
  while (n) {
    ssize_t done=write ? pwrite(fd,p,n,(off_t)at) : pread(fd,p,n,(off_t)at);
    if (done<0 && errno==EINTR) continue;
    if (done<=0) return false;
    p+=done;n-=(size_t)done;at+=(uint64_t)done;
  }
  return true;
}
static CettaActStatus export_fail(CettaActSpaceExport *j,CettaActStatus s) {
  j->failed=true;j->failure=s;return s;
}
static bool export_append(CettaActSpaceExport *j,const void *p,size_t n) {
  if (!export_io(j->payload,(void *)p,n,j->payload_end,true)) return false;
  j->payload_end+=n;return true;
}
static bool export_var(CettaActSpaceExport *j,uint64_t value) {
  unsigned char bytes[9];size_t n=cetta_act_put_varint(bytes,value);
  return export_append(j,bytes,n);
}
static CettaActStatus export_sorter(CettaActSpaceExport *j,size_t budget,CettaByteRunSort **out) {
  int fds[3]={-1,-1,-1};CettaActStatus s=CETTA_ACT_OK;
  for (size_t i=0;i<3 && s==CETTA_ACT_OK;i++) s=cetta_act_publication_scratch(j->publication,&fds[i]);
  if (s==CETTA_ACT_OK) s=cetta_byte_sort_open(fds[0],fds[1],fds[2],budget,out);
  for (size_t i=0;i<3;i++) if (fds[i]>=0) close(fds[i]);
  return s;
}
void cetta_act_space_export_close(CettaActSpaceExport *j) {
  if (!j) return;
  {
    CETTA_SCOPED_SHARED_TRANSITION(cleanup);
    space_occurrence_cursor_release(&j->cursor);
    if (j->has_capture) space_free(&j->captured);
    arena_free(&j->retention);arena_free(&j->scratch);
  }
  cetta_byte_sort_close(j->bodies);cetta_byte_sort_close(j->offsets);cetta_byte_sort_close(j->keys);
  validation_close(j->validation);cetta_act_builder_close(j->builder);
  int fd[4]={j->payload,j->occurrences,j->postings,j->trie};
  for (size_t i=0;i<4;i++) if (fd[i]>=0) close(fd[i]);
  cetta_act_publication_close(j->publication);
  free(j->wire);free(j->identity_wire);free(j);
}
CettaActStatus cetta_act_space_export_open(Space *space,const char *path,bool mork,
    size_t maximum,size_t memory,CettaActSpaceExport **out) {
  if (!out) return CETTA_ACT_CORRUPT;
  *out=NULL;
  if (!space || !maximum || memory/4<cetta_act_builder_minimum_memory()) return CETTA_ACT_LIMIT;
  CettaActSpaceExport *j=calloc(1,sizeof(*j));if (!j) return CETTA_ACT_NOMEM;
  j->payload=j->occurrences=j->postings=j->trie=-1;
  j->maximum=maximum;j->memory=memory;j->mork=mork;
  arena_init_detached(&j->retention);arena_init_detached(&j->scratch);j->empty=arena_mark(&j->scratch);
  space_occurrence_cursor_init_empty(&j->cursor);
  CettaActStatus s=CETTA_ACT_OK;
  {
    CETTA_SCOPED_SHARED_TRANSITION(capture);
    if (!space_owner_is_current(space)) s=CETTA_ACT_CORRUPT;
    else if (space->match_backend.kind==SPACE_ENGINE_SOURCE) {
      space_init_with_universe(&j->captured,space->native.universe);j->has_capture=true;
      if (!space_source_snapshot_init(&j->captured,space)) s=CETTA_ACT_NOMEM;
      else space=&j->captured;
    } else if (!atom_space(&j->retention,space)) s=CETTA_ACT_NOMEM;
    if (s==CETTA_ACT_OK && !space_occurrence_cursor_init_scan(space,&j->cursor)) s=CETTA_ACT_NOMEM;
  }
  if (s==CETTA_ACT_OK) s=cetta_act_publication_open(path,&j->publication);
  if (s==CETTA_ACT_OK) s=cetta_act_publication_sidecar(j->publication,false);
  if (s==CETTA_ACT_OK) s=export_sorter(j,memory/4,&j->keys);
  if (!mork && s==CETTA_ACT_OK) s=export_sorter(j,memory/4,&j->bodies);
  if (!mork && s==CETTA_ACT_OK) s=export_sorter(j,memory/4,&j->offsets);
  int *files[4]={&j->payload,&j->occurrences,&j->postings,&j->trie};
  for (unsigned i=0;i<4 && s==CETTA_ACT_OK;i++) s=cetta_act_publication_scratch(j->publication,files[i]);
  if (s==CETTA_ACT_OK) {
    int frontier=-1,key=-1;
    s=cetta_act_publication_scratch(j->publication,&frontier);
    if (s==CETTA_ACT_OK) s=cetta_act_publication_scratch(j->publication,&key);
    if (s==CETTA_ACT_OK) s=cetta_act_builder_open(mork ? cetta_act_publication_fd(j->publication) : j->trie,
        frontier,key,memory/4,&j->builder);
    if (frontier>=0) close(frontier);
    if (key>=0) close(key);
  }
  if (s==CETTA_ACT_OK && !mork) {
    unsigned char nonce[16];int random=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
    if (random<0) s=CETTA_ACT_IO;
    else {
      size_t n=0;
      while (n<16) {
        ssize_t got=read(random,nonce+n,16-n);
        if (got<0 && errno==EINTR) continue;
        if (got<=0) {s=CETTA_ACT_IO;break;}n+=(size_t)got;
      }
      close(random);
      if (s==CETTA_ACT_OK) for (unsigned i=0;i<16;i++) snprintf(j->domain+2*i,3,"%02x",nonce[i]);
    }
  }
  if (s!=CETTA_ACT_OK) {cetta_act_space_export_close(j);return s;}
  *out=j;return CETTA_ACT_OK;
}
static CettaActStatus export_capture_row(CettaActSpaceExport *j) {
  __attribute__((cleanup(cetta_shared_transition_guard_leave))) CettaSharedTransitionGuard capture={0};
  CettaIndex index=0;Atom *atom=NULL;
  arena_reset(&j->scratch,j->empty);
  SpaceOccurrenceCursorStep step;
  if (j->cursor.source_ops) {
    /* The captured prototype and its row arena are private to this job.
     * Source reads may perform I/O; they hold no evaluator transition guard. */
    if (j->cursor.source_failure) return j->cursor.source_failure==SPACE_OCCURRENCE_CURSOR_CAPACITY ? CETTA_ACT_NOMEM : CETTA_ACT_CORRUPT;
    step=j->cursor.source_ops->next(j->cursor.source_state,&index);
    if (step==SPACE_OCCURRENCE_CURSOR_ITEM) atom=j->cursor.source_ops->atom(j->cursor.source_state,index);
    if (step==SPACE_OCCURRENCE_CURSOR_ITEM && !atom) return CETTA_ACT_CORRUPT;
  } else {
    cetta_shared_transition_guard_enter(&capture);
    step=space_occurrence_cursor_next(&j->cursor,&index,&atom);
  }
  if (step==SPACE_OCCURRENCE_CURSOR_PROGRESS) return CETTA_ACT_PROGRESS;
  if (step==SPACE_OCCURRENCE_CURSOR_END) return CETTA_ACT_END;
  if (step!=SPACE_OCCURRENCE_CURSOR_ITEM)
    return step==SPACE_OCCURRENCE_CURSOR_CAPACITY ? CETTA_ACT_NOMEM : CETTA_ACT_CORRUPT;
  if (j->count==UINT64_MAX) return CETTA_ACT_LIMIT;
  if (j->mork) {
    const char *why=NULL;
    if (!cetta_mm2_atom_to_bridge_expr_bytes(&j->scratch,atom,&j->wire,&j->wire_size,&why)) return CETTA_ACT_LIMIT;
    if (j->wire_size>j->maximum) return CETTA_ACT_LIMIT;
  } else {
    CettaAtomWireStatus w=cetta_atom_wire_encode_bounded(atom,j->maximum,&j->wire,&j->wire_size);
    if (w!=ATOM_WIRE_OK) return wire_status(w);
    Atom *id=j->cursor.source_ops ? space_occurrence_cursor_identity(&j->cursor,index,&j->scratch) : NULL;
    if (!j->cursor.source_ops || !j->cursor.source_ops->identity || (id && id->kind==ATOM_EXPR && id->expr.len &&
        atom_is_symbol(id->expr.elems[0],"LocalOccurrence"))) {
      if (j->count>INT64_MAX) return CETTA_ACT_LIMIT;
      id=atom_expr3(&j->scratch,atom_symbol(&j->scratch,"SpaceOccurrenceIdV1"),
          atom_string(&j->scratch,j->domain),atom_int(&j->scratch,(int64_t)j->count));
    }
    if (!id || atom_has_vars(id)) return CETTA_ACT_CORRUPT;
    j->occurrence=(ExportOccurrence){0};
    j->occurrence.compact=native_identity(id,j->native_domain,&j->occurrence.native_ordinal);
    w=cetta_atom_wire_encode_bounded(id,j->maximum,&j->identity_wire,&j->identity_size);
    if (w!=ATOM_WIRE_OK) return wire_status(w);
  }
  j->row_pending=true;return CETTA_ACT_OK;
}
static CettaActStatus export_posting_key(CettaActSpaceExport *j,CettaActBytes key,bool identity) {
  CettaByteSpan span;CettaActStatus s=cetta_byte_sort_key(j->keys,key,&span);
  if (s!=CETTA_ACT_OK) return s;
  ExportPosting p={span,j->count,identity};
  uint64_t at;
  if (__builtin_mul_overflow(j->posting_at,(uint64_t)sizeof(p),&at) ||
      !export_io(j->postings,&p,sizeof(p),at,true)) return CETTA_ACT_IO;
  ++j->posting_at;++j->posting_count;++j->occurrence.postings;return CETTA_ACT_OK;
}
static CettaActStatus export_capture_postings(CettaActSpaceExport *j) {
  unsigned char occurrence[9]={OCC},ordinal[8];setbe(occurrence+1,j->count);setbe(ordinal,j->count);
  CettaActStatus s=export_posting_key(j,(CettaActBytes){occurrence,9},false);
  Bytes key={0};unsigned char ns=ID;
  if (s==CETTA_ACT_OK && (!add(&key,&ns,1) || !add(&key,j->identity_wire,j->identity_size))) s=CETTA_ACT_NOMEM;
  if (s==CETTA_ACT_OK) {
    CettaByteSortInfo info;cetta_byte_sort_info(j->keys,&info);
    j->occurrence.identity=(CettaByteSpan){info.key_bytes+1,j->identity_size};
    s=export_posting_key(j,view(&key),true);
  }
  free(key.data);
  if (s!=CETTA_ACT_OK) return s;
  arena_reset(&j->scratch,j->empty);Atom *atom=NULL;
  CettaAtomWireStatus w=cetta_atom_wire_decode_bounded(&j->scratch,j->wire,j->wire_size,j->maximum,&atom);
  if (w!=ATOM_WIRE_OK) return wire_status(w);
  if (atom->kind==ATOM_VAR) {
    unsigned char k[9]={OPEN};memcpy(k+1,ordinal,8);
    s=export_posting_key(j,(CettaActBytes){k,9},false);
  } else if (atom->kind==ATOM_EXPR) {
    for (size_t p=0;p<atom->expr.len && p<3 && s==CETTA_ACT_OK;p++) {
      key=(Bytes){0};
      if (!field_prefix(&key,atom->expr.len,p,atom->expr.elems[p],j->maximum) || !add(&key,ordinal,8)) s=CETTA_ACT_NOMEM;
      else s=export_posting_key(j,view(&key),false);
      free(key.data);
    }
  }
  return s;
}
static CettaActStatus export_sort_work(CettaByteRunSort *sorter,uint64_t work) {
  CettaActStatus s=cetta_byte_sort_step(sorter,work);
  return s==CETTA_ACT_OK ? CETTA_ACT_PROGRESS : s;
}
static CettaActStatus export_capture(CettaActSpaceExport *j,uint64_t work) {
  CettaActStatus s=CETTA_ACT_OK;
  if (!j->row_pending) {
    s=export_capture_row(j);
    if (s==CETTA_ACT_END) {
      j->posting_at=0;
      j->phase=j->mork ? EXPORT_KEY_SORT : EXPORT_BODY_SORT;
      s=cetta_byte_sort_seal(j->mork ? j->keys : j->bodies);
      return s==CETTA_ACT_OK ? CETTA_ACT_PROGRESS : s;
    }
    if (s!=CETTA_ACT_OK) return s;
  }
  CettaByteRunSort *sorter=j->mork ? j->keys : j->bodies;
  if (!j->body_added) {
    if (!j->body_span.size) s=cetta_byte_sort_key(sorter,(CettaActBytes){j->wire,j->wire_size},&j->body_span);
    if (s==CETTA_ACT_OK) s=cetta_byte_sort_add(sorter,j->body_span,j->body_span.offset);
    if (s==CETTA_ACT_PROGRESS) return export_sort_work(sorter,work);
    if (s!=CETTA_ACT_OK) return s;
    j->body_added=true;j->occurrence.body=j->body_span.offset;
  }
  if (!j->mork) {
    if (j->occurrence.compact && !j->domain_added) {
      if (!j->domain_span.size) s=cetta_byte_sort_key(j->bodies,(CettaActBytes){j->native_domain,16},&j->domain_span);
      if (s==CETTA_ACT_OK) s=cetta_byte_sort_add(j->bodies,j->domain_span,j->domain_span.offset);
      if (s==CETTA_ACT_PROGRESS) return export_sort_work(j->bodies,work);
      if (s!=CETTA_ACT_OK) return s;
      j->domain_added=true;j->occurrence.domain=j->domain_span.offset;
    }
    s=export_capture_postings(j);
    if (s!=CETTA_ACT_OK) return s;
    uint64_t at;
    if (__builtin_mul_overflow(j->count,(uint64_t)sizeof(j->occurrence),&at) ||
        !export_io(j->occurrences,&j->occurrence,sizeof(j->occurrence),at,true)) return CETTA_ACT_IO;
  }
  free(j->wire);j->wire=NULL;j->wire_size=0;free(j->identity_wire);j->identity_wire=NULL;j->identity_size=0;
  j->body_span=j->domain_span=(CettaByteSpan){0};j->row_pending=j->body_added=j->domain_added=false;
  ++j->count;return CETTA_ACT_PROGRESS;
}
static CettaActStatus export_mapping(CettaActSpaceExport *j,uint64_t reference,uint64_t *offset) {
  CettaByteSortItem item;CettaActStatus s=cetta_byte_sort_next(j->offsets,&item);
  if (s!=CETTA_ACT_OK) return s==CETTA_ACT_END ? CETTA_ACT_CORRUPT : s;
  unsigned char key[8];size_t n=0;
  s=cetta_byte_sort_read(j->offsets,item.key,0,key,8,&n);
  if (s!=CETTA_ACT_OK) return s;
  if (item.key.size!=8 || n!=8 || be64(key)!=reference) return CETTA_ACT_CORRUPT;
  *offset=item.value;return CETTA_ACT_OK;
}
CettaActStatus cetta_act_space_export_step(CettaActSpaceExport *j,uint64_t work) {
  if (!j) return CETTA_ACT_CORRUPT;
  if (j->failed) return j->failure;
  if (j->phase==EXPORT_COMPLETE) return CETTA_ACT_OK;
  if (!work) return CETTA_ACT_PROGRESS;
  CettaActStatus s=CETTA_ACT_OK;size_t n;int order;
  switch (j->phase) {
  case EXPORT_CAPTURE:s=export_capture(j,work);break;
  case EXPORT_BODY_SORT:case EXPORT_MAP_SORT:case EXPORT_KEY_SORT: {
    CettaByteRunSort *sorter=j->phase==EXPORT_BODY_SORT ? j->bodies : j->phase==EXPORT_MAP_SORT ? j->offsets : j->keys;
    s=cetta_byte_sort_step(sorter,work);
    if (s==CETTA_ACT_OK) {j->phase=j->phase==EXPORT_BODY_SORT ? EXPORT_BODY_NEXT : j->phase==EXPORT_MAP_SORT ? EXPORT_OCCURRENCE : EXPORT_KEY_NEXT;j->have_previous=false;}
    break;
  }
  case EXPORT_BODY_NEXT:
    s=cetta_byte_sort_next(j->bodies,&j->current);
    if (s==CETTA_ACT_END) {s=cetta_byte_sort_seal(j->offsets);j->phase=EXPORT_MAP_SORT;}
    else if (s==CETTA_ACT_OK) j->phase=EXPORT_BODY_COMPARE;
    break;
  case EXPORT_BODY_COMPARE:
    if (j->have_previous) {
      s=cetta_byte_sort_compare(j->bodies,j->previous,j->current.key,work,&order);
      if (s!=CETTA_ACT_OK) break;
    } else order=1;
    if (!order) j->phase=EXPORT_MAP_ADD;
    else {
      j->unique_offset=j->payload_end;
      if (!export_var(j,j->current.key.size)) {s=CETTA_ACT_IO;break;}
      j->copy_at=0;j->copy_end=j->current.key.size;j->phase=EXPORT_BODY_COPY;
    }
    break;
  case EXPORT_BODY_COPY:
    n=j->copy_end-j->copy_at<sizeof(j->io) ? (size_t)(j->copy_end-j->copy_at) : sizeof(j->io);
    if (n>work) n=(size_t)work;
    s=cetta_byte_sort_read(j->bodies,j->current.key,j->copy_at,j->io,n,&n);
    if (s==CETTA_ACT_OK && !export_append(j,j->io,n)) s=CETTA_ACT_IO;
    if (s==CETTA_ACT_OK && (j->copy_at+=n)==j->copy_end) j->phase=EXPORT_MAP_ADD;
    break;
  case EXPORT_MAP_ADD: {
    unsigned char key[8];setbe(key,j->current.value);
    s=cetta_byte_sort_put(j->offsets,(CettaActBytes){key,8},j->unique_offset);
    if (s==CETTA_ACT_PROGRESS) s=export_sort_work(j->offsets,work);
    else if (s==CETTA_ACT_OK) {j->previous=j->current.key;j->have_previous=true;j->phase=EXPORT_BODY_NEXT;}
    break;
  }
  case EXPORT_OCCURRENCE:
    if (j->occurrence_at==j->count) {
      CettaByteSortItem unused;
      if (cetta_byte_sort_next(j->offsets,&unused)!=CETTA_ACT_END) {s=CETTA_ACT_CORRUPT;break;}
      s=cetta_byte_sort_seal(j->keys);j->phase=EXPORT_KEY_SORT;break;
    }
    if (!export_io(j->occurrences,&j->occurrence,sizeof(j->occurrence),j->occurrence_at*sizeof(j->occurrence),false)) {s=CETTA_ACT_IO;break;}
    s=export_mapping(j,j->occurrence.body,&j->body_offset);
    if (s==CETTA_ACT_OK && j->occurrence.compact) s=export_mapping(j,j->occurrence.domain,&j->domain_offset);
    if (s!=CETTA_ACT_OK) break;
    j->record_offset=j->payload_end;j->postings_left=j->occurrence.postings;
    if (j->occurrence.compact) {
      if (!export_var(j,0) || !export_var(j,j->domain_offset) || !export_var(j,j->occurrence.native_ordinal) || !export_var(j,j->body_offset)) s=CETTA_ACT_IO;
      else j->phase=EXPORT_POSTINGS;
    } else {
      if (!export_var(j,j->occurrence.identity.size)) {s=CETTA_ACT_IO;break;}
      j->copy_at=0;j->copy_end=j->occurrence.identity.size;j->phase=EXPORT_ID_COPY;
    }
    break;
  case EXPORT_ID_COPY:
    n=j->copy_end-j->copy_at<sizeof(j->io) ? (size_t)(j->copy_end-j->copy_at) : sizeof(j->io);if (n>work) n=(size_t)work;
    s=cetta_byte_sort_read(j->keys,j->occurrence.identity,j->copy_at,j->io,n,&n);
    if (s==CETTA_ACT_OK && !export_append(j,j->io,n)) s=CETTA_ACT_IO;
    if (s==CETTA_ACT_OK && (j->copy_at+=n)==j->copy_end) {
      unsigned char offset[8];setle(offset,j->body_offset);
      if (!export_append(j,offset,8)) s=CETTA_ACT_IO;else j->phase=EXPORT_POSTINGS;
    }
    break;
  case EXPORT_POSTINGS:
    if (!j->postings_left) {++j->occurrence_at;j->phase=EXPORT_OCCURRENCE;break;}
    if (!export_io(j->postings,&j->posting,sizeof(j->posting),j->posting_at*sizeof(j->posting),false)) {s=CETTA_ACT_IO;break;}
    if (j->posting.ordinal!=j->occurrence_at) {s=CETTA_ACT_CORRUPT;break;}
    s=cetta_byte_sort_add(j->keys,j->posting.key,j->posting.identity ? j->occurrence_at : j->record_offset);
    if (s==CETTA_ACT_PROGRESS) s=export_sort_work(j->keys,work);
    else if (s==CETTA_ACT_OK) {++j->posting_at;--j->postings_left;}
    break;
  case EXPORT_KEY_NEXT:
    s=cetta_byte_sort_next(j->keys,&j->current);
    if (s==CETTA_ACT_END) {s=CETTA_ACT_OK;j->phase=EXPORT_TRIE_FINISH;}
    else if (s==CETTA_ACT_OK) j->phase=EXPORT_KEY_COMPARE;
    break;
  case EXPORT_KEY_COMPARE:
    if (j->have_previous) {
      s=cetta_byte_sort_compare(j->keys,j->previous,j->current.key,work,&order);
      if (s!=CETTA_ACT_OK) break;
    } else order=1;
    if (!order) {if (j->mork) j->phase=EXPORT_KEY_NEXT;else s=CETTA_ACT_CORRUPT;}
    else j->phase=EXPORT_KEY_BUILD;
    break;
  case EXPORT_KEY_BUILD:
    s=cetta_act_builder_put_fd(j->builder,cetta_byte_sort_key_fd(j->keys),j->current.key.offset,j->current.key.size,j->mork ? 0 : j->current.value);
    if (s==CETTA_ACT_OK) {j->previous=j->current.key;j->have_previous=true;j->phase=EXPORT_KEY_NEXT;}
    break;
  case EXPORT_TRIE_FINISH:
    s=cetta_act_builder_finish_step(j->builder,work,&j->trie_size);
    if (s==CETTA_ACT_OK) {
      if (j->mork) j->phase=EXPORT_VALIDATE;
      else {
        memcpy(j->header,"CACT0001",8);setle(j->header+8,HEADER);setle(j->header+16,HEADER);
        setle(j->header+24,j->trie_size);setle(j->header+32,HEADER+j->trie_size);
        setle(j->header+40,j->payload_end);setle(j->header+48,j->count);setle(j->header+56,1);
        cetta_native_sha256_init(&j->sha);j->phase=EXPORT_ASSEMBLE;
      }
    }
    break;
  case EXPORT_ASSEMBLE: {
    int fd=cetta_act_publication_fd(j->publication);
    if (!j->assembly_part) {
      if (!export_io(fd,j->header,HEADER,0,true)) {s=CETTA_ACT_IO;break;}
      cetta_native_sha256_update(&j->sha,j->header,HEADER);j->assembly_part=1;j->assembly_at=0;j->assembly_size=HEADER;
    } else if (j->assembly_part<3) {
      uint64_t length=j->assembly_part==1 ? j->trie_size : j->payload_end;
      n=length-j->assembly_at<sizeof(j->io) ? (size_t)(length-j->assembly_at) : sizeof(j->io);if (n>work) n=(size_t)work;
      if (!export_io(j->assembly_part==1 ? j->trie : j->payload,j->io,n,j->assembly_at,false) ||
          !export_io(fd,j->io,n,j->assembly_size,true)) {s=CETTA_ACT_IO;break;}
      cetta_native_sha256_update(&j->sha,j->io,n);j->assembly_at+=n;j->assembly_size+=n;
      if (j->assembly_at==length) {++j->assembly_part;j->assembly_at=0;}
    } else {
      unsigned char digest[32];cetta_native_sha256_finish(&j->sha,digest);
      if (!export_io(fd,digest,32,j->assembly_size,true)) s=CETTA_ACT_IO;
      else {j->assembly_size+=32;j->phase=EXPORT_VALIDATE;}
    }
    break;
  }
  case EXPORT_VALIDATE: {
    if (!j->validation) {
      s=validation_open(cetta_act_publication_fd(j->publication),j->maximum,&j->validation);
      if (s!=CETTA_ACT_OK) break;
    }
    s=validation_step(j->validation,work);
    if (s==CETTA_ACT_OK) j->phase=EXPORT_PUBLISH;
    break;
  }
  case EXPORT_PUBLISH:
    s=cetta_act_publication_commit(j->publication);
    if (s==CETTA_ACT_OK || s==CETTA_ACT_UNCERTAIN) {
      CettaActStatus cleanup=cetta_act_publication_sidecar(j->publication,true);
      if (cleanup!=CETTA_ACT_OK) s=CETTA_ACT_UNCERTAIN;
    }
    if (s==CETTA_ACT_OK) {j->phase=EXPORT_COMPLETE;return CETTA_ACT_OK;}
    break;
  default:s=CETTA_ACT_CORRUPT;
  }
  if (s==CETTA_ACT_OK || s==CETTA_ACT_PROGRESS) return CETTA_ACT_PROGRESS;
  return export_fail(j,s);
}
bool cetta_act_space_export_info(const CettaActSpaceExport *j,CettaActSpaceExportInfo *out) {
  if (!j || !out) return false;
  *out=(CettaActSpaceExportInfo){.occurrences=j->count,.payload_bytes=j->payload_end,
    .trie_bytes=j->trie_size,.heap_bytes=sizeof(*j)+j->wire_size+j->identity_size,
    .phase=j->phase,.complete=j->phase==EXPORT_COMPLETE};
  CettaByteRunSort *sorters[3]={j->keys,j->bodies,j->offsets};
  for (size_t i=0;i<3;i++) {
    CettaByteSortInfo info;
    if (cetta_byte_sort_info(sorters[i],&info)) {out->heap_bytes+=info.heap_bytes;out->temporary_bytes+=info.key_bytes+info.run_bytes;}
  }
  CettaActBuildInfo builder;
  if (cetta_act_builder_info(j->builder,&builder)) {out->heap_bytes+=builder.heap_bytes;out->temporary_bytes+=builder.output_bytes+builder.scratch_bytes;}
  out->temporary_bytes+=j->payload_end+j->count*sizeof(ExportOccurrence)+j->posting_count*sizeof(ExportPosting)+j->assembly_size;
  CettaStorageCensus extra={0};arena_storage_census(&j->scratch,&extra);
  validation_storage(j->validation,&extra);cetta_storage_finish(&extra);
  bool valid=!extra.incomplete && !extra.overflow && extra.bytes<=SIZE_MAX-out->heap_bytes;
  if (valid) out->heap_bytes+=extra.bytes;
  cetta_storage_census_free(&extra);return valid;
}
void cetta_act_space_export_storage(const CettaActSpaceExport *j,CettaStorageCensus *census) {
  if (!j || !cetta_storage_object(census,j,sizeof(*j),CETTA_STORAGE_BLOCK)) return;
  arena_storage_census(&j->retention,census);arena_storage_census(&j->scratch,census);
  if (j->has_capture) space_storage_census(&j->captured,census);
  cetta_storage_array(census,j->wire,j->wire_size,1,0);cetta_storage_array(census,j->identity_wire,j->identity_size,1,0);
  cetta_byte_sort_storage(j->keys,census);cetta_byte_sort_storage(j->bodies,census);cetta_byte_sort_storage(j->offsets,census);
  cetta_act_builder_storage(j->builder,census);validation_storage(j->validation,census);
}
CettaActStatus cetta_act_space_dump(Space *space,const char *path,bool mork,size_t maximum) {
  CettaActSpaceExport *job=NULL;
  CettaActStatus status=cetta_act_space_export_open(space,path,mork,maximum,32u*1024u*1024u,&job);
  if (status!=CETTA_ACT_OK) return status;
  do status=cetta_act_space_export_step(job,65536);while (status==CETTA_ACT_PROGRESS);
  cetta_act_space_export_close(job);return status;
}

/* Whole-Space construction uses external sorting and sequential offset joins. */
CettaActStatus cetta_act_space_load(Space *target, Arena *fallback,
                                    const char *path, size_t maximum) {
  if (!target ||
      (target->match_backend.kind != SPACE_ENGINE_SOURCE &&
       ((target->match_backend.kind != SPACE_ENGINE_NATIVE &&
         target->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) ||
        !target->native.universe)))
    return CETTA_ACT_CORRUPT;
  if (target->match_backend.kind == SPACE_ENGINE_SOURCE &&
      (!target->match_backend.ops || !target->match_backend.ops->source_edits))
    return CETTA_ACT_READ_ONLY;
  Source *source = NULL;
  CettaActStatus status = source_read(path, maximum, &source);
  if (status != CETTA_ACT_OK)
    return status;
  Cursor *cursor = NULL;
  status = cursor_new(source, NULL, &cursor);
  release(source);
  if (status != CETTA_ACT_OK)
    return status;
  Arena values;
  arena_init_detached(&values);
  Atom **atoms = NULL;
  size_t len = 0, cap = 0;
  CettaIndex index;
  SpaceOccurrenceCursorStep step;
  while ((step = cursor_next(cursor, &index)) == SPACE_OCCURRENCE_CURSOR_ITEM) {
    if (!grow((void **)&atoms, &cap, len + 1, sizeof(*atoms)) ||
        !(atoms[len] = atom_deep_copy(&values, cursor->atom))) {
      status = CETTA_ACT_NOMEM;
      break;
    }
    len++;
  }
  if (status == CETTA_ACT_OK && step != SPACE_OCCURRENCE_CURSOR_END)
    status = CETTA_ACT_CORRUPT;
  if (status == CETTA_ACT_OK && !space_admit_atoms_batch_from_source_arena(
                                    target, fallback, &values, atoms, len))
    status = CETTA_ACT_NOMEM;
  free(atoms);
  arena_free(&values);
  cursor_free(cursor);
  return status;
}
