#include "handleReal.h"

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
extern "C" void fpsan_handle_fptrunc(float val, temp_entry* op1){
  op1->computed = val;
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
extern "C" unsigned int  fpsan_check_conversion(long real, long computed,
						temp_entry *realRes){
  if(real != computed){
    return 1;
  }
  return 0;
}

unsigned int check_error(temp_entry *realRes, double computedRes){
#ifdef TRACING  
  if(debugtrace){
    std::cout<<"m_expr starts\n";
    m_expr.clear();
    fpsan_trace(realRes);
    fpsan_get_fpcore(realRes);
    std::cout<<"\nm_expr ends\n";
    std::cout<<"\n";
  }


  if(realRes->error >= ERRORTHRESHOLD)
    return 4; 
  return 0;
#else
  int bits_error = m_update_error(realRes, computedRes);
  if(bits_error > ERRORTHRESHOLD)
    return 4;

  return 0;
#endif   
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

    m_shadow_stack =
      (temp_entry *)mmap(0, length, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
#ifdef TRACING
    m_lock_key_map =
      (size_t *)mmap(0, length, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
    assert(m_lock_key_map != (void *)-1);
#endif    


#ifdef METADATA_AS_TRIE
    
    size_t memLen = SS_PRIMARY_TABLE_ENTRIES * sizeof(smem_entry *);    
    m_shadow_memory = 
      (smem_entry **)mmap(0, memLen, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);

#else
    size_t hash_size = (HASH_TABLE_ENTRIES) * sizeof(smem_entry);
    m_shadow_memory = 
      (smem_entry *) mmap(0, hash_size, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
    
#endif    

    assert(m_shadow_stack != (void *)-1);
    assert(m_shadow_memory != (void *)-1);

#ifdef TRACING    
    m_key_stack_top = 1;
    m_key_counter = 1;

    m_lock_key_map[m_key_stack_top] = m_key_counter;
#endif    

    m_stack_top = 0;
  }
}

//primarily used in the LLVM IR for initializing stack metadata
extern "C" void fpsan_init_mpfr(temp_entry *op) {

#ifdef TRACING  
  op->lock = m_key_stack_top;
  op->key = m_lock_key_map[m_key_stack_top];
#endif  

}

extern "C" void fpsan_init_store_shadow_dconst(smem_entry *op, double d,
						  unsigned int linenumber) {
  
  if(!op->is_init){
    op->is_init = true;
  }
  m_store_shadow_dconst(op, d, linenumber);
}

extern "C" void fpsan_init_store_shadow_fconst(smem_entry *op, float f, unsigned int linenumber) {

  if(!op->is_init){
    op->is_init = true;
  }
  m_store_shadow_fconst(op, f, linenumber);
}

int m_isnan(mpfr_t real){
    return mpfr_nan_p(real);
}


void m_store_shadow_dconst(smem_entry *op, double d, unsigned int linenumber) {

  op->val = d;

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

  op->val = f;
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

#ifdef METADATA_AS_TRIE

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
  }
  return realAddr;
}

#else

smem_entry* m_get_shadowaddress (size_t address){
  size_t addr_int = address >> 2;
  size_t index = addr_int  % HASH_TABLE_ENTRIES;
  smem_entry* realAddr = m_shadow_memory + index;
  if(!realAddr->is_init){
    realAddr->is_init = true;
  }
  return realAddr;
}

#endif

extern "C" void fpsan_handle_memset(void *toAddr, int val,
    int size) {

  size_t toAddrInt = (size_t)(toAddr);
  for (int i = 0; i < size; i++) {
    smem_entry *dst = m_get_shadowaddress(toAddrInt + i);
    if (!dst->is_init) {
      dst->is_init = true;
    }
    dst->val = val;

#ifdef TRACING
    dst->error = 0;
    dst->lock = m_key_stack_top;
    dst->key = m_lock_key_map[m_key_stack_top];
    dst->tmp_ptr = 0;
#endif

    dst->is_init = true;
    dst->lineno = 0;
    dst->computed = val;
    dst->opcode = CONSTANT;
  }
}

extern "C" void fpsan_handle_memcpy(void* toAddr, void* fromAddr, int size){
  
  size_t toAddrInt = (size_t) (toAddr);
  size_t fromAddrInt = (size_t) (fromAddr);
  for(int i=0; i<size; i++){
    smem_entry* dst = m_get_shadowaddress(toAddrInt+i);
    smem_entry* src = m_get_shadowaddress(fromAddrInt+i);
    dst->val = src->val;

#ifdef TRACING  
    dst->error = src->error;
    dst->lock = m_key_stack_top; 
    dst->key = m_lock_key_map[m_key_stack_top]; 
    dst->tmp_ptr = src->tmp_ptr;
#endif  

    dst->is_init = true;
    dst->lineno = src->lineno;
    dst->computed = src->computed;
    dst->opcode = src->opcode;
  }
}
void m_print_real(double val){
  printf("%e", val);
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
    dst->val = src->val;
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
  dest->val = src->val;
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

#ifdef SELECTIVE
  
  double orig = (double) d;
  if(orig != dest->computed){
    fpsan_store_tempmeta_fconst(src, d, 0); //for global variables
    return;
  }
#endif
  
  /* copy the metadata from shadow memory to temp metadata*/
  src->val = dest->val;
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

#ifdef SELECTIVE
  /* double value in the metadata space mismatches with the computed
     value */
  if(d != dest->computed){
    fpsan_store_tempmeta_dconst(src, d, 0); 
    return;
  }
#endif
  
  src->val = dest->val;
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
    if(debug){
      std::cout<<"__load_d copying default\n";
    }
    fpsan_store_tempmeta_dconst(src, d, 0); //for global variables
  }
#endif
}

void handle_math_d(fp_op opCode, double op1d, temp_entry *op, 
		   double computedResd,  temp_entry *res,
		   unsigned int linenumber) {

  switch(opCode){
    case SQRT:
      res->val = sqrt(op->val);
      break;
    case FLOOR:
      res->val = floor(op->val);
      break;
    case CEIL:
      res->val = ceil(op->val);
      break;
    case TAN:
      res->val = tan(op->val);
      break;
    case TANH:
      res->val = tanh(op->val);
      break;
    case SIN:
      res->val = sin(op->val);
      break;
    case SINH:
      res->val = sinh(op->val);
      break;
    case COS:
      res->val = cos(op->val);
      break;
    case COSH:
      res->val = cosh(op->val);
      break;
    case ACOS:
      res->val = acos(op->val);
      break;
    case ATAN:
      res->val = atan(op->val);
      break;
    case ABS:
      res->val = abs(op->val);
      break;
    case LOG:
      res->val = log(op->val);
      break;
    case LOG10:
      res->val = log10(op->val);
      break;
    case ASIN: 
      res->val = asin(op->val);
      break;
    case EXP: 
      res->val = exp(op->val);
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
      res->val = op1->val + op2->val;
      break;
      
    case FSUB: 
      res->val = op1->val - op2->val;
      break;

    case FMUL: 
      res->val = op1->val * op2->val;
      break;
      
    case FDIV: 
      res->val = op1->val / op2->val;
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

extern "C" void fpsan_mpfr_fneg(temp_entry *op1Idx, temp_entry *res,
    unsigned int linenumber) {

  mpfr_t zero;
  mpfr_init2(zero, m_precision[buf_id].index);
  mpfr_set_d(zero, 0, MPFR_RNDN);

  mpfr_sub(res->val, zero, op1Idx->val, MPFR_RNDN);
  mpfr_clear(zero);
#ifdef TRACING
  res->op1_lock = op1Idx->op1_lock;
  res->op2_lock = op1Idx->op2_lock;
  res->op1_key = op1Idx->op1_key;
  res->op2_key = op1Idx->op2_key;
  res->lock = op1Idx->lock;
  res->key = op1Idx->key;
  res->lhs = op1Idx->lhs;
  res->rhs = op1Idx->rhs;
  res->timestamp = op1Idx->timestamp;
  res->error = op1Idx->error;
#endif

  res->inst_id = op1Idx->inst_id;
  res->opcode = op1Idx->opcode;
  res->computed = -op1Idx->computed;
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
  if(isinf(computedResD))
    infCount++;
}

bool m_check_branch(double op1, double op2,
    size_t fcmpFlag){
  bool realRes = false;
  switch (fcmpFlag) {
    case 0:
      realRes = false;
      break;
    case 1: /*oeq*/
      if (!isnan(op1) && !isnan(op2)) {
        if (op1 == op2)
          realRes = true;
      }
      break;
    case 2: /*ogt*/
      if (!isnan(op1) && !isnan(op2)) {
        if (op1 > op2) {
          realRes = true;
        }
      }
      break;
    case 3: /*oge*/
      if (!isnan(op1) && !isnan(op2)) {
        if (op1 >= op2) {
          realRes = true;
        }
      }
      break;
    case 4: /*olt*/
      if (!isnan(op1) && !isnan(op2)) {
        if (op1 < op2) {
          realRes = true;
        }
      }
      break;
    case 5: /*ole*/
      if (!isnan(op1) && !isnan(op2)) {
        if (op1 <= op2) {
          realRes = true;
        }
      }
      break;
    case 6: /*one*/
      if (!isnan(op1) && !isnan(op2)) {
        if (op1 != op2) {
          realRes = true;
        }
      }
      break;
    case 7: /*ord*/
      if (!isnan(op1) && !isnan(op2)) {
        realRes = true;
      }
      break;
    case 8:
      if (isnan(op1) && isnan(op2)) {
        realRes = true;
      }
      break;
    case 9:
      if (isnan(op1) || isnan(op2) || op1 == op2)
        realRes = true;
      break;
    case 10:
      if (isnan(op1) || isnan(op2) || op1 > op2)
        realRes = true;
      break;
    case 11:
      if (isnan(op1) || isnan(op2) || op1 >= op2)
        realRes = true;
      break;
    case 12:
      if (isnan(op1) || isnan(op2) || op1 < op2)
        realRes = true;
      break;
    case 13:
      if (isnan(op1) || isnan(op2) || op1 <= op2)
        realRes = true;
      break;
    case 14:
      if (isnan(op1) || isnan(op2) || op1 != op2) {
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
    dest->val = src->val;
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
  dest->val = src->val;
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
  fprintf(m_errfile, "Error above %d bits found %zd\n", ERRORTHRESHOLD, errorCount);
  fprintf(m_errfile, "Total NaN found %zd\n", nanCount);
  fprintf(m_errfile, "Total Inf found %zd\n", infCount);
  fprintf(m_errfile, "Total branch flips found %zd\n", flipsCount);
  fprintf(m_errfile, "Total catastrophic cancellation found %zd\n", ccCount);

  fclose(m_errfile);
  fclose(m_brfile);
}

int m_update_error(temp_entry *real, double computedVal){
  unsigned long ulpsError = m_ulpd(real->val, computedVal);

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

  unsigned long ulpsError = m_ulpd(dest->val,  computed);

  printf("getError real:");
  m_print_real(dest->val);
  printf("\n");
  printf("getError computed: %e", computed);
  printf("\n");
  double bitsError = log2(ulpsError + 1);
  fprintf(m_errfile, "computed:%e real:%e Error: %lu ulps (%lf bits)\n", computed, dest->val, ulpsError, bitsError);
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

extern "C" void fpsan_mpfr_llvm_f(temp_entry* op1, 
                                 float op1d,
                                 temp_entry* res, 
                                 float computedRes,
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

extern "C" void fpsan_mpfr_llvm_cos_f64(temp_entry* op1, 
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

extern "C" void fpsan_mpfr_llvm_floor_f(temp_entry* op1Idx, 
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
  double exp = op2d;
  res->val = exp2(exp);
  res->val = op1Idx->val * res->val;
  
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

extern "C" void fpsan_mpfr_llvm_sin_f64(temp_entry* op1, temp_entry* res,
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
