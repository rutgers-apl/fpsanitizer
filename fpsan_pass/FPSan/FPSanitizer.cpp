#include "FPSanitizer.h"
#include "llvm/IR/CallSite.h"  
#include "llvm/IR/ConstantFolder.h"
#include "llvm/ADT/SCCIterator.h" 
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"

static const char *const addFunArg = "__add_fun_arg_";  
static const char *const computeReal = "__compute_real_d_";  


//enum TYPE{MPFR, Posit64, Posit32, Posit16, Posit8};
enum TYPE{Double, Posit32, MPFR, Posit16, Posit8};

static cl::opt<int> Precision("fpsan-precision",
    cl::desc("default mpfr precision is initialized to 512"),
    cl::Hidden, cl::init(512));

static cl::opt<int> ENV("fpsan-with-type",
    cl::desc("shadow execution with mpfr"),
    cl::Hidden, cl::init(2));

void FPSanitizer::addFunctionsToList(std::string FN) {
	std::ofstream myfile;
	myfile.open("functions.txt", std::ios::out|std::ios::app);
	if (myfile.is_open()){
		myfile <<FN;
		myfile << "\n";
		myfile.close();
	}
}

//check name of the function and check if it is in list of functions given by 
//developer and return true else false.
bool FPSanitizer::isListedFunction(StringRef FN, std::string FileName) {
	std::ifstream infile(FileName);
	std::string line;
	while (std::getline(infile, line)) {
		if (FN.compare(line) == 0){
			return true;
		}
	}
	return false;
}

bool FPSanitizer::isFloatType(Type *InsType){
	if(InsType->getTypeID() == Type::DoubleTyID ||
	   InsType->getTypeID() == Type::FloatTyID)
		return true;
	return false;
}

bool FPSanitizer::isFloat(Type *InsType){
	if(InsType->getTypeID() == Type::FloatTyID)
		return true;
	return false;
}
bool FPSanitizer::isDouble(Type *InsType){
	if(InsType->getTypeID() == Type::DoubleTyID)
		return true;
	return false;
}

ConstantInt* FPSanitizer::GetInstId(Function *F, Instruction* I) {
  Module *M = F->getParent();
  MDNode* uniqueIdMDNode = I->getMetadata("fpsan_inst_id");
  if (uniqueIdMDNode == NULL) {
    return ConstantInt::get(Type::getInt64Ty(M->getContext()), 0);
//    exit(1);
  }

  Metadata* uniqueIdMetadata = uniqueIdMDNode->getOperand(0).get();
  ConstantAsMetadata* uniqueIdMD = dyn_cast<ConstantAsMetadata>(uniqueIdMetadata);
  Constant* uniqueIdConstant = uniqueIdMD->getValue();
  return dyn_cast<ConstantInt>(uniqueIdConstant);
}

void FPSanitizer::createGEP(Function *F, AllocaInst *Alloca, long TotalAlloca){
  Function::iterator Fit = F->begin();
  BasicBlock &BB = *Fit;
  Instruction *I = dyn_cast<Instruction>(Alloca);
  Module *M = F->getParent();
  Instruction *Next = getNextInstruction(I, &BB);

  IntegerType* Int32Ty = Type::getInt32Ty(M->getContext());
  IntegerType* Int1Ty = Type::getInt1Ty(M->getContext());

  ConstantInt* instId = GetInstId(F, I);
  const DebugLoc &instDebugLoc = I->getDebugLoc();
  bool debugInfoAvail = false;;
  unsigned int lineNum = 0;
  unsigned int colNum = 0;
  if (instDebugLoc) {
    debugInfoAvail = true;
    lineNum = instDebugLoc.getLine();
    colNum = instDebugLoc.getCol();
    if (lineNum == 0 && colNum == 0) debugInfoAvail = false;
  }

  ConstantInt* debugInfoAvailable = ConstantInt::get(Int1Ty, debugInfoAvail);
  ConstantInt* lineNumber = ConstantInt::get(Int32Ty, lineNum);
  ConstantInt* colNumber = ConstantInt::get(Int32Ty, colNum);

  IRBuilder<> IRB(Next);
  Instruction *End;
  for (auto &BB : *F) {
    for (auto &I : BB) {
      if (dyn_cast<ReturnInst>(&I)){
        End = &I;
      }
    }
  }
  IRBuilder<> IRBE(End);
  int index = 0;

  Type* VoidTy = Type::getVoidTy(M->getContext());
  for (auto &BB : *F) {
    for (auto &I : BB) {
      if (BinaryOperator* BO = dyn_cast<BinaryOperator>(&I)){
        switch(BO->getOpcode()) {                                                                                                
          case Instruction::FAdd:
          case Instruction::FSub:
          case Instruction::FMul:
          case Instruction::FDiv:
            {
              if(index-1 > TotalAlloca){
                errs()<<"Error\n\n\n: index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
              }
              Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
                ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
              Value *BOGEP = IRB.CreateGEP(Alloca, Indices);

              GEPMap.insert(std::pair<Instruction*, Value*>(&I, BOGEP));
              Value *Op1 = BO->getOperand(0);
              Value *Op2 = BO->getOperand(1);

              if(ENV == MPFR){ 
                FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
                IRB.CreateCall(FuncInit, {BOGEP});

                FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
                IRBE.CreateCall(FuncInit, {BOGEP});
              }
              index++;
              bool Op1Call = false;
              bool Op2Call = false;

              if(CallInst *CI = dyn_cast<CallInst>(Op1)){
                Function *Callee = CI->getCalledFunction();
                if (Callee) {
                  if(!isListedFunction(Callee->getName(), "mathFunc.txt")){
                    //this operand is function which is not defined, we consider this as a constant
                    Op1Call = true;
                  }
                }
              }
              if(CallInst *CI = dyn_cast<CallInst>(Op2)){
                Function *Callee = CI->getCalledFunction();
                if (Callee) {
                  if(!isListedFunction(Callee->getName(), "mathFunc.txt")){
                    Op2Call = true;
                  }
                }
              }
              if(isa<ConstantFP>(Op1) || Op1Call){
                if(index-1 > TotalAlloca){
                  errs()<<"Error\n\n\n: index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
                }
                Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
                  ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
                Value *BOGEP = IRB.CreateGEP(Alloca, Indices);
                GEPMap.insert(std::pair<Instruction*, Value*>(dyn_cast<Instruction>(Op1), BOGEP));

                if(ENV == MPFR){ 
                  FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
                  IRB.CreateCall(FuncInit, {BOGEP});
                }
                if(isFloat(Op1->getType())){
                  if(ENV == MPFR){ 
                    FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_dconst", VoidTy, MPtrTy, Op1->getType(), Int32Ty);
                  }
                  if(ENV == Posit32 || ENV == Double){
                    FuncInit = M->getOrInsertFunction("setP32ToF", VoidTy, MPtrTy, Op1->getType(), Int32Ty);
                  }
                }
                else if(isDouble(Op1->getType())){
                  if(ENV == MPFR){ 
                    FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_dconst", VoidTy, MPtrTy, Op1->getType(), Int32Ty);
                  }
                  if(ENV == Posit32 || ENV == Double){
                    FuncInit = M->getOrInsertFunction("setP32ToD", VoidTy, MPtrTy, Op1->getType(), Int32Ty);
                  }
                }
                if(Op1Call){
                  Instruction *Next = getNextInstruction(BO, &BB);
                  IRBuilder<> IRBI(Next);
                  IRBI.CreateCall(FuncInit, {BOGEP, Op1, lineNumber});
                }
                else
                  IRB.CreateCall(FuncInit, {BOGEP, Op1, lineNumber});
                ConsMap.insert(std::pair<Value*, Value*>(Op1, BOGEP));
                if(ENV == MPFR){ 
                  FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
                  IRBE.CreateCall(FuncInit, {BOGEP});
                }
                index++;
              }
              if(isa<ConstantFP>(Op2) || Op2Call){
                if(index-1 > TotalAlloca){
                  errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
                }
                Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
                  ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
                Value *BOGEP = IRB.CreateGEP(Alloca, Indices);
                GEPMap.insert(std::pair<Instruction*, Value*>(dyn_cast<Instruction>(Op2), BOGEP));

                if(ENV == MPFR){ 
                  FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
                  IRB.CreateCall(FuncInit, {BOGEP});
                }
                if(isFloat(Op2->getType())){
                  if(ENV == MPFR){
                    FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_fconst", VoidTy, MPtrTy, Op2->getType(), Int32Ty);
                  }
                  else if(ENV == Posit32 || ENV == Double){
                    FuncInit = M->getOrInsertFunction("setP32ToF", VoidTy, MPtrTy, Op2->getType(), Int32Ty);
                  }
                }
                else if(isDouble(Op2->getType())){
                  if(ENV == MPFR){
                    FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_dconst", VoidTy, MPtrTy, Op2->getType(), Int32Ty);
                  }
                  else if(ENV == Posit32 || ENV == Double){
                    FuncInit = M->getOrInsertFunction("setP32ToD", VoidTy, MPtrTy, Op2->getType(), Int32Ty);
                  }
                }
                if(Op2Call){
                  Instruction *Next = getNextInstruction(BO, &BB);
                  IRBuilder<> IRBI(Next);
                  IRBI.CreateCall(FuncInit, {BOGEP, Op2, lineNumber});
                }
                else
                  IRB.CreateCall(FuncInit, {BOGEP, Op2, lineNumber});
                ConsMap.insert(std::pair<Value*, Value*>(Op2, BOGEP));

                if(ENV == MPFR){ 
                  FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
                  IRBE.CreateCall(FuncInit, {BOGEP});
                }
                index++;
              }
            }
        }
      }
      else if (SIToFPInst *UI = dyn_cast<SIToFPInst>(&I)){
        Instruction *Next = getNextInstruction(UI, &BB);
        IRBuilder<> IRBI(Next);
        if(index-1 > TotalAlloca){
          errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
        }
        Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
          ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
        Value *BOGEP = IRB.CreateGEP(Alloca, Indices);
        GEPMap.insert(std::pair<Instruction*, Value*>(dyn_cast<Instruction>(UI), BOGEP));

        if(ENV == MPFR){ 
          FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
          IRB.CreateCall(FuncInit, {BOGEP});
        }
        if(isFloat(UI->getType())){
          if(ENV == MPFR){ 
            FuncInit = M->getOrInsertFunction("setMpfrF", VoidTy, MPtrTy, UI->getType(), Int32Ty);
          }
          if(ENV == Posit32 || ENV == Double){
            FuncInit = M->getOrInsertFunction("setP32ToF", VoidTy, MPtrTy, UI->getType(), Int32Ty);
          }
        }
        else if(isDouble(UI->getType())){
          if(ENV == MPFR){ 
            FuncInit = M->getOrInsertFunction("setMpfrD", VoidTy, MPtrTy, UI->getType(), Int32Ty);
          }
          if(ENV == Posit32 || ENV == Double){
            FuncInit = M->getOrInsertFunction("setP32ToD", VoidTy, MPtrTy, UI->getType(), Int32Ty);
          }
        }
        IRBI.CreateCall(FuncInit, {BOGEP, UI, lineNumber});
        ConsMap.insert(std::pair<Value*, Value*>(UI, BOGEP));
        if(ENV == MPFR){ 
          FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
          IRBE.CreateCall(FuncInit, {BOGEP});
        }
        index++;
      }
      else if (BitCastInst *UI = dyn_cast<BitCastInst>(&I)){
        if(isFloatType(UI->getType())){
          Instruction *Next = getNextInstruction(UI, &BB);
          IRBuilder<> IRBI(Next);
          if(index-1 > TotalAlloca){
            errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
          }
          Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
            ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
          Value *BOGEP = IRB.CreateGEP(Alloca, Indices);
          GEPMap.insert(std::pair<Instruction*, Value*>(dyn_cast<Instruction>(UI), BOGEP));

          if(ENV == MPFR){ 
            FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
            IRB.CreateCall(FuncInit, {BOGEP});
          }
          if(isFloat(UI->getType())){
            if(ENV == MPFR){ 
              FuncInit = M->getOrInsertFunction("setMpfrF", VoidTy, MPtrTy, UI->getType(), Int32Ty);
            }
            if(ENV == Posit32 || ENV == Double){
              FuncInit = M->getOrInsertFunction("setP32ToF", VoidTy, MPtrTy, UI->getType(), Int32Ty);
            }
          }
          else if(isDouble(UI->getType())){
            if(ENV == MPFR){ 
              FuncInit = M->getOrInsertFunction("setMpfrD", VoidTy, MPtrTy, UI->getType(), Int32Ty);
            }
            if(ENV == Posit32 || ENV == Double){
              FuncInit = M->getOrInsertFunction("setP32ToD", VoidTy, MPtrTy, UI->getType(), Int32Ty);
            }
          }
          IRBI.CreateCall(FuncInit, {BOGEP, UI, lineNumber});
          ConsMap.insert(std::pair<Value*, Value*>(UI, BOGEP));
          if(ENV == MPFR){ 
            FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
            IRBE.CreateCall(FuncInit, {BOGEP});
          }
          index++;
        }
      }
      else if (UIToFPInst *UI = dyn_cast<UIToFPInst>(&I)){
        Instruction *Next = getNextInstruction(UI, &BB);
        IRBuilder<> IRBI(Next);
        if(index-1 > TotalAlloca){
          errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
        }
        Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
          ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
        Value *BOGEP = IRB.CreateGEP(Alloca, Indices);
        GEPMap.insert(std::pair<Instruction*, Value*>(dyn_cast<Instruction>(UI), BOGEP));

        if(ENV == MPFR){ 
          FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
          IRB.CreateCall(FuncInit, {BOGEP});
        }
        if(isFloat(UI->getType())){
          if(ENV == MPFR){ 
            FuncInit = M->getOrInsertFunction("setMpfrF", VoidTy, MPtrTy, UI->getType(), Int32Ty);
          }
          if(ENV == Posit32 || ENV == Double){
            FuncInit = M->getOrInsertFunction("setP32ToF", VoidTy, MPtrTy, UI->getType(), Int32Ty);
          }
        }
        else if(isDouble(UI->getType())){
          if(ENV == MPFR){ 
            FuncInit = M->getOrInsertFunction("setMpfrD", VoidTy, MPtrTy, UI->getType(), Int32Ty);
          }
          if(ENV == Posit32 || ENV == Double){
            FuncInit = M->getOrInsertFunction("setP32ToD", VoidTy, MPtrTy, UI->getType(), Int32Ty);
          }
        }
        IRBI.CreateCall(FuncInit, {BOGEP, UI, lineNumber});
        ConsMap.insert(std::pair<Value*, Value*>(UI, BOGEP));
        if(ENV == MPFR){ 
          FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
          IRBE.CreateCall(FuncInit, {BOGEP});
        }
        index++;
      }
      else if (FCmpInst *FCI = dyn_cast<FCmpInst>(&I)){
        Value *Op1 = FCI->getOperand(0);
        Value *Op2 = FCI->getOperand(1);

        bool Op1Call = false;
        bool Op2Call = false;

        if(CallInst *CI = dyn_cast<CallInst>(Op1)){
          Function *Callee = CI->getCalledFunction();
          if (Callee) {
            if(!isListedFunction(Callee->getName(), "mathFunc.txt")){
              //this operand is function which is not defined, we consider this as a constant
              Op1Call = true;
            }
          }
        }
        if(CallInst *CI = dyn_cast<CallInst>(Op2)){
          Function *Callee = CI->getCalledFunction();
          if (Callee) {
            if(!isListedFunction(Callee->getName(), "mathFunc.txt")){
              Op2Call = true;
            }
          }
        }

        if(isa<ConstantFP>(Op1) || Op1Call){
          if(index-1 > TotalAlloca){
            errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
          }
          Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
            ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
          Value *BOGEP = IRB.CreateGEP(Alloca, Indices);
          GEPMap.insert(std::pair<Instruction*, Value*>(dyn_cast<Instruction>(Op1), BOGEP));

          if(ENV == MPFR){ 
            FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
            IRB.CreateCall(FuncInit, {BOGEP});
          }
          if(isFloat(Op1->getType())){
            if(ENV == MPFR){ 
              FuncInit = M->getOrInsertFunction("setMpfrF", VoidTy, MPtrTy, Op1->getType(), Int32Ty);
            }
            if(ENV == Posit32 || ENV == Double){
              FuncInit = M->getOrInsertFunction("setP32ToF", VoidTy, MPtrTy, Op1->getType(), Int32Ty);
            }
          }
          else if(isDouble(Op1->getType())){
            if(ENV == MPFR){ 
              FuncInit = M->getOrInsertFunction("setMpfrD", VoidTy, MPtrTy, Op1->getType(), Int32Ty);
            }
            if(ENV == Posit32 || ENV == Double){
              FuncInit = M->getOrInsertFunction("setP32ToD", VoidTy, MPtrTy, Op1->getType(), Int32Ty);
            }
          }
          if(Op1Call){
            Instruction *Next = getNextInstruction(FCI, &BB);
            IRBuilder<> IRBI(Next);
            IRBI.CreateCall(FuncInit, {BOGEP, Op1, lineNumber});
          }
          else
            IRB.CreateCall(FuncInit, {BOGEP, Op1, lineNumber});
          ConsMap.insert(std::pair<Value*, Value*>(Op1, BOGEP));
          if(ENV == MPFR){ 
            FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
            IRBE.CreateCall(FuncInit, {BOGEP});
          }
          index++;
        }
        if(isa<ConstantFP>(Op2) || Op2Call){
          if(index-1 > TotalAlloca){
            errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
          }
          Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
            ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
          Value *BOGEP = IRB.CreateGEP(Alloca, Indices);
          GEPMap.insert(std::pair<Instruction*, Value*>(dyn_cast<Instruction>(Op2), BOGEP));

          if(ENV == MPFR){ 
            FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
            IRB.CreateCall(FuncInit, {BOGEP});
          }
          if(isFloat(Op2->getType())){
            if(ENV == MPFR){
              FuncInit = M->getOrInsertFunction("setMpfrF", VoidTy, MPtrTy, Op2->getType(), Int32Ty);
            }
            else if(ENV == Posit32 || ENV == Double){
              FuncInit = M->getOrInsertFunction("setP32ToF", VoidTy, MPtrTy, Op2->getType(), Int32Ty);
            }
          }
          else if(isDouble(Op2->getType())){
            if(ENV == MPFR){
              FuncInit = M->getOrInsertFunction("setMpfrD", VoidTy, MPtrTy, Op2->getType(), Int32Ty);
            }
            else if(ENV == Posit32 || ENV == Double){
              FuncInit = M->getOrInsertFunction("setP32ToD", VoidTy, MPtrTy, Op2->getType(), Int32Ty);
            }
          }
          if(Op2Call){
            Instruction *Next = getNextInstruction(FCI, &BB);
            IRBuilder<> IRBI(Next);
            IRBI.CreateCall(FuncInit, {BOGEP, Op2, lineNumber});
          }
          else
            IRB.CreateCall(FuncInit, {BOGEP, Op2, lineNumber});
          ConsMap.insert(std::pair<Value*, Value*>(Op2, BOGEP));

          if(ENV == MPFR){ 
            FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
            IRBE.CreateCall(FuncInit, {BOGEP});
          }
          index++;
        }
      }
      else if (LoadInst *LI = dyn_cast<LoadInst>(&I)){
        if(isFloatType(LI->getType())){
          if(index-1 > TotalAlloca){
            errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
          }
          Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
            ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
          Value *BOGEP = IRB.CreateGEP(Alloca, Indices);
          GEPMap.insert(std::pair<Instruction*, Value*>(&I, BOGEP));

          if(ENV == MPFR){ 
            GEPMap.insert(std::pair<Instruction*, Value*>(&I, BOGEP));
            FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
            IRB.CreateCall(FuncInit, {BOGEP});

            FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
            IRBE.CreateCall(FuncInit, {BOGEP});
          } 
          index++;
        }
      }
      else if (CallInst *CI = dyn_cast<CallInst>(&I)){
        Function *Callee = CI->getCalledFunction();
        if (Callee) {
          if(isListedFunction(Callee->getName(), "mathFunc.txt")){
            if(index-1 > TotalAlloca){
              errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
            }
            Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
              ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
            Value *BOGEP = IRB.CreateGEP(Alloca, Indices);
            GEPMap.insert(std::pair<Instruction*, Value*>(&I, BOGEP));

            if(ENV == MPFR){ 
              GEPMap.insert(std::pair<Instruction*, Value*>(&I, BOGEP));
              FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
              IRB.CreateCall(FuncInit, {BOGEP});

              FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
              IRBE.CreateCall(FuncInit, {BOGEP});
            }
            index++;
          }
          else if(isListedFunction(Callee->getName(), "functions.txt")){
            if(index-1 > TotalAlloca){
              errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
            }
            Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
              ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
            Value *BOGEP = IRB.CreateGEP(Alloca, Indices);

            GEPMap.insert(std::pair<Instruction*, Value*>(&I, BOGEP));

            if(ENV == MPFR){ 
              FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
              IRB.CreateCall(FuncInit, {BOGEP});

              FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
              IRBE.CreateCall(FuncInit, {BOGEP});
            }
            index++;
          }

          if(isListedFunction(Callee->getName(), "mathFunc.txt") || 
              isListedFunction(Callee->getName(), "functions.txt")){
            size_t NumOperands = CI->getNumArgOperands();
            Value *Op[NumOperands];
            Type *OpTy[NumOperands];
            bool Op1Call[NumOperands];
            for(int i = 0; i < NumOperands; i++){
              Op[i] = CI->getArgOperand(i);
              OpTy[i] = Op[i]->getType(); // this should be of float
              Op1Call[i] = false;

              //handle function call which take as operand another function call,
              //but that function is not defined. It then should be treated a constant.
              if(isListedFunction(Callee->getName(), "mathFunc.txt")){
                if(CallInst *CI = dyn_cast<CallInst>(Op[i])){
                  Function *Callee = CI->getCalledFunction();
                  if (Callee) {
                    if(!isListedFunction(Callee->getName(), "mathFunc.txt")){
                      if(!isListedFunction(Callee->getName(), "functions.txt") ){
                      //this operand is function which is not defined, we consider this as a constant
                        Op1Call[i] = true;
                      }
                    }
                  }
                }
              }
              if(isa<ConstantFP>(Op[i]) || Op1Call[i]){
                if(index-1 > TotalAlloca){
                  errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
                }
                Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
                  ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
                Value *BOGEP = IRB.CreateGEP(Alloca, Indices);

                GEPMap.insert(std::pair<Instruction*, Value*>(dyn_cast<Instruction>(Op[i]), BOGEP));

                if(ENV == MPFR){ 
                  FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
                  IRB.CreateCall(FuncInit, {BOGEP});
                }
                if(isFloat(Op[i]->getType())){
                  if(ENV == MPFR){
                    FuncInit = M->getOrInsertFunction("setMpfrF", VoidTy, MPtrTy, OpTy[i], Int32Ty);
                  }
                  else if(ENV == Posit32 || ENV == Double){
                    FuncInit = M->getOrInsertFunction("setP32ToF", VoidTy, MPtrTy, OpTy[i], Int32Ty);
                  }
                }
                else if(isDouble(Op[i]->getType())){
                  if(ENV == MPFR){
                    FuncInit = M->getOrInsertFunction("setMpfrD", VoidTy, MPtrTy, OpTy[i], Int32Ty);
                  }
                  else if(ENV == Posit32 || ENV == Double){
                    FuncInit = M->getOrInsertFunction("setP32ToD", VoidTy, MPtrTy, OpTy[i], Int32Ty);
                  }
                }

                if(Op1Call[i]){
                  Instruction *Next = getNextInstruction(BO, &BB);
                  IRBuilder<> IRBI(Next);
                  IRBI.CreateCall(FuncInit, {BOGEP, Op[i], lineNumber});
                }
                else
                  IRB.CreateCall(FuncInit, {BOGEP, Op[i], lineNumber});
                ConsMap.insert(std::pair<Value*, Value*>(Op[i], BOGEP));

                if(ENV == MPFR){ 
                  FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
                  IRBE.CreateCall(FuncInit, {BOGEP});
                }
                index++;
              }
            }
          }

        }
      }
      else if (FPTruncInst* FP = dyn_cast<FPTruncInst>(&I)){
        if(index-1 > TotalAlloca){
          errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
        }
        Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
          ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
        Value *BOGEP = IRB.CreateGEP(Alloca, Indices);
        GEPMap.insert(std::pair<Instruction*, Value*>(dyn_cast<Instruction>(FP), BOGEP));

        if(ENV == MPFR){ 
          FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
          IRB.CreateCall(FuncInit, {BOGEP});
        }
        Instruction *Next = getNextInstruction(FP, &BB);
        IRBuilder<> IRBB(Next);
        if(isFloat(FP->getType())){
          if(ENV == MPFR){
            FuncInit = M->getOrInsertFunction("setMpfrF", VoidTy, MPtrTy, FP->getType(), Int32Ty);
          }
          else if(ENV == Posit32 || ENV == Double){
            FuncInit = M->getOrInsertFunction("setP32ToF", VoidTy, MPtrTy, FP->getType(), Int32Ty);
          }
        }
        else if(isDouble(FP->getType())){
          if(ENV == MPFR){
            FuncInit = M->getOrInsertFunction("setMpfrD", VoidTy, MPtrTy, FP->getType(), Int32Ty);
          }
          else if(ENV == Posit32 || ENV == Double){
            FuncInit = M->getOrInsertFunction("setP32ToD", VoidTy, MPtrTy, FP->getType(), Int32Ty);
          }
        }
        IRBB.CreateCall(FuncInit, {BOGEP, FP, lineNumber});
        ConsMap.insert(std::pair<Value*, Value*>(FP, BOGEP));

        if(ENV == MPFR){ 
          FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
          IRBE.CreateCall(FuncInit, {BOGEP});
        }
        index++;
      }
      else if (FCmpInst *FCI = dyn_cast<FCmpInst>(&I)){
        if(isFloatType(I.getType())){
          if(isa<ConstantFP>((FCI->getOperand(0)))){
            if(index-1 > TotalAlloca){
              errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
            }
            Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
              ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
            Value *BOGEP = IRB.CreateGEP(Alloca, Indices);

            GEPMap.insert(std::pair<Instruction*, Value*>(dyn_cast<Instruction>(FCI->getOperand(0)), BOGEP));

            if(ENV == MPFR){ 
              FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
              IRB.CreateCall(FuncInit, {BOGEP});
            }
            if(isFloat(FCI->getOperand(0)->getType())){
              if(ENV == MPFR){
                FuncInit = M->getOrInsertFunction("setMpfrF", VoidTy, MPtrTy, FCI->getOperand(0)->getType(), Int32Ty);
              }
              else if(ENV == Posit32 || ENV == Double){
                FuncInit = M->getOrInsertFunction("setP32ToF", VoidTy, MPtrTy, FCI->getOperand(0)->getType(), Int32Ty);
              }
            }
            else if(isDouble(FCI->getOperand(0)->getType())){
              if(ENV == MPFR){
                FuncInit = M->getOrInsertFunction("setMpfrD", VoidTy, MPtrTy, FCI->getOperand(0)->getType(), Int32Ty);
              }
              else if(ENV == Posit32 || ENV == Double){
                FuncInit = M->getOrInsertFunction("setP32ToD", VoidTy, MPtrTy, FCI->getOperand(0)->getType(), Int32Ty);
              }
            }
            IRB.CreateCall(FuncInit, {BOGEP, FCI->getOperand(0), lineNumber});
            ConsMap.insert(std::pair<Value*, Value*>(FCI->getOperand(0), BOGEP));

            if(ENV == MPFR){ 
              FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
              IRBE.CreateCall(FuncInit, {BOGEP});
            }
            index++;
          }
          if(isa<ConstantFP>((FCI->getOperand(1)))){
            if(index-1 > TotalAlloca){
              errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
            }
            Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
              ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
            Value *BOGEP = IRB.CreateGEP(Alloca, Indices);

            GEPMap.insert(std::pair<Instruction*, Value*>(dyn_cast<Instruction>(FCI->getOperand(1)), BOGEP));

            if(ENV == MPFR){ 
              FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
              IRB.CreateCall(FuncInit, {BOGEP});
            }
            if(isFloat(FCI->getOperand(1)->getType())){
              if(ENV == MPFR){
                FuncInit = M->getOrInsertFunction("setMpfrF", VoidTy, MPtrTy, FCI->getOperand(1)->getType(), Int32Ty);
              }
              else if(ENV == Posit32 || ENV == Double){
                FuncInit = M->getOrInsertFunction("setP32ToF", VoidTy, MPtrTy, FCI->getOperand(1)->getType(), Int32Ty);
              }
            }
            else if(isDouble(FCI->getOperand(1)->getType())){
              if(ENV == MPFR){
                FuncInit = M->getOrInsertFunction("setMpfrD", VoidTy, MPtrTy, FCI->getOperand(1)->getType(), Int32Ty);
              }
              else if(ENV == Posit32 || ENV == Double){
                FuncInit = M->getOrInsertFunction("setP32ToD", VoidTy, MPtrTy, FCI->getOperand(1)->getType(), Int32Ty);
              }
            }
            IRB.CreateCall(FuncInit, {BOGEP, FCI->getOperand(1), lineNumber});
            ConsMap.insert(std::pair<Value*, Value*>(FCI->getOperand(1), BOGEP));

            if(ENV == MPFR){ 
              FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
              IRBE.CreateCall(FuncInit, {BOGEP});
            }
            index++;
          }
        }
      }
      else if (SelectInst *SI = dyn_cast<SelectInst>(&I)){
        if(isFloatType(I.getType())){
          if(isa<ConstantFP>((SI->getOperand(1)))){
            if(index-1 > TotalAlloca){
              errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
            }
            Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
              ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
            Value *BOGEP = IRB.CreateGEP(Alloca, Indices);

            GEPMap.insert(std::pair<Instruction*, Value*>(dyn_cast<Instruction>(SI->getOperand(1)), BOGEP));

            if(ENV == MPFR){ 
              FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
              IRB.CreateCall(FuncInit, {BOGEP});
            }
            if(isFloat(SI->getOperand(1)->getType())){
              if(ENV == MPFR){
                FuncInit = M->getOrInsertFunction("setMpfrF", VoidTy, MPtrTy, SI->getOperand(1)->getType(), Int32Ty);
              }
              else if(ENV == Posit32 || ENV == Double){
                FuncInit = M->getOrInsertFunction("setP32ToF", VoidTy, MPtrTy, SI->getOperand(1)->getType(), Int32Ty);
              }
            }
            else if(isDouble(SI->getOperand(1)->getType())){
              if(ENV == MPFR){
                FuncInit = M->getOrInsertFunction("setMpfrD", VoidTy, MPtrTy, SI->getOperand(1)->getType(), Int32Ty);
              }
              else if(ENV == Posit32 || ENV == Double){
                FuncInit = M->getOrInsertFunction("setP32ToD", VoidTy, MPtrTy, SI->getOperand(1)->getType(), Int32Ty);
              }
            }
            IRB.CreateCall(FuncInit, {BOGEP, SI->getOperand(1), lineNumber});
            ConsMap.insert(std::pair<Value*, Value*>(SI->getOperand(1), BOGEP));

            if(ENV == MPFR){ 
              FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
              IRBE.CreateCall(FuncInit, {BOGEP});
            }
            index++;
          }
          if(isa<ConstantFP>((SI->getOperand(2)))){
            if(index-1 > TotalAlloca){
              errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
            }
            Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
              ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
            Value *BOGEP = IRB.CreateGEP(Alloca, Indices);

            GEPMap.insert(std::pair<Instruction*, Value*>(dyn_cast<Instruction>(SI->getOperand(2)), BOGEP));

            if(ENV == MPFR){ 
              FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
              IRB.CreateCall(FuncInit, {BOGEP});
            }
            if(isFloat(SI->getOperand(2)->getType())){
              if(ENV == MPFR){
                FuncInit = M->getOrInsertFunction("setMpfrF", VoidTy, MPtrTy, SI->getOperand(2)->getType(), Int32Ty);
              }
              else if(ENV == Posit32 || ENV == Double){
                FuncInit = M->getOrInsertFunction("setP32ToF", VoidTy, MPtrTy, SI->getOperand(2)->getType(), Int32Ty);
              }
            }
            else if(isDouble(SI->getOperand(2)->getType())){
              if(ENV == MPFR){
                FuncInit = M->getOrInsertFunction("setMpfrD", VoidTy, MPtrTy, SI->getOperand(2)->getType(), Int32Ty);
              }
              else if(ENV == Posit32 || ENV == Double){
                FuncInit = M->getOrInsertFunction("setP32ToD", VoidTy, MPtrTy, SI->getOperand(2)->getType(), Int32Ty);
              }
            }
            IRB.CreateCall(FuncInit, {BOGEP, SI->getOperand(2), lineNumber});
            ConsMap.insert(std::pair<Value*, Value*>(SI->getOperand(2), BOGEP));

            if(ENV == MPFR){ 
              FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
              IRBE.CreateCall(FuncInit, {BOGEP});
            }
            index++;
          }
        }
      }
      else if(PHINode *PN = dyn_cast<PHINode>(&I)){
        if(isFloatType(I.getType())){
          if(index-1 > TotalAlloca){
            errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
          }

          Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
            ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
          Value *BOGEP = IRB.CreateGEP(Alloca, Indices);

          GEPMap.insert(std::pair<Instruction*, Value*>(&I, BOGEP));

          if(ENV == MPFR){ 
            FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
            IRB.CreateCall(FuncInit, {BOGEP});

            FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
            IRBE.CreateCall(FuncInit, {BOGEP});
          }
          index++;
        }
        for (unsigned PI = 0, PE = PN->getNumIncomingValues(); PI != PE; ++PI) {
          Value *IncValue = PN->getIncomingValue(PI);

          if (IncValue == PN) continue; //TODO
          if(isa<ConstantFP>(IncValue))
            if(isa<ConstantFP>(IncValue)){
              if(index-1 > TotalAlloca){
                errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
              }
              Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
                ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
              Value *BOGEP = IRB.CreateGEP(Alloca, Indices);

              GEPMap.insert(std::pair<Instruction*, Value*>(dyn_cast<Instruction>(IncValue), BOGEP));

              if(ENV == MPFR){ 
                FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
                IRB.CreateCall(FuncInit, {BOGEP});
              }
              if(isFloat(IncValue->getType())){
                if(ENV == MPFR){
                  FuncInit = M->getOrInsertFunction("setMpfrF", VoidTy, MPtrTy, IncValue->getType(), Int32Ty);
                }
                else if(ENV == Posit32 || ENV == Double){
                  FuncInit = M->getOrInsertFunction("setP32ToF", VoidTy, MPtrTy, IncValue->getType(), Int32Ty);
                }
              }
              else if(isDouble(IncValue->getType())){
                if(ENV == MPFR){
                  FuncInit = M->getOrInsertFunction("setMpfrD", VoidTy, MPtrTy, IncValue->getType(), Int32Ty);
                }
                else if(ENV == Posit32 || ENV == Double){
                  FuncInit = M->getOrInsertFunction("setP32ToD", VoidTy, MPtrTy, IncValue->getType(), Int32Ty);
                }
              }
              IRB.CreateCall(FuncInit, {BOGEP, IncValue, lineNumber});
              ConsMap.insert(std::pair<Value*, Value*>(IncValue, BOGEP));

              if(ENV == MPFR){ 
                FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
                IRBE.CreateCall(FuncInit, {BOGEP});
              }
              index++;
            }
        }
      }
    }
  }
}

AllocaInst * FPSanitizer::createAlloca(Function *F, size_t InsCount){
  Function::iterator Fit = F->begin();
  BasicBlock &BB = *Fit; 
  BasicBlock::iterator BBit = BB.begin();
  Instruction *First = &*BBit;
  IRBuilder<> IRB(First);
  Module *M = F->getParent();

  Instruction *End;
  for (auto &BB : *F) {	
    for (auto &I : BB) {
      if (dyn_cast<ReturnInst>(&I)){
        End = &I;
      }
    }
  }
  IRBuilder<> IRBE(End);

  AllocaInst *Alloca = IRB.CreateAlloca(ArrayType::get(Real, InsCount),
      nullptr);
  return Alloca;
}

void FPSanitizer::callGetArgument(Function *F){
  for (Function::arg_iterator ait = F->arg_begin(), aend = F->arg_end();
      ait != aend; ++ait) {
    Argument *A = &*ait;
    if(isFloatType(A->getType())){
      Function::iterator Fit = F->begin();
      BasicBlock &BB = *Fit; 
      BasicBlock::iterator BBit = BB.begin();
      Instruction *First = &*BBit;
      IRBuilder<> IRB(First);
      Module *M = F->getParent();
      Type* Int64Ty = Type::getInt64Ty(M->getContext());
      size_t Idx =  ArgMap.at(A);
      long TotalArgs = FuncTotalArg.at(F);
      Constant* ArgNo = ConstantInt::get(Type::getInt64Ty(M->getContext()), TotalArgs-Idx);
      SetRealTemp = M->getOrInsertFunction("fpsan_get_arg", MPtrTy, Int64Ty, A->getType());
      Value* ConsInsIndex = IRB.CreateCall(SetRealTemp, {ArgNo, A});
      MArgMap.insert(std::pair<Argument*, Instruction*>(A, dyn_cast<Instruction>(ConsInsIndex)));
    }
  }
}

void FPSanitizer::createMpfrAlloca(Function *F){
  long TotalArg = 1;
  long TotalAlloca = 0;
  for (Function::arg_iterator ait = F->arg_begin(), aend = F->arg_end();
      ait != aend; ++ait) {
    Argument *A = &*ait;
    ArgMap.insert(std::pair<Argument*, long>(A, TotalArg));
    TotalArg++;
  }

  FuncTotalArg.insert(std::pair<Function*, long>(F, TotalArg));
  TotalArg = 1;
  for (auto &BB : *F) {
    for (auto &I : BB) {
      if (BinaryOperator* BO = dyn_cast<BinaryOperator>(&I)){
        switch(BO->getOpcode()) {                                                                                                
          case Instruction::FAdd:
          case Instruction::FSub:
          case Instruction::FMul:
          case Instruction::FDiv:
            {
              Value *Op1 = BO->getOperand(0);
              Value *Op2 = BO->getOperand(1);

              TotalAlloca++;
              bool Op1Call = false;
              bool Op2Call = false;

              if(CallInst *CI = dyn_cast<CallInst>(Op1)){
                Function *Callee = CI->getCalledFunction();
                if (Callee) {
                  if(!isListedFunction(Callee->getName(), "mathFunc.txt")){
                    //this operand is function which is not defined, we consider this as a constant
                    Op1Call = true;
                  }
                }
              }
              if(CallInst *CI = dyn_cast<CallInst>(Op2)){
                Function *Callee = CI->getCalledFunction();
                if (Callee) {
                  if(!isListedFunction(Callee->getName(), "mathFunc.txt")){
                    Op2Call = true;
                  }
                }
              }
              if(isa<ConstantFP>(Op1) || Op1Call){
                TotalAlloca++;
              }
              if(isa<ConstantFP>(Op2) || Op2Call){
                TotalAlloca++;
              }
            }
        }
      }
      else if (SIToFPInst *UI = dyn_cast<SIToFPInst>(&I)){
        TotalAlloca++;
      }
      else if (BitCastInst *UI = dyn_cast<BitCastInst>(&I)){
        if(isFloatType(UI->getType())){
          TotalAlloca++;
        }
      }
      else if (UIToFPInst *UI = dyn_cast<UIToFPInst>(&I)){
        TotalAlloca++;
      }
      else if (FCmpInst *FCI = dyn_cast<FCmpInst>(&I)){
        Value *Op1 = FCI->getOperand(0);
        Value *Op2 = FCI->getOperand(1);

        bool Op1Call = false;
        bool Op2Call = false;

        if(CallInst *CI = dyn_cast<CallInst>(Op1)){
          Function *Callee = CI->getCalledFunction();
          if (Callee) {
            if(!isListedFunction(Callee->getName(), "mathFunc.txt")){
              //this operand is function which is not defined, we consider this as a constant
              Op1Call = true;
            }
          }
        }
        if(CallInst *CI = dyn_cast<CallInst>(Op2)){
          Function *Callee = CI->getCalledFunction();
          if (Callee) {
            if(!isListedFunction(Callee->getName(), "mathFunc.txt")){
              Op2Call = true;
            }
          }
        }

        if(isa<ConstantFP>(Op1) || Op1Call){
          TotalAlloca++;
        }
        if(isa<ConstantFP>(Op2) || Op2Call){
          TotalAlloca++;
        }
      }
      else if (LoadInst *LI = dyn_cast<LoadInst>(&I)){
        if(isFloatType(LI->getType())){
          TotalAlloca++;
        }
      }
      else if (CallInst *CI = dyn_cast<CallInst>(&I)){
        Function *Callee = CI->getCalledFunction();
        if (Callee) {
          if(isListedFunction(Callee->getName(), "mathFunc.txt")){
            TotalAlloca++;
          }
          else if(isListedFunction(Callee->getName(), "functions.txt")){
            TotalAlloca++;
          }

          if(isListedFunction(Callee->getName(), "mathFunc.txt") || 
              isListedFunction(Callee->getName(), "functions.txt")){
            size_t NumOperands = CI->getNumArgOperands();
            Value *Op[NumOperands];
            Type *OpTy[NumOperands];
            bool Op1Call[NumOperands];
            for(int i = 0; i < NumOperands; i++){
              Op[i] = CI->getArgOperand(i);
              OpTy[i] = Op[i]->getType(); // this should be of float
              Op1Call[i] = false;

              if(isListedFunction(Callee->getName(), "mathFunc.txt")){
                if(CallInst *CI = dyn_cast<CallInst>(Op[i])){
                  Function *Callee = CI->getCalledFunction();
                  if (Callee) {
                    if(!isListedFunction(Callee->getName(), "mathFunc.txt")){
                      //this operand is function which is not defined, we consider this as a constant
                      Op1Call[i] = true;
                    }
                  }
                }
              }
              if(isa<ConstantFP>(Op[i]) || Op1Call[i]){
                TotalAlloca++;
              }
            }
          }

        }
      }
      else if (FPTruncInst* FP = dyn_cast<FPTruncInst>(&I)){
        TotalAlloca++;
      }
      else if (FCmpInst *FCI = dyn_cast<FCmpInst>(&I)){
        if(isFloatType(I.getType())){
          if(isa<ConstantFP>((FCI->getOperand(0)))){
            TotalAlloca++;
          }
          if(isa<ConstantFP>((FCI->getOperand(1)))){
            TotalAlloca++;
          }
        }
      }
      else if (SelectInst *SI = dyn_cast<SelectInst>(&I)){
        if(isFloatType(I.getType())){
          if(isa<ConstantFP>((SI->getOperand(1)))){
            TotalAlloca++;
          }
          if(isa<ConstantFP>((SI->getOperand(2)))){
            TotalAlloca++;
          }
        }
      }
      else if(PHINode *PN = dyn_cast<PHINode>(&I)){
        if(isFloatType(I.getType())){
          TotalAlloca++;
        }
        for (unsigned PI = 0, PE = PN->getNumIncomingValues(); PI != PE; ++PI) {
          Value *IncValue = PN->getIncomingValue(PI);

          if (IncValue == PN) continue; //TODO
          if(isa<ConstantFP>(IncValue))
            if(isa<ConstantFP>(IncValue)){
              TotalAlloca++;
            }
        }
      }
    }
  }
  AllocaInst *Alloca = createAlloca(F, TotalAlloca);
  createGEP(F, Alloca, TotalAlloca);
  TotalAlloca = 0;
}


Instruction*
FPSanitizer::getNextInstruction(Instruction *I, BasicBlock *BB){
  Instruction *Next;
  for (BasicBlock::iterator BBit = BB->begin(), BBend = BB->end();
       BBit != BBend; ++BBit) {
    Next = &*BBit;
    if(I == Next){
      Next = &*(++BBit);
      break;
    }
  }
  return Next;
}

Instruction*
FPSanitizer::getNextInstructionNotPhi(Instruction *I, BasicBlock *BB){
  Instruction *Next;
    for (auto &I : *BB) {
      if(!isa<PHINode>(I)){
        Next = &I;
        break;
      }
  }
  return Next;
}

void FPSanitizer::findInterestingFunctions(Function *F){

  bool flag = false;
  
  for (auto &BB : *F) {
    for (auto &I : BB) {
      if (LoadInst *LI = dyn_cast<LoadInst>(&I)){
        if(isFloatType(I.getType())){
	        flag = true;
        }
      }
      else if (StoreInst *SI = dyn_cast<StoreInst>(&I)){
        if(isFloatType(I.getOperand(0)->getType())){
	        flag = true;
        }
      }
      else if (BinaryOperator* BO = dyn_cast<BinaryOperator>(&I)){
        switch(BO->getOpcode()) {
	        case Instruction::FAdd:
	        case Instruction::FSub:
	        case Instruction::FMul:
	        case Instruction::FDiv:{
	          flag = true;
	        }
	        case Instruction::Add:
	        case Instruction::Sub:
	        case Instruction::Mul:
	        case Instruction::UDiv:
	        case Instruction::SDiv:
	        case Instruction::URem:
	        case Instruction::SRem:
	        case Instruction::FRem:
	        case Instruction::Shl:
	        case Instruction::LShr:
	        case Instruction::AShr:
	        case Instruction::And:
	        case Instruction::Or:
	        case Instruction::Xor:
	        case Instruction::BinaryOpsEnd:{}
	      }
      } 
      else if (CallInst *CI = dyn_cast<CallInst>(&I)){
	      Function *Callee = CI->getCalledFunction();
	      if (Callee) {
	        std::string name = Callee->getName();
	        if(isListedFunction(Callee->getName(), "mathFunc.txt"))
	          flag = true;
	      }
      }
    }
  }
  if(flag){
    std::string name = F->getName();
    addFunctionsToList(name);
  }
}

void FPSanitizer::handleFuncMainInit(Function *F){
  Function::iterator Fit = F->begin();
  BasicBlock &BB = *Fit; 
  BasicBlock::iterator BBit = BB.begin();                                                                                                                                   
  Instruction *First = &*BBit;

  Module *M = F->getParent();
  IRBuilder<> IRB(First);

  Type* VoidTy = Type::getVoidTy(M->getContext());
  Type* Int64Ty = Type::getInt64Ty(M->getContext());

  Constant* Prec = ConstantInt::get(Type::getInt64Ty(M->getContext()), Precision);
  Finish = M->getOrInsertFunction("fpsan_init", VoidTy, Int64Ty);
  long TotIns = 0;

  IRB.CreateCall(Finish, {Prec});
}

void FPSanitizer::handleMainRet(Instruction *I, Function *F){ 
  Module *M = F->getParent();
  IRBuilder<> IRB(I);
  Type* VoidTy = Type::getVoidTy(M->getContext());
  Finish = M->getOrInsertFunction("fpsan_finish", VoidTy);
  IRB.CreateCall(Finish, {});
}


void
FPSanitizer::handleCallInst (CallInst *CI,
				BasicBlock *BB,
				Function *F,
				std::string CallName) {

  Instruction *I = dyn_cast<Instruction>(CI);
  Module *M = F->getParent();
  Instruction *Next = getNextInstruction(I, BB);
  IRBuilder<> IRBN(Next);
  IRBuilder<> IRB(I);

  Function *Callee = CI->getCalledFunction();

  Type* Int64Ty = Type::getInt64Ty(M->getContext());
  Type* VoidTy = Type::getVoidTy(M->getContext());

  if(isFloatType(Callee->getReturnType())){
    long InsIndex;
    Value *BOGEP = GEPMap.at(CI);
    FuncInit = M->getOrInsertFunction("fpsan_get_return", VoidTy, MPtrTy);
    IRBN.CreateCall(FuncInit, {BOGEP});
    MInsMap.insert(std::pair<Instruction*, Instruction*>(I, dyn_cast<Instruction>(BOGEP)));
  } 

  size_t NumOperands = CI->getNumArgOperands();
  Value *Op[NumOperands];
  Type *OpTy[NumOperands];
  for(int i = 0; i < NumOperands; i++){
    Op[i] = CI->getArgOperand(i);
    OpTy[i] = Op[i]->getType(); // this should be of float
    if(isFloatType(OpTy[i])){
      Instruction *OpIns = dyn_cast<Instruction>(Op[i]);
      Value *OpIdx = handleOperand(I, Op[i], F);
      Constant* ArgNo = ConstantInt::get(Type::getInt64Ty(M->getContext()), i+1);
      if(isFloat(OpTy[i]))
        AddFunArg = M->getOrInsertFunction("fpsan_set_arg_f", VoidTy, Int64Ty, MPtrTy, OpTy[i]);
      else if(isDouble(OpTy[i]))
        AddFunArg = M->getOrInsertFunction("fpsan_set_arg_d", VoidTy, Int64Ty, MPtrTy, OpTy[i]);

      IRB.CreateCall(AddFunArg, {ArgNo, OpIdx, Op[i]});
    }
  }
}

void FPSanitizer::handleFuncInit(Function *F){
  Function::iterator Fit = F->begin();
  BasicBlock &BB = *Fit; 
  BasicBlock::iterator BBit = BB.begin();
  Instruction *First = &*BBit;

  Module *M = F->getParent();
  IRBuilder<> IRB(First);
  Type* VoidTy = Type::getVoidTy(M->getContext());
  Type* Int64Ty = Type::getInt64Ty(M->getContext());

  FuncInit = M->getOrInsertFunction("fpsan_func_init", VoidTy, Int64Ty);
  long TotalArgs = FuncTotalArg.at(F);
  Constant* ConsTotIns = ConstantInt::get(Type::getInt64Ty(M->getContext()), TotalArgs); 
  IRB.CreateCall(FuncInit, {ConsTotIns});
}

void
FPSanitizer::handlePositLibFunc (CallInst *CI,
				BasicBlock *BB,
				Function *F,
				std::string CallName) {
  
  Instruction *I = dyn_cast<Instruction>(CI);
  Instruction *Next = getNextInstruction(dyn_cast<Instruction>(CI), BB);
  IRBuilder<> IRB(Next);
  Module *M = F->getParent();
  
  Type* VoidTy = Type::getVoidTy(M->getContext());
  
  Type* Int64Ty = Type::getInt64Ty(M->getContext());
  Value *OP = CI->getOperand(0);
  
  Value *BOGEP = GEPMap.at(I);
  
  Type *OpTy = OP->getType();
  
  Value *Index1;
  
  Value* ConsIdx1 = handleOperand(I, OP, F);
  
  HandleFunc = M->getOrInsertFunction("__posit_"+CallName, VoidTy, OpTy, MPtrTy, OpTy, MPtrTy);
  IRB.CreateCall(HandleFunc, {OP, ConsIdx1, CI, BOGEP});
  MInsMap.insert(std::pair<Instruction*, Instruction*>(I, dyn_cast<Instruction>(BOGEP)));
}

void
FPSanitizer::handleMathLibFunc (CallInst *CI,
				BasicBlock *BB,
				Function *F,
				std::string CallName) {

  Instruction *I = dyn_cast<Instruction>(CI);
  Instruction *Next = getNextInstruction(dyn_cast<Instruction>(CI), BB);
  IRBuilder<> IRB(Next);
  Module *M = F->getParent();
  
  Type* VoidTy = Type::getVoidTy(M->getContext());
  
  IntegerType* Int32Ty = Type::getInt32Ty(M->getContext());
  IntegerType* Int1Ty = Type::getInt1Ty(M->getContext());
  Type* Int64Ty = Type::getInt64Ty(M->getContext());

  SmallVector<Type *, 4> ArgsTy;
  SmallVector<Value*, 8> ArgsVal;


  ConstantInt* instId = GetInstId(F, I);
  const DebugLoc &instDebugLoc = I->getDebugLoc();
  bool debugInfoAvail = false;;
  unsigned int lineNum = 0;
  unsigned int colNum = 0;
  if (instDebugLoc) {
    debugInfoAvail = true;
    lineNum = instDebugLoc.getLine();
    colNum = instDebugLoc.getCol();
    if (lineNum == 0 && colNum == 0) debugInfoAvail = false;
  }

  ConstantInt* debugInfoAvailable = ConstantInt::get(Int1Ty, debugInfoAvail);
  ConstantInt* lineNumber = ConstantInt::get(Int32Ty, lineNum);
  ConstantInt* colNumber = ConstantInt::get(Int32Ty, colNum);

  Value *BOGEP = GEPMap.at(I);

  Value *Index1;
  
  size_t NumOperands = CI->getNumArgOperands();
  Value *Op[NumOperands];
  Type *OpTy[NumOperands];
  bool Op1Call[NumOperands];
  Value* ConsIdx[NumOperands];


  std::string funcName;
  if(ENV == MPFR){ 
    if(CallName == "llvm.ceil.f64"){
      funcName = "fpsan_mpfr_llvm_ceil";
    }
    else if(CallName == "llvm.floor.f64"){
      funcName = "fpsan_mpfr_llvm_floor";
    }
    else if(CallName == "llvm.fabs.f64"){
      funcName = "fpsan_mpfr_llvm_fabs";
    }
    else
      funcName = "fpsan_mpfr_"+CallName;
  }
  if(ENV == Posit32 || ENV == Double){
    funcName = "__p32_"+CallName;
  }
  if(ENV == Posit16){
    funcName = "__p16_"+CallName;
  }

  for(int i = 0; i < NumOperands; i++){
    Op[i] = CI->getArgOperand(i);
    OpTy[i] = Op[i]->getType(); // this should be of float
    Op1Call[i] = false;
    if(isFloatType(OpTy[i])){
      ConsIdx[i] = handleOperand(I, Op[i], F);
      ArgsVal.push_back(ConsIdx[i]);
      ArgsTy.push_back(MPtrTy);
    }
    ArgsVal.push_back(Op[i]);
    ArgsTy.push_back(OpTy[i]);
  }
  ArgsTy.push_back(MPtrTy);
  ArgsTy.push_back(CI->getType());
  ArgsTy.push_back(Int64Ty);
  ArgsTy.push_back(Int1Ty);
  ArgsTy.push_back(Int32Ty);
  ArgsTy.push_back(Int32Ty);

  ArgsVal.push_back(BOGEP);
  ArgsVal.push_back(CI);
  ArgsVal.push_back(instId);
  ArgsVal.push_back(debugInfoAvailable);
  ArgsVal.push_back(lineNumber);
  ArgsVal.push_back(colNumber);
  if(NumOperands > 1)
    funcName = funcName + std::to_string(NumOperands);
  HandleFunc = M->getOrInsertFunction(funcName, FunctionType::get(IRB.getVoidTy(), ArgsTy, false));
  IRB.CreateCall(HandleFunc, ArgsVal);
  MInsMap.insert(std::pair<Instruction*, Instruction*>(I, dyn_cast<Instruction>(BOGEP)));
  FuncInit = M->getOrInsertFunction("fpsan_trace", VoidTy, MPtrTy, CI->getType());
  IRB.CreateCall(FuncInit, {BOGEP, CI});
}

Value*
FPSanitizer::handleOperand(Instruction *I, Value* OP, Function *F){
  Module *M = F->getParent();
  long Idx = 0;
	
  IRBuilder<> IRB(I);
  Value* ConsInsIndex;
  Instruction *OpIns = dyn_cast<Instruction>(OP);	
  Type* Int64Ty = Type::getInt64Ty(M->getContext());

  if(ConsMap.count(OP) != 0){
    ConsInsIndex = ConsMap.at(OP);
  }
  else if(isa<PHINode>(OP)){
    ConsInsIndex = GEPMap.at(dyn_cast<Instruction>(OP));
  }
  else if(MInsMap.count(dyn_cast<Instruction>(OP)) != 0){
    ConsInsIndex = MInsMap.at(dyn_cast<Instruction>(OP));
  }
  else if(isa<Argument>(OP) && (ArgMap.count(dyn_cast<Argument>(OP)) != 0)){
    Idx =  ArgMap.at(dyn_cast<Argument>(OP));
    ConsInsIndex = MArgMap.at(dyn_cast<Argument>(OP));
  }
  else if(isa<FPTruncInst>(OP) || isa<FPExtInst>(OP)){
    Value *OP1 = OpIns->getOperand(0);

    if(isa<FPTruncInst>(OP1) || isa<FPExtInst>(OP1)){
      Value *OP2 = (dyn_cast<Instruction>(OP1))->getOperand(0);
      if(MInsMap.count(dyn_cast<Instruction>(OP2)) != 0){ //TODO need recursive func
        ConsInsIndex = MInsMap.at(dyn_cast<Instruction>(OP2));
      }
      else{
        F->dump();
        errs()<<"\nError1 !!! operand not found OP:"<<*OP<<"\n";
        errs()<<"In Inst:"<<"\n";
        I->dump();
        exit(1);
      }
    }
    else if(MInsMap.count(dyn_cast<Instruction>(OP1)) != 0){
      ConsInsIndex = MInsMap.at(dyn_cast<Instruction>(OP1));
    }
    else if(ConsMap.count(OP1) != 0){
      ConsInsIndex = ConsMap.at(OP1);
    }
    else{
      F->dump();
      errs()<<"\nError2 !!! operand not found OP:"<<*OP<<"\n";
      errs()<<"In Inst:"<<"\n";
      I->dump();
      exit(1);
    }
  }
  else{
    F->dump();
    errs()<<"\nError3 !!! operand not found OP:"<<*OP<<"\n";
    errs()<<"In Inst:"<<"\n";
    I->dump();
    exit(1);
  }
  return ConsInsIndex;
}

void FPSanitizer::handleStore(StoreInst *SI, BasicBlock *BB, Function *F){
  Instruction *I = dyn_cast<Instruction>(SI);
  Instruction *Next = getNextInstruction(I, BB);
  IRBuilder<> IRB(Next);
  Module *M = F->getParent();

  LLVMContext &C = F->getContext();
  Value *Addr = SI->getPointerOperand();
  Value *OP = SI->getOperand(0);

  Type *StoreTy = I->getOperand(0)->getType();
  IntegerType* Int32Ty = Type::getInt32Ty(M->getContext());
  IntegerType* Int1Ty = Type::getInt1Ty(M->getContext());
  Type* Int64Ty = Type::getInt64Ty(M->getContext());

  Type* VoidTy = Type::getVoidTy(M->getContext());
  Type* PtrVoidTy = PointerType::getUnqual(Type::getInt8Ty(M->getContext()));

  ConstantInt* instId = GetInstId(F, I);
  const DebugLoc &instDebugLoc = I->getDebugLoc();
  bool debugInfoAvail = false;;
  unsigned int lineNum = 0;
  unsigned int colNum = 0;
  if (instDebugLoc) {
    debugInfoAvail = true;
    lineNum = instDebugLoc.getLine();
    colNum = instDebugLoc.getCol();
    if (lineNum == 0 && colNum == 0) debugInfoAvail = false;
  }

  ConstantInt* debugInfoAvailable = ConstantInt::get(Int1Ty, debugInfoAvail);
  ConstantInt* lineNumber = ConstantInt::get(Int32Ty, lineNum);
  ConstantInt* colNumber = ConstantInt::get(Int32Ty, colNum);

  bool BTFlag = false;
  Type *OpTy = OP->getType();
  Instruction *OpIns = dyn_cast<Instruction>(OP);
  //TODO: do we need to check for bitcast for store?
  BitCastInst* BCToAddr = new BitCastInst(Addr, 
					  PointerType::getUnqual(Type::getInt8Ty(M->getContext())),"", I);
  if (BitCastInst *BI = dyn_cast<BitCastInst>(Addr)){
    BTFlag = checkIfBitcastFromFP(BI);
  }
  if(isFloatType(StoreTy) || BTFlag){
    if(MInsMap.count(OpIns) != 0){ //handling registers
      Value* index = MInsMap.at(OpIns);
      SetRealTemp = M->getOrInsertFunction("fpsan_store_shadow", VoidTy, PtrVoidTy, MPtrTy);
      IRB.CreateCall(SetRealTemp, {BCToAddr, index});
    }
    else{
      if(isFloat(StoreTy)){
        SetRealTemp = M->getOrInsertFunction("fpsan_store_shadow_fconst", VoidTy, PtrVoidTy, OpTy, Int32Ty);
        IRB.CreateCall(SetRealTemp, {BCToAddr, OP, lineNumber});
      }
      else if(isDouble(StoreTy)){
        SetRealTemp = M->getOrInsertFunction("fpsan_store_shadow_dconst", VoidTy, PtrVoidTy, OpTy, Int32Ty);
        IRB.CreateCall(SetRealTemp, {BCToAddr, OP, lineNumber});
      }
    }
  }
}


void FPSanitizer::handleNewPhi(Function *F){
  Module *M = F->getParent();
  Instruction* Next;
  long NumPhi = 0;
  BasicBlock *IBB, *BB;
  for(auto it = NewPhiMap.begin(); it != NewPhiMap.end(); ++it)
  {
    if(PHINode *PN = dyn_cast<PHINode>(it->first)){
      PHINode* iPHI = dyn_cast<PHINode>(it->second);
      for (unsigned PI = 0, PE = PN->getNumIncomingValues(); PI != PE; ++PI) {
        IBB = PN->getIncomingBlock(PI);
        Value *IncValue = PN->getIncomingValue(PI);
        BB = PN->getParent();

        if (IncValue == PN) continue; //TODO
        Value* InsIndex = handleOperand(it->first, IncValue, F);
        iPHI->addIncoming(InsIndex, IBB);
      }
    }
  }
}

void FPSanitizer::handlePhi(PHINode *PN, BasicBlock *BB, Function *F){
  Module *M = F->getParent();
  Type* Int64Ty = Type::getInt64Ty(M->getContext());
  Type* VoidTy = Type::getVoidTy(M->getContext());
  IRBuilder<> IRB(dyn_cast<Instruction>(dyn_cast<Instruction>(PN)));

  PHINode* iPHI = IRB.CreatePHI (MPtrTy, 2);
  //Wherever old phi node has been used, we need to replace it with new phi node. That's
  //why need to track it and keep it in RegIdMap
  MInsMap.insert(std::pair<Instruction*, Instruction*>(PN, iPHI));
  NewPhiMap.insert(std::pair<Instruction*, Instruction*>(dyn_cast<Instruction>(PN), iPHI));

  Instruction* Next;
  Next = getNextInstructionNotPhi(PN, BB);
  //create call to runtime to copy 
  //  Next++;
  IRBuilder<> IRBN(Next);

  Value *BOGEP = GEPMap.at(PN);

  AddFunArg = M->getOrInsertFunction("fpsan_copy_phi", VoidTy, MPtrTy, MPtrTy);
  IRBN.CreateCall(AddFunArg, {iPHI, BOGEP});
} 

void FPSanitizer::handleSelect(SelectInst *SI, BasicBlock *BB, Function *F){
  Instruction *I = dyn_cast<Instruction>(SI);
  Instruction *Next = getNextInstruction(I, BB);
  IRBuilder<> IRB(Next);
  Module *M = F->getParent();

  Value *Index1;
  Value *Index2;

  Value *OP1 = SI->getOperand(0);

  Value* InsIndex2 = handleOperand(I, SI->getOperand(1), F);
  Value* InsIndex3 = handleOperand(I, SI->getOperand(2), F);

  Value *NewOp2 = dyn_cast<Value>(InsIndex2); 
  Value *NewOp3 = dyn_cast<Value>(InsIndex3);
 
  Type* FCIOpType = SI->getOperand(0)->getType();

  Value *Select = IRB.CreateSelect(OP1, NewOp2, NewOp3); 
  Instruction *NewIns = dyn_cast<Instruction>(Select);

  MInsMap.insert(std::pair<Instruction*, Instruction*>(SI, NewIns));
}

void FPSanitizer::handleReturn(ReturnInst *RI, BasicBlock *BB, Function *F){

  Instruction *Ins = dyn_cast<Instruction>(RI);
  Module *M = F->getParent();

  Value *Index1;
  Value *Index2;

  Type* Int64Ty = Type::getInt64Ty(M->getContext());
  Type* VoidTy = Type::getVoidTy(M->getContext());

  Value* OpIdx;
  //Find first mpfr clear
  for (auto &BB : *F) {
    for (auto &I : BB) {
      if (CallInst *CI = dyn_cast<CallInst>(&I)){
        Function *Callee = CI->getCalledFunction();
        if(Callee && Callee->getName() == "fpsan_clear_mpfr"){
            Ins = &I;
          break;
        }
      }
    }
  }
  
  IRBuilder<> IRB(Ins);
  if (RI->getNumOperands() != 0){
    Value *OP = RI->getOperand(0);
    if(isFloatType(OP->getType())){
        OpIdx = handleOperand(dyn_cast<Instruction>(RI), OP, F);
        long TotalArgs = FuncTotalArg.at(F);
        Constant* ArgNo = ConstantInt::get(Type::getInt64Ty(M->getContext()), 0); // 0 for return
        Constant* TotalArgsConst = ConstantInt::get(Type::getInt64Ty(M->getContext()), TotalArgs); 
        AddFunArg = M->getOrInsertFunction("fpsan_set_return", VoidTy, MPtrTy, Int64Ty, OP->getType());
        IRB.CreateCall(AddFunArg, {OpIdx, TotalArgsConst, OP});
    }
    else{
      FuncInit = M->getOrInsertFunction("fpsan_func_exit", VoidTy, Int64Ty);
      long TotalArgs = FuncTotalArg.at(F);
      Constant* ConsTotIns = ConstantInt::get(Type::getInt64Ty(M->getContext()), TotalArgs); 
      IRB.CreateCall(FuncInit, {ConsTotIns});
    }
  }
  else{
    FuncInit = M->getOrInsertFunction("fpsan_func_exit", VoidTy, Int64Ty);
    long TotalArgs = FuncTotalArg.at(F);
    Constant* ConsTotIns = ConstantInt::get(Type::getInt64Ty(M->getContext()), TotalArgs); 
    IRB.CreateCall(FuncInit, {ConsTotIns});
  }
}

void FPSanitizer::handleBinOp(BinaryOperator* BO, BasicBlock *BB, Function *F){
  Instruction *I = dyn_cast<Instruction>(BO);
  Instruction *Next = getNextInstruction(I, BB);
  IRBuilder<> IRB(Next);
  Module *M = F->getParent();

  Value *Index1;
  Value *Index2;
	
  Value* InsIndex1 = handleOperand(I, BO->getOperand(0), F);
  Value* InsIndex2 = handleOperand(I, BO->getOperand(1), F);

  Type* BOType = BO->getOperand(0)->getType();
  Type* Int64Ty = Type::getInt64Ty(M->getContext());
  Type* VoidTy = Type::getVoidTy(M->getContext());
  IntegerType* Int1Ty = Type::getInt1Ty(M->getContext());
  IntegerType* Int32Ty = Type::getInt32Ty(M->getContext());

  ConstantInt* instId = GetInstId(F, I);
  const DebugLoc &instDebugLoc = I->getDebugLoc();
  bool debugInfoAvail = false;;
  unsigned int lineNum = 0;
  unsigned int colNum = 0;
  if (instDebugLoc) {
    debugInfoAvail = true;
    lineNum = instDebugLoc.getLine();
    colNum = instDebugLoc.getCol();
    if (lineNum == 0 && colNum == 0) debugInfoAvail = false;
  }

  ConstantInt* debugInfoAvailable = ConstantInt::get(Int1Ty, debugInfoAvail);
  ConstantInt* lineNumber = ConstantInt::get(Int32Ty, lineNum);
  ConstantInt* colNumber = ConstantInt::get(Int32Ty, colNum);

  Value *BOGEP = GEPMap.at(I);

  std::string opName(I->getOpcodeName());
  if(ENV == MPFR){
    if(isFloat(BO->getType())){
      ComputeReal = M->getOrInsertFunction("fpsan_mpfr_"+opName+"_f", VoidTy, MPtrTy, MPtrTy, MPtrTy, BOType, BOType,
				       BOType, Int64Ty, Int1Ty, Int32Ty, Int32Ty);
    }
    else if(isDouble(BO->getType())){
      ComputeReal = M->getOrInsertFunction("fpsan_mpfr_"+opName, VoidTy, MPtrTy, MPtrTy, MPtrTy, BOType, BOType,
				       BOType, Int64Ty, Int1Ty, Int32Ty, Int32Ty);
    }
  }
  else if(ENV == Posit32 || ENV == Double){
    if(isFloat(BO->getType())){
      ComputeReal = M->getOrInsertFunction("__p32_"+opName+"_f", VoidTy, BOType, MPtrTy, BOType, 
				       MPtrTy, BOType, MPtrTy, Int64Ty);
    }
    else if(isDouble(BO->getType())){
      ComputeReal = M->getOrInsertFunction("__p32_"+opName, VoidTy, BOType, MPtrTy, BOType, 
				       MPtrTy, BOType, MPtrTy, Int64Ty);
    }
  }
  else if(ENV == Posit16){
    if(isFloat(BO->getType())){
      ComputeReal = M->getOrInsertFunction("__p16_"+opName+"_f", VoidTy, BOType, MPtrTy, BOType, 
				       MPtrTy, BOType, MPtrTy, Int64Ty);
    }
    else if(isDouble(BO->getType())){
      ComputeReal = M->getOrInsertFunction("__p16_"+opName, VoidTy, BOType, MPtrTy, BOType, 
				       MPtrTy, BOType, MPtrTy, Int64Ty);
    }
  }
  IRB.CreateCall(ComputeReal, {InsIndex1, InsIndex2, BOGEP, BO->getOperand(0), BO->getOperand(1), BO, 
	        instId, debugInfoAvailable, lineNumber, colNumber});
  MInsMap.insert(std::pair<Instruction*, Instruction*>(I, dyn_cast<Instruction>(BOGEP)));
  FuncInit = M->getOrInsertFunction("fpsan_trace", VoidTy, MPtrTy, BOType);
  IRB.CreateCall(FuncInit, {BOGEP, BO});
}

void FPSanitizer::handleFcmp(FCmpInst *FCI, BasicBlock *BB, Function *F){

  Instruction *I = dyn_cast<Instruction>(FCI);
  Instruction *Next = getNextInstruction(I, BB);
  IRBuilder<> IRB(Next);
  Module *M = F->getParent();

  Value *Index1;
  Value *Index2;

  Value* InsIndex1 = handleOperand(I, FCI->getOperand(0), F);
  Value* InsIndex2 = handleOperand(I, FCI->getOperand(1), F);

  Type* FCIOpType = FCI->getOperand(0)->getType();
  Type* Int64Ty = Type::getInt64Ty(M->getContext());
  IntegerType* Int1Ty = Type::getInt1Ty(M->getContext());
  IntegerType* Int32Ty = Type::getInt32Ty(M->getContext());

  ConstantInt* instId = GetInstId(F, I);
  const DebugLoc &instDebugLoc = I->getDebugLoc();
  bool debugInfoAvail = false;;
  unsigned int lineNum = 0;
  unsigned int colNum = 0;
  if (instDebugLoc) {
    debugInfoAvail = true;
    lineNum = instDebugLoc.getLine();
    colNum = instDebugLoc.getCol();
    if (lineNum == 0 && colNum == 0) debugInfoAvail = false;
  }

  ConstantInt* debugInfoAvailable = ConstantInt::get(Int1Ty, debugInfoAvail);
  ConstantInt* lineNumber = ConstantInt::get(Int32Ty, lineNum);
  ConstantInt* colNumber = ConstantInt::get(Int32Ty, colNum);

  Constant* OpCode = ConstantInt::get(Type::getInt64Ty(M->getContext()), FCI->getPredicate());
  if(isFloat(FCIOpType))
    CheckBranch = M->getOrInsertFunction("fpsan_check_branch_f", Int1Ty, FCIOpType, MPtrTy, FCIOpType, 
				       MPtrTy, Int64Ty, Int1Ty, Int32Ty);
  else if(isDouble(FCIOpType)){
    CheckBranch = M->getOrInsertFunction("fpsan_check_branch_d", Int1Ty, FCIOpType, MPtrTy, FCIOpType, 
				       MPtrTy, Int64Ty, Int1Ty, Int32Ty);
  }
  IRB.CreateCall(CheckBranch, {FCI->getOperand(0), InsIndex1, FCI->getOperand(1), InsIndex2, 
	OpCode, I, lineNumber});
}

bool FPSanitizer::checkIfBitcastFromFP(BitCastInst *BI){
  bool BTFlag = false;
  Type *BITy = BI->getOperand(0)->getType();
  //check if load operand is bitcast and bitcast operand is float type
  if(isFloatType(BITy)){
    BTFlag = true;
  }
  //check if load operand is bitcast and bitcast operand is struct type.	
  //check if struct has any member of float type
  else if(BITy->getPointerElementType()->getTypeID() == Type::StructTyID){
    StructType *STyL = cast<StructType>(BITy->getPointerElementType());
    int num = STyL->getNumElements();
    for(int i = 0; i < num; i++) {
      if(isFloatType(STyL->getElementType(i)))
	      BTFlag = true;
    }
  }
  return BTFlag;
}

void FPSanitizer::handleLoad(LoadInst *LI, BasicBlock *BB, Function *F){
  Instruction *I = dyn_cast<Instruction>(LI);
  Module *M = F->getParent();
  Instruction *Next = getNextInstruction(I, BB);
  IRBuilder<> IRB(Next);

  LLVMContext &C = F->getContext();

  Type* PtrVoidTy = PointerType::getUnqual(Type::getInt8Ty(C));
  Type* VoidTy = Type::getVoidTy(C);
  IntegerType* Int1Ty = Type::getInt1Ty(M->getContext());
  IntegerType* Int32Ty = Type::getInt32Ty(M->getContext());

  Value *Addr = LI->getPointerOperand();
  if (isa<GlobalVariable>(Addr)){
  }
  bool BTFlag = false;

  BitCastInst* BCToAddr = new BitCastInst(Addr, 
      PointerType::getUnqual(Type::getInt8Ty(M->getContext())),"", I);
  if(isFloatType(LI->getType()) || BTFlag){
    Value *BOGEP = GEPMap.at(LI);
    long InsIndex;
    Constant* CBTFlagD = ConstantInt::get(Type::getInt1Ty(M->getContext()), BTFlag);
    if(isFloat(LI->getType())){    
      LoadCall = M->getOrInsertFunction("fpsan_load_shadow_fconst", VoidTy, MPtrTy, PtrVoidTy, LI->getType()); //TODO:28 Jan: Do we really need load_f?
    }
    else if(isDouble(LI->getType())){
      LoadCall = M->getOrInsertFunction("fpsan_load_shadow_dconst", VoidTy, MPtrTy, PtrVoidTy, LI->getType());
    }
    Value* LoadI = IRB.CreateCall(LoadCall, {BOGEP, BCToAddr, LI});
    MInsMap.insert(std::pair<Instruction*, Instruction*>(I, dyn_cast<Instruction>(BOGEP)));
  }
}

void FPSanitizer::handleIns(Instruction *I, BasicBlock *BB, Function *F){

  //instrument interesting instructions
  if (LoadInst *LI = dyn_cast<LoadInst>(I)){
    handleLoad(LI, BB, F);
  }
  else if (FCmpInst *FCI = dyn_cast<FCmpInst>(I)){
    handleFcmp(FCI, BB, F);
  }
  else if (StoreInst *SI = dyn_cast<StoreInst>(I)){
    handleStore(SI, BB, F);
  }
  else if (SelectInst *SI = dyn_cast<SelectInst>(I)){
    if(isFloatType(I->getType())){
      handleSelect(SI, BB, F);
    }
  }
  else if (ExtractValueInst *EVI = dyn_cast<ExtractValueInst>(I)){
  }
  else if (BinaryOperator* BO = dyn_cast<BinaryOperator>(I)){
    switch(BO->getOpcode()) {                                                                                                                                         
      case Instruction::FAdd:                                                                        
      case Instruction::FSub:
      case Instruction::FMul:
      case Instruction::FDiv:{
                               handleBinOp(BO, BB, F);
                             } 
    }
  }
  else if (CallInst *CI = dyn_cast<CallInst>(I)){
    Function *Callee = CI->getCalledFunction();
    if (Callee) {
      if(isListedFunction(Callee->getName(), "positFunc.txt"))
        handlePositLibFunc(CI, BB, F, Callee->getName());
      if(isListedFunction(Callee->getName(), "mathFunc.txt"))
        handleMathLibFunc(CI, BB, F, Callee->getName());
      else if(isListedFunction(Callee->getName(), "functions.txt"))
        handleCallInst(CI, BB, F, Callee->getName());
    }
  }
}

bool FPSanitizer::runOnModule(Module &M) {

  LLVMContext &C = M.getContext();

  if(ENV == MPFR){
    StructType* MPFRTy1 = StructType::create(M.getContext(), "struct.fpsan_mpfr");
    MPFRTy1->setBody({Type::getInt64Ty(M.getContext()), Type::getInt32Ty(M.getContext()),
        Type::getInt64Ty(M.getContext()), Type::getInt64PtrTy(M.getContext())});

    MPFRTy = StructType::create(M.getContext(), "struct.f_mpfr");
    MPFRTy->setBody(llvm::ArrayType::get(MPFRTy1, 1));

    Real = StructType::create(M.getContext(), "struct.temp_entry");
    RealPtr = Real->getPointerTo();
    Real->setBody({MPFRTy,
	  Type::getDoubleTy(M.getContext()),
	  Type::getInt32Ty(M.getContext()),
	  Type::getInt32Ty(M.getContext()),
	  
	  Type::getInt64Ty(M.getContext()),
	  Type::getInt64Ty(M.getContext()),
	  Type::getInt64Ty(M.getContext()),
	  Type::getInt64Ty(M.getContext()),
	  RealPtr,
	  
	  Type::getInt64Ty(M.getContext()),
	  Type::getInt64Ty(M.getContext()),
	  RealPtr,
	  
	  Type::getInt64Ty(M.getContext()),
	  Type::getInt64Ty(M.getContext()),
	  Type::getInt1Ty(M.getContext())});

    MPtrTy = Real->getPointerTo();
  }
  else if(ENV == Posit32){
    MPFRTy = StructType::create(M.getContext(), "struct.f_posit32");
    MPFRTy->setBody({Type::getInt32Ty(M.getContext())});
    MPtrTy = MPFRTy->getPointerTo();
  }
  else if(ENV == Posit16){
    MPFRTy = StructType::create(M.getContext(), "struct.f_posit16");
    MPFRTy->setBody({Type::getInt16Ty(M.getContext())});
    MPtrTy = MPFRTy->getPointerTo();
  }
  else if(ENV == Double){
    MPFRTy = StructType::create(M.getContext(), "struct.f_double");
    MPFRTy->setBody({Type::getDoubleTy(M.getContext())});
    MPtrTy = MPFRTy->getPointerTo();
  }
//TODO::Iterate over global arrays to initialize shadow memory
  for (Module::global_iterator GVI = M.global_begin(), E = M.global_end();
               GVI != E; ) {
    GlobalVariable *GV = &*GVI++;
//    if(isFloatType(GV->getType())){
      if(GV->hasInitializer()){
        Constant *Init = GV->getInitializer();
      }
  //  }
  }
  // Find functions that perform floating point computation. No
  // instrumentation if the function does not perform any FP
  // computations.
  for (auto &F : M) {
    if (F.isDeclaration()) continue;
    findInterestingFunctions(&F);
  }

  for (auto &F : M) {
    if (F.isDeclaration()) continue;
    if (!isListedFunction(F.getName(), "functions.txt")) continue;
    //All instrumented functions are listed in AllFuncList	
    AllFuncList.push_back(&F);
  } 

  int instId = 0;
  //instrument interesting instructions
  Instruction *LastPhi = NULL;
  for (Function *F : reverse(AllFuncList)) {
    //give unique indexes to instructions and instrument with call to
    //dynamic lib
    createMpfrAlloca(F);

    //if argument is used in any floating point computation, then we need to retrieve that argument
    //from shadow stack.
    //Instead of call __get_arg everytime opearnd is used, it is better to call once in start of the function
    //and remember the address of shadow stack.
    callGetArgument(F);

    if(F->getName() != "main"){
      //add func_init and func_exit in the start and end of the function to set shadow stack variables
      handleFuncInit(F);
    }
    for (auto &BB : *F) {
      for (auto &I : BB) {
        LLVMContext& instContext = I.getContext();
        ConstantInt* instUniqueId = ConstantInt::get(Type::getInt64Ty(M.getContext()), instId);
        ConstantAsMetadata* uniqueId = ConstantAsMetadata::get(instUniqueId);
        MDNode* md = MDNode::get(instContext, uniqueId);
        I.setMetadata("fpsan_inst_id", md);
        instId++;
         if(PHINode *PN = dyn_cast<PHINode>(&I)){
          if(isFloatType(I.getType())){
            handlePhi(PN, &BB, F);
            LastPhi = &I;
          }
        }
        handleIns(&I, &BB, F);
      }
    }
    for (auto &BB : *F) {
      for (auto &I : BB) {
        if (ReturnInst *RI = dyn_cast<ReturnInst>(&I)){
          handleReturn(RI, &BB, F);
        }
      }
    }
    handleNewPhi(F);
    NewPhiMap.clear(); 
    MInsMap.clear(); 
    GEPMap.clear(); 
    ConsMap.clear(); 
  } 
  for (auto &F : M) {
    if (F.isDeclaration()) continue;
    if(F.getName() == "main"){
      //add init and finish func in the start and end of the main function to initialize shadow memory
      handleFuncMainInit(&F);
    }
    for (auto &BB : F) {
      for (auto &I : BB) {
        if (dyn_cast<ReturnInst>(&I)){
          if(F.getName() == "main"){
            handleMainRet(&I, &F);
          }
        }
      }
    }
  } 
//  M.dump();
 
  return true;
}



void addFPPass(const PassManagerBuilder &Builder, legacy::PassManagerBase &PM) {
  PM.add(new FPSanitizer());
}

RegisterStandardPasses SOpt(PassManagerBuilder::EP_OptimizerLast,
			    addFPPass);
RegisterStandardPasses S(PassManagerBuilder::EP_EnabledOnOptLevel0,
                         addFPPass);

char FPSanitizer::ID = 0;
static const RegisterPass<FPSanitizer> Y("fpsan", "instrument fp operations", false, false);
