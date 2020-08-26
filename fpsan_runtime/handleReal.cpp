#include "handleReal.h"

/*
We don't want to call mpfr_init on every add or sub.That's why we keep 
it as global variables and do init once and just update on every add or sub 
*/
#define ERRORTHRESHOLD1 35
#define ERRORTHRESHOLD2 45
#define ERRORTHRESHOLD3 55
#define ERRORTHRESHOLD4 63
mpfr_t op1_mpfr, op2_mpfr, res_mpfr;
mpfr_t computed, temp_diff;
// fpsan_trace: a function that user can set a breakpoint on to
// generate DAGs

#ifdef TRACING
extern "C" void fpsan_fpcore(temp_entry *cur){
  if(cur){
    if(m_lock_key_map[cur->op1_lock] != cur->op1_key ){
      if(m_var_map.count(cur) > 0){
        varString += "( x_"+ m_var_map.at(cur) + ")";
      }
      else{
        varCount++;
        auto var = std::to_string(varCount);
        varString += "( x_"+ var + ")";
        m_var_map.insert( std::pair<temp_entry*, std::string>(cur, var) );
      }
      return;
    }
    if(m_lock_key_map[cur->op2_lock] != cur->op2_key){
      if(m_var_map.count(cur) > 0){
        varString += "( x_"+ m_var_map.at(cur) + ")";
      }
      else{
        varCount++;
        auto var = std::to_string(varCount);
        varString += "( x_"+ var + ")";
        m_var_map.insert( std::pair<temp_entry*, std::string>(cur, var) );
      }
      return;
    }
    if(cur->opcode == CONSTANT){
      if(m_var_map.count(cur) > 0){
        varString += "( x_"+ m_var_map.at(cur);
      }
      else{
        varCount++;
        auto var = std::to_string(varCount);
        varString += "( x_"+ var ;
        m_var_map.insert( std::pair<temp_entry*, std::string>(cur, var) );
      }
    }
    else{
      varString += "(" + m_get_string_opcode_fpcore(cur->opcode);
    }
    if(cur->lhs != NULL){
      if(cur->lhs->timestamp < cur->timestamp){
        fpsan_fpcore(cur->lhs);
      }
    }
    if(cur->rhs != NULL){
      if(cur->rhs->timestamp < cur->timestamp){
        fpsan_fpcore(cur->rhs);
      }
    }
    varString += ")";
  }
}

extern "C" void fpsan_get_fpcore(temp_entry *cur){
  fflush(stdout);
  fpsan_fpcore(cur);
  std::string out_fpcore;
  out_fpcore = "(FPCore ( ";

  while(varCount > 0){
    out_fpcore += "x_"+std::to_string(varCount) + " ";
    varCount--;
  }
  out_fpcore += ")\n";
  out_fpcore += varString; 
  out_fpcore += ")\n";
  fprintf(m_fpcore, "%s",out_fpcore.c_str());
  varString = "";
  varCount = 0;
}
extern "C" void fpsan_trace(temp_entry *current){
  m_expr.push_back(current);
  int level;
  while(!m_expr.empty()){
    level = m_expr.size();
    temp_entry *cur = m_expr.front();
    if(cur == NULL){
      return;
    }
    if(m_lock_key_map[cur->op1_lock] != cur->op1_key ){
      return;
    }
    if(m_lock_key_map[cur->op2_lock] != cur->op2_key){
      return;
    }
    std::cout<<"\n";
    std::cout<<" "<<cur->lineno<<" "<<m_get_string_opcode(cur->opcode)<<" ";
    fflush(stdout);
    if(cur->lhs != NULL){
      std::cout<<" "<<cur->lhs->lineno<<" ";
      if(cur->lhs->timestamp < cur->timestamp){
        m_expr.push_back(cur->lhs);
        fflush(stdout);
      }
    }
    if(cur->rhs != NULL){
      std::cout<<" "<<cur->rhs->lineno<<" ";
      if(cur->rhs->timestamp < cur->timestamp){
        m_expr.push_back(cur->rhs);
        fflush(stdout);
      }
    }
    std::cout<<"(real:";
    m_print_real(cur->val);
    printf(" computed: %e", cur->computed);
    std::cout<<", error:"<<cur->error<<" "<<")";
    fflush(stdout);
    m_expr.pop_front();
    level--;
  }
  int depth = m_get_depth(current);
  std::cout<<"depth:"<<depth<<"\n";
}
#endif


// fpsan_check_branch, fpsan_check_conversion, fpsan_check_error are
// functions that user can set breakpoint on

extern "C" bool fpsan_check_branch_f(float op1d, temp_entry* op1,
				     float op2d, temp_entry* op2,
				     size_t fcmpFlag, bool computedRes,
				     size_t lineNo){

  bool realRes = m_check_branch(&(op1->val), &(op2->val), fcmpFlag);
  if(realRes != computedRes){
    flipsCount++;
  }
  return realRes;
}

extern "C" bool fpsan_check_branch_d(double op1d, temp_entry* op1,
				 double op2d, temp_entry* op2,
				 size_t fcmpFlag, bool computedRes,
				 size_t lineNo){

  bool realRes = m_check_branch(&(op1->val), &(op2->val), fcmpFlag);
  if(realRes != computedRes){
    flipsCount++;
  }
  return realRes;
}
extern "C" unsigned int  fpsan_check_conversion(long real, long computed,
						temp_entry *realRes){
  if(real != computed){
    return 1;
  }
  return 0;
}

unsigned int check_error(temp_entry *realRes, double computedRes){
#ifdef TRACING
  if(realRes->error >= ERRORTHRESHOLD4){
    errorCount63++;
    return 1;
  }
  else if(realRes->error >= ERRORTHRESHOLD3){
    errorCount55++;
    return 2;
  }
  else if(realRes->error >= ERRORTHRESHOLD2){
    errorCount45++;
    return 3;
  }
  else if(realRes->error >= ERRORTHRESHOLD1){
    errorCount35++;
    return 4;
  }
#else
  int bits_error = m_update_error(realRes, computedRes, 0);
  if(bits_error > ERRORTHRESHOLD4){
    errorCount63++;
    return 1;
  }
  else if(bits_error > ERRORTHRESHOLD3){
    errorCount55++;
    return 2;
  }
  else if(bits_error > ERRORTHRESHOLD2){
    errorCount45++;
    return 3;
  }
  else if(bits_error > ERRORTHRESHOLD1){
    errorCount35++;
    return 4;
  }

#endif
  return 0;
}

extern "C" unsigned int fpsan_check_error_f(temp_entry *realRes, float computedRes){
  return check_error(realRes, computedRes);
}

extern "C" unsigned int fpsan_check_error(temp_entry *realRes, double computedRes){
  return check_error(realRes, computedRes);
}

extern "C" void fpsan_init() {
  if (!m_init_flag) {
    
    m_fpcore = fopen ("fpsan.fpcore","w");
    m_errfile = fopen ("error.log","w");
    m_brfile = fopen ("branch.log","w");
    
    //printf("sizeof Real %lu", sizeof(struct smem_entry));
    m_init_flag = true;
    size_t length = MAX_STACK_SIZE * sizeof(temp_entry);

    size_t memLen = SS_PRIMARY_TABLE_ENTRIES * sizeof(smem_entry *);
    m_shadow_stack =
      (temp_entry *)mmap(0, length, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
#ifdef TRACING
    m_lock_key_map =
      (size_t *)mmap(0, length, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
    assert(m_lock_key_map != (void *)-1);
#endif    

    m_shadow_memory =
      (smem_entry **)mmap(0, memLen, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);

    assert(m_shadow_stack != (void *)-1);
    assert(m_shadow_memory != (void *)-1);

#ifdef TRACING    
    m_key_stack_top = 1;
    m_key_counter = 1;

    m_lock_key_map[m_key_stack_top] = m_key_counter;
#endif    

    for(int i =0; i<MAX_STACK_SIZE; i++){
      mpfr_init2(m_shadow_stack[i].val, m_precision);
    }

    m_stack_top = 0;
    m_prec_bits_f = 23;
    m_prec_bits_d = 52;
    mpfr_init2(op1_mpfr, m_precision);
    mpfr_init2(op2_mpfr, m_precision);
    mpfr_init2(res_mpfr, m_precision);
    mpfr_init2(temp_diff, m_precision);
    mpfr_init2(computed, m_precision);
  }
}

void m_set_mpfr(mpfr_t *val1, mpfr_t *val2) {
  mpfr_set(*val1, *val2, MPFR_RNDN);
}

//primarily used in the LLVM IR for initializing stack metadata
extern "C" void fpsan_init_mpfr(temp_entry *op) {

  mpfr_init2(op->val, m_precision);

#ifdef TRACING  
  op->lock = m_key_stack_top;
  op->key = m_lock_key_map[m_key_stack_top];
#endif  

}

extern "C" void fpsan_init_store_shadow_dconst(smem_entry *op, double d,
						  unsigned int linenumber) {
  
  mpfr_init2(op->val, m_precision);
  m_store_shadow_dconst(op, d, linenumber);

}

extern "C" void fpsan_init_store_shadow_fconst(smem_entry *op, float f, unsigned int linenumber) {

  mpfr_init2(op->val, m_precision);
  m_store_shadow_fconst(op, f, linenumber);

}

int m_isnan(mpfr_t real){
    return mpfr_nan_p(real);
}


void m_store_shadow_dconst(smem_entry *op, double d, unsigned int linenumber) {

  mpfr_set_d(op->val, d, MPFR_RNDN);

#ifdef TRACING  
  op->lock = m_key_stack_top;
  op->key = m_lock_key_map[m_key_stack_top];
  op->error = 0;
  op->tmp_ptr = NULL;
#endif
  
  op->lineno = linenumber;
  op->opcode = CONSTANT;
  op->computed = d;

}

void m_store_shadow_fconst(smem_entry *op, float f, unsigned int linenumber) {

  mpfr_set_flt(op->val, f, MPFR_RNDN);
#ifdef TRACING  
  op->lock = m_key_stack_top;
  op->key = m_lock_key_map[m_key_stack_top];
  op->error = 0;
  op->tmp_ptr = NULL;
#endif
  
  op->lineno = linenumber;
  op->opcode = CONSTANT;
  op->computed = f;

}

extern "C" void fpsan_store_tempmeta_dconst(temp_entry *op, double d, unsigned int linenumber) {

  mpfr_set_d(op->val, d, MPFR_RNDN);

#ifdef TRACING  
  op->lock = m_key_stack_top;
  op->key = m_lock_key_map[m_key_stack_top];
  op->op1_lock = 0;
  op->op1_key = 0;
  op->op2_lock = 0;
  op->op2_key = 0;
  op->lhs = NULL;
  op->rhs = NULL;
  op->error = 0;
  op->timestamp = m_timestamp++;
#endif
  
  op->lineno = linenumber;
  op->opcode = CONSTANT;
  op->computed = d;

}

extern "C" void fpsan_store_tempmeta_fconst(temp_entry *op, float f, unsigned int linenumber) {

  mpfr_set_flt(op->val, f, MPFR_RNDN);

#ifdef TRACING  
  op->lock = m_key_stack_top;
  op->key = m_lock_key_map[m_key_stack_top];
  op->op1_lock = 0;
  op->op1_key = 0;
  op->op2_lock = 0;
  op->op2_key = 0;
  op->lhs = NULL;
  op->rhs = NULL;
  op->error = 0;
  op->timestamp = m_timestamp++;
#endif
  
  op->lineno = linenumber;
  op->opcode = CONSTANT;
  op->computed = f;

}

extern "C" void fpsan_clear_mpfr(mpfr_t *val) {
  mpfr_clear(*val);
}

float m_get_float(mpfr_t mpfr_val) { return mpfr_get_flt(mpfr_val, MPFR_RNDN); }

double m_get_double(mpfr_t mpfr_val) { return mpfr_get_d(mpfr_val, MPFR_RNDN); }

long double m_get_longdouble(temp_entry *real) {
  return mpfr_get_ld(real->val, MPFR_RNDN);
}

smem_entry* m_get_shadowaddress(size_t address){
  size_t addrInt = address >> 2;
  size_t primary_index = (addrInt >> SECONDARY_INDEX_BITS );
  smem_entry* primary_ptr = m_shadow_memory[primary_index];
  if (primary_ptr == NULL) {
    size_t sec_length = (SS_SEC_TABLE_ENTRIES) * sizeof(smem_entry);
    primary_ptr = (smem_entry*)mmap(0, sec_length, PROT_READ| PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
    m_shadow_memory[primary_index] = primary_ptr;
  }
  size_t offset = (addrInt) & SECONDARY_MASK;
  smem_entry* realAddr = primary_ptr + offset;
  if(!realAddr->is_init){
    realAddr->is_init = true;
    mpfr_init2(realAddr->val, m_precision);
  }
  return realAddr;
}

void m_print_real(mpfr_t mpfr_val){

  mpfr_out_str (stdout, 10, 15, mpfr_val, MPFR_RNDN);
}


/* storing metadata for constants */

extern "C" void fpsan_store_shadow_fconst(void* toAddr, float op, unsigned int linenumber){

  
  size_t toAddrInt = (size_t) toAddr;
  smem_entry* dest = m_get_shadowaddress(toAddrInt);
  fpsan_init_store_shadow_fconst(dest, op, linenumber);
  dest->is_init = true;
}

/* storing metadata for constants */

extern "C" void fpsan_store_shadow_dconst(void* toAddr, double op,
					  unsigned int linenumber){

  size_t toAddrInt = (size_t) toAddr;
  smem_entry* dest = m_get_shadowaddress(toAddrInt);
  fpsan_init_store_shadow_dconst(dest, op, linenumber);
  dest->is_init = true;

}

extern "C" void fpsan_copy_phi(temp_entry* src, temp_entry* dst){

  if(src != NULL){
    m_set_mpfr(&(dst->val), &(src->val));
    dst->computed = src->computed;
    dst->opcode = src->opcode;
    dst->lineno = src->lineno;
    dst->is_init = true;

#ifdef TRACING    
    dst->lock = m_key_stack_top;
    dst->key = m_lock_key_map[m_key_stack_top];
    dst->error = src->error;

    dst->op1_lock = src->op1_lock;
    dst->op1_key = src->op1_key;
    dst->lhs = src->lhs;
    dst->op2_lock = src->op2_lock;
    dst->op2_key = src->op2_key;
    dst->rhs = src->rhs;
    dst->timestamp = src->timestamp;
#endif
  }
}

extern "C" void fpsan_store_shadow(void* toAddr, temp_entry* src){

  if(src == NULL){
    std::cout<<"Error !!! __set_real trying to read invalid memory\n";
    return;
  }
  
  size_t toAddrInt = (size_t) toAddr;
  smem_entry* dest = m_get_shadowaddress(toAddrInt);
  
  /*copy val*/
  m_set_mpfr(&(dest->val), &(src->val));
  /*copy everything else except res key and opcode*/
#ifdef TRACING  
  dest->error = src->error;
  dest->lock = m_key_stack_top; 
  dest->key = m_lock_key_map[m_key_stack_top]; 
  dest->tmp_ptr = src;
#endif  

  dest->is_init = true;
  dest->lineno = src->lineno;
  dest->computed = src->computed;
  dest->opcode = src->opcode;

}

extern "C" void fpsan_load_shadow_fconst(temp_entry *src, void *Addr, float d){

  if(src == NULL) {
    if(debug){
      printf("__load_d:Error !!! __load_d trying to load from invalid stack\n");
    }
    return;
  }

  size_t AddrInt = (size_t) Addr;
  smem_entry* dest = m_get_shadowaddress(AddrInt);

  /* copy the metadata from shadow memory to temp metadata*/
  m_set_mpfr(&(src->val), &(dest->val));
#ifdef TRACING  
  src->lock = m_key_stack_top; 
  src->key = m_lock_key_map[m_key_stack_top];
  src->error = dest->error;
#endif
  
  src->lineno = dest->lineno;
  src->computed = dest->computed;
  src->opcode = dest->opcode;


#ifdef TRACING  
  /* if the temp metadata is not available (i.e, the function
     producing the value has returned), then treat the value loaded
     from the shadow memory as a constant from the perspective of
     tracing */
  
  if( dest->tmp_ptr != NULL &&
      m_lock_key_map[dest->lock] == dest->key){
    
    src->op1_lock = dest->tmp_ptr->op1_lock;
    src->op1_key = dest->tmp_ptr->op1_key;
    src->lhs = dest->tmp_ptr->lhs;
    
    src->op2_lock = dest->tmp_ptr->op2_lock;
    src->op2_key = dest->tmp_ptr->op2_key;
    src->rhs = dest->tmp_ptr->rhs;
    
    src->timestamp = dest->tmp_ptr->timestamp;
  }
  else{
    /* No trace information available, treat like a constant */
    if(debug)
      std::cout<<"__load_d copying default\n";

#if 0    
    /* This looks redundant with fpsan_store_tempmeta_fconst operations */
    src->op1_lock = 0;
    src->op1_key = 0;
    src->op2_lock = 0;
    src->op2_key = 0;
    src->lhs = NULL;
    src->rhs = NULL;
    src->timestamp = m_timestamp++;
#endif    
    fpsan_store_tempmeta_fconst(src, d, 0); //for global variables
  }
#endif
  
}

extern "C" void fpsan_load_shadow_dconst(temp_entry *src, void *Addr, double d){

  if(src == NULL){
    if(debug){
      printf("__load_d:Error !!! __load_d trying to load from invalid stack\n");
    }
    return;
  }
  
  size_t AddrInt = (size_t) Addr;
  smem_entry* dest = m_get_shadowaddress(AddrInt);

  m_set_mpfr(&(src->val), &(dest->val));
  src->lineno = dest->lineno;
  src->computed = dest->computed;
  src->opcode = dest->opcode;


#ifdef TRACING  
  src->lock = m_key_stack_top; 
  src->key = m_lock_key_map[m_key_stack_top];
  src->error = dest->error;

  /* if the temp metadata is not available (i.e, the function
     producing the value has returned), then treat the value loaded
     from the shadow memory as a constant from the perspective of
     tracing */
  
  if( dest->tmp_ptr != NULL &&
      m_lock_key_map[dest->lock] == dest->key){
    src->op1_lock = dest->tmp_ptr->op1_lock;
    src->op1_key = dest->tmp_ptr->op1_key;
    src->op2_lock = dest->tmp_ptr->op2_lock;
    src->op2_key = dest->tmp_ptr->op2_key;
    src->lhs = dest->tmp_ptr->lhs;
    src->rhs = dest->tmp_ptr->rhs;
    src->timestamp = dest->tmp_ptr->timestamp;
  }
  else{
    if(debug)
      std::cout<<"__load_d copying default\n";
#if 0
    /* This looks redundant with fpsan_store_tempmeta_fconst operations */
    src->op1_lock = 0;
    src->op1_key = 0;
    src->op2_lock = 0;
    src->op2_key = 0;
    src->lhs = NULL;
    src->rhs = NULL;
    src->timestamp = m_timestamp++;
#endif    
    fpsan_store_tempmeta_dconst(src, d, 0); //for global variables
  }
#endif
  
}

void handle_math_d(fp_op opCode, double op1d, temp_entry *op, 
		   double computedResd,  temp_entry *res,
		   unsigned int linenumber) {

  switch(opCode){
    case SQRT:
      mpfr_sqrt(res->val, op->val, MPFR_RNDN);
      break;
    case FLOOR:
      mpfr_floor(res->val, op->val);
      break;
    case CEIL:
      mpfr_ceil(res->val, op->val);
      break;
    case TAN:
      mpfr_tan(res->val, op->val, MPFR_RNDN);
      break;
    case TANH:
      mpfr_tanh(res->val, op->val, MPFR_RNDN);
      break;
    case SIN:
      mpfr_sin(res->val, op->val, MPFR_RNDN);
      break;
    case SINH:
      mpfr_sinh(res->val, op->val, MPFR_RNDN);
      break;
    case COS:
      mpfr_cos(res->val, op->val, MPFR_RNDN);
      break;
    case COSH:
      mpfr_cosh(res->val, op->val, MPFR_RNDN);
      break;
    case ACOS:
      mpfr_acos(res->val, op->val, MPFR_RNDN);
      break;
    case ATAN:
      mpfr_atan(res->val, op->val, MPFR_RNDN);
      break;
    case ABS:
      mpfr_abs(res->val, op->val, MPFR_RNDN);
      break;
    case LOG:
      mpfr_log(res->val, op->val, MPFR_RNDN);
      break;
    case LOG10:
      mpfr_log10(res->val, op->val, MPFR_RNDN);
      break;
    case ASIN: 
      mpfr_asin(res->val, op->val, MPFR_RNDN);
      break;
    case EXP: 
      mpfr_exp(res->val, op->val, MPFR_RNDN);
      break;
    default:
      std::cout<<"Error!!! Math function not supported\n\n";
      exit(1);
      break;
  }
  if(isinf(computedResd))
    infCount++;
  if (computedResd != computedResd)
    nanCount++;

#ifdef TRACING  
  int bitsError = m_update_error(res, computedResd);
  res->op1_lock = op->lock;
  res->op1_key = m_lock_key_map[op->lock];
  res->op2_lock = 0;
  res->op2_key = 0;
  res->lock = m_key_stack_top;
  res->key = m_lock_key_map[m_key_stack_top];
  res->lhs = op;
  res->rhs = nullptr;
  res->timestamp = m_timestamp++;
  res->error = bitsError;
#endif
  
  res->lineno = linenumber;
  res->opcode = opCode;
  res->computed = computedResd;
}

void m_compute(fp_op opCode, double op1d,
	       temp_entry *op1, double op2d,
	       temp_entry *op2, double computedResd,
	       temp_entry *res, size_t lineNo) {
  
  switch(opCode) {                                                                                            
    case FADD: 
      mpfr_add (res->val, op1->val, op2->val, MPFR_RNDN);      
      break;
      
    case FSUB: 
      mpfr_sub (res->val, op1->val, op2->val, MPFR_RNDN);
      break;

    case FMUL: 
      mpfr_mul (res->val, op1->val, op2->val, MPFR_RNDN);
      break;
      
    case FDIV: 
      mpfr_div (res->val, op1->val, op2->val, MPFR_RNDN);
      break;
      
    default:
      // do nothing
      break;
  } 

  if (computedResd != computedResd)
    nanCount++;
  if(debug){
    printf("compute: res:%p\n", res);
    m_print_real(res->val);
    printf("compute: res:%p\n", res);
  }

#ifdef TRACING  
  int bitsError = m_update_error(res, computedResd);
  res->op1_lock = op1->lock;
  res->op2_lock = op2->lock;
  res->op1_key = m_lock_key_map[op1->lock];
  res->op2_key = m_lock_key_map[op2->lock];
  res->lock = m_key_stack_top;
  res->key = m_lock_key_map[m_key_stack_top];
  res->lhs = op1;
  res->rhs = op2;
  res->timestamp = m_timestamp++;
  res->error = bitsError;
#endif

  res->lineno = lineNo;
  res->opcode = opCode;
  res->computed = computedResd;
}

unsigned int m_get_exact_bits(
    double opD, int precBits,
    temp_entry *shadow){

  mpfr_set_d(computed, opD, MPFR_RNDN);

  mpfr_sub(temp_diff, shadow->val, computed, MPFR_RNDN);

  mpfr_exp_t exp_real = mpfr_get_exp(shadow->val);
  mpfr_exp_t exp_computed = mpfr_get_exp(computed);
  mpfr_exp_t exp_diff = mpfr_get_exp(temp_diff);

  if(mpfr_cmp(computed, shadow->val) == 0){
    return precBits;
  }
  else if(exp_real != exp_computed){
    return 0;
  }
  else{
    if(mpfr_cmp_ui(temp_diff, 0) != 0) {
      if(precBits < abs(exp_real -  exp_diff)){
        return precBits;
      }
      else{
        return abs(exp_real -  exp_diff);
      }
    }
    else{
      return 0;
    }
  }
}

mpfr_exp_t m_get_cancelled_bits(double op1, double op2, double res){
  mpfr_set_d(op1_mpfr, op1, MPFR_RNDN);

  mpfr_set_d(op2_mpfr, op2, MPFR_RNDN);

  mpfr_set_d(res_mpfr, res, MPFR_RNDN);

  mpfr_exp_t exp_op1 = mpfr_get_exp(op1_mpfr);
  mpfr_exp_t exp_op2 = mpfr_get_exp(op2_mpfr);
  mpfr_exp_t exp_res = mpfr_get_exp(res_mpfr);

  mpfr_exp_t max_exp;
  if( mpfr_regular_p(op1_mpfr) == 0 ||
      mpfr_regular_p(op2_mpfr) == 0 ||
      mpfr_regular_p(res_mpfr) == 0)
    return 0;

  if(exp_op1 > exp_op2)
    max_exp = exp_op1;
  else
    max_exp = exp_op2;

  if(max_exp > exp_res)
    return abs(max_exp - exp_res);
  else
    return 0;
}

unsigned int m_get_cbad(mpfr_exp_t cbits,
    unsigned int ebitsOp1,
    unsigned int ebitsOp2){
  unsigned int min_ebits;
  if (ebitsOp1 > ebitsOp2)
    min_ebits = ebitsOp2;
  else
    min_ebits = ebitsOp1;
  int badness = 1 + cbits - min_ebits;
  if(badness > 0)
    return badness;
  else
    return 0;
}

#ifdef TRACING
unsigned int m_check_cc(double op1, 
                        double op2, 
                        double res,
                        int precBits,
                        temp_entry *shadowOp1,
                        temp_entry *shadowOp2,
                        temp_entry *shadowVal){

  /* If op1 or op2 is NaR, then it is not catastrophic cancellation*/
  if (isnan(op1) || isnan(op2)) return 0;
  /* If result is 0 and it has error, then it is catastrophic cancellation*/
  if ((res == 0) && shadowVal->error != 0) return 1;

  unsigned int ebitsOp1 = m_get_exact_bits(op1, precBits, shadowOp1);
  unsigned int ebitsOp2 = m_get_exact_bits(op2, precBits, shadowOp2);
  mpfr_exp_t cbits = m_get_cancelled_bits(op1, op2, res);
  unsigned int cbad = m_get_cbad(cbits, ebitsOp1, ebitsOp2);
  return cbad;
}
#endif

extern "C" void fpsan_mpfr_fadd_f( temp_entry* op1Idx,
				   temp_entry* op2Idx,
				   temp_entry* res,
				   float op1d,
				   float op2d,
				   float computedResD,
				   unsigned long long int instId,
				   bool debugInfoAvail,
				   unsigned int linenumber,
				   unsigned int colnumber) {
  
  m_compute(FADD, op1d, op1Idx, op2d, op2Idx,
      computedResD, res, linenumber);

#ifdef TRACING
  unsigned int cbad = 0;
  if(((op1d < 0) && (op2d > 0)) ||
      ((op1d > 0) && (op2d < 0))){
    cbad = m_check_cc(op1d, op2d, computedResD, m_prec_bits_f, op1Idx, op2Idx, res);
    if(cbad > 0)
      ccCount++;
  }
#endif
}

extern "C" void fpsan_mpfr_fsub_f( temp_entry* op1Idx,
				   temp_entry* op2Idx,
				   temp_entry* res,
				   float op1d,
				   float op2d,
				   float computedResD,
				   unsigned long long int instId,
				   bool debugInfoAvail,
				   unsigned int linenumber,
				   unsigned int colnumber) {
  
  m_compute(FSUB, op1d, op1Idx, op2d, op2Idx,
	    computedResD, res, linenumber);

#ifdef TRACING
  unsigned int cbad = 0;
  if(((op1d < 0) && (op2d < 0)) ||
      ((op1d > 0) && (op2d > 0))){
    cbad = m_check_cc(op1d, op2d, computedResD, m_prec_bits_f, op1Idx, op2Idx, res);
    if(cbad > 0)
      ccCount++;
  }
#endif
}

extern "C" void fpsan_mpfr_fmul_f( temp_entry* op1Idx,
				   temp_entry* op2Idx,
				   temp_entry* res,
				   float op1d,
				   float op2d,
				   float computedResD,
				   unsigned long long int instId,
				   bool debugInfoAvail,
				   unsigned int linenumber,
				   unsigned int colnumber) {
  
  m_compute(FMUL, op1d, op1Idx, op2d, op2Idx,
	    computedResD, res, linenumber);
}

extern "C" void fpsan_mpfr_fdiv_f( temp_entry* op1Idx,
				   temp_entry* op2Idx,
				   temp_entry* res,
				   float op1d,
				   float op2d,
				   float computedResD,
				   unsigned long long int instId,
				   bool debugInfoAvail,
				   unsigned int linenumber,
				   unsigned int colnumber) {
  
  m_compute(FDIV, op1d, op1Idx, op2d, op2Idx, 
	    computedResD, res, linenumber);
  if(isinf(computedResD))
    infCount++;
}

extern "C" void fpsan_mpfr_fadd( temp_entry* op1Idx,
				 temp_entry* op2Idx,
				 temp_entry* res,
				 double op1d,
				 double op2d,
				 double computedResD,
				 unsigned long long int instId,
				 bool debugInfoAvail,
				 unsigned int linenumber,
				 unsigned int colnumber) {
  
  m_compute(FADD, op1d, op1Idx, op2d, op2Idx,
	    computedResD, res, linenumber);

#ifdef TRACING
  unsigned int cbad = 0;
  if(((op1d < 0) && (op2d > 0)) ||
      ((op1d > 0) && (op2d < 0))){
    cbad = m_check_cc(op1d, op2d, computedResD, m_prec_bits_d, op1Idx, op2Idx, res);
    if(cbad > 0)
      ccCount++;
  }
#endif
}

extern "C" void fpsan_mpfr_fsub( temp_entry* op1Idx,
				 temp_entry* op2Idx,
				 temp_entry* res,
				 double op1d,
				 double op2d,
				 double computedResD,
				 unsigned long long int instId,
				 bool debugInfoAvail,
				 unsigned int linenumber,
				 unsigned int colnumber) {
  
  m_compute(FSUB, op1d, op1Idx, op2d, op2Idx,
	    computedResD, res, linenumber);

#ifdef TRACING
  unsigned int cbad = 0;
  if(((op1d < 0) && (op2d < 0)) ||
      ((op1d > 0) && (op2d > 0))){
    cbad = m_check_cc(op1d, op2d, computedResD, m_prec_bits_d, op1Idx, op2Idx, res);
    if(cbad > 0)
      ccCount++;
  }
#endif
}

extern "C" void fpsan_mpfr_fmul( temp_entry* op1Idx,
				 temp_entry* op2Idx,
				 temp_entry* res,
				 double op1d,
				 double op2d,
				 double computedResD,
				 unsigned long long int instId,
				 bool debugInfoAvail,
				 unsigned int linenumber,
				 unsigned int colnumber) {
  
  m_compute(FMUL, op1d, op1Idx, op2d, op2Idx,
	    computedResD, res, linenumber);
}

extern "C" void fpsan_mpfr_fdiv( temp_entry* op1Idx,
				 temp_entry* op2Idx,
				 temp_entry* res,
				 double op1d,
				 double op2d,
				 double computedResD,
				 unsigned long long int instId,
				 bool debugInfoAvail,
				 unsigned int linenumber,
				 unsigned int colnumber) {
  
  m_compute(FDIV, op1d, op1Idx, op2d, op2Idx, 
	    computedResD, res, linenumber);
  if(isinf(computedResD))
    infCount++;
}

bool m_check_branch(mpfr_t* op1, mpfr_t* op2,
		    size_t fcmpFlag){
  bool realRes = false;
  int ret = mpfr_cmp(*op1, *op2);

  switch(fcmpFlag){
    case 0:
      realRes = false;
      break;
    case 1: /*oeq*/
      if(!m_isnan(*op1) && !m_isnan(*op2)){
        if(ret == 0)
          realRes = true;
      }
      break;
    case 2: /*ogt*/
      if(!m_isnan(*op1) && !m_isnan(*op2)){
        if(ret > 0){
          realRes = true;
        }
      }
      break;
    case 3:
      if(!m_isnan(*op1) && !m_isnan(*op2)){
        if(ret > 0 || ret == 0){
          realRes = true;
        }
      }
      break;
    case 4: /*olt*/
      if(!m_isnan(*op1) && !m_isnan(*op2)){
        if(ret < 0){
          realRes = true;
        }
      }
      break;
    case 5:
      if(!m_isnan(*op1) && !m_isnan(*op2)){
        if(ret < 0 || ret == 0){
          realRes = true;
        }
      }
      break;
    case 6:
      if(!m_isnan(*op1) && !m_isnan(*op2)){
        if(ret != 0){
          realRes = true;
        }
      }
      break;
    case 7:
      if(!m_isnan(*op1) && !m_isnan(*op2)){
        realRes = true;
      }
      break;
    case 8:
      if(m_isnan(*op1) && m_isnan(*op2)){
        realRes = true;
      }
      break;
    case 9:
      if(m_isnan(*op1) || m_isnan(*op2) || ret == 0)
        realRes = true;
      break;
    case 10:
      if(m_isnan(*op1) || m_isnan(*op2) || ret > 0)
        realRes = true;
      break;
    case 11:
      if(m_isnan(*op1) || m_isnan(*op2) || ret >= 0)
        realRes = true;
      break;
    case 12: 
      if(m_isnan(*op1) || m_isnan(*op2) || ret < 0)
        realRes = true;
      break;
    case 13:
      if(m_isnan(*op1) || m_isnan(*op2) || ret <= 0)
        realRes = true;
      break;
    case 14:
      if(m_isnan(*op1) || m_isnan(*op2) || ret != 0){
        realRes = true;
      }
      break;
    case 15:
      realRes = true;
      break;
  }
  return realRes;
}



std::string m_get_string_opcode_fpcore(size_t opcode){
  switch(opcode){
    case FADD:
      return "+";
    case FMUL:
      return "*";
    case FSUB:
      return "-";
    case FDIV:
      return "/";
    case CONSTANT:
      return "CONSTANT";
    case SQRT:  
      return "sqrt";
    case FLOOR:  
      return "floor";
    case TAN:  
      return "tan";
    case SIN:  
      return "sin";
    case COS:  
      return "cos";
    case ATAN:  
      return "atan";
    case ABS:  
      return "abs";
    case LOG:  
      return "log";
    case ASIN:  
      return "asin";
    case EXP:  
      return "exp";
    case POW:
      return "pow";
    default:
      return "Unknown";
  }
}
std::string m_get_string_opcode(size_t opcode){
  switch(opcode){
    case FADD:
      return "FADD";
    case FMUL:
      return "FMUL";
    case FSUB:
      return "FSUB";
    case FDIV:
      return "FDIV";
    case CONSTANT:
      return "CONSTANT";
    case SQRT:  
      return "SQRT";
    case FLOOR:  
      return "FLOOR";
    case TAN:  
      return "TAN";
    case SIN:  
      return "SIN";
    case COS:  
      return "COS";
    case ATAN:  
      return "ATAN";
    case ABS:  
      return "ABS";
    case LOG:  
      return "LOG";
    case ASIN:  
      return "ASIN";
    case EXP:  
      return "EXP";
    case POW:
      return "POW";
    default:
      return "Unknown";
  }
}

#ifdef TRACING
int m_get_depth(temp_entry *current){
  int depth = 0;
  m_expr.push_back(current);
  int level;
  while(!m_expr.empty()){
    level = m_expr.size();
    std::cout<<"\n";
    while(level > 0){
      temp_entry *cur = m_expr.front();
      if(cur == NULL){
        return depth;
      }
      if(m_lock_key_map[cur->op1_lock] != cur->op1_key ){
        return depth;
      }
      if(m_lock_key_map[cur->op2_lock] != cur->op2_key){
        return depth;
      }
      if(cur->lhs != NULL){
        if(cur->lhs->timestamp < cur->timestamp){
          m_expr.push_back(cur->lhs);
        }
      }
      if(cur->rhs != NULL){
        if(cur->rhs->timestamp < cur->timestamp){
          m_expr.push_back(cur->rhs);
        }
      }
      m_expr.pop_front();
      level--;
    }
    depth++;
  }
  return depth;
}
#endif

extern "C" void fpsan_func_init(long totalArgs) {

#ifdef TRACING  
  m_key_stack_top++;
  m_key_counter++;
  m_lock_key_map[m_key_stack_top] = m_key_counter;
#endif
  
  m_stack_top = m_stack_top + totalArgs;

  
}

extern "C" void fpsan_func_exit(long totalArgs) {
  
#ifdef TRACING
  m_lock_key_map[m_key_stack_top] = 0;
  m_key_stack_top--;
#endif  

  m_stack_top = m_stack_top - totalArgs;

  
}

/* Copy the metadata of the return value of the function and insert
   into the shadow stack. The space for the return value is allocated
   by the caller. This happens in the callee. */

extern "C" void fpsan_set_return(temp_entry* src, size_t totalArgs, double op) {
  
  /* Santosh: revisit this design, make 0 distance from the stack top
     as the return */

  temp_entry *dest = &(m_shadow_stack[m_stack_top - totalArgs]); 
  if(src != NULL){
    m_set_mpfr(&(dest->val), &(src->val));
    dest->computed = src->computed;
    dest->opcode = src->opcode;
    dest->lineno = src->lineno;
    dest->is_init = true;
    
#ifdef TRACING    
    dest->error = src->error;
    dest->lock = m_key_stack_top;
    dest->key = m_lock_key_map[m_key_stack_top];

    dest->op1_lock = src->op1_lock;
    dest->op1_key = src->op1_key;
    dest->lhs = src->lhs;
    dest->op2_lock = src->op2_lock;
    dest->op2_key = src->op2_key;
    dest->rhs = src->rhs;
    dest->timestamp = src->timestamp;
     
#endif
    
  }
  else{
    std::cout<<"__set_return copying src is null:"<<"\n";
    fpsan_store_tempmeta_dconst(dest, op, 0); 
    //    m_store_shadow_dconst(dest, op, 0); //when we return tmp 
    //we don't need to set metadata as it is null
  }
  /*  one of set_return or func_exit is called, so cleanup the
      shadow stack */
  m_stack_top = m_stack_top - totalArgs;

#ifdef TRACING  
  m_lock_key_map[m_key_stack_top] = 0;
  m_key_stack_top--;
#endif
  
}

/* Retrieve the metadata for the return value from the shadow
   stack. This happens in the caller. */
extern "C" void fpsan_get_return(temp_entry* dest) {

  temp_entry *src = &(m_shadow_stack[m_stack_top]); //save return m_stack_top - totalArgs 
  m_set_mpfr(&(dest->val), &(src->val));
  dest->computed = src->computed;
  dest->lineno = src->lineno;
  dest->opcode = src->opcode;

#ifdef TRACING  
  dest->lock = m_key_stack_top;
  dest->key = m_lock_key_map[m_key_stack_top];
  dest->error = src->error;
  dest->timestamp = m_timestamp++;

  dest->op1_lock = src->op1_lock;
  dest->op1_key = src->op1_key;
  dest->op2_lock = src->op2_lock;
  dest->op2_key = src->op2_key;
  dest->lhs = src->lhs;
  dest->rhs = src->rhs;

#endif  

}

/* The callee retrieves the metadata from the shadow stack */

extern "C" temp_entry* fpsan_get_arg(size_t argIdx, double op) {

  temp_entry *dst = &(m_shadow_stack[m_stack_top-argIdx]);

#ifdef TRACING  
  dst->lock = m_key_stack_top;
  dst->key = m_lock_key_map[m_key_stack_top];
#endif  

  if(!dst->is_init){ //caller maybe is not instrumented for set_arg
    fpsan_store_tempmeta_dconst(dst, op, 0);
    //m_store_shadow_dconst(dst, op, 0);
  }

  return dst;
}

extern "C" void fpsan_set_arg_f(size_t argIdx, temp_entry* src, float op) {

  temp_entry *dest = &(m_shadow_stack[argIdx+m_stack_top]);
  assert(argIdx < MAX_STACK_SIZE && "Arg index is more than MAX_STACK_SIZE");

  /* Santosh: Check if we will ever have src == NULL with arguments */
  if(src != NULL){
    m_set_mpfr(&(dest->val), &(src->val));
    dest->computed = src->computed;
    dest->opcode = src->opcode;
    dest->lineno = src->lineno;
    dest->is_init = true;

#ifdef TRACING    
    dest->lock = m_key_stack_top;
    dest->key = m_lock_key_map[m_key_stack_top];
    dest->error = src->error;

    dest->op1_lock = src->op1_lock;
    dest->op1_key = src->op1_key;
    dest->lhs = src->lhs;
    dest->op2_lock = src->op2_lock;
    dest->op2_key = src->op2_key;
    dest->rhs = src->rhs;
    dest->timestamp = src->timestamp;

#endif
    
  }
  else{
    fpsan_store_tempmeta_fconst(dest, op, 0);
    //    m_store_shadow_fconst(dest, op, 0);
    dest->is_init = true;
  }

}

extern "C" void fpsan_set_arg_d(size_t argIdx, temp_entry* src, double op) {

  temp_entry *dest = &(m_shadow_stack[argIdx+m_stack_top]);
  assert(argIdx < MAX_STACK_SIZE && "Arg index is more than MAX_STACK_SIZE");

  /* Santosh: Check if we will ever have src == NULL with arguments */
  if(src != NULL){
    m_set_mpfr(&(dest->val), &(src->val));
    dest->computed = src->computed;
    dest->lineno = src->lineno;
    dest->opcode = src->opcode;
    dest->is_init = true;

#ifdef TRACING    
    dest->lock = m_key_stack_top;
    dest->key = m_lock_key_map[m_key_stack_top];
    dest->error = src->error;

    dest->op1_lock = src->op1_lock;
    dest->op1_key = src->op1_key;
    dest->lhs = src->lhs;
    dest->op2_lock = src->op2_lock;
    dest->op2_key = src->op2_key;
    dest->rhs = src->rhs;
    dest->timestamp = src->timestamp;
    

#endif
    
  }
  else{
    fpsan_store_tempmeta_fconst(dest, op, 0);
    //    m_store_shadow_dconst(dest, op, 0);
    dest->is_init = true;
  }
}

unsigned long m_ulpd(double x, double y) {
  if (x == 0)
    x = 0; // -0 == 0
  if (y == 0)
    y = 0; // -0 == 0

  /* if (x != x && y != y) return 0; */
  if (x != x)
    return ULLONG_MAX - 1; // Maximum error
  if (y != y)
    return ULLONG_MAX - 1; // Maximum error

  long long xx = *((long long *)&x);
  xx = xx < 0 ? LLONG_MIN - xx : xx;

  long long yy = *((long long *)&y);
  yy = yy < 0 ? LLONG_MIN - yy : yy;
  return xx >= yy ? xx - yy : yy - xx;
}


extern "C" void fpsan_finish() {

  if (!m_init_flag) {
    return;
  }
  fprintf(m_errfile, "Error above %d bits found %zd\n", ERRORTHRESHOLD4, errorCount63);
  fprintf(m_errfile, "Error above %d bits found %zd\n", ERRORTHRESHOLD3, errorCount55);
  fprintf(m_errfile, "Error above %d bits found %zd\n", ERRORTHRESHOLD2, errorCount45);
  fprintf(m_errfile, "Error above %d bits found %zd\n", ERRORTHRESHOLD1, errorCount35);
  fprintf(m_errfile, "Total NaN found %zd\n", nanCount);
  fprintf(m_errfile, "Total Inf found %zd\n", infCount);
  fprintf(m_errfile, "Total branch flips found %zd\n", flipsCount);
  fprintf(m_errfile, "Total catastrophic cancellation found %zd\n", ccCount);

  fclose(m_errfile);
  fclose(m_brfile);
}

int m_update_error(temp_entry *real, double computedVal){
  double shadowRounded = m_get_double(real->val);
  unsigned long ulpsError = m_ulpd(shadowRounded, computedVal);

  double bitsError = log2(ulpsError + 1);

  if (debugerror){
    std::cout<<"\nThe shadow value is ";
    m_print_real(real->val);
    if (computedVal != computedVal){
      std::cout<<", but NaN was computed.\n";
    } else {
      std::cout<<", but ";
      printf("%e", computedVal);
      std::cout<<" was computed.\n";
      std::cout<<"m_update_error: computedVal:"<<computedVal<<"\n";
    }
    printf("%f bits error (%lu ulps)\n",
        bitsError, ulpsError);
    std::cout<<"****************\n\n";
  }
  return bitsError;
}


extern "C" void fpsan_get_error(void *Addr, double computed){
  size_t AddrInt = (size_t) Addr;
  smem_entry* dest = m_get_shadowaddress(AddrInt);

  double shadowRounded = m_get_double(dest->val);
  unsigned long ulpsError = m_ulpd(shadowRounded,  computed);

  printf("getError real:");
  m_print_real(dest->val);
  printf("\n");
  printf("getError computed: %e", computed);
  printf("\n");
  double bitsError = log2(ulpsError + 1);
  fprintf(m_errfile, "computed:%e real:%e Error: %lu ulps (%lf bits)\n", computed, shadowRounded, ulpsError, bitsError);
}


/* Math library functions */
extern "C" void fpsan_mpfr_sqrt(temp_entry* op1, 
				double op1d,
				temp_entry* res, 
				double computedRes,
				unsigned long long int instId, 
				bool debugInfoAvail, 
				unsigned int linenumber, 
				unsigned int colnumber){

  handle_math_d(SQRT, op1d, op1, computedRes, res, linenumber);
 
}

extern "C" void fpsan_mpfr_sqrtf(temp_entry* op1, 
				float op1d,
				temp_entry* res, 
				float computedRes,
				unsigned long long int instId, 
				bool debugInfoAvail, 
				unsigned int linenumber, 
				unsigned int colnumber){

  handle_math_d(SQRT, op1d, op1, computedRes, res, linenumber);
 
}
extern "C" void fpsan_mpfr_floor(temp_entry* op1, 
                                 double op1d,
                                 temp_entry* res, 
                                 double computedRes,
                                 unsigned long long int instId, 
                                 bool debugInfoAvail, 
                                 unsigned int linenumber, 
                                 unsigned int colnumber){

  handle_math_d(FLOOR, op1d, op1, computedRes, res, linenumber);
  
}

extern "C" void fpsan_mpfr_tanh(temp_entry* op1, 
				double op1d,
				temp_entry* res, 
				double computedRes,
				unsigned long long int instId, 
				bool debugInfoAvail, 
				unsigned int linenumber, 
				unsigned int colnumber){

  handle_math_d(TANH, op1d, op1, computedRes, res, linenumber);

}

extern "C" void fpsan_mpfr_tan(temp_entry* op1, 
			       double op1d,
			       temp_entry* res, 
			       double computedRes,
			       unsigned long long int instId, 
			       bool debugInfoAvail, 
			       unsigned int linenumber, 
			       unsigned int colnumber){

  handle_math_d(TAN, op1d, op1, computedRes, res, linenumber);

}

extern "C" void fpsan_mpfr_acos(temp_entry* op1, 
				double op1d,
				temp_entry* res, 
				double computedRes,
				unsigned long long int instId, 
				bool debugInfoAvail, 
				unsigned int linenumber, 
				unsigned int colnumber){

  handle_math_d(ACOS, op1d, op1, computedRes, res, linenumber);
}

extern "C" void fpsan_mpfr_cosh(temp_entry* op1, 
				double op1d,
				temp_entry* res, 
				double computedRes,
				unsigned long long int instId, 
				bool debugInfoAvail, 
				unsigned int linenumber, 
				unsigned int colnumber){

  handle_math_d(COSH, op1d, op1, computedRes, res, linenumber);

}

extern "C" void fpsan_mpfr_cos(temp_entry* op1, 
			       double op1d,
			       temp_entry* res, 
			       double computedRes,
			       unsigned long long int instId, 
			       bool debugInfoAvail, 
			       unsigned int linenumber, 
			       unsigned int colnumber){

  handle_math_d(COS, op1d, op1, computedRes, res, linenumber);

}

extern "C" void fpsan_mpfr_atan(temp_entry* op1, 
				double op1d,
				temp_entry* res, 
				double computedRes,
				unsigned long long int instId, 
				bool debugInfoAvail, 
				unsigned int linenumber, 
				unsigned int colnumber){

  handle_math_d(ATAN, op1d, op1, computedRes, res, linenumber);
}

extern "C" void fpsan_mpfr_llvm_ceil(temp_entry* op1, 
				     double op1d,
				     temp_entry* res, 
				     double computedRes,
				     unsigned long long int instId, 
				     bool debugInfoAvail, 
				     unsigned int linenumber, 
				     unsigned int colnumber){

  handle_math_d(CEIL, op1d, op1, computedRes, res, linenumber);

}

extern "C" void fpsan_mpfr_llvm_floor(temp_entry* op1Idx, 
				      double op1d,
				      temp_entry* res, 
				      double computedRes,
				      unsigned long long int instId, 
				      bool debugInfoAvail, 
				      unsigned int linenumber, 
				      unsigned int colnumber){

  handle_math_d(FLOOR, op1d, op1Idx, computedRes, res, linenumber);

}

extern "C" void fpsan_mpfr_exp(temp_entry* op1Idx, 
			   double op1d,
			   temp_entry* res, 
			   double computedRes,
			   unsigned long long int instId, 
			   bool debugInfoAvail, 
			   unsigned int linenumber, 
			   unsigned int colnumber){
  handle_math_d(EXP, op1d, op1Idx, computedRes, res, linenumber);

}

extern "C" void fpsan_mpfr_GSL_MIN_DBL2(temp_entry* op1Idx, double op1d, 
					temp_entry* op2Idx, double op2d, 
					temp_entry* res, double computedRes,
					unsigned long long int instId, 
					bool debugInfoAvail, 
					unsigned int linenumber, 
					unsigned int colnumber){

  mpfr_min(res->val, op1Idx->val, op2Idx->val, MPFR_RNDN);
  res->lineno = linenumber;
  res->opcode = MIN;
  res->computed = computedRes;

#ifdef TRACING  
  int bitsError = m_update_error(res, computedRes);
  res->op1_lock = op1Idx->lock;
  res->op1_key = m_lock_key_map[op1Idx->lock];
  res->op2_lock = op2Idx->lock;
  res->op2_key = m_lock_key_map[op2Idx->lock];
  res->lock = m_key_stack_top;
  res->key = m_lock_key_map[m_key_stack_top];
  res->lhs = op1Idx;
  res->rhs = op2Idx;
  res->timestamp = m_timestamp++;
  res->error = bitsError;
#endif  
  
}


extern "C" void fpsan_mpfr_GSL_MAX_DBL2(temp_entry* op1Idx, double op1d, 
					temp_entry* op2Idx, double op2d, 
					temp_entry* res, double computedRes,
					unsigned long long int instId, 
					bool debugInfoAvail, 
					unsigned int linenumber, 
					unsigned int colnumber){
  

  mpfr_max(res->val, op1Idx->val, op2Idx->val, MPFR_RNDN);
  res->opcode = MAX;
  res->computed = computedRes;
  res->lineno = linenumber;

#ifdef TRACING
  
  int bitsError = m_update_error(res, computedRes);
  res->op1_lock = op1Idx->lock;
  res->op1_key = m_lock_key_map[op1Idx->lock];
  res->op2_lock = op2Idx->lock;
  res->op2_key = m_lock_key_map[op2Idx->lock];
  res->lock = m_key_stack_top;
  res->key = m_lock_key_map[m_key_stack_top];
  res->lhs = op1Idx;
  res->rhs = op2Idx;
  res->timestamp = m_timestamp++;
  res->error = bitsError;
#endif
  
}

extern "C" void fpsan_mpfr_ldexp2(temp_entry* op1Idx, double op1d, 
				  int op2d, 
				  temp_entry* res, double computedRes,
				  unsigned long long int instId, 
				  bool debugInfoAvail, 
				  unsigned int linenumber, 
				  unsigned int colnumber){
  
  //op1*2^(op2)
  mpfr_t exp;
  mpfr_init2(exp, m_precision);
  mpfr_set_si(exp, op2d, MPFR_RNDN);
  mpfr_exp2(res->val, exp, MPFR_RNDN);
  mpfr_mul(res->val, op1Idx->val, res->val,  MPFR_RNDN);
  
  mpfr_clear(exp);

  res->lineno = linenumber;
  res->opcode = LDEXP;
  res->computed = computedRes;


#ifdef TRACING  
  
  int bitsError = m_update_error(res, computedRes);
  res->op1_lock = op1Idx->lock;
  res->op1_key = m_lock_key_map[op1Idx->lock];
  res->op2_lock = 0;
  res->op2_key = 0;
  res->lock = m_key_stack_top;
  res->key = m_lock_key_map[m_key_stack_top];
  res->lhs = op1Idx;
  res->rhs = NULL;
  res->timestamp = m_timestamp++;
  res->error = bitsError;
#endif
  
}

extern "C" void fpsan_mpfr_fmod2(temp_entry* op1Idx, double op1d, 
				 temp_entry* op2Idx, double op2d, 
				 temp_entry* res, double computedRes,
				 unsigned long long int instId, 
				 bool debugInfoAvail, 
				 unsigned int linenumber, 
				 unsigned int colnumber){
  
  mpfr_fmod(res->val, op1Idx->val, op2Idx->val, MPFR_RNDN);
  res->opcode = FMOD;
  res->computed = computedRes;
  res->lineno = linenumber;  

#ifdef TRACING  
  int bitsError = m_update_error(res, computedRes);
  res->op1_lock = op1Idx->lock;
  res->op1_key = m_lock_key_map[op1Idx->lock];
  res->op2_lock = op2Idx->lock;
  res->op2_key = m_lock_key_map[op2Idx->lock];
  res->lock = m_key_stack_top;
  res->key = m_lock_key_map[m_key_stack_top];
  res->lhs = op1Idx;
  res->rhs = op2Idx;
  res->timestamp = m_timestamp++;
  res->error = bitsError;
#endif
  
}

extern "C" void fpsan_mpfr_atan22(temp_entry* op1, double op1d, 
				  temp_entry* op2, double op2d, 
				  temp_entry* res, double computedRes,
				  unsigned long long int instId, 
				  bool debugInfoAvail, 
				  unsigned int linenumber, 
				  unsigned int colnumber){
  
  
  mpfr_atan2(res->val, op1->val, op2->val, MPFR_RNDN);
  res->opcode = ATAN2;
  res->computed = computedRes;
  res->lineno = linenumber;

#ifdef TRACING
  
  int bitsError = m_update_error(res, computedRes);
  res->op1_lock = op1->lock;
  res->op1_key = m_lock_key_map[op1->lock];
  res->op2_lock = op2->lock;
  res->op2_key = m_lock_key_map[op2->lock];
  res->lock = m_key_stack_top;
  res->key = m_lock_key_map[m_key_stack_top];
  res->lhs = op1;
  res->rhs = op2;
  res->timestamp = m_timestamp++;
  res->error = bitsError;
  
#endif
}

extern "C" void fpsan_mpfr_hypot2(temp_entry* op1, double op1d, 
				  temp_entry* op2, double op2d, 
				  temp_entry* res, double computedRes,
				  unsigned long long int instId, 
				  bool debugInfoAvail, 
				  unsigned int linenumber, 
				  unsigned int colnumber){
  
  mpfr_hypot(res->val, op1->val, op2->val, MPFR_RNDN);
  res->opcode = HYPOT;
  res->computed = computedRes;
  res->lineno = linenumber;  

#ifdef TRACING
  
  int bitsError = m_update_error(res, computedRes);
  res->op1_lock = op1->lock;
  res->op1_key = m_lock_key_map[op1->lock];
  res->op2_lock = op2->lock;
  res->op2_key = m_lock_key_map[op2->lock];
  res->lock = m_key_stack_top;
  res->key = m_lock_key_map[m_key_stack_top];
  res->lhs = op1;
  res->rhs = op2;
  res->timestamp = m_timestamp++;
  res->error = bitsError;
#endif  

}

extern "C" void fpsan_mpfr_pow2(temp_entry* op1, double op1d, 
				temp_entry* op2, double op2d, 
				temp_entry* res, double computedRes,
				unsigned long long int instId, 
				bool debugInfoAvail, 
				unsigned int linenumber, 
				unsigned int colnumber){
  
  mpfr_pow(res->val, op1->val, op2->val, MPFR_RNDN);
  res->opcode = POW;
  res->computed = computedRes;
  res->lineno = linenumber;  

#ifdef TRACING  
  int bitsError = m_update_error(res, computedRes);
  res->op1_lock = op1->lock;
  res->op1_key = m_lock_key_map[op1->lock];
  res->op2_lock = op2->lock;
  res->op2_key = m_lock_key_map[op2->lock];
  res->lock = m_key_stack_top;
  res->key = m_lock_key_map[m_key_stack_top];
  res->lhs = op1;
  res->rhs = op2;

  res->timestamp = m_timestamp++;
  res->error = bitsError;
#endif
  
}

extern "C" void fpsan_mpfr_llvm_fabs(temp_entry* op1, 
				     double op1d,
				     temp_entry* res, 
				     double computedRes,
				     unsigned long long int instId, 
				     bool debugInfoAvail, 
				     unsigned int linenumber, 
				     unsigned int colnumber){

  handle_math_d(ABS, op1d, op1, computedRes, res, linenumber);

}

extern "C" void fpsan_mpfr_abs(temp_entry* op1, temp_entry* res,
			       double op1d, double computedRes,
			       unsigned long long int instId,
			       bool debugInfoAvail, unsigned int linenumber,
			       unsigned int colnumber){


  handle_math_d(ABS, op1d, op1, computedRes, res, linenumber);

}

extern "C" void fpsan_mpfr_log10(temp_entry* op1, temp_entry* res,
			       double op1d, double computedRes,
			       unsigned long long int instId,
			       bool debugInfoAvail, unsigned int linenumber,
			       unsigned int colnumber){

  handle_math_d(LOG10, op1d, op1, computedRes, res, linenumber);

}
extern "C" void fpsan_mpfr_log(temp_entry* op1, temp_entry* res,
			       double op1d, double computedRes,
			       unsigned long long int instId,
			       bool debugInfoAvail, unsigned int linenumber,
			       unsigned int colnumber){

  handle_math_d(LOG, op1d, op1, computedRes, res, linenumber);

}

extern "C" void fpsan_mpfr_sinh(temp_entry* op1, temp_entry* res,
				double op1d, double computedRes,
				unsigned long long int instId,
				bool debugInfoAvail, unsigned int linenumber,
				unsigned int colnumber){

  handle_math_d(SIN, op1d, op1, computedRes, res, linenumber);

}

extern "C" void fpsan_mpfr_sin(temp_entry* op1, temp_entry* res,
			       double op1d, double computedRes,
			       unsigned long long int instId,
			       bool debugInfoAvail, unsigned int linenumber,
			       unsigned int colnumber){
  handle_math_d(SIN, op1d, op1, computedRes, res, linenumber);

}

extern "C" void fpsan_mpfr_asin(temp_entry* op1,
				temp_entry* res,
				double op1d,
				double computedRes,
				unsigned long long int instId,
				bool debugInfoAvail,
				unsigned int linenumber,
				unsigned int colnumber){
  handle_math_d(ASIN, op1d, op1, computedRes, res, linenumber);

}
