//===--------- SROA.cpp - Scalar Replacement of Aggregates -----------------===//
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

#define DEBUG_TYPE "scalarrepl"

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
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Support/Casting.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/PtrUseVisitor.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"

#include <vector>
#include <map>

using namespace llvm;

namespace {
  struct SROA : public FunctionPass {
    static char ID; // Pass identification
    SROA() : FunctionPass(ID) { }

    // Entry point for the overall scalar-replacement pass
    bool runOnFunction(Function &F);

    // getAnalysisUsage - List passes required by this pass.  We also know it
    // will not alter the CFG, so say so.
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
        AU.addRequired<AssumptionCacheTracker>();
     AU.addRequired<DominatorTreeWrapperPass>();
	AU.setPreservesCFG();
    }
  };
}

char SROA::ID = 0;
static RegisterPass<SROA> X("scalarrepl-akashk4",
			    "Scalar Replacement of Aggregates (by akashk4)",
			    false /* does not modify the CFG */,
			    false /* transformation, not just analysis */);


// Public interface to create the ScalarReplAggregates pass.
// This function is provided to you.
FunctionPass *createMyScalarReplAggregatesPass() { return new SROA(); }

STATISTIC(NumReplaced,  "Number of aggregate allocas broken up");
STATISTIC(NumPromoted,  "Number of scalar allocas promoted to register");

// Invoke the Mem2reg pass
static  bool PromoteAllocas(std::vector<AllocaInst *> &AllocaList, Function &F, 
                            DominatorTree &DT, AssumptionCache &AC) {
    if(AllocaList.empty())
        return false;
    NumPromoted += AllocaList.size();
    PromoteMemToReg(AllocaList, DT, &AC);
    return true;
}

// Perfoms some analysis as to whether SROA should be performed on an alloca.
static bool isPromotable(const Instruction *I) {
     for(const auto *U : I->users()) {
        if(const auto *LI = dyn_cast<LoadInst>(U)) {
            errs() << "--LOAD: " << *LI << "\n";
            if(LI->isVolatile())
                return false;
            errs() << "LOAD: " << *LI << "\n";
            continue;
        }
        if(const auto *SI = dyn_cast<StoreInst>(U)) {
            errs() << "--STORE: " << *SI << "\n";
            if(SI->getOperand(0) == I || SI->isVolatile())
                return false;
            errs() << "STORE: " << *SI << "\n";
            continue;
        }
        if(const auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
            errs() << "--GEP: " << *GEP << "\n";
            // All indices should be constants
            if(!isa<ConstantInt>(GEP->getOperand(1)) 
            || !isa<ConstantInt>(GEP->getOperand(2))) {
                return false;
            }
            errs() << "GEP: " << *GEP << "\n";
            if(!dyn_cast<PointerType>(GEP->getType())->getElementType()->isPointerTy()) {
                if(!isPromotable(GEP))
                    return false;
            }
            continue;
        }

         // Some leeway in comparison instructions for getelement ptrs.
         // Safe to be conservative here. Looks like a very rare case.
        if(isa<ICmpInst>(U)) {
            if(!isa<GetElementPtrInst>(I))
                return false;
            continue;
        }
        if(const auto *BCI = dyn_cast<BitCastInst>(U)) {
            // Bit cast usually complicates things here, so
            // we just deal with this simple case and chicken out. 
            if(!onlyUsedByLifetimeMarkers(BCI))
                return false;
            continue;
        }
        if(const auto *II = dyn_cast<IntrinsicInst>(U)) {
            errs() << "INTRINSIC\n";
            if(!II->isLifetimeStartOrEnd())
                return false;
            continue;
        }
        return false;
    }
    return true;
}

// This checks if alloca should be promoted to memory.
static bool isPromotableAlloca(const AllocaInst *AI) {
    // Assess the types first
    auto *AITy = AI->getType();
    if(!AITy->isIntOrIntVectorTy() && !AITy->isFPOrFPVectorTy() && !AITy->isPtrOrPtrVectorTy()) {
        return false;
    }

   for(const auto *U : AI->users()) {
        if(const auto *LI = dyn_cast<LoadInst>(U)) {
            if(LI->isVolatile())
                return false;
            continue;
        }
        if(const auto *SI = dyn_cast<StoreInst>(U)) {
            if(SI->getOperand(0) == AI || SI->isVolatile())
                return false;
            continue;
        }
        if(const auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
            if(GEP->getType() != Type::getInt8PtrTy(U->getContext(), AI->getType()->getAddressSpace()))
                return false;

            // Cannot expect non-zero indices anywhere. If there are any, its not promotable.
            if(!GEP->hasAllZeroIndices())
                return false;
            
            // The result of this used in lifetime instrinsics is as far
            // as we are willing to tolerate.
            if(!onlyUsedByLifetimeMarkers(GEP))
                return false;

            continue;
        }
        if(const auto *BCI = dyn_cast<BitCastInst>(U)) {
            // Bit cast usually complicates things here, so
            // we just deal with this simple case and chicken out. 
            if(!onlyUsedByLifetimeMarkers(BCI))
                return false;
            continue;
        }
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
static void ExtractOffsets(AllocaInst &AI,
            std::map<uint64_t, std::vector<GetElementPtrInst *>> &OffsetsGEPsMap) {
    for(const auto *U : AI.users()) {
         if(const auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
            auto *Offset = cast<ConstantInt>(GEP->getOperand(2));
            OffsetsGEPsMap[Offset->getZExtValue()].push_back(const_cast<GetElementPtrInst *>(GEP));
        }
    }
}

static bool AnalyzeAlloca(AllocaInst *AI, SmallVector<AllocaInst *, 4> &Worklist, 
                            SmallVector<AllocaInst *, 4> &TryPromotelist) {
    errs() << "ANALYZING ALLOCA: " << *AI << "\n";
    // If alloca has no use, remove the useless thing.
    if(AI->use_empty()) {
        AI->eraseFromParent();
        return true;
    }

    // Skip any alloca which is not a struct or an array
    //if(!AI->getAllocatedType()->isStructTy() && !AI->isArrayAllocation()) {
    if(!isa<CompositeType>(AI->getAllocatedType()) && !isa<SequentialType>(AI->getAllocatedType())) {
        errs() << "NOT AN ARRAY NOR STRUCT\n";
        TryPromotelist.push_back(AI);
        return false;
    }

    // If the size of array or vector is more than 5, abort mission.
    if(auto *SeqTy = dyn_cast<SequentialType>(AI->getAllocatedType())) {
        if(SeqTy->getNumElements() > 5) {
            errs() << "ARRAY/VECTOR TOO BIG\n";
            return false;
        }
    }

    // We can deal with small arrays, but not zero size.
    const DataLayout &DL = AI->getModule()->getDataLayout();
    if(!DL.getTypeAllocSize(AI->getAllocatedType())) {
        errs() << "DATA LAYOUT ABORT\n";
        TryPromotelist.push_back(AI);
        return false;
    }

    // Is this alloca promotable?
    if(!isPromotable(AI)) {
        errs() << "ALLOCA CANNOT SROA\n";
        TryPromotelist.push_back(AI);
        return false;
    }

    // Now, we extract specific elements of the aggregate alloca
    // and use them separately.
    std::map<uint64_t, std::vector<GetElementPtrInst *>> OffsetsGEPsMap;
    ExtractOffsets(*AI, OffsetsGEPsMap);
    errs() << "OFFSETS EXTRACTED\n";

    // Deal with the alloca one offset at a time. Offsets that we do not
    // deal with here are useless anyway. So this pass is justified in 
    // removing those values.
    auto *FirstInst = AI->getParent()->getFirstNonPHI();
    for(auto &Entry : OffsetsGEPsMap) {
        uint64_t Offset = Entry.first;
        errs()  << "CONSIDERING OFFSET: " << Offset << "\n";
        auto &GEPVect = Entry.second;
        
        // Create an alloca for element at given offset
        Type *AllocType;
        if(auto *SeqAllocType = dyn_cast<SequentialType>(AI->getAllocatedType())) {
            errs() << "ARRAY OR VECTOR\n";
            AllocType = SeqAllocType->getElementType();
        } else {
            errs() << "STRUCT TYPE\n";
            // Its composite type
            auto *CompAllocType = dyn_cast<CompositeType>(AI->getAllocatedType());
            assert(CompAllocType && "Alloca should be of conposite type.");
            AllocType = CompAllocType->getTypeAtIndex(Offset);
        }
        auto *NewAlloca = new AllocaInst(AllocType, 
                            AI->getType()->getAddressSpace(), "", FirstInst);
        errs() << "NEW ALLOCA: " << *NewAlloca << "\n";
        NumReplaced++;
        
        // Replace the uses of this GEP with the new Alloca
        for(auto *GEP : GEPVect) {
            errs() << "CONSIDERED GEP: " << *GEP << "\n";
            GEP->replaceAllUsesWith(NewAlloca);
        }

        // Add the new alloca to the worklist
        Worklist.push_back(NewAlloca);
    }

    // Invalidate and remove the old alloca
    if(!OffsetsGEPsMap.empty()) {
        AI->replaceAllUsesWith(UndefValue::get(AI->getType()));
        AI->eraseFromParent();
    }
    errs() << "OLD ALLOCA ERASED FROM PARENT\n";
    return true;
}

static bool RunOnFunction(Function &F, DominatorTree &DT,
                                       AssumptionCache &AC) {
    errs() << "RUN ON FUNCTION:" << F.getName() << " \n";

    // Get all allocas first
    SmallVector<AllocaInst *, 4> Worklist;
    for(auto &I : F.getEntryBlock()) {
        if(auto *AI = dyn_cast<AllocaInst>(&I))
            Worklist.push_back(AI);
    }

    bool Changed = false;
    SmallVector<AllocaInst *, 4> TempWorklist;
    do {
        errs() << "PRINTING FUNCTION BEFORE ANALYSIS: \n";
        F.print(errs());
        SmallVector<AllocaInst *, 4> TryPromotelist;
        while(!Worklist.empty()) 
            Changed |= AnalyzeAlloca(Worklist.pop_back_val(), TempWorklist, TryPromotelist);
        errs() << "PRINTING FUNCTION AFTER ANALYSIS: \n";
        F.print(errs());
        TryPromotelist.append(TempWorklist.begin(), TempWorklist.end());
        std::vector<AllocaInst *> AllocaList;
        for(auto *AI : TryPromotelist) {
            errs() << "TRY ALLOCA: " << *AI << "\n";
            if(isPromotableAlloca(AI)) {
                AllocaList.push_back(AI);
                errs() << "YES\n";
            } else {
                errs() << "NOT\n";
            }
        }
        Changed |= PromoteAllocas(AllocaList, F, DT, AC);
        errs() << "PRINTING FUNCTION AFTER PROMOTION: \n";
        F.print(errs());
        for(auto *AI : AllocaList) {
            auto It = find(TempWorklist, AI);
            if(It == TempWorklist.end())
                continue;
            TempWorklist.erase(It);
        }
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
