#define _POSIX_C_SOURCE 200809L
#include "act_trie.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static CettaActBytes bytes(const void *p, size_t n) {
  return (CettaActBytes){p, n};
}
static void roundtrip(const CettaActItem *items, size_t count) {
  unsigned char *image = NULL;
  size_t size = 0;
  assert(cetta_act_build(items, count, &image, &size) == CETTA_ACT_OK);
  CettaActTree *tree = NULL;
  assert(cetta_act_from_bytes(bytes(image, size), &tree) == CETTA_ACT_OK);
  uint64_t n = 0;
  assert(cetta_act_validate(tree, &n) == CETTA_ACT_OK && n == count);
  CettaActCursor *cursor = NULL;
  assert(cetta_act_cursor_open(tree, NULL, NULL, &cursor) == CETTA_ACT_OK);
  for (size_t i = 0; i < count; i++) {
    CettaActBytes key;
    uint64_t value;
    assert(cetta_act_get(tree, items[i].key, &value) == CETTA_ACT_OK &&
           value == items[i].value);
    assert(cetta_act_cursor_next(cursor, &key, &value) == CETTA_ACT_OK);
    assert(key.size == items[i].key.size &&
           (!key.size || !memcmp(key.data, items[i].key.data, key.size)) &&
           value == items[i].value);
    if (i == count / 2) {
      CettaActCursor *copy = NULL;
      assert(cetta_act_cursor_clone(cursor, false, &copy) == CETTA_ACT_OK);
      if (i + 1 < count) {
        assert(cetta_act_cursor_next(copy, &key, &value) == CETTA_ACT_OK);
        assert(value == items[i + 1].value);
      }
      cetta_act_cursor_close(copy);
    }
  }
  CettaActBytes key;
  uint64_t value;
  assert(cetta_act_cursor_next(cursor, &key, &value) == CETTA_ACT_END);
  cetta_act_cursor_close(cursor);
  cetta_act_release(tree);
  free(image);
}
int main(int argc, char **argv) {
  if (argc == 2) {
    CettaActTree *t = NULL;
    assert(cetta_act_open(argv[1], &t) == CETTA_ACT_OK);
    CettaActCursor *c = NULL;
    assert(cetta_act_cursor_open(t, NULL, NULL, &c) == CETTA_ACT_OK);
    CettaActBytes key;
    uint64_t value;
    CettaActStatus s;
    while ((s = cetta_act_cursor_next(c, &key, &value)) == CETTA_ACT_OK) {
      for (size_t i = 0; i < key.size; i++)
        printf("%02x", key.data[i]);
      printf(" %llu\n", (unsigned long long)value);
    }
    assert(s == CETTA_ACT_END);
    cetta_act_cursor_close(c);
    cetta_act_release(t);
    return 0;
  }
  uint64_t values[] = {0, 1, 247, 248, 255, 256, 65535, 65536, UINT64_MAX};
  for (size_t i = 0; i < sizeof(values) / sizeof(*values); i++) {
    unsigned char data[9];
    size_t len = cetta_act_put_varint(data, values[i]), used;
    uint64_t value;
    assert(cetta_act_varint(data, len, &value, &used) && value == values[i] &&
           used == len);
    for (size_t n = 0; n < len; n++)
      assert(!cetta_act_varint(data, n, &value, &used));
  }
  roundtrip(NULL, 0);
  CettaActItem words[] = {{bytes("", 0), 1},       {bytes("a", 1), 2},
                          {bytes("abc", 3), 3},    {bytes("abd", 3), 4},
                          {bytes("abe000", 6), 5}, {bytes("z", 1), UINT64_MAX}};
  roundtrip(words, 6);
  unsigned char keys[256];
  CettaActItem wide[257];
  wide[0] = (CettaActItem){bytes("", 0), 999};
  for (size_t i = 0; i < 256; i++) {
    keys[i] = (unsigned char)i;
    wide[i + 1] = (CettaActItem){bytes(keys + i, 1), i};
  }
  roundtrip(wide, 257);
  size_t count = 10000;
  unsigned char *data = malloc(count * 8);
  CettaActItem *items = malloc(count * sizeof(*items));
  assert(data && items);
  for (size_t i = 0; i < count; i++) {
    for (unsigned j = 0; j < 8; j++)
      data[i * 8 + j] = (unsigned char)((uint64_t)i >> (56 - 8 * j));
    items[i] = (CettaActItem){bytes(data + i * 8, 8), i};
  }
  roundtrip(items, count);
  unsigned char *image;
  size_t size;
  assert(cetta_act_build(words, 6, &image, &size) == CETTA_ACT_OK);
  CettaActTree *t;
  assert(cetta_act_from_bytes(bytes(image, size), &t) == CETTA_ACT_OK);
  CettaActCursor *c;
  assert(cetta_act_cursor_prefix(t, bytes("ab", 2), &c) == CETTA_ACT_OK);
  CettaActBytes key;
  uint64_t value;
  for (unsigned i = 3; i <= 5; i++) {
    assert(cetta_act_cursor_next(c, &key, &value) == CETTA_ACT_OK);
    assert(value == i);
  }
  assert(cetta_act_cursor_next(c, &key, &value) == CETTA_ACT_END);
  cetta_act_cursor_close(c);
  CettaActBytes lo = bytes("abc", 3), hi = bytes("abe", 3);
  assert(cetta_act_cursor_open(t, &lo, &hi, &c) == CETTA_ACT_OK);
  assert(cetta_act_cursor_next(c, &key, &value) == CETTA_ACT_OK && value == 3);
  assert(cetta_act_cursor_next(c, &key, &value) == CETTA_ACT_OK && value == 4);
  assert(cetta_act_cursor_next(c, &key, &value) == CETTA_ACT_END);
  cetta_act_cursor_close(c);
  for (size_t i = 0; i < size; i++) {
    CettaActTree *bad = NULL;
    CettaActStatus s = cetta_act_from_bytes(bytes(image, i), &bad);
    if (s == CETTA_ACT_OK) {
      uint64_t n;
      cetta_act_validate(bad, &n);
      cetta_act_release(bad);
    }
  }
  for (size_t i = 0; i < size; i++) {
    unsigned char prev = image[i];
    image[i] ^= 255;
    CettaActTree *bad = NULL;
    if (cetta_act_from_bytes(bytes(image, size), &bad) == CETTA_ACT_OK) {
      uint64_t n;
      cetta_act_validate(bad, &n);
      cetta_act_release(bad);
    }
    image[i] = prev;
  }
  char path[] = "runtime/act-test-XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  close(fd);
  assert(cetta_act_publish(path, bytes(image, size)) == CETTA_ACT_OK);
  CettaActTree *mapped;
  assert(cetta_act_open(path, &mapped) == CETTA_ACT_OK);
  assert(cetta_act_publish(path, bytes(image, size)) == CETTA_ACT_OK);
  unlink(path);
  assert(cetta_act_get(mapped, bytes("z", 1), &value) == CETTA_ACT_OK &&
         value == UINT64_MAX);
  cetta_act_release(mapped);
  cetta_act_release(t);
  free(image);
  free(items);
  free(data);
  puts("ACT trie checks passed");
  return 0;
}
