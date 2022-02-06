// Copyright 2021 Bradley D. Nelson
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#define PRINT_ERRORS 0

#define CELL_MASK (sizeof(cell_t) - 1)
#define CELL_LEN(n) (((n) + CELL_MASK) / sizeof(cell_t))
#define FIND(name) find((name), sizeof(name) - 1)
#define UPPER(ch) (((ch) >= 'a' && (ch) <= 'z') ? ((ch) & 0x5F) : (ch))
#define CELL_ALIGNED(a) ((((cell_t) (a)) + CELL_MASK) & ~CELL_MASK)
#define IMMEDIATE 1
#define SMUDGE 2

// Maximum ALSO layers.
#define VOCABULARY_DEPTH 16

#if PRINT_ERRORS
#include <stdio.h>
#endif

static struct {
  cell_t *heap, **current, ***context;
  cell_t *latestxt, notfound;
  cell_t *heap_start;
  cell_t heap_size, stack_cells;
  const char *boot;
  cell_t boot_size;
  const char *tib;
  cell_t ntib, tin, state, base;
  int argc;
  char **argv;
  cell_t *(*runner)(cell_t *rp);  // pointer to forth_run

  // Layout not used by Forth.
  cell_t *rp;  // spot to park main thread
  cell_t DOLIT_XT, DOFLIT_XT, DOEXIT_XT, YIELD_XT;
} g_sys;

static cell_t convert(const char *pos, cell_t n, cell_t base, cell_t *ret) {
  *ret = 0;
  cell_t negate = 0;
  if (!n) { return 0; }
  if (*pos == '-') { negate = -1; ++pos; --n; }
  if (*pos == '$') { base = 16; ++pos; --n; }
  for (; n; --n) {
    uintptr_t d = UPPER(*pos) - '0';
    if (d > 9) {
      d -= 7;
      if (d < 10) { return 0; }
    }
    if (d >= (uintptr_t) base) { return 0; }
    *ret = *ret * base + d;
    ++pos;
  }
  if (negate) { *ret = -*ret; }
  return -1;
}

static cell_t fconvert(const char *pos, cell_t n, float *ret) {
  *ret = 0;
  cell_t negate = 0;
  cell_t has_dot = 0;
  cell_t exp = 0;
  float shift = 1.0;
  if (!n) { return 0; }
  if (*pos == '-') { negate = -1; ++pos; --n; }
  for (; n; --n) {
    if (*pos >= '0' && *pos <= '9') {
      if (has_dot) {
        shift = shift * 0.1f;
        *ret = *ret + (*pos - '0') * shift;
      } else {
        *ret = *ret * 10 + (*pos - '0');
      }
    } else if (*pos == 'e' || *pos == 'E') {
      break;
    } else if (*pos == '.') {
      if (has_dot) { return 0; }
      has_dot = -1;
    }
    ++pos;
  }
  if (!n) { return 0; }  // must have E
  ++pos; --n;
  if (n) {
    if (!convert(pos, n, 10, &exp)) { return 0; }
  }
  if (exp < -128 || exp > 128) { return 0; }
  for (;exp < 0; ++exp) { *ret *= 0.1f; }
  for (;exp > 0; --exp) { *ret *= 10.0f; }
  if (negate) { *ret = -*ret; }
  return -1;
}

static cell_t same(const char *a, const char *b, cell_t len) {
  for (;len && UPPER(*a) == UPPER(*b); --len, ++a, ++b);
  return len == 0;
}

static cell_t find(const char *name, cell_t len) {
  for (cell_t ***voc = g_sys.context; *voc; ++voc) {
    cell_t xt = (cell_t) **voc;
    while (xt) {
      if (!(*TOFLAGS(xt) & SMUDGE) && len == *TONAMELEN(xt) &&
          same(name, TONAME(xt), len)) {
        return xt;
      }
      xt = *TOLINK(xt);
    }
  }
  return 0;
}

static void finish(void) {
  if (g_sys.latestxt && !*TOPARAMS(g_sys.latestxt)) {
    cell_t sz = g_sys.heap - &g_sys.latestxt[1];
    if (sz < 0 || sz > 0xffff) { sz = 0xffff; }
    *TOPARAMS(g_sys.latestxt) = sz;
  }
}

static void create(const char *name, cell_t nlength, cell_t flags, void *op) {
  finish();
  g_sys.heap = (cell_t *) CELL_ALIGNED(g_sys.heap);
  char *pos = (char *) g_sys.heap;
  for (cell_t n = nlength; n; --n) { *pos++ = *name++; }  // name
  g_sys.heap += CELL_LEN(nlength);
  *g_sys.heap++ = (cell_t) *g_sys.current;  // link
  *g_sys.heap++ = (nlength << 8) | flags;  // flags & length
  *g_sys.current = g_sys.heap;
  g_sys.latestxt = g_sys.heap;
  *g_sys.heap++ = (cell_t) op;  // code
}

static int match(char sep, char ch) {
  return sep == ch || (sep == ' ' && (ch == '\t' || ch == '\n' || ch == '\r'));
}

static cell_t parse(cell_t sep, cell_t *ret) {
  if (sep == ' ') {
    while (g_sys.tin < g_sys.ntib &&
           match(sep, g_sys.tib[g_sys.tin])) { ++g_sys.tin; }
  }
  *ret = (cell_t) (g_sys.tib + g_sys.tin);
  while (g_sys.tin < g_sys.ntib &&
         !match(sep, g_sys.tib[g_sys.tin])) { ++g_sys.tin; }
  cell_t len = g_sys.tin - (*ret - (cell_t) g_sys.tib);
  if (g_sys.tin < g_sys.ntib) { ++g_sys.tin; }
  return len;
}

static cell_t *evaluate1(cell_t *sp, float **fp) {
  cell_t call = 0;
  cell_t name;
  cell_t len = parse(' ', &name);
  if (len == 0) { *++sp = 0; return sp; }  // ignore empty
  cell_t xt = find((const char *) name, len);
  if (xt) {
    if (g_sys.state && !(((cell_t *) xt)[-1] & IMMEDIATE)) {
      *g_sys.heap++ = xt;
    } else {
      call = xt;
    }
  } else {
    cell_t n;
    if (convert((const char *) name, len, g_sys.base, &n)) {
      if (g_sys.state) {
        *g_sys.heap++ = g_sys.DOLIT_XT;
        *g_sys.heap++ = n;
      } else {
        *++sp = n;
      }
    } else {
      float f;
      if (fconvert((const char *) name, len, &f)) {
        if (g_sys.state) {
          *g_sys.heap++ = g_sys.DOFLIT_XT;
          *(float *) g_sys.heap++ = f;
        } else {
          *++(*fp) = f;
        }
      } else {
#if PRINT_ERRORS
        fprintf(stderr, "CANT FIND: ");
        fwrite((void *) name, 1, len, stderr);
        fprintf(stderr, "\n");
#endif
        *++sp = name;
        *++sp = len;
        *++sp = -1;
        call = g_sys.notfound;
      }
    }
  }
  *++sp = call;
  return sp;
}

static cell_t *forth_run(cell_t *initrp);

static void forth_init(int argc, char *argv[], void *heap,
                       const char *src, cell_t src_len) {
  g_sys.heap_start = (cell_t *) heap;
  g_sys.heap_size = HEAP_SIZE;
  g_sys.stack_cells = STACK_CELLS;
  g_sys.boot = src;
  g_sys.boot_size = src_len;

  g_sys.heap = g_sys.heap_start + 4;  // Leave a little room.
  float *fp = (float *) (g_sys.heap + 1); g_sys.heap += STACK_CELLS;
  cell_t *rp = g_sys.heap + 1; g_sys.heap += STACK_CELLS;
  cell_t *sp = g_sys.heap + 1; g_sys.heap += STACK_CELLS;

  // FORTH worldlist (relocated when vocabularies added).
  cell_t *forth_wordlist = g_sys.heap;
  *g_sys.heap++ = 0;
  // Vocabulary stack
  g_sys.current = (cell_t **) forth_wordlist;
  g_sys.context = (cell_t ***) g_sys.heap;
  g_sys.latestxt = 0;
  *g_sys.heap++ = (cell_t) forth_wordlist;
  for (int i = 0; i < VOCABULARY_DEPTH; ++i) { *g_sys.heap++ = 0; }

  forth_run(0);
  g_sys.latestxt = 0;  // So ; doesn't get wrong size.
  g_sys.DOLIT_XT = FIND("DOLIT");
  g_sys.DOFLIT_XT = FIND("DOFLIT");
  g_sys.DOEXIT_XT = FIND("EXIT");
  g_sys.YIELD_XT = FIND("YIELD");
  g_sys.notfound = FIND("DROP");
  cell_t *start = g_sys.heap;
  *g_sys.heap++ = FIND("EVALUATE1");
  *g_sys.heap++ = FIND("BRANCH");
  *g_sys.heap++ = (cell_t) start;
  g_sys.argc = argc;
  g_sys.argv = argv;
  g_sys.base = 10;
  g_sys.tib = src;
  g_sys.ntib = src_len;
  *++rp = (cell_t) fp;
  *++rp = (cell_t) sp;
  *++rp = (cell_t) start;
  g_sys.rp = rp;
  g_sys.runner = forth_run;
}
