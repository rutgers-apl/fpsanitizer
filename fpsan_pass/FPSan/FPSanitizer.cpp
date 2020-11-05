#include "FPSanitizer.h"
#include "llvm/IR/CallSite.h"  
#include "llvm/IR/ConstantFolder.h"
#include "llvm/ADT/SCCIterator.h" 
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"

static cl::opt<int> Precision("fpsan-precision",
    cl::desc("default mpfr precision is initialized to 512"),
    cl::Hidden, cl::init(512));

static cl::opt<int> ENV("fpsan-with-type",
    cl::desc("shadow execution with mpfr"),
    cl::Hidden, cl::init(2));

void FPSanitizer::addFunctionsToList(std::string FN) {
  std::ofstream myfile;
  myfile.open("functions.txt", std::ios::out|std::ios::app);
  if(isListedFunction(FN, "forbid.txt")) return;
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


              FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
              IRB.CreateCall(FuncInit, {BOGEP});

              FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
              IRBE.CreateCall(FuncInit, {BOGEP});

              index++;

              if(isa<ConstantFP>(Op1)){
                if(index-1 > TotalAlloca){
                  errs()<<"Error\n\n\n: index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
                }
                Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
                  ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
                Value *BOGEP = IRB.CreateGEP(Alloca, Indices);
                GEPMap.insert(std::pair<Instruction*, Value*>(dyn_cast<Instruction>(Op1), BOGEP));


                FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
                IRB.CreateCall(FuncInit, {BOGEP});

                if(isFloat(Op1->getType())){
                  FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_fconst", VoidTy, MPtrTy, Op1->getType(), Int32Ty);

                }
                else if(isDouble(Op1->getType())){

                  FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_dconst", VoidTy, MPtrTy, Op1->getType(), Int32Ty);

                }
                IRB.CreateCall(FuncInit, {BOGEP, Op1, lineNumber});
                ConsMap.insert(std::pair<Value*, Value*>(Op1, BOGEP));


                FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
                IRBE.CreateCall(FuncInit, {BOGEP});

                index++;
              }
              if(isa<ConstantFP>(Op2)){
                if(index-1 > TotalAlloca){
                  errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
                }
                Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
                  ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
                Value *BOGEP = IRB.CreateGEP(Alloca, Indices);
                GEPMap.insert(std::pair<Instruction*, Value*>(dyn_cast<Instruction>(Op2), BOGEP));


                FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
                IRB.CreateCall(FuncInit, {BOGEP});

                if(isFloat(Op2->getType())){

                  FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_fconst", VoidTy, MPtrTy, Op2->getType(), Int32Ty);

                }
                else if(isDouble(Op2->getType())){

                  FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_dconst", VoidTy, MPtrTy, Op2->getType(), Int32Ty);

                }
                IRB.CreateCall(FuncInit, {BOGEP, Op2, lineNumber});
                ConsMap.insert(std::pair<Value*, Value*>(Op2, BOGEP));

                FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
                IRBE.CreateCall(FuncInit, {BOGEP});
                index++;
              }
            }
        }
      }
      else if (UnaryOperator *UO = dyn_cast<UnaryOperator>(&I)) {
        if (GEPMap.count(&I) != 0) {
          continue;
        }
        switch (UO->getOpcode()) {
          case Instruction::FNeg: {
            Instruction *Next = getNextInstruction(UO, &BB);
            IRBuilder<> IRBI(Next);
            if (index - 1 > TotalAlloca) {
              errs() << "Error:\n\n\n index > TotalAlloca " << index << ":"<< TotalAlloca << "\n";
            }
            Value *Indices[] = {
              ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
              ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
            Value *BOGEP = IRB.CreateGEP(Alloca, Indices);
            GEPMap.insert(std::pair<Instruction *, Value *>(dyn_cast<Instruction>(UO), BOGEP));

            FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
            IRB.CreateCall(FuncInit, {BOGEP});

            FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
            IRBE.CreateCall(FuncInit, {BOGEP});
            index++;
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


        FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
        IRB.CreateCall(FuncInit, {BOGEP});

        if(isFloat(UI->getType())){
          FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_fconst", VoidTy, MPtrTy, UI->getType(), Int32Ty);
        }
        else if(isDouble(UI->getType())){

          FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_dconst", VoidTy, MPtrTy, UI->getType(), Int32Ty);
        }
        IRBI.CreateCall(FuncInit, {BOGEP, UI, lineNumber});
        ConsMap.insert(std::pair<Value*, Value*>(UI, BOGEP));

        FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
        IRBE.CreateCall(FuncInit, {BOGEP});
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

          FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
          IRB.CreateCall(FuncInit, {BOGEP});

          if(isFloat(UI->getType())){
            FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_fconst", VoidTy, MPtrTy, UI->getType(), Int32Ty);
          }
          else if(isDouble(UI->getType())){
            FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_dconst", VoidTy, MPtrTy, UI->getType(), Int32Ty);
          }

          IRBI.CreateCall(FuncInit, {BOGEP, UI, lineNumber});
          ConsMap.insert(std::pair<Value*, Value*>(UI, BOGEP));
          FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
          IRBE.CreateCall(FuncInit, {BOGEP});

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


        FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
        IRB.CreateCall(FuncInit, {BOGEP});

        if(isFloat(UI->getType())){
          FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_fconst", VoidTy, MPtrTy, UI->getType(), Int32Ty);
        }
        else if(isDouble(UI->getType())){

          FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_dconst", VoidTy, MPtrTy, UI->getType(), Int32Ty);

        }
        IRBI.CreateCall(FuncInit, {BOGEP, UI, lineNumber});
        ConsMap.insert(std::pair<Value*, Value*>(UI, BOGEP));

        FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
        IRBE.CreateCall(FuncInit, {BOGEP});

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


          FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
          IRB.CreateCall(FuncInit, {BOGEP});

          if(isFloat(Op1->getType())){
            FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_fconst", VoidTy, MPtrTy, Op1->getType(), Int32Ty);
          }
          else if(isDouble(Op1->getType())){

            FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_dconst", VoidTy, MPtrTy, Op1->getType(), Int32Ty);
          }
          if(Op1Call){
            Instruction *Next = getNextInstruction(FCI, &BB);
            IRBuilder<> IRBI(Next);
            IRBI.CreateCall(FuncInit, {BOGEP, Op1, lineNumber});
          }
          else
            IRB.CreateCall(FuncInit, {BOGEP, Op1, lineNumber});
          ConsMap.insert(std::pair<Value*, Value*>(Op1, BOGEP));

          FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
          IRBE.CreateCall(FuncInit, {BOGEP});
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


          FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
          IRB.CreateCall(FuncInit, {BOGEP});

          if(isFloat(Op2->getType())){
            FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_fconst", VoidTy, MPtrTy, Op2->getType(), Int32Ty);
          }
          else if(isDouble(Op2->getType())){

            FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_dconst", VoidTy, MPtrTy, Op2->getType(), Int32Ty);

          }
          if(Op2Call){
            Instruction *Next = getNextInstruction(FCI, &BB);
            IRBuilder<> IRBI(Next);
            IRBI.CreateCall(FuncInit, {BOGEP, Op2, lineNumber});
          }
          else
            IRB.CreateCall(FuncInit, {BOGEP, Op2, lineNumber});
          ConsMap.insert(std::pair<Value*, Value*>(Op2, BOGEP));


          FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
          IRBE.CreateCall(FuncInit, {BOGEP});

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


          GEPMap.insert(std::pair<Instruction*, Value*>(&I, BOGEP));
          FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
          IRB.CreateCall(FuncInit, {BOGEP});

          FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
          IRBE.CreateCall(FuncInit, {BOGEP});

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


            GEPMap.insert(std::pair<Instruction*, Value*>(&I, BOGEP));
            FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
            IRB.CreateCall(FuncInit, {BOGEP});

            FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
            IRBE.CreateCall(FuncInit, {BOGEP});

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


            FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
            IRB.CreateCall(FuncInit, {BOGEP});

            FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
            IRBE.CreateCall(FuncInit, {BOGEP});

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


                FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
                IRB.CreateCall(FuncInit, {BOGEP});

                if(isFloat(Op[i]->getType())){

                  FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_fconst", VoidTy, MPtrTy, OpTy[i], Int32Ty);
                }
                else if(isDouble(Op[i]->getType())){

                  FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_dconst", VoidTy, MPtrTy, OpTy[i], Int32Ty);
                }

                if(Op1Call[i]){
                  Instruction *Next = getNextInstruction(BO, &BB);
                  IRBuilder<> IRBI(Next);
                  IRBI.CreateCall(FuncInit, {BOGEP, Op[i], lineNumber});
                }
                else
                  IRB.CreateCall(FuncInit, {BOGEP, Op[i], lineNumber});
                ConsMap.insert(std::pair<Value*, Value*>(Op[i], BOGEP));


                FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
                IRBE.CreateCall(FuncInit, {BOGEP});

                index++;
              }
            }
          }
        }
        else{//indirect
          if(isFloatType(CI->getType())){
            if(index-1 > TotalAlloca){
              errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
            }
            Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
              ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
            Value *BOGEP = IRB.CreateGEP(Alloca, Indices);

            GEPMap.insert(std::pair<Instruction*, Value*>(&I, BOGEP));


            FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
            IRB.CreateCall(FuncInit, {BOGEP});

            FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
            IRBE.CreateCall(FuncInit, {BOGEP});

            index++;
          }
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
            if(isa<ConstantFP>(Op[i])){
              if(index-1 > TotalAlloca){
                errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
              }
              Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
                ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
              Value *BOGEP = IRB.CreateGEP(Alloca, Indices);

              GEPMap.insert(std::pair<Instruction*, Value*>(dyn_cast<Instruction>(Op[i]), BOGEP));


              FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
              IRB.CreateCall(FuncInit, {BOGEP});

              if(isFloat(Op[i]->getType())){

                FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_fconst", VoidTy, MPtrTy, OpTy[i], Int32Ty);
              }
              else if(isDouble(Op[i]->getType())){

                FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_dconst", VoidTy, MPtrTy, OpTy[i], Int32Ty);
              }

              IRB.CreateCall(FuncInit, {BOGEP, Op[i], lineNumber});
              ConsMap.insert(std::pair<Value*, Value*>(Op[i], BOGEP));


              FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
              IRBE.CreateCall(FuncInit, {BOGEP});

              index++;
            }
          }
        }
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

            FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
            IRB.CreateCall(FuncInit, {BOGEP});

            if(isFloat(FCI->getOperand(0)->getType())){

              FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_fconst", VoidTy, MPtrTy, FCI->getOperand(0)->getType(), Int32Ty);

            }
            else if(isDouble(FCI->getOperand(0)->getType())){

              FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_dconst", VoidTy, MPtrTy, FCI->getOperand(0)->getType(), Int32Ty);

            }
            IRB.CreateCall(FuncInit, {BOGEP, FCI->getOperand(0), lineNumber});
            ConsMap.insert(std::pair<Value*, Value*>(FCI->getOperand(0), BOGEP));

            FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
            IRBE.CreateCall(FuncInit, {BOGEP});
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


            FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
            IRB.CreateCall(FuncInit, {BOGEP});

            if(isFloat(FCI->getOperand(1)->getType())){
              FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_fconst", VoidTy, MPtrTy, FCI->getOperand(1)->getType(), Int32Ty);
            }
            else if(isDouble(FCI->getOperand(1)->getType())){
              FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_dconst", VoidTy, MPtrTy, FCI->getOperand(1)->getType(), Int32Ty);
            }
            IRB.CreateCall(FuncInit, {BOGEP, FCI->getOperand(1), lineNumber});
            ConsMap.insert(std::pair<Value*, Value*>(FCI->getOperand(1), BOGEP));


            FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
            IRBE.CreateCall(FuncInit, {BOGEP});

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


            FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
            IRB.CreateCall(FuncInit, {BOGEP});

            if(isFloat(SI->getOperand(1)->getType())){

              FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_fconst", VoidTy, MPtrTy, SI->getOperand(1)->getType(), Int32Ty);
            }
            else if(isDouble(SI->getOperand(1)->getType())){

              FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_dconst", VoidTy, MPtrTy, SI->getOperand(1)->getType(), Int32Ty);
            }
            IRB.CreateCall(FuncInit, {BOGEP, SI->getOperand(1), lineNumber});
            ConsMap.insert(std::pair<Value*, Value*>(SI->getOperand(1), BOGEP));


            FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
            IRBE.CreateCall(FuncInit, {BOGEP});
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

            FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
            IRB.CreateCall(FuncInit, {BOGEP});

            if(isFloat(SI->getOperand(2)->getType())){

              FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_fconst", VoidTy, MPtrTy, SI->getOperand(2)->getType(), Int32Ty);
            }
            else if(isDouble(SI->getOperand(2)->getType())){

              FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_dconst", VoidTy, MPtrTy, SI->getOperand(2)->getType(), Int32Ty);	      
            }
            IRB.CreateCall(FuncInit, {BOGEP, SI->getOperand(2), lineNumber});
            ConsMap.insert(std::pair<Value*, Value*>(SI->getOperand(2), BOGEP));

            FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
            IRBE.CreateCall(FuncInit, {BOGEP});

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


          FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
          IRB.CreateCall(FuncInit, {BOGEP});

          FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
          IRBE.CreateCall(FuncInit, {BOGEP});

          index++;
        }
        for (unsigned PI = 0, PE = PN->getNumIncomingValues(); PI != PE; ++PI) {
          Value *IncValue = PN->getIncomingValue(PI);

          if (IncValue == PN) continue; //TODO
          if(isa<ConstantFP>(IncValue)){
            if(index-1 > TotalAlloca){
              errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
            }
            Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
              ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
            Value *BOGEP = IRB.CreateGEP(Alloca, Indices);

            GEPMap.insert(std::pair<Instruction*, Value*>(dyn_cast<Instruction>(IncValue), BOGEP));

            FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
            IRB.CreateCall(FuncInit, {BOGEP});

            if(isFloat(IncValue->getType())){

              FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_fconst", VoidTy, MPtrTy, IncValue->getType(), Int32Ty);
            }
            else if(isDouble(IncValue->getType())){

              FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_dconst", VoidTy, MPtrTy, IncValue->getType(), Int32Ty);
            }
            IRB.CreateCall(FuncInit, {BOGEP, IncValue, lineNumber});
            ConsMap.insert(std::pair<Value*, Value*>(IncValue, BOGEP));

            FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
            IRBE.CreateCall(FuncInit, {BOGEP});

            index++;
          }
        }
      }
      if(ReturnInst *RT = dyn_cast<ReturnInst>(&I)){
        if (RT->getNumOperands() != 0){
          Value *Op = RT->getOperand(0);
          if(isFloatType(Op->getType())){
            if(isa<ConstantFP>(Op)){
              if(index-1 > TotalAlloca){
                errs()<<"Error:\n\n\n index > TotalAlloca "<<index<<":"<<TotalAlloca<<"\n";
              }
              Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(M->getContext()), 0),
                ConstantInt::get(Type::getInt32Ty(M->getContext()), index)};
              Value *BOGEP = IRB.CreateGEP(Alloca, Indices);

              GEPMap.insert(std::pair<Instruction*, Value*>(dyn_cast<Instruction>(Op), BOGEP));

              FuncInit = M->getOrInsertFunction("fpsan_init_mpfr", VoidTy, MPtrTy);
              IRB.CreateCall(FuncInit, {BOGEP});

              if(isFloat(Op->getType())){
                FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_fconst", VoidTy, MPtrTy, Op->getType(), Int32Ty);
              }
              else if(isDouble(Op->getType())){
                FuncInit = M->getOrInsertFunction("fpsan_store_tempmeta_dconst", VoidTy, MPtrTy, Op->getType(), Int32Ty);
              }
              IRB.CreateCall(FuncInit, {BOGEP, Op, lineNumber});
              ConsMap.insert(std::pair<Value*, Value*>(Op, BOGEP));

              FuncInit = M->getOrInsertFunction("fpsan_clear_mpfr", VoidTy, MPtrTy);
              IRBE.CreateCall(FuncInit, {BOGEP});

              index++;
            }
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

long FPSanitizer::getTotalFPInst(Function *F){
  long TotalAlloca = 0;
  for (auto &BB : *F) {
    for (auto &I : BB) {
      if (UnaryOperator *UO = dyn_cast<UnaryOperator>(&I)) {
        switch (UO->getOpcode()) {
          case Instruction::FNeg:
            {
              TotalAlloca++;
            }
        }
      }
      else if (BinaryOperator* BO = dyn_cast<BinaryOperator>(&I)){
        switch(BO->getOpcode()) {                                                                                                
          case Instruction::FAdd:
          case Instruction::FSub:
          case Instruction::FMul:
          case Instruction::FDiv:
            {
              Value *Op1 = BO->getOperand(0);
              Value *Op2 = BO->getOperand(1);

              TotalAlloca++;

              if(isa<ConstantFP>(Op1)){
                TotalAlloca++;
              }
              if(isa<ConstantFP>(Op2)){
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
        else{//indirect
          if(isFloatType(CI->getType())){
            TotalAlloca++;
          }
          size_t NumOperands = CI->getNumArgOperands();
          Value *Op[NumOperands];
          Type *OpTy[NumOperands];
          bool Op1Call[NumOperands];
          for(int i = 0; i < NumOperands; i++){
            Op[i] = CI->getArgOperand(i);
            OpTy[i] = Op[i]->getType(); // this should be of float

            if(isa<ConstantFP>(Op[i])){
              TotalAlloca++;
            }
          }
        }
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
      else if(ReturnInst *RT = dyn_cast<ReturnInst>(&I)){
        if (RT->getNumOperands() != 0){
          Value *Op = RT->getOperand(0);
          if(isFloatType(Op->getType())){
            if(isa<ConstantFP>(Op)){
              TotalAlloca++;
            }
          }
        }
      }
    }
  }
  return TotalAlloca;
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
  TotalAlloca = getTotalFPInst(F);
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
  long TotalFPInst = getTotalFPInst(F); 
  if(TotalFPInst > 0){
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
  Finish = M->getOrInsertFunction("fpsan_init", VoidTy);
  long TotIns = 0;

  IRB.CreateCall(Finish);
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
    Function *F) {

  Instruction *I = dyn_cast<Instruction>(CI);
  Module *M = F->getParent();
  Instruction *Next = getNextInstruction(I, BB);
  IRBuilder<> IRBN(Next);
  IRBuilder<> IRB(I);


  Type* Int64Ty = Type::getInt64Ty(M->getContext());
  Type* VoidTy = Type::getVoidTy(M->getContext());

  if(isFloatType(CI->getType())){
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
      Value *OpIdx;
      bool res = handleOperand(Op[i], &OpIdx);
      if(!res){
        errs()<<"\nError !!! metadata not found for operand:\n";
        Op[i]->dump();
        errs()<<"In Inst:"<<"\n";
        I->dump();
        exit(1);
      }
      Constant* ArgNo = ConstantInt::get(Type::getInt64Ty(M->getContext()), i+1);
      if(isFloat(OpTy[i]))
        AddFunArg = M->getOrInsertFunction("fpsan_set_arg_f", VoidTy, Int64Ty, MPtrTy, OpTy[i]);
      else if(isDouble(OpTy[i]))
        AddFunArg = M->getOrInsertFunction("fpsan_set_arg_d", VoidTy, Int64Ty, MPtrTy, OpTy[i]);

      IRB.CreateCall(AddFunArg, {ArgNo, OpIdx, Op[i]});
    }
  }
}

void FPSanitizer::handleStartSlice(CallInst *CI, Function *F) {
  Function::iterator Fit = F->begin();
  BasicBlock &BB = *Fit;
  BasicBlock::iterator BBit = BB.begin();
  Instruction *First = &*BBit;

  Module *M = F->getParent();

  Type *VoidTy = Type::getVoidTy(M->getContext());
  IntegerType *Int32Ty = Type::getInt32Ty(M->getContext());
  Type *PtrVoidTy = PointerType::getUnqual(Type::getInt8Ty(M->getContext()));

  IRBuilder<> IRB(First);

  Finish = M->getOrInsertFunction("fpsan_slice_start", VoidTy);
  IRB.CreateCall(Finish, {});
}

void FPSanitizer::handleEndSlice(CallInst *CI, Function *F) {
  Function::iterator Fit = F->begin();
  BasicBlock &BB = *Fit;
  BasicBlock::iterator BBit = BB.begin();
  Instruction *First = &*BBit;

  Module *M = F->getParent();

  Type *VoidTy = Type::getVoidTy(M->getContext());
  IntegerType *Int32Ty = Type::getInt32Ty(M->getContext());
  Type *PtrVoidTy = PointerType::getUnqual(Type::getInt8Ty(M->getContext()));

  IRBuilder<> IRB(CI);

  Finish = M->getOrInsertFunction("fpsan_slice_end", VoidTy);
  IRB.CreateCall(Finish, {});
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

void FPSanitizer::handleMemset(CallInst *CI, BasicBlock *BB, Function *F, std::string CallName) {
  Instruction *I = dyn_cast<Instruction>(CI);
  Instruction *Next = getNextInstruction(dyn_cast<Instruction>(CI), BB);
  IRBuilder<> IRB(Next);
  Module *M = F->getParent();

  Type *VoidTy = Type::getVoidTy(M->getContext());

  IntegerType *Int32Ty = Type::getInt32Ty(M->getContext());
  IntegerType *Int1Ty = Type::getInt1Ty(M->getContext());
  IntegerType *Int8Ty = Type::getInt8Ty(M->getContext());
  Type *Int64Ty = Type::getInt64Ty(M->getContext());
  Type *PtrVoidTy = PointerType::getUnqual(Type::getInt8Ty(M->getContext()));

  ConstantInt *instId = GetInstId(F, I);
  const DebugLoc &instDebugLoc = I->getDebugLoc();
  bool debugInfoAvail = false;
  unsigned int lineNum = 0;
  unsigned int colNum = 0;
  if (instDebugLoc) {
    debugInfoAvail = true;
    lineNum = instDebugLoc.getLine();
    colNum = instDebugLoc.getCol();
    if (lineNum == 0 && colNum == 0)
      debugInfoAvail = false;
  }
  ConstantInt *debugInfoAvailable = ConstantInt::get(Int1Ty, debugInfoAvail);
  ConstantInt *lineNumber = ConstantInt::get(Int32Ty, lineNum);
  ConstantInt *colNumber = ConstantInt::get(Int32Ty, colNum);

  Value *Op1Addr = CI->getOperand(0);
  Value *Op2Val = CI->getOperand(1);
  Value *size = CI->getOperand(2);
  if (BitCastInst *BI = dyn_cast<BitCastInst>(Op1Addr)) {
    if (checkIfBitcastFromFP(BI)) {
      FuncInit = M->getOrInsertFunction("fpsan_handle_memset", VoidTy,
          PtrVoidTy, Int8Ty, Int64Ty);
      IRB.CreateCall(FuncInit, {Op1Addr, Op2Val, size});
    }
  }
  if (LoadInst *LI = dyn_cast<LoadInst>(Op1Addr)) {
    Value *Addr = LI->getPointerOperand();
    if (BitCastInst *BI = dyn_cast<BitCastInst>(Addr)) {
      if (checkIfBitcastFromFP(BI)) {
        FuncInit = M->getOrInsertFunction("fpsan_handle_memset", VoidTy,
            PtrVoidTy, Int8Ty, Int64Ty);
        IRB.CreateCall(FuncInit, {Addr, Op2Val, size});
      }
    }
  }
}

void
FPSanitizer::handleMemCpy (CallInst *CI,
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

  Value *Op1Addr = CI->getOperand(0);
  Value *Op2Addr = CI->getOperand(1);
  Value *size = CI->getOperand(2);
  BitCastInst*
    BCToAddr1 = new BitCastInst(Op1Addr, 
        PointerType::getUnqual(Type::getInt8Ty(M->getContext())),"", I);
  BitCastInst*
    BCToAddr2 = new BitCastInst(Op2Addr, 
        PointerType::getUnqual(Type::getInt8Ty(M->getContext())),"", I);
  if (BitCastInst *BI = dyn_cast<BitCastInst>(Op1Addr)){
    if(checkIfBitcastFromFP(BI)){
      FuncInit = M->getOrInsertFunction("fpsan_handle_memcpy", VoidTy, PtrVoidTy, PtrVoidTy, Int64Ty);
      IRB.CreateCall(FuncInit, {BCToAddr1, BCToAddr2, size});
    }
  }
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

  if (CallName == "llvm.cos.f64") {
    funcName = "fpsan_mpfr_llvm_cos_f64";
  }
  else if (CallName == "llvm.sin.f64") {
    funcName = "fpsan_mpfr_llvm_sin_f64";
  }
  else if(CallName == "llvm.ceil.f64"){
    funcName = "fpsan_mpfr_llvm_ceil";
  }
  else if(CallName == "llvm.floor.f64"){
    funcName = "fpsan_mpfr_llvm_floor";
  }
  else if(CallName == "llvm.floor.f32"){
    funcName = "fpsan_mpfr_llvm_floor_f";
  }
  else if(CallName == "llvm.fabs.f64"){
    funcName = "fpsan_mpfr_llvm_fabs";
  }
  else {
    funcName = "fpsan_mpfr_"+CallName;
  }  

  for(int i = 0; i < NumOperands; i++){
    Op[i] = CI->getArgOperand(i);
    OpTy[i] = Op[i]->getType(); // this should be of float
    Op1Call[i] = false;
    if(isFloatType(OpTy[i])){
      bool res = handleOperand(Op[i], &ConsIdx[i]);
      if(!res){
        errs()<<"\nError !!! metadata not found for operand:\n";
        Op[i]->dump();
        errs()<<"In Inst:"<<"\n";
        I->dump();
        exit(1);
      }
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
  if(isFloat(CI->getType())){
    FuncInit = M->getOrInsertFunction("fpsan_check_error_f", VoidTy, MPtrTy, CI->getType());
  }
  else{
    FuncInit = M->getOrInsertFunction("fpsan_check_error", VoidTy, MPtrTy, CI->getType());
  }
  IRB.CreateCall(FuncInit, {BOGEP, CI});
}

bool FPSanitizer::handleOperand(Value* OP, Value** ConsInsIndex){
  long Idx = 0;

  Instruction *OpIns = dyn_cast<Instruction>(OP);	

  if(ConsMap.count(OP) != 0){
    *ConsInsIndex = ConsMap.at(OP);
    return true;
  }
  else if(isa<PHINode>(OP)){
    *ConsInsIndex = GEPMap.at(dyn_cast<Instruction>(OP));
    return true;
  }
  else if(MInsMap.count(dyn_cast<Instruction>(OP)) != 0){
    *ConsInsIndex = MInsMap.at(dyn_cast<Instruction>(OP));
    return true;
  }
  else if(isa<Argument>(OP) && (ArgMap.count(dyn_cast<Argument>(OP)) != 0)){
    Idx =  ArgMap.at(dyn_cast<Argument>(OP));
    *ConsInsIndex = MArgMap.at(dyn_cast<Argument>(OP));
    return true;
  }
  else if(isa<FPTruncInst>(OP) || isa<FPExtInst>(OP)){
    Value *OP1 = OpIns->getOperand(0);
    return handleOperand(OP1, ConsInsIndex);
/*
    if(isa<FPTruncInst>(OP1) || isa<FPExtInst>(OP1)){
      Value *OP2 = (dyn_cast<Instruction>(OP1))->getOperand(0);
      if(MInsMap.count(dyn_cast<Instruction>(OP2)) != 0){ //TODO need recursive func
        *ConsInsIndex = MInsMap.at(dyn_cast<Instruction>(OP2));
        return true;
      }
      else{
        return false;
      }
    }
    else if(MInsMap.count(dyn_cast<Instruction>(OP1)) != 0){
      *ConsInsIndex = MInsMap.at(dyn_cast<Instruction>(OP1));
      return true;
    }
    else if(ConsMap.count(OP1) != 0){
      *ConsInsIndex = ConsMap.at(OP1);
      return true;
    }
    else{
      return false;
    }
    */
  }
  else if(isa<UndefValue>(OP)){
    *ConsInsIndex = UndefValue::get(MPtrTy);
    return true;
  }
  else{
    return false;
  }
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
  BitCastInst*
    BCToAddr = new BitCastInst(Addr, 
        PointerType::getUnqual(Type::getInt8Ty(M->getContext())),"", I);
  if (BitCastInst *BI = dyn_cast<BitCastInst>(Addr)){
    BTFlag = checkIfBitcastFromFP(BI);
  }
  if(isFloatType(StoreTy) || BTFlag){
    Value* InsIndex;
    bool res = handleOperand(OP, &InsIndex);
    if(res){ //handling registers
      SetRealTemp = M->getOrInsertFunction("fpsan_store_shadow", VoidTy, PtrVoidTy, MPtrTy);
      IRB.CreateCall(SetRealTemp, {BCToAddr, InsIndex});
    }
    else{
      if(isFloat(StoreTy)){
        SetRealTemp = M->getOrInsertFunction("fpsan_store_shadow_fconst",
            VoidTy, PtrVoidTy, OpTy, Int32Ty);
        IRB.CreateCall(SetRealTemp, {BCToAddr, OP, lineNumber});
      }
      else if(isDouble(StoreTy)){
        SetRealTemp = M->getOrInsertFunction("fpsan_store_shadow_dconst",
            VoidTy, PtrVoidTy, OpTy, Int32Ty);
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
        Value* InsIndex;
        bool res = handleOperand(IncValue, &InsIndex);
        if(!res){
          errs()<<"handleNewPhi:Error !!! metadata not found for operand:\n";
          IncValue->dump();
          errs()<<"In Inst:"<<"\n";
          it->first->dump();
          exit(1);
        }
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
  // Wherever old phi node has been used, we need to replace it with
  //new phi node. That's why need to track it and keep it in RegIdMap
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

  Value *OP1 = SI->getOperand(0);

  Value* InsIndex2, *InsIndex3;
  bool res1 = handleOperand(SI->getOperand(1), &InsIndex2);
  if(!res1){
    errs()<<"\nhandleSelect: Error !!! metadata not found for op:"<<"\n";
    SI->getOperand(1)->dump();
    errs()<<"In Inst:"<<"\n";
    I->dump();
    exit(1);
  }
  bool res2 = handleOperand(SI->getOperand(2), &InsIndex3);
  if(!res2){
    errs()<<"\nhandleSelect: Error !!! metadata not found for op:"<<"\n";
    SI->getOperand(1)->dump();
    errs()<<"In Inst:"<<"\n";
    I->dump();
    exit(1);
  }

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
      bool res = handleOperand(OP, &OpIdx);
      if(!res){
        errs()<<"\nhandleReturn: Error !!! metadata not found for op:"<<"\n";
        OP->dump();
        errs()<<"In Inst:"<<"\n";
        OP->dump();
        exit(1);
      }
      long TotalArgs = FuncTotalArg.at(F);
      Constant* ArgNo = ConstantInt::get(Type::getInt64Ty(M->getContext()), 0); // 0 for return
      Constant* TotalArgsConst = ConstantInt::get(Type::getInt64Ty(M->getContext()), TotalArgs); 
      AddFunArg = M->getOrInsertFunction("fpsan_set_return", VoidTy, MPtrTy, Int64Ty, OP->getType());
      IRB.CreateCall(AddFunArg, {OpIdx, TotalArgsConst, OP});
      return;
    }

  }
  FuncInit = M->getOrInsertFunction("fpsan_func_exit", VoidTy, Int64Ty);
  long TotalArgs = FuncTotalArg.at(F);
  Constant* ConsTotIns = ConstantInt::get(Type::getInt64Ty(M->getContext()), TotalArgs); 
  IRB.CreateCall(FuncInit, {ConsTotIns});
}

void FPSanitizer::handleFNeg(UnaryOperator *UO, BasicBlock *BB, Function *F) {
  Instruction *I = dyn_cast<Instruction>(UO);
  Instruction *Next = getNextInstruction(I, BB);
  IRBuilder<> IRB(Next);
  Module *M = F->getParent();

  Value *InsIndex1;
  bool res1 = handleOperand(UO->getOperand(0), &InsIndex1);
  if (!res1) {
    errs() << *F << "\n";
    errs() << "handleBinOp: Error !!! metadata not found for op:"
      << "\n";
    errs() << *UO->getOperand(0);
    errs() << "In Inst:"
      << "\n";
    errs() << *I;
    exit(1);
  }
  Type *VoidTy = Type::getVoidTy(M->getContext());
  IntegerType *Int32Ty = Type::getInt32Ty(M->getContext());

  ConstantInt *instId = GetInstId(F, I);
  const DebugLoc &instDebugLoc = I->getDebugLoc();
  bool debugInfoAvail = false;
  unsigned int lineNum = 0;
  unsigned int colNum = 0;
  if (instDebugLoc) {
    debugInfoAvail = true;
    lineNum = instDebugLoc.getLine();
    colNum = instDebugLoc.getCol();
    if (lineNum == 0 && colNum == 0)
      debugInfoAvail = false;
  }
  ConstantInt *lineNumber = ConstantInt::get(Int32Ty, lineNum);

  Value *BOGEP = GEPMap.at(I);

  std::string opName(I->getOpcodeName());

  ComputeReal = M->getOrInsertFunction("fpsan_mpfr_fneg", VoidTy, MPtrTy, MPtrTy, Int32Ty);

  IRB.CreateCall(ComputeReal, {InsIndex1, BOGEP, lineNumber});
  MInsMap.insert(std::pair<Instruction *, Instruction *>(I, dyn_cast<Instruction>(BOGEP)));
}

void FPSanitizer::handleBinOp(BinaryOperator* BO, BasicBlock *BB, Function *F){
  Instruction *I = dyn_cast<Instruction>(BO);
  Instruction *Next = getNextInstruction(I, BB);
  IRBuilder<> IRB(Next);
  Module *M = F->getParent();

  Value* InsIndex1, *InsIndex2; 
  bool res1 = handleOperand(BO->getOperand(0), &InsIndex1);
  if(!res1){
    errs()<<"handleBinOp: Error !!! metadata not found for op:"<<"\n";
    BO->getOperand(0)->dump();
    errs()<<"In Inst:"<<"\n";
    I->dump();
    exit(1);
  }

  bool res2 = handleOperand(BO->getOperand(1), &InsIndex2);
  if(!res2){
    errs()<<"handleBinOp: Error !!! metadata not found for op:"<<"\n";
    BO->getOperand(1)->dump();
    errs()<<"In Inst:"<<"\n";
    I->dump();
    exit(1);
  }
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

  if(isFloat(BO->getType())){
    ComputeReal = M->getOrInsertFunction("fpsan_mpfr_"+opName+"_f", VoidTy, MPtrTy, MPtrTy, MPtrTy, BOType, BOType,
        BOType, Int64Ty, Int1Ty, Int32Ty, Int32Ty);
    FuncInit = M->getOrInsertFunction("fpsan_check_error_f", VoidTy, MPtrTy, BOType);
  }
  else if(isDouble(BO->getType())){
    ComputeReal = M->getOrInsertFunction("fpsan_mpfr_"+opName, VoidTy, MPtrTy, MPtrTy, MPtrTy, BOType, BOType,
        BOType, Int64Ty, Int1Ty, Int32Ty, Int32Ty);
    FuncInit = M->getOrInsertFunction("fpsan_check_error", VoidTy, MPtrTy, BOType);
  }

  IRB.CreateCall(ComputeReal, {InsIndex1, InsIndex2, BOGEP, BO->getOperand(0), BO->getOperand(1), BO, 
      instId, debugInfoAvailable, lineNumber, colNumber});
  MInsMap.insert(std::pair<Instruction*, Instruction*>(I, dyn_cast<Instruction>(BOGEP)));
  IRB.CreateCall(FuncInit, {BOGEP, BO});
}

void FPSanitizer::handleFPTrunc(FPTruncInst *FPT, BasicBlock *BB, Function *F){

  Instruction *I = dyn_cast<Instruction>(FPT);
  Instruction *Next = getNextInstruction(I, BB);
  IRBuilder<> IRB(Next);
  Module *M = F->getParent();

  Value *InsIndex1;
  bool res1 = handleOperand(FPT->getOperand(0), &InsIndex1);
  if(!res1){
    errs()<<"handleFPTrunc: Error !!! metadata not found for op:"<<"\n";
    FPT->getOperand(0)->dump();
    errs()<<"In Inst:"<<"\n";
    I->dump();
    exit(1);
  }
  Type* VoidTy = Type::getVoidTy(M->getContext());

  CheckBranch = M->getOrInsertFunction("fpsan_handle_fptrunc", VoidTy, FPT->getType(), MPtrTy);
  IRB.CreateCall(CheckBranch, {FPT, InsIndex1});
}

void FPSanitizer::handleFcmp(FCmpInst *FCI, BasicBlock *BB, Function *F){

  Instruction *I = dyn_cast<Instruction>(FCI);
  Instruction *Next = getNextInstruction(I, BB);
  IRBuilder<> IRB(Next);
  Module *M = F->getParent();

  Value *InsIndex1, *InsIndex2;
  bool res1 = handleOperand(FCI->getOperand(0), &InsIndex1);
  if(!res1){
    errs()<<"handleFcmp: Error !!! metadata not found for op:"<<"\n";
    FCI->getOperand(0)->dump();
    errs()<<"In Inst:"<<"\n";
    I->dump();
    exit(1);
  }
  bool res2 = handleOperand(FCI->getOperand(1), &InsIndex2);
  if(!res2){
    errs()<<"handleFcmp: Error !!! metadata not found for op:"<<"\n";
    FCI->getOperand(1)->dump();
    errs()<<"In Inst:"<<"\n";
    I->dump();
    exit(1);
  }
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
  else if (FPTruncInst *FPT = dyn_cast<FPTruncInst>(I)){
    handleFPTrunc(FPT, BB, F);
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
  else if (UnaryOperator *UO = dyn_cast<UnaryOperator>(I)) {
    switch (UO->getOpcode()) {
      case Instruction::FNeg: 
        {
          handleFNeg(UO, BB, F);
        }
    }
  }
  else if (BinaryOperator* BO = dyn_cast<BinaryOperator>(I)){
    switch(BO->getOpcode()) {                                                                                                                                         
      case Instruction::FAdd:                                                                        
      case Instruction::FSub:
      case Instruction::FMul:
      case Instruction::FDiv:
        {
          handleBinOp(BO, BB, F);
        } 
    }
  }
  else if (CallInst *CI = dyn_cast<CallInst>(I)){
    Function *Callee = CI->getCalledFunction();
    if (Callee) {
      if(Callee->getName().startswith("llvm.memcpy"))
        handleMemCpy(CI, BB, F, Callee->getName());
      else if (Callee->getName().startswith("llvm.memset"))
        handleMemset(CI, BB, F, Callee->getName());
      else if(isListedFunction(Callee->getName(), "mathFunc.txt"))
        handleMathLibFunc(CI, BB, F, Callee->getName());
      else if(isListedFunction(Callee->getName(), "functions.txt")){
        handleCallInst(CI, BB, F);
      }
    }
    else if(CallSite(I).isIndirectCall()){
      handleCallInst(CI, BB, F);
    }
  }     
}

bool FPSanitizer::runOnModule(Module &M) {

  LLVMContext &C = M.getContext();


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
	Type::getInt1Ty(M.getContext()),

	// All the tracing data types start here 
	Type::getInt32Ty(M.getContext()),
	
	Type::getInt64Ty(M.getContext()),
	Type::getInt64Ty(M.getContext()),
	
	Type::getInt64Ty(M.getContext()),
	Type::getInt64Ty(M.getContext()),
	RealPtr,
	
	Type::getInt64Ty(M.getContext()),
	Type::getInt64Ty(M.getContext()),
	RealPtr,
	
	Type::getInt64Ty(M.getContext())

	});

  MPtrTy = Real->getPointerTo();
  

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

  for (auto &F : M) {
    if (F.isDeclaration()) continue;
    for (auto &BB : F) {
      for (auto &I : BB) {
        if (CallInst *CI = dyn_cast<CallInst>(&I)){
          Function *Callee = CI->getCalledFunction();
          if (Callee) {
            if (Callee->getName().startswith("end_slice")) {
              handleEndSlice(CI, &F);
            } else if (Callee->getName().startswith("start_slice")) {
              handleStartSlice(CI, &F);
            }
          }
        }     
      }
    }
  }
  int instId = 0;
  //instrument interesting instructions
  Instruction *LastPhi = NULL;
  for (Function *F : reverse(AllFuncList)) {
    //give unique indexes to instructions and instrument with call to
    //dynamic lib
    createMpfrAlloca(F);

    //if argument is used in any floating point computation, then we
    //need to retrieve that argument from shadow stack.  Instead of
    //call __get_arg everytime opearnd is used, it is better to call
    //once in start of the function and remember the address of shadow
    //stack.
    callGetArgument(F);

    if(F->getName() != "main"){
      //add func_init and func_exit in the start and end of the
      //function to set shadow stack variables
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
        if(F->getName() != "main"){
          if (ReturnInst *RI = dyn_cast<ReturnInst>(&I)){
            handleReturn(RI, &BB, F);
          }
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
