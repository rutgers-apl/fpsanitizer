#ifndef __FPSAN_RUNTIME_H__
#define __FPSAN_RUNTIME_H__
#include <iostream>
#include <map>
#include <queue>
#include <math.h>
#include <gmp.h>
#include <mpfr.h>
#include <vector>
#include <stack>
#include <list>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <fstream>
#include <queue>
#include <iostream>
#include <stdlib.h>
#include <execinfo.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <asm/unistd.h>

#define MMAP_FLAGS (MAP_PRIVATE| MAP_ANONYMOUS| MAP_NORESERVE)
#define MAX_STACK_SIZE 500
#define MAX_SIZE 1000

/* Assumption: float types are 4-byte aligned. */
const size_t SS_PRIMARY_TABLE_ENTRIES = ((size_t) 4194304);//2^22
const size_t SS_SEC_TABLE_ENTRIES = ((size_t) 16*(size_t) 1024 * (size_t) 1024); // 2^24
const size_t PRIMARY_INDEX_BITS = 22;
const size_t SECONDARY_INDEX_BITS = 24;
const size_t SECONDARY_MASK = 0xffffff;

#define debug 0
#define debugtrace 0
#define debugerror 0

#define ERRORTHRESHOLD 35
#define ERRORTHRESHOLD1 35
#define ERRORTHRESHOLD2 45
#define ERRORTHRESHOLD3 55
#define ERRORTHRESHOLD4 63


FILE * m_errfile;
FILE * m_brfile;


enum fp_op{FADD, FSUB, FMUL, FDIV, CONSTANT, SQRT, CEIL, FLOOR, TAN, SIN,
	   COS, ATAN, ABS, LOG, ASIN, EXP, POW, MIN, MAX, LDEXP, FMOD, ATAN2,
	   HYPOT, COSH, SINH, TANH, ACOS, UNKNOWN};

struct error_info {
  double error;
  unsigned int cbad;
  unsigned int regime;
  unsigned int linenumber;
  unsigned int colnumber;
  bool debugInfoAvail;
};

/* smem_entry: metadata maintained with each shadow memory location.
 * val   : mpfr value for the shadow execution
 * error : number of bits in error. Why is it here?
 * computed: double value 
 * lineno: line number in the source file
 * is_init: is the MPFR initialized
 * opcode: opcode of the operation that created the result
 * lock: CETS style metadata for validity of the temporary metadata pointer
 * key:  CETS style metadata for validity of the temporary metadata pointer
 * tmp_ptr: Pointer to the metadata of the temporary 
 */
struct smem_entry{

  mpfr_t val;
  int error;
  double computed;
  unsigned int lineno;
  enum fp_op opcode;
  bool is_init;
  unsigned int lock;
  unsigned int key;
  struct temp_entry *tmp_ptr;

};

struct temp_entry{

  mpfr_t val;
  double computed;
  int error;
  unsigned int lineno;

  size_t lock;
  size_t key;

  size_t op1_lock;
  size_t op1_key;
  struct temp_entry* lhs;

  size_t op2_lock;
  size_t op2_key;
  struct temp_entry* rhs;

  enum fp_op opcode;
  size_t timestamp;
  bool is_init;
};

# if !defined(PREC_128) && !defined(PREC_256) && !defined(PREC_512) && !defined(PREC_1024) 
#  define PREC_512
# endif

# if !defined(PRECISION) 
# ifdef PREC_128
#   define PRECISION 128
# endif
# ifdef PREC_256
#   define PRECISION 256
# endif
# ifdef PREC_512
#   define PRECISION 512
# endif
# ifdef PREC_1024
#   define PRECISION 1024
# endif
#endif

size_t * m_lock_key_map;
smem_entry * m_shadow_stack;
smem_entry ** m_shadow_memory;

size_t m_precision = PRECISION;

size_t m_stack_top = 0;
size_t m_timestamp = 0;
size_t m_key_stack_top = 0;
size_t m_key_counter = 0;
bool m_init_flag = false;

size_t infCount = 0;
size_t nanCount = 0;
size_t errorCount = 0;
size_t flipsCount = 0;

std::map<unsigned long long int, struct error_info> m_inst_error_map;
size_t *frameCur;
std::list <temp_entry*> m_expr;

std::string m_get_string_opcode(size_t);
unsigned long m_ulpd(double x, double y);
unsigned long m_ulpf(float x, float y);
int m_update_error(temp_entry *mpfr_val, double computedVal); 
void m_print_error(size_t opcode, 
		   temp_entry * real, 
		   double d_value, 
		   unsigned int cbad,
		   unsigned long long int instId, 
		   bool debugInfoAvail, 
		   unsigned int linenumber, 
		   unsigned int colnumber);
void m_print_real(mpfr_t);
void m_print_trace (void);
int m_isnan(mpfr_t real);
int m_get_depth(temp_entry*);
void m_compute(fp_op , double, temp_entry *, double,
	       temp_entry *, double, temp_entry *, size_t);

void m_store_shadow_dconst(smem_entry *, double , unsigned int);
void m_store_shadow_fconst(smem_entry *, float , unsigned int);

#endif
