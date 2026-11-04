#define _POSIX_C_SOURCE 200809L
/* ACTree03, the byte-trie format of PathMap's src/arena_compact.rs
 * (revision cb4e1e4eecb75f0be9846b4b75c69d5e2d2f647b), read and written in
 * C.  The format follows PathMap, Copyright (c) Adam Vandervorst, whose MIT
 * license (LICENSES/PathMap-MIT.txt) covers this file.  This implementation
 * uses checked byte reads and explicit traversal stacks. */
#include "act_trie.h"
#include "storage_census.h"
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
  unsigned char *data;
  size_t len, cap;
} Buffer;
typedef struct {
  size_t id, end, child, line_at, line_len;
  uint64_t value;
  unsigned count;
  bool line, valued;
  unsigned char labels[256];
} Node;
struct CettaActTree {
  _Atomic size_t refs;
  unsigned char *data;
  size_t size, root;
  bool mapped;
  struct CettaActTree *parent;
};
typedef struct {
  Node node;
  size_t base, child;
  unsigned next;
  bool entered, emitted;
} Frame;
struct CettaActCursor {
  CettaActTree *tree;
  Frame *frames;
  size_t len, cap;
  Buffer key, lower, upper;
  bool has_lower, has_upper, terminal;
  CettaActStatus status;
};
static uint64_t read64(const unsigned char *p) {
  uint64_t v = 0;
  for (unsigned i = 0; i < 8; i++)
    v |= (uint64_t)p[i] << (8 * i);
  return v;
}
static void write64(unsigned char *p, uint64_t v) {
  for (unsigned i = 0; i < 8; i++)
    p[i] = (unsigned char)(v >> (8 * i));
}
static bool reserve(void **p, size_t *cap, size_t need, size_t unit) {
  if (need <= *cap)
    return true;
  if (!unit || need > SIZE_MAX / unit)
    return false;
  size_t n = *cap ? *cap : 1;
  while (n < need) {
    if (n > SIZE_MAX / 2) {
      n = need;
      break;
    }
    n *= 2;
  }
  if (n > SIZE_MAX / unit)
    n = need;
  void *q = realloc(*p, n * unit);
  if (!q)
    return false;
  *p = q;
  *cap = n;
  return true;
}
static bool append(Buffer *b, const void *p, size_t n) {
  if (n > SIZE_MAX - b->len ||
      !reserve((void **)&b->data, &b->cap, b->len + n, 1))
    return false;
  if (n)
    memcpy(b->data + b->len, p, n);
  b->len += n;
  return true;
}
static int compare(CettaActBytes a, CettaActBytes b) {
  size_t n = a.size < b.size ? a.size : b.size;
  int c = n ? memcmp(a.data, b.data, n) : 0;
  return c ? c : (a.size > b.size) - (a.size < b.size);
}
bool cetta_act_varint(const unsigned char *p, size_t n, uint64_t *v,
                      size_t *used) {
  if (!p || !n || !v || !used)
    return false;
  unsigned bytes = p[0] > 247 ? p[0] - 247 : 0;
  if (n < 1u + bytes)
    return false;
  *v = bytes ? 0 : p[0];
  for (unsigned i = 0; i < bytes; i++)
    *v |= (uint64_t)p[i + 1] << (i * 8);
  *used = bytes + 1;
  return true;
}
size_t cetta_act_put_varint(unsigned char p[9], uint64_t v) {
  if (v <= 247) {
    p[0] = (unsigned char)v;
    return 1;
  }
  unsigned n = 0;
  uint64_t rest = v;
  while (rest) {
    p[++n] = (unsigned char)rest;
    rest >>= 8;
  }
  p[0] = (unsigned char)(247 + n);
  return n + 1;
}
static bool number(const CettaActTree *t, size_t *at, uint64_t *v) {
  size_t used;
  if (*at >= t->size ||
      !cetta_act_varint(t->data + *at, t->size - *at, v, &used))
    return false;
  *at += used;
  return true;
}
static bool back(size_t id, uint64_t delta, size_t *out) {
  if (!delta || id < 16 || delta > id - 16)
    return false;
  *out = id - (size_t)delta;
  return true;
}
static CettaActStatus node_read(const CettaActTree *t, size_t id, Node *n) {
  if (id < 16 || id >= t->size)
    return CETTA_ACT_CORRUPT;
  *n = (Node){.id = id};
  unsigned char h = t->data[id];
  size_t at = id + 1;
  uint64_t v;
  n->line = (h & 128) != 0;
  n->valued = (h & 64) != 0;
  if (n->valued && !number(t, &at, &n->value))
    return CETTA_ACT_CORRUPT;
  if (n->line) {
    if (h & 62)
      return CETTA_ACT_CORRUPT;
    n->count = h & 1;
    if (n->count && (!number(t, &at, &v) || !back(id, v, &n->child)))
      return CETTA_ACT_CORRUPT;
    size_t line;
    if (!number(t, &at, &v) || !back(id, v, &line) || !number(t, &line, &v) ||
        !v || v > SIZE_MAX)
      return CETTA_ACT_CORRUPT;
    if (line > id || v > id - line)
      return CETTA_ACT_CORRUPT;
    n->line_at = line;
    n->line_len = (size_t)v;
  } else {
    unsigned code = h & 63;
    if (code > 32)
      return CETTA_ACT_CORRUPT;
    if (code && (!number(t, &at, &v) || !back(id, v, &n->child)))
      return CETTA_ACT_CORRUPT;
    if (code > t->size - at)
      return CETTA_ACT_CORRUPT;
    if (code < 32) {
      n->count = code;
      for (unsigned i = 0; i < code; i++) {
        unsigned char b = t->data[at++];
        if (i && b <= n->labels[i - 1])
          return CETTA_ACT_CORRUPT;
        n->labels[i] = b;
      }
    } else {
      for (unsigned i = 0; i < 256; i++)
        if (t->data[at + i / 8] & (1u << (i % 8)))
          n->labels[n->count++] = (unsigned char)i;
      at += 32;
      if (n->count < 32)
        return CETTA_ACT_CORRUPT;
    }
  }
  n->end = at;
  return CETTA_ACT_OK;
}
/* Siblings are adjacent variable-size descriptors. Locating one must not
 * expand every preceding sibling's mask or follow its compressed path. */
static CettaActStatus node_skip(const CettaActTree *t, size_t id, size_t *end) {
  if (id < 16 || id >= t->size)
    return CETTA_ACT_CORRUPT;
  unsigned h = t->data[id], code = h & 63u;
  bool line = (h & 128u) != 0;
  size_t at = id + 1u, ignored;
  uint64_t value;
  if ((line && (code & 62u)) || (!line && code > 32u) ||
      ((h & 64u) && !number(t, &at, &value)) ||
      (code && (!number(t, &at, &value) || !back(id, value, &ignored))))
    return CETTA_ACT_CORRUPT;
  if (line) {
    if (!number(t, &at, &value) || !back(id, value, &ignored))
      return CETTA_ACT_CORRUPT;
  } else {
    if (code > t->size - at)
      return CETTA_ACT_CORRUPT;
    at += code;
  }
  *end = at;
  return CETTA_ACT_OK;
}
static CettaActStatus image_check(CettaActTree *t) {
  if (t->size < 25 || memcmp(t->data, "ACTree03", 8))
    return CETTA_ACT_CORRUPT;
  uint64_t root = read64(t->data + 8);
  if (root > SIZE_MAX)
    return CETTA_ACT_CORRUPT;
  t->root = (size_t)root;
  Node n;
  CettaActStatus s = node_read(t, t->root, &n);
  return s == CETTA_ACT_OK && n.end > t->size - 8 ? CETTA_ACT_CORRUPT : s;
}
CettaActStatus cetta_act_map(const char *path, CettaActTree **out) {
  if (!out || !path)
    return CETTA_ACT_CORRUPT;
  *out = NULL;
  int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0)
    return CETTA_ACT_IO;
  struct stat st;
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 25 ||
      (uintmax_t)st.st_size > SIZE_MAX) {
    close(fd);
    return CETTA_ACT_CORRUPT;
  }
  CettaActTree *t = calloc(1, sizeof(*t));
  if (!t) {
    close(fd);
    return CETTA_ACT_NOMEM;
  }
  t->size = (size_t)st.st_size;
  t->data = mmap(NULL, t->size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (t->data == MAP_FAILED) {
    free(t);
    return CETTA_ACT_IO;
  }
  t->mapped = true;
  atomic_init(&t->refs, 1);
  *out = t;
  return CETTA_ACT_OK;
}
CettaActStatus cetta_act_open(const char *path, CettaActTree **out) {
  CettaActStatus s = cetta_act_map(path, out);
  if (s != CETTA_ACT_OK)
    return s;
  s = image_check(*out);
  if (s != CETTA_ACT_OK) {
    cetta_act_release(*out);
    *out = NULL;
  }
  return s;
}
CettaActStatus cetta_act_view(CettaActTree *parent, size_t offset, size_t size,
                              CettaActTree **out) {
  if (!out)
    return CETTA_ACT_CORRUPT;
  *out = NULL;
  if (!parent || offset > parent->size || size > parent->size - offset)
    return CETTA_ACT_CORRUPT;
  CettaActTree *t = calloc(1, sizeof(*t));
  if (!t)
    return CETTA_ACT_NOMEM;
  t->data = parent->data + offset;
  t->size = size;
  t->parent = parent;
  atomic_init(&t->refs, 1);
  cetta_act_retain(parent);
  CettaActStatus s = image_check(t);
  if (s != CETTA_ACT_OK) {
    cetta_act_release(t);
    return s;
  }
  *out = t;
  return CETTA_ACT_OK;
}
CettaActStatus cetta_act_from_bytes(CettaActBytes b, CettaActTree **out) {
  if (!out || (b.size && !b.data))
    return CETTA_ACT_CORRUPT;
  *out = NULL;
  CettaActTree *t = calloc(1, sizeof(*t));
  if (!t)
    return CETTA_ACT_NOMEM;
  t->data = malloc(b.size ? b.size : 1);
  if (!t->data) {
    free(t);
    return CETTA_ACT_NOMEM;
  }
  if (b.size)
    memcpy(t->data, b.data, b.size);
  t->size = b.size;
  atomic_init(&t->refs, 1);
  CettaActStatus s = image_check(t);
  if (s != CETTA_ACT_OK) {
    cetta_act_release(t);
    return s;
  }
  *out = t;
  return CETTA_ACT_OK;
}
void cetta_act_retain(CettaActTree *t) {
  if (t)
    atomic_fetch_add_explicit(&t->refs, 1, memory_order_relaxed);
}
void cetta_act_release(CettaActTree *t) {
  if (t && atomic_fetch_sub_explicit(&t->refs, 1, memory_order_acq_rel) == 1) {
    if (t->parent)
      cetta_act_release(t->parent);
    else if (t->mapped)
      munmap(t->data, t->size);
    else
      free(t->data);
    free(t);
  }
}
size_t cetta_act_mapped_bytes(const CettaActTree *t) { return t ? t->size : 0; }
CettaActBytes cetta_act_image(const CettaActTree *t) {
  return (CettaActBytes){t ? t->data : NULL, t ? t->size : 0};
}

static CettaActStatus push(CettaActCursor *c, size_t id, size_t base) {
  if (!reserve((void **)&c->frames, &c->cap, c->len + 1, sizeof(*c->frames)))
    return CETTA_ACT_NOMEM;
  Frame f = {.base = base};
  CettaActStatus s = node_read(c->tree, id, &f.node);
  if (s != CETTA_ACT_OK)
    return s;
  f.child = f.node.child;
  c->frames[c->len++] = f;
  return CETTA_ACT_OK;
}
void cetta_act_cursor_close(CettaActCursor *c) {
  if (c) {
    cetta_act_release(c->tree);
    free(c->frames);
    free(c->key.data);
    free(c->lower.data);
    free(c->upper.data);
    free(c);
  }
}
void cetta_act_storage(const CettaActTree *t, CettaStorageCensus *census) {
  while (t &&
         cetta_storage_object(census, t, sizeof(*t), CETTA_STORAGE_BLOCK)) {
    if (!t->parent && !t->mapped)
      cetta_storage_array(census, t->data, t->size ? t->size : 1, 1, 0);
    t = t->parent;
  }
}
void cetta_act_cursor_storage(const CettaActCursor *c,
                              CettaStorageCensus *census) {
  if (!c || !cetta_storage_object(census, c, sizeof(*c), CETTA_STORAGE_BLOCK))
    return;
  cetta_storage_array(census, c->frames, c->cap, sizeof(*c->frames), 0);
  cetta_storage_array(census, c->key.data, c->key.cap, 1, 0);
  cetta_storage_array(census, c->lower.data, c->lower.cap, 1, 0);
  cetta_storage_array(census, c->upper.data, c->upper.cap, 1, 0);
  cetta_act_storage(c->tree, census);
}
CettaActStatus cetta_act_cursor_open(CettaActTree *t, const CettaActBytes *lo,
                                     const CettaActBytes *hi,
                                     CettaActCursor **out) {
  if (!out || !t || (lo && lo->size && !lo->data) ||
      (hi && hi->size && !hi->data))
    return CETTA_ACT_CORRUPT;
  *out = NULL;
  CettaActCursor *c = calloc(1, sizeof(*c));
  if (!c)
    return CETTA_ACT_NOMEM;
  c->tree = t;
  cetta_act_retain(t);
  c->has_lower = lo != NULL;
  c->has_upper = hi != NULL;
  if ((lo && !append(&c->lower, lo->data, lo->size)) ||
      (hi && !append(&c->upper, hi->data, hi->size))) {
    cetta_act_cursor_close(c);
    return CETTA_ACT_NOMEM;
  }
  CettaActStatus s = push(c, t->root, 0);
  if (s != CETTA_ACT_OK) {
    cetta_act_cursor_close(c);
    return s;
  }
  *out = c;
  return CETTA_ACT_OK;
}
CettaActStatus cetta_act_cursor_prefix(CettaActTree *t, CettaActBytes prefix,
                                       CettaActCursor **out) {
  if (!out || (prefix.size && !prefix.data))
    return CETTA_ACT_CORRUPT;
  Buffer upper = {0};
  if (!append(&upper, prefix.data, prefix.size))
    return CETTA_ACT_NOMEM;
  while (upper.len && upper.data[upper.len - 1] == 255)
    --upper.len;
  if (upper.len)
    ++upper.data[upper.len - 1];
  CettaActBytes bound = {upper.data, upper.len};
  CettaActStatus s =
      cetta_act_cursor_open(t, &prefix, upper.len ? &bound : NULL, out);
  free(upper.data);
  return s;
}
CettaActStatus cetta_act_cursor_clone(const CettaActCursor *c, bool rewind,
                                      CettaActCursor **out) {
  if (!c || !out)
    return CETTA_ACT_CORRUPT;
  CettaActBytes lo = {c->lower.data, c->lower.len},
                hi = {c->upper.data, c->upper.len};
  CettaActStatus s = cetta_act_cursor_open(c->tree, c->has_lower ? &lo : NULL,
                                           c->has_upper ? &hi : NULL, out);
  if (s != CETTA_ACT_OK || rewind)
    return s;
  CettaActCursor *d = *out;
  if (!reserve((void **)&d->frames, &d->cap, c->len, sizeof(*d->frames)) ||
      !append(&d->key, c->key.data, c->key.len)) {
    cetta_act_cursor_close(d);
    *out = NULL;
    return CETTA_ACT_NOMEM;
  }
  if (c->len)
    memcpy(d->frames, c->frames, c->len * sizeof(*d->frames));
  d->len = c->len;
  d->terminal = c->terminal;
  d->status = c->status;
  return CETTA_ACT_OK;
}
static bool below_lower(const CettaActCursor *c) {
  size_t n = c->key.len < c->lower.len ? c->key.len : c->lower.len;
  return c->has_lower && n && memcmp(c->key.data, c->lower.data, n) < 0;
}
CettaActStatus cetta_act_cursor_next(CettaActCursor *c, CettaActBytes *key,
                                     uint64_t *value) {
  if (!c || !key || !value)
    return CETTA_ACT_CORRUPT;
  if (c->terminal)
    return c->status;
  CettaActStatus s = CETTA_ACT_END;
  while (c->len) {
    Frame *f = &c->frames[c->len - 1];
    Node *n = &f->node;
    if (f->entered)
      c->key.len = f->base + (n->line ? n->line_len : 0);
    if (!f->entered) {
      c->key.len = f->base;
      if (n->line &&
          !append(&c->key, c->tree->data + n->line_at, n->line_len)) {
        s = CETTA_ACT_NOMEM;
        break;
      }
      f->entered = true;
      if (below_lower(c)) {
        c->len--;
        continue;
      }
      if (c->has_upper &&
          compare((CettaActBytes){c->key.data, c->key.len},
                  (CettaActBytes){c->upper.data, c->upper.len}) >= 0)
        break;
    }
    if (!f->emitted) {
      f->emitted = true;
      if (n->valued &&
          (!c->has_lower ||
           compare((CettaActBytes){c->key.data, c->key.len},
                   (CettaActBytes){c->lower.data, c->lower.len}) >= 0)) {
        *key = (CettaActBytes){c->key.data, c->key.len};
        *value = n->value;
        return CETTA_ACT_OK;
      }
    }
    if (f->next < n->count) {
      size_t id = f->child;
      unsigned next = f->next++;
      Node child;
      s = node_read(c->tree, id, &child);
      if (s != CETTA_ACT_OK)
        break;
      if (child.end > n->id) {
        s = CETTA_ACT_CORRUPT;
        break;
      }
      f->child = child.end;
      if (!n->line && !append(&c->key, &n->labels[next], 1)) {
        s = CETTA_ACT_NOMEM;
        break;
      }
      s = push(c, id, c->key.len);
      if (s != CETTA_ACT_OK)
        break;
    } else {
      c->len--;
      if (c->len) {
        Frame *p = &c->frames[c->len - 1];
        c->key.len = p->base + (p->node.line ? p->node.line_len : 0);
      }
    }
  }
  if (s == CETTA_ACT_OK)
    s = CETTA_ACT_END;
  c->terminal = true;
  c->status = s;
  return s;
}
CettaActStatus cetta_act_get(const CettaActTree *t, CettaActBytes key,
                             uint64_t *out) {
  if (!t || !out || (key.size && !key.data))
    return CETTA_ACT_CORRUPT;
  size_t id = t->root, at = 0, limit = t->size - 8;
  for (;;) {
    Node n;
    CettaActStatus s = node_read(t, id, &n);
    if (s != CETTA_ACT_OK)
      return s;
    if (n.end > limit)
      return CETTA_ACT_CORRUPT;
    limit = n.id;
    if (n.line) {
      if (n.line_len > key.size - at ||
          memcmp(t->data + n.line_at, key.data + at, n.line_len))
        return CETTA_ACT_NOT_FOUND;
      at += n.line_len;
    }
    if (at == key.size) {
      if (!n.valued) {
        if (n.line && n.count) {
          id = n.child;
          continue;
        }
        return CETTA_ACT_NOT_FOUND;
      }
      *out = n.value;
      return CETTA_ACT_OK;
    }
    if (!n.count)
      return CETTA_ACT_NOT_FOUND;
    size_t child = n.child;
    if (n.line) {
      id = child;
      continue;
    }
    unsigned index = 0;
    while (index < n.count && n.labels[index] != key.data[at])
      index++;
    if (index == n.count)
      return CETTA_ACT_NOT_FOUND;
    for (unsigned i = 0; i < index; i++) {
      size_t next;
      s = node_skip(t, child, &next);
      if (s != CETTA_ACT_OK)
        return s;
      child = next;
    }
    if (child >= n.id)
      return CETTA_ACT_CORRUPT;
    id = child;
    at++;
  }
}
CettaActStatus cetta_act_validate(CettaActTree *t, uint64_t *count) {
  if (count)
    *count = 0;
  CettaActCursor *c = NULL;
  CettaActStatus s = cetta_act_cursor_open(t, NULL, NULL, &c);
  uint64_t n = 0, v;
  CettaActBytes key;
  Buffer previous = {0};
  bool have_previous = false;
  if (s != CETTA_ACT_OK)
    return s;
  while ((s = cetta_act_cursor_next(c, &key, &v)) == CETTA_ACT_OK) {
    if (have_previous &&
        compare((CettaActBytes){previous.data, previous.len}, key) >= 0) {
      s = CETTA_ACT_CORRUPT;
      break;
    }
    previous.len = 0;
    if (!append(&previous, key.data, key.size)) {
      s = CETTA_ACT_NOMEM;
      break;
    }
    have_previous = true;
    if (n == UINT64_MAX) {
      s = CETTA_ACT_LIMIT;
      break;
    }
    n++;
  }
  cetta_act_cursor_close(c);
  free(previous.data);
  if (count)
    *count = n;
  return s == CETTA_ACT_END ? CETTA_ACT_OK : s;
}

typedef struct {
  bool line, valued;
  uint64_t value;
  size_t child, line_id;
  unsigned count;
  unsigned char labels[256];
} Written;
typedef struct {
  size_t lo, hi, depth, common, next, stop;
  unsigned count;
  bool initialized;
  Written *children;
  size_t capacity;
  unsigned char labels[256];
} BuildFrame;
static bool put(Buffer *b, uint64_t v) {
  unsigned char bytes[9];
  size_t n = cetta_act_put_varint(bytes, v);
  return append(b, bytes, n);
}
static bool emit(Buffer *b, const Written *n, size_t *id) {
  *id = b->len;
  unsigned char h =
      (n->line ? 128 : 0) | (n->valued ? 64 : 0) |
      (n->line ? (n->count ? 1 : 0) : (n->count < 32 ? n->count : 32));
  if (!append(b, &h, 1) || (n->valued && !put(b, n->value)) ||
      (n->count && !put(b, *id - n->child)))
    return false;
  if (n->line)
    return put(b, *id - n->line_id);
  if (n->count < 32)
    return append(b, n->labels, n->count);
  unsigned char mask[32] = {0};
  for (unsigned i = 0; i < n->count; i++)
    mask[n->labels[i] / 8] |= 1u << (n->labels[i] % 8);
  return append(b, mask, 32);
}
CettaActStatus cetta_act_build(const CettaActItem *items, size_t count,
                               unsigned char **out, size_t *size) {
  if (!out || !size || (count && !items))
    return CETTA_ACT_CORRUPT;
  *out = NULL;
  *size = 0;
  for (size_t i = 0; i < count; i++)
    if (items[i].key.size && !items[i].key.data)
      return CETTA_ACT_CORRUPT;
  for (size_t i = 1; i < count; i++)
    if (compare(items[i - 1].key, items[i].key) >= 0)
      return CETTA_ACT_CORRUPT;
  Buffer b = {0};
  unsigned char header[16] = "ACTree03";
  if (!append(&b, header, 16))
    return CETTA_ACT_NOMEM;
  BuildFrame *stack = NULL;
  size_t len = 0, cap = 0;
  CettaActStatus s = CETTA_ACT_NOMEM;
  if (!reserve((void **)&stack, &cap, 1, sizeof(*stack)))
    goto done;
  stack[len++] = (BuildFrame){.hi = count};
  Written result = {0};
  while (len) {
    BuildFrame *f = &stack[len - 1];
    if (!f->initialized) {
      f->initialized = true;
      f->common = f->depth;
      f->next = f->lo;
      if (f->lo < f->hi) {
        CettaActBytes first = items[f->lo].key, last = items[f->hi - 1].key;
        while (f->common < first.size && f->common < last.size &&
               first.data[f->common] == last.data[f->common])
          f->common++;
      }
      if (f->common > f->depth) {
        size_t lo = f->lo, hi = f->hi, d = f->common;
        if (!reserve((void **)&stack, &cap, len + 1, sizeof(*stack)))
          goto done;
        stack[len++] = (BuildFrame){.lo = lo, .hi = hi, .depth = d};
        continue;
      }
      if (f->next < f->hi && items[f->next].key.size == f->depth)
        f->next++;
    }
    if (f->common > f->depth) {
      Written line = {.line = true};
      if (result.valued && !result.count && !result.line) {
        line.valued = true;
        line.value = result.value;
      } else {
        if (!emit(&b, &result, &line.child))
          goto done;
        line.count = 1;
      }
      line.line_id = b.len;
      if (!put(&b, f->common - f->depth) ||
          !append(&b, items[f->lo].key.data + f->depth, f->common - f->depth))
        goto done;
      result = line;
      len--;
    } else if (f->next < f->hi) {
      size_t lo = f->next, hi = lo + 1, d = f->depth;
      unsigned char label = items[lo].key.data[d];
      while (hi < f->hi && items[hi].key.size > d &&
             items[hi].key.data[d] == label)
        hi++;
      f->labels[f->count] = label;
      f->next = hi;
      f->stop = 1;
      if (!reserve((void **)&stack, &cap, len + 1, sizeof(*stack)))
        goto done;
      stack[len++] = (BuildFrame){.lo = lo, .hi = hi, .depth = d + 1};
      continue;
    } else {
      Written branch = {.count = f->count};
      if (f->lo < f->hi && items[f->lo].key.size == f->depth) {
        branch.valued = true;
        branch.value = items[f->lo].value;
      }
      for (unsigned i = 0; i < f->count; i++) {
        size_t id;
        if (!emit(&b, &f->children[i], &id))
          goto done;
        if (!i)
          branch.child = id;
        branch.labels[i] = f->labels[i];
      }
      result = branch;
      free(f->children);
      f->children = NULL;
      len--;
    }
    if (len) {
      BuildFrame *parent = &stack[len - 1];
      if (parent->common == parent->depth && parent->stop) {
        if (!reserve((void **)&parent->children, &parent->capacity,
                     parent->count + 1u, sizeof(*parent->children)))
          goto done;
        parent->children[parent->count++] = result;
        parent->stop = 0;
      }
    }
  }
  size_t root;
  if (!emit(&b, &result, &root))
    goto done;
  write64(b.data + 8, root);
  unsigned char pad[8] = {0};
  if (!append(&b, pad, 8))
    goto done;
  *out = b.data;
  *size = b.len;
  b.data = NULL;
  s = CETTA_ACT_OK;
done:
  for (size_t i = 0; i < len; i++)
    free(stack[i].children);
  free(stack);
  free(b.data);
  return s;
}
/* The image is staged in a file created as any file written there would be:
 * mode 0666 under the umask and the directory's default ACL, not mkstemp's
 * 0600, so a published image is as readable as its neighbours.  The name is
 * the path, the process and a serial; creation is exclusive. */
static int publish_temporary(char *tmp, size_t stem) {
  static _Atomic unsigned long serial;
  for (unsigned attempt = 0; attempt < 64; attempt++) {
    unsigned long next =
        atomic_fetch_add_explicit(&serial, 1, memory_order_relaxed);
    snprintf(tmp + stem, 48, ".tmp-%ld-%lx", (long)getpid(), next);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (fd >= 0 || errno != EEXIST)
      return fd;
  }
  errno = EEXIST;
  return -1;
}
CettaActStatus cetta_act_publish(const char *path, CettaActBytes bytes) {
  if (!path || !*path || (bytes.size && !bytes.data))
    return CETTA_ACT_CORRUPT;
  size_t n = strlen(path);
  if (n > SIZE_MAX - 48)
    return CETTA_ACT_LIMIT;
  char *tmp = malloc(n + 48), *parent = strdup(path);
  if (!tmp || !parent) {
    free(tmp);
    free(parent);
    return CETTA_ACT_NOMEM;
  }
  memcpy(tmp, path, n);
  tmp[n] = 0;
  char *slash = strrchr(parent, '/');
  if (slash) {
    if (slash == parent)
      slash[1] = 0;
    else
      *slash = 0;
  } else {
    free(parent);
    parent = strdup(".");
    if (!parent) {
      free(tmp);
      return CETTA_ACT_NOMEM;
    }
  }
  int dir = open(parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC), fd = -1;
  CettaActStatus s = CETTA_ACT_IO;
  bool staged = false;
  if (dir < 0)
    goto end;
  fd = publish_temporary(tmp, n);
  if (fd < 0)
    goto end;
  staged = true;
  size_t at = 0;
  while (at < bytes.size) {
    ssize_t w = write(fd, bytes.data + at, bytes.size - at);
    if (w < 0 && errno == EINTR)
      continue;
    if (w <= 0)
      goto end;
    at += (size_t)w;
  }
  if (fsync(fd))
    goto end;
  if (close(fd)) {
    fd = -1;
    goto end;
  }
  fd = -1;
  if (rename(tmp, path))
    goto end;
  staged = false;
  if (fsync(dir)) {
    s = CETTA_ACT_UNCERTAIN;
    goto end;
  }
  s = CETTA_ACT_OK;
end:
  if (fd >= 0)
    close(fd);
  if (staged)
    unlink(tmp);
  if (dir >= 0)
    close(dir);
  free(tmp);
  free(parent);
  return s;
}
const char *cetta_act_status_name(CettaActStatus s) {
  static const char *const names[] = {
      "ok",       "end",   "not-found", "io",          "corrupt",
      "capacity", "limit", "uncertain", "unsupported", "read-only"};
  return (unsigned)s < sizeof(names) / sizeof(*names) ? names[s] : "unknown";
}
