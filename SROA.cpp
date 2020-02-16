//===- ScalarReplAggregates.cpp - Scalar Replacement of Aggregates --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This transformation implements the well known scalar replacement of
// aggregates transformation.  This xform breaks up alloca instructions of
// structure type into individual alloca instructions for
// each member (if possible).  Then, if possible, it transforms the individual
// alloca instructions into nice clean scalar SSA form.
//
// This combines an SRoA algorithm with Mem2Reg because they
// often interact, especially for C++ programs.  As such, this code
// iterates between SRoA and Mem2Reg until we run out of things to promote.
//
//===----------------------------------------------------------------------===//


#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/PtrUseVisitor.h"
#include "llvm/Transforms/Utils/Local.h"


#include "llvm/Transforms/Utils/PromoteMemToReg.h"

#include <vector>

using namespace llvm;

STATISTIC(NumReplaced,  "Number of aggregate allocas broken up");
STATISTIC(NumPromoted,  "Number of scalar allocas promoted to register");

// Define the pass
namespace {
    struct SROA : public FunctionPass {
        static char ID; // Pass identification
        SROA() : FunctionPass(ID) { }

        // Entry point for the overall scalar-replacement pass
        bool runOnFunction(Function &F);

        // getAnalysisUsage - List passes required by this pass.
        virtual void getAnalysisUsage(AnalysisUsage &AU) const {
            AU.addRequired<AssumptionCacheTracker>();
            AU.addRequired<DominatorTreeWrapperPass>();
            AU.setPreservesCFG();
        }
    };
}

// Register the pass
char SROA::ID = 0;
static RegisterPass<SROA> X("scalarrepl-akashk4",
			    "Scalar Replacement of Aggregates (by <akashk4>)",
			    false /* does not modify the CFG */,
			    false /* transformation, not just analysis */);

// Public interface to create the ScalarReplAggregates pass.
FunctionPass *createMyScalarReplAggregatesPass() { return new SROA(); }


// Invoke the Mem2reg pass
static  bool PromoteAllocas(std::vector<AllocaInst *> &AllocaList, Function &F, 
                            DominatorTree &DT, AssumptionCache &AC) {
    if(AllocaList.empty())
        return false;

    NumPromoted += PromotableAllocas.size();
    PromoteMemToReg(PromotableAllocas, DT, &AC);
    AllocaList.clear();
    return true;
}

static bool isPromotable(const Instruction *I) {
     for(const auto *U : I->users()) {
        if(const auto *LI = dyn_cast<LoadInst>(U)) {
            if(LI->isVolatile())
                return false;
            continue;
        }
        if(const auto *SI = dyn_cast<StoreInst>(U)) {
            if(SI->getOperand(0) == I || SI->isVolatile())
                return false;
            continue;
        }
        if(const auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
            // All indices should be constants
            if(!isa<ConstantInt>(GEP->getOperand(1)) 
            || !isa<ConstantInt>(GEP->getOperand(2))) {
                return false;
            }   
            continue;
        }
        //if(const auto *BCI = dyn_cast<BitCastInst>(U)) {
          //  if(!isPromotable(BCI))
            //    return false;
            //continue;
        //}
        if(const auto *II = dyn_cast<IntrinsicInst>(U)) {
            if(!II->isLifetimeStartOrEnd())
                return false;
            continue;
        }
        return false;
    }
    return true;
}

static bool isAllocaPromotable(const AllocaInst *AI) {
   for(const auto *U : AI->users()) {
        if(const auto *LI = dyn_cast<LoadInst>(U)) {
            if(LI->isVolatile())
                return false;
            continue;
        }
        if(const auto *SI = dyn_cast<StoreInst>(U)) {
            if(SI->getOperand(0) == I || SI->isVolatile())
                return false;
            continue;
        }
        if(const auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
            if(GEP->getType() != Type::getInt8PtrTy(U->getContext(), AI->getAddressSpace()))
                return false;
            if(!GEP->hasAllZeroIndices())
                return false;
            if(!onlyUsedByLifetimeMarkers(GEP))
                return false;
            continue;
        }
        //if(const auto *BCI = dyn_cast<BitCastInst>(U)) {
          //  if(!isPromotable(BCI))
            //    return false;
            //continue;
        //}
        if(const auto *II = dyn_cast<IntrinsicInst>(U)) {
            if(!II->isLifetimeStartOrEnd())
                return false;
            continue;
        }
        return false;
    }
    return true;
}

// This split the GEPs with more than 2 indices into multiple GEPs with 2 indices.
//static void SplitGEPs(AllocaInst &AI) {
 //   for(const auto *U : AI.users()) {
 //       if(const auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
  //          if(GEP->)
  //      }
  //  }
//}

// Extracts offsets
static void ExtractOffsets(AllocaInst &AI, DenseMap<uint64_t, 
            DenseMap<uint64_t, SmallVector<GetElementPtrInst *, 4>> &OffsetsGEPsMap) {
    for(const auto *U : AI.users()) {
         if(const auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
            auto *Offset = cast<ConstantInt>(GEP->getOperand(2));
            OffsetsGEPMap[Offset->getZExtValue()].push_back(GEP);
        }
    }
}

static bool AnalyzeAlloca(AllocaInst *AI, SmallVector<AllocaInst *, 4> &Worklist) {
    // If alloca has no use, skip it
    if(AI->use_empty())
        return false;

    // Skip any alloca which is not a struct or a small array
    if(!AI->getAllocatedType()->isStructTy() 
    || (AI->isArrayAllocation() && AI->)) {
        return false;
    }
    
    // Is this alloca promotable?
    if(!isAllocaPromotable(AI))
        return false;

    // Now, we extract specific elements of the aggregate alloca
    // and use them separately.
    DenseMap<uint64_t, SmallVector<GetElementPtrInst *, 4>> OffsetsGEPsMap;
    ExtractOffsets(AI, OffsetsGEPsMap);

    // Deal with the alloca one offset at a time
    auto *FirstInst = AI->getParent()->getFirstNonPHI();
    for(auto *Entry : OffsetsGEPsMap) {
        uint64_t Offset = Entry.first;
        auto *GEPVect = Entry.second;
        
        // Create an alloca for element at given offset
        Type *AllocType;
        if(auto *SeqAllocType = dyn_cast<SequentialType>(AI->getAllocatedType())) {
            AllocType = SeqAllocType->getElementType();
        } else {
            // Its composite type
            auto *CompAllocType = dyn_cast<CompositeTypeType>(AI->getAllocatedType());
            assert(CompAllocType && "Alloca should be of conposite type.");
            AllocType = CompAllocType->getTypeAtIndex(Offset);
        }
        auto *NewAlloca = new AllocaInst(AllocType, 
                            AI->getType()->getAddressSpace(), "", FirstInst);
        NumReplaced++;
        
        // Replace the uses of this GEP with the new Alloca
        for(auto *GEP : GEPVect)
            GEP->replaceAllUsesWith(NewAlloca);
    
        // Add the new alloca to the worklist
        Worklist.push_back(NewAlloca);
    }

    // Invalidate and remove the old alloca
    if(!OffsetsGEPsMap.empty())
        AI->replaceAllUsesWith(UndefValue::get(AI->getType()));
    AI->eraseFromParent();

    return true;
}

static bool RunOnFunction(Function &F, DominatorTree &DT,
                                       AssumptionCache &AC) {
    // Get all allocas first
    SmallVector<AllocaInst *, 4> Worklist;
    for(auto &I : F.getEntryBlock()) {
        if(auto *AI = dyn_cast<AllocaInst>(I))
            Worklist.push_back(AI);
    }
    bool Changed = false;
    SmallVector<AllocaInst *, 4> TempWorklist;
    do {
        while(!Worklist.empty())
            Changed |= AnalyzeAlloca(Worklist.pop_back_val(), TempWorklist);
        std::vector<AllocaInst *> AllocaList;
        for(auto *AI : TempWorklist) {
            if(isAllocaPromotable(AI))
                AllocaList.push_back(AI);
        }
        Changed |= PromoteAllocas(AllocaList, F, DT, AC);
        Worklist = TempWorklist;
        TempWorklist.clear();
    } while(!Worklist.empty());

    return Changed;
}

bool SROA::runOnFunction(Function &F) {
 // Get dominator tree and assumptions cache
    auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    auto &AC = getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);

    // Run the analysis
    bool Changed = RunOnFunction(F, DT, AC);

    // Print the stats
    errs() << "Number of aggregate allocas broken up: " << NumReplaced << "\n";
    errs() << "Number of scalar allocas promoted to register: " << NumPromoted << "\n";

    return Changed;
}