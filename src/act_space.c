#define _POSIX_C_SOURCE 200809L
#include "act_space.h"
#include "atom_wire.h"
#include "mm2_lower.h"
#include "native_sha256.h"
#include "space_source_overlay.h"
#include "storage_census.h"
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
static bool var(Bytes *b, uint64_t n) {
  unsigned char p[9];
  size_t size = cetta_act_put_varint(p, n);
  return add(b, p, size);
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
static const SpaceSourceCursorOps cursor_ops = {.clone = cursor_clone,
                                                .select = cursor_select,
                                                .next = cursor_next,
                                                .atom = cursor_atom,
                                                .identity = cursor_identity,
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
static CettaActStatus source_read(const char *path, size_t maximum,
                                  Source **out) {
  *out = NULL;
  if (!path || !*path)
    return CETTA_ACT_CORRUPT;
  if (sidecar_exists(path))
    return CETTA_ACT_CORRUPT;
  Source *s = calloc(1, sizeof(*s));
  if (!s)
    return CETTA_ACT_NOMEM;
  atomic_init(&s->refs, 1);
  s->maximum = maximum;
  CettaActStatus st = cetta_act_map(path, &s->mapping);
  if (st != CETTA_ACT_OK) {
    release(s);
    return st;
  }
  CettaActBytes image = cetta_act_image(s->mapping);
  if (!memcmp(image.data, "ACTree03", 8)) {
    st = cetta_act_view(s->mapping, 0, image.size, &s->trie);
  } else if (image.size >= HEADER + DIGEST &&
             !memcmp(image.data, "CACT0001", 8)) {
    if (le64(image.data + 8) != HEADER || le64(image.data + 56) != 1) {
      st = CETTA_ACT_CORRUPT;
      goto failed;
    }
    uint64_t at = le64(image.data + 16), len = le64(image.data + 24),
             body = le64(image.data + 32), blen = le64(image.data + 40);
    if (at != HEADER || len > SIZE_MAX - at || body != at + len ||
        body > image.size - DIGEST || blen != image.size - DIGEST - body) {
      st = CETTA_ACT_CORRUPT;
      goto failed;
    }
    CettaNativeSha256 sha;
    unsigned char digest[32];
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, image.data, image.size - DIGEST);
    cetta_native_sha256_finish(&sha, digest);
    if (memcmp(digest, image.data + image.size - DIGEST, DIGEST)) {
      st = CETTA_ACT_CORRUPT;
      goto failed;
    }
    s->cetta = true;
    s->payload_at = (size_t)body;
    s->payload_size = (size_t)blen;
    s->count = le64(image.data + 48);
    st = cetta_act_view(s->mapping, (size_t)at, (size_t)len, &s->trie);
  } else
    st = CETTA_ACT_CORRUPT;
  if (st != CETTA_ACT_OK)
    goto failed;
  st = cetta_act_validate(s->trie, NULL);
  if (st != CETTA_ACT_OK)
    goto failed;
  Cursor *c = NULL;
  st = cursor_new(s, NULL, &c);
  if (st != CETTA_ACT_OK)
    goto failed;
  CettaIndex index;
  SpaceOccurrenceCursorStep step;
  while ((step = cursor_next(c, &index)) == SPACE_OCCURRENCE_CURSOR_ITEM) {
    if (s->cetta) {
      unsigned char key[9] = {OCC};
      setbe(key + 1, index);
      uint64_t record;
      st = cetta_act_get(s->trie, (CettaActBytes){key, 9}, &record);
      if (st == CETTA_ACT_OK)
        st = check_index(s, index, record, c->atom, c->identity);
      if (st != CETTA_ACT_OK)
        break;
    }
  }
  if (st == CETTA_ACT_OK && step != SPACE_OCCURRENCE_CURSOR_END)
    st = CETTA_ACT_CORRUPT;
  if (!s->cetta)
    s->count = c->seen;
  cursor_free(c);
  if (st != CETTA_ACT_OK)
    goto failed;
  *out = s;
  return CETTA_ACT_OK;
failed:
  release(s);
  return st;
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

typedef struct {
  CettaActItem *items;
  size_t len, cap;
} Items;
static bool item(Items *v, CettaActBytes prefix, const unsigned char *suffix,
                 size_t n, uint64_t value) {
  if (n > SIZE_MAX - prefix.size ||
      !grow((void **)&v->items, &v->cap, v->len + 1, sizeof(*v->items)))
    return false;
  unsigned char *p = malloc(prefix.size + n ? prefix.size + n : 1);
  if (!p)
    return false;
  if (prefix.size)
    memcpy(p, prefix.data, prefix.size);
  if (n)
    memcpy(p + prefix.size, suffix, n);
  v->items[v->len++] = (CettaActItem){{p, prefix.size + n}, value};
  return true;
}
static int item_compare(const void *a, const void *b) {
  CettaActBytes x = ((const CettaActItem *)a)->key,
                y = ((const CettaActItem *)b)->key;
  size_t n = x.size < y.size ? x.size : y.size;
  int c = n ? memcmp(x.data, y.data, n) : 0;
  return c ? c : (x.size > y.size) - (x.size < y.size);
}
typedef struct {
  unsigned char *wire;
  size_t size, offset;
  uint64_t hash;
} Payload;
static bool payload(Items *unused, Payload **table, size_t *capacity,
                    size_t *used, Bytes *out, unsigned char *wire, size_t size,
                    size_t *offset) {
  (void)unused;
  uint64_t hash = UINT64_C(1469598103934665603);
  for (size_t i = 0; i < size; i++)
    hash = (hash ^ wire[i]) * UINT64_C(1099511628211);
  if (!*capacity || *used >= *capacity / 2) {
    size_t cap = *capacity ? *capacity * 2 : 32;
    if (cap < *capacity || cap > SIZE_MAX / sizeof(Payload)) {
      free(wire);
      return false;
    }
    Payload *next = calloc(cap, sizeof(*next));
    if (!next) {
      free(wire);
      return false;
    }
    for (size_t i = 0; i < *capacity; i++)
      if ((*table)[i].wire) {
        size_t at = (*table)[i].hash & (cap - 1);
        while (next[at].wire)
          at = (at + 1) & (cap - 1);
        next[at] = (*table)[i];
      }
    free(*table);
    *table = next;
    *capacity = cap;
  }
  size_t at = hash & (*capacity - 1);
  while ((*table)[at].wire) {
    Payload *p = &(*table)[at];
    if (p->hash == hash && p->size == size && !memcmp(p->wire, wire, size)) {
      *offset = p->offset;
      free(wire);
      return true;
    }
    at = (at + 1) & (*capacity - 1);
  }
  *offset = out->len;
  if (!var(out, size) || !add(out, wire, size)) {
    free(wire);
    return false;
  }
  (*table)[at] = (Payload){wire, size, *offset, hash};
  (*used)++;
  return true;
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
/* The old sidecar protocol is deliberately refused on reads. Remove a stale
 * sidecar only after publishing the new self-contained image: a concurrent
 * reader may refuse briefly, but cannot accept the old lossy image as new. */
static CettaActStatus publish_snapshot(const char *path, CettaActBytes bytes) {
  if (!path)
    return CETTA_ACT_CORRUPT;
  size_t n = strlen(path);
  if (n > SIZE_MAX - 8)
    return CETTA_ACT_LIMIT;
  char *sidecar = malloc(n + 8), *directory = strdup(path);
  if (!sidecar || !directory) {
    free(sidecar);
    free(directory);
    return CETTA_ACT_NOMEM;
  }
  memcpy(sidecar, path, n);
  memcpy(sidecar + n, ".copies", 8);
  struct stat st;
  bool stale = lstat(sidecar, &st) == 0;
  CettaActStatus result = CETTA_ACT_IO;
  if ((stale && !S_ISREG(st.st_mode)) || (!stale && errno != ENOENT))
    goto done;
  result = cetta_act_publish(path, bytes);
  if (stale && (result == CETTA_ACT_OK || result == CETTA_ACT_UNCERTAIN)) {
    if (unlink(sidecar)) {
      result = CETTA_ACT_UNCERTAIN;
      goto done;
    }
    char *slash = strrchr(directory, '/');
    if (slash) {
      if (slash == directory)
        slash[1] = 0;
      else
        *slash = 0;
    } else {
      directory[0] = '.';
      directory[1] = 0;
    }
    int fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0 || fsync(fd))
      result = CETTA_ACT_UNCERTAIN;
    if (fd >= 0)
      close(fd);
  }
done:
  free(sidecar);
  free(directory);
  return result;
}
CettaActStatus cetta_act_space_dump(Space *space, const char *path, bool mork,
                                    size_t maximum) {
  if (!space)
    return CETTA_ACT_CORRUPT;
  SpaceOccurrenceCursor cursor;
  space_occurrence_cursor_init_empty(&cursor);
  if (!space_occurrence_cursor_init_scan(space, &cursor))
    return CETTA_ACT_NOMEM;
  Arena scratch;
  arena_init_detached(&scratch);
  ArenaMark mark = arena_mark(&scratch);
  Items items = {0};
  Bytes bodies = {0};
  Payload *table = NULL;
  size_t cap = 0, used = 0;
  uint64_t count = 0;
  CettaActStatus status = CETTA_ACT_OK;
  CettaIndex index;
  Atom *atom;
  SpaceOccurrenceCursorStep step;
  /* A new domain is needed only for positional in-memory occurrences. */
  char domain[33];
  if (!mork) {
    unsigned char nonce[16];
    int random = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (random < 0) {
      status = CETTA_ACT_IO;
      goto done;
    }
    size_t at = 0;
    while (at < sizeof(nonce)) {
      ssize_t n = read(random, nonce + at, sizeof(nonce) - at);
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0) {
        close(random);
        status = CETTA_ACT_IO;
        goto done;
      }
      at += (size_t)n;
    }
    close(random);
    for (unsigned i = 0; i < 16; i++)
      snprintf(domain + 2 * i, 3, "%02x", nonce[i]);
  }
  while ((step = space_occurrence_cursor_next(&cursor, &index, &atom)) ==
         SPACE_OCCURRENCE_CURSOR_ITEM) {
    arena_reset(&scratch, mark);
    unsigned char *wire = NULL;
    size_t size = 0;
    const char *why = NULL;
    if (mork) {
      if (!cetta_mm2_atom_to_bridge_expr_bytes(&scratch, atom, &wire, &size,
                                               &why)) {
        status = CETTA_ACT_LIMIT;
        goto done;
      }
      bool ok = item(&items, (CettaActBytes){wire, size}, NULL, 0, 0);
      free(wire);
      if (!ok) {
        status = CETTA_ACT_NOMEM;
        goto done;
      }
    } else {
      CettaAtomWireStatus w =
          cetta_atom_wire_encode_bounded(atom, maximum, &wire, &size);
      if (w != ATOM_WIRE_OK) {
        status = wire_status(w);
        goto done;
      }
      unsigned char ns = ID;
      bool ok;
      size_t body;
      if (!payload(&items, &table, &cap, &used, &bodies, wire, size, &body)) {
        status = CETTA_ACT_NOMEM;
        goto done;
      }
      if (!cursor.source_ops && count > INT64_MAX) {
        status = CETTA_ACT_LIMIT;
        goto done;
      }
      Atom *id =
          cursor.source_ops
              ? space_occurrence_cursor_identity(&cursor, index, &scratch)
              : atom_expr3(&scratch,
                           atom_symbol(&scratch, "SpaceOccurrenceIdV1"),
                           atom_string(&scratch, domain),
                           atom_int(&scratch, (int64_t)count));
      /* Overlay additions have process-local coordinates, not persistent
       * identities. A checkpoint gives them a fresh persistent domain;
       * identities inherited from the captured base remain unchanged. */
      if (id && atom_head_symbol_id(id) != SYMBOL_ID_NONE &&
          id->kind == ATOM_EXPR && id->expr.len &&
          atom_is_symbol(id->expr.elems[0], "LocalOccurrence")) {
        if (count > INT64_MAX) {
          status = CETTA_ACT_LIMIT;
          goto done;
        }
        id = atom_expr3(&scratch, atom_symbol(&scratch, "SpaceOccurrenceIdV1"),
                        atom_string(&scratch, domain),
                        atom_int(&scratch, (int64_t)count));
      }
      if (!id || atom_has_vars(id)) {
        status = CETTA_ACT_CORRUPT;
        goto done;
      }
      unsigned char *idwire = NULL;
      size_t idsize;
      w = cetta_atom_wire_encode_bounded(id, maximum, &idwire, &idsize);
      if (w != ATOM_WIRE_OK) {
        status = wire_status(w);
        goto done;
      }
      unsigned char native_domain[16];
      uint64_t native_ordinal = 0;
      bool compact = native_identity(id, native_domain, &native_ordinal);
      size_t domain_offset = 0;
      if (compact) {
        unsigned char *copy = malloc(16);
        if (copy)
          memcpy(copy, native_domain, 16);
        if (!copy || !payload(&items, &table, &cap, &used, &bodies, copy, 16,
                              &domain_offset)) {
          free(idwire);
          status = CETTA_ACT_NOMEM;
          goto done;
        }
      }
      size_t record = bodies.len;
      unsigned char body_offset[8], ordinal[8], occurrence[9] = {OCC};
      setle(body_offset, body);
      setbe(ordinal, count);
      setbe(occurrence + 1, count);
      ok = compact ? var(&bodies, 0) && var(&bodies, domain_offset) &&
                         var(&bodies, native_ordinal) && var(&bodies, body)
                   : var(&bodies, idsize) && add(&bodies, idwire, idsize) &&
                         add(&bodies, body_offset, 8);
      ok = ok && item(&items, (CettaActBytes){occurrence, 9}, NULL, 0, record);
      ns = ID;
      Bytes idkey = {0};
      ok = ok && add(&idkey, &ns, 1) && add(&idkey, idwire, idsize) &&
           item(&items, view(&idkey), NULL, 0, count);
      free(idkey.data);
      free(idwire);
      if (atom->kind == ATOM_VAR) {
        ns = OPEN;
        ok = ok && item(&items, (CettaActBytes){&ns, 1}, ordinal, 8, record);
      } else if (atom->kind == ATOM_EXPR)
        for (size_t p = 0; ok && p < atom->expr.len && p < 3; p++) {
          Bytes key = {0};
          ok = field_prefix(&key, atom->expr.len, p, atom->expr.elems[p],
                            maximum) &&
               item(&items, view(&key), ordinal, 8, record);
          free(key.data);
        }
      if (!ok) {
        status = CETTA_ACT_NOMEM;
        goto done;
      }
    }
    if (count == UINT64_MAX) {
      status = CETTA_ACT_LIMIT;
      goto done;
    }
    count++;
  }
  if (step != SPACE_OCCURRENCE_CURSOR_END) {
    status = step == SPACE_OCCURRENCE_CURSOR_CAPACITY ? CETTA_ACT_NOMEM
                                                      : CETTA_ACT_CORRUPT;
    goto done;
  }
  qsort(items.items, items.len, sizeof(*items.items), item_compare);
  size_t unique = 0;
  for (size_t i = 0; i < items.len; i++) {
    if (unique &&
        item_compare(&items.items[unique - 1], &items.items[i]) == 0) {
      if (!mork) {
        status = CETTA_ACT_CORRUPT;
        goto done;
      }
      free((void *)items.items[i].key.data);
      items.items[i].key.data = NULL;
      continue;
    }
    items.items[unique++] = items.items[i];
    if (unique - 1 != i)
      items.items[i].key.data = NULL;
  }
  items.len = unique;
  unsigned char *trie = NULL;
  size_t trie_size;
  status = cetta_act_build(items.items, items.len, &trie, &trie_size);
  if (status != CETTA_ACT_OK)
    goto done;
  if (mork) {
    status = publish_snapshot(path, (CettaActBytes){trie, trie_size});
    free(trie);
  } else {
    Bytes file = {0};
    unsigned char header[HEADER] = {0};
    memcpy(header, "CACT0001", 8);
    setle(header + 8, HEADER);
    setle(header + 16, HEADER);
    setle(header + 24, trie_size);
    setle(header + 32, HEADER + trie_size);
    setle(header + 40, bodies.len);
    setle(header + 48, count);
    setle(header + 56, 1);
    bool ok = add(&file, header, HEADER) && add(&file, trie, trie_size) &&
              add(&file, bodies.data, bodies.len);
    free(trie);
    if (ok) {
      CettaNativeSha256 sha;
      unsigned char digest[32];
      cetta_native_sha256_init(&sha);
      cetta_native_sha256_update(&sha, file.data, file.len);
      cetta_native_sha256_finish(&sha, digest);
      ok = add(&file, digest, 32);
    }
    status = ok ? publish_snapshot(path, view(&file)) : CETTA_ACT_NOMEM;
    free(file.data);
  }
done:
  space_occurrence_cursor_release(&cursor);
  arena_free(&scratch);
  for (size_t i = 0; i < items.len; i++)
    free((void *)items.items[i].key.data);
  free(items.items);
  free(bodies.data);
  for (size_t i = 0; i < cap; i++)
    free(table[i].wire);
  free(table);
  return status;
}
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
