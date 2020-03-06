#include "handleReal.h"

// fpsan_trace: a function that user can set a breakpoint on to
// generate DAGs
extern "C" void fpsan_trace(temp_entry *current){
  m_expr.push_back(current);
  int level;
  while(!m_expr.empty()){
    level = m_expr.size();
    temp_entry *cur = m_expr.front();
    std::cout<<"\n";
    if(cur == NULL){
      return;
    }
    std::cout<<" "<<cur->lineno<<" "<<m_get_string_opcode(cur->opcode)<<" ";
    fflush(stdout);
    if(m_lock_key_map[cur->op1_lock] != cur->op1_key ){
      return;
    }
    if(m_lock_key_map[cur->op2_lock] != cur->op2_key){
      return;
    }
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



// fpsan_check_branch, fpsan_check_conversion, fpsan_check_error are
// functions that user can set breakpoint on

extern "C" unsigned int  fpsan_check_branch(bool realBr, bool computedBr,
					    temp_entry *realRes1,
					    temp_entry *realRes2){
  if(realBr != computedBr) {
    return 1;
  }
  return 0;
}

extern "C" unsigned int  fpsan_check_conversion(long real, long computed,
						temp_entry *realRes){
  if(real != computed){
    return 1;
  }
  return 0;
}

extern "C" unsigned int fpsan_check_error(temp_entry *realRes, double computedRes){
  if(debugtrace){
    std::cout<<"m_expr starts\n";
    m_expr.clear();
    fpsan_trace(realRes);
    std::cout<<"\nm_expr ends\n";
    std::cout<<"\n";
  }
  if(realRes->error > ERRORTHRESHOLD4)
    return 4;
  else if(realRes->error > ERRORTHRESHOLD3) //With shadow execution
    return 3;
  else if(realRes->error > ERRORTHRESHOLD2) //With shadow execution
    return 2;
  else if(realRes->error > ERRORTHRESHOLD1) //With shadow execution
    return 1;
  return 0;
}




extern "C" void fpsan_init() {
  if (!m_init_flag) {
    
    m_errfile = fopen ("error.log","w");
    m_brfile = fopen ("branch.log","w");
    
    //printf("sizeof Real %lu", sizeof(struct smem_entry));
    m_init_flag = true;
    size_t length = MAX_STACK_SIZE * sizeof(temp_entry);
    size_t memLen = SS_PRIMARY_TABLE_ENTRIES * sizeof(temp_entry);
    m_shadow_stack =
      (smem_entry *)mmap(0, length, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
    m_lock_key_map =
      (size_t *)mmap(0, length, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
    m_shadow_memory =
      (smem_entry **)mmap(0, memLen, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
    assert(m_lock_key_map != (void *)-1);
    assert(m_shadow_stack != (void *)-1);
    assert(m_shadow_memory != (void *)-1);

    m_key_stack_top = 1;
    m_key_counter = 1;

    m_lock_key_map[m_key_stack_top] = m_key_counter;

    for(int i =0; i<MAX_STACK_SIZE; i++){
      mpfr_init2(m_shadow_stack[i].val, m_precision);
    }
    m_stack_top = 0;
  }
}

void m_set_mpfr(mpfr_t *val1, mpfr_t *val2) {
  mpfr_set(*val1, *val2, MPFR_RNDN);
}

//primarily used in the LLVM IR for initializing stack metadata
extern "C" void fpsan_init_mpfr(temp_entry *op) {

  mpfr_init2(op->val, m_precision);
  op->lock = m_key_stack_top;
  op->key = m_lock_key_map[m_key_stack_top];

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
  op->lock = m_key_stack_top;
  op->key = m_lock_key_map[m_key_stack_top];
  op->error = 0;
  op->lineno = linenumber;
  op->opcode = CONSTANT;
  op->tmp_ptr = NULL;
  op->computed = d;

}

void m_store_shadow_fconst(smem_entry *op, float f, unsigned int linenumber) {

  mpfr_set_flt(op->val, f, MPFR_RNDN);
  op->lock = m_key_stack_top;
  op->key = m_lock_key_map[m_key_stack_top];
  op->error = 0;
  op->lineno = linenumber;
  op->opcode = CONSTANT;
  op->tmp_ptr = NULL;
  op->computed = f;

}

extern "C" void fpsan_store_tempmeta_dconst(temp_entry *op, double d, unsigned int linenumber) {

  mpfr_set_d(op->val, d, MPFR_RNDN);
  op->lock = m_key_stack_top;
  op->key = m_lock_key_map[m_key_stack_top];
  op->op1_lock = 0;
  op->op1_key = 0;
  op->op2_lock = 0;
  op->op2_key = 0;
  op->lhs = NULL;
  op->rhs = NULL;
  op->error = 0;
  op->lineno = linenumber;
  op->opcode = CONSTANT;
  op->timestamp = m_timestamp++;
  op->computed = d;

}

extern "C" void fpsan_store_tempmeta_fconst(temp_entry *op, float f, unsigned int linenumber) {

  mpfr_set_flt(op->val, f, MPFR_RNDN);
  op->lock = m_key_stack_top;
  op->key = m_lock_key_map[m_key_stack_top];
  op->op1_lock = 0;
  op->op1_key = 0;
  op->op2_lock = 0;
  op->op2_key = 0;
  op->lhs = NULL;
  op->rhs = NULL;
  op->error = 0;
  op->lineno = linenumber;
  op->opcode = CONSTANT;
  op->timestamp = m_timestamp++;
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
  dest->error = src->error;
  dest->lineno = src->lineno;
  dest->computed = src->computed;
  dest->lock = m_key_stack_top; 
  dest->key = m_lock_key_map[m_key_stack_top]; 
  dest->is_init = true;
  dest->tmp_ptr = src;
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
  src->lock = m_key_stack_top; 
  src->key = m_lock_key_map[m_key_stack_top];
  src->error = dest->error;
  src->lineno = dest->lineno;
  src->computed = dest->computed;
  src->opcode = dest->opcode;


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
    src->op1_lock = 0;
    src->op1_key = 0;
    src->op2_lock = 0;
    src->op2_key = 0;
    src->lhs = NULL;
    src->rhs = NULL;
    src->timestamp = m_timestamp++;
    fpsan_store_tempmeta_fconst(src, d, 0); //for global variables
  }
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

  src->lock = m_key_stack_top; 
  src->key = m_lock_key_map[m_key_stack_top];
  src->error = dest->error;
  src->lineno = dest->lineno;
  src->computed = dest->computed;
  src->opcode = dest->opcode;

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
    src->op1_lock = 0;
    src->op1_key = 0;
    src->op2_lock = 0;
    src->op2_key = 0;
    src->lhs = NULL;
    src->rhs = NULL;
    src->timestamp = m_timestamp++;
    fpsan_store_tempmeta_dconst(src, d, 0); //for global variables
  }
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

  int bitsError = m_update_error(res, computedResd);
  res->op1_lock = op->lock;
  res->op1_key = m_lock_key_map[op->lock];
  res->op2_lock = 0;
  res->op2_key = 0;
  res->lock = m_key_stack_top;
  res->key = m_lock_key_map[m_key_stack_top];
  res->lhs = op;
  res->rhs = nullptr;
  res->lineno = linenumber;
  res->timestamp = m_timestamp++;
  res->error = bitsError;
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

  if(isinf(computedResd))
    infCount++;
  if (computedResd != computedResd)
    nanCount++;
  if(debug){
    printf("compute: res:%p\n", res);
    m_print_real(res->val);
    printf("compute: res:%p\n", res);
  }
  int bitsError = m_update_error(res, computedResd);
  res->op1_lock = op1->lock;
  res->op2_lock = op2->lock;
  res->op1_key = m_lock_key_map[op1->lock];
  res->op2_key = m_lock_key_map[op2->lock];
  res->lock = m_key_stack_top;
  res->key = m_lock_key_map[m_key_stack_top];
  res->lhs = op1;
  res->rhs = op2;
  res->lineno = lineNo;
  res->timestamp = m_timestamp++;
  res->error = bitsError;
  res->opcode = opCode;
  res->computed = computedResd;
}

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


extern "C" void fpsan_func_init(long totalArgs) {

  m_key_stack_top++;
  m_key_counter++;
  m_lock_key_map[m_key_stack_top] = m_key_counter;
  m_stack_top = m_stack_top + totalArgs;
}

extern "C" void fpsan_func_exit(long totalArgs) {

  m_lock_key_map[m_key_stack_top] = 0;
  m_key_stack_top--;

  m_stack_top = m_stack_top - totalArgs;
}

/* Copy the metadata of the return value of the function and insert
   into the shadow stack. The space for the return value is allocated
   by the caller. This happens in the callee. */

extern "C" void fpsan_set_return(temp_entry* src, size_t totalArgs, double op) {

  /* Santosh: revisit this design, make 0 distance from the stack top
     as the return */

  smem_entry *dest = &(m_shadow_stack[m_stack_top - totalArgs]); 
  if(src != NULL){
    m_set_mpfr(&(dest->val), &(src->val));
    dest->computed = src->computed;
    dest->error = src->error;
    dest->lock = m_key_stack_top;
    dest->key = m_lock_key_map[m_key_stack_top];
    dest->lineno = src->lineno;
    dest->opcode = src->opcode;
    dest->tmp_ptr = src;
  }
  else{
    std::cout<<"__set_return copying src is null:"<<"\n";
    m_store_shadow_dconst(dest, op, 0); //when we return tmp 
    //we don't need to set metadata as it is null
  }
  /*  one of set_return or func_exit is called, so cleanup the
      shadow stack */
  m_stack_top = m_stack_top - totalArgs;
  m_lock_key_map[m_key_stack_top] = 0;
  m_key_stack_top--;
}

/* Retrieve the metadata for the return value from the shadow
   stack. This happens in the caller. */
extern "C" void fpsan_get_return(temp_entry* dest) {

  smem_entry *src = &(m_shadow_stack[m_stack_top]); //save return m_stack_top - totalArgs 
  m_set_mpfr(&(dest->val), &(src->val));
  dest->lock = m_key_stack_top;
  dest->key = m_lock_key_map[m_key_stack_top];
  dest->computed = src->computed;
  dest->error = src->error;
  dest->lineno = src->lineno;
  dest->timestamp = m_timestamp++;
  dest->opcode = src->opcode;

  if(m_lock_key_map[src->lock] == src->key){

    dest->op1_lock = src->tmp_ptr->op1_lock;
    dest->op1_key = src->tmp_ptr->op1_key;
    dest->op2_lock = src->tmp_ptr->op2_lock;
    dest->op2_key = src->tmp_ptr->op2_key;
    dest->lhs = src->tmp_ptr->lhs;
    dest->rhs = src->tmp_ptr->rhs;
  }
  else{
    dest->op1_lock = 0;
    dest->op1_key = 0;
    dest->op2_lock = 0;
    dest->op2_key = 0;
    dest->lhs = 0;
    dest->rhs = 0;
  }

}

/* The callee retrieves the metadata from the shadow stack */

extern "C" temp_entry* fpsan_get_arg(size_t argIdx, double op) {

  smem_entry *dst = &(m_shadow_stack[m_stack_top-argIdx]);
  
  dst->tmp_ptr->lock = m_key_stack_top;
  dst->tmp_ptr->key = m_lock_key_map[m_key_stack_top];

  if(!dst->is_init){ //caller maybe is not instrumented for set_arg
    m_store_shadow_dconst(dst, op, 0);
  }

  return dst->tmp_ptr;
}

extern "C" void fpsan_set_arg_f(size_t argIdx, temp_entry* src, float op) {

  smem_entry *dest = &(m_shadow_stack[argIdx+m_stack_top]);
  assert(argIdx < MAX_STACK_SIZE && "Arg index is more than MAX_STACK_SIZE");

  /* Santosh: Check if we will ever have src == NULL with arguments */
  if(src != NULL){
    m_set_mpfr(&(dest->val), &(src->val));
    dest->lock = m_key_stack_top;
    dest->key = m_lock_key_map[m_key_stack_top];
    dest->computed = src->computed;
    dest->error = src->error;
    dest->lineno = src->lineno;
    dest->is_init = true;
    dest->tmp_ptr = src;
  }
  else{
    
    m_store_shadow_fconst(dest, op, 0);
    dest->is_init = true;
  }

}

extern "C" void fpsan_set_arg_d(size_t argIdx, temp_entry* src, double op) {

  smem_entry *dest = &(m_shadow_stack[argIdx+m_stack_top]);
  assert(argIdx < MAX_STACK_SIZE && "Arg index is more than MAX_STACK_SIZE");

  /* Santosh: Check if we will ever have src == NULL with arguments */
  if(src != NULL){
    m_set_mpfr(&(dest->val), &(src->val));
    dest->lock = m_key_stack_top;
    dest->key = m_lock_key_map[m_key_stack_top];
    dest->computed = src->computed;
    dest->error = src->error;
    dest->lineno = src->lineno;
    dest->is_init = true;
    dest->tmp_ptr = src;
  }
  else{

    m_store_shadow_dconst(dest, op, 0);
    dest->is_init = true;
  }
}

void m_print_error(size_t opcode, temp_entry * real,
		 double d_value, unsigned int cbad,
		 unsigned long long int instId,
		 bool debugInfoAvail, unsigned int linenumber,
		 unsigned int colnumber){
  
  double shadow_rounded = m_get_double(real->val);

  unsigned long ulp_error = m_ulpd(shadow_rounded, d_value);
  double bits_error = log2(ulp_error + 1);


  if(bits_error > ERRORTHRESHOLD){
    if(m_inst_error_map.count(instId) == 0){
      m_inst_error_map[instId] = {bits_error, cbad, linenumber, colnumber, debugInfoAvail};
    }
    else{
      double old_error = m_inst_error_map[instId].error;
      if(old_error < bits_error){
        m_inst_error_map[instId].error =  bits_error;
      }
      m_inst_error_map[instId].cbad = cbad;
    }
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

  fprintf(m_errfile, "Error above 50 bits found %zd\n", errorCount);
  fprintf(m_errfile, "Total NaN found %zd\n", nanCount);
  fprintf(m_errfile, "Total Inf found %zd\n", infCount);
  fprintf(m_errfile, "Total branch flips found %zd\n", flipsCount);

  fclose(m_errfile);
  fclose(m_brfile);
}

int m_update_error(temp_entry *real, double computedVal){
  double shadowRounded = m_get_double(real->val);
  unsigned long ulpsError = m_ulpd(shadowRounded, computedVal);

  double bitsError = log2(ulpsError + 1);

  if(bitsError >  ERRORTHRESHOLD)
    errorCount++;
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
  
  int bitsError = m_update_error(res, computedRes);
  res->op1_lock = op1Idx->lock;
  res->op1_key = m_lock_key_map[op1Idx->lock];
  res->op2_lock = op2Idx->lock;
  res->op2_key = m_lock_key_map[op2Idx->lock];
  res->lock = m_key_stack_top;
  res->key = m_lock_key_map[m_key_stack_top];
  res->lhs = op1Idx;
  res->rhs = op2Idx;
  res->lineno = linenumber;
  res->timestamp = m_timestamp++;
  res->error = bitsError;
  res->opcode = MIN;
  res->computed = computedRes;
}


extern "C" void fpsan_mpfr_GSL_MAX_DBL2(temp_entry* op1Idx, double op1d, 
					temp_entry* op2Idx, double op2d, 
					temp_entry* res, double computedRes,
					unsigned long long int instId, 
					bool debugInfoAvail, 
					unsigned int linenumber, 
					unsigned int colnumber){
  

  mpfr_max(res->val, op1Idx->val, op2Idx->val, MPFR_RNDN);

  int bitsError = m_update_error(res, computedRes);
  res->op1_lock = op1Idx->lock;
  res->op1_key = m_lock_key_map[op1Idx->lock];
  res->op2_lock = op2Idx->lock;
  res->op2_key = m_lock_key_map[op2Idx->lock];
  res->lock = m_key_stack_top;
  res->key = m_lock_key_map[m_key_stack_top];
  res->lhs = op1Idx;
  res->rhs = op2Idx;
  res->lineno = linenumber;
  res->timestamp = m_timestamp++;
  res->error = bitsError;
  res->opcode = MAX;
  res->computed = computedRes;
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
  
  int bitsError = m_update_error(res, computedRes);
  res->op1_lock = op1Idx->lock;
  res->op1_key = m_lock_key_map[op1Idx->lock];
  res->op2_lock = 0;
  res->op2_key = 0;
  res->lock = m_key_stack_top;
  res->key = m_lock_key_map[m_key_stack_top];
  res->lhs = op1Idx;
  res->rhs = NULL;
  res->lineno = linenumber;
  res->timestamp = m_timestamp++;
  res->error = bitsError;
  res->opcode = LDEXP;
  res->computed = computedRes;
}

extern "C" void fpsan_mpfr_fmod2(temp_entry* op1Idx, double op1d, 
				 temp_entry* op2Idx, double op2d, 
				 temp_entry* res, double computedRes,
				 unsigned long long int instId, 
				 bool debugInfoAvail, 
				 unsigned int linenumber, 
				 unsigned int colnumber){
  
  mpfr_fmod(res->val, op1Idx->val, op2Idx->val, MPFR_RNDN);
  
  int bitsError = m_update_error(res, computedRes);
  res->op1_lock = op1Idx->lock;
  res->op1_key = m_lock_key_map[op1Idx->lock];
  res->op2_lock = op2Idx->lock;
  res->op2_key = m_lock_key_map[op2Idx->lock];
  res->lock = m_key_stack_top;
  res->key = m_lock_key_map[m_key_stack_top];
  res->lhs = op1Idx;
  res->rhs = op2Idx;
  res->lineno = linenumber;
  res->timestamp = m_timestamp++;
  res->error = bitsError;
  res->opcode = FMOD;
  res->computed = computedRes;
}

extern "C" void fpsan_mpfr_atan22(temp_entry* op1, double op1d, 
				  temp_entry* op2, double op2d, 
				  temp_entry* res, double computedRes,
				  unsigned long long int instId, 
				  bool debugInfoAvail, 
				  unsigned int linenumber, 
				  unsigned int colnumber){
  
  
  mpfr_atan2(res->val, op1->val, op2->val, MPFR_RNDN);
  
  int bitsError = m_update_error(res, computedRes);
  res->op1_lock = op1->lock;
  res->op1_key = m_lock_key_map[op1->lock];
  res->op2_lock = op2->lock;
  res->op2_key = m_lock_key_map[op2->lock];
  res->lock = m_key_stack_top;
  res->key = m_lock_key_map[m_key_stack_top];
  res->lhs = op1;
  res->rhs = op2;
  res->lineno = linenumber;
  res->timestamp = m_timestamp++;
  res->error = bitsError;
  res->opcode = ATAN2;
  res->computed = computedRes;

}

extern "C" void fpsan_mpfr_hypot2(temp_entry* op1, double op1d, 
				  temp_entry* op2, double op2d, 
				  temp_entry* res, double computedRes,
				  unsigned long long int instId, 
				  bool debugInfoAvail, 
				  unsigned int linenumber, 
				  unsigned int colnumber){
  
  mpfr_hypot(res->val, op1->val, op2->val, MPFR_RNDN);
  
  int bitsError = m_update_error(res, computedRes);
  res->op1_lock = op1->lock;
  res->op1_key = m_lock_key_map[op1->lock];
  res->op2_lock = op2->lock;
  res->op2_key = m_lock_key_map[op2->lock];
  res->lock = m_key_stack_top;
  res->key = m_lock_key_map[m_key_stack_top];
  res->lhs = op1;
  res->rhs = op2;
  res->lineno = linenumber;
  res->timestamp = m_timestamp++;
  res->error = bitsError;
  res->opcode = HYPOT;
  res->computed = computedRes;

}

extern "C" void fpsan_mpfr_pow2(temp_entry* op1, double op1d, 
				temp_entry* op2, double op2d, 
				temp_entry* res, double computedRes,
				unsigned long long int instId, 
				bool debugInfoAvail, 
				unsigned int linenumber, 
				unsigned int colnumber){
  
  mpfr_pow(res->val, op1->val, op2->val, MPFR_RNDN);
  
  int bitsError = m_update_error(res, computedRes);
  res->op1_lock = op1->lock;
  res->op1_key = m_lock_key_map[op1->lock];
  res->op2_lock = op2->lock;
  res->op2_key = m_lock_key_map[op2->lock];
  res->lock = m_key_stack_top;
  res->key = m_lock_key_map[m_key_stack_top];
  res->lhs = op1;
  res->rhs = op2;
  res->lineno = linenumber;
  res->timestamp = m_timestamp++;
  res->error = bitsError;
  res->opcode = POW;
  res->computed = computedRes;
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
