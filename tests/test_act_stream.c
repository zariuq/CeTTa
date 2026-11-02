#define _POSIX_C_SOURCE 200809L
#include "act_trie.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void check(const CettaActItem *items, size_t count, size_t budget) {
  char names[3][64]; int files[3];
  for (unsigned i = 0; i < 3; i++) {
    snprintf(names[i], sizeof(names[i]), "runtime/act-stream-%u-XXXXXX", i);
    files[i] = mkstemp(names[i]); assert(files[i] >= 0);
  }
  CettaActBuilder *b = NULL;
  assert(cetta_act_builder_open(files[0], files[1], files[2], budget, &b) == CETTA_ACT_OK);
  CettaActBuildInfo info;
  for (size_t i = 0; i < count; i++) {
    assert(cetta_act_builder_put(b, items[i].key, items[i].value) == CETTA_ACT_OK);
    assert(cetta_act_builder_info(b, &info));
    assert(info.records == i + 1 && info.heap_bytes <= budget && info.peak_heap_bytes <= budget);
  }
  uint64_t size = 0, repeated;
  assert(cetta_act_builder_finish(b, &size) == CETTA_ACT_OK);
  assert(cetta_act_builder_finish(b, &repeated) == CETTA_ACT_OK && size == repeated);
  assert(cetta_act_builder_put(b, (CettaActBytes){NULL, 0}, 0) == CETTA_ACT_CORRUPT);
  cetta_act_builder_close(b);
  struct stat st; assert(!fstat(files[0], &st) && (uint64_t)st.st_size == size);
  CettaActTree *t = NULL; uint64_t n = UINT64_MAX;
  assert(cetta_act_open(names[0], &t) == CETTA_ACT_OK);
  assert(cetta_act_validate(t, &n) == CETTA_ACT_OK && n == count);
  CettaActCursor *c = NULL;
  assert(cetta_act_cursor_open(t, NULL, NULL, &c) == CETTA_ACT_OK);
  for (size_t i = 0; i < count; i++) {
    CettaActBytes key; uint64_t value;
    assert(cetta_act_cursor_next(c, &key, &value) == CETTA_ACT_OK);
    assert(key.size == items[i].key.size && value == items[i].value &&
           (!key.size || !memcmp(key.data, items[i].key.data, key.size)));
    assert(cetta_act_get(t, items[i].key, &value) == CETTA_ACT_OK && value == items[i].value);
  }
  CettaActBytes key; uint64_t value;
  assert(cetta_act_cursor_next(c, &key, &value) == CETTA_ACT_END);
  cetta_act_cursor_close(c);
  assert(cetta_act_cursor_open(t,NULL,NULL,&c)==CETTA_ACT_OK);
  unsigned pauses=0;
  for (size_t i=0;i<count;i++) {
    CettaActStatus status;
    do {
      status=cetta_act_cursor_next_bounded(c,1,&key,&value);
      if (status==CETTA_ACT_PROGRESS) ++pauses;
    } while (status==CETTA_ACT_PROGRESS);
    assert(status==CETTA_ACT_OK && value==items[i].value);
  }
  CettaActStatus status;
  do {status=cetta_act_cursor_next_bounded(c,1,&key,&value);} while (status==CETTA_ACT_PROGRESS);
  assert(status==CETTA_ACT_END);
  if (count>3) assert(pauses);
  cetta_act_cursor_close(c);
  assert(cetta_act_cursor_open_reverse(t,NULL,NULL,&c)==CETTA_ACT_OK);
  for (size_t i=count;i>0;i--) {
    assert(cetta_act_cursor_next(c,&key,&value)==CETTA_ACT_OK);
    assert(value==items[i-1].value && key.size==items[i-1].key.size &&
           (!key.size || !memcmp(key.data,items[i-1].key.data,key.size)));
    if (i==count/2) {
      CettaActCursor *copy=NULL;
      assert(cetta_act_cursor_clone(c,false,&copy)==CETTA_ACT_OK);
      if (i>1) assert(cetta_act_cursor_next(copy,&key,&value)==CETTA_ACT_OK && value==items[i-2].value);
      cetta_act_cursor_close(copy);
    }
  }
  assert(cetta_act_cursor_next(c,&key,&value)==CETTA_ACT_END);
  cetta_act_cursor_close(c);
  if (count>3) {
    size_t lo=count/4,hi=count-count/4;
    assert(cetta_act_cursor_open_reverse(t,&items[lo].key,&items[hi].key,&c)==CETTA_ACT_OK);
    for (size_t i=hi;i>lo;i--)
      assert(cetta_act_cursor_next(c,&key,&value)==CETTA_ACT_OK && value==items[i-1].value);
    assert(cetta_act_cursor_next(c,&key,&value)==CETTA_ACT_END);
    cetta_act_cursor_close(c);
  }
  cetta_act_release(t);
  for (unsigned i = 0; i < 3; i++) { close(files[i]); assert(!unlink(names[i])); }
}
int main(void) {
  const size_t budget = 256 * 1024;
  check(NULL, 0, budget);
  CettaActItem words[] = {
    {{(const unsigned char *)"",0},1}, {{(const unsigned char *)"a",1},2},
    {{(const unsigned char *)"abc",3},3}, {{(const unsigned char *)"abd",3},4},
    {{(const unsigned char *)"abe000",6},5}, {{(const unsigned char *)"z",1},UINT64_MAX}
  };
  check(words, sizeof(words) / sizeof(*words), budget);
  unsigned char labels[256]; CettaActItem wide[257];
  wide[0] = words[0];
  for (unsigned i = 0; i < 256; i++) {
    labels[i] = (unsigned char)i;
    wide[i + 1] = (CettaActItem){{labels + i,1},i};
  }
  check(wide, 257, budget);
  size_t count = 65536;
  unsigned char *keys = malloc(count * 8); CettaActItem *items = malloc(count * sizeof(*items));
  assert(keys && items);
  for (size_t i = 0; i < count; i++) {
    for (unsigned j = 0; j < 8; j++) keys[8*i+j] = (unsigned char)((uint64_t)i >> (56-8*j));
    items[i] = (CettaActItem){{keys + i*8,8},i * 91};
  }
  check(items, count, budget); free(items); free(keys);
  size_t depth = 1500;
  keys = malloc(depth); items = malloc(depth * sizeof(*items)); assert(keys && items);
  memset(keys, 'a', depth);
  for (size_t i = 0; i < depth; i++) items[i] = (CettaActItem){{keys,i+1},i};
  check(items, depth, budget); free(items); free(keys);
  size_t big = 1024 * 1024;
  keys = malloc(big * 2); assert(keys); memset(keys, 'a', big * 2);
  keys[big-1] = 0; keys[big*2-1] = 1;
  CettaActItem long_keys[] = {{{keys,big},77}, {{keys+big,big},99}};
  check(long_keys, 2, budget); free(keys);
  char names[3][64]; int fd[3];
  for (unsigned i=0;i<3;i++) {
    snprintf(names[i],sizeof(names[i]),"runtime/act-failure-%u-XXXXXX",i);
    fd[i]=mkstemp(names[i]);assert(fd[i]>=0);
  }
  CettaActBuilder *b = NULL;
  assert(cetta_act_builder_open(fd[0],fd[0],fd[2],budget,&b)==CETTA_ACT_CORRUPT && !b);
  assert(cetta_act_builder_open(fd[0],fd[1],fd[2],1,&b)==CETTA_ACT_LIMIT && !b);
  assert(cetta_act_builder_open(fd[0],fd[1],fd[2],budget,&b)==CETTA_ACT_OK);
  assert(cetta_act_builder_put(b,words[1].key,1)==CETTA_ACT_OK);
  assert(cetta_act_builder_put(b,words[1].key,2)==CETTA_ACT_CORRUPT);
  uint64_t size;
  assert(cetta_act_builder_finish(b,&size)==CETTA_ACT_CORRUPT && !size);
  cetta_act_builder_close(b);
  for (unsigned i=0;i<3;i++) {close(fd[i]);assert(!unlink(names[i]));}
  puts("ACT streaming checks passed");
}
