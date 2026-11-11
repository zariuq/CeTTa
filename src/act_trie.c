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
  size_t *children;
  unsigned next;
  bool entered, emitted;
} Frame;
struct CettaActCursor {
  CettaActTree *tree;
  Frame *frames;
  size_t len, cap;
  Buffer key, lower, upper;
  size_t key_limit;
  bool has_lower, has_upper, terminal, reverse;
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
CettaActStatus cetta_act_map_fd(int fd, CettaActTree **out) {
  if (!out)
    return CETTA_ACT_CORRUPT;
  *out = NULL;
  struct stat st;
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 25 ||
      (uintmax_t)st.st_size > SIZE_MAX) {
    return CETTA_ACT_CORRUPT;
  }
  CettaActTree *t = calloc(1, sizeof(*t));
  if (!t) {
    return CETTA_ACT_NOMEM;
  }
  t->size = (size_t)st.st_size;
  t->data = mmap(NULL, t->size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (t->data == MAP_FAILED) {
    free(t);
    return CETTA_ACT_IO;
  }
  t->mapped = true;
  atomic_init(&t->refs, 1);
  *out = t;
  return CETTA_ACT_OK;
}
CettaActStatus cetta_act_map(const char *path, CettaActTree **out) {
  if (!out || !path) return CETTA_ACT_CORRUPT;
  *out = NULL;
  int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0) return CETTA_ACT_IO;
  CettaActStatus s = cetta_act_map_fd(fd,out); close(fd); return s;
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
  if (c->reverse && f.node.count) {
    f.children = malloc(f.node.count * sizeof(*f.children));
    if (!f.children) return CETTA_ACT_NOMEM;
    size_t child = f.node.child;
    for (unsigned i = 0; i < f.node.count; i++) {
      f.children[i] = child;
      s = node_skip(c->tree, child, &child);
      if (s != CETTA_ACT_OK || child > f.node.id) {
        free(f.children);
        return s == CETTA_ACT_OK ? CETTA_ACT_CORRUPT : s;
      }
    }
    f.next = f.node.count;
  }
  c->frames[c->len++] = f;
  return CETTA_ACT_OK;
}
void cetta_act_cursor_close(CettaActCursor *c) {
  if (c) {
    cetta_act_release(c->tree);
    for (size_t i = 0; i < c->len; i++) free(c->frames[i].children);
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
  for (size_t i = 0; i < c->len; i++)
    cetta_storage_array(census, c->frames[i].children,
        c->frames[i].children ? c->frames[i].node.count : 0, sizeof(size_t), 0);
  cetta_storage_array(census, c->key.data, c->key.cap, 1, 0);
  cetta_storage_array(census, c->lower.data, c->lower.cap, 1, 0);
  cetta_storage_array(census, c->upper.data, c->upper.cap, 1, 0);
  cetta_act_storage(c->tree, census);
}
static CettaActStatus cursor_open(CettaActTree *t, const CettaActBytes *lo,
    const CettaActBytes *hi, bool reverse, CettaActCursor **out) {
  if (!out || !t || (lo && lo->size && !lo->data) ||
      (hi && hi->size && !hi->data))
    return CETTA_ACT_CORRUPT;
  *out = NULL;
  CettaActCursor *c = calloc(1, sizeof(*c));
  if (!c)
    return CETTA_ACT_NOMEM;
  c->tree = t;
  c->reverse = reverse;c->key_limit=SIZE_MAX;
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
CettaActStatus cetta_act_cursor_open(CettaActTree *t, const CettaActBytes *lo,
    const CettaActBytes *hi, CettaActCursor **out) {
  return cursor_open(t, lo, hi, false, out);
}
CettaActStatus cetta_act_cursor_open_reverse(CettaActTree *t,
    const CettaActBytes *lo, const CettaActBytes *hi, CettaActCursor **out) {
  return cursor_open(t, lo, hi, true, out);
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
  CettaActStatus s = cursor_open(c->tree, c->has_lower ? &lo : NULL,
                               c->has_upper ? &hi : NULL, c->reverse, out);
  if (s != CETTA_ACT_OK || rewind)
    return s;
  CettaActCursor *d = *out;d->key_limit=c->key_limit;
  for (size_t i = 0; i < d->len; i++) free(d->frames[i].children);
  d->len = 0;
  if (!reserve((void **)&d->frames, &d->cap, c->len, sizeof(*d->frames)) ||
      !append(&d->key, c->key.data, c->key.len)) {
    cetta_act_cursor_close(d);
    *out = NULL;
    return CETTA_ACT_NOMEM;
  }
  for (size_t i = 0; i < c->len; i++) {
    Frame frame = c->frames[i]; frame.children = NULL;
    if (c->frames[i].children) {
      frame.children = malloc(frame.node.count * sizeof(*frame.children));
      if (!frame.children) {
        cetta_act_cursor_close(d); *out = NULL; return CETTA_ACT_NOMEM;
      }
      memcpy(frame.children, c->frames[i].children,
             frame.node.count * sizeof(*frame.children));
    }
    d->frames[d->len++] = frame;
  }
  d->terminal = c->terminal;
  d->status = c->status;
  return CETTA_ACT_OK;
}
static bool below_lower(const CettaActCursor *c) {
  size_t n = c->key.len < c->lower.len ? c->key.len : c->lower.len;
  return c->has_lower && n && memcmp(c->key.data, c->lower.data, n) < 0;
}
static CettaActStatus cursor_previous(CettaActCursor *c,uint64_t work,CettaActBytes *key,
                                       uint64_t *value) {
  if (c->terminal) return c->status;
  CettaActStatus s = CETTA_ACT_END;
  while (c->len) {
    if (!work--) return CETTA_ACT_PROGRESS;
    Frame *f = &c->frames[c->len - 1]; Node *n = &f->node;
    if (f->entered) c->key.len = f->base + (n->line ? n->line_len : 0);
    if (!f->entered) {
      c->key.len = f->base;
      if (n->line && (c->key.len>c->key_limit || n->line_len>c->key_limit-c->key.len)) {
        s=CETTA_ACT_CORRUPT;break;
      }
      if (n->line && !append(&c->key, c->tree->data + n->line_at, n->line_len)) {
        s = CETTA_ACT_NOMEM; break;
      }
      f->entered = true;
      if (below_lower(c)) break;
      if (c->has_upper && compare((CettaActBytes){c->key.data,c->key.len},
              (CettaActBytes){c->upper.data,c->upper.len}) >= 0) {
        free(f->children); f->children = NULL; --c->len; continue;
      }
    }
    if (f->next) {
      unsigned i = --f->next; size_t id = f->children[i];
      if (!n->line && c->key.len>=c->key_limit) {s=CETTA_ACT_CORRUPT;break;}
      if (!n->line && !append(&c->key, n->labels + i, 1)) {
        s = CETTA_ACT_NOMEM; break;
      }
      s = push(c,id,c->key.len);
      if (s != CETTA_ACT_OK) break;
      continue;
    }
    if (!f->emitted) {
      f->emitted = true;
      if (n->valued && (!c->has_lower ||
          compare((CettaActBytes){c->key.data,c->key.len},
                  (CettaActBytes){c->lower.data,c->lower.len}) >= 0)) {
        *key = (CettaActBytes){c->key.data,c->key.len}; *value = n->value;
        return CETTA_ACT_OK;
      }
    }
    free(f->children); f->children = NULL; --c->len;
  }
  if (s == CETTA_ACT_OK) s = CETTA_ACT_END;
  c->terminal = true; c->status = s; return s;
}
CettaActStatus cetta_act_cursor_next_bounded(CettaActCursor *c,uint64_t work,CettaActBytes *key,
                                     uint64_t *value) {
  if (!c || !key || !value)
    return CETTA_ACT_CORRUPT;
  if (c->reverse) return cursor_previous(c,work,key,value);
  if (c->terminal)
    return c->status;
  CettaActStatus s = CETTA_ACT_END;
  while (c->len) {
    if (!work--) return CETTA_ACT_PROGRESS;
    Frame *f = &c->frames[c->len - 1];
    Node *n = &f->node;
    if (f->entered)
      c->key.len = f->base + (n->line ? n->line_len : 0);
    if (!f->entered) {
      c->key.len = f->base;
      if (n->line && (c->key.len>c->key_limit || n->line_len>c->key_limit-c->key.len)) {
        s=CETTA_ACT_CORRUPT;break;
      }
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
      if (!n->line && c->key.len>=c->key_limit) {s=CETTA_ACT_CORRUPT;break;}
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
CettaActStatus cetta_act_cursor_next(CettaActCursor *c,CettaActBytes *key,uint64_t *value) {
  return cetta_act_cursor_next_bounded(c,UINT64_MAX,key,value);
}
bool cetta_act_cursor_key_limit(CettaActCursor *c,size_t maximum) {
  if (!c || c->key.len>maximum) return false;
  c->key_limit=maximum;return true;
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
static CettaActStatus build_memory(const CettaActItem *,size_t,unsigned char **,size_t *);
CettaActStatus cetta_act_build(const CettaActItem *items,size_t count,
                                unsigned char **out,size_t *size) {
  return build_memory(items,count,out,size);
}

/* Only the active branch is resident. Ancestor summaries and oversized keys
 * are kept in private seekable scratch files. Child descriptors are emitted
 * together after all descendants, as required by ACTree03. */
typedef struct {
  uint64_t depth, value;
  unsigned count;
  bool valued;
  Written children[256];
  unsigned char labels[256];
} StreamFrame;
typedef struct {
  uint64_t depth, value, count, valued;
} SavedFrame;
struct CettaActBuilder {
  int output, frontier, key_file;
  uint64_t end, flushed, frontier_end, previous_size, value, records;
  size_t budget, key_limit, output_used, peak_heap;
  Buffer previous, memory_output, memory_frontier;
  StreamFrame frame;
  bool active, have_previous, spooled, finished, finishing, memory;
  Written final;
  uint64_t final_depth;
  CettaActStatus failure;
  unsigned char output_buffer[32768], scratch[32768], incoming[32768];
};
static bool file_at(int fd, void *data, size_t size, uint64_t offset,
                    bool writing) {
  if (offset > INT64_MAX || size > (uint64_t)INT64_MAX - offset)
    return false;
  unsigned char *p = data;
  while (size) {
    ssize_t n = writing ? pwrite(fd, p, size, (off_t)offset)
                        : pread(fd, p, size, (off_t)offset);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    p += (size_t)n; size -= (size_t)n; offset += (uint64_t)n;
  }
  return true;
}
static CettaActStatus builder_fail(CettaActBuilder *b, CettaActStatus s) {
  if (b && b->failure == CETTA_ACT_OK) b->failure = s;
  return b ? b->failure : s;
}
static bool stream_flush(CettaActBuilder *b) {
  if (b->memory) return true;
  if (!file_at(b->output, b->output_buffer, b->output_used, b->flushed, true))
    return false;
  b->flushed += b->output_used; b->output_used = 0;
  return true;
}
static bool stream_append(CettaActBuilder *b, const void *raw, size_t size) {
  if (size > (uint64_t)INT64_MAX - b->end) return false;
  if (b->memory) {
    if (!append(&b->memory_output,raw,size)) {b->failure=CETTA_ACT_NOMEM;return false;}
    b->end+=size;return true;
  }
  const unsigned char *p = raw;
  while (size) {
    size_t amount = sizeof(b->output_buffer) - b->output_used;
    if (amount > size) amount = size;
    memcpy(b->output_buffer + b->output_used, p, amount);
    b->output_used += amount; b->end += amount; p += amount; size -= amount;
    if (b->output_used == sizeof(b->output_buffer) && !stream_flush(b))
      return false;
  }
  return true;
}
static bool stream_number(CettaActBuilder *b, uint64_t value) {
  unsigned char bytes[9]; size_t n = cetta_act_put_varint(bytes, value);
  return stream_append(b, bytes, n);
}
static bool stream_emit(CettaActBuilder *b, const Written *n, size_t *id) {
  if (b->end > SIZE_MAX) return false;
  *id = (size_t)b->end;
  unsigned char h = (n->line ? 128 : 0) | (n->valued ? 64 : 0) |
      (n->line ? (n->count ? 1 : 0) : (n->count < 32 ? n->count : 32));
  if (!stream_append(b, &h, 1) || (n->valued && !stream_number(b, n->value)) ||
      (n->count && (n->child >= *id || !stream_number(b, *id - n->child))))
    return false;
  if (n->line)
    return n->line_id < *id && stream_number(b, *id - n->line_id);
  if (n->count < 32) return stream_append(b, n->labels, n->count);
  unsigned char mask[32] = {0};
  for (unsigned i = 0; i < n->count; i++)
    mask[n->labels[i] / 8] |= 1u << (n->labels[i] % 8);
  return stream_append(b, mask, sizeof(mask));
}
static bool previous_read(CettaActBuilder *b, uint64_t offset, void *out,
                          size_t size) {
  if (offset > b->previous_size || size > b->previous_size - offset) return false;
  if (b->spooled) return file_at(b->key_file, out, size, offset, false);
  if (size) memcpy(out, b->previous.data + (size_t)offset, size);
  return true;
}

typedef struct {const unsigned char *data;int fd;uint64_t offset,size;} InputKey;
static bool input_read(const InputKey *key,uint64_t offset,void *out,size_t size) {
  if (offset>key->size || size>key->size-offset) return false;
  if (key->fd>=0) return file_at(key->fd,out,size,key->offset+offset,false);
  if (size) memcpy(out,key->data+(size_t)offset,size);
  return true;
}
static CettaActStatus previous_save(CettaActBuilder *b,const InputKey *key) {
  if (key->size<=b->key_limit) {
    size_t size=(size_t)key->size;
    if (size>b->previous.cap) {
      size_t cap=b->previous.cap ? b->previous.cap : 1;
      while (cap<size && cap<=b->key_limit/2) cap*=2;
      if (cap<size) cap=size;
      void *p=realloc(b->previous.data,cap);if (!p) return CETTA_ACT_NOMEM;
      if (!b->memory) {
        size_t preparation=sizeof(*b)+b->previous.cap+cap;
        if (preparation>b->peak_heap) b->peak_heap=preparation;
      }
      b->previous.data=p;b->previous.cap=cap;
    }
    if (!input_read(key,0,b->previous.data,size)) return CETTA_ACT_IO;
    if (b->spooled && ftruncate(b->key_file,0)) return CETTA_ACT_IO;
    b->spooled=false;b->previous.len=size;
  } else {
    for (uint64_t offset=0;offset<key->size;) {
      size_t n=key->size-offset<sizeof(b->scratch) ? (size_t)(key->size-offset) : sizeof(b->scratch);
      if (!input_read(key,offset,b->scratch,n) || !file_at(b->key_file,b->scratch,n,offset,true)) return CETTA_ACT_IO;
      offset+=n;
    }
    if (ftruncate(b->key_file,(off_t)key->size)) return CETTA_ACT_IO;
    b->spooled=true;
  }
  b->previous_size=key->size;b->have_previous=true;return CETTA_ACT_OK;
}
static bool frontier_at(CettaActBuilder *b,void *data,size_t n,uint64_t at,bool writing) {
  if (!b->memory) return file_at(b->frontier,data,n,at,writing);
  if (at>SIZE_MAX || n>SIZE_MAX-(size_t)at) return false;
  if (writing) {
    if (at!=b->memory_frontier.len || !append(&b->memory_frontier,data,n)) {
      b->failure=CETTA_ACT_NOMEM;return false;
    }
  } else {
    if (at>b->memory_frontier.len || n>b->memory_frontier.len-(size_t)at) return false;
    memcpy(data,b->memory_frontier.data+(size_t)at,n);
  }
  return true;
}
static bool frame_save(CettaActBuilder *b) {
  SavedFrame h = {b->frame.depth, b->frame.value, b->frame.count,
                   b->frame.valued ? 1u : 0u};
  uint64_t at = b->frontier_end;
  size_t children = b->frame.count * sizeof(Written);
  uint64_t size = sizeof(h) + children + b->frame.count;
  unsigned char tail[8]; write64(tail, size);
  if (!frontier_at(b, &h, sizeof(h), at, true) ||
      !frontier_at(b, b->frame.children, children, at + sizeof(h), true) ||
      !frontier_at(b, b->frame.labels, b->frame.count,
                at + sizeof(h) + children, true) ||
      !frontier_at(b, tail, sizeof(tail), at + size, true)) return false;
  b->frontier_end += size + sizeof(tail); return true;
}
static bool frame_restore(CettaActBuilder *b) {
  if (!b->frontier_end) { b->active = false; return true; }
  unsigned char tail[8]; SavedFrame h;
  if (b->frontier_end < sizeof(tail) ||
      !frontier_at(b, tail, sizeof(tail), b->frontier_end - sizeof(tail), false))
    return false;
  uint64_t size = read64(tail);
  if (size > b->frontier_end - sizeof(tail) || size < sizeof(h)) return false;
  uint64_t at = b->frontier_end - sizeof(tail) - size;
  if (!frontier_at(b, &h, sizeof(h), at, false) || h.count > 256 || h.valued > 1 ||
      size != sizeof(h) + h.count * sizeof(Written) + h.count) return false;
  size_t children = (size_t)h.count * sizeof(Written);
  if (!frontier_at(b, b->frame.children, children, at + sizeof(h), false) ||
      !frontier_at(b, b->frame.labels, (size_t)h.count,
                at + sizeof(h) + children, false) || (!b->memory && ftruncate(b->frontier, (off_t)at)))
    return false;
  b->frame.depth = h.depth; b->frame.value = h.value;
  b->frame.count = (unsigned)h.count; b->frame.valued = h.valued != 0;
  if (b->memory) b->memory_frontier.len=(size_t)at;
  b->frontier_end = at; b->active = true; return true;
}
static bool wrap_previous(CettaActBuilder *b, Written *result,
                          uint64_t from, uint64_t to) {
  if (from > to || to > b->previous_size) return false;
  if (from == to) return true;
  Written line = {.line = true};
  if (result->valued && !result->count && !result->line) {
    line.valued = true; line.value = result->value;
  } else {
    if (!stream_emit(b, result, &line.child)) return false;
    line.count = 1;
  }
  if (b->end > SIZE_MAX) return false;
  line.line_id = (size_t)b->end;
  if (!stream_number(b, to - from)) return false;
  while (from < to) {
    size_t amount = to - from < sizeof(b->scratch) ? (size_t)(to - from) : sizeof(b->scratch);
    if (!previous_read(b, from, b->scratch, amount) || !stream_append(b, b->scratch, amount))
      return false;
    from += amount;
  }
  *result = line; return true;
}
static bool frame_attach(CettaActBuilder *b, Written result, uint64_t depth) {
  unsigned char label;
  if (!b->active || b->frame.count == 256 || b->frame.depth == UINT64_MAX ||
      !wrap_previous(b, &result, b->frame.depth + 1, depth) ||
      !previous_read(b, b->frame.depth, &label, 1)) return false;
  unsigned n = b->frame.count;
  if (n && label <= b->frame.labels[n - 1]) return false;
  b->frame.children[n] = result; b->frame.labels[n] = label;
  ++b->frame.count; return true;
}
static bool frame_finish(CettaActBuilder *b, Written *result, uint64_t *depth) {
  Written branch = {.count = b->frame.count, .valued = b->frame.valued,
                     .value = b->frame.value};
  for (unsigned i = 0; i < b->frame.count; i++) {
    size_t id;
    if (!stream_emit(b, &b->frame.children[i], &id)) return false;
    if (!i) branch.child = id;
    branch.labels[i] = b->frame.labels[i];
  }
  *depth = b->frame.depth; *result = branch;
  return frame_restore(b);
}
void cetta_act_builder_close(CettaActBuilder *b) {
  if (!b) return;
  if (b->output >= 0) close(b->output);
  if (b->frontier >= 0) close(b->frontier);
  if (b->key_file >= 0) close(b->key_file);
  free(b->previous.data);free(b->memory_output.data);free(b->memory_frontier.data);free(b);
}
size_t cetta_act_builder_minimum_memory(void) {return sizeof(CettaActBuilder);}
CettaActStatus cetta_act_builder_open(int output, int frontier, int key_file,
                                      size_t budget, CettaActBuilder **out) {
  if (!out) return CETTA_ACT_CORRUPT;
  *out = NULL;
  if (budget < sizeof(CettaActBuilder)) return CETTA_ACT_LIMIT;
  int files[3] = {output, frontier, key_file}; struct stat st[3];
  for (unsigned i = 0; i < 3; i++) {
    if (fstat(files[i], &st[i])) return CETTA_ACT_IO;
    int flags=fcntl(files[i],F_GETFL);if (flags<0) return CETTA_ACT_IO;
    if ((flags&O_ACCMODE)!=O_RDWR || (flags&O_APPEND) || !S_ISREG(st[i].st_mode) || st[i].st_size) return CETTA_ACT_CORRUPT;
    for (unsigned j = 0; j < i; j++)
      if (st[i].st_dev == st[j].st_dev && st[i].st_ino == st[j].st_ino) return CETTA_ACT_CORRUPT;
  }
  CettaActBuilder *b = calloc(1, sizeof(*b));
  if (!b) return CETTA_ACT_NOMEM;
  b->output = b->frontier = b->key_file = -1;
  /* Reserve the old and new key buffers together during realloc. */
  b->budget = budget; b->key_limit = (budget - sizeof(*b)) / 2;
  b->peak_heap = sizeof(*b);
  b->output = fcntl(output, F_DUPFD_CLOEXEC, 0);
  b->frontier = fcntl(frontier, F_DUPFD_CLOEXEC, 0);
  b->key_file = fcntl(key_file, F_DUPFD_CLOEXEC, 0);
  if (b->output < 0 || b->frontier < 0 || b->key_file < 0) {
    cetta_act_builder_close(b); return CETTA_ACT_IO;
  }
  unsigned char header[16] = "ACTree03";
  if (!stream_append(b, header, sizeof(header))) {
    cetta_act_builder_close(b); return CETTA_ACT_IO;
  }
  *out = b; return CETTA_ACT_OK;
}
static CettaActStatus builder_put_key(CettaActBuilder *b,const InputKey *key,uint64_t value) {
  if (!b || b->finished || b->finishing) return CETTA_ACT_CORRUPT;
  if (b->failure != CETTA_ACT_OK) return b->failure;
  if (key->size>INT64_MAX || b->records==UINT64_MAX)
    return builder_fail(b, CETTA_ACT_LIMIT);
  if (b->have_previous) {
    uint64_t common = 0, smaller = b->previous_size < key->size ? b->previous_size : key->size;
    int order = 0;
    while (common < smaller) {
      size_t amount = smaller - common < sizeof(b->scratch) ? (size_t)(smaller - common) : sizeof(b->scratch);
      if (!previous_read(b,common,b->scratch,amount) || !input_read(key,common,b->incoming,amount)) return builder_fail(b,CETTA_ACT_IO);
      size_t i=0;
      while (i<amount && b->scratch[i]==b->incoming[i]) i++;
      common+=i;
      if (i<amount) {order=b->scratch[i]<b->incoming[i] ? -1 : 1;break;}
    }
    if (!order) order = (b->previous_size > key->size) - (b->previous_size < key->size);
    if (order >= 0) return builder_fail(b, CETTA_ACT_CORRUPT);
    Written result = {.valued = true, .value = b->value};
    uint64_t depth = b->previous_size;
    while (b->active && b->frame.depth > common) {
      if (!frame_attach(b, result, depth) || !frame_finish(b, &result, &depth))
        return builder_fail(b, CETTA_ACT_IO);
    }
    if (b->active && b->frame.depth == common) {
      if (!frame_attach(b, result, depth)) return builder_fail(b, CETTA_ACT_IO);
    } else {
      if (b->active && !frame_save(b)) return builder_fail(b, CETTA_ACT_IO);
      b->frame.count = 0; b->frame.depth = common;
      b->frame.valued = common == depth;
      b->frame.value = b->frame.valued ? result.value : 0;
      b->active = true;
      if (!b->frame.valued && !frame_attach(b, result, depth))
        return builder_fail(b, CETTA_ACT_IO);
    }
  }
  CettaActStatus s = previous_save(b, key);
  if (s != CETTA_ACT_OK) return builder_fail(b, s);
  b->value = value; ++b->records; return CETTA_ACT_OK;
}

CettaActStatus cetta_act_builder_put(CettaActBuilder *b,CettaActBytes key,uint64_t value) {
  if (key.size && !key.data) return b ? builder_fail(b,CETTA_ACT_CORRUPT) : CETTA_ACT_CORRUPT;
  InputKey input={.data=key.data,.fd=-1,.size=key.size};return builder_put_key(b,&input,value);
}
CettaActStatus cetta_act_builder_put_fd(CettaActBuilder *b,int fd,uint64_t offset,uint64_t size,uint64_t value) {
  if (!b) return CETTA_ACT_CORRUPT;
  if (b->failure!=CETTA_ACT_OK) return b->failure;
  struct stat source,other;
  if (fstat(fd,&source)) return builder_fail(b,CETTA_ACT_IO);
  if (!S_ISREG(source.st_mode) || source.st_size<0 || offset>(uint64_t)source.st_size || size>(uint64_t)source.st_size-offset)
    return builder_fail(b,CETTA_ACT_CORRUPT);
  int owned[]={b->output,b->frontier,b->key_file};
  for (unsigned i=0;i<3;i++) if (owned[i]>=0) {
    if (fstat(owned[i],&other)) return builder_fail(b,CETTA_ACT_IO);
    if (source.st_dev==other.st_dev && source.st_ino==other.st_ino) return builder_fail(b,CETTA_ACT_CORRUPT);
  }
  InputKey input={.fd=fd,.offset=offset,.size=size};return builder_put_key(b,&input,value);
}
CettaActStatus cetta_act_builder_finish_step(CettaActBuilder *b,uint64_t work,uint64_t *size) {
  if (!b || !size) return CETTA_ACT_CORRUPT;
  *size=0;
  if (b->failure!=CETTA_ACT_OK) return b->failure;
  if (b->finished) {*size=b->end;return CETTA_ACT_OK;}
  if (!work) return CETTA_ACT_PROGRESS;
  if (!b->finishing) {
    b->finishing=true;b->final=(Written){.valued=b->have_previous,.value=b->value};
    b->final_depth=b->previous_size;
  }
  while (b->active && work) {
    uint64_t before=b->end,frontier=b->frontier_end;
    if (!frame_attach(b,b->final,b->final_depth) || !frame_finish(b,&b->final,&b->final_depth))
      return builder_fail(b,CETTA_ACT_IO);
    uint64_t emitted=b->end-before,read=frontier-b->frontier_end;
    if (emitted>=work || read>=work-emitted) work=0;
    else work-=emitted+read+1;
  }
  if (b->active || !work) return CETTA_ACT_PROGRESS;
  size_t root;unsigned char tail[8]={0},encoded[8];
  if (!wrap_previous(b,&b->final,0,b->final_depth) || !stream_emit(b,&b->final,&root) ||
      !stream_append(b,tail,sizeof(tail)) || !stream_flush(b)) return builder_fail(b,CETTA_ACT_IO);
  write64(encoded,root);
  if (b->memory) memcpy(b->memory_output.data+8,encoded,sizeof(encoded));
  else if (!file_at(b->output,encoded,sizeof(encoded),8,true)) return builder_fail(b,CETTA_ACT_IO);
  b->finished=true;*size=b->end;return CETTA_ACT_OK;
}
CettaActStatus cetta_act_builder_finish(CettaActBuilder *b,uint64_t *size) {
  CettaActStatus s;
  do {s=cetta_act_builder_finish_step(b,UINT64_MAX,size);} while (s==CETTA_ACT_PROGRESS);
  return s;
}
bool cetta_act_builder_info(const CettaActBuilder *b, CettaActBuildInfo *out) {
  if (!b || !out) return false;
  *out = (CettaActBuildInfo){.records = b->records, .output_bytes = b->end,
      .scratch_bytes = b->frontier_end + (b->spooled ? b->previous_size : 0),
      .heap_bytes = sizeof(*b) + b->previous.cap + b->memory_output.cap + b->memory_frontier.cap,
      .peak_heap_bytes = b->peak_heap};
  return true;
}

void cetta_act_builder_storage(const CettaActBuilder *b,CettaStorageCensus *census) {
  if (!b || !cetta_storage_object(census,b,sizeof(*b),CETTA_STORAGE_BLOCK)) return;
  cetta_storage_array(census,b->previous.data,b->previous.cap,1,0);
  cetta_storage_array(census,b->memory_output.data,b->memory_output.cap,1,0);
  cetta_storage_array(census,b->memory_frontier.data,b->memory_frontier.cap,1,0);
}
static CettaActStatus build_memory(const CettaActItem *items,size_t count,
                                    unsigned char **out,size_t *size) {
  if (!out || !size || (count && !items)) return CETTA_ACT_CORRUPT;
  *out=NULL;*size=0;
  for (size_t i=0;i<count;i++)
    if (items[i].key.size && !items[i].key.data) return CETTA_ACT_CORRUPT;
  CettaActBuilder *b=calloc(1,sizeof(*b));
  if (!b) return CETTA_ACT_NOMEM;
  b->output=b->frontier=b->key_file=-1;b->memory=true;
  b->budget=SIZE_MAX;b->key_limit=SIZE_MAX-sizeof(*b);
  unsigned char header[16]="ACTree03";
  CettaActStatus s=stream_append(b,header,sizeof(header)) ? CETTA_ACT_OK : CETTA_ACT_NOMEM;
  for (size_t i=0;s==CETTA_ACT_OK && i<count;i++) s=cetta_act_builder_put(b,items[i].key,items[i].value);
  uint64_t length;
  if (s==CETTA_ACT_OK) s=cetta_act_builder_finish(b,&length);
  if (s==CETTA_ACT_OK) {
    *out=b->memory_output.data;*size=b->memory_output.len;b->memory_output=(Buffer){0};
  }
  cetta_act_builder_close(b);return s;
}

struct CettaActPublication {
  int directory,output;
  char *name;
  char temporary[48];
  bool renamed,committed,attempted,private_output;
};
static bool publication_sync(int fd) {
  int status;do {status=fsync(fd);} while (status<0 && errno==EINTR);return status==0;
}
static bool temporary_name(char out[48],const char *prefix) {
  unsigned char nonce[16];int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
  if (fd<0) return false;
  size_t at=0;
  while (at<sizeof(nonce)) {
    ssize_t n=read(fd,nonce+at,sizeof(nonce)-at);
    if (n<0 && errno==EINTR) continue;
    if (n<=0) {close(fd);return false;}at+=(size_t)n;
  }
  close(fd);size_t n=strlen(prefix);memcpy(out,prefix,n);
  static const char digits[]="0123456789abcdef";
  for (unsigned i=0;i<16;i++) {out[n+2*i]=digits[nonce[i]>>4];out[n+2*i+1]=digits[nonce[i]&15];}
  out[n+32]=0;return true;
}
void cetta_act_publication_close(CettaActPublication *p) {
  if (!p) return;
  if (p->output>=0) close(p->output);
  if (p->directory>=0) {
    if (!p->renamed && p->temporary[0]) unlinkat(p->directory,p->temporary,0);
    close(p->directory);
  }
  free(p->name);free(p);
}
CettaActStatus cetta_act_publication_open(const char *path,CettaActPublication **out) {
  if (!out || !path || !*path) return CETTA_ACT_CORRUPT;
  *out=NULL;char *parent=strdup(path);
  if (!parent) return CETTA_ACT_NOMEM;
  char *slash=strrchr(parent,'/');const char *name=slash ? slash+1 : parent;
  if (!*name || !strcmp(name,".") || !strcmp(name,"..")) {free(parent);return CETTA_ACT_CORRUPT;}
  CettaActPublication *p=calloc(1,sizeof(*p));
  if (!p) {free(parent);return CETTA_ACT_NOMEM;}
  p->directory=p->output=-1;p->name=strdup(name);
  if (!p->name) {free(parent);cetta_act_publication_close(p);return CETTA_ACT_NOMEM;}
  const char *directory=".";
  if (slash) {if (slash==parent) slash[1]=0;else *slash=0;directory=parent;}
  p->directory=open(directory,O_RDONLY|O_DIRECTORY|O_CLOEXEC);free(parent);
  if (p->directory<0) {cetta_act_publication_close(p);return CETTA_ACT_IO;}
  for (;;) {
    p->temporary[0]=0;
    if (!temporary_name(p->temporary,".act-publish-")) {cetta_act_publication_close(p);return CETTA_ACT_IO;}
    /* Created as any file written there would be: 0666 under the umask and
     * the directory's default ACL, so a published image is as readable as its
     * neighbours.  Private outputs and scratch files stay 0600. */
    p->output=openat(p->directory,p->temporary,O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC,0666);
    if (p->output>=0) break;
    if (errno!=EEXIST) {p->temporary[0]=0;cetta_act_publication_close(p);return CETTA_ACT_IO;}
    p->temporary[0]=0;
  }
  *out=p;return CETTA_ACT_OK;
}
int cetta_act_publication_fd(const CettaActPublication *p) {return p ? p->output : -1;}
CettaActStatus cetta_act_publication_open_private(int directory,CettaActPublication **out) {
  if (!out || directory<0) return CETTA_ACT_CORRUPT;
  *out=NULL;CettaActPublication *p=calloc(1,sizeof(*p));
  if (!p) return CETTA_ACT_NOMEM;
  p->directory=fcntl(directory,F_DUPFD_CLOEXEC,0);p->output=-1;p->private_output=true;
  if (p->directory<0) {cetta_act_publication_close(p);return CETTA_ACT_IO;}
  CettaActStatus s=cetta_act_publication_scratch(p,&p->output);
  if (s!=CETTA_ACT_OK) {cetta_act_publication_close(p);return s;}
  *out=p;return CETTA_ACT_OK;
}
CettaActStatus cetta_act_publication_scratch(const CettaActPublication *p,int *out) {
  if (!p || !out) return CETTA_ACT_CORRUPT;
  *out=-1;
#ifdef O_TMPFILE
  int anonymous=openat(p->directory,".",O_TMPFILE|O_RDWR|O_CLOEXEC,0600);
  if (anonymous>=0) {*out=anonymous;return CETTA_ACT_OK;}
#endif
  for (;;) {
    char name[48];if (!temporary_name(name,".act-scratch-")) return CETTA_ACT_IO;
    int fd=openat(p->directory,name,O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC,0600);
    if (fd<0) {if (errno==EEXIST) continue;return CETTA_ACT_IO;}
    if (unlinkat(p->directory,name,0)) {close(fd);return CETTA_ACT_IO;}
    *out=fd;return CETTA_ACT_OK;
  }
}
CettaActStatus cetta_act_publication_commit(CettaActPublication *p) {
  if (!p) return CETTA_ACT_CORRUPT;
  if (p->committed) return CETTA_ACT_OK;
  if (p->attempted) return CETTA_ACT_UNCERTAIN;
  if (!publication_sync(p->output)) return CETTA_ACT_IO;
  if (p->private_output) {p->committed=true;return CETTA_ACT_OK;}
  p->attempted=true;
  if (renameat(p->directory,p->temporary,p->directory,p->name)) return CETTA_ACT_UNCERTAIN;
  p->renamed=true;
  if (!publication_sync(p->directory)) return CETTA_ACT_UNCERTAIN;
  p->committed=true;return CETTA_ACT_OK;
}
CettaActStatus cetta_act_publication_sidecar(CettaActPublication *p,bool retire) {
  if (!p) return CETTA_ACT_CORRUPT;
  if (p->private_output) return CETTA_ACT_OK;
  size_t n=strlen(p->name);
  if (n>SIZE_MAX-8) return CETTA_ACT_LIMIT;
  char *name=malloc(n+8);if (!name) return CETTA_ACT_NOMEM;
  memcpy(name,p->name,n);memcpy(name+n,".copies",8);
  struct stat st;CettaActStatus status=CETTA_ACT_OK;
  if (fstatat(p->directory,name,&st,AT_SYMLINK_NOFOLLOW)) {
    if (errno!=ENOENT) status=CETTA_ACT_IO;
  } else if (!S_ISREG(st.st_mode)) status=CETTA_ACT_IO;
  else if (retire) {
    if (!p->renamed) status=CETTA_ACT_UNCERTAIN;
    else if (unlinkat(p->directory,name,0) || !publication_sync(p->directory)) status=CETTA_ACT_UNCERTAIN;
  }
  free(name);return status;
}
CettaActStatus cetta_act_publish(const char *path,CettaActBytes bytes) {
  if (bytes.size && !bytes.data) return CETTA_ACT_CORRUPT;
  CettaActPublication *p=NULL;CettaActStatus status=cetta_act_publication_open(path,&p);
  if (status!=CETTA_ACT_OK) return status;
  size_t at=0;
  while (at<bytes.size) {
    ssize_t n=write(p->output,bytes.data+at,bytes.size-at);
    if (n<0 && errno==EINTR) continue;
    if (n<=0) {status=CETTA_ACT_IO;goto done;}at+=(size_t)n;
  }
  status=cetta_act_publication_commit(p);
done:
  cetta_act_publication_close(p);return status;
}
const char *cetta_act_status_name(CettaActStatus s) {
  static const char *const names[] = {
      "ok",       "end",   "not-found", "io",          "corrupt",
      "capacity", "limit", "uncertain", "unsupported", "read-only", "progress"};
  return (unsigned)s < sizeof(names) / sizeof(*names) ? names[s] : "unknown";
}
