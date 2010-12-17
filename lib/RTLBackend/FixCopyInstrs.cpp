//====-- FixCopyInstrs.cpp - Fuse the copys into micro state ----*- C++ -*-===//
// 
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implement the FixCopyInstrs pass, which move the copy instructions
// into the microstates.
//
//===----------------------------------------------------------------------===//
#include "vtm/Passes.h"
#include "vtm/FUInfo.h"
#include "vtm/MicroState.h"
#include "vtm/BitLevelInfo.h"

#include "llvm/Function.h"

#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "fix-regietsr-copy"
#include "llvm/Support/Debug.h"

using namespace llvm;

namespace {
struct FixCopy : public MachineFunctionPass{
  static char ID;
  BitLevelInfo *BLI;

  FixCopy() : MachineFunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const {
    MachineFunctionPass::getAnalysisUsage(AU);
    // Diry hack: Force re-run the bitlevel info.
    AU.addRequired<BitLevelInfo>();
    AU.setPreservesAll();
  }


  void FuseCopyInstr(MachineInstr &Copy, LLVMContext &Context);

  void runOnMachineBasicBlock(MachineBasicBlock &MBB, LLVMContext &Context);

  bool runOnMachineFunction(MachineFunction &MF) {
    BLI = &getAnalysis<BitLevelInfo>();

    LLVMContext &Context = MF.getFunction()->getContext();

    for (MachineFunction::iterator I = MF.begin(), E = MF.end();
         I != E; ++I)
      runOnMachineBasicBlock(*I, Context);

    return false;
  }

};
}

char FixCopy::ID = 0;

Pass *llvm::createFixCopyPass() {
  return new FixCopy();
}

static MachineInstr &findNextControl(MachineInstr &I) {
  MachineBasicBlock *MBB = I.getParent();
  MachineBasicBlock::iterator It = I;
  while (It != MBB->end()) {
    ++It;
    if (It->getOpcode() == VTM::Control
        || It->getOpcode() ==VTM::Terminator)
      return *It;
  }

  assert(0 && "Can not find next control!");
  return I;
}

void FixCopy::FuseCopyInstr(MachineInstr &Copy, LLVMContext &Context) {
  ucState Ctrl(findNextControl(Copy));

  MachineOperand &SrcOp = Copy.getOperand(1),
                 &DstOp = Copy.getOperand(0);

  unsigned SrcWire = 0;

  unsigned SrcReg = SrcOp.getReg(),
           DstReg = DstOp.getReg();

  for (ucState::iterator I = Ctrl.begin(), E = Ctrl.end(); I != E; ++I) {
    ucOp Op = *I;
    for (ucOp::op_iterator MI = Op.op_begin(), ME = Op.op_end();
         MI != ME; ++MI) {
      MachineOperand &MO = *MI;
      if (!MO.isReg()) continue;
      // TODO: Overcome this!
      assert((!MO.isUse() || MO.getReg() != DstReg)
              && "Can not fuse instruction!");
      // Forward the wire value if necessary.
      if (MO.isDef() && MO.getReg() == SrcReg)
        SrcWire =Op->getWireNum();
    }
  }

  // Transfer the operands.
  Copy.RemoveOperand(1);
  Copy.RemoveOperand(0);
  MachineInstrBuilder MIB(&*Ctrl);
  if (SrcWire) {
    MDNode *OpCode = MetaToken::createDefReg(0, SrcWire, Context);
    MIB.addMetadata(OpCode).addOperand(DstOp);
  } else {
    MDNode *OpCode = MetaToken::createInstr(0, Copy, FuncUnitId().getData(), Context);
    MIB.addMetadata(OpCode).addOperand(DstOp).addOperand(SrcOp);
  }
  // Discard the operand.
  Copy.eraseFromParent();
}

void FixCopy::runOnMachineBasicBlock(MachineBasicBlock &MBB,
                                     LLVMContext &Context) {
  DEBUG(dbgs() << MBB.getName() << " After register allocation:\n");
  SmallVector<MachineInstr*, 8> Copys;
  for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end();
      I != E; ++I) {
    MachineInstr *Instr = I;
    switch (Instr->getOpcode()) {
    case VTM::Control:
    case VTM::Datapath:
    case VTM::Terminator:
      DEBUG(ucState(*Instr).dump());
      break;
    case VTM::COPY:
      DEBUG(Instr->dump());
      Copys.push_back(Instr);
      break;
    default:
      DEBUG(Instr->dump());
      assert(0 && "Unexpected instruction!");
      break;
    }
  }

  while (!Copys.empty())
    FuseCopyInstr(*Copys.pop_back_val(), Context);

  DEBUG(
  dbgs() << MBB.getName() << " After copy fixed:\n";
  for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end();
    I != E; ++I) {
      MachineInstr *Instr = I;
      switch (Instr->getOpcode()) {
    case VTM::Control:
    case VTM::Datapath:
    case VTM::Terminator:
      ucState(*Instr).dump();
      break;
    default:
      DEBUG(Instr->dump());
      assert(0 && "Unexpected instruction!");
      break;
      }
  });
}
