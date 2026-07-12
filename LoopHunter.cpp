// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Henrique Bucher
//===- LoopHunter.cpp - flag irreducible loops the optimizer skips --------===//
//
// An out-of-tree LLVM (new pass manager) plugin that reports IRREDUCIBLE loops.
//
// Why: LLVM's LoopInfo -- what LICM, loop-unroll, loop-unswitch, and the
// vectorizer all run on -- only recognizes *natural* (reducible, single-entry)
// loops. A loop whose cycle has more than one entry is irreducible: it gets
// none of those optimizations, and the compiler emits NO diagnostic. This pass
// finds them so they stop being silent.
//
// It uses CycleInfo (GenericCycleInfo, the Wei/Tan/Chen single-pass DFS), which
// captures *all* cycles including irreducible ones; a cycle is irreducible iff
// it has more than one entry block (Cycle::isReducible() == Entries.size()==1).
//
// Usage:
//   clang++-20 -O2 -g -S -emit-llvm foo.cpp -o foo.ll
//   opt-20 -load-pass-plugin=./libLoopHunter.so -passes=loop-hunter \
//          -disable-output foo.ll
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/CycleAnalysis.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/CycleInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include <string>

using namespace llvm;

namespace {

// Best-effort "file:line" for a block: first instruction carrying a DebugLoc.
static std::string locOf(const BasicBlock *BB) {
  for (const Instruction &I : *BB)
    if (const DebugLoc &DL = I.getDebugLoc())
      if (DL.getLine())
        return (DL->getFilename() + ":" + Twine(DL->getLine())).str();
  return "";
}

// A readable label for a block: its name if it has one, else its %N operand id.
static std::string bbName(const BasicBlock *BB) {
  if (BB->hasName())
    return BB->getName().str();
  std::string S;
  raw_string_ostream OS(S);
  BB->printAsOperand(OS, /*PrintType=*/false);
  return OS.str();
}

// A cycle-wide fallback location: the first block in the cycle that has one.
static std::string locOfCycle(const Cycle *C) {
  std::string L = locOf(C->getHeader());
  if (!L.empty())
    return L;
  for (const BasicBlock *BB : C->blocks())
    if (std::string BL = locOf(BB); !BL.empty())
      return BL;
  return "<no-debug-info; build with -g -O1>";
}

static unsigned reportCycle(const Cycle *C, const Function &F,
                            raw_ostream &OS, unsigned Depth) {
  unsigned Found = 0;
  if (!C->isReducible()) {
    ++Found;
    // Compiler-diagnostic-style output ("file:line: warning: ...") so editors
    // and CI (GitHub annotations, problem matchers) parse it like any warning.
    std::string Loc = locOfCycle(C);
    if (!Loc.empty() && Loc[0] != '<')
      OS << Loc << ": ";
    OS << "warning: irreducible loop in " << demangle(F.getName().str())
       << " (" << C->getEntries().size()
       << " entries) -- vectorizer/LICM/unroll skip it silently [loop-hunter]\n";
    for (const BasicBlock *E : C->getEntries()) {
      std::string L = locOf(E);
      if (!L.empty())
        OS << L << ": ";
      OS << "note: entry into the cycle via block " << bbName(E) << "\n";
    }
    OS << "note: fix: give the cycle a single entry (buffer-and-restart instead"
          " of resume-into-the-middle; don't jump or switch past the loop head)"
          "\n";
  }
  // Recurse: an irreducible cycle can nest reducible ones and vice versa.
  for (const Cycle *Child : C->children())
    Found += reportCycle(Child, F, OS, Depth + 1);
  return Found;
}

struct LoopHunterPass : PassInfoMixin<LoopHunterPass> {
  // Module-level tally so we can print a summary and set an exit-worthy signal.
  static inline unsigned TotalIrreducible = 0;

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    if (F.isDeclaration())
      return PreservedAnalyses::all();
    CycleInfo &CI = FAM.getResult<CycleAnalysis>(F);
    for (const Cycle *C : CI.toplevel_cycles())
      TotalIrreducible += reportCycle(C, F, errs(), 0);
    return PreservedAnalyses::all();
  }
  static bool isRequired() { return true; }
};

// A tiny module pass that prints the final tally after all functions run.
struct LoopHunterSummary : PassInfoMixin<LoopHunterSummary> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    if (LoopHunterPass::TotalIrreducible == 0)
      errs() << "loop-hunter: no irreducible loops found.\n";
    else
      errs() << "loop-hunter: " << LoopHunterPass::TotalIrreducible
             << " irreducible loop(s) found (see above).\n";
    return PreservedAnalyses::all();
  }
};

} // namespace

llvm::PassPluginLibraryInfo getLoopHunterPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "LoopHunter", "v0.1",
          [](PassBuilder &PB) {
            // (1) Manual / scan mode: opt -passes=loop-hunter
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "loop-hunter") {
                    FPM.addPass(LoopHunterPass());
                    return true;
                  }
                  return false;
                });
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "loop-hunter-summary") {
                    MPM.addPass(LoopHunterSummary());
                    return true;
                  }
                  return false;
                });
            // (2) Inline-build mode: clang -fpass-plugin=libLoopHunter.so
            //     Auto-run right before the vectorizer -- exactly the point
            //     where an irreducible loop is about to be silently skipped.
            //     No -passes= needed; just add the flag to CXXFLAGS.
            PB.registerVectorizerStartEPCallback(
                [](FunctionPassManager &FPM, OptimizationLevel Level) {
                  FPM.addPass(LoopHunterPass());
                });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getLoopHunterPluginInfo();
}
