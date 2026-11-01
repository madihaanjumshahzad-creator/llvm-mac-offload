// MacOffload.cpp - LLVM 18 pass plugin, two function passes
//
// mac-report:  for each innermost loop print loads, stores, bytes and MACs
//              per iteration (how much data moves per MAC)
// mac-offload: find  for (i = 0; i < n; i++) s += (int32_t)a[i] * (int32_t)b[i];
//              with int8 a, b and replace the loop by
//              s = s_init + mac_accel_dot(a, b, n);
//
// opt -load-pass-plugin=./build/MacOffload.so -passes=mac-offload in.ll -S -o out.ll

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"

using namespace llvm;
using namespace llvm::PatternMatch;

namespace {

// mac-report
struct MacReportPass : PassInfoMixin<MacReportPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    auto &LI = FAM.getResult<LoopAnalysis>(F);
    const DataLayout &DL = F.getParent()->getDataLayout();

    for (Loop *L : LI.getLoopsInPreorder()) {
      if (!L->isInnermost())
        continue;
      unsigned Loads = 0, Stores = 0, Macs = 0;
      uint64_t Bytes = 0;
      for (BasicBlock *BB : L->blocks())
        for (Instruction &I : *BB) {
          if (auto *LdI = dyn_cast<LoadInst>(&I)) {
            Loads++;
            Bytes += DL.getTypeStoreSize(LdI->getType());
          } else if (auto *St = dyn_cast<StoreInst>(&I)) {
            Stores++;
            Bytes += DL.getTypeStoreSize(St->getValueOperand()->getType());
          } else if (I.getOpcode() == Instruction::Add ||
                     I.getOpcode() == Instruction::FAdd) {
            // add of a mul = 1 MAC
            for (Value *Op : I.operands())
              if (auto *M = dyn_cast<BinaryOperator>(Op))
                if (M->getOpcode() == Instruction::Mul ||
                    M->getOpcode() == Instruction::FMul) {
                  Macs++;
                  break;
                }
          }
        }
      errs() << "mac-report: " << F.getName() << ", loop at "
             << L->getHeader()->getName() << ": loads=" << Loads
             << " stores=" << Stores << " bytes/iter=" << Bytes
             << " MACs/iter=" << Macs;
      if (Macs)
        errs() << " bytes/MAC=" << format("%.1f", double(Bytes) / Macs);
      errs() << "\n";
    }
    return PreservedAnalyses::all();
  }
};

// mac-offload
struct DotLoop {
  PHINode *Acc = nullptr;      // accumulator phi in the header
  Instruction *Sum = nullptr;  // acc + a[i]*b[i], value leaving the loop
  Value *BaseA = nullptr, *BaseB = nullptr;
};

// sext(load i8 (gep i8 Base, IV)) -> Base, else null
static Value *matchInt8Element(Value *V, PHINode *IV, Loop *L) {
  Value *Ld;
  if (!match(V, m_SExt(m_Value(Ld))))
    return nullptr;
  auto *Load = dyn_cast<LoadInst>(Ld);
  if (!Load || !Load->isSimple() || !Load->getType()->isIntegerTy(8))
    return nullptr;
  auto *GEP = dyn_cast<GetElementPtrInst>(Load->getPointerOperand());
  if (!GEP || GEP->getNumIndices() != 1 ||
      !GEP->getSourceElementType()->isIntegerTy(8))
    return nullptr;
  Value *Idx = GEP->getOperand(1);
  if (auto *Ext = dyn_cast<CastInst>(Idx))  // index can be extended
    Idx = Ext->getOperand(0);
  if (Idx != IV)
    return nullptr;
  Value *Base = GEP->getPointerOperand();
  return L->isLoopInvariant(Base) ? Base : nullptr;
}

static bool matchDotLoop(Loop *L, ScalarEvolution &SE, DotLoop &D) {
  // only one block, IV from 0 with step 1
  if (L->getNumBlocks() != 1 || !L->getLoopPreheader() || !L->getExitBlock())
    return false;
  PHINode *IV = L->getCanonicalInductionVariable();  // phi [0, pre], [iv+1, latch]
  if (!IV)
    return false;
  auto *IVRec = dyn_cast<SCEVAddRecExpr>(SE.getSCEV(IV));
  if (!IVRec || !IVRec->getStart()->isZero() || !IVRec->getStepRecurrence(SE)->isOne())
    return false;

  BasicBlock *H = L->getHeader();
  // no stores or calls in the loop
  for (Instruction &I : *H)
    if (I.mayWriteToMemory() || isa<CallBase>(&I))
      return false;

  for (PHINode &P : H->phis()) {
    if (&P == IV || !P.getType()->isIntegerTy(32))
      continue;
    auto *Sum = dyn_cast<Instruction>(P.getIncomingValueForBlock(L->getLoopLatch()));
    Value *X, *Y;
    if (!Sum || !match(Sum, m_c_Add(m_Mul(m_Value(X), m_Value(Y)), m_Specific(&P))))
      continue;
    Value *A = matchInt8Element(X, IV, L), *B = matchInt8Element(Y, IV, L);
    if (!A || !B)
      continue;
    // sum can only be used by the phi and the exit phis
    bool UsesOk = true;
    for (User *U : Sum->users())
      if (U != &P && !(isa<PHINode>(U) &&
                       cast<Instruction>(U)->getParent() == L->getExitBlock()))
        UsesOk = false;
    if (!UsesOk)
      continue;
    D.Acc = &P;
    D.Sum = Sum;
    D.BaseA = A;
    D.BaseB = B;
    return true;
  }
  return false;
}

struct MacOffloadPass : PassInfoMixin<MacOffloadPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    auto &LI = FAM.getResult<LoopAnalysis>(F);
    auto &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);
    auto &DT = FAM.getResult<DominatorTreeAnalysis>(F);
    Module &M = *F.getParent();
    LLVMContext &Ctx = M.getContext();

    SmallVector<Loop *, 4> Work;
    for (Loop *L : LI.getLoopsInPreorder())
      if (L->isInnermost())
        Work.push_back(L);

    bool Changed = false;
    for (Loop *L : Work) {
      // at -O1 the loop can start right from entry, so add a preheader for the call
      if (!L->getLoopPreheader() && L->getNumBlocks() == 1 &&
          InsertPreheaderForLoop(L, &DT, &LI, nullptr, false)) {
        SE.forgetLoop(L);
        Changed = true;
      }
      DotLoop D;
      if (!matchDotLoop(L, SE, D))
        continue;
      const SCEV *BTC = SE.getBackedgeTakenCount(L);
      if (isa<SCEVCouldNotCompute>(BTC))
        continue;

      BasicBlock *Pre = L->getLoopPreheader(), *H = L->getHeader(),
                 *Exit = L->getExitBlock();

      // if another value (e.g. the IV) is used after the loop, skip it
      bool OnlySumLeaves = true;
      for (PHINode &EP : Exit->phis())
        if (EP.getBasicBlockIndex(H) >= 0 && EP.getIncomingValueForBlock(H) != D.Sum)
          OnlySumLeaves = false;
      if (!OnlySumLeaves)
        continue;

      // n = backedge taken count + 1
      Type *I32 = Type::getInt32Ty(Ctx);
      const SCEV *Trip = SE.getAddExpr(BTC, SE.getOne(BTC->getType()));
      SCEVExpander Exp(SE, M.getDataLayout(), "mac.n");
      Instruction *InsPt = Pre->getTerminator();
      Value *N = Exp.expandCodeFor(Trip, Trip->getType(), InsPt);
      IRBuilder<> B(InsPt);
      N = B.CreateZExtOrTrunc(N, I32, "mac.n32");

      FunctionCallee Fn = M.getOrInsertFunction(
          "mac_accel_dot", I32, PointerType::getUnqual(Ctx),
          PointerType::getUnqual(Ctx), I32);
      Value *Dot = B.CreateCall(Fn, {D.BaseA, D.BaseB, N}, "mac.dot");
      Value *Res = B.CreateAdd(D.Acc->getIncomingValueForBlock(Pre), Dot, "mac.sum");

      // preheader jumps straight to the exit, old loop becomes dead
      for (PHINode &EP : Exit->phis())
        EP.addIncoming(Res, Pre);
      H->removePredecessor(Pre);
      Pre->getTerminator()->replaceUsesOfWith(H, Exit);

      errs() << "mac-offload: " << F.getName() << ": int8 dot-product loop at "
             << H->getName() << " replaced by mac_accel_dot\n";
      Changed = true;
    }

    if (!Changed)
      return PreservedAnalyses::all();
    EliminateUnreachableBlocks(F);
    return PreservedAnalyses::none();
  }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "MacOffload", "0.1", [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "mac-offload") {
                    FPM.addPass(MacOffloadPass());
                    return true;
                  }
                  if (Name == "mac-report") {
                    FPM.addPass(MacReportPass());
                    return true;
                  }
                  return false;
                });
          }};
}
