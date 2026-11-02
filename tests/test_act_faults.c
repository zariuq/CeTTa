#define _POSIX_C_SOURCE 200809L
#include "act_trie.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static size_t allocation, fail_allocation, syncs, fail_sync;
static bool fail_write, fail_rename;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
ssize_t __real_write(int, const void *, size_t);
int __real_fsync(int);
int __real_rename(const char *, const char *);
static bool denied(void) {
  return fail_allocation && ++allocation == fail_allocation;
}
void *__wrap_malloc(size_t n) { return denied() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t w) {
  return denied() ? NULL : __real_calloc(n, w);
}
void *__wrap_realloc(void *p, size_t n) {
  return denied() ? NULL : __real_realloc(p, n);
}
ssize_t __wrap_write(int fd, const void *p, size_t n) {
  if (fail_write) {
    errno = EIO;
    return -1;
  }
  return __real_write(fd, p, n);
}
int __wrap_fsync(int fd) {
  if (fail_sync && ++syncs == fail_sync) {
    errno = EIO;
    return -1;
  }
  return __real_fsync(fd);
}
int __wrap_rename(const char *a, const char *b) {
  if (fail_rename) {
    errno = EIO;
    return -1;
  }
  return __real_rename(a, b);
}
static uint64_t read_value(const char *path) {
  CettaActTree *tree = NULL;
  uint64_t value = 0;
  assert(cetta_act_open(path, &tree) == CETTA_ACT_OK);
  assert(cetta_act_validate(tree, NULL) == CETTA_ACT_OK);
  assert(cetta_act_get(tree, (CettaActBytes){(const unsigned char *)"abc", 3},
                       &value) == CETTA_ACT_OK);
  cetta_act_release(tree);
  return value;
}
int main(void) {
  const char *keys[] = {"", "a", "ab", "abc", "abd", "abcde", "z"};
  /* Strict byte order, including keys which are prefixes of another. */
  const unsigned order[] = {0, 1, 2, 3, 5, 4, 6};
  CettaActItem items[7];
  for (size_t i = 0; i < 7; i++)
    items[i] = (CettaActItem){
        {(const unsigned char *)keys[order[i]], strlen(keys[order[i]])}, i};
  size_t faults = 0;
  for (size_t n = 1; n < 100; n++) {
    allocation = 0;
    fail_allocation = n;
    unsigned char *image = NULL;
    size_t size = 0;
    CettaActStatus status = cetta_act_build(items, 7, &image, &size);
    fail_allocation = 0;
    if (status == CETTA_ACT_NOMEM) {
      assert(!image && !size);
      faults++;
    } else {
      assert(status == CETTA_ACT_OK && image && size);
      free(image);
    }
  }
  assert(faults > 10);
  CettaActItem item = {{(const unsigned char *)"abc", 3}, 1};
  unsigned char *old, *next;
  size_t old_size, next_size;
  assert(cetta_act_build(&item, 1, &old, &old_size) == CETTA_ACT_OK);
  item.value = 2;
  assert(cetta_act_build(&item, 1, &next, &next_size) == CETTA_ACT_OK);
  char path[] = "runtime/act-publish-XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  close(fd);
  assert(cetta_act_publish(path, (CettaActBytes){old, old_size}) ==
         CETTA_ACT_OK);
  fail_write = true;
  assert(cetta_act_publish(path, (CettaActBytes){next, next_size}) ==
         CETTA_ACT_IO);
  fail_write = false;
  assert(read_value(path) == 1);
  syncs = 0;
  fail_sync = 1;
  assert(cetta_act_publish(path, (CettaActBytes){next, next_size}) ==
         CETTA_ACT_IO);
  fail_sync = 0;
  assert(read_value(path) == 1);
  fail_rename = true;
  assert(cetta_act_publish(path, (CettaActBytes){next, next_size}) ==
         CETTA_ACT_IO);
  fail_rename = false;
  assert(read_value(path) == 1);
  syncs = 0;
  fail_sync = 2;
  assert(cetta_act_publish(path, (CettaActBytes){next, next_size}) ==
         CETTA_ACT_UNCERTAIN);
  fail_sync = 0;
  assert(read_value(path) == 2);
  unlink(path);
  free(old);
  free(next);
  printf("ACT faults: %zu allocation failures and four publication cutpoints\n",
         faults);
  return 0;
}
