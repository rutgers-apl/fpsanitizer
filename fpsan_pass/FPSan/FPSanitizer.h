//===-FPSanitizer.h  - Interface ---------------------------------*- C++ -*-===//
//
//
//
//===----------------------------------------------------------------------===//
//
// This pass instruments floating point instructions by inserting
// calls to the runtime to perform shadow execution with arbitrary
// precision. 
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include <fstream>
#include <queue>

using namespace llvm;

namespace {
  struct FPSanitizer : public ModulePass {
  
  public:
    FPSanitizer() : ModulePass(ID) {}

    virtual bool runOnModule(Module &module);
    void createInitMpfr(Value* BOGEP, Function *F, AllocaInst *Alloca, size_t index);
    void createInitAndSetMpfr(Value* BOGEP, Function *F, AllocaInst *Alloca, size_t index, Value *OP);
    void createInitAndSetP32(Value* BOGEP, Function *F, AllocaInst *Alloca, size_t index, Value *OP);
    void instrumentAllFunctions(std::string FN);
    void createMpfrAlloca(Function *F);
    void callGetArgument(Function *F);
    AllocaInst *createAlloca(Function *F, size_t InsCount);
    void createGEP(Function *F, AllocaInst *Alloca, long TotalAlloca);
    void clearAlloca(Function *F, size_t InsCount);
    Instruction* getNextInstruction(Instruction *I, BasicBlock *BB);
    Instruction* getNextInstructionNotPhi(Instruction *I, BasicBlock *BB);
    void findInterestingFunctions(Function *F);
    bool handleOperand(Instruction *I, Value* OP, Function *F, Value **InstIdx);
    void handleStore(StoreInst *SI, BasicBlock *BB, Function *F);
    void handleNewPhi(Function *F);
    void copyPhi(Instruction *I, Function *F);
    void handlePhi(PHINode *PN, BasicBlock *BB, Function *F);
    void handleSelect(SelectInst *SI, BasicBlock *BB, Function *F);
    void handleBinOp(BinaryOperator* BO, BasicBlock *BB, Function *F);
    void handleFcmp(FCmpInst *FCI, BasicBlock *BB, Function *F);
    void handleReturn(ReturnInst *RI, BasicBlock *BB, Function *F);
    bool checkIfBitcastFromFP(BitCastInst *BI);
    void handleLoad(LoadInst *LI, BasicBlock *BB, Function *F);
    void handleMathLibFunc(CallInst *CI, BasicBlock *BB, Function *F, std::string Name);
    void handlePositLibFunc(CallInst *CI, BasicBlock *BB, Function *F, std::string Name);
		void handleCallInst (CallInst *CI, BasicBlock *BB, Function *F, std::string CallName);
    bool isListedFunction(StringRef FN, std::string FileName);
    void addFunctionsToList(std::string FN);
    bool isFloatType(Type *InsType);
    bool isFloat(Type *InsType);
    bool isDouble(Type *InsType);
    void handleMainRet(Instruction *I, Function *F);
    void handleFuncInit(Function *F);
    void handleFuncMainInit(Function *F);
    void handleInit(Module *M);
    void handleIns(Instruction *I, BasicBlock *BB, Function *F);
    long getTotalFPInst(Function *F);
    ConstantInt* GetInstId(Function *F, Instruction* I);
    StructType *MPFRTy;
    Type *MPtrTy;
    Type *RealPtr;
    StructType* Real;
    std::map<Value*, Value*> ConsMap;
    std::map<Instruction*, Value*> GEPMap;
    std::map<ConstantFP*, Value*> ConstantMap;
    //map new instruction with old esp. for select and phi
    std::map<Instruction*, Instruction*> RegIdMap;
    std::map<Instruction*, Instruction*> NewPhiMap;
    //track unique index for instructions
    std::map<Instruction*, Instruction*> MInsMap;
    std::map<Argument*, Instruction*> MArgMap;
    //Arguments can not be instruction, so we need a seperate map to hold indexes for arguments
    std::map<Argument*, size_t> ArgMap;
    std::map<Function*, size_t> FuncTotalArg;
    //list of all functions need to be instrumented
    SmallVector<Function*, 8> AllFuncList;
    SmallVector<Value*, 8> AllocaInstList;
    SmallVector<Argument*, 8> ArgList;
    static char ID; // Pass identification
    long InsCount = 0;

  private:
    FunctionCallee Func;
    FunctionCallee LoadCall;
    FunctionCallee ComputeReal;
    FunctionCallee FuncExit;
    FunctionCallee CheckBranch;
    FunctionCallee FuncInit;
    FunctionCallee Finish;
    FunctionCallee HandleFunc;
    FunctionCallee SetRealTemp;
    FunctionCallee AddFunArg;
  };
}

