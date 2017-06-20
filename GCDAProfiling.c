/*===- GCDAProfiling.c - Support library for GCDA file emission -----------===*\
|*
|*                     The LLVM Compiler Infrastructure
|*
|* This file is distributed under the University of Illinois Open Source
|* License. See LICENSE.TXT for details.
|* 
|*===----------------------------------------------------------------------===*|
|* 
|* This file implements the call back routines for the gcov profiling
|* instrumentation pass. Link against this library when running code through
|* the -insert-gcov-profiling LLVM pass.
|*
|* We emit files in a corrupt version of GCOV's "gcda" file format. These files
|* are only close enough that LCOV will happily parse them. Anything that lcov
|* ignores is missing.
|*
|* TODO: gcov is multi-process safe by having each exit open the existing file
|* and append to it. We'd like to achieve that and be thread-safe too.
|*
\*===----------------------------------------------------------------------===*/

#include <libcgc.h>

typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned char uint8_t;

int printf(const char *fmt, ...);

static void *memset(void *s, int c, size_t n)
{
    size_t i;
    unsigned char *buf = s;

    for (i = 0; i < n; i++)
        buf[i] = c;
    return s;
}

static size_t strlen(const char *s)
{
    size_t n = 0;
    while (*s++)
        n++;
    return n;
}

static char *malloc(size_t length) {
  void *addr;
  int r = allocate(length + sizeof(size_t), 0, &addr);
  if (r != 0)
    return NULL;

  *(size_t*)addr = length; 
  return (char *)addr + sizeof(size_t);
}

static void free(void *addr) {
  if (addr == NULL)
    return;

  void *origin_addr = (char *)addr - sizeof(size_t);
  size_t length = *(size_t*) origin_addr;

  deallocate(origin_addr, length);
}


static void *realloc(void *ptr, size_t new_length) {
  free(ptr);
  return malloc(new_length);
}


static void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *udst = dst;
    const unsigned char *usrc = src;
    size_t i;

    for (i = 0; i < n; i++)
        udst[i] = usrc[i];

    return dst;
}

static char *strdup(const char *s1)
{
  size_t len = strlen(s1);
  char *cpy = malloc(len + 1);

  if (!cpy)
    return NULL;

  memcpy(cpy, s1, len);
  cpy[len] = '\0';

  return cpy;
}

/*
 * --- GCOV file format I/O primitives ---
 */

/*
 * The current file name we're outputting. Used primarily for error logging.
 */
static char filename[1024];

/*
 * Buffer that we write things into.
 */
#define WRITE_BUFFER_SIZE (16 * 1024 * 1024)
static char *write_buffer = NULL;
static uint64_t cur_buffer_size = 0;
static uint64_t cur_pos = 0;

/*
 * A list of functions to write out the data.
 */
typedef void (*writeout_fn)();

struct writeout_fn_node {
  writeout_fn fn;
  struct writeout_fn_node *next;
};

static struct writeout_fn_node *writeout_fn_head = NULL;
static struct writeout_fn_node *writeout_fn_tail = NULL;

/*
 *  A list of flush functions that our __gcov_flush() function should call.
 */
typedef void (*flush_fn)();

struct flush_fn_node {
  flush_fn fn;
  struct flush_fn_node *next;
};

static struct flush_fn_node *flush_fn_head = NULL;
static struct flush_fn_node *flush_fn_tail = NULL;

static void write_bytes(const char *s, size_t len) {
  memcpy(&write_buffer[cur_pos], s, len);
  cur_pos += len;
}

static void write_32bit_value(uint32_t i) {
  write_bytes((char*)&i, 4);
}

static void write_64bit_value(uint64_t i) {
  write_bytes((char*)&i, 8);
}

static uint32_t length_of_string(const char *s) {
  return (strlen(s) / 4) + 1;
}

static void write_string(const char *s) {
  uint32_t len = length_of_string(s);
  write_32bit_value(len);
  write_bytes(s, strlen(s));
  write_bytes("\0\0\0\0", 4 - (strlen(s) % 4));
}

static uint32_t read_32bit_value() {
  uint32_t val;
  
  val = *(uint32_t*)&write_buffer[cur_pos];
  cur_pos += 4;
  return val;
}

static uint64_t read_64bit_value() {
  uint64_t val;

  val = *(uint64_t*)&write_buffer[cur_pos];
  cur_pos += 8;
  return val;
}

/*
 * --- LLVM line counter API ---
 */

/* A file in this case is a translation unit. Each .o file built with line
 * profiling enabled will emit to a different file. Only one file may be
 * started at a time.
 */
void llvm_gcda_start_file(const char *orig_filename, const char version[4]) {
  int i = 0;
  memset(filename, 0, 2048);
  while (orig_filename[i]) {
    filename[i] = orig_filename[i];
    i++;
  }

  write_buffer = malloc(WRITE_BUFFER_SIZE);
  memset(write_buffer, 0, WRITE_BUFFER_SIZE);
  cur_buffer_size = WRITE_BUFFER_SIZE;
  cur_pos = 0;

  write_bytes("adcg", 4);
  write_bytes(version, 4);
  write_bytes("MVLL", 4);
}

/* Given an array of pointers to counters (counters), increment the n-th one,
 * where we're also given a pointer to n (predecessor).
 */
void llvm_gcda_increment_indirect_counter(uint32_t *predecessor,
                                          uint64_t **counters) {
  uint64_t *counter;
  uint32_t pred;

  pred = *predecessor;
  if (pred == 0xffffffff)
    return;
  counter = counters[pred];

  /* Don't crash if the pred# is out of sync. This can happen due to threads,
     or because of a TODO in GCOVProfiling.cpp buildEdgeLookupTable(). */
  if (counter)
    ++*counter;
}

void llvm_gcda_emit_function(uint32_t ident, const char *function_name,
                             uint8_t use_extra_checksum) {
  uint32_t len = 2;

  if (use_extra_checksum)
    len++;

  /* function tag */
  write_bytes("\0\0\0\1", 4);
  if (function_name)
    len += 1 + length_of_string(function_name);
  write_32bit_value(len);
  write_32bit_value(ident);
  write_32bit_value(0);
  if (use_extra_checksum)
    write_32bit_value(0);
  if (function_name)
    write_string(function_name);
}

void llvm_gcda_emit_arcs(uint32_t num_counters, uint64_t *counters) {
  uint32_t i;
  uint64_t *old_ctrs = NULL;
  uint32_t val = 0;
  uint64_t save_cur_pos = cur_pos;

  val = read_32bit_value();

  if (val != (uint32_t)0) {
    /* There are counters present in the file. Merge them. */
    if (val != 0x01a10000) {
      // printf("profiling:invalid arc tag (0x%08x)\n", val);
      return;
    }

    val = read_32bit_value();
    if (val == (uint32_t)-1 || val / 2 != num_counters) {
      // printf("profiling:invalid number of counters (%d)\n", val);
      return;
    }

    old_ctrs = (uint64_t *)malloc(sizeof(uint64_t) * num_counters);
    for (i = 0; i < num_counters; ++i)
      old_ctrs[i] = read_64bit_value();
  }

  cur_pos = save_cur_pos;

  /* Counter #1 (arcs) tag */
  write_bytes("\0\0\xa1\1", 4);
  // printf("num_counters:%d\n", num_counters);
  write_32bit_value(num_counters * 2);
  for (i = 0; i < num_counters; ++i) {
    counters[i] += (old_ctrs ? old_ctrs[i] : 0);
    // printf("counters[%d]:%d\n", i, counters[i]);
    write_64bit_value(counters[i]);
  }

  free(old_ctrs);
}

void llvm_gcda_summary_info() {
  const int obj_summary_len = 9; // length for gcov compatibility
  uint32_t i;
  uint32_t runs = 1;
  uint32_t val = 0;
  uint64_t save_cur_pos = cur_pos;

  val = read_32bit_value();

  if (val != (uint32_t)0) {
    /* There are counters present in the file. Merge them. */
    if (val != 0xa1000000) {
      // fprintf(stderr, "profiling:invalid object tag (0x%08x)\n", val);
      return;
    }

    val = read_32bit_value(); // length
    if (val != obj_summary_len) {
      // fprintf(stderr, "profiling:invalid object length (%d)\n", val); // length
      return;
    }

    read_32bit_value(); // checksum, unused
    read_32bit_value(); // num, unused
    runs += read_32bit_value(); // add previous run count to new counter
  }

  cur_pos = save_cur_pos;

  /* Object summary tag */
  write_bytes("\0\0\0\xa1", 4);
  write_32bit_value(obj_summary_len);
  write_32bit_value(0); // checksum, unused
  write_32bit_value(0); // num, unused
  write_32bit_value(runs);
  for (i = 3; i < obj_summary_len; ++i) 
    write_32bit_value(0);

  /* Program summary tag */
  write_bytes("\0\0\0\xa3", 4); // tag indicates 1 program
  write_32bit_value(0); // 0 length
}

void llvm_gcda_end_file() {
  if (write_buffer)
    ;

  free(write_buffer);
  write_buffer = NULL;
}

void llvm_register_writeout_function(writeout_fn fn) {
  struct writeout_fn_node *new_node = (struct writeout_fn_node *)malloc(sizeof(struct writeout_fn_node));
  new_node->fn = fn;
  new_node->next = NULL;

  if (!writeout_fn_head) {
    writeout_fn_head = writeout_fn_tail = new_node;
  } else {
    writeout_fn_tail->next = new_node;
    writeout_fn_tail = new_node;
  }
}

__attribute__((destructor))
void llvm_writeout_files() {
  struct writeout_fn_node *curr = writeout_fn_head;

  while (curr) {
    curr->fn();
    curr = curr->next;
  }
}
__attribute__((destructor))
void llvm_delete_writeout_function_list() {
  while (writeout_fn_head) {
    struct writeout_fn_node *node = writeout_fn_head;
    writeout_fn_head = writeout_fn_head->next;
    free(node);
  }
  
  writeout_fn_head = writeout_fn_tail = NULL;
}

void llvm_register_flush_function(flush_fn fn) {
  struct flush_fn_node *new_node = (struct flush_fn_node *)malloc(sizeof(struct flush_fn_node));
  new_node->fn = fn;
  new_node->next = NULL;

  if (!flush_fn_head) {
    flush_fn_head = flush_fn_tail = new_node;
  } else {
    flush_fn_tail->next = new_node;
    flush_fn_tail = new_node;
  }
}

void __gcov_flush() {
  struct flush_fn_node *curr = flush_fn_head;

  while (curr) {
    curr->fn();
    curr = curr->next;
  }
}

__attribute__((destructor))
void llvm_delete_flush_function_list() {
  while (flush_fn_head) {
    struct flush_fn_node *node = flush_fn_head;
    flush_fn_head = flush_fn_head->next;
    free(node);
  }

  flush_fn_head = flush_fn_tail = NULL;
}

void llvm_gcov_init(writeout_fn wfn, flush_fn ffn) {
  static int atexit_ran = 0;

  if (wfn)
    llvm_register_writeout_function(wfn);

  if (ffn)
    llvm_register_flush_function(ffn);
}
