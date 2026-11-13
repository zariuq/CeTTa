/* step_driver.c: firings read from stdin, run on the reference implementation of one step of
 * the transform calculus (tcalc_ref).
 *
 *   firing            start a firing; its terms share one scope of locals (X, Y, ...)
 *   take I.J TERM     take the occurrence made by make J of firing I; TERM is its label
 *   read I.J TERM     read it
 *   make TERM         make an occurrence labelled TERM
 *   schema TERM       make a stored schema: TERM's locals are its private variables
 *   tell A = B        tell an equation
 *   apart A # B       tell a disequation
 *   equal A = B       guard: A and B are equal at every solution
 *   absent TERM       guard: no present label equals TERM at any solution
 *   count N TERM      guard: exactly N present labels equal TERM at every solution
 *   fresh K           the firing mints K names, written #0 .. #K-1
 *   end               fire: prints "admitted" or "refused shapes|fits|unsolvable|guard"
 *   dump              prints "occ LABEL" for each present occurrence, oldest first
 */
#include "tcalc_ref.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAXF = 512, MAXM = 8, MAXP = 16 };
static tr_occ made[MAXF][MAXM];
static int nmade[MAXF];

static char *trim(char *s) {
  while (*s == ' ') s++;
  size_t n = strlen(s);
  while (n && (s[n - 1] == '\n' || s[n - 1] == ' ')) s[--n] = 0;
  return s;
}

static tr_occ occ_of(const char *ref) {
  int i = -1, j = -1;
  if (sscanf(ref, "%d.%d", &i, &j) != 2 || i < 0 || i >= MAXF || j < 0 || j >= nmade[i]) return (tr_occ)1 << 62;
  return made[i][j] ? made[i][j] : (tr_occ)1 << 62;
}

int main(void) {
  tr_ctx *c = tr_ctx_new();
  tr_state *s = tr_state_new(c);
  char line[4096];
  int firing = -1;
  tr_scope *scope = NULL;
  tr_use takes[MAXP], reads[MAXP];
  tr_make makes[MAXP];
  tr_eq tells[MAXP], apart[MAXP];
  tr_guard guards[MAXP];
  size_t nt = 0, nr = 0, nm = 0, ne = 0, na = 0, ng = 0;
  uint32_t fresh = 0;
  bool bad = false;
  while (fgets(line, sizeof line, stdin)) {
    char *p = trim(line);
    if (!strcmp(p, "firing")) {
      firing++;
      if (scope) tr_scope_free(scope);
      scope = tr_scope_new();
      nt = nr = nm = ne = na = ng = 0;
      fresh = 0;
      bad = false;
    } else if (!strncmp(p, "take ", 5) || !strncmp(p, "read ", 5)) {
      bool take = p[0] == 't';
      char *ref = p + 5, *term = strchr(ref, ' ');
      if (!term) { bad = true; continue; }
      *term++ = 0;
      tr_use u = {tr_read(c, scope, term), occ_of(ref)};
      if (u.pattern == TR_NONE) bad = true;
      if (take) takes[nt++] = u; else reads[nr++] = u;
    } else if (!strncmp(p, "make ", 5)) {
      tr_term t = tr_read(c, scope, p + 5);
      if (t == TR_NONE) bad = true;
      makes[nm++] = (tr_make){t, 0, 0};
    } else if (!strncmp(p, "schema ", 7)) {
      tr_scope *own = tr_scope_new();
      tr_term t = tr_read(c, own, p + 7);
      if (t == TR_NONE) bad = true;
      makes[nm++] = (tr_make){t, 0, tr_scope_size(own)};
      tr_scope_free(own);
    } else if (!strncmp(p, "tell ", 5) || !strncmp(p, "apart ", 6) || !strncmp(p, "equal ", 6)) {
      char kind = p[0];
      char *left = strchr(p, ' ') + 1;
      char *mid = strstr(left, kind == 'a' ? " # " : " = ");
      if (!mid) { bad = true; continue; }
      *mid = 0;
      tr_term a = tr_read(c, scope, left), b = tr_read(c, scope, mid + 3);
      if (a == TR_NONE || b == TR_NONE) bad = true;
      if (kind == 't') tells[ne++] = (tr_eq){a, b};
      else if (kind == 'a') apart[na++] = (tr_eq){a, b};
      else guards[ng++] = (tr_guard){.kind = TR_G_EQUAL, .a = a, .b = b};
    } else if (!strncmp(p, "absent ", 7)) {
      tr_term a = tr_read(c, scope, p + 7);
      if (a == TR_NONE) bad = true;
      guards[ng++] = (tr_guard){.kind = TR_G_ABSENT, .a = a};
    } else if (!strncmp(p, "count ", 6)) {
      char *term = strchr(p + 6, ' ');
      if (!term) { bad = true; continue; }
      *term++ = 0;
      tr_term a = tr_read(c, scope, term);
      if (a == TR_NONE) bad = true;
      guards[ng++] = (tr_guard){.kind = TR_G_COUNT, .a = a, .number = (size_t)atoi(p + 6)};
    } else if (!strncmp(p, "fresh ", 6)) {
      fresh = (uint32_t)atoi(p + 6);
    } else if (!strcmp(p, "end")) {
      if (bad) { puts("syntax"); continue; }
      tr_occ ids[MAXP] = {0};
      tr_result r = {.made = ids};
      tr_firing f = {.locals = tr_scope_size(scope), .fresh = fresh,
                     .takes = takes, .ntakes = nt, .reads = reads, .nreads = nr,
                     .makes = makes, .nmakes = nm, .tells = tells, .ntells = ne,
                     .apart = apart, .napart = na, .guards = guards, .nguards = ng};
      tr_verdict v = tr_fire(s, &f, &r);
      if (v == TR_ADMITTED) {
        puts("admitted");
        if (firing >= 0 && firing < MAXF) {
          nmade[firing] = (int)(nm < MAXM ? nm : MAXM);
          for (int j = 0; j < nmade[firing]; j++) made[firing][j] = ids[j];
        }
      } else if (v == TR_NO_VERDICT) {
        puts("no-verdict");
      } else {
        static const char *why[] = {"ok", "shapes", "fits", "unsolvable", "guard"};
        printf("refused %s\n", why[r.reason]);
      }
    } else if (!strcmp(p, "dump")) {
      tr_term label;
      for (tr_occ o = tr_next_present(s, 0, &label); o; o = tr_next_present(s, o, &label)) {
        tr_term stored;
        fputs("occ ", stdout);
        if (tr_present(s, o, &stored)) tr_print(stdout, c, tr_resolve(s, stored));
        putchar('\n');
      }
      puts("end");
    }
  }
  return 0;
}
