// Copyright 2012 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "hydrogen.h"

#include <algorithm>

#include "v8.h"
#include "codegen.h"
#include "full-codegen.h"
#include "hashmap.h"
#include "lithium-allocator.h"
#include "parser.h"
#include "scopeinfo.h"
#include "scopes.h"
#include "stub-cache.h"

#if V8_TARGET_ARCH_IA32
#include "ia32/lithium-codegen-ia32.h"
#elif V8_TARGET_ARCH_X64
#include "x64/lithium-codegen-x64.h"
#elif V8_TARGET_ARCH_ARM
#include "arm/lithium-codegen-arm.h"
#elif V8_TARGET_ARCH_MIPS
#include "mips/lithium-codegen-mips.h"
#else
#error Unsupported target architecture.
#endif

namespace v8 {
namespace internal {

HBasicBlock::HBasicBlock(HGraph* graph)
    : block_id_(graph->GetNextBlockID()),
      graph_(graph),
      phis_(4, graph->zone()),
      first_(NULL),
      last_(NULL),
      end_(NULL),
      loop_information_(NULL),
      predecessors_(2, graph->zone()),
      dominator_(NULL),
      dominated_blocks_(4, graph->zone()),
      last_environment_(NULL),
      argument_count_(-1),
      first_instruction_index_(-1),
      last_instruction_index_(-1),
      deleted_phis_(4, graph->zone()),
      parent_loop_header_(NULL),
      is_inline_return_target_(false),
      is_deoptimizing_(false),
      dominates_loop_successors_(false),
      is_osr_entry_(false) { }


Isolate* HBasicBlock::isolate() const {
  return graph_->isolate();
}


void HBasicBlock::AttachLoopInformation() {
  ASSERT(!IsLoopHeader());
  loop_information_ = new(zone()) HLoopInformation(this, zone());
}


void HBasicBlock::DetachLoopInformation() {
  ASSERT(IsLoopHeader());
  loop_information_ = NULL;
}


void HBasicBlock::AddPhi(HPhi* phi) {
  ASSERT(!IsStartBlock());
  phis_.Add(phi, zone());
  phi->SetBlock(this);
}


void HBasicBlock::RemovePhi(HPhi* phi) {
  ASSERT(phi->block() == this);
  ASSERT(phis_.Contains(phi));
  ASSERT(phi->HasNoUses() || !phi->is_live());
  phi->Kill();
  phis_.RemoveElement(phi);
  phi->SetBlock(NULL);
}


void HBasicBlock::AddInstruction(HInstruction* instr) {
  ASSERT(!IsStartBlock() || !IsFinished());
  ASSERT(!instr->IsLinked());
  ASSERT(!IsFinished());

  if (first_ == NULL) {
    ASSERT(last_environment() != NULL);
    ASSERT(!last_environment()->ast_id().IsNone());
    HBlockEntry* entry = new(zone()) HBlockEntry();
    entry->InitializeAsFirst(this);
    first_ = last_ = entry;
  }
  instr->InsertAfter(last_);
}


HDeoptimize* HBasicBlock::CreateDeoptimize(
    HDeoptimize::UseEnvironment has_uses) {
  ASSERT(HasEnvironment());
  if (has_uses == HDeoptimize::kNoUses)
    return new(zone()) HDeoptimize(0, zone());

  HEnvironment* environment = last_environment();
  HDeoptimize* instr = new(zone()) HDeoptimize(environment->length(), zone());
  for (int i = 0; i < environment->length(); i++) {
    HValue* val = environment->values()->at(i);
    instr->AddEnvironmentValue(val, zone());
  }

  return instr;
}


HSimulate* HBasicBlock::CreateSimulate(BailoutId ast_id,
                                       RemovableSimulate removable) {
  ASSERT(HasEnvironment());
  HEnvironment* environment = last_environment();
  ASSERT(ast_id.IsNone() ||
         ast_id == BailoutId::StubEntry() ||
         environment->closure()->shared()->VerifyBailoutId(ast_id));

  int push_count = environment->push_count();
  int pop_count = environment->pop_count();

  HSimulate* instr =
      new(zone()) HSimulate(ast_id, pop_count, zone(), removable);
  // Order of pushed values: newest (top of stack) first. This allows
  // HSimulate::MergeInto() to easily append additional pushed values
  // that are older (from further down the stack).
  for (int i = 0; i < push_count; ++i) {
    instr->AddPushedValue(environment->ExpressionStackAt(i));
  }
  for (GrowableBitVector::Iterator it(environment->assigned_variables(),
                                      zone());
       !it.Done();
       it.Advance()) {
    int index = it.Current();
    instr->AddAssignedValue(index, environment->Lookup(index));
  }
  environment->ClearHistory();
  return instr;
}


void HBasicBlock::Finish(HControlInstruction* end) {
  ASSERT(!IsFinished());
  AddInstruction(end);
  end_ = end;
  for (HSuccessorIterator it(end); !it.Done(); it.Advance()) {
    it.Current()->RegisterPredecessor(this);
  }
}


void HBasicBlock::Goto(HBasicBlock* block,
                       FunctionState* state,
                       bool add_simulate) {
  bool drop_extra = state != NULL &&
      state->inlining_kind() == DROP_EXTRA_ON_RETURN;

  if (block->IsInlineReturnTarget()) {
    AddInstruction(new(zone()) HLeaveInlined());
    last_environment_ = last_environment()->DiscardInlined(drop_extra);
  }

  if (add_simulate) AddSimulate(BailoutId::None());
  HGoto* instr = new(zone()) HGoto(block);
  Finish(instr);
}


void HBasicBlock::AddLeaveInlined(HValue* return_value,
                                  FunctionState* state) {
  HBasicBlock* target = state->function_return();
  bool drop_extra = state->inlining_kind() == DROP_EXTRA_ON_RETURN;

  ASSERT(target->IsInlineReturnTarget());
  ASSERT(return_value != NULL);
  AddInstruction(new(zone()) HLeaveInlined());
  last_environment_ = last_environment()->DiscardInlined(drop_extra);
  last_environment()->Push(return_value);
  AddSimulate(BailoutId::None());
  HGoto* instr = new(zone()) HGoto(target);
  Finish(instr);
}


void HBasicBlock::SetInitialEnvironment(HEnvironment* env) {
  ASSERT(!HasEnvironment());
  ASSERT(first() == NULL);
  UpdateEnvironment(env);
}


void HBasicBlock::SetJoinId(BailoutId ast_id) {
  int length = predecessors_.length();
  ASSERT(length > 0);
  for (int i = 0; i < length; i++) {
    HBasicBlock* predecessor = predecessors_[i];
    ASSERT(predecessor->end()->IsGoto());
    HSimulate* simulate = HSimulate::cast(predecessor->end()->previous());
    ASSERT(i != 0 ||
           (predecessor->last_environment()->closure().is_null() ||
            predecessor->last_environment()->closure()->shared()
              ->VerifyBailoutId(ast_id)));
    simulate->set_ast_id(ast_id);
    predecessor->last_environment()->set_ast_id(ast_id);
  }
}


bool HBasicBlock::Dominates(HBasicBlock* other) const {
  HBasicBlock* current = other->dominator();
  while (current != NULL) {
    if (current == this) return true;
    current = current->dominator();
  }
  return false;
}


int HBasicBlock::LoopNestingDepth() const {
  const HBasicBlock* current = this;
  int result  = (current->IsLoopHeader()) ? 1 : 0;
  while (current->parent_loop_header() != NULL) {
    current = current->parent_loop_header();
    result++;
  }
  return result;
}


void HBasicBlock::PostProcessLoopHeader(IterationStatement* stmt) {
  ASSERT(IsLoopHeader());

  SetJoinId(stmt->EntryId());
  if (predecessors()->length() == 1) {
    // This is a degenerated loop.
    DetachLoopInformation();
    return;
  }

  // Only the first entry into the loop is from outside the loop. All other
  // entries must be back edges.
  for (int i = 1; i < predecessors()->length(); ++i) {
    loop_information()->RegisterBackEdge(predecessors()->at(i));
  }
}


void HBasicBlock::RegisterPredecessor(HBasicBlock* pred) {
  if (HasPredecessor()) {
    // Only loop header blocks can have a predecessor added after
    // instructions have been added to the block (they have phis for all
    // values in the environment, these phis may be eliminated later).
    ASSERT(IsLoopHeader() || first_ == NULL);
    HEnvironment* incoming_env = pred->last_environment();
    if (IsLoopHeader()) {
      ASSERT(phis()->length() == incoming_env->length());
      for (int i = 0; i < phis_.length(); ++i) {
        phis_[i]->AddInput(incoming_env->values()->at(i));
      }
    } else {
      last_environment()->AddIncomingEdge(this, pred->last_environment());
    }
  } else if (!HasEnvironment() && !IsFinished()) {
    ASSERT(!IsLoopHeader());
    SetInitialEnvironment(pred->last_environment()->Copy());
  }

  predecessors_.Add(pred, zone());
}


void HBasicBlock::AddDominatedBlock(HBasicBlock* block) {
  ASSERT(!dominated_blocks_.Contains(block));
  // Keep the list of dominated blocks sorted such that if there is two
  // succeeding block in this list, the predecessor is before the successor.
  int index = 0;
  while (index < dominated_blocks_.length() &&
         dominated_blocks_[index]->block_id() < block->block_id()) {
    ++index;
  }
  dominated_blocks_.InsertAt(index, block, zone());
}


void HBasicBlock::AssignCommonDominator(HBasicBlock* other) {
  if (dominator_ == NULL) {
    dominator_ = other;
    other->AddDominatedBlock(this);
  } else if (other->dominator() != NULL) {
    HBasicBlock* first = dominator_;
    HBasicBlock* second = other;

    while (first != second) {
      if (first->block_id() > second->block_id()) {
        first = first->dominator();
      } else {
        second = second->dominator();
      }
      ASSERT(first != NULL && second != NULL);
    }

    if (dominator_ != first) {
      ASSERT(dominator_->dominated_blocks_.Contains(this));
      dominator_->dominated_blocks_.RemoveElement(this);
      dominator_ = first;
      first->AddDominatedBlock(this);
    }
  }
}


void HBasicBlock::AssignLoopSuccessorDominators() {
  // Mark blocks that dominate all subsequent reachable blocks inside their
  // loop. Exploit the fact that blocks are sorted in reverse post order. When
  // the loop is visited in increasing block id order, if the number of
  // non-loop-exiting successor edges at the dominator_candidate block doesn't
  // exceed the number of previously encountered predecessor edges, there is no
  // path from the loop header to any block with higher id that doesn't go
  // through the dominator_candidate block. In this case, the
  // dominator_candidate block is guaranteed to dominate all blocks reachable
  // from it with higher ids.
  HBasicBlock* last = loop_information()->GetLastBackEdge();
  int outstanding_successors = 1;  // one edge from the pre-header
  // Header always dominates everything.
  MarkAsLoopSuccessorDominator();
  for (int j = block_id(); j <= last->block_id(); ++j) {
    HBasicBlock* dominator_candidate = graph_->blocks()->at(j);
    for (HPredecessorIterator it(dominator_candidate); !it.Done();
         it.Advance()) {
      HBasicBlock* predecessor = it.Current();
      // Don't count back edges.
      if (predecessor->block_id() < dominator_candidate->block_id()) {
        outstanding_successors--;
      }
    }

    // If more successors than predecessors have been seen in the loop up to
    // now, it's not possible to guarantee that the current block dominates
    // all of the blocks with higher IDs. In this case, assume conservatively
    // that those paths through loop that don't go through the current block
    // contain all of the loop's dependencies. Also be careful to record
    // dominator information about the current loop that's being processed,
    // and not nested loops, which will be processed when
    // AssignLoopSuccessorDominators gets called on their header.
    ASSERT(outstanding_successors >= 0);
    HBasicBlock* parent_loop_header = dominator_candidate->parent_loop_header();
    if (outstanding_successors == 0 &&
        (parent_loop_header == this && !dominator_candidate->IsLoopHeader())) {
      dominator_candidate->MarkAsLoopSuccessorDominator();
    }
    HControlInstruction* end = dominator_candidate->end();
    for (HSuccessorIterator it(end); !it.Done(); it.Advance()) {
      HBasicBlock* successor = it.Current();
      // Only count successors that remain inside the loop and don't loop back
      // to a loop header.
      if (successor->block_id() > dominator_candidate->block_id() &&
          successor->block_id() <= last->block_id()) {
        // Backwards edges must land on loop headers.
        ASSERT(successor->block_id() > dominator_candidate->block_id() ||
               successor->IsLoopHeader());
        outstanding_successors++;
      }
    }
  }
}


int HBasicBlock::PredecessorIndexOf(HBasicBlock* predecessor) const {
  for (int i = 0; i < predecessors_.length(); ++i) {
    if (predecessors_[i] == predecessor) return i;
  }
  UNREACHABLE();
  return -1;
}


#ifdef DEBUG
void HBasicBlock::Verify() {
  // Check that every block is finished.
  ASSERT(IsFinished());
  ASSERT(block_id() >= 0);

  // Check that the incoming edges are in edge split form.
  if (predecessors_.length() > 1) {
    for (int i = 0; i < predecessors_.length(); ++i) {
      ASSERT(predecessors_[i]->end()->SecondSuccessor() == NULL);
    }
  }
}
#endif


void HLoopInformation::RegisterBackEdge(HBasicBlock* block) {
  this->back_edges_.Add(block, block->zone());
  AddBlock(block);
}


HBasicBlock* HLoopInformation::GetLastBackEdge() const {
  int max_id = -1;
  HBasicBlock* result = NULL;
  for (int i = 0; i < back_edges_.length(); ++i) {
    HBasicBlock* cur = back_edges_[i];
    if (cur->block_id() > max_id) {
      max_id = cur->block_id();
      result = cur;
    }
  }
  return result;
}


void HLoopInformation::AddBlock(HBasicBlock* block) {
  if (block == loop_header()) return;
  if (block->parent_loop_header() == loop_header()) return;
  if (block->parent_loop_header() != NULL) {
    AddBlock(block->parent_loop_header());
  } else {
    block->set_parent_loop_header(loop_header());
    blocks_.Add(block, block->zone());
    for (int i = 0; i < block->predecessors()->length(); ++i) {
      AddBlock(block->predecessors()->at(i));
    }
  }
}


#ifdef DEBUG

// Checks reachability of the blocks in this graph and stores a bit in
// the BitVector "reachable()" for every block that can be reached
// from the start block of the graph. If "dont_visit" is non-null, the given
// block is treated as if it would not be part of the graph. "visited_count()"
// returns the number of reachable blocks.
class ReachabilityAnalyzer BASE_EMBEDDED {
 public:
  ReachabilityAnalyzer(HBasicBlock* entry_block,
                       int block_count,
                       HBasicBlock* dont_visit)
      : visited_count_(0),
        stack_(16, entry_block->zone()),
        reachable_(block_count, entry_block->zone()),
        dont_visit_(dont_visit) {
    PushBlock(entry_block);
    Analyze();
  }

  int visited_count() const { return visited_count_; }
  const BitVector* reachable() const { return &reachable_; }

 private:
  void PushBlock(HBasicBlock* block) {
    if (block != NULL && block != dont_visit_ &&
        !reachable_.Contains(block->block_id())) {
      reachable_.Add(block->block_id());
      stack_.Add(block, block->zone());
      visited_count_++;
    }
  }

  void Analyze() {
    while (!stack_.is_empty()) {
      HControlInstruction* end = stack_.RemoveLast()->end();
      for (HSuccessorIterator it(end); !it.Done(); it.Advance()) {
        PushBlock(it.Current());
      }
    }
  }

  int visited_count_;
  ZoneList<HBasicBlock*> stack_;
  BitVector reachable_;
  HBasicBlock* dont_visit_;
};


void HGraph::Verify(bool do_full_verify) const {
  Heap::RelocationLock(isolate()->heap());
  ALLOW_HANDLE_DEREF(isolate(), "debug mode verification");
  for (int i = 0; i < blocks_.length(); i++) {
    HBasicBlock* block = blocks_.at(i);

    block->Verify();

    // Check that every block contains at least one node and that only the last
    // node is a control instruction.
    HInstruction* current = block->first();
    ASSERT(current != NULL && current->IsBlockEntry());
    while (current != NULL) {
      ASSERT((current->next() == NULL) == current->IsControlInstruction());
      ASSERT(current->block() == block);
      current->Verify();
      current = current->next();
    }

    // Check that successors are correctly set.
    HBasicBlock* first = block->end()->FirstSuccessor();
    HBasicBlock* second = block->end()->SecondSuccessor();
    ASSERT(second == NULL || first != NULL);

    // Check that the predecessor array is correct.
    if (first != NULL) {
      ASSERT(first->predecessors()->Contains(block));
      if (second != NULL) {
        ASSERT(second->predecessors()->Contains(block));
      }
    }

    // Check that phis have correct arguments.
    for (int j = 0; j < block->phis()->length(); j++) {
      HPhi* phi = block->phis()->at(j);
      phi->Verify();
    }

    // Check that all join blocks have predecessors that end with an
    // unconditional goto and agree on their environment node id.
    if (block->predecessors()->length() >= 2) {
      BailoutId id =
          block->predecessors()->first()->last_environment()->ast_id();
      for (int k = 0; k < block->predecessors()->length(); k++) {
        HBasicBlock* predecessor = block->predecessors()->at(k);
        ASSERT(predecessor->end()->IsGoto());
        ASSERT(predecessor->last_environment()->ast_id() == id);
      }
    }
  }

  // Check special property of first block to have no predecessors.
  ASSERT(blocks_.at(0)->predecessors()->is_empty());

  if (do_full_verify) {
    // Check that the graph is fully connected.
    ReachabilityAnalyzer analyzer(entry_block_, blocks_.length(), NULL);
    ASSERT(analyzer.visited_count() == blocks_.length());

    // Check that entry block dominator is NULL.
    ASSERT(entry_block_->dominator() == NULL);

    // Check dominators.
    for (int i = 0; i < blocks_.length(); ++i) {
      HBasicBlock* block = blocks_.at(i);
      if (block->dominator() == NULL) {
        // Only start block may have no dominator assigned to.
        ASSERT(i == 0);
      } else {
        // Assert that block is unreachable if dominator must not be visited.
        ReachabilityAnalyzer dominator_analyzer(entry_block_,
                                                blocks_.length(),
                                                block->dominator());
        ASSERT(!dominator_analyzer.reachable()->Contains(block->block_id()));
      }
    }
  }
}

#endif


HConstant* HGraph::GetConstantInt32(SetOncePointer<HConstant>* pointer,
                                    int32_t value) {
  if (!pointer->is_set()) {
    HConstant* constant =
        new(zone()) HConstant(value, Representation::Integer32());
    constant->InsertAfter(GetConstantUndefined());
    pointer->set(constant);
  }
  return pointer->get();
}


HConstant* HGraph::GetConstantSmi(SetOncePointer<HConstant>* pointer,
                                  int32_t value) {
  if (!pointer->is_set()) {
    HConstant* constant =
        new(zone()) HConstant(Handle<Object>(Smi::FromInt(value), isolate()),
                              Representation::Tagged());
    constant->InsertAfter(GetConstantUndefined());
    pointer->set(constant);
  }
  return pointer->get();
}


HConstant* HGraph::GetConstant0() {
  return GetConstantInt32(&constant_0_, 0);
}


HConstant* HGraph::GetConstant1() {
  return GetConstantInt32(&constant_1_, 1);
}


HConstant* HGraph::GetConstantMinus1() {
  return GetConstantInt32(&constant_minus1_, -1);
}


#define DEFINE_GET_CONSTANT(Name, name, htype, boolean_value)                  \
HConstant* HGraph::GetConstant##Name() {                                       \
  if (!constant_##name##_.is_set()) {                                          \
    HConstant* constant = new(zone()) HConstant(                               \
        isolate()->factory()->name##_value(),                                  \
        UniqueValueId(isolate()->heap()->name##_value()),                      \
        Representation::Tagged(),                                              \
        htype,                                                                 \
        false,                                                                 \
        true,                                                                  \
        boolean_value);                                                        \
    constant->InsertAfter(GetConstantUndefined());                             \
    constant_##name##_.set(constant);                                          \
  }                                                                            \
  return constant_##name##_.get();                                             \
}


DEFINE_GET_CONSTANT(True, true, HType::Boolean(), true)
DEFINE_GET_CONSTANT(False, false, HType::Boolean(), false)
DEFINE_GET_CONSTANT(Hole, the_hole, HType::Tagged(), false)
DEFINE_GET_CONSTANT(Null, null, HType::Tagged(), false)


HConstant* HGraph::GetConstantSmi0() {
  return GetConstantSmi(&constant_smi_0_, 0);
}


HConstant* HGraph::GetConstantSmi1() {
  return GetConstantSmi(&constant_smi_1_, 1);
}


#undef DEFINE_GET_CONSTANT


HConstant* HGraph::GetInvalidContext() {
  return GetConstantInt32(&constant_invalid_context_, 0xFFFFC0C7);
}


HGraphBuilder::IfBuilder::IfBuilder(HGraphBuilder* builder, int position)
    : builder_(builder),
      position_(position),
      finished_(false),
      did_then_(false),
      did_else_(false),
      did_and_(false),
      did_or_(false),
      captured_(false),
      needs_compare_(true),
      split_edge_merge_block_(NULL) {
  HEnvironment* env = builder->environment();
  first_true_block_ = builder->CreateBasicBlock(env->Copy());
  last_true_block_ = NULL;
  first_false_block_ = builder->CreateBasicBlock(env->Copy());
}


HGraphBuilder::IfBuilder::IfBuilder(
    HGraphBuilder* builder,
    HIfContinuation* continuation)
    : builder_(builder),
      position_(RelocInfo::kNoPosition),
      finished_(false),
      did_then_(false),
      did_else_(false),
      did_and_(false),
      did_or_(false),
      captured_(false),
      needs_compare_(false),
      first_true_block_(NULL),
      first_false_block_(NULL),
      split_edge_merge_block_(NULL),
      merge_block_(NULL) {
  continuation->Continue(&first_true_block_,
                         &first_false_block_,
                         &position_);
}


HInstruction* HGraphBuilder::IfBuilder::IfCompare(
    HValue* left,
    HValue* right,
    Token::Value token,
    Representation input_representation) {
  HCompareIDAndBranch* compare =
      new(zone()) HCompareIDAndBranch(left, right, token);
  compare->set_observed_input_representation(input_representation,
                                             input_representation);
  compare->ChangeRepresentation(input_representation);
  AddCompare(compare);
  return compare;
}


HInstruction* HGraphBuilder::IfBuilder::IfCompareMap(HValue* left,
                                                     Handle<Map> map) {
  HCompareMap* compare =
      new(zone()) HCompareMap(left, map,
                              first_true_block_, first_false_block_);
  AddCompare(compare);
  return compare;
}


void HGraphBuilder::IfBuilder::AddCompare(HControlInstruction* compare) {
  if (split_edge_merge_block_ != NULL) {
    HEnvironment* env = first_false_block_->last_environment();
    HBasicBlock* split_edge =
        builder_->CreateBasicBlock(env->Copy());
    if (did_or_) {
      compare->SetSuccessorAt(0, split_edge);
      compare->SetSuccessorAt(1, first_false_block_);
    } else {
      compare->SetSuccessorAt(0, first_true_block_);
      compare->SetSuccessorAt(1, split_edge);
    }
    split_edge->GotoNoSimulate(split_edge_merge_block_);
  } else {
    compare->SetSuccessorAt(0, first_true_block_);
    compare->SetSuccessorAt(1, first_false_block_);
  }
  builder_->current_block()->Finish(compare);
  needs_compare_ = false;
}


void HGraphBuilder::IfBuilder::Or() {
  ASSERT(!did_and_);
  did_or_ = true;
  HEnvironment* env = first_false_block_->last_environment();
  if (split_edge_merge_block_ == NULL) {
    split_edge_merge_block_ =
        builder_->CreateBasicBlock(env->Copy());
    first_true_block_->GotoNoSimulate(split_edge_merge_block_);
    first_true_block_ = split_edge_merge_block_;
  }
  builder_->set_current_block(first_false_block_);
  first_false_block_ = builder_->CreateBasicBlock(env->Copy());
}


void HGraphBuilder::IfBuilder::And() {
  ASSERT(!did_or_);
  did_and_ = true;
  HEnvironment* env = first_false_block_->last_environment();
  if (split_edge_merge_block_ == NULL) {
    split_edge_merge_block_ = builder_->CreateBasicBlock(env->Copy());
    first_false_block_->GotoNoSimulate(split_edge_merge_block_);
    first_false_block_ = split_edge_merge_block_;
  }
  builder_->set_current_block(first_true_block_);
  first_true_block_ = builder_->CreateBasicBlock(env->Copy());
}


void HGraphBuilder::IfBuilder::CaptureContinuation(
    HIfContinuation* continuation) {
  ASSERT(!finished_);
  ASSERT(!captured_);
  HBasicBlock* true_block = last_true_block_ == NULL
      ? first_true_block_
      : last_true_block_;
  HBasicBlock* false_block = did_else_ && (first_false_block_ != NULL)
      ? builder_->current_block()
      : first_false_block_;
  continuation->Capture(true_block, false_block, position_);
  captured_ = true;
  End();
}


void HGraphBuilder::IfBuilder::Then() {
  ASSERT(!captured_);
  ASSERT(!finished_);
  did_then_ = true;
  if (needs_compare_) {
    // Handle if's without any expressions, they jump directly to the "else"
    // branch.
    builder_->current_block()->GotoNoSimulate(first_false_block_);
    first_true_block_ = NULL;
  }
  builder_->set_current_block(first_true_block_);
}


void HGraphBuilder::IfBuilder::Else() {
  ASSERT(did_then_);
  ASSERT(!captured_);
  ASSERT(!finished_);
  last_true_block_ = builder_->current_block();
  ASSERT(first_true_block_ == NULL || !last_true_block_->IsFinished());
  builder_->set_current_block(first_false_block_);
  did_else_ = true;
}


void HGraphBuilder::IfBuilder::Deopt() {
  HBasicBlock* block = builder_->current_block();
  block->FinishExitWithDeoptimization(HDeoptimize::kUseAll);
  if (did_else_) {
    first_false_block_ = NULL;
  } else {
    first_true_block_ = NULL;
  }
}


void HGraphBuilder::IfBuilder::Return(HValue* value) {
  HBasicBlock* block = builder_->current_block();
  block->Finish(new(zone()) HReturn(value,
                                    builder_->environment()->LookupContext(),
                                    builder_->graph()->GetConstantMinus1()));
  if (did_else_) {
    first_false_block_ = NULL;
  } else {
    first_true_block_ = NULL;
  }
}


void HGraphBuilder::IfBuilder::End() {
  if (!captured_) {
    ASSERT(did_then_);
    if (!did_else_) {
      last_true_block_ = builder_->current_block();
    }
    if (first_true_block_ == NULL) {
      // Deopt on true. Nothing to do, just continue the false block.
    } else if (first_false_block_ == NULL) {
      // Deopt on false. Nothing to do except switching to the true block.
      builder_->set_current_block(last_true_block_);
    } else {
      HEnvironment* merge_env = last_true_block_->last_environment()->Copy();
      merge_block_ = builder_->CreateBasicBlock(merge_env);
      ASSERT(!finished_);
      if (!did_else_) Else();
      ASSERT(!last_true_block_->IsFinished());
      HBasicBlock* last_false_block = builder_->current_block();
      ASSERT(!last_false_block->IsFinished());
      last_true_block_->GotoNoSimulate(merge_block_);
      last_false_block->GotoNoSimulate(merge_block_);
      builder_->set_current_block(merge_block_);
    }
  }
  finished_ = true;
}


HGraphBuilder::LoopBuilder::LoopBuilder(HGraphBuilder* builder,
                                        HValue* context,
                                        LoopBuilder::Direction direction)
    : builder_(builder),
      context_(context),
      direction_(direction),
      finished_(false) {
  header_block_ = builder->CreateLoopHeaderBlock();
  body_block_ = NULL;
  exit_block_ = NULL;
}


HValue* HGraphBuilder::LoopBuilder::BeginBody(
    HValue* initial,
    HValue* terminating,
    Token::Value token,
    Representation input_representation) {
  HEnvironment* env = builder_->environment();
  phi_ = new(zone()) HPhi(env->values()->length(), zone());
  header_block_->AddPhi(phi_);
  phi_->AddInput(initial);
  phi_->ChangeRepresentation(Representation::Integer32());
  env->Push(initial);
  builder_->current_block()->GotoNoSimulate(header_block_);

  HEnvironment* body_env = env->Copy();
  HEnvironment* exit_env = env->Copy();
  body_block_ = builder_->CreateBasicBlock(body_env);
  exit_block_ = builder_->CreateBasicBlock(exit_env);
  // Remove the phi from the expression stack
  body_env->Pop();

  builder_->set_current_block(header_block_);
  HCompareIDAndBranch* compare =
      new(zone()) HCompareIDAndBranch(phi_, terminating, token);
  compare->set_observed_input_representation(input_representation,
                                             input_representation);
  compare->ChangeRepresentation(input_representation);
  compare->SetSuccessorAt(0, body_block_);
  compare->SetSuccessorAt(1, exit_block_);
  builder_->current_block()->Finish(compare);

  builder_->set_current_block(body_block_);
  if (direction_ == kPreIncrement || direction_ == kPreDecrement) {
    HValue* one = builder_->graph()->GetConstant1();
    if (direction_ == kPreIncrement) {
      increment_ = HAdd::New(zone(), context_, phi_, one);
    } else {
      increment_ = HSub::New(zone(), context_, phi_, one);
    }
    increment_->ClearFlag(HValue::kCanOverflow);
    increment_->ChangeRepresentation(Representation::Integer32());
    builder_->AddInstruction(increment_);
    return increment_;
  } else {
    return phi_;
  }
}


void HGraphBuilder::LoopBuilder::EndBody() {
  ASSERT(!finished_);

  if (direction_ == kPostIncrement || direction_ == kPostDecrement) {
    HValue* one = builder_->graph()->GetConstant1();
    if (direction_ == kPostIncrement) {
      increment_ = HAdd::New(zone(), context_, phi_, one);
    } else {
      increment_ = HSub::New(zone(), context_, phi_, one);
    }
    increment_->ClearFlag(HValue::kCanOverflow);
    increment_->ChangeRepresentation(Representation::Integer32());
    builder_->AddInstruction(increment_);
  }

  // Push the new increment value on the expression stack to merge into the phi.
  builder_->environment()->Push(increment_);
  builder_->current_block()->GotoNoSimulate(header_block_);
  header_block_->loop_information()->RegisterBackEdge(body_block_);

  builder_->set_current_block(exit_block_);
  // Pop the phi from the expression stack
  builder_->environment()->Pop();
  finished_ = true;
}


HGraph* HGraphBuilder::CreateGraph() {
  graph_ = new(zone()) HGraph(info_);
  if (FLAG_hydrogen_stats) isolate()->GetHStatistics()->Initialize(info_);
  HPhase phase("H_Block building", isolate());
  set_current_block(graph()->entry_block());
  if (!BuildGraph()) return NULL;
  graph()->FinalizeUniqueValueIds();
  return graph_;
}


HInstruction* HGraphBuilder::AddInstruction(HInstruction* instr) {
  ASSERT(current_block() != NULL);
  current_block()->AddInstruction(instr);
  if (no_side_effects_scope_count_ > 0) {
    instr->SetFlag(HValue::kHasNoObservableSideEffects);
  }
  return instr;
}


void HGraphBuilder::AddSimulate(BailoutId id,
                                RemovableSimulate removable) {
  ASSERT(current_block() != NULL);
  ASSERT(no_side_effects_scope_count_ == 0);
  current_block()->AddSimulate(id, removable);
}


HBoundsCheck* HGraphBuilder::AddBoundsCheck(HValue* index,
                                            HValue* length,
                                            BoundsCheckKeyMode key_mode,
                                            Representation r) {
  if (!index->type().IsSmi()) {
    index = new(graph()->zone()) HCheckSmiOrInt32(index);
    AddInstruction(HCheckSmiOrInt32::cast(index));
  }
  if (!length->type().IsSmi()) {
    length = new(graph()->zone()) HCheckSmiOrInt32(length);
    AddInstruction(HCheckSmiOrInt32::cast(length));
  }
  HBoundsCheck* result = new(graph()->zone()) HBoundsCheck(
      index, length, key_mode, r);
  AddInstruction(result);
  return result;
}


HReturn* HGraphBuilder::AddReturn(HValue* value) {
  HValue* context = environment()->LookupContext();
  int num_parameters = graph()->info()->num_parameters();
  HValue* params = AddInstruction(new(graph()->zone())
      HConstant(num_parameters, Representation::Integer32()));
  HReturn* return_instruction = new(graph()->zone())
      HReturn(value, context, params);
  current_block()->FinishExit(return_instruction);
  return return_instruction;
}


HBasicBlock* HGraphBuilder::CreateBasicBlock(HEnvironment* env) {
  HBasicBlock* b = graph()->CreateBasicBlock();
  b->SetInitialEnvironment(env);
  return b;
}


HBasicBlock* HGraphBuilder::CreateLoopHeaderBlock() {
  HBasicBlock* header = graph()->CreateBasicBlock();
  HEnvironment* entry_env = environment()->CopyAsLoopHeader(header);
  header->SetInitialEnvironment(entry_env);
  header->AttachLoopInformation();
  return header;
}


HValue* HGraphBuilder::BuildCheckNonSmi(HValue* obj) {
  HCheckNonSmi* check = new(zone()) HCheckNonSmi(obj);
  AddInstruction(check);
  return check;
}


HValue* HGraphBuilder::BuildCheckMap(HValue* obj,
                                              Handle<Map> map) {
  HCheckMaps* check = HCheckMaps::New(obj, map, zone());
  AddInstruction(check);
  return check;
}


HInstruction* HGraphBuilder::BuildExternalArrayElementAccess(
    HValue* external_elements,
    HValue* checked_key,
    HValue* val,
    HValue* dependency,
    ElementsKind elements_kind,
    bool is_store) {
  Zone* zone = this->zone();
  if (is_store) {
    ASSERT(val != NULL);
    switch (elements_kind) {
      case EXTERNAL_PIXEL_ELEMENTS: {
        val = AddInstruction(new(zone) HClampToUint8(val));
        break;
      }
      case EXTERNAL_BYTE_ELEMENTS:
      case EXTERNAL_UNSIGNED_BYTE_ELEMENTS:
      case EXTERNAL_SHORT_ELEMENTS:
      case EXTERNAL_UNSIGNED_SHORT_ELEMENTS:
      case EXTERNAL_INT_ELEMENTS:
      case EXTERNAL_UNSIGNED_INT_ELEMENTS: {
        break;
      }
      case EXTERNAL_FLOAT_ELEMENTS:
      case EXTERNAL_DOUBLE_ELEMENTS:
        break;
      case FAST_SMI_ELEMENTS:
      case FAST_ELEMENTS:
      case FAST_DOUBLE_ELEMENTS:
      case FAST_HOLEY_SMI_ELEMENTS:
      case FAST_HOLEY_ELEMENTS:
      case FAST_HOLEY_DOUBLE_ELEMENTS:
      case DICTIONARY_ELEMENTS:
      case NON_STRICT_ARGUMENTS_ELEMENTS:
        UNREACHABLE();
        break;
    }
    return new(zone) HStoreKeyed(external_elements, checked_key,
                                 val, elements_kind);
  } else {
    ASSERT(val == NULL);
    HLoadKeyed* load =
       new(zone) HLoadKeyed(
           external_elements, checked_key, dependency, elements_kind);
    if (FLAG_opt_safe_uint32_operations &&
        elements_kind == EXTERNAL_UNSIGNED_INT_ELEMENTS) {
      graph()->RecordUint32Instruction(load);
    }
    return load;
  }
}


HInstruction* HGraphBuilder::BuildFastElementAccess(
    HValue* elements,
    HValue* checked_key,
    HValue* val,
    HValue* load_dependency,
    ElementsKind elements_kind,
    bool is_store,
    KeyedAccessStoreMode store_mode) {
  Zone* zone = this->zone();
  if (is_store) {
    ASSERT(val != NULL);
    switch (elements_kind) {
      case FAST_SMI_ELEMENTS:
      case FAST_HOLEY_SMI_ELEMENTS:
        // Smi-only arrays need a smi check.
        AddInstruction(new(zone) HCheckSmi(val));
        // Fall through.
      case FAST_ELEMENTS:
      case FAST_HOLEY_ELEMENTS:
      case FAST_DOUBLE_ELEMENTS:
      case FAST_HOLEY_DOUBLE_ELEMENTS:
        return new(zone) HStoreKeyed(elements, checked_key, val, elements_kind);
      default:
        UNREACHABLE();
        return NULL;
    }
  }
  // It's an element load (!is_store).
  return new(zone) HLoadKeyed(elements,
                              checked_key,
                              load_dependency,
                              elements_kind);
}


HValue* HGraphBuilder::BuildCheckForCapacityGrow(HValue* object,
                                                 HValue* elements,
                                                 ElementsKind kind,
                                                 HValue* length,
                                                 HValue* key,
                                                 bool is_js_array) {
  Zone* zone = this->zone();
  IfBuilder length_checker(this);

  length_checker.IfCompare(length, key, Token::EQ);
  length_checker.Then();

  HValue* current_capacity =
      AddInstruction(new(zone) HFixedArrayBaseLength(elements));

  IfBuilder capacity_checker(this);

  capacity_checker.IfCompare(length, current_capacity, Token::EQ);
  capacity_checker.Then();

  HValue* context = environment()->LookupContext();

  HValue* new_capacity =
      BuildNewElementsCapacity(context, current_capacity);

  HValue* new_elements = BuildGrowElementsCapacity(object, elements,
                                                   kind, length,
                                                   new_capacity);

  environment()->Push(new_elements);
  capacity_checker.Else();

  environment()->Push(elements);
  capacity_checker.End();

  if (is_js_array) {
    HValue* new_length = AddInstruction(
        HAdd::New(zone, context, length, graph_->GetConstant1()));
    new_length->ChangeRepresentation(Representation::Integer32());
    new_length->ClearFlag(HValue::kCanOverflow);

    Factory* factory = isolate()->factory();
    Representation representation = IsFastElementsKind(kind)
        ? Representation::Smi() : Representation::Tagged();
    HInstruction* length_store = AddInstruction(new(zone) HStoreNamedField(
        object,
        factory->length_field_string(),
        new_length, true,
        representation,
        JSArray::kLengthOffset));
    length_store->SetGVNFlag(kChangesArrayLengths);
  }

  length_checker.Else();

  AddBoundsCheck(key, length, ALLOW_SMI_KEY);
  environment()->Push(elements);

  length_checker.End();

  return environment()->Pop();
}


HValue* HGraphBuilder::BuildCopyElementsOnWrite(HValue* object,
                                                HValue* elements,
                                                ElementsKind kind,
                                                HValue* length) {
  Zone* zone = this->zone();
  Heap* heap = isolate()->heap();

  IfBuilder cow_checker(this);

  cow_checker.IfCompareMap(elements,
                           Handle<Map>(heap->fixed_cow_array_map()));
  cow_checker.Then();

  HValue* capacity =
      AddInstruction(new(zone) HFixedArrayBaseLength(elements));

  HValue* new_elements = BuildGrowElementsCapacity(object, elements,
                                                   kind, length, capacity);

  environment()->Push(new_elements);

  cow_checker.Else();

  environment()->Push(elements);

  cow_checker.End();

  return environment()->Pop();
}


HInstruction* HGraphBuilder::BuildUncheckedMonomorphicElementAccess(
    HValue* object,
    HValue* key,
    HValue* val,
    HCheckMaps* mapcheck,
    bool is_js_array,
    ElementsKind elements_kind,
    bool is_store,
    KeyedAccessStoreMode store_mode,
    Representation checked_index_representation) {
  ASSERT(!IsExternalArrayElementsKind(elements_kind) || !is_js_array);
  Zone* zone = this->zone();
  // No GVNFlag is necessary for ElementsKind if there is an explicit dependency
  // on a HElementsTransition instruction. The flag can also be removed if the
  // map to check has FAST_HOLEY_ELEMENTS, since there can be no further
  // ElementsKind transitions. Finally, the dependency can be removed for stores
  // for FAST_ELEMENTS, since a transition to HOLEY elements won't change the
  // generated store code.
  if ((elements_kind == FAST_HOLEY_ELEMENTS) ||
      (elements_kind == FAST_ELEMENTS && is_store)) {
    if (mapcheck != NULL) {
      mapcheck->ClearGVNFlag(kDependsOnElementsKind);
    }
  }
  bool fast_smi_only_elements = IsFastSmiElementsKind(elements_kind);
  bool fast_elements = IsFastObjectElementsKind(elements_kind);
  HValue* elements =
      AddInstruction(new(zone) HLoadElements(object, mapcheck));
  if (is_store && (fast_elements || fast_smi_only_elements) &&
      store_mode != STORE_NO_TRANSITION_HANDLE_COW) {
    HCheckMaps* check_cow_map = HCheckMaps::New(
        elements, isolate()->factory()->fixed_array_map(), zone);
    check_cow_map->ClearGVNFlag(kDependsOnElementsKind);
    AddInstruction(check_cow_map);
  }
  HInstruction* length = NULL;
  if (is_js_array) {
    length = AddInstruction(
        HLoadNamedField::NewArrayLength(zone, object, mapcheck, HType::Smi()));
  } else {
    length = AddInstruction(new(zone) HFixedArrayBaseLength(elements));
  }
  HValue* checked_key = NULL;
  if (IsExternalArrayElementsKind(elements_kind)) {
    if (store_mode == STORE_NO_TRANSITION_IGNORE_OUT_OF_BOUNDS) {
      NoObservableSideEffectsScope no_effects(this);
      HLoadExternalArrayPointer* external_elements =
          new(zone) HLoadExternalArrayPointer(elements);
      AddInstruction(external_elements);
      IfBuilder length_checker(this);
      length_checker.IfCompare(key, length, Token::LT);
      length_checker.Then();
      IfBuilder negative_checker(this);
      HValue* bounds_check = negative_checker.IfCompare(
          key, graph()->GetConstant0(), Token::GTE);
      negative_checker.Then();
      HInstruction* result = BuildExternalArrayElementAccess(
          external_elements, key, val, bounds_check,
          elements_kind, is_store);
      AddInstruction(result);
      negative_checker.ElseDeopt();
      length_checker.End();
      return result;
    } else {
      ASSERT(store_mode == STANDARD_STORE);
      checked_key = AddBoundsCheck(
          key, length, ALLOW_SMI_KEY, checked_index_representation);
      HLoadExternalArrayPointer* external_elements =
          new(zone) HLoadExternalArrayPointer(elements);
      AddInstruction(external_elements);
      return AddInstruction(BuildExternalArrayElementAccess(
          external_elements, checked_key, val, mapcheck,
          elements_kind, is_store));
    }
  }
  ASSERT(fast_smi_only_elements ||
         fast_elements ||
         IsFastDoubleElementsKind(elements_kind));

  if (is_store && IsFastSmiElementsKind(elements_kind) &&
      !val->type().IsSmi()) {
    AddInstruction(new(zone) HCheckSmi(val));
  }

  if (IsGrowStoreMode(store_mode)) {
    NoObservableSideEffectsScope no_effects(this);

    elements = BuildCheckForCapacityGrow(object, elements, elements_kind,
                                         length, key, is_js_array);
    if (!key->type().IsSmi()) {
      checked_key = AddInstruction(new(zone) HCheckSmiOrInt32(key));
    } else {
      checked_key = key;
    }
  } else {
    checked_key = AddBoundsCheck(
        key, length, ALLOW_SMI_KEY, checked_index_representation);

    if (is_store && (fast_elements || fast_smi_only_elements)) {
      if (store_mode == STORE_NO_TRANSITION_HANDLE_COW) {
        NoObservableSideEffectsScope no_effects(this);

        elements = BuildCopyElementsOnWrite(object, elements, elements_kind,
                                            length);
      } else {
        HCheckMaps* check_cow_map = HCheckMaps::New(
            elements, isolate()->factory()->fixed_array_map(), zone);
        check_cow_map->ClearGVNFlag(kDependsOnElementsKind);
        AddInstruction(check_cow_map);
      }
    }
  }
  return AddInstruction(
      BuildFastElementAccess(elements, checked_key, val, mapcheck,
                             elements_kind, is_store, store_mode));
}


HValue* HGraphBuilder::BuildAllocateElements(HValue* context,
                                             ElementsKind kind,
                                             HValue* capacity) {
  Zone* zone = this->zone();

  int elements_size = IsFastDoubleElementsKind(kind)
      ? kDoubleSize : kPointerSize;
  HConstant* elements_size_value =
      new(zone) HConstant(elements_size, Representation::Integer32());
  AddInstruction(elements_size_value);
  HValue* mul = AddInstruction(
                    HMul::New(zone, context, capacity, elements_size_value));
  mul->ChangeRepresentation(Representation::Integer32());
  mul->ClearFlag(HValue::kCanOverflow);

  HConstant* header_size =
      new(zone) HConstant(FixedArray::kHeaderSize, Representation::Integer32());
  AddInstruction(header_size);
  HValue* total_size = AddInstruction(
                           HAdd::New(zone, context, mul, header_size));
  total_size->ChangeRepresentation(Representation::Integer32());
  total_size->ClearFlag(HValue::kCanOverflow);

  HAllocate::Flags flags = HAllocate::DefaultFlags(kind);
  if (FLAG_pretenure_literals) {
    // TODO(hpayer): When pretenuring can be internalized, flags can become
    // private to HAllocate.
    if (IsFastDoubleElementsKind(kind)) {
      flags = static_cast<HAllocate::Flags>(
          flags | HAllocate::CAN_ALLOCATE_IN_OLD_DATA_SPACE);
    } else {
      flags = static_cast<HAllocate::Flags>(
          flags | HAllocate::CAN_ALLOCATE_IN_OLD_POINTER_SPACE);
    }
  }

  HValue* elements =
      AddInstruction(new(zone) HAllocate(context, total_size,
                                         HType::JSArray(), flags));
  return elements;
}


void HGraphBuilder::BuildInitializeElements(HValue* elements,
                                            ElementsKind kind,
                                            HValue* capacity) {
  Zone* zone = this->zone();
  Factory* factory = isolate()->factory();
  Handle<Map> map = IsFastDoubleElementsKind(kind)
      ? factory->fixed_double_array_map()
      : factory->fixed_array_map();
  BuildStoreMap(elements, map);

  Handle<String> fixed_array_length_field_name = factory->length_field_string();
  Representation representation = IsFastElementsKind(kind)
      ? Representation::Smi() : Representation::Tagged();
  HInstruction* store_length =
      new(zone) HStoreNamedField(elements, fixed_array_length_field_name,
                                 capacity, true, representation,
                                 FixedArray::kLengthOffset);
  AddInstruction(store_length);
}


HValue* HGraphBuilder::BuildAllocateAndInitializeElements(HValue* context,
                                                          ElementsKind kind,
                                                          HValue* capacity) {
  HValue* new_elements = BuildAllocateElements(context, kind, capacity);
  BuildInitializeElements(new_elements, kind, capacity);
  return new_elements;
}


HInnerAllocatedObject* HGraphBuilder::BuildJSArrayHeader(HValue* array,
    HValue* array_map,
    AllocationSiteMode mode,
    HValue* allocation_site_payload,
    HValue* length_field) {

  BuildStoreMap(array, array_map);

  HConstant* empty_fixed_array =
      new(zone()) HConstant(
          Handle<FixedArray>(isolate()->heap()->empty_fixed_array()),
          Representation::Tagged());
  AddInstruction(empty_fixed_array);

  AddInstruction(new(zone()) HStoreNamedField(array,
      isolate()->factory()->properties_field_symbol(),
      empty_fixed_array,
      true,
      Representation::Tagged(),
      JSArray::kPropertiesOffset));

  HInstruction* length_store = AddInstruction(
      new(zone()) HStoreNamedField(array,
                                   isolate()->factory()->length_field_string(),
                                   length_field,
                                   true,
                                   Representation::Tagged(),
                                   JSArray::kLengthOffset));
  length_store->SetGVNFlag(kChangesArrayLengths);

  if (mode == TRACK_ALLOCATION_SITE) {
    BuildCreateAllocationSiteInfo(array,
                                  JSArray::kSize,
                                  allocation_site_payload);
  }

  int elements_location = JSArray::kSize;
  if (mode == TRACK_ALLOCATION_SITE) {
    elements_location += AllocationSiteInfo::kSize;
  }

  HInnerAllocatedObject* elements = new(zone()) HInnerAllocatedObject(
      array,
      elements_location);
  AddInstruction(elements);

  HInstruction* elements_store = AddInstruction(
      new(zone()) HStoreNamedField(
          array,
          isolate()->factory()->elements_field_string(),
          elements,
          true,
          Representation::Tagged(),
          JSArray::kElementsOffset));
  elements_store->SetGVNFlag(kChangesElementsPointer);

  return elements;
}


HInstruction* HGraphBuilder::BuildStoreMap(HValue* object,
                                           HValue* map) {
  Zone* zone = this->zone();
  Factory* factory = isolate()->factory();
  Handle<String> map_field_name = factory->map_field_string();
  HInstruction* store_map =
      new(zone) HStoreNamedField(object, map_field_name, map,
                                 true, Representation::Tagged(),
                                 JSObject::kMapOffset);
  store_map->ClearGVNFlag(kChangesInobjectFields);
  store_map->SetGVNFlag(kChangesMaps);
  AddInstruction(store_map);
  return store_map;
}


HInstruction* HGraphBuilder::BuildStoreMap(HValue* object,
                                           Handle<Map> map) {
  Zone* zone = this->zone();
  HValue* map_constant =
      AddInstruction(new(zone) HConstant(map, Representation::Tagged()));
  return BuildStoreMap(object, map_constant);
}


HValue* HGraphBuilder::BuildNewElementsCapacity(HValue* context,
                                                HValue* old_capacity) {
  Zone* zone = this->zone();
  HValue* half_old_capacity =
      AddInstruction(HShr::New(zone, context, old_capacity,
                               graph_->GetConstant1()));
  half_old_capacity->ChangeRepresentation(Representation::Integer32());
  half_old_capacity->ClearFlag(HValue::kCanOverflow);

  HValue* new_capacity = AddInstruction(
      HAdd::New(zone, context, half_old_capacity, old_capacity));
  new_capacity->ChangeRepresentation(Representation::Integer32());
  new_capacity->ClearFlag(HValue::kCanOverflow);

  HValue* min_growth =
      AddInstruction(new(zone) HConstant(16, Representation::Integer32()));

  new_capacity = AddInstruction(
      HAdd::New(zone, context, new_capacity, min_growth));
  new_capacity->ChangeRepresentation(Representation::Integer32());
  new_capacity->ClearFlag(HValue::kCanOverflow);

  return new_capacity;
}


void HGraphBuilder::BuildNewSpaceArrayCheck(HValue* length, ElementsKind kind) {
  Zone* zone = this->zone();
  Heap* heap = isolate()->heap();
  int element_size = IsFastDoubleElementsKind(kind) ? kDoubleSize
                                                    : kPointerSize;
  int max_size = heap->MaxNewSpaceAllocationSize() / element_size;
  max_size -= JSArray::kSize / element_size;
  HConstant* max_size_constant =
      new(zone) HConstant(max_size, Representation::Integer32());
  AddInstruction(max_size_constant);
  // Since we're forcing Integer32 representation for this HBoundsCheck,
  // there's no need to Smi-check the index.
  AddInstruction(new(zone)
                 HBoundsCheck(length, max_size_constant,
                              DONT_ALLOW_SMI_KEY, Representation::Integer32()));
}


HValue* HGraphBuilder::BuildGrowElementsCapacity(HValue* object,
                                                 HValue* elements,
                                                 ElementsKind kind,
                                                 HValue* length,
                                                 HValue* new_capacity) {
  Zone* zone = this->zone();
  HValue* context = environment()->LookupContext();

  BuildNewSpaceArrayCheck(new_capacity, kind);

  HValue* new_elements =
      BuildAllocateAndInitializeElements(context, kind, new_capacity);

  BuildCopyElements(context, elements, kind,
                    new_elements, kind,
                    length, new_capacity);

  Factory* factory = isolate()->factory();
  HInstruction* elements_store = AddInstruction(new(zone) HStoreNamedField(
      object,
      factory->elements_field_string(),
      new_elements, true, Representation::Tagged(),
      JSArray::kElementsOffset));
  elements_store->SetGVNFlag(kChangesElementsPointer);

  return new_elements;
}


void HGraphBuilder::BuildFillElementsWithHole(HValue* context,
                                              HValue* elements,
                                              ElementsKind elements_kind,
                                              HValue* from,
                                              HValue* to) {
  // Fast elements kinds need to be initialized in case statements below cause
  // a garbage collection.
  Factory* factory = isolate()->factory();

  double nan_double = FixedDoubleArray::hole_nan_as_double();
  Zone* zone = this->zone();
  HValue* hole = IsFastSmiOrObjectElementsKind(elements_kind)
      ? AddInstruction(new(zone) HConstant(factory->the_hole_value(),
                                           Representation::Tagged()))
      : AddInstruction(new(zone) HConstant(nan_double,
                                           Representation::Double()));

  // Special loop unfolding case
  static const int kLoopUnfoldLimit = 4;
  bool unfold_loop = false;
  int initial_capacity = JSArray::kPreallocatedArrayElements;
  if (from->IsConstant() && to->IsConstant() &&
      initial_capacity <= kLoopUnfoldLimit) {
    HConstant* constant_from = HConstant::cast(from);
    HConstant* constant_to = HConstant::cast(to);

    if (constant_from->HasInteger32Value() &&
        constant_from->Integer32Value() == 0 &&
        constant_to->HasInteger32Value() &&
        constant_to->Integer32Value() == initial_capacity) {
      unfold_loop = true;
    }
  }

  if (unfold_loop) {
    for (int i = 0; i < initial_capacity; i++) {
      HInstruction* key = AddInstruction(new(zone)
                             HConstant(i, Representation::Integer32()));
      AddInstruction(new(zone) HStoreKeyed(elements, key, hole, elements_kind));
    }
  } else {
    LoopBuilder builder(this, context, LoopBuilder::kPostIncrement);

    HValue* key = builder.BeginBody(from, to, Token::LT);

    AddInstruction(new(zone) HStoreKeyed(elements, key, hole, elements_kind));

    builder.EndBody();
  }
}


void HGraphBuilder::BuildCopyElements(HValue* context,
                                      HValue* from_elements,
                                      ElementsKind from_elements_kind,
                                      HValue* to_elements,
                                      ElementsKind to_elements_kind,
                                      HValue* length,
                                      HValue* capacity) {
  bool pre_fill_with_holes =
      IsFastDoubleElementsKind(from_elements_kind) &&
      IsFastObjectElementsKind(to_elements_kind);

  if (pre_fill_with_holes) {
    // If the copy might trigger a GC, make sure that the FixedArray is
    // pre-initialized with holes to make sure that it's always in a consistent
    // state.
    BuildFillElementsWithHole(context, to_elements, to_elements_kind,
                              graph()->GetConstant0(), capacity);
  }

  LoopBuilder builder(this, context, LoopBuilder::kPostIncrement);

  HValue* key = builder.BeginBody(graph()->GetConstant0(), length, Token::LT);

  HValue* element =
      AddInstruction(new(zone()) HLoadKeyed(from_elements, key, NULL,
                                            from_elements_kind,
                                            ALLOW_RETURN_HOLE));

  AddInstruction(new(zone()) HStoreKeyed(to_elements, key, element,
                                         to_elements_kind));

  builder.EndBody();

  if (!pre_fill_with_holes && length != capacity) {
    // Fill unused capacity with the hole.
    BuildFillElementsWithHole(context, to_elements, to_elements_kind,
                              key, capacity);
  }
}


HValue* HGraphBuilder::BuildCloneShallowArray(HContext* context,
                                              HValue* boilerplate,
                                              AllocationSiteMode mode,
                                              ElementsKind kind,
                                              int length) {
  Zone* zone = this->zone();
  Factory* factory = isolate()->factory();

  NoObservableSideEffectsScope no_effects(this);

  // All sizes here are multiples of kPointerSize.
  int size = JSArray::kSize;
  if (mode == TRACK_ALLOCATION_SITE) {
    size += AllocationSiteInfo::kSize;
  }
  int elems_offset = size;
  if (length > 0) {
    size += IsFastDoubleElementsKind(kind)
        ? FixedDoubleArray::SizeFor(length)
        : FixedArray::SizeFor(length);
  }

  HAllocate::Flags allocate_flags = HAllocate::DefaultFlags(kind);
  // Allocate both the JS array and the elements array in one big
  // allocation. This avoids multiple limit checks.
  HValue* size_in_bytes =
      AddInstruction(new(zone) HConstant(size, Representation::Integer32()));
  HInstruction* object =
      AddInstruction(new(zone) HAllocate(context,
                                         size_in_bytes,
                                         HType::JSObject(),
                                         allocate_flags));

  // Copy the JS array part.
  for (int i = 0; i < JSArray::kSize; i += kPointerSize) {
    if ((i != JSArray::kElementsOffset) || (length == 0)) {
      HInstruction* value = AddInstruction(new(zone) HLoadNamedField(
          boilerplate, true, Representation::Tagged(), i));
      if (i != JSArray::kMapOffset) {
        AddInstruction(new(zone) HStoreNamedField(object,
                                                  factory->empty_string(),
                                                  value, true,
                                                  Representation::Tagged(), i));
      } else {
        BuildStoreMap(object, value);
      }
    }
  }

  // Create an allocation site info if requested.
  if (mode == TRACK_ALLOCATION_SITE) {
    BuildCreateAllocationSiteInfo(object, JSArray::kSize, boilerplate);
  }

  if (length > 0) {
    // Get hold of the elements array of the boilerplate and setup the
    // elements pointer in the resulting object.
    HValue* boilerplate_elements =
        AddInstruction(new(zone) HLoadElements(boilerplate, NULL));
    HValue* object_elements =
        AddInstruction(new(zone) HInnerAllocatedObject(object, elems_offset));
    AddInstruction(new(zone) HStoreNamedField(object,
                                              factory->elements_field_string(),
                                              object_elements, true,
                                              Representation::Tagged(),
                                              JSObject::kElementsOffset));

    // Copy the elements array header.
    for (int i = 0; i < FixedArrayBase::kHeaderSize; i += kPointerSize) {
      HInstruction* value =
          AddInstruction(new(zone) HLoadNamedField(
              boilerplate_elements, true, Representation::Tagged(), i));
      AddInstruction(new(zone) HStoreNamedField(object_elements,
                                                factory->empty_string(),
                                                value, true,
                                                Representation::Tagged(), i));
    }

    // Copy the elements array contents.
    // TODO(mstarzinger): Teach HGraphBuilder::BuildCopyElements to unfold
    // copying loops with constant length up to a given boundary and use this
    // helper here instead.
    for (int i = 0; i < length; i++) {
      HValue* key_constant =
          AddInstruction(new(zone) HConstant(i, Representation::Integer32()));
      HInstruction* value =
          AddInstruction(new(zone) HLoadKeyed(boilerplate_elements,
                                              key_constant,
                                              NULL,
                                              kind));
      AddInstruction(new(zone) HStoreKeyed(object_elements,
                                           key_constant,
                                           value,
                                           kind));
    }
  }

  return object;
}


void HGraphBuilder::BuildCompareNil(
    HValue* value,
    EqualityKind kind,
    CompareNilICStub::Types types,
    Handle<Map> map,
    int position,
    HIfContinuation* continuation) {
  IfBuilder if_nil(this, position);
  bool needs_or = false;
  if ((types & CompareNilICStub::kCompareAgainstNull) != 0) {
    if (needs_or) if_nil.Or();
    if_nil.If<HCompareObjectEqAndBranch>(value, graph()->GetConstantNull());
    needs_or = true;
  }
  if ((types & CompareNilICStub::kCompareAgainstUndefined) != 0) {
    if (needs_or) if_nil.Or();
    if_nil.If<HCompareObjectEqAndBranch>(value,
                                         graph()->GetConstantUndefined());
    needs_or = true;
  }
  // Handle either undetectable or monomorphic, not both.
  ASSERT(((types & CompareNilICStub::kCompareAgainstUndetectable) == 0) ||
         ((types & CompareNilICStub::kCompareAgainstMonomorphicMap) == 0));
  if ((types & CompareNilICStub::kCompareAgainstUndetectable) != 0) {
    if (needs_or) if_nil.Or();
    if_nil.If<HIsUndetectableAndBranch>(value);
  } else {
    if_nil.Then();
    if_nil.Else();
    if ((types & CompareNilICStub::kCompareAgainstMonomorphicMap) != 0) {
      BuildCheckNonSmi(value);
      // For ICs, the map checked below is a sentinel map that gets replaced by
      // the monomorphic map when the code is used as a template to generate a
      // new IC. For optimized functions, there is no sentinel map, the map
      // emitted below is the actual monomorphic map.
      BuildCheckMap(value, map);
    } else {
      if (kind == kNonStrictEquality) {
        if_nil.Deopt();
      }
    }
  }

  if_nil.CaptureContinuation(continuation);
}


HValue* HGraphBuilder::BuildCreateAllocationSiteInfo(HValue* previous_object,
                                                     int previous_object_size,
                                                     HValue* payload) {
  HInnerAllocatedObject* alloc_site = new(zone())
        HInnerAllocatedObject(previous_object, previous_object_size);
  AddInstruction(alloc_site);
  Handle<Map> alloc_site_map(isolate()->heap()->allocation_site_info_map());
  BuildStoreMap(alloc_site, alloc_site_map);
  AddInstruction(new(zone()) HStoreNamedField(alloc_site,
      isolate()->factory()->payload_string(),
      payload,
      true,
      Representation::Tagged(),
      AllocationSiteInfo::kPayloadOffset));
  return alloc_site;
}


HGraphBuilder::JSArrayBuilder::JSArrayBuilder(HGraphBuilder* builder,
                                              ElementsKind kind,
                                              HValue* allocation_site_payload,
                                              AllocationSiteMode mode) :
        builder_(builder),
        kind_(kind),
        allocation_site_payload_(allocation_site_payload) {
  if (mode == DONT_TRACK_ALLOCATION_SITE) {
    mode_ = mode;
  } else {
    mode_ = AllocationSiteInfo::GetMode(kind);
  }
}


HValue* HGraphBuilder::JSArrayBuilder::EmitMapCode(HValue* context) {
  // Get the global context, the native context, the map array
  HInstruction* global_object = AddInstruction(new(zone())
      HGlobalObject(context));
  HInstruction* native_context = AddInstruction(new(zone())
      HLoadNamedField(global_object, true, Representation::Tagged(),
                      GlobalObject::kNativeContextOffset));
  int offset = Context::kHeaderSize +
      kPointerSize * Context::JS_ARRAY_MAPS_INDEX;
  HInstruction* map_array = AddInstruction(new(zone())
      HLoadNamedField(native_context, true, Representation::Tagged(), offset));
  offset = kind_ * kPointerSize + FixedArrayBase::kHeaderSize;
  return AddInstruction(new(zone()) HLoadNamedField(
      map_array, true, Representation::Tagged(), offset));
}


HValue* HGraphBuilder::JSArrayBuilder::EstablishAllocationSize(
    HValue* length_node) {
  HValue* context = builder()->environment()->LookupContext();
  ASSERT(length_node != NULL);

  int base_size = JSArray::kSize;
  if (mode_ == TRACK_ALLOCATION_SITE) {
    base_size += AllocationSiteInfo::kSize;
  }

  if (IsFastDoubleElementsKind(kind_)) {
    base_size += FixedDoubleArray::kHeaderSize;
  } else {
    base_size += FixedArray::kHeaderSize;
  }

  HInstruction* elements_size_value = new(zone())
      HConstant(elements_size(), Representation::Integer32());
  AddInstruction(elements_size_value);
  HInstruction* mul = HMul::New(zone(), context, length_node,
                                elements_size_value);
  mul->ChangeRepresentation(Representation::Integer32());
  mul->ClearFlag(HValue::kCanOverflow);
  AddInstruction(mul);

  HInstruction* base = new(zone()) HConstant(base_size,
                                             Representation::Integer32());
  AddInstruction(base);
  HInstruction* total_size = HAdd::New(zone(), context, base, mul);
  total_size->ChangeRepresentation(Representation::Integer32());
  total_size->ClearFlag(HValue::kCanOverflow);
  AddInstruction(total_size);
  return total_size;
}


HValue* HGraphBuilder::JSArrayBuilder::EstablishEmptyArrayAllocationSize() {
  int base_size = JSArray::kSize;
  if (mode_ == TRACK_ALLOCATION_SITE) {
    base_size += AllocationSiteInfo::kSize;
  }

  base_size += IsFastDoubleElementsKind(kind_)
      ? FixedDoubleArray::SizeFor(initial_capacity())
      : FixedArray::SizeFor(initial_capacity());

  HConstant* array_size =
      new(zone()) HConstant(base_size, Representation::Integer32());
  AddInstruction(array_size);
  return array_size;
}


HValue* HGraphBuilder::JSArrayBuilder::AllocateEmptyArray() {
  HValue* size_in_bytes = EstablishEmptyArrayAllocationSize();
  HConstant* capacity =
      new(zone()) HConstant(initial_capacity(), Representation::Integer32());
  AddInstruction(capacity);
  return AllocateArray(size_in_bytes,
                       capacity,
                       builder()->graph()->GetConstant0(),
                       true);
}


HValue* HGraphBuilder::JSArrayBuilder::AllocateArray(HValue* capacity,
                                                     HValue* length_field,
                                                     bool fill_with_hole) {
  HValue* size_in_bytes = EstablishAllocationSize(capacity);
  return AllocateArray(size_in_bytes, capacity, length_field, fill_with_hole);
}


HValue* HGraphBuilder::JSArrayBuilder::AllocateArray(HValue* size_in_bytes,
                                                     HValue* capacity,
                                                     HValue* length_field,
                                                     bool fill_with_hole) {
  HValue* context = builder()->environment()->LookupContext();

  // Allocate (dealing with failure appropriately)
  HAllocate::Flags flags = HAllocate::DefaultFlags(kind_);
  HAllocate* new_object = new(zone()) HAllocate(context, size_in_bytes,
                                                HType::JSArray(), flags);
  AddInstruction(new_object);

  // Fill in the fields: map, properties, length
  HValue* map = EmitMapCode(context);
  elements_location_ = builder()->BuildJSArrayHeader(new_object,
                                                     map,
                                                     mode_,
                                                     allocation_site_payload_,
                                                     length_field);

  // Initialize the elements
  builder()->BuildInitializeElements(elements_location_, kind_, capacity);

  if (fill_with_hole) {
    builder()->BuildFillElementsWithHole(context, elements_location_, kind_,
                                         graph()->GetConstant0(), capacity);
  }

  return new_object;
}


HOptimizedGraphBuilder::HOptimizedGraphBuilder(CompilationInfo* info,
                                               TypeFeedbackOracle* oracle)
    : HGraphBuilder(info),
      function_state_(NULL),
      initial_function_state_(this, info, oracle, NORMAL_RETURN),
      ast_context_(NULL),
      break_scope_(NULL),
      inlined_count_(0),
      globals_(10, info->zone()),
      inline_bailout_(false) {
  // This is not initialized in the initializer list because the
  // constructor for the initial state relies on function_state_ == NULL
  // to know it's the initial state.
  function_state_= &initial_function_state_;
  InitializeAstVisitor();
}


HBasicBlock* HOptimizedGraphBuilder::CreateJoin(HBasicBlock* first,
                                                HBasicBlock* second,
                                                BailoutId join_id) {
  if (first == NULL) {
    return second;
  } else if (second == NULL) {
    return first;
  } else {
    HBasicBlock* join_block = graph()->CreateBasicBlock();
    first->Goto(join_block);
    second->Goto(join_block);
    join_block->SetJoinId(join_id);
    return join_block;
  }
}


HBasicBlock* HOptimizedGraphBuilder::JoinContinue(IterationStatement* statement,
                                                  HBasicBlock* exit_block,
                                                  HBasicBlock* continue_block) {
  if (continue_block != NULL) {
    if (exit_block != NULL) exit_block->Goto(continue_block);
    continue_block->SetJoinId(statement->ContinueId());
    return continue_block;
  }
  return exit_block;
}


HBasicBlock* HOptimizedGraphBuilder::CreateLoop(IterationStatement* statement,
                                                HBasicBlock* loop_entry,
                                                HBasicBlock* body_exit,
                                                HBasicBlock* loop_successor,
                                                HBasicBlock* break_block) {
  if (body_exit != NULL) body_exit->Goto(loop_entry);
  loop_entry->PostProcessLoopHeader(statement);
  if (break_block != NULL) {
    if (loop_successor != NULL) loop_successor->Goto(break_block);
    break_block->SetJoinId(statement->ExitId());
    return break_block;
  }
  return loop_successor;
}


void HBasicBlock::FinishExit(HControlInstruction* instruction) {
  Finish(instruction);
  ClearEnvironment();
}


HGraph::HGraph(CompilationInfo* info)
    : isolate_(info->isolate()),
      next_block_id_(0),
      entry_block_(NULL),
      blocks_(8, info->zone()),
      values_(16, info->zone()),
      phi_list_(NULL),
      uint32_instructions_(NULL),
      info_(info),
      zone_(info->zone()),
      is_recursive_(false),
      use_optimistic_licm_(false),
      has_soft_deoptimize_(false),
      type_change_checksum_(0) {
  if (info->IsStub()) {
    HydrogenCodeStub* stub = info->code_stub();
    CodeStubInterfaceDescriptor* descriptor =
        stub->GetInterfaceDescriptor(isolate_);
    start_environment_ =
        new(zone_) HEnvironment(zone_, descriptor->environment_length());
  } else {
    start_environment_ =
        new(zone_) HEnvironment(NULL, info->scope(), info->closure(), zone_);
  }
  start_environment_->set_ast_id(BailoutId::FunctionEntry());
  entry_block_ = CreateBasicBlock();
  entry_block_->SetInitialEnvironment(start_environment_);
}


HBasicBlock* HGraph::CreateBasicBlock() {
  HBasicBlock* result = new(zone()) HBasicBlock(this);
  blocks_.Add(result, zone());
  return result;
}


void HGraph::FinalizeUniqueValueIds() {
  AssertNoAllocation no_gc;
  ASSERT(!isolate()->optimizing_compiler_thread()->IsOptimizerThread());
  for (int i = 0; i < blocks()->length(); ++i) {
    for (HInstruction* instr = blocks()->at(i)->first();
        instr != NULL;
        instr = instr->next()) {
      instr->FinalizeUniqueValueId();
    }
  }
}


void HGraph::Canonicalize() {
  if (!FLAG_use_canonicalizing) return;
  HPhase phase("H_Canonicalize", this);
  for (int i = 0; i < blocks()->length(); ++i) {
    HInstruction* instr = blocks()->at(i)->first();
    while (instr != NULL) {
      HValue* value = instr->Canonicalize();
      if (value != instr) instr->DeleteAndReplaceWith(value);
      instr = instr->next();
    }
  }
}

// Block ordering was implemented with two mutually recursive methods,
// HGraph::Postorder and HGraph::PostorderLoopBlocks.
// The recursion could lead to stack overflow so the algorithm has been
// implemented iteratively.
// At a high level the algorithm looks like this:
//
// Postorder(block, loop_header) : {
//   if (block has already been visited or is of another loop) return;
//   mark block as visited;
//   if (block is a loop header) {
//     VisitLoopMembers(block, loop_header);
//     VisitSuccessorsOfLoopHeader(block);
//   } else {
//     VisitSuccessors(block)
//   }
//   put block in result list;
// }
//
// VisitLoopMembers(block, outer_loop_header) {
//   foreach (block b in block loop members) {
//     VisitSuccessorsOfLoopMember(b, outer_loop_header);
//     if (b is loop header) VisitLoopMembers(b);
//   }
// }
//
// VisitSuccessorsOfLoopMember(block, outer_loop_header) {
//   foreach (block b in block successors) Postorder(b, outer_loop_header)
// }
//
// VisitSuccessorsOfLoopHeader(block) {
//   foreach (block b in block successors) Postorder(b, block)
// }
//
// VisitSuccessors(block, loop_header) {
//   foreach (block b in block successors) Postorder(b, loop_header)
// }
//
// The ordering is started calling Postorder(entry, NULL).
//
// Each instance of PostorderProcessor represents the "stack frame" of the
// recursion, and particularly keeps the state of the loop (iteration) of the
// "Visit..." function it represents.
// To recycle memory we keep all the frames in a double linked list but
// this means that we cannot use constructors to initialize the frames.
//
class PostorderProcessor : public ZoneObject {
 public:
  // Back link (towards the stack bottom).
  PostorderProcessor* parent() {return father_; }
  // Forward link (towards the stack top).
  PostorderProcessor* child() {return child_; }
  HBasicBlock* block() { return block_; }
  HLoopInformation* loop() { return loop_; }
  HBasicBlock* loop_header() { return loop_header_; }

  static PostorderProcessor* CreateEntryProcessor(Zone* zone,
                                                  HBasicBlock* block,
                                                  BitVector* visited) {
    PostorderProcessor* result = new(zone) PostorderProcessor(NULL);
    return result->SetupSuccessors(zone, block, NULL, visited);
  }

  PostorderProcessor* PerformStep(Zone* zone,
                                  BitVector* visited,
                                  ZoneList<HBasicBlock*>* order) {
    PostorderProcessor* next =
        PerformNonBacktrackingStep(zone, visited, order);
    if (next != NULL) {
      return next;
    } else {
      return Backtrack(zone, visited, order);
    }
  }

 private:
  explicit PostorderProcessor(PostorderProcessor* father)
      : father_(father), child_(NULL), successor_iterator(NULL) { }

  // Each enum value states the cycle whose state is kept by this instance.
  enum LoopKind {
    NONE,
    SUCCESSORS,
    SUCCESSORS_OF_LOOP_HEADER,
    LOOP_MEMBERS,
    SUCCESSORS_OF_LOOP_MEMBER
  };

  // Each "Setup..." method is like a constructor for a cycle state.
  PostorderProcessor* SetupSuccessors(Zone* zone,
                                      HBasicBlock* block,
                                      HBasicBlock* loop_header,
                                      BitVector* visited) {
    if (block == NULL || visited->Contains(block->block_id()) ||
        block->parent_loop_header() != loop_header) {
      kind_ = NONE;
      block_ = NULL;
      loop_ = NULL;
      loop_header_ = NULL;
      return this;
    } else {
      block_ = block;
      loop_ = NULL;
      visited->Add(block->block_id());

      if (block->IsLoopHeader()) {
        kind_ = SUCCESSORS_OF_LOOP_HEADER;
        loop_header_ = block;
        InitializeSuccessors();
        PostorderProcessor* result = Push(zone);
        return result->SetupLoopMembers(zone, block, block->loop_information(),
                                        loop_header);
      } else {
        ASSERT(block->IsFinished());
        kind_ = SUCCESSORS;
        loop_header_ = loop_header;
        InitializeSuccessors();
        return this;
      }
    }
  }

  PostorderProcessor* SetupLoopMembers(Zone* zone,
                                       HBasicBlock* block,
                                       HLoopInformation* loop,
                                       HBasicBlock* loop_header) {
    kind_ = LOOP_MEMBERS;
    block_ = block;
    loop_ = loop;
    loop_header_ = loop_header;
    InitializeLoopMembers();
    return this;
  }

  PostorderProcessor* SetupSuccessorsOfLoopMember(
      HBasicBlock* block,
      HLoopInformation* loop,
      HBasicBlock* loop_header) {
    kind_ = SUCCESSORS_OF_LOOP_MEMBER;
    block_ = block;
    loop_ = loop;
    loop_header_ = loop_header;
    InitializeSuccessors();
    return this;
  }

  // This method "allocates" a new stack frame.
  PostorderProcessor* Push(Zone* zone) {
    if (child_ == NULL) {
      child_ = new(zone) PostorderProcessor(this);
    }
    return child_;
  }

  void ClosePostorder(ZoneList<HBasicBlock*>* order, Zone* zone) {
    ASSERT(block_->end()->FirstSuccessor() == NULL ||
           order->Contains(block_->end()->FirstSuccessor()) ||
           block_->end()->FirstSuccessor()->IsLoopHeader());
    ASSERT(block_->end()->SecondSuccessor() == NULL ||
           order->Contains(block_->end()->SecondSuccessor()) ||
           block_->end()->SecondSuccessor()->IsLoopHeader());
    order->Add(block_, zone);
  }

  // This method is the basic block to walk up the stack.
  PostorderProcessor* Pop(Zone* zone,
                          BitVector* visited,
                          ZoneList<HBasicBlock*>* order) {
    switch (kind_) {
      case SUCCESSORS:
      case SUCCESSORS_OF_LOOP_HEADER:
        ClosePostorder(order, zone);
        return father_;
      case LOOP_MEMBERS:
        return father_;
      case SUCCESSORS_OF_LOOP_MEMBER:
        if (block()->IsLoopHeader() && block() != loop_->loop_header()) {
          // In this case we need to perform a LOOP_MEMBERS cycle so we
          // initialize it and return this instead of father.
          return SetupLoopMembers(zone, block(),
                                  block()->loop_information(), loop_header_);
        } else {
          return father_;
        }
      case NONE:
        return father_;
    }
    UNREACHABLE();
    return NULL;
  }

  // Walks up the stack.
  PostorderProcessor* Backtrack(Zone* zone,
                                BitVector* visited,
                                ZoneList<HBasicBlock*>* order) {
    PostorderProcessor* parent = Pop(zone, visited, order);
    while (parent != NULL) {
      PostorderProcessor* next =
          parent->PerformNonBacktrackingStep(zone, visited, order);
      if (next != NULL) {
        return next;
      } else {
        parent = parent->Pop(zone, visited, order);
      }
    }
    return NULL;
  }

  PostorderProcessor* PerformNonBacktrackingStep(
      Zone* zone,
      BitVector* visited,
      ZoneList<HBasicBlock*>* order) {
    HBasicBlock* next_block;
    switch (kind_) {
      case SUCCESSORS:
        next_block = AdvanceSuccessors();
        if (next_block != NULL) {
          PostorderProcessor* result = Push(zone);
          return result->SetupSuccessors(zone, next_block,
                                         loop_header_, visited);
        }
        break;
      case SUCCESSORS_OF_LOOP_HEADER:
        next_block = AdvanceSuccessors();
        if (next_block != NULL) {
          PostorderProcessor* result = Push(zone);
          return result->SetupSuccessors(zone, next_block,
                                         block(), visited);
        }
        break;
      case LOOP_MEMBERS:
        next_block = AdvanceLoopMembers();
        if (next_block != NULL) {
          PostorderProcessor* result = Push(zone);
          return result->SetupSuccessorsOfLoopMember(next_block,
                                                     loop_, loop_header_);
        }
        break;
      case SUCCESSORS_OF_LOOP_MEMBER:
        next_block = AdvanceSuccessors();
        if (next_block != NULL) {
          PostorderProcessor* result = Push(zone);
          return result->SetupSuccessors(zone, next_block,
                                         loop_header_, visited);
        }
        break;
      case NONE:
        return NULL;
    }
    return NULL;
  }

  // The following two methods implement a "foreach b in successors" cycle.
  void InitializeSuccessors() {
    loop_index = 0;
    loop_length = 0;
    successor_iterator = HSuccessorIterator(block_->end());
  }

  HBasicBlock* AdvanceSuccessors() {
    if (!successor_iterator.Done()) {
      HBasicBlock* result = successor_iterator.Current();
      successor_iterator.Advance();
      return result;
    }
    return NULL;
  }

  // The following two methods implement a "foreach b in loop members" cycle.
  void InitializeLoopMembers() {
    loop_index = 0;
    loop_length = loop_->blocks()->length();
  }

  HBasicBlock* AdvanceLoopMembers() {
    if (loop_index < loop_length) {
      HBasicBlock* result = loop_->blocks()->at(loop_index);
      loop_index++;
      return result;
    } else {
      return NULL;
    }
  }

  LoopKind kind_;
  PostorderProcessor* father_;
  PostorderProcessor* child_;
  HLoopInformation* loop_;
  HBasicBlock* block_;
  HBasicBlock* loop_header_;
  int loop_index;
  int loop_length;
  HSuccessorIterator successor_iterator;
};


void HGraph::OrderBlocks() {
  HPhase phase("H_Block ordering", isolate());
  BitVector visited(blocks_.length(), zone());

  ZoneList<HBasicBlock*> reverse_result(8, zone());
  HBasicBlock* start = blocks_[0];
  PostorderProcessor* postorder =
      PostorderProcessor::CreateEntryProcessor(zone(), start, &visited);
  while (postorder != NULL) {
    postorder = postorder->PerformStep(zone(), &visited, &reverse_result);
  }
  blocks_.Rewind(0);
  int index = 0;
  for (int i = reverse_result.length() - 1; i >= 0; --i) {
    HBasicBlock* b = reverse_result[i];
    blocks_.Add(b, zone());
    b->set_block_id(index++);
  }
}


void HGraph::AssignDominators() {
  HPhase phase("H_Assign dominators", this);
  for (int i = 0; i < blocks_.length(); ++i) {
    HBasicBlock* block = blocks_[i];
    if (block->IsLoopHeader()) {
      // Only the first predecessor of a loop header is from outside the loop.
      // All others are back edges, and thus cannot dominate the loop header.
      block->AssignCommonDominator(block->predecessors()->first());
      block->AssignLoopSuccessorDominators();
    } else {
      for (int j = blocks_[i]->predecessors()->length() - 1; j >= 0; --j) {
        blocks_[i]->AssignCommonDominator(blocks_[i]->predecessors()->at(j));
      }
    }
  }
}


// Mark all blocks that are dominated by an unconditional soft deoptimize to
// prevent code motion across those blocks.
void HGraph::PropagateDeoptimizingMark() {
  HPhase phase("H_Propagate deoptimizing mark", this);
  // Skip this phase if there is nothing to be done anyway.
  if (!has_soft_deoptimize()) return;
  MarkAsDeoptimizingRecursively(entry_block());
  NullifyUnreachableInstructions();
}


void HGraph::MarkAsDeoptimizingRecursively(HBasicBlock* block) {
  for (int i = 0; i < block->dominated_blocks()->length(); ++i) {
    HBasicBlock* dominated = block->dominated_blocks()->at(i);
    if (block->IsDeoptimizing()) dominated->MarkAsDeoptimizing();
    MarkAsDeoptimizingRecursively(dominated);
  }
}


void HGraph::NullifyUnreachableInstructions() {
  if (!FLAG_unreachable_code_elimination) return;
  int block_count = blocks_.length();
  for (int i = 0; i < block_count; ++i) {
    HBasicBlock* block = blocks_.at(i);
    bool nullify = false;
    const ZoneList<HBasicBlock*>* predecessors = block->predecessors();
    int predecessors_length = predecessors->length();
    bool all_predecessors_deoptimizing = (predecessors_length > 0);
    for (int j = 0; j < predecessors_length; ++j) {
      if (!predecessors->at(j)->IsDeoptimizing()) {
        all_predecessors_deoptimizing = false;
        break;
      }
    }
    if (all_predecessors_deoptimizing) nullify = true;
    for (HInstruction* instr = block->first(); instr != NULL;
         instr = instr->next()) {
      // Leave the basic structure of the graph intact.
      if (instr->IsBlockEntry()) continue;
      if (instr->IsControlInstruction()) continue;
      if (instr->IsSimulate()) continue;
      if (instr->IsEnterInlined()) continue;
      if (instr->IsLeaveInlined()) continue;
      if (nullify) {
        HInstruction* last_dummy = NULL;
        for (int j = 0; j < instr->OperandCount(); ++j) {
          HValue* operand = instr->OperandAt(j);
          // Insert an HDummyUse for each operand, unless the operand
          // is an HDummyUse itself. If it's even from the same block,
          // remember it as a potential replacement for the instruction.
          if (operand->IsDummyUse()) {
            if (operand->block() == instr->block() &&
                last_dummy == NULL) {
              last_dummy = HInstruction::cast(operand);
            }
            continue;
          }
          if (operand->IsControlInstruction()) {
            // Inserting a dummy use for a value that's not defined anywhere
            // will fail. Some instructions define fake inputs on such
            // values as control flow dependencies.
            continue;
          }
          HDummyUse* dummy = new(zone()) HDummyUse(operand);
          dummy->InsertBefore(instr);
          last_dummy = dummy;
        }
        if (last_dummy == NULL) last_dummy = GetConstant1();
        instr->DeleteAndReplaceWith(last_dummy);
        continue;
      }
      if (instr->IsSoftDeoptimize()) {
        ASSERT(block->IsDeoptimizing());
        nullify = true;
      }
    }
  }
}


// Replace all phis consisting of a single non-loop operand plus any number of
// loop operands by that single non-loop operand.
void HGraph::EliminateRedundantPhis() {
  HPhase phase("H_Redundant phi elimination", this);

  // We do a simple fixed point iteration without any work list, because
  // machine-generated JavaScript can lead to a very dense Hydrogen graph with
  // an enormous work list and will consequently result in OOM. Experiments
  // showed that this simple algorithm is good enough, and even e.g. tracking
  // the set or range of blocks to consider is not a real improvement.
  bool need_another_iteration;
  ZoneList<HPhi*> redundant_phis(blocks_.length(), zone());
  do {
    need_another_iteration = false;
    for (int i = 0; i < blocks_.length(); ++i) {
      HBasicBlock* block = blocks_[i];
      for (int j = 0; j < block->phis()->length(); j++) {
        HPhi* phi = block->phis()->at(j);
        HValue* replacement = phi->GetRedundantReplacement();
        if (replacement != NULL) {
          // Remember phi to avoid concurrent modification of the block's phis.
          redundant_phis.Add(phi, zone());
          for (HUseIterator it(phi->uses()); !it.Done(); it.Advance()) {
            HValue* value = it.value();
            value->SetOperandAt(it.index(), replacement);
            need_another_iteration |= value->IsPhi();
          }
        }
      }
      for (int i = 0; i < redundant_phis.length(); i++) {
        block->RemovePhi(redundant_phis[i]);
      }
      redundant_phis.Clear();
    }
  } while (need_another_iteration);

#if DEBUG
  // Make sure that we *really* removed all redundant phis.
  for (int i = 0; i < blocks_.length(); ++i) {
    for (int j = 0; j < blocks_[i]->phis()->length(); j++) {
      ASSERT(blocks_[i]->phis()->at(j)->GetRedundantReplacement() == NULL);
    }
  }
#endif
}


void HGraph::EliminateUnreachablePhis() {
  HPhase phase("H_Unreachable phi elimination", this);

  // Initialize worklist.
  ZoneList<HPhi*> phi_list(blocks_.length(), zone());
  ZoneList<HPhi*> worklist(blocks_.length(), zone());
  for (int i = 0; i < blocks_.length(); ++i) {
    for (int j = 0; j < blocks_[i]->phis()->length(); j++) {
      HPhi* phi = blocks_[i]->phis()->at(j);
      phi_list.Add(phi, zone());
      // We can't eliminate phis in the receiver position in the environment
      // because in case of throwing an error we need this value to
      // construct a stack trace.
      if (phi->HasRealUses() || phi->IsReceiver())  {
        phi->set_is_live(true);
        worklist.Add(phi, zone());
      }
    }
  }

  // Iteratively mark live phis.
  while (!worklist.is_empty()) {
    HPhi* phi = worklist.RemoveLast();
    for (int i = 0; i < phi->OperandCount(); i++) {
      HValue* operand = phi->OperandAt(i);
      if (operand->IsPhi() && !HPhi::cast(operand)->is_live()) {
        HPhi::cast(operand)->set_is_live(true);
        worklist.Add(HPhi::cast(operand), zone());
      }
    }
  }

  // Remove unreachable phis.
  for (int i = 0; i < phi_list.length(); i++) {
    HPhi* phi = phi_list[i];
    if (!phi->is_live()) {
      HBasicBlock* block = phi->block();
      block->RemovePhi(phi);
      block->RecordDeletedPhi(phi->merged_index());
    }
  }
}


bool HGraph::CheckArgumentsPhiUses() {
  int block_count = blocks_.length();
  for (int i = 0; i < block_count; ++i) {
    for (int j = 0; j < blocks_[i]->phis()->length(); ++j) {
      HPhi* phi = blocks_[i]->phis()->at(j);
      // We don't support phi uses of arguments for now.
      if (phi->CheckFlag(HValue::kIsArguments)) return false;
    }
  }
  return true;
}


bool HGraph::CheckConstPhiUses() {
  int block_count = blocks_.length();
  for (int i = 0; i < block_count; ++i) {
    for (int j = 0; j < blocks_[i]->phis()->length(); ++j) {
      HPhi* phi = blocks_[i]->phis()->at(j);
      // Check for the hole value (from an uninitialized const).
      for (int k = 0; k < phi->OperandCount(); k++) {
        if (phi->OperandAt(k) == GetConstantHole()) return false;
      }
    }
  }
  return true;
}


void HGraph::CollectPhis() {
  int block_count = blocks_.length();
  phi_list_ = new(zone()) ZoneList<HPhi*>(block_count, zone());
  for (int i = 0; i < block_count; ++i) {
    for (int j = 0; j < blocks_[i]->phis()->length(); ++j) {
      HPhi* phi = blocks_[i]->phis()->at(j);
      phi_list_->Add(phi, zone());
    }
  }
}


void HGraph::InferTypes(ZoneList<HValue*>* worklist) {
  BitVector in_worklist(GetMaximumValueID(), zone());
  for (int i = 0; i < worklist->length(); ++i) {
    ASSERT(!in_worklist.Contains(worklist->at(i)->id()));
    in_worklist.Add(worklist->at(i)->id());
  }

  while (!worklist->is_empty()) {
    HValue* current = worklist->RemoveLast();
    in_worklist.Remove(current->id());
    if (current->UpdateInferredType()) {
      for (HUseIterator it(current->uses()); !it.Done(); it.Advance()) {
        HValue* use = it.value();
        if (!in_worklist.Contains(use->id())) {
          in_worklist.Add(use->id());
          worklist->Add(use, zone());
        }
      }
    }
  }
}


class HRangeAnalysis BASE_EMBEDDED {
 public:
  explicit HRangeAnalysis(HGraph* graph) :
      graph_(graph), zone_(graph->zone()), changed_ranges_(16, zone_) { }

  void Analyze();

 private:
  void TraceRange(const char* msg, ...);
  void Analyze(HBasicBlock* block);
  void InferControlFlowRange(HCompareIDAndBranch* test, HBasicBlock* dest);
  void UpdateControlFlowRange(Token::Value op, HValue* value, HValue* other);
  void InferRange(HValue* value);
  void RollBackTo(int index);
  void AddRange(HValue* value, Range* range);

  HGraph* graph_;
  Zone* zone_;
  ZoneList<HValue*> changed_ranges_;
};


void HRangeAnalysis::TraceRange(const char* msg, ...) {
  if (FLAG_trace_range) {
    va_list arguments;
    va_start(arguments, msg);
    OS::VPrint(msg, arguments);
    va_end(arguments);
  }
}


void HRangeAnalysis::Analyze() {
  HPhase phase("H_Range analysis", graph_);
  Analyze(graph_->entry_block());
}


void HRangeAnalysis::Analyze(HBasicBlock* block) {
  TraceRange("Analyzing block B%d\n", block->block_id());

  int last_changed_range = changed_ranges_.length() - 1;

  // Infer range based on control flow.
  if (block->predecessors()->length() == 1) {
    HBasicBlock* pred = block->predecessors()->first();
    if (pred->end()->IsCompareIDAndBranch()) {
      InferControlFlowRange(HCompareIDAndBranch::cast(pred->end()), block);
    }
  }

  // Process phi instructions.
  for (int i = 0; i < block->phis()->length(); ++i) {
    HPhi* phi = block->phis()->at(i);
    InferRange(phi);
  }

  // Go through all instructions of the current block.
  HInstruction* instr = block->first();
  while (instr != block->end()) {
    InferRange(instr);
    instr = instr->next();
  }

  // Continue analysis in all dominated blocks.
  for (int i = 0; i < block->dominated_blocks()->length(); ++i) {
    Analyze(block->dominated_blocks()->at(i));
  }

  RollBackTo(last_changed_range);
}


void HRangeAnalysis::InferControlFlowRange(HCompareIDAndBranch* test,
                                           HBasicBlock* dest) {
  ASSERT((test->FirstSuccessor() == dest) == (test->SecondSuccessor() != dest));
  if (test->representation().IsInteger32()) {
    Token::Value op = test->token();
    if (test->SecondSuccessor() == dest) {
      op = Token::NegateCompareOp(op);
    }
    Token::Value inverted_op = Token::ReverseCompareOp(op);
    UpdateControlFlowRange(op, test->left(), test->right());
    UpdateControlFlowRange(inverted_op, test->right(), test->left());
  }
}


// We know that value [op] other. Use this information to update the range on
// value.
void HRangeAnalysis::UpdateControlFlowRange(Token::Value op,
                                            HValue* value,
                                            HValue* other) {
  Range temp_range;
  Range* range = other->range() != NULL ? other->range() : &temp_range;
  Range* new_range = NULL;

  TraceRange("Control flow range infer %d %s %d\n",
             value->id(),
             Token::Name(op),
             other->id());

  if (op == Token::EQ || op == Token::EQ_STRICT) {
    // The same range has to apply for value.
    new_range = range->Copy(zone_);
  } else if (op == Token::LT || op == Token::LTE) {
    new_range = range->CopyClearLower(zone_);
    if (op == Token::LT) {
      new_range->AddConstant(-1);
    }
  } else if (op == Token::GT || op == Token::GTE) {
    new_range = range->CopyClearUpper(zone_);
    if (op == Token::GT) {
      new_range->AddConstant(1);
    }
  }

  if (new_range != NULL && !new_range->IsMostGeneric()) {
    AddRange(value, new_range);
  }
}


void HRangeAnalysis::InferRange(HValue* value) {
  ASSERT(!value->HasRange());
  if (!value->representation().IsNone()) {
    value->ComputeInitialRange(zone_);
    Range* range = value->range();
    TraceRange("Initial inferred range of %d (%s) set to [%d,%d]\n",
               value->id(),
               value->Mnemonic(),
               range->lower(),
               range->upper());
  }
}


void HRangeAnalysis::RollBackTo(int index) {
  for (int i = index + 1; i < changed_ranges_.length(); ++i) {
    changed_ranges_[i]->RemoveLastAddedRange();
  }
  changed_ranges_.Rewind(index + 1);
}


void HRangeAnalysis::AddRange(HValue* value, Range* range) {
  Range* original_range = value->range();
  value->AddNewRange(range, zone_);
  changed_ranges_.Add(value, zone_);
  Range* new_range = value->range();
  TraceRange("Updated range of %d set to [%d,%d]\n",
             value->id(),
             new_range->lower(),
             new_range->upper());
  if (original_range != NULL) {
    TraceRange("Original range was [%d,%d]\n",
               original_range->lower(),
               original_range->upper());
  }
  TraceRange("New information was [%d,%d]\n",
             range->lower(),
             range->upper());
}


void TraceGVN(const char* msg, ...) {
  va_list arguments;
  va_start(arguments, msg);
  OS::VPrint(msg, arguments);
  va_end(arguments);
}

// Wrap TraceGVN in macros to avoid the expense of evaluating its arguments when
// --trace-gvn is off.
#define TRACE_GVN_1(msg, a1)                    \
  if (FLAG_trace_gvn) {                         \
    TraceGVN(msg, a1);                          \
  }

#define TRACE_GVN_2(msg, a1, a2)                \
  if (FLAG_trace_gvn) {                         \
    TraceGVN(msg, a1, a2);                      \
  }

#define TRACE_GVN_3(msg, a1, a2, a3)            \
  if (FLAG_trace_gvn) {                         \
    TraceGVN(msg, a1, a2, a3);                  \
  }

#define TRACE_GVN_4(msg, a1, a2, a3, a4)        \
  if (FLAG_trace_gvn) {                         \
    TraceGVN(msg, a1, a2, a3, a4);              \
  }

#define TRACE_GVN_5(msg, a1, a2, a3, a4, a5)    \
  if (FLAG_trace_gvn) {                         \
    TraceGVN(msg, a1, a2, a3, a4, a5);          \
  }


HValueMap::HValueMap(Zone* zone, const HValueMap* other)
    : array_size_(other->array_size_),
      lists_size_(other->lists_size_),
      count_(other->count_),
      present_flags_(other->present_flags_),
      array_(zone->NewArray<HValueMapListElement>(other->array_size_)),
      lists_(zone->NewArray<HValueMapListElement>(other->lists_size_)),
      free_list_head_(other->free_list_head_) {
  OS::MemCopy(
      array_, other->array_, array_size_ * sizeof(HValueMapListElement));
  OS::MemCopy(
      lists_, other->lists_, lists_size_ * sizeof(HValueMapListElement));
}


void HValueMap::Kill(GVNFlagSet flags) {
  GVNFlagSet depends_flags = HValue::ConvertChangesToDependsFlags(flags);
  if (!present_flags_.ContainsAnyOf(depends_flags)) return;
  present_flags_.RemoveAll();
  for (int i = 0; i < array_size_; ++i) {
    HValue* value = array_[i].value;
    if (value != NULL) {
      // Clear list of collisions first, so we know if it becomes empty.
      int kept = kNil;  // List of kept elements.
      int next;
      for (int current = array_[i].next; current != kNil; current = next) {
        next = lists_[current].next;
        HValue* value = lists_[current].value;
        if (value->gvn_flags().ContainsAnyOf(depends_flags)) {
          // Drop it.
          count_--;
          lists_[current].next = free_list_head_;
          free_list_head_ = current;
        } else {
          // Keep it.
          lists_[current].next = kept;
          kept = current;
          present_flags_.Add(value->gvn_flags());
        }
      }
      array_[i].next = kept;

      // Now possibly drop directly indexed element.
      value = array_[i].value;
      if (value->gvn_flags().ContainsAnyOf(depends_flags)) {  // Drop it.
        count_--;
        int head = array_[i].next;
        if (head == kNil) {
          array_[i].value = NULL;
        } else {
          array_[i].value = lists_[head].value;
          array_[i].next = lists_[head].next;
          lists_[head].next = free_list_head_;
          free_list_head_ = head;
        }
      } else {
        present_flags_.Add(value->gvn_flags());  // Keep it.
      }
    }
  }
}


HValue* HValueMap::Lookup(HValue* value) const {
  uint32_t hash = static_cast<uint32_t>(value->Hashcode());
  uint32_t pos = Bound(hash);
  if (array_[pos].value != NULL) {
    if (array_[pos].value->Equals(value)) return array_[pos].value;
    int next = array_[pos].next;
    while (next != kNil) {
      if (lists_[next].value->Equals(value)) return lists_[next].value;
      next = lists_[next].next;
    }
  }
  return NULL;
}


void HValueMap::Resize(int new_size, Zone* zone) {
  ASSERT(new_size > count_);
  // Hashing the values into the new array has no more collisions than in the
  // old hash map, so we can use the existing lists_ array, if we are careful.

  // Make sure we have at least one free element.
  if (free_list_head_ == kNil) {
    ResizeLists(lists_size_ << 1, zone);
  }

  HValueMapListElement* new_array =
      zone->NewArray<HValueMapListElement>(new_size);
  memset(new_array, 0, sizeof(HValueMapListElement) * new_size);

  HValueMapListElement* old_array = array_;
  int old_size = array_size_;

  int old_count = count_;
  count_ = 0;
  // Do not modify present_flags_.  It is currently correct.
  array_size_ = new_size;
  array_ = new_array;

  if (old_array != NULL) {
    // Iterate over all the elements in lists, rehashing them.
    for (int i = 0; i < old_size; ++i) {
      if (old_array[i].value != NULL) {
        int current = old_array[i].next;
        while (current != kNil) {
          Insert(lists_[current].value, zone);
          int next = lists_[current].next;
          lists_[current].next = free_list_head_;
          free_list_head_ = current;
          current = next;
        }
        // Rehash the directly stored value.
        Insert(old_array[i].value, zone);
      }
    }
  }
  USE(old_count);
  ASSERT(count_ == old_count);
}


void HValueMap::ResizeLists(int new_size, Zone* zone) {
  ASSERT(new_size > lists_size_);

  HValueMapListElement* new_lists =
      zone->NewArray<HValueMapListElement>(new_size);
  memset(new_lists, 0, sizeof(HValueMapListElement) * new_size);

  HValueMapListElement* old_lists = lists_;
  int old_size = lists_size_;

  lists_size_ = new_size;
  lists_ = new_lists;

  if (old_lists != NULL) {
    OS::MemCopy(lists_, old_lists, old_size * sizeof(HValueMapListElement));
  }
  for (int i = old_size; i < lists_size_; ++i) {
    lists_[i].next = free_list_head_;
    free_list_head_ = i;
  }
}


void HValueMap::Insert(HValue* value, Zone* zone) {
  ASSERT(value != NULL);
  // Resizing when half of the hashtable is filled up.
  if (count_ >= array_size_ >> 1) Resize(array_size_ << 1, zone);
  ASSERT(count_ < array_size_);
  count_++;
  uint32_t pos = Bound(static_cast<uint32_t>(value->Hashcode()));
  if (array_[pos].value == NULL) {
    array_[pos].value = value;
    array_[pos].next = kNil;
  } else {
    if (free_list_head_ == kNil) {
      ResizeLists(lists_size_ << 1, zone);
    }
    int new_element_pos = free_list_head_;
    ASSERT(new_element_pos != kNil);
    free_list_head_ = lists_[free_list_head_].next;
    lists_[new_element_pos].value = value;
    lists_[new_element_pos].next = array_[pos].next;
    ASSERT(array_[pos].next == kNil || lists_[array_[pos].next].value != NULL);
    array_[pos].next = new_element_pos;
  }
}


HSideEffectMap::HSideEffectMap() : count_(0) {
  memset(data_, 0, kNumberOfTrackedSideEffects * kPointerSize);
}


HSideEffectMap::HSideEffectMap(HSideEffectMap* other) : count_(other->count_) {
  *this = *other;  // Calls operator=.
}


HSideEffectMap& HSideEffectMap::operator= (const HSideEffectMap& other) {
  if (this != &other) {
    OS::MemCopy(data_, other.data_, kNumberOfTrackedSideEffects * kPointerSize);
  }
  return *this;
}

void HSideEffectMap::Kill(GVNFlagSet flags) {
  for (int i = 0; i < kNumberOfTrackedSideEffects; i++) {
    GVNFlag changes_flag = HValue::ChangesFlagFromInt(i);
    if (flags.Contains(changes_flag)) {
      if (data_[i] != NULL) count_--;
      data_[i] = NULL;
    }
  }
}


void HSideEffectMap::Store(GVNFlagSet flags, HInstruction* instr) {
  for (int i = 0; i < kNumberOfTrackedSideEffects; i++) {
    GVNFlag changes_flag = HValue::ChangesFlagFromInt(i);
    if (flags.Contains(changes_flag)) {
      if (data_[i] == NULL) count_++;
      data_[i] = instr;
    }
  }
}


class HStackCheckEliminator BASE_EMBEDDED {
 public:
  explicit HStackCheckEliminator(HGraph* graph) : graph_(graph) { }

  void Process();

 private:
  HGraph* graph_;
};


void HStackCheckEliminator::Process() {
  // For each loop block walk the dominator tree from the backwards branch to
  // the loop header. If a call instruction is encountered the backwards branch
  // is dominated by a call and the stack check in the backwards branch can be
  // removed.
  for (int i = 0; i < graph_->blocks()->length(); i++) {
    HBasicBlock* block = graph_->blocks()->at(i);
    if (block->IsLoopHeader()) {
      HBasicBlock* back_edge = block->loop_information()->GetLastBackEdge();
      HBasicBlock* dominator = back_edge;
      while (true) {
        HInstruction* instr = dominator->first();
        while (instr != NULL) {
          if (instr->IsCall()) {
            block->loop_information()->stack_check()->Eliminate();
            break;
          }
          instr = instr->next();
        }

        // Done when the loop header is processed.
        if (dominator == block) break;

        // Move up the dominator tree.
        dominator = dominator->dominator();
      }
    }
  }
}


// Simple sparse set with O(1) add, contains, and clear.
class SparseSet {
 public:
  SparseSet(Zone* zone, int capacity)
      : capacity_(capacity),
        length_(0),
        dense_(zone->NewArray<int>(capacity)),
        sparse_(zone->NewArray<int>(capacity)) {
#ifndef NVALGRIND
    // Initialize the sparse array to make valgrind happy.
    memset(sparse_, 0, sizeof(sparse_[0]) * capacity);
#endif
  }

  bool Contains(int n) const {
    ASSERT(0 <= n && n < capacity_);
    int d = sparse_[n];
    return 0 <= d && d < length_ && dense_[d] == n;
  }

  bool Add(int n) {
    if (Contains(n)) return false;
    dense_[length_] = n;
    sparse_[n] = length_;
    ++length_;
    return true;
  }

  void Clear() { length_ = 0; }

 private:
  int capacity_;
  int length_;
  int* dense_;
  int* sparse_;

  DISALLOW_COPY_AND_ASSIGN(SparseSet);
};


class HGlobalValueNumberer BASE_EMBEDDED {
 public:
  explicit HGlobalValueNumberer(HGraph* graph, CompilationInfo* info)
      : graph_(graph),
        info_(info),
        removed_side_effects_(false),
        block_side_effects_(graph->blocks()->length(), graph->zone()),
        loop_side_effects_(graph->blocks()->length(), graph->zone()),
        visited_on_paths_(graph->zone(), graph->blocks()->length()) {
#ifdef DEBUG
    ASSERT(info->isolate()->optimizing_compiler_thread()->IsOptimizerThread() ||
           !info->isolate()->heap()->IsAllocationAllowed());
#endif
    block_side_effects_.AddBlock(GVNFlagSet(), graph_->blocks()->length(),
                                 graph_->zone());
    loop_side_effects_.AddBlock(GVNFlagSet(), graph_->blocks()->length(),
                                graph_->zone());
  }

  // Returns true if values with side effects are removed.
  bool Analyze();

 private:
  GVNFlagSet CollectSideEffectsOnPathsToDominatedBlock(
      HBasicBlock* dominator,
      HBasicBlock* dominated);
  void AnalyzeGraph();
  void ComputeBlockSideEffects();
  void LoopInvariantCodeMotion();
  void ProcessLoopBlock(HBasicBlock* block,
                        HBasicBlock* before_loop,
                        GVNFlagSet loop_kills,
                        GVNFlagSet* accumulated_first_time_depends,
                        GVNFlagSet* accumulated_first_time_changes);
  bool AllowCodeMotion();
  bool ShouldMove(HInstruction* instr, HBasicBlock* loop_header);

  HGraph* graph() { return graph_; }
  CompilationInfo* info() { return info_; }
  Zone* zone() const { return graph_->zone(); }

  HGraph* graph_;
  CompilationInfo* info_;
  bool removed_side_effects_;

  // A map of block IDs to their side effects.
  ZoneList<GVNFlagSet> block_side_effects_;

  // A map of loop header block IDs to their loop's side effects.
  ZoneList<GVNFlagSet> loop_side_effects_;

  // Used when collecting side effects on paths from dominator to
  // dominated.
  SparseSet visited_on_paths_;
};


bool HGlobalValueNumberer::Analyze() {
  removed_side_effects_ = false;
  ComputeBlockSideEffects();
  if (FLAG_loop_invariant_code_motion) {
    LoopInvariantCodeMotion();
  }
  AnalyzeGraph();
  return removed_side_effects_;
}


void HGlobalValueNumberer::ComputeBlockSideEffects() {
  // The Analyze phase of GVN can be called multiple times. Clear loop side
  // effects before computing them to erase the contents from previous Analyze
  // passes.
  for (int i = 0; i < loop_side_effects_.length(); ++i) {
    loop_side_effects_[i].RemoveAll();
  }
  for (int i = graph_->blocks()->length() - 1; i >= 0; --i) {
    // Compute side effects for the block.
    HBasicBlock* block = graph_->blocks()->at(i);
    HInstruction* instr = block->first();
    int id = block->block_id();
    GVNFlagSet side_effects;
    while (instr != NULL) {
      side_effects.Add(instr->ChangesFlags());
      if (instr->IsSoftDeoptimize()) {
        block_side_effects_[id].RemoveAll();
        side_effects.RemoveAll();
        break;
      }
      instr = instr->next();
    }
    block_side_effects_[id].Add(side_effects);

    // Loop headers are part of their loop.
    if (block->IsLoopHeader()) {
      loop_side_effects_[id].Add(side_effects);
    }

    // Propagate loop side effects upwards.
    if (block->HasParentLoopHeader()) {
      int header_id = block->parent_loop_header()->block_id();
      loop_side_effects_[header_id].Add(block->IsLoopHeader()
                                        ? loop_side_effects_[id]
                                        : side_effects);
    }
  }
}


SmartArrayPointer<char> GetGVNFlagsString(GVNFlagSet flags) {
  char underlying_buffer[kLastFlag * 128];
  Vector<char> buffer(underlying_buffer, sizeof(underlying_buffer));
#if DEBUG
  int offset = 0;
  const char* separator = "";
  const char* comma = ", ";
  buffer[0] = 0;
  uint32_t set_depends_on = 0;
  uint32_t set_changes = 0;
  for (int bit = 0; bit < kLastFlag; ++bit) {
    if ((flags.ToIntegral() & (1 << bit)) != 0) {
      if (bit % 2 == 0) {
        set_changes++;
      } else {
        set_depends_on++;
      }
    }
  }
  bool positive_changes = set_changes < (kLastFlag / 2);
  bool positive_depends_on = set_depends_on < (kLastFlag / 2);
  if (set_changes > 0) {
    if (positive_changes) {
      offset += OS::SNPrintF(buffer + offset, "changes [");
    } else {
      offset += OS::SNPrintF(buffer + offset, "changes all except [");
    }
    for (int bit = 0; bit < kLastFlag; ++bit) {
      if (((flags.ToIntegral() & (1 << bit)) != 0) == positive_changes) {
        switch (static_cast<GVNFlag>(bit)) {
#define DECLARE_FLAG(type)                                       \
          case kChanges##type:                                   \
            offset += OS::SNPrintF(buffer + offset, separator);  \
            offset += OS::SNPrintF(buffer + offset, #type);      \
            separator = comma;                                   \
            break;
GVN_TRACKED_FLAG_LIST(DECLARE_FLAG)
GVN_UNTRACKED_FLAG_LIST(DECLARE_FLAG)
#undef DECLARE_FLAG
          default:
              break;
        }
      }
    }
    offset += OS::SNPrintF(buffer + offset, "]");
  }
  if (set_depends_on > 0) {
    separator = "";
    if (set_changes > 0) {
      offset += OS::SNPrintF(buffer + offset, ", ");
    }
    if (positive_depends_on) {
      offset += OS::SNPrintF(buffer + offset, "depends on [");
    } else {
      offset += OS::SNPrintF(buffer + offset, "depends on all except [");
    }
    for (int bit = 0; bit < kLastFlag; ++bit) {
      if (((flags.ToIntegral() & (1 << bit)) != 0) == positive_depends_on) {
        switch (static_cast<GVNFlag>(bit)) {
#define DECLARE_FLAG(type)                                       \
          case kDependsOn##type:                                 \
            offset += OS::SNPrintF(buffer + offset, separator);  \
            offset += OS::SNPrintF(buffer + offset, #type);      \
            separator = comma;                                   \
            break;
GVN_TRACKED_FLAG_LIST(DECLARE_FLAG)
GVN_UNTRACKED_FLAG_LIST(DECLARE_FLAG)
#undef DECLARE_FLAG
          default:
            break;
        }
      }
    }
    offset += OS::SNPrintF(buffer + offset, "]");
  }
#else
  OS::SNPrintF(buffer, "0x%08X", flags.ToIntegral());
#endif
  size_t string_len = strlen(underlying_buffer) + 1;
  ASSERT(string_len <= sizeof(underlying_buffer));
  char* result = new char[strlen(underlying_buffer) + 1];
  OS::MemCopy(result, underlying_buffer, string_len);
  return SmartArrayPointer<char>(result);
}


void HGlobalValueNumberer::LoopInvariantCodeMotion() {
  TRACE_GVN_1("Using optimistic loop invariant code motion: %s\n",
              graph_->use_optimistic_licm() ? "yes" : "no");
  for (int i = graph_->blocks()->length() - 1; i >= 0; --i) {
    HBasicBlock* block = graph_->blocks()->at(i);
    if (block->IsLoopHeader()) {
      GVNFlagSet side_effects = loop_side_effects_[block->block_id()];
      TRACE_GVN_2("Try loop invariant motion for block B%d %s\n",
                  block->block_id(),
                  *GetGVNFlagsString(side_effects));

      GVNFlagSet accumulated_first_time_depends;
      GVNFlagSet accumulated_first_time_changes;
      HBasicBlock* last = block->loop_information()->GetLastBackEdge();
      for (int j = block->block_id(); j <= last->block_id(); ++j) {
        ProcessLoopBlock(graph_->blocks()->at(j), block, side_effects,
                         &accumulated_first_time_depends,
                         &accumulated_first_time_changes);
      }
    }
  }
}


void HGlobalValueNumberer::ProcessLoopBlock(
    HBasicBlock* block,
    HBasicBlock* loop_header,
    GVNFlagSet loop_kills,
    GVNFlagSet* first_time_depends,
    GVNFlagSet* first_time_changes) {
  HBasicBlock* pre_header = loop_header->predecessors()->at(0);
  GVNFlagSet depends_flags = HValue::ConvertChangesToDependsFlags(loop_kills);
  TRACE_GVN_2("Loop invariant motion for B%d %s\n",
              block->block_id(),
              *GetGVNFlagsString(depends_flags));
  HInstruction* instr = block->first();
  while (instr != NULL) {
    HInstruction* next = instr->next();
    bool hoisted = false;
    if (instr->CheckFlag(HValue::kUseGVN)) {
      TRACE_GVN_4("Checking instruction %d (%s) %s. Loop %s\n",
                  instr->id(),
                  instr->Mnemonic(),
                  *GetGVNFlagsString(instr->gvn_flags()),
                  *GetGVNFlagsString(loop_kills));
      bool can_hoist = !instr->gvn_flags().ContainsAnyOf(depends_flags);
      if (can_hoist && !graph()->use_optimistic_licm()) {
        can_hoist = block->IsLoopSuccessorDominator();
      }

      if (can_hoist) {
        bool inputs_loop_invariant = true;
        for (int i = 0; i < instr->OperandCount(); ++i) {
          if (instr->OperandAt(i)->IsDefinedAfter(pre_header)) {
            inputs_loop_invariant = false;
          }
        }

        if (inputs_loop_invariant && ShouldMove(instr, loop_header)) {
          TRACE_GVN_1("Hoisting loop invariant instruction %d\n", instr->id());
          // Move the instruction out of the loop.
          instr->Unlink();
          instr->InsertBefore(pre_header->end());
          if (instr->HasSideEffects()) removed_side_effects_ = true;
          hoisted = true;
        }
      }
    }
    if (!hoisted) {
      // If an instruction is not hoisted, we have to account for its side
      // effects when hoisting later HTransitionElementsKind instructions.
      GVNFlagSet previous_depends = *first_time_depends;
      GVNFlagSet previous_changes = *first_time_changes;
      first_time_depends->Add(instr->DependsOnFlags());
      first_time_changes->Add(instr->ChangesFlags());
      if (!(previous_depends == *first_time_depends)) {
        TRACE_GVN_1("Updated first-time accumulated %s\n",
                    *GetGVNFlagsString(*first_time_depends));
      }
      if (!(previous_changes == *first_time_changes)) {
        TRACE_GVN_1("Updated first-time accumulated %s\n",
                    *GetGVNFlagsString(*first_time_changes));
      }
    }
    instr = next;
  }
}


bool HGlobalValueNumberer::AllowCodeMotion() {
  return info()->IsStub() || info()->opt_count() + 1 < FLAG_max_opt_count;
}


bool HGlobalValueNumberer::ShouldMove(HInstruction* instr,
                                      HBasicBlock* loop_header) {
  // If we've disabled code motion or we're in a block that unconditionally
  // deoptimizes, don't move any instructions.
  return AllowCodeMotion() && !instr->block()->IsDeoptimizing();
}


GVNFlagSet HGlobalValueNumberer::CollectSideEffectsOnPathsToDominatedBlock(
    HBasicBlock* dominator, HBasicBlock* dominated) {
  GVNFlagSet side_effects;
  for (int i = 0; i < dominated->predecessors()->length(); ++i) {
    HBasicBlock* block = dominated->predecessors()->at(i);
    if (dominator->block_id() < block->block_id() &&
        block->block_id() < dominated->block_id() &&
        visited_on_paths_.Add(block->block_id())) {
      side_effects.Add(block_side_effects_[block->block_id()]);
      if (block->IsLoopHeader()) {
        side_effects.Add(loop_side_effects_[block->block_id()]);
      }
      side_effects.Add(CollectSideEffectsOnPathsToDominatedBlock(
          dominator, block));
    }
  }
  return side_effects;
}


// Each instance of this class is like a "stack frame" for the recursive
// traversal of the dominator tree done during GVN (the stack is handled
// as a double linked list).
// We reuse frames when possible so the list length is limited by the depth
// of the dominator tree but this forces us to initialize each frame calling
// an explicit "Initialize" method instead of a using constructor.
class GvnBasicBlockState: public ZoneObject {
 public:
  static GvnBasicBlockState* CreateEntry(Zone* zone,
                                         HBasicBlock* entry_block,
                                         HValueMap* entry_map) {
    return new(zone)
        GvnBasicBlockState(NULL, entry_block, entry_map, NULL, zone);
  }

  HBasicBlock* block() { return block_; }
  HValueMap* map() { return map_; }
  HSideEffectMap* dominators() { return &dominators_; }

  GvnBasicBlockState* next_in_dominator_tree_traversal(
      Zone* zone,
      HBasicBlock** dominator) {
    // This assignment needs to happen before calling next_dominated() because
    // that call can reuse "this" if we are at the last dominated block.
    *dominator = block();
    GvnBasicBlockState* result = next_dominated(zone);
    if (result == NULL) {
      GvnBasicBlockState* dominator_state = pop();
      if (dominator_state != NULL) {
        // This branch is guaranteed not to return NULL because pop() never
        // returns a state where "is_done() == true".
        *dominator = dominator_state->block();
        result = dominator_state->next_dominated(zone);
      } else {
        // Unnecessary (we are returning NULL) but done for cleanness.
        *dominator = NULL;
      }
    }
    return result;
  }

 private:
  void Initialize(HBasicBlock* block,
                  HValueMap* map,
                  HSideEffectMap* dominators,
                  bool copy_map,
                  Zone* zone) {
    block_ = block;
    map_ = copy_map ? map->Copy(zone) : map;
    dominated_index_ = -1;
    length_ = block->dominated_blocks()->length();
    if (dominators != NULL) {
      dominators_ = *dominators;
    }
  }
  bool is_done() { return dominated_index_ >= length_; }

  GvnBasicBlockState(GvnBasicBlockState* previous,
                     HBasicBlock* block,
                     HValueMap* map,
                     HSideEffectMap* dominators,
                     Zone* zone)
      : previous_(previous), next_(NULL) {
    Initialize(block, map, dominators, true, zone);
  }

  GvnBasicBlockState* next_dominated(Zone* zone) {
    dominated_index_++;
    if (dominated_index_ == length_ - 1) {
      // No need to copy the map for the last child in the dominator tree.
      Initialize(block_->dominated_blocks()->at(dominated_index_),
                 map(),
                 dominators(),
                 false,
                 zone);
      return this;
    } else if (dominated_index_ < length_) {
      return push(zone,
                  block_->dominated_blocks()->at(dominated_index_),
                  dominators());
    } else {
      return NULL;
    }
  }

  GvnBasicBlockState* push(Zone* zone,
                           HBasicBlock* block,
                           HSideEffectMap* dominators) {
    if (next_ == NULL) {
      next_ =
          new(zone) GvnBasicBlockState(this, block, map(), dominators, zone);
    } else {
      next_->Initialize(block, map(), dominators, true, zone);
    }
    return next_;
  }
  GvnBasicBlockState* pop() {
    GvnBasicBlockState* result = previous_;
    while (result != NULL && result->is_done()) {
      TRACE_GVN_2("Backtracking from block B%d to block b%d\n",
                  block()->block_id(),
                  previous_->block()->block_id())
      result = result->previous_;
    }
    return result;
  }

  GvnBasicBlockState* previous_;
  GvnBasicBlockState* next_;
  HBasicBlock* block_;
  HValueMap* map_;
  HSideEffectMap dominators_;
  int dominated_index_;
  int length_;
};

// This is a recursive traversal of the dominator tree but it has been turned
// into a loop to avoid stack overflows.
// The logical "stack frames" of the recursion are kept in a list of
// GvnBasicBlockState instances.
void HGlobalValueNumberer::AnalyzeGraph() {
  HBasicBlock* entry_block = graph_->entry_block();
  HValueMap* entry_map = new(zone()) HValueMap(zone());
  GvnBasicBlockState* current =
      GvnBasicBlockState::CreateEntry(zone(), entry_block, entry_map);

  while (current != NULL) {
    HBasicBlock* block = current->block();
    HValueMap* map = current->map();
    HSideEffectMap* dominators = current->dominators();

    TRACE_GVN_2("Analyzing block B%d%s\n",
                block->block_id(),
                block->IsLoopHeader() ? " (loop header)" : "");

    // If this is a loop header kill everything killed by the loop.
    if (block->IsLoopHeader()) {
      map->Kill(loop_side_effects_[block->block_id()]);
    }

    // Go through all instructions of the current block.
    HInstruction* instr = block->first();
    while (instr != NULL) {
      HInstruction* next = instr->next();
      GVNFlagSet flags = instr->ChangesFlags();
      if (!flags.IsEmpty()) {
        // Clear all instructions in the map that are affected by side effects.
        // Store instruction as the dominating one for tracked side effects.
        map->Kill(flags);
        dominators->Store(flags, instr);
        TRACE_GVN_2("Instruction %d %s\n", instr->id(),
                    *GetGVNFlagsString(flags));
      }
      if (instr->CheckFlag(HValue::kUseGVN)) {
        ASSERT(!instr->HasObservableSideEffects());
        HValue* other = map->Lookup(instr);
        if (other != NULL) {
          ASSERT(instr->Equals(other) && other->Equals(instr));
          TRACE_GVN_4("Replacing value %d (%s) with value %d (%s)\n",
                      instr->id(),
                      instr->Mnemonic(),
                      other->id(),
                      other->Mnemonic());
          if (instr->HasSideEffects()) removed_side_effects_ = true;
          instr->DeleteAndReplaceWith(other);
        } else {
          map->Add(instr, zone());
        }
      }
      if (instr->IsLinked() &&
          instr->CheckFlag(HValue::kTrackSideEffectDominators)) {
        for (int i = 0; i < kNumberOfTrackedSideEffects; i++) {
          HValue* other = dominators->at(i);
          GVNFlag changes_flag = HValue::ChangesFlagFromInt(i);
          GVNFlag depends_on_flag = HValue::DependsOnFlagFromInt(i);
          if (instr->DependsOnFlags().Contains(depends_on_flag) &&
              (other != NULL)) {
            TRACE_GVN_5("Side-effect #%d in %d (%s) is dominated by %d (%s)\n",
                        i,
                        instr->id(),
                        instr->Mnemonic(),
                        other->id(),
                        other->Mnemonic());
            instr->SetSideEffectDominator(changes_flag, other);
          }
        }
      }
      instr = next;
    }

    HBasicBlock* dominator_block;
    GvnBasicBlockState* next =
        current->next_in_dominator_tree_traversal(zone(), &dominator_block);

    if (next != NULL) {
      HBasicBlock* dominated = next->block();
      HValueMap* successor_map = next->map();
      HSideEffectMap* successor_dominators = next->dominators();

      // Kill everything killed on any path between this block and the
      // dominated block.  We don't have to traverse these paths if the
      // value map and the dominators list is already empty.  If the range
      // of block ids (block_id, dominated_id) is empty there are no such
      // paths.
      if ((!successor_map->IsEmpty() || !successor_dominators->IsEmpty()) &&
          dominator_block->block_id() + 1 < dominated->block_id()) {
        visited_on_paths_.Clear();
        GVNFlagSet side_effects_on_all_paths =
            CollectSideEffectsOnPathsToDominatedBlock(dominator_block,
                                                      dominated);
        successor_map->Kill(side_effects_on_all_paths);
        successor_dominators->Kill(side_effects_on_all_paths);
      }
    }
    current = next;
  }
}


void HInferRepresentation::AddToWorklist(HValue* current) {
  if (current->representation().IsTagged()) return;
  if (!current->CheckFlag(HValue::kFlexibleRepresentation)) return;
  if (in_worklist_.Contains(current->id())) return;
  worklist_.Add(current, zone());
  in_worklist_.Add(current->id());
}


void HInferRepresentation::Analyze() {
  HPhase phase("H_Infer representations", graph_);

  // (1) Initialize bit vectors and count real uses. Each phi gets a
  // bit-vector of length <number of phis>.
  const ZoneList<HPhi*>* phi_list = graph_->phi_list();
  int phi_count = phi_list->length();
  ZoneList<BitVector*> connected_phis(phi_count, graph_->zone());
  for (int i = 0; i < phi_count; ++i) {
    phi_list->at(i)->InitRealUses(i);
    BitVector* connected_set = new(zone()) BitVector(phi_count, graph_->zone());
    connected_set->Add(i);
    connected_phis.Add(connected_set, zone());
  }

  // (2) Do a fixed point iteration to find the set of connected phis.  A
  // phi is connected to another phi if its value is used either directly or
  // indirectly through a transitive closure of the def-use relation.
  bool change = true;
  while (change) {
    change = false;
    // We normally have far more "forward edges" than "backward edges",
    // so we terminate faster when we walk backwards.
    for (int i = phi_count - 1; i >= 0; --i) {
      HPhi* phi = phi_list->at(i);
      for (HUseIterator it(phi->uses()); !it.Done(); it.Advance()) {
        HValue* use = it.value();
        if (use->IsPhi()) {
          int id = HPhi::cast(use)->phi_id();
          if (connected_phis[i]->UnionIsChanged(*connected_phis[id]))
            change = true;
        }
      }
    }
  }

  // Simplify constant phi inputs where possible.
  for (int i = 0; i < phi_count; ++i) {
    phi_list->at(i)->SimplifyConstantInputs();
  }

  // Use the phi reachability information from step 2 to
  // push information about values which can't be converted to integer
  // without deoptimization through the phi use-def chains, avoiding
  // unnecessary deoptimizations later.
  for (int i = 0; i < phi_count; ++i) {
    HPhi* phi = phi_list->at(i);
    bool cti = phi->AllOperandsConvertibleToInteger();
    if (cti) continue;

    for (BitVector::Iterator it(connected_phis.at(i));
         !it.Done();
         it.Advance()) {
      HPhi* phi = phi_list->at(it.Current());
      phi->set_is_convertible_to_integer(false);
    }
  }

  // Use the phi reachability information from step 2 to
  // sum up the non-phi use counts of all connected phis.
  for (int i = 0; i < phi_count; ++i) {
    HPhi* phi = phi_list->at(i);
    for (BitVector::Iterator it(connected_phis.at(i));
         !it.Done();
         it.Advance()) {
      int index = it.Current();
      HPhi* it_use = phi_list->at(index);
      if (index != i) phi->AddNonPhiUsesFrom(it_use);  // Don't count twice.
    }
  }

  // Initialize work list
  for (int i = 0; i < graph_->blocks()->length(); ++i) {
    HBasicBlock* block = graph_->blocks()->at(i);
    const ZoneList<HPhi*>* phis = block->phis();
    for (int j = 0; j < phis->length(); ++j) {
      AddToWorklist(phis->at(j));
    }

    HInstruction* current = block->first();
    while (current != NULL) {
      AddToWorklist(current);
      current = current->next();
    }
  }

  // Do a fixed point iteration, trying to improve representations
  while (!worklist_.is_empty()) {
    HValue* current = worklist_.RemoveLast();
    in_worklist_.Remove(current->id());
    current->InferRepresentation(this);
  }

  // Lastly: any instruction that we don't have representation information
  // for defaults to Tagged.
  for (int i = 0; i < graph_->blocks()->length(); ++i) {
    HBasicBlock* block = graph_->blocks()->at(i);
    const ZoneList<HPhi*>* phis = block->phis();
    for (int j = 0; j < phis->length(); ++j) {
      HPhi* phi = phis->at(j);
      if (phi->representation().IsNone()) {
        phi->ChangeRepresentation(Representation::Tagged());
      }
    }
    for (HInstruction* current = block->first();
         current != NULL; current = current->next()) {
      if (current->representation().IsNone() &&
          current->CheckFlag(HInstruction::kFlexibleRepresentation)) {
        current->ChangeRepresentation(Representation::Tagged());
      }
    }
  }
}


void HGraph::MergeRemovableSimulates() {
  ZoneList<HSimulate*> mergelist(2, zone());
  for (int i = 0; i < blocks()->length(); ++i) {
    HBasicBlock* block = blocks()->at(i);
    // Make sure the merge list is empty at the start of a block.
    ASSERT(mergelist.is_empty());
    // Nasty heuristic: Never remove the first simulate in a block. This
    // just so happens to have a beneficial effect on register allocation.
    bool first = true;
    for (HInstruction* current = block->first();
         current != NULL; current = current->next()) {
      if (current->IsLeaveInlined()) {
        // Never fold simulates from inlined environments into simulates
        // in the outer environment.
        // (Before each HEnterInlined, there is a non-foldable HSimulate
        // anyway, so we get the barrier in the other direction for free.)
        // Simply remove all accumulated simulates without merging.  This
        // is safe because simulates after instructions with side effects
        // are never added to the merge list.
        while (!mergelist.is_empty()) {
          mergelist.RemoveLast()->DeleteAndReplaceWith(NULL);
        }
        continue;
      }
      // Skip the non-simulates and the first simulate.
      if (!current->IsSimulate()) continue;
      if (first) {
        first = false;
        continue;
      }
      HSimulate* current_simulate = HSimulate::cast(current);
      if ((current_simulate->previous()->HasObservableSideEffects() &&
           !current_simulate->next()->IsSimulate()) ||
          !current_simulate->is_candidate_for_removal()) {
        // This simulate is not suitable for folding.
        // Fold the ones accumulated so far.
        current_simulate->MergeWith(&mergelist);
        continue;
      } else {
        // Accumulate this simulate for folding later on.
        mergelist.Add(current_simulate, zone());
      }
    }

    if (!mergelist.is_empty()) {
      // Merge the accumulated simulates at the end of the block.
      HSimulate* last = mergelist.RemoveLast();
      last->MergeWith(&mergelist);
    }
  }
}


void HGraph::InitializeInferredTypes() {
  HPhase phase("H_Inferring types", this);
  InitializeInferredTypes(0, this->blocks_.length() - 1);
}


void HGraph::InitializeInferredTypes(int from_inclusive, int to_inclusive) {
  for (int i = from_inclusive; i <= to_inclusive; ++i) {
    HBasicBlock* block = blocks_[i];

    const ZoneList<HPhi*>* phis = block->phis();
    for (int j = 0; j < phis->length(); j++) {
      phis->at(j)->UpdateInferredType();
    }

    HInstruction* current = block->first();
    while (current != NULL) {
      current->UpdateInferredType();
      current = current->next();
    }

    if (block->IsLoopHeader()) {
      HBasicBlock* last_back_edge =
          block->loop_information()->GetLastBackEdge();
      InitializeInferredTypes(i + 1, last_back_edge->block_id());
      // Skip all blocks already processed by the recursive call.
      i = last_back_edge->block_id();
      // Update phis of the loop header now after the whole loop body is
      // guaranteed to be processed.
      ZoneList<HValue*> worklist(block->phis()->length(), zone());
      for (int j = 0; j < block->phis()->length(); ++j) {
        worklist.Add(block->phis()->at(j), zone());
      }
      InferTypes(&worklist);
    }
  }
}


void HGraph::PropagateMinusZeroChecks(HValue* value, BitVector* visited) {
  HValue* current = value;
  while (current != NULL) {
    if (visited->Contains(current->id())) return;

    // For phis, we must propagate the check to all of its inputs.
    if (current->IsPhi()) {
      visited->Add(current->id());
      HPhi* phi = HPhi::cast(current);
      for (int i = 0; i < phi->OperandCount(); ++i) {
        PropagateMinusZeroChecks(phi->OperandAt(i), visited);
      }
      break;
    }

    // For multiplication, division, and Math.min/max(), we must propagate
    // to the left and the right side.
    if (current->IsMul()) {
      HMul* mul = HMul::cast(current);
      mul->EnsureAndPropagateNotMinusZero(visited);
      PropagateMinusZeroChecks(mul->left(), visited);
      PropagateMinusZeroChecks(mul->right(), visited);
    } else if (current->IsDiv()) {
      HDiv* div = HDiv::cast(current);
      div->EnsureAndPropagateNotMinusZero(visited);
      PropagateMinusZeroChecks(div->left(), visited);
      PropagateMinusZeroChecks(div->right(), visited);
    } else if (current->IsMathMinMax()) {
      HMathMinMax* minmax = HMathMinMax::cast(current);
      visited->Add(minmax->id());
      PropagateMinusZeroChecks(minmax->left(), visited);
      PropagateMinusZeroChecks(minmax->right(), visited);
    }

    current = current->EnsureAndPropagateNotMinusZero(visited);
  }
}


void HGraph::InsertRepresentationChangeForUse(HValue* value,
                                              HValue* use_value,
                                              int use_index,
                                              Representation to) {
  // Insert the representation change right before its use. For phi-uses we
  // insert at the end of the corresponding predecessor.
  HInstruction* next = NULL;
  if (use_value->IsPhi()) {
    next = use_value->block()->predecessors()->at(use_index)->end();
  } else {
    next = HInstruction::cast(use_value);
  }
  // For constants we try to make the representation change at compile
  // time. When a representation change is not possible without loss of
  // information we treat constants like normal instructions and insert the
  // change instructions for them.
  HInstruction* new_value = NULL;
  bool is_truncating = use_value->CheckFlag(HValue::kTruncatingToInt32);
  bool deoptimize_on_undefined =
      use_value->CheckFlag(HValue::kDeoptimizeOnUndefined);
  if (value->IsConstant()) {
    HConstant* constant = HConstant::cast(value);
    // Try to create a new copy of the constant with the new representation.
    new_value = (is_truncating && to.IsInteger32())
        ? constant->CopyToTruncatedInt32(zone())
        : constant->CopyToRepresentation(to, zone());
  }

  if (new_value == NULL) {
    new_value = new(zone()) HChange(value, to,
                                    is_truncating, deoptimize_on_undefined);
  }

  new_value->InsertBefore(next);
  use_value->SetOperandAt(use_index, new_value);
}


void HGraph::InsertRepresentationChangesForValue(HValue* value) {
  Representation r = value->representation();
  if (r.IsNone()) return;
  if (value->HasNoUses()) return;

  for (HUseIterator it(value->uses()); !it.Done(); it.Advance()) {
    HValue* use_value = it.value();
    int use_index = it.index();
    Representation req = use_value->RequiredInputRepresentation(use_index);
    if (req.IsNone() || req.Equals(r)) continue;
    InsertRepresentationChangeForUse(value, use_value, use_index, req);
  }
  if (value->HasNoUses()) {
    ASSERT(value->IsConstant());
    value->DeleteAndReplaceWith(NULL);
  }

  // The only purpose of a HForceRepresentation is to represent the value
  // after the (possible) HChange instruction.  We make it disappear.
  if (value->IsForceRepresentation()) {
    value->DeleteAndReplaceWith(HForceRepresentation::cast(value)->value());
  }
}


void HGraph::InsertRepresentationChanges() {
  HPhase phase("H_Representation changes", this);

  // Compute truncation flag for phis: Initially assume that all
  // int32-phis allow truncation and iteratively remove the ones that
  // are used in an operation that does not allow a truncating
  // conversion.
  // TODO(fschneider): Replace this with a worklist-based iteration.
  for (int i = 0; i < phi_list()->length(); i++) {
    HPhi* phi = phi_list()->at(i);
    if (phi->representation().IsInteger32()) {
      phi->SetFlag(HValue::kTruncatingToInt32);
    }
  }
  bool change = true;
  while (change) {
    change = false;
    for (int i = 0; i < phi_list()->length(); i++) {
      HPhi* phi = phi_list()->at(i);
      if (!phi->CheckFlag(HValue::kTruncatingToInt32)) continue;
      for (HUseIterator it(phi->uses()); !it.Done(); it.Advance()) {
        // If a Phi is used as a non-truncating int32 or as a double,
        // clear its "truncating" flag.
        HValue* use = it.value();
        Representation input_representation =
            use->RequiredInputRepresentation(it.index());
        if ((input_representation.IsInteger32() &&
             !use->CheckFlag(HValue::kTruncatingToInt32)) ||
            input_representation.IsDouble()) {
          if (FLAG_trace_representation) {
            PrintF("#%d Phi is not truncating because of #%d %s\n",
                   phi->id(), it.value()->id(), it.value()->Mnemonic());
          }
          phi->ClearFlag(HValue::kTruncatingToInt32);
          change = true;
          break;
        }
      }
    }
  }

  for (int i = 0; i < blocks_.length(); ++i) {
    // Process phi instructions first.
    const ZoneList<HPhi*>* phis = blocks_[i]->phis();
    for (int j = 0; j < phis->length(); j++) {
      InsertRepresentationChangesForValue(phis->at(j));
    }

    // Process normal instructions.
    HInstruction* current = blocks_[i]->first();
    while (current != NULL) {
      HInstruction* next = current->next();
      InsertRepresentationChangesForValue(current);
      current = next;
    }
  }
}


void HGraph::RecursivelyMarkPhiDeoptimizeOnUndefined(HPhi* phi) {
  if (phi->CheckFlag(HValue::kDeoptimizeOnUndefined)) return;
  phi->SetFlag(HValue::kDeoptimizeOnUndefined);
  for (int i = 0; i < phi->OperandCount(); ++i) {
    HValue* input = phi->OperandAt(i);
    if (input->IsPhi()) {
      RecursivelyMarkPhiDeoptimizeOnUndefined(HPhi::cast(input));
    }
  }
}


void HGraph::MarkDeoptimizeOnUndefined() {
  HPhase phase("H_MarkDeoptimizeOnUndefined", this);
  // Compute DeoptimizeOnUndefined flag for phis.
  // Any phi that can reach a use with DeoptimizeOnUndefined set must
  // have DeoptimizeOnUndefined set.  Currently only HCompareIDAndBranch, with
  // double input representation, has this flag set.
  // The flag is used by HChange tagged->double, which must deoptimize
  // if one of its uses has this flag set.
  for (int i = 0; i < phi_list()->length(); i++) {
    HPhi* phi = phi_list()->at(i);
    if (phi->representation().IsDouble()) {
      for (HUseIterator it(phi->uses()); !it.Done(); it.Advance()) {
        if (it.value()->CheckFlag(HValue::kDeoptimizeOnUndefined)) {
          RecursivelyMarkPhiDeoptimizeOnUndefined(phi);
          break;
        }
      }
    }
  }
}


// Discover instructions that can be marked with kUint32 flag allowing
// them to produce full range uint32 values.
class Uint32Analysis BASE_EMBEDDED {
 public:
  explicit Uint32Analysis(Zone* zone) : zone_(zone), phis_(4, zone) { }

  void Analyze(HInstruction* current);

  void UnmarkUnsafePhis();

 private:
  bool IsSafeUint32Use(HValue* val, HValue* use);
  bool Uint32UsesAreSafe(HValue* uint32val);
  bool CheckPhiOperands(HPhi* phi);
  void UnmarkPhi(HPhi* phi, ZoneList<HPhi*>* worklist);

  Zone* zone_;
  ZoneList<HPhi*> phis_;
};


bool Uint32Analysis::IsSafeUint32Use(HValue* val, HValue* use) {
  // Operations that operatate on bits are safe.
  if (use->IsBitwise() ||
      use->IsShl() ||
      use->IsSar() ||
      use->IsShr() ||
      use->IsBitNot()) {
    return true;
  } else if (use->IsChange() || use->IsSimulate()) {
    // Conversions and deoptimization have special support for unt32.
    return true;
  } else if (use->IsStoreKeyed()) {
    HStoreKeyed* store = HStoreKeyed::cast(use);
    if (store->is_external()) {
      // Storing a value into an external integer array is a bit level
      // operation.
      if (store->value() == val) {
        // Clamping or a conversion to double should have beed inserted.
        ASSERT(store->elements_kind() != EXTERNAL_PIXEL_ELEMENTS);
        ASSERT(store->elements_kind() != EXTERNAL_FLOAT_ELEMENTS);
        ASSERT(store->elements_kind() != EXTERNAL_DOUBLE_ELEMENTS);
        return true;
      }
    }
  }

  return false;
}


// Iterate over all uses and verify that they are uint32 safe: either don't
// distinguish between int32 and uint32 due to their bitwise nature or
// have special support for uint32 values.
// Encountered phis are optimisitically treated as safe uint32 uses,
// marked with kUint32 flag and collected in the phis_ list. A separate
// path will be performed later by UnmarkUnsafePhis to clear kUint32 from
// phis that are not actually uint32-safe (it requries fix point iteration).
bool Uint32Analysis::Uint32UsesAreSafe(HValue* uint32val) {
  bool collect_phi_uses = false;
  for (HUseIterator it(uint32val->uses()); !it.Done(); it.Advance()) {
    HValue* use = it.value();

    if (use->IsPhi()) {
      if (!use->CheckFlag(HInstruction::kUint32)) {
        // There is a phi use of this value from a phis that is not yet
        // collected in phis_ array. Separate pass is required.
        collect_phi_uses = true;
      }

      // Optimistically treat phis as uint32 safe.
      continue;
    }

    if (!IsSafeUint32Use(uint32val, use)) {
      return false;
    }
  }

  if (collect_phi_uses) {
    for (HUseIterator it(uint32val->uses()); !it.Done(); it.Advance()) {
      HValue* use = it.value();

      // There is a phi use of this value from a phis that is not yet
      // collected in phis_ array. Separate pass is required.
      if (use->IsPhi() && !use->CheckFlag(HInstruction::kUint32)) {
        use->SetFlag(HInstruction::kUint32);
        phis_.Add(HPhi::cast(use), zone_);
      }
    }
  }

  return true;
}


// Analyze instruction and mark it with kUint32 if all its uses are uint32
// safe.
void Uint32Analysis::Analyze(HInstruction* current) {
  if (Uint32UsesAreSafe(current)) current->SetFlag(HInstruction::kUint32);
}


// Check if all operands to the given phi are marked with kUint32 flag.
bool Uint32Analysis::CheckPhiOperands(HPhi* phi) {
  if (!phi->CheckFlag(HInstruction::kUint32)) {
    // This phi is not uint32 safe. No need to check operands.
    return false;
  }

  for (int j = 0; j < phi->OperandCount(); j++) {
    HValue* operand = phi->OperandAt(j);
    if (!operand->CheckFlag(HInstruction::kUint32)) {
      // Lazyly mark constants that fit into uint32 range with kUint32 flag.
      if (operand->IsInteger32Constant() &&
          operand->GetInteger32Constant() >= 0) {
        operand->SetFlag(HInstruction::kUint32);
        continue;
      }

      // This phi is not safe, some operands are not uint32 values.
      return false;
    }
  }

  return true;
}


// Remove kUint32 flag from the phi itself and its operands. If any operand
// was a phi marked with kUint32 place it into a worklist for
// transitive clearing of kUint32 flag.
void Uint32Analysis::UnmarkPhi(HPhi* phi, ZoneList<HPhi*>* worklist) {
  phi->ClearFlag(HInstruction::kUint32);
  for (int j = 0; j < phi->OperandCount(); j++) {
    HValue* operand = phi->OperandAt(j);
    if (operand->CheckFlag(HInstruction::kUint32)) {
      operand->ClearFlag(HInstruction::kUint32);
      if (operand->IsPhi()) {
        worklist->Add(HPhi::cast(operand), zone_);
      }
    }
  }
}


void Uint32Analysis::UnmarkUnsafePhis() {
  // No phis were collected. Nothing to do.
  if (phis_.length() == 0) return;

  // Worklist used to transitively clear kUint32 from phis that
  // are used as arguments to other phis.
  ZoneList<HPhi*> worklist(phis_.length(), zone_);

  // Phi can be used as a uint32 value if and only if
  // all its operands are uint32 values and all its
  // uses are uint32 safe.

  // Iterate over collected phis and unmark those that
  // are unsafe. When unmarking phi unmark its operands
  // and add it to the worklist if it is a phi as well.
  // Phis that are still marked as safe are shifted down
  // so that all safe phis form a prefix of the phis_ array.
  int phi_count = 0;
  for (int i = 0; i < phis_.length(); i++) {
    HPhi* phi = phis_[i];

    if (CheckPhiOperands(phi) && Uint32UsesAreSafe(phi)) {
      phis_[phi_count++] = phi;
    } else {
      UnmarkPhi(phi, &worklist);
    }
  }

  // Now phis array contains only those phis that have safe
  // non-phi uses. Start transitively clearing kUint32 flag
  // from phi operands of discovered non-safe phies until
  // only safe phies are left.
  while (!worklist.is_empty())  {
    while (!worklist.is_empty()) {
      HPhi* phi = worklist.RemoveLast();
      UnmarkPhi(phi, &worklist);
    }

    // Check if any operands to safe phies were unmarked
    // turning a safe phi into unsafe. The same value
    // can flow into several phis.
    int new_phi_count = 0;
    for (int i = 0; i < phi_count; i++) {
      HPhi* phi = phis_[i];

      if (CheckPhiOperands(phi)) {
        phis_[new_phi_count++] = phi;
      } else {
        UnmarkPhi(phi, &worklist);
      }
    }
    phi_count = new_phi_count;
  }
}


void HGraph::ComputeSafeUint32Operations() {
  if (!FLAG_opt_safe_uint32_operations || uint32_instructions_ == NULL) {
    return;
  }

  Uint32Analysis analysis(zone());
  for (int i = 0; i < uint32_instructions_->length(); ++i) {
    HInstruction* current = uint32_instructions_->at(i);
    if (current->IsLinked() && current->representation().IsInteger32()) {
      analysis.Analyze(current);
    }
  }

  // Some phis might have been optimistically marked with kUint32 flag.
  // Remove this flag from those phis that are unsafe and propagate
  // this information transitively potentially clearing kUint32 flag
  // from some non-phi operations that are used as operands to unsafe phis.
  analysis.UnmarkUnsafePhis();
}


void HGraph::ComputeMinusZeroChecks() {
  BitVector visited(GetMaximumValueID(), zone());
  for (int i = 0; i < blocks_.length(); ++i) {
    for (HInstruction* current = blocks_[i]->first();
         current != NULL;
         current = current->next()) {
      if (current->IsChange()) {
        HChange* change = HChange::cast(current);
        // Propagate flags for negative zero checks upwards from conversions
        // int32-to-tagged and int32-to-double.
        Representation from = change->value()->representation();
        ASSERT(from.Equals(change->from()));
        if (from.IsInteger32()) {
          ASSERT(change->to().IsTagged() || change->to().IsDouble());
          ASSERT(visited.IsEmpty());
          PropagateMinusZeroChecks(change->value(), &visited);
          visited.Clear();
        }
      }
    }
  }
}


// Implementation of utility class to encapsulate the translation state for
// a (possibly inlined) function.
FunctionState::FunctionState(HOptimizedGraphBuilder* owner,
                             CompilationInfo* info,
                             TypeFeedbackOracle* oracle,
                             InliningKind inlining_kind)
    : owner_(owner),
      compilation_info_(info),
      oracle_(oracle),
      call_context_(NULL),
      inlining_kind_(inlining_kind),
      function_return_(NULL),
      test_context_(NULL),
      entry_(NULL),
      arguments_elements_(NULL),
      outer_(owner->function_state()) {
  if (outer_ != NULL) {
    // State for an inline function.
    if (owner->ast_context()->IsTest()) {
      HBasicBlock* if_true = owner->graph()->CreateBasicBlock();
      HBasicBlock* if_false = owner->graph()->CreateBasicBlock();
      if_true->MarkAsInlineReturnTarget();
      if_false->MarkAsInlineReturnTarget();
      TestContext* outer_test_context = TestContext::cast(owner->ast_context());
      Expression* cond = outer_test_context->condition();
      TypeFeedbackOracle* outer_oracle = outer_test_context->oracle();
      // The AstContext constructor pushed on the context stack.  This newed
      // instance is the reason that AstContext can't be BASE_EMBEDDED.
      test_context_ =
          new TestContext(owner, cond, outer_oracle, if_true, if_false);
    } else {
      function_return_ = owner->graph()->CreateBasicBlock();
      function_return()->MarkAsInlineReturnTarget();
    }
    // Set this after possibly allocating a new TestContext above.
    call_context_ = owner->ast_context();
  }

  // Push on the state stack.
  owner->set_function_state(this);
}


FunctionState::~FunctionState() {
  delete test_context_;
  owner_->set_function_state(outer_);
}


// Implementation of utility classes to represent an expression's context in
// the AST.
AstContext::AstContext(HOptimizedGraphBuilder* owner, Expression::Context kind)
    : owner_(owner),
      kind_(kind),
      outer_(owner->ast_context()),
      for_typeof_(false) {
  owner->set_ast_context(this);  // Push.
#ifdef DEBUG
  ASSERT(owner->environment()->frame_type() == JS_FUNCTION);
  original_length_ = owner->environment()->length();
#endif
}


AstContext::~AstContext() {
  owner_->set_ast_context(outer_);  // Pop.
}


EffectContext::~EffectContext() {
  ASSERT(owner()->HasStackOverflow() ||
         owner()->current_block() == NULL ||
         (owner()->environment()->length() == original_length_ &&
          owner()->environment()->frame_type() == JS_FUNCTION));
}


ValueContext::~ValueContext() {
  ASSERT(owner()->HasStackOverflow() ||
         owner()->current_block() == NULL ||
         (owner()->environment()->length() == original_length_ + 1 &&
          owner()->environment()->frame_type() == JS_FUNCTION));
}


void EffectContext::ReturnValue(HValue* value) {
  // The value is simply ignored.
}


void ValueContext::ReturnValue(HValue* value) {
  // The value is tracked in the bailout environment, and communicated
  // through the environment as the result of the expression.
  if (!arguments_allowed() && value->CheckFlag(HValue::kIsArguments)) {
    owner()->Bailout("bad value context for arguments value");
  }
  owner()->Push(value);
}


void TestContext::ReturnValue(HValue* value) {
  BuildBranch(value);
}


void EffectContext::ReturnInstruction(HInstruction* instr, BailoutId ast_id) {
  ASSERT(!instr->IsControlInstruction());
  owner()->AddInstruction(instr);
  if (instr->HasObservableSideEffects()) {
    owner()->AddSimulate(ast_id, REMOVABLE_SIMULATE);
  }
}


void EffectContext::ReturnControl(HControlInstruction* instr,
                                  BailoutId ast_id) {
  ASSERT(!instr->HasObservableSideEffects());
  HBasicBlock* empty_true = owner()->graph()->CreateBasicBlock();
  HBasicBlock* empty_false = owner()->graph()->CreateBasicBlock();
  instr->SetSuccessorAt(0, empty_true);
  instr->SetSuccessorAt(1, empty_false);
  owner()->current_block()->Finish(instr);
  HBasicBlock* join = owner()->CreateJoin(empty_true, empty_false, ast_id);
  owner()->set_current_block(join);
}


void EffectContext::ReturnContinuation(HIfContinuation* continuation,
                                       BailoutId ast_id) {
  HBasicBlock* true_branch = NULL;
  HBasicBlock* false_branch = NULL;
  continuation->Continue(&true_branch, &false_branch, NULL);
  if (!continuation->IsTrueReachable()) {
    owner()->set_current_block(false_branch);
  } else if (!continuation->IsFalseReachable()) {
    owner()->set_current_block(true_branch);
  } else {
    HBasicBlock* join = owner()->CreateJoin(true_branch, false_branch, ast_id);
    owner()->set_current_block(join);
  }
}


void ValueContext::ReturnInstruction(HInstruction* instr, BailoutId ast_id) {
  ASSERT(!instr->IsControlInstruction());
  if (!arguments_allowed() && instr->CheckFlag(HValue::kIsArguments)) {
    return owner()->Bailout("bad value context for arguments object value");
  }
  owner()->AddInstruction(instr);
  owner()->Push(instr);
  if (instr->HasObservableSideEffects()) {
    owner()->AddSimulate(ast_id, REMOVABLE_SIMULATE);
  }
}


void ValueContext::ReturnControl(HControlInstruction* instr, BailoutId ast_id) {
  ASSERT(!instr->HasObservableSideEffects());
  if (!arguments_allowed() && instr->CheckFlag(HValue::kIsArguments)) {
    return owner()->Bailout("bad value context for arguments object value");
  }
  HBasicBlock* materialize_false = owner()->graph()->CreateBasicBlock();
  HBasicBlock* materialize_true = owner()->graph()->CreateBasicBlock();
  instr->SetSuccessorAt(0, materialize_true);
  instr->SetSuccessorAt(1, materialize_false);
  owner()->current_block()->Finish(instr);
  owner()->set_current_block(materialize_true);
  owner()->Push(owner()->graph()->GetConstantTrue());
  owner()->set_current_block(materialize_false);
  owner()->Push(owner()->graph()->GetConstantFalse());
  HBasicBlock* join =
    owner()->CreateJoin(materialize_true, materialize_false, ast_id);
  owner()->set_current_block(join);
}


void ValueContext::ReturnContinuation(HIfContinuation* continuation,
                                      BailoutId ast_id) {
  HBasicBlock* materialize_true = NULL;
  HBasicBlock* materialize_false = NULL;
  continuation->Continue(&materialize_true, &materialize_false, NULL);
  if (continuation->IsTrueReachable()) {
    owner()->set_current_block(materialize_true);
    owner()->Push(owner()->graph()->GetConstantTrue());
    owner()->set_current_block(materialize_true);
  }
  if (continuation->IsFalseReachable()) {
    owner()->set_current_block(materialize_false);
    owner()->Push(owner()->graph()->GetConstantFalse());
    owner()->set_current_block(materialize_false);
  }
  if (continuation->TrueAndFalseReachable()) {
    HBasicBlock* join =
        owner()->CreateJoin(materialize_true, materialize_false, ast_id);
    owner()->set_current_block(join);
  }
}


void TestContext::ReturnInstruction(HInstruction* instr, BailoutId ast_id) {
  ASSERT(!instr->IsControlInstruction());
  HOptimizedGraphBuilder* builder = owner();
  builder->AddInstruction(instr);
  // We expect a simulate after every expression with side effects, though
  // this one isn't actually needed (and wouldn't work if it were targeted).
  if (instr->HasObservableSideEffects()) {
    builder->Push(instr);
    builder->AddSimulate(ast_id, REMOVABLE_SIMULATE);
    builder->Pop();
  }
  BuildBranch(instr);
}


void TestContext::ReturnControl(HControlInstruction* instr, BailoutId ast_id) {
  ASSERT(!instr->HasObservableSideEffects());
  HBasicBlock* empty_true = owner()->graph()->CreateBasicBlock();
  HBasicBlock* empty_false = owner()->graph()->CreateBasicBlock();
  instr->SetSuccessorAt(0, empty_true);
  instr->SetSuccessorAt(1, empty_false);
  owner()->current_block()->Finish(instr);
  empty_true->Goto(if_true(), owner()->function_state());
  empty_false->Goto(if_false(), owner()->function_state());
  owner()->set_current_block(NULL);
}


void TestContext::ReturnContinuation(HIfContinuation* continuation,
                                     BailoutId ast_id) {
  HBasicBlock* true_branch = NULL;
  HBasicBlock* false_branch = NULL;
  continuation->Continue(&true_branch, &false_branch, NULL);
  if (continuation->IsTrueReachable()) {
    true_branch->Goto(if_true(), owner()->function_state());
  }
  if (continuation->IsFalseReachable()) {
    false_branch->Goto(if_false(), owner()->function_state());
  }
  owner()->set_current_block(NULL);
}


void TestContext::BuildBranch(HValue* value) {
  // We expect the graph to be in edge-split form: there is no edge that
  // connects a branch node to a join node.  We conservatively ensure that
  // property by always adding an empty block on the outgoing edges of this
  // branch.
  HOptimizedGraphBuilder* builder = owner();
  if (value != NULL && value->CheckFlag(HValue::kIsArguments)) {
    builder->Bailout("arguments object value in a test context");
  }
  if (value->IsConstant()) {
    HConstant* constant_value = HConstant::cast(value);
    if (constant_value->BooleanValue()) {
      builder->current_block()->Goto(if_true(), builder->function_state());
    } else {
      builder->current_block()->Goto(if_false(), builder->function_state());
    }
    builder->set_current_block(NULL);
    return;
  }
  HBasicBlock* empty_true = builder->graph()->CreateBasicBlock();
  HBasicBlock* empty_false = builder->graph()->CreateBasicBlock();
  TypeFeedbackId test_id = condition()->test_id();
  ToBooleanStub::Types expected(oracle()->ToBooleanTypes(test_id));
  HBranch* test = new(zone()) HBranch(value, empty_true, empty_false, expected);
  builder->current_block()->Finish(test);

  empty_true->Goto(if_true(), builder->function_state());
  empty_false->Goto(if_false(), builder->function_state());
  builder->set_current_block(NULL);
}


// HOptimizedGraphBuilder infrastructure for bailing out and checking bailouts.
#define CHECK_BAILOUT(call)                     \
  do {                                          \
    call;                                       \
    if (HasStackOverflow()) return;             \
  } while (false)


#define CHECK_ALIVE(call)                                       \
  do {                                                          \
    call;                                                       \
    if (HasStackOverflow() || current_block() == NULL) return;  \
  } while (false)


void HOptimizedGraphBuilder::Bailout(const char* reason) {
  info()->set_bailout_reason(reason);
  SetStackOverflow();
}


void HOptimizedGraphBuilder::VisitForEffect(Expression* expr) {
  EffectContext for_effect(this);
  Visit(expr);
}


void HOptimizedGraphBuilder::VisitForValue(Expression* expr,
                                           ArgumentsAllowedFlag flag) {
  ValueContext for_value(this, flag);
  Visit(expr);
}


void HOptimizedGraphBuilder::VisitForTypeOf(Expression* expr) {
  ValueContext for_value(this, ARGUMENTS_NOT_ALLOWED);
  for_value.set_for_typeof(true);
  Visit(expr);
}



void HOptimizedGraphBuilder::VisitForControl(Expression* expr,
                                             HBasicBlock* true_block,
                                             HBasicBlock* false_block) {
  TestContext for_test(this, expr, oracle(), true_block, false_block);
  Visit(expr);
}


void HOptimizedGraphBuilder::VisitArgument(Expression* expr) {
  CHECK_ALIVE(VisitForValue(expr));
  Push(AddInstruction(new(zone()) HPushArgument(Pop())));
}


void HOptimizedGraphBuilder::VisitArgumentList(
    ZoneList<Expression*>* arguments) {
  for (int i = 0; i < arguments->length(); i++) {
    CHECK_ALIVE(VisitArgument(arguments->at(i)));
  }
}


void HOptimizedGraphBuilder::VisitExpressions(
    ZoneList<Expression*>* exprs) {
  for (int i = 0; i < exprs->length(); ++i) {
    CHECK_ALIVE(VisitForValue(exprs->at(i)));
  }
}


bool HOptimizedGraphBuilder::BuildGraph() {
  if (info()->function()->is_generator()) {
    Bailout("function is a generator");
    return false;
  }
  Scope* scope = info()->scope();
  if (scope->HasIllegalRedeclaration()) {
    Bailout("function with illegal redeclaration");
    return false;
  }
  if (scope->calls_eval()) {
    Bailout("function calls eval");
    return false;
  }
  SetUpScope(scope);

  // Add an edge to the body entry.  This is warty: the graph's start
  // environment will be used by the Lithium translation as the initial
  // environment on graph entry, but it has now been mutated by the
  // Hydrogen translation of the instructions in the start block.  This
  // environment uses values which have not been defined yet.  These
  // Hydrogen instructions will then be replayed by the Lithium
  // translation, so they cannot have an environment effect.  The edge to
  // the body's entry block (along with some special logic for the start
  // block in HInstruction::InsertAfter) seals the start block from
  // getting unwanted instructions inserted.
  //
  // TODO(kmillikin): Fix this.  Stop mutating the initial environment.
  // Make the Hydrogen instructions in the initial block into Hydrogen
  // values (but not instructions), present in the initial environment and
  // not replayed by the Lithium translation.
  HEnvironment* initial_env = environment()->CopyWithoutHistory();
  HBasicBlock* body_entry = CreateBasicBlock(initial_env);
  current_block()->Goto(body_entry);
  body_entry->SetJoinId(BailoutId::FunctionEntry());
  set_current_block(body_entry);

  // Handle implicit declaration of the function name in named function
  // expressions before other declarations.
  if (scope->is_function_scope() && scope->function() != NULL) {
    VisitVariableDeclaration(scope->function());
  }
  VisitDeclarations(scope->declarations());
  AddSimulate(BailoutId::Declarations());

  HValue* context = environment()->LookupContext();
  AddInstruction(
      new(zone()) HStackCheck(context, HStackCheck::kFunctionEntry));

  VisitStatements(info()->function()->body());
  if (HasStackOverflow()) return false;

  if (current_block() != NULL) {
    AddReturn(graph()->GetConstantUndefined());
    set_current_block(NULL);
  }

  // If the checksum of the number of type info changes is the same as the
  // last time this function was compiled, then this recompile is likely not
  // due to missing/inadequate type feedback, but rather too aggressive
  // optimization. Disable optimistic LICM in that case.
  Handle<Code> unoptimized_code(info()->shared_info()->code());
  ASSERT(unoptimized_code->kind() == Code::FUNCTION);
  Handle<TypeFeedbackInfo> type_info(
      TypeFeedbackInfo::cast(unoptimized_code->type_feedback_info()));
  int checksum = type_info->own_type_change_checksum();
  int composite_checksum = graph()->update_type_change_checksum(checksum);
  graph()->set_use_optimistic_licm(
      !type_info->matches_inlined_type_change_checksum(composite_checksum));
  type_info->set_inlined_type_change_checksum(composite_checksum);

  return true;
}


void HGraph::GlobalValueNumbering() {
  // Perform common subexpression elimination and loop-invariant code motion.
  if (FLAG_use_gvn) {
    HPhase phase("H_Global value numbering", this);
    HGlobalValueNumberer gvn(this, info());
    bool removed_side_effects = gvn.Analyze();
    // Trigger a second analysis pass to further eliminate duplicate values that
    // could only be discovered by removing side-effect-generating instructions
    // during the first pass.
    if (FLAG_smi_only_arrays && removed_side_effects) {
      removed_side_effects = gvn.Analyze();
      ASSERT(!removed_side_effects);
    }
  }
}


bool HGraph::Optimize(SmartArrayPointer<char>* bailout_reason) {
  *bailout_reason = SmartArrayPointer<char>();
  OrderBlocks();
  AssignDominators();

  // We need to create a HConstant "zero" now so that GVN will fold every
  // zero-valued constant in the graph together.
  // The constant is needed to make idef-based bounds check work: the pass
  // evaluates relations with "zero" and that zero cannot be created after GVN.
  GetConstant0();

#ifdef DEBUG
  // Do a full verify after building the graph and computing dominators.
  Verify(true);
#endif

  PropagateDeoptimizingMark();
  if (!CheckConstPhiUses()) {
    *bailout_reason = SmartArrayPointer<char>(StrDup(
        "Unsupported phi use of const variable"));
    return false;
  }
  EliminateRedundantPhis();
  if (!CheckArgumentsPhiUses()) {
    *bailout_reason = SmartArrayPointer<char>(StrDup(
        "Unsupported phi use of arguments"));
    return false;
  }
  if (FLAG_eliminate_dead_phis) EliminateUnreachablePhis();
  CollectPhis();

  if (has_osr_loop_entry()) {
    const ZoneList<HPhi*>* phis = osr_loop_entry()->phis();
    for (int j = 0; j < phis->length(); j++) {
      HPhi* phi = phis->at(j);
      osr_values()->at(phi->merged_index())->set_incoming_value(phi);
    }
  }

  HInferRepresentation rep(this);
  rep.Analyze();

  // Remove HSimulate instructions that have turned out not to be needed
  // after all by folding them into the following HSimulate.
  // This must happen after inferring representations.
  MergeRemovableSimulates();

  MarkDeoptimizeOnUndefined();
  InsertRepresentationChanges();

  InitializeInferredTypes();

  // Must be performed before canonicalization to ensure that Canonicalize
  // will not remove semantically meaningful ToInt32 operations e.g. BIT_OR with
  // zero.
  ComputeSafeUint32Operations();

  Canonicalize();

  GlobalValueNumbering();

  if (FLAG_use_range) {
    HRangeAnalysis rangeAnalysis(this);
    rangeAnalysis.Analyze();
  }
  ComputeMinusZeroChecks();

  // Eliminate redundant stack checks on backwards branches.
  HStackCheckEliminator sce(this);
  sce.Process();

  if (FLAG_idefs) SetupInformativeDefinitions();
  if (FLAG_array_bounds_checks_elimination && !FLAG_idefs) {
    EliminateRedundantBoundsChecks();
  }
  if (FLAG_array_index_dehoisting) DehoistSimpleArrayIndexComputations();
  if (FLAG_dead_code_elimination) DeadCodeElimination();

  RestoreActualValues();

  return true;
}


void HGraph::SetupInformativeDefinitionsInBlock(HBasicBlock* block) {
  for (int phi_index = 0; phi_index < block->phis()->length(); phi_index++) {
    HPhi* phi = block->phis()->at(phi_index);
    phi->AddInformativeDefinitions();
    phi->SetFlag(HValue::kIDefsProcessingDone);
    // We do not support phis that "redefine just one operand".
    ASSERT(!phi->IsInformativeDefinition());
  }

  for (HInstruction* i = block->first(); i != NULL; i = i->next()) {
    i->AddInformativeDefinitions();
    i->SetFlag(HValue::kIDefsProcessingDone);
    i->UpdateRedefinedUsesWhileSettingUpInformativeDefinitions();
  }
}


// This method is recursive, so if its stack frame is large it could
// cause a stack overflow.
// To keep the individual stack frames small we do the actual work inside
// SetupInformativeDefinitionsInBlock();
void HGraph::SetupInformativeDefinitionsRecursively(HBasicBlock* block) {
  SetupInformativeDefinitionsInBlock(block);
  for (int i = 0; i < block->dominated_blocks()->length(); ++i) {
    SetupInformativeDefinitionsRecursively(block->dominated_blocks()->at(i));
  }

  for (HInstruction* i = block->first(); i != NULL; i = i->next()) {
    if (i->IsBoundsCheck()) {
      HBoundsCheck* check = HBoundsCheck::cast(i);
      check->ApplyIndexChange();
    }
  }
}


void HGraph::SetupInformativeDefinitions() {
  HPhase phase("H_Setup informative definitions", this);
  SetupInformativeDefinitionsRecursively(entry_block());
}


// We try to "factor up" HBoundsCheck instructions towards the root of the
// dominator tree.
// For now we handle checks where the index is like "exp + int32value".
// If in the dominator tree we check "exp + v1" and later (dominated)
// "exp + v2", if v2 <= v1 we can safely remove the second check, and if
// v2 > v1 we can use v2 in the 1st check and again remove the second.
// To do so we keep a dictionary of all checks where the key if the pair
// "exp, length".
// The class BoundsCheckKey represents this key.
class BoundsCheckKey : public ZoneObject {
 public:
  HValue* IndexBase() const { return index_base_; }
  HValue* Length() const { return length_; }

  uint32_t Hash() {
    return static_cast<uint32_t>(index_base_->Hashcode() ^ length_->Hashcode());
  }

  static BoundsCheckKey* Create(Zone* zone,
                                HBoundsCheck* check,
                                int32_t* offset) {
    if (!check->index()->representation().IsInteger32()) return NULL;

    HValue* index_base = NULL;
    HConstant* constant = NULL;
    bool is_sub = false;

    if (check->index()->IsAdd()) {
      HAdd* index = HAdd::cast(check->index());
      if (index->left()->IsConstant()) {
        constant = HConstant::cast(index->left());
        index_base = index->right();
      } else if (index->right()->IsConstant()) {
        constant = HConstant::cast(index->right());
        index_base = index->left();
      }
    } else if (check->index()->IsSub()) {
      HSub* index = HSub::cast(check->index());
      is_sub = true;
      if (index->left()->IsConstant()) {
        constant = HConstant::cast(index->left());
        index_base = index->right();
      } else if (index->right()->IsConstant()) {
        constant = HConstant::cast(index->right());
        index_base = index->left();
      }
    }

    if (constant != NULL && constant->HasInteger32Value()) {
      *offset = is_sub ? - constant->Integer32Value()
                       : constant->Integer32Value();
    } else {
      *offset = 0;
      index_base = check->index();
    }

    return new(zone) BoundsCheckKey(index_base, check->length());
  }

 private:
  BoundsCheckKey(HValue* index_base, HValue* length)
    : index_base_(index_base),
      length_(length) { }

  HValue* index_base_;
  HValue* length_;
};


// Data about each HBoundsCheck that can be eliminated or moved.
// It is the "value" in the dictionary indexed by "base-index, length"
// (the key is BoundsCheckKey).
// We scan the code with a dominator tree traversal.
// Traversing the dominator tree we keep a stack (implemented as a singly
// linked list) of "data" for each basic block that contains a relevant check
// with the same key (the dictionary holds the head of the list).
// We also keep all the "data" created for a given basic block in a list, and
// use it to "clean up" the dictionary when backtracking in the dominator tree
// traversal.
// Doing this each dictionary entry always directly points to the check that
// is dominating the code being examined now.
// We also track the current "offset" of the index expression and use it to
// decide if any check is already "covered" (so it can be removed) or not.
class BoundsCheckBbData: public ZoneObject {
 public:
  BoundsCheckKey* Key() const { return key_; }
  int32_t LowerOffset() const { return lower_offset_; }
  int32_t UpperOffset() const { return upper_offset_; }
  HBasicBlock* BasicBlock() const { return basic_block_; }
  HBoundsCheck* LowerCheck() const { return lower_check_; }
  HBoundsCheck* UpperCheck() const { return upper_check_; }
  BoundsCheckBbData* NextInBasicBlock() const { return next_in_bb_; }
  BoundsCheckBbData* FatherInDominatorTree() const { return father_in_dt_; }

  bool OffsetIsCovered(int32_t offset) const {
    return offset >= LowerOffset() && offset <= UpperOffset();
  }

  bool HasSingleCheck() { return lower_check_ == upper_check_; }

  // The goal of this method is to modify either upper_offset_ or
  // lower_offset_ so that also new_offset is covered (the covered
  // range grows).
  //
  // The precondition is that new_check follows UpperCheck() and
  // LowerCheck() in the same basic block, and that new_offset is not
  // covered (otherwise we could simply remove new_check).
  //
  // If HasSingleCheck() is true then new_check is added as "second check"
  // (either upper or lower; note that HasSingleCheck() becomes false).
  // Otherwise one of the current checks is modified so that it also covers
  // new_offset, and new_check is removed.
  //
  // If the check cannot be modified because the context is unknown it
  // returns false, otherwise it returns true.
  bool CoverCheck(HBoundsCheck* new_check,
                  int32_t new_offset) {
    ASSERT(new_check->index()->representation().IsInteger32());
    bool keep_new_check = false;

    if (new_offset > upper_offset_) {
      upper_offset_ = new_offset;
      if (HasSingleCheck()) {
        keep_new_check = true;
        upper_check_ = new_check;
      } else {
        bool result = BuildOffsetAdd(upper_check_,
                                     &added_upper_index_,
                                     &added_upper_offset_,
                                     Key()->IndexBase(),
                                     new_check->index()->representation(),
                                     new_offset);
        if (!result) return false;
        upper_check_->ReplaceAllUsesWith(upper_check_->index());
        upper_check_->SetOperandAt(0, added_upper_index_);
      }
    } else if (new_offset < lower_offset_) {
      lower_offset_ = new_offset;
      if (HasSingleCheck()) {
        keep_new_check = true;
        lower_check_ = new_check;
      } else {
        bool result = BuildOffsetAdd(lower_check_,
                                     &added_lower_index_,
                                     &added_lower_offset_,
                                     Key()->IndexBase(),
                                     new_check->index()->representation(),
                                     new_offset);
        if (!result) return false;
        lower_check_->ReplaceAllUsesWith(lower_check_->index());
        lower_check_->SetOperandAt(0, added_lower_index_);
      }
    } else {
      ASSERT(false);
    }

    if (!keep_new_check) {
      new_check->DeleteAndReplaceWith(new_check->ActualValue());
    }

    return true;
  }

  void RemoveZeroOperations() {
    RemoveZeroAdd(&added_lower_index_, &added_lower_offset_);
    RemoveZeroAdd(&added_upper_index_, &added_upper_offset_);
  }

  BoundsCheckBbData(BoundsCheckKey* key,
                    int32_t lower_offset,
                    int32_t upper_offset,
                    HBasicBlock* bb,
                    HBoundsCheck* lower_check,
                    HBoundsCheck* upper_check,
                    BoundsCheckBbData* next_in_bb,
                    BoundsCheckBbData* father_in_dt)
  : key_(key),
    lower_offset_(lower_offset),
    upper_offset_(upper_offset),
    basic_block_(bb),
    lower_check_(lower_check),
    upper_check_(upper_check),
    added_lower_index_(NULL),
    added_lower_offset_(NULL),
    added_upper_index_(NULL),
    added_upper_offset_(NULL),
    next_in_bb_(next_in_bb),
    father_in_dt_(father_in_dt) { }

 private:
  BoundsCheckKey* key_;
  int32_t lower_offset_;
  int32_t upper_offset_;
  HBasicBlock* basic_block_;
  HBoundsCheck* lower_check_;
  HBoundsCheck* upper_check_;
  HInstruction* added_lower_index_;
  HConstant* added_lower_offset_;
  HInstruction* added_upper_index_;
  HConstant* added_upper_offset_;
  BoundsCheckBbData* next_in_bb_;
  BoundsCheckBbData* father_in_dt_;

  // Given an existing add instruction and a bounds check it tries to
  // find the current context (either of the add or of the check index).
  HValue* IndexContext(HInstruction* add, HBoundsCheck* check) {
    if (add != NULL && add->IsAdd()) {
      return HAdd::cast(add)->context();
    }
    if (check->index()->IsBinaryOperation()) {
      return HBinaryOperation::cast(check->index())->context();
    }
    return NULL;
  }

  // This function returns false if it cannot build the add because the
  // current context cannot be determined.
  bool BuildOffsetAdd(HBoundsCheck* check,
                      HInstruction** add,
                      HConstant** constant,
                      HValue* original_value,
                      Representation representation,
                      int32_t new_offset) {
    HValue* index_context = IndexContext(*add, check);
    if (index_context == NULL) return false;

    HConstant* new_constant = new(BasicBlock()->zone())
       HConstant(new_offset, Representation::Integer32());
    if (*add == NULL) {
      new_constant->InsertBefore(check);
      (*add) = HAdd::New(
          BasicBlock()->zone(), index_context, original_value, new_constant);
      (*add)->AssumeRepresentation(representation);
      (*add)->InsertBefore(check);
    } else {
      new_constant->InsertBefore(*add);
      (*constant)->DeleteAndReplaceWith(new_constant);
    }
    *constant = new_constant;
    return true;
  }

  void RemoveZeroAdd(HInstruction** add, HConstant** constant) {
    if (*add != NULL && (*add)->IsAdd() && (*constant)->Integer32Value() == 0) {
      (*add)->DeleteAndReplaceWith(HAdd::cast(*add)->left());
      (*constant)->DeleteAndReplaceWith(NULL);
    }
  }
};


static bool BoundsCheckKeyMatch(void* key1, void* key2) {
  BoundsCheckKey* k1 = static_cast<BoundsCheckKey*>(key1);
  BoundsCheckKey* k2 = static_cast<BoundsCheckKey*>(key2);
  return k1->IndexBase() == k2->IndexBase() && k1->Length() == k2->Length();
}


class BoundsCheckTable : private ZoneHashMap {
 public:
  BoundsCheckBbData** LookupOrInsert(BoundsCheckKey* key, Zone* zone) {
    return reinterpret_cast<BoundsCheckBbData**>(
        &(Lookup(key, key->Hash(), true, ZoneAllocationPolicy(zone))->value));
  }

  void Insert(BoundsCheckKey* key, BoundsCheckBbData* data, Zone* zone) {
    Lookup(key, key->Hash(), true, ZoneAllocationPolicy(zone))->value = data;
  }

  void Delete(BoundsCheckKey* key) {
    Remove(key, key->Hash());
  }

  explicit BoundsCheckTable(Zone* zone)
      : ZoneHashMap(BoundsCheckKeyMatch, ZoneHashMap::kDefaultHashMapCapacity,
                    ZoneAllocationPolicy(zone)) { }
};


// Eliminates checks in bb and recursively in the dominated blocks.
// Also replace the results of check instructions with the original value, if
// the result is used. This is safe now, since we don't do code motion after
// this point. It enables better register allocation since the value produced
// by check instructions is really a copy of the original value.
void HGraph::EliminateRedundantBoundsChecks(HBasicBlock* bb,
                                            BoundsCheckTable* table) {
  BoundsCheckBbData* bb_data_list = NULL;

  for (HInstruction* i = bb->first(); i != NULL; i = i->next()) {
    if (!i->IsBoundsCheck()) continue;

    HBoundsCheck* check = HBoundsCheck::cast(i);
    int32_t offset;
    BoundsCheckKey* key =
        BoundsCheckKey::Create(zone(), check, &offset);
    if (key == NULL) continue;
    BoundsCheckBbData** data_p = table->LookupOrInsert(key, zone());
    BoundsCheckBbData* data = *data_p;
    if (data == NULL) {
      bb_data_list = new(zone()) BoundsCheckBbData(key,
                                                   offset,
                                                   offset,
                                                   bb,
                                                   check,
                                                   check,
                                                   bb_data_list,
                                                   NULL);
      *data_p = bb_data_list;
    } else if (data->OffsetIsCovered(offset)) {
      check->DeleteAndReplaceWith(check->ActualValue());
    } else if (data->BasicBlock() != bb ||
               !data->CoverCheck(check, offset)) {
      // If the check is in the current BB we try to modify it by calling
      // "CoverCheck", but if also that fails we record the current offsets
      // in a new data instance because from now on they are covered.
      int32_t new_lower_offset = offset < data->LowerOffset()
          ? offset
          : data->LowerOffset();
      int32_t new_upper_offset = offset > data->UpperOffset()
          ? offset
          : data->UpperOffset();
      bb_data_list = new(zone()) BoundsCheckBbData(key,
                                                   new_lower_offset,
                                                   new_upper_offset,
                                                   bb,
                                                   data->LowerCheck(),
                                                   data->UpperCheck(),
                                                   bb_data_list,
                                                   data);
      table->Insert(key, bb_data_list, zone());
    }
  }

  for (int i = 0; i < bb->dominated_blocks()->length(); ++i) {
    EliminateRedundantBoundsChecks(bb->dominated_blocks()->at(i), table);
  }

  for (BoundsCheckBbData* data = bb_data_list;
       data != NULL;
       data = data->NextInBasicBlock()) {
    data->RemoveZeroOperations();
    if (data->FatherInDominatorTree()) {
      table->Insert(data->Key(), data->FatherInDominatorTree(), zone());
    } else {
      table->Delete(data->Key());
    }
  }
}


void HGraph::EliminateRedundantBoundsChecks() {
  HPhase phase("H_Eliminate bounds checks", this);
  BoundsCheckTable checks_table(zone());
  EliminateRedundantBoundsChecks(entry_block(), &checks_table);
}


static void DehoistArrayIndex(ArrayInstructionInterface* array_operation) {
  HValue* index = array_operation->GetKey()->ActualValue();
  if (!index->representation().IsInteger32()) return;

  HConstant* constant;
  HValue* subexpression;
  int32_t sign;
  if (index->IsAdd()) {
    sign = 1;
    HAdd* add = HAdd::cast(index);
    if (add->left()->IsConstant()) {
      subexpression = add->right();
      constant = HConstant::cast(add->left());
    } else if (add->right()->IsConstant()) {
      subexpression = add->left();
      constant = HConstant::cast(add->right());
    } else {
      return;
    }
  } else if (index->IsSub()) {
    sign = -1;
    HSub* sub = HSub::cast(index);
    if (sub->left()->IsConstant()) {
      subexpression = sub->right();
      constant = HConstant::cast(sub->left());
    } else if (sub->right()->IsConstant()) {
      subexpression = sub->left();
      constant = HConstant::cast(sub->right());
    } return;
  } else {
    return;
  }

  if (!constant->HasInteger32Value()) return;
  int32_t value = constant->Integer32Value() * sign;
  // We limit offset values to 30 bits because we want to avoid the risk of
  // overflows when the offset is added to the object header size.
  if (value >= 1 << 30 || value < 0) return;
  array_operation->SetKey(subexpression);
  if (index->HasNoUses()) {
    index->DeleteAndReplaceWith(NULL);
  }
  ASSERT(value >= 0);
  array_operation->SetIndexOffset(static_cast<uint32_t>(value));
  array_operation->SetDehoisted(true);
}


void HGraph::DehoistSimpleArrayIndexComputations() {
  HPhase phase("H_Dehoist index computations", this);
  for (int i = 0; i < blocks()->length(); ++i) {
    for (HInstruction* instr = blocks()->at(i)->first();
        instr != NULL;
        instr = instr->next()) {
      ArrayInstructionInterface* array_instruction = NULL;
      if (instr->IsLoadKeyed()) {
        HLoadKeyed* op = HLoadKeyed::cast(instr);
        array_instruction = static_cast<ArrayInstructionInterface*>(op);
      } else if (instr->IsStoreKeyed()) {
        HStoreKeyed* op = HStoreKeyed::cast(instr);
        array_instruction = static_cast<ArrayInstructionInterface*>(op);
      } else {
        continue;
      }
      DehoistArrayIndex(array_instruction);
    }
  }
}


void HGraph::DeadCodeElimination() {
  HPhase phase("H_Dead code elimination", this);
  ZoneList<HInstruction*> worklist(blocks_.length(), zone());
  for (int i = 0; i < blocks()->length(); ++i) {
    for (HInstruction* instr = blocks()->at(i)->first();
         instr != NULL;
         instr = instr->next()) {
      if (instr->IsDead()) worklist.Add(instr, zone());
    }
  }

  while (!worklist.is_empty()) {
    HInstruction* instr = worklist.RemoveLast();
    // This happens when an instruction is used multiple times as operand. That
    // in turn could happen through GVN.
    if (!instr->IsLinked()) continue;
    if (FLAG_trace_dead_code_elimination) {
      HeapStringAllocator allocator;
      StringStream stream(&allocator);
      instr->PrintNameTo(&stream);
      stream.Add(" = ");
      instr->PrintTo(&stream);
      PrintF("[removing dead instruction %s]\n", *stream.ToCString());
    }
    instr->DeleteAndReplaceWith(NULL);
    for (int i = 0; i < instr->OperandCount(); ++i) {
      HValue* operand = instr->OperandAt(i);
      if (operand->IsDead()) worklist.Add(HInstruction::cast(operand), zone());
    }
  }
}


void HGraph::RestoreActualValues() {
  HPhase phase("H_Restore actual values", this);

  for (int block_index = 0; block_index < blocks()->length(); block_index++) {
    HBasicBlock* block = blocks()->at(block_index);

#ifdef DEBUG
    for (int i = 0; i < block->phis()->length(); i++) {
      HPhi* phi = block->phis()->at(i);
      ASSERT(phi->ActualValue() == phi);
    }
#endif

    for (HInstruction* instruction = block->first();
        instruction != NULL;
        instruction = instruction->next()) {
      if (instruction->ActualValue() != instruction) {
        ASSERT(instruction->IsInformativeDefinition());
        if (instruction->IsPurelyInformativeDefinition()) {
          instruction->DeleteAndReplaceWith(instruction->RedefinedOperand());
        } else {
          instruction->ReplaceAllUsesWith(instruction->ActualValue());
        }
      }
    }
  }
}


void HOptimizedGraphBuilder::AddPhi(HPhi* instr) {
  ASSERT(current_block() != NULL);
  current_block()->AddPhi(instr);
}


void HOptimizedGraphBuilder::PushAndAdd(HInstruction* instr) {
  Push(instr);
  AddInstruction(instr);
}


void HOptimizedGraphBuilder::AddSoftDeoptimize() {
  if (FLAG_always_opt) return;
  if (current_block()->IsDeoptimizing()) return;
  AddInstruction(new(zone()) HSoftDeoptimize());
  current_block()->MarkAsDeoptimizing();
  graph()->set_has_soft_deoptimize(true);
}


template <class Instruction>
HInstruction* HOptimizedGraphBuilder::PreProcessCall(Instruction* call) {
  int count = call->argument_count();
  ZoneList<HValue*> arguments(count, zone());
  for (int i = 0; i < count; ++i) {
    arguments.Add(Pop(), zone());
  }

  while (!arguments.is_empty()) {
    AddInstruction(new(zone()) HPushArgument(arguments.RemoveLast()));
  }
  return call;
}


void HOptimizedGraphBuilder::SetUpScope(Scope* scope) {
  HConstant* undefined_constant = new(zone()) HConstant(
      isolate()->factory()->undefined_value(), Representation::Tagged());
  AddInstruction(undefined_constant);
  graph()->set_undefined_constant(undefined_constant);

  HArgumentsObject* object = new(zone()) HArgumentsObject;
  AddInstruction(object);
  graph()->SetArgumentsObject(object);

  // Set the initial values of parameters including "this".  "This" has
  // parameter index 0.
  ASSERT_EQ(scope->num_parameters() + 1, environment()->parameter_count());

  for (int i = 0; i < environment()->parameter_count(); ++i) {
    HInstruction* parameter = AddInstruction(new(zone()) HParameter(i));
    environment()->Bind(i, parameter);
  }

  // First special is HContext.
  HInstruction* context = AddInstruction(new(zone()) HContext);
  environment()->BindContext(context);

  // Initialize specials and locals to undefined.
  for (int i = environment()->parameter_count() + 1;
       i < environment()->length();
       ++i) {
    environment()->Bind(i, undefined_constant);
  }

  // Handle the arguments and arguments shadow variables specially (they do
  // not have declarations).
  if (scope->arguments() != NULL) {
    if (!scope->arguments()->IsStackAllocated()) {
      return Bailout("context-allocated arguments");
    }

    environment()->Bind(scope->arguments(),
                        graph()->GetArgumentsObject());
  }
}


void HOptimizedGraphBuilder::VisitStatements(ZoneList<Statement*>* statements) {
  for (int i = 0; i < statements->length(); i++) {
    CHECK_ALIVE(Visit(statements->at(i)));
  }
}


void HOptimizedGraphBuilder::VisitBlock(Block* stmt) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  if (stmt->scope() != NULL) {
    return Bailout("ScopedBlock");
  }
  BreakAndContinueInfo break_info(stmt);
  { BreakAndContinueScope push(&break_info, this);
    CHECK_BAILOUT(VisitStatements(stmt->statements()));
  }
  HBasicBlock* break_block = break_info.break_block();
  if (break_block != NULL) {
    if (current_block() != NULL) current_block()->Goto(break_block);
    break_block->SetJoinId(stmt->ExitId());
    set_current_block(break_block);
  }
}


void HOptimizedGraphBuilder::VisitExpressionStatement(
    ExpressionStatement* stmt) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  VisitForEffect(stmt->expression());
}


void HOptimizedGraphBuilder::VisitEmptyStatement(EmptyStatement* stmt) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
}


void HOptimizedGraphBuilder::VisitIfStatement(IfStatement* stmt) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  if (stmt->condition()->ToBooleanIsTrue()) {
    AddSimulate(stmt->ThenId());
    Visit(stmt->then_statement());
  } else if (stmt->condition()->ToBooleanIsFalse()) {
    AddSimulate(stmt->ElseId());
    Visit(stmt->else_statement());
  } else {
    HBasicBlock* cond_true = graph()->CreateBasicBlock();
    HBasicBlock* cond_false = graph()->CreateBasicBlock();
    CHECK_BAILOUT(VisitForControl(stmt->condition(), cond_true, cond_false));

    if (cond_true->HasPredecessor()) {
      cond_true->SetJoinId(stmt->ThenId());
      set_current_block(cond_true);
      CHECK_BAILOUT(Visit(stmt->then_statement()));
      cond_true = current_block();
    } else {
      cond_true = NULL;
    }

    if (cond_false->HasPredecessor()) {
      cond_false->SetJoinId(stmt->ElseId());
      set_current_block(cond_false);
      CHECK_BAILOUT(Visit(stmt->else_statement()));
      cond_false = current_block();
    } else {
      cond_false = NULL;
    }

    HBasicBlock* join = CreateJoin(cond_true, cond_false, stmt->IfId());
    set_current_block(join);
  }
}


HBasicBlock* HOptimizedGraphBuilder::BreakAndContinueScope::Get(
    BreakableStatement* stmt,
    BreakType type,
    int* drop_extra) {
  *drop_extra = 0;
  BreakAndContinueScope* current = this;
  while (current != NULL && current->info()->target() != stmt) {
    *drop_extra += current->info()->drop_extra();
    current = current->next();
  }
  ASSERT(current != NULL);  // Always found (unless stack is malformed).

  if (type == BREAK) {
    *drop_extra += current->info()->drop_extra();
  }

  HBasicBlock* block = NULL;
  switch (type) {
    case BREAK:
      block = current->info()->break_block();
      if (block == NULL) {
        block = current->owner()->graph()->CreateBasicBlock();
        current->info()->set_break_block(block);
      }
      break;

    case CONTINUE:
      block = current->info()->continue_block();
      if (block == NULL) {
        block = current->owner()->graph()->CreateBasicBlock();
        current->info()->set_continue_block(block);
      }
      break;
  }

  return block;
}


void HOptimizedGraphBuilder::VisitContinueStatement(
    ContinueStatement* stmt) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  int drop_extra = 0;
  HBasicBlock* continue_block = break_scope()->Get(stmt->target(),
                                                   CONTINUE,
                                                   &drop_extra);
  Drop(drop_extra);
  current_block()->Goto(continue_block);
  set_current_block(NULL);
}


void HOptimizedGraphBuilder::VisitBreakStatement(BreakStatement* stmt) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  int drop_extra = 0;
  HBasicBlock* break_block = break_scope()->Get(stmt->target(),
                                                BREAK,
                                                &drop_extra);
  Drop(drop_extra);
  current_block()->Goto(break_block);
  set_current_block(NULL);
}


void HOptimizedGraphBuilder::VisitReturnStatement(ReturnStatement* stmt) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  FunctionState* state = function_state();
  AstContext* context = call_context();
  if (context == NULL) {
    // Not an inlined return, so an actual one.
    CHECK_ALIVE(VisitForValue(stmt->expression()));
    HValue* result = environment()->Pop();
    AddReturn(result);
  } else if (state->inlining_kind() == CONSTRUCT_CALL_RETURN) {
    // Return from an inlined construct call. In a test context the return value
    // will always evaluate to true, in a value context the return value needs
    // to be a JSObject.
    if (context->IsTest()) {
      TestContext* test = TestContext::cast(context);
      CHECK_ALIVE(VisitForEffect(stmt->expression()));
      current_block()->Goto(test->if_true(), state);
    } else if (context->IsEffect()) {
      CHECK_ALIVE(VisitForEffect(stmt->expression()));
      current_block()->Goto(function_return(), state);
    } else {
      ASSERT(context->IsValue());
      CHECK_ALIVE(VisitForValue(stmt->expression()));
      HValue* return_value = Pop();
      HValue* receiver = environment()->arguments_environment()->Lookup(0);
      HHasInstanceTypeAndBranch* typecheck =
          new(zone()) HHasInstanceTypeAndBranch(return_value,
                                                FIRST_SPEC_OBJECT_TYPE,
                                                LAST_SPEC_OBJECT_TYPE);
      HBasicBlock* if_spec_object = graph()->CreateBasicBlock();
      HBasicBlock* not_spec_object = graph()->CreateBasicBlock();
      typecheck->SetSuccessorAt(0, if_spec_object);
      typecheck->SetSuccessorAt(1, not_spec_object);
      current_block()->Finish(typecheck);
      if_spec_object->AddLeaveInlined(return_value, state);
      not_spec_object->AddLeaveInlined(receiver, state);
    }
  } else if (state->inlining_kind() == SETTER_CALL_RETURN) {
    // Return from an inlined setter call. The returned value is never used, the
    // value of an assignment is always the value of the RHS of the assignment.
    CHECK_ALIVE(VisitForEffect(stmt->expression()));
    if (context->IsTest()) {
      HValue* rhs = environment()->arguments_environment()->Lookup(1);
      context->ReturnValue(rhs);
    } else if (context->IsEffect()) {
      current_block()->Goto(function_return(), state);
    } else {
      ASSERT(context->IsValue());
      HValue* rhs = environment()->arguments_environment()->Lookup(1);
      current_block()->AddLeaveInlined(rhs, state);
    }
  } else {
    // Return from a normal inlined function. Visit the subexpression in the
    // expression context of the call.
    if (context->IsTest()) {
      TestContext* test = TestContext::cast(context);
      VisitForControl(stmt->expression(), test->if_true(), test->if_false());
    } else if (context->IsEffect()) {
      CHECK_ALIVE(VisitForEffect(stmt->expression()));
      current_block()->Goto(function_return(), state);
    } else {
      ASSERT(context->IsValue());
      CHECK_ALIVE(VisitForValue(stmt->expression()));
      current_block()->AddLeaveInlined(Pop(), state);
    }
  }
  set_current_block(NULL);
}


void HOptimizedGraphBuilder::VisitWithStatement(WithStatement* stmt) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  return Bailout("WithStatement");
}


void HOptimizedGraphBuilder::VisitSwitchStatement(SwitchStatement* stmt) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  // We only optimize switch statements with smi-literal smi comparisons,
  // with a bounded number of clauses.
  const int kCaseClauseLimit = 128;
  ZoneList<CaseClause*>* clauses = stmt->cases();
  int clause_count = clauses->length();
  if (clause_count > kCaseClauseLimit) {
    return Bailout("SwitchStatement: too many clauses");
  }

  HValue* context = environment()->LookupContext();

  CHECK_ALIVE(VisitForValue(stmt->tag()));
  AddSimulate(stmt->EntryId());
  HValue* tag_value = Pop();
  HBasicBlock* first_test_block = current_block();

  SwitchType switch_type = UNKNOWN_SWITCH;

  // 1. Extract clause type
  for (int i = 0; i < clause_count; ++i) {
    CaseClause* clause = clauses->at(i);
    if (clause->is_default()) continue;

    if (switch_type == UNKNOWN_SWITCH) {
      if (clause->label()->IsSmiLiteral()) {
        switch_type = SMI_SWITCH;
      } else if (clause->label()->IsStringLiteral()) {
        switch_type = STRING_SWITCH;
      } else {
        return Bailout("SwitchStatement: non-literal switch label");
      }
    } else if ((switch_type == STRING_SWITCH &&
                !clause->label()->IsStringLiteral()) ||
               (switch_type == SMI_SWITCH &&
                !clause->label()->IsSmiLiteral())) {
      return Bailout("SwitchStatement: mixed label types are not supported");
    }
  }

  HUnaryControlInstruction* string_check = NULL;
  HBasicBlock* not_string_block = NULL;

  // Test switch's tag value if all clauses are string literals
  if (switch_type == STRING_SWITCH) {
    string_check = new(zone()) HIsStringAndBranch(tag_value);
    first_test_block = graph()->CreateBasicBlock();
    not_string_block = graph()->CreateBasicBlock();

    string_check->SetSuccessorAt(0, first_test_block);
    string_check->SetSuccessorAt(1, not_string_block);
    current_block()->Finish(string_check);

    set_current_block(first_test_block);
  }

  // 2. Build all the tests, with dangling true branches
  BailoutId default_id = BailoutId::None();
  for (int i = 0; i < clause_count; ++i) {
    CaseClause* clause = clauses->at(i);
    if (clause->is_default()) {
      default_id = clause->EntryId();
      continue;
    }
    if (switch_type == SMI_SWITCH) {
      clause->RecordTypeFeedback(oracle());
    }

    // Generate a compare and branch.
    CHECK_ALIVE(VisitForValue(clause->label()));
    HValue* label_value = Pop();

    HBasicBlock* next_test_block = graph()->CreateBasicBlock();
    HBasicBlock* body_block = graph()->CreateBasicBlock();

    HControlInstruction* compare;

    if (switch_type == SMI_SWITCH) {
      if (!clause->IsSmiCompare()) {
        // Finish with deoptimize and add uses of enviroment values to
        // account for invisible uses.
        current_block()->FinishExitWithDeoptimization(HDeoptimize::kUseAll);
        set_current_block(NULL);
        break;
      }

      HCompareIDAndBranch* compare_ =
          new(zone()) HCompareIDAndBranch(tag_value,
                                          label_value,
                                          Token::EQ_STRICT);
      compare_->set_observed_input_representation(
          Representation::Integer32(), Representation::Integer32());
      compare = compare_;
    } else {
      compare = new(zone()) HStringCompareAndBranch(context, tag_value,
                                                    label_value,
                                                    Token::EQ_STRICT);
    }

    compare->SetSuccessorAt(0, body_block);
    compare->SetSuccessorAt(1, next_test_block);
    current_block()->Finish(compare);

    set_current_block(next_test_block);
  }

  // Save the current block to use for the default or to join with the
  // exit.  This block is NULL if we deoptimized.
  HBasicBlock* last_block = current_block();

  if (not_string_block != NULL) {
    BailoutId join_id = !default_id.IsNone() ? default_id : stmt->ExitId();
    last_block = CreateJoin(last_block, not_string_block, join_id);
  }

  // 3. Loop over the clauses and the linked list of tests in lockstep,
  // translating the clause bodies.
  HBasicBlock* curr_test_block = first_test_block;
  HBasicBlock* fall_through_block = NULL;

  BreakAndContinueInfo break_info(stmt);
  { BreakAndContinueScope push(&break_info, this);
    for (int i = 0; i < clause_count; ++i) {
      CaseClause* clause = clauses->at(i);

      // Identify the block where normal (non-fall-through) control flow
      // goes to.
      HBasicBlock* normal_block = NULL;
      if (clause->is_default()) {
        if (last_block != NULL) {
          normal_block = last_block;
          last_block = NULL;  // Cleared to indicate we've handled it.
        }
      } else if (!curr_test_block->end()->IsDeoptimize()) {
        normal_block = curr_test_block->end()->FirstSuccessor();
        curr_test_block = curr_test_block->end()->SecondSuccessor();
      }

      // Identify a block to emit the body into.
      if (normal_block == NULL) {
        if (fall_through_block == NULL) {
          // (a) Unreachable.
          if (clause->is_default()) {
            continue;  // Might still be reachable clause bodies.
          } else {
            break;
          }
        } else {
          // (b) Reachable only as fall through.
          set_current_block(fall_through_block);
        }
      } else if (fall_through_block == NULL) {
        // (c) Reachable only normally.
        set_current_block(normal_block);
      } else {
        // (d) Reachable both ways.
        HBasicBlock* join = CreateJoin(fall_through_block,
                                       normal_block,
                                       clause->EntryId());
        set_current_block(join);
      }

      CHECK_BAILOUT(VisitStatements(clause->statements()));
      fall_through_block = current_block();
    }
  }

  // Create an up-to-3-way join.  Use the break block if it exists since
  // it's already a join block.
  HBasicBlock* break_block = break_info.break_block();
  if (break_block == NULL) {
    set_current_block(CreateJoin(fall_through_block,
                                 last_block,
                                 stmt->ExitId()));
  } else {
    if (fall_through_block != NULL) fall_through_block->Goto(break_block);
    if (last_block != NULL) last_block->Goto(break_block);
    break_block->SetJoinId(stmt->ExitId());
    set_current_block(break_block);
  }
}


bool HOptimizedGraphBuilder::HasOsrEntryAt(IterationStatement* statement) {
  return statement->OsrEntryId() == info()->osr_ast_id();
}


bool HOptimizedGraphBuilder::PreProcessOsrEntry(IterationStatement* statement) {
  if (!HasOsrEntryAt(statement)) return false;

  HBasicBlock* non_osr_entry = graph()->CreateBasicBlock();
  HBasicBlock* osr_entry = graph()->CreateBasicBlock();
  HValue* true_value = graph()->GetConstantTrue();
  HBranch* test = new(zone()) HBranch(true_value, non_osr_entry, osr_entry);
  current_block()->Finish(test);

  HBasicBlock* loop_predecessor = graph()->CreateBasicBlock();
  non_osr_entry->Goto(loop_predecessor);

  set_current_block(osr_entry);
  osr_entry->set_osr_entry();
  BailoutId osr_entry_id = statement->OsrEntryId();
  int first_expression_index = environment()->first_expression_index();
  int length = environment()->length();
  ZoneList<HUnknownOSRValue*>* osr_values =
      new(zone()) ZoneList<HUnknownOSRValue*>(length, zone());

  for (int i = 0; i < first_expression_index; ++i) {
    HUnknownOSRValue* osr_value = new(zone()) HUnknownOSRValue;
    AddInstruction(osr_value);
    environment()->Bind(i, osr_value);
    osr_values->Add(osr_value, zone());
  }

  if (first_expression_index != length) {
    environment()->Drop(length - first_expression_index);
    for (int i = first_expression_index; i < length; ++i) {
      HUnknownOSRValue* osr_value = new(zone()) HUnknownOSRValue;
      AddInstruction(osr_value);
      environment()->Push(osr_value);
      osr_values->Add(osr_value, zone());
    }
  }

  graph()->set_osr_values(osr_values);

  AddSimulate(osr_entry_id);
  AddInstruction(new(zone()) HOsrEntry(osr_entry_id));
  HContext* context = new(zone()) HContext;
  AddInstruction(context);
  environment()->BindContext(context);
  current_block()->Goto(loop_predecessor);
  loop_predecessor->SetJoinId(statement->EntryId());
  set_current_block(loop_predecessor);
  return true;
}


void HOptimizedGraphBuilder::VisitLoopBody(IterationStatement* stmt,
                                           HBasicBlock* loop_entry,
                                           BreakAndContinueInfo* break_info) {
  BreakAndContinueScope push(break_info, this);
  AddSimulate(stmt->StackCheckId());
  HValue* context = environment()->LookupContext();
  HStackCheck* stack_check =
    new(zone()) HStackCheck(context, HStackCheck::kBackwardsBranch);
  AddInstruction(stack_check);
  ASSERT(loop_entry->IsLoopHeader());
  loop_entry->loop_information()->set_stack_check(stack_check);
  CHECK_BAILOUT(Visit(stmt->body()));
}


void HOptimizedGraphBuilder::VisitDoWhileStatement(DoWhileStatement* stmt) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  ASSERT(current_block() != NULL);
  bool osr_entry = PreProcessOsrEntry(stmt);
  HBasicBlock* loop_entry = CreateLoopHeaderBlock();
  current_block()->Goto(loop_entry);
  set_current_block(loop_entry);
  if (osr_entry) graph()->set_osr_loop_entry(loop_entry);

  BreakAndContinueInfo break_info(stmt);
  CHECK_BAILOUT(VisitLoopBody(stmt, loop_entry, &break_info));
  HBasicBlock* body_exit =
      JoinContinue(stmt, current_block(), break_info.continue_block());
  HBasicBlock* loop_successor = NULL;
  if (body_exit != NULL && !stmt->cond()->ToBooleanIsTrue()) {
    set_current_block(body_exit);
    // The block for a true condition, the actual predecessor block of the
    // back edge.
    body_exit = graph()->CreateBasicBlock();
    loop_successor = graph()->CreateBasicBlock();
    CHECK_BAILOUT(VisitForControl(stmt->cond(), body_exit, loop_successor));
    if (body_exit->HasPredecessor()) {
      body_exit->SetJoinId(stmt->BackEdgeId());
    } else {
      body_exit = NULL;
    }
    if (loop_successor->HasPredecessor()) {
      loop_successor->SetJoinId(stmt->ExitId());
    } else {
      loop_successor = NULL;
    }
  }
  HBasicBlock* loop_exit = CreateLoop(stmt,
                                      loop_entry,
                                      body_exit,
                                      loop_successor,
                                      break_info.break_block());
  set_current_block(loop_exit);
}


void HOptimizedGraphBuilder::VisitWhileStatement(WhileStatement* stmt) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  ASSERT(current_block() != NULL);
  bool osr_entry = PreProcessOsrEntry(stmt);
  HBasicBlock* loop_entry = CreateLoopHeaderBlock();
  current_block()->Goto(loop_entry);
  set_current_block(loop_entry);
  if (osr_entry) graph()->set_osr_loop_entry(loop_entry);


  // If the condition is constant true, do not generate a branch.
  HBasicBlock* loop_successor = NULL;
  if (!stmt->cond()->ToBooleanIsTrue()) {
    HBasicBlock* body_entry = graph()->CreateBasicBlock();
    loop_successor = graph()->CreateBasicBlock();
    CHECK_BAILOUT(VisitForControl(stmt->cond(), body_entry, loop_successor));
    if (body_entry->HasPredecessor()) {
      body_entry->SetJoinId(stmt->BodyId());
      set_current_block(body_entry);
    }
    if (loop_successor->HasPredecessor()) {
      loop_successor->SetJoinId(stmt->ExitId());
    } else {
      loop_successor = NULL;
    }
  }

  BreakAndContinueInfo break_info(stmt);
  if (current_block() != NULL) {
    CHECK_BAILOUT(VisitLoopBody(stmt, loop_entry, &break_info));
  }
  HBasicBlock* body_exit =
      JoinContinue(stmt, current_block(), break_info.continue_block());
  HBasicBlock* loop_exit = CreateLoop(stmt,
                                      loop_entry,
                                      body_exit,
                                      loop_successor,
                                      break_info.break_block());
  set_current_block(loop_exit);
}


void HOptimizedGraphBuilder::VisitForStatement(ForStatement* stmt) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  if (stmt->init() != NULL) {
    CHECK_ALIVE(Visit(stmt->init()));
  }
  ASSERT(current_block() != NULL);
  bool osr_entry = PreProcessOsrEntry(stmt);
  HBasicBlock* loop_entry = CreateLoopHeaderBlock();
  current_block()->Goto(loop_entry);
  set_current_block(loop_entry);
  if (osr_entry) graph()->set_osr_loop_entry(loop_entry);

  HBasicBlock* loop_successor = NULL;
  if (stmt->cond() != NULL) {
    HBasicBlock* body_entry = graph()->CreateBasicBlock();
    loop_successor = graph()->CreateBasicBlock();
    CHECK_BAILOUT(VisitForControl(stmt->cond(), body_entry, loop_successor));
    if (body_entry->HasPredecessor()) {
      body_entry->SetJoinId(stmt->BodyId());
      set_current_block(body_entry);
    }
    if (loop_successor->HasPredecessor()) {
      loop_successor->SetJoinId(stmt->ExitId());
    } else {
      loop_successor = NULL;
    }
  }

  BreakAndContinueInfo break_info(stmt);
  if (current_block() != NULL) {
    CHECK_BAILOUT(VisitLoopBody(stmt, loop_entry, &break_info));
  }
  HBasicBlock* body_exit =
      JoinContinue(stmt, current_block(), break_info.continue_block());

  if (stmt->next() != NULL && body_exit != NULL) {
    set_current_block(body_exit);
    CHECK_BAILOUT(Visit(stmt->next()));
    body_exit = current_block();
  }

  HBasicBlock* loop_exit = CreateLoop(stmt,
                                      loop_entry,
                                      body_exit,
                                      loop_successor,
                                      break_info.break_block());
  set_current_block(loop_exit);
}


void HOptimizedGraphBuilder::VisitForInStatement(ForInStatement* stmt) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());

  if (!FLAG_optimize_for_in) {
    return Bailout("ForInStatement optimization is disabled");
  }

  if (!oracle()->IsForInFastCase(stmt)) {
    return Bailout("ForInStatement is not fast case");
  }

  if (!stmt->each()->IsVariableProxy() ||
      !stmt->each()->AsVariableProxy()->var()->IsStackLocal()) {
    return Bailout("ForInStatement with non-local each variable");
  }

  Variable* each_var = stmt->each()->AsVariableProxy()->var();

  CHECK_ALIVE(VisitForValue(stmt->enumerable()));
  HValue* enumerable = Top();  // Leave enumerable at the top.

  HInstruction* map = AddInstruction(new(zone()) HForInPrepareMap(
      environment()->LookupContext(), enumerable));
  AddSimulate(stmt->PrepareId());

  HInstruction* array = AddInstruction(
      new(zone()) HForInCacheArray(
          enumerable,
          map,
          DescriptorArray::kEnumCacheBridgeCacheIndex));

  HInstruction* enum_length = AddInstruction(new(zone()) HMapEnumLength(map));

  HInstruction* start_index = AddInstruction(new(zone()) HConstant(
      Handle<Object>(Smi::FromInt(0), isolate()), Representation::Integer32()));

  Push(map);
  Push(array);
  Push(enum_length);
  Push(start_index);

  HInstruction* index_cache = AddInstruction(
      new(zone()) HForInCacheArray(
          enumerable,
          map,
          DescriptorArray::kEnumCacheBridgeIndicesCacheIndex));
  HForInCacheArray::cast(array)->set_index_cache(
      HForInCacheArray::cast(index_cache));

  bool osr_entry = PreProcessOsrEntry(stmt);
  HBasicBlock* loop_entry = CreateLoopHeaderBlock();
  current_block()->Goto(loop_entry);
  set_current_block(loop_entry);
  if (osr_entry) graph()->set_osr_loop_entry(loop_entry);

  HValue* index = environment()->ExpressionStackAt(0);
  HValue* limit = environment()->ExpressionStackAt(1);

  // Check that we still have more keys.
  HCompareIDAndBranch* compare_index =
      new(zone()) HCompareIDAndBranch(index, limit, Token::LT);
  compare_index->set_observed_input_representation(
      Representation::Integer32(), Representation::Integer32());

  HBasicBlock* loop_body = graph()->CreateBasicBlock();
  HBasicBlock* loop_successor = graph()->CreateBasicBlock();

  compare_index->SetSuccessorAt(0, loop_body);
  compare_index->SetSuccessorAt(1, loop_successor);
  current_block()->Finish(compare_index);

  set_current_block(loop_successor);
  Drop(5);

  set_current_block(loop_body);

  HValue* key = AddInstruction(
      new(zone()) HLoadKeyed(
          environment()->ExpressionStackAt(2),  // Enum cache.
          environment()->ExpressionStackAt(0),  // Iteration index.
          environment()->ExpressionStackAt(0),
          FAST_ELEMENTS));

  // Check if the expected map still matches that of the enumerable.
  // If not just deoptimize.
  AddInstruction(new(zone()) HCheckMapValue(
      environment()->ExpressionStackAt(4),
      environment()->ExpressionStackAt(3)));

  Bind(each_var, key);

  BreakAndContinueInfo break_info(stmt, 5);
  CHECK_BAILOUT(VisitLoopBody(stmt, loop_entry, &break_info));

  HBasicBlock* body_exit =
      JoinContinue(stmt, current_block(), break_info.continue_block());

  if (body_exit != NULL) {
    set_current_block(body_exit);

    HValue* current_index = Pop();
    HInstruction* new_index = HAdd::New(zone(),
                                        environment()->LookupContext(),
                                        current_index,
                                        graph()->GetConstant1());
    new_index->AssumeRepresentation(Representation::Integer32());
    PushAndAdd(new_index);
    body_exit = current_block();
  }

  HBasicBlock* loop_exit = CreateLoop(stmt,
                                      loop_entry,
                                      body_exit,
                                      loop_successor,
                                      break_info.break_block());

  set_current_block(loop_exit);
}


void HOptimizedGraphBuilder::VisitTryCatchStatement(TryCatchStatement* stmt) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  return Bailout("TryCatchStatement");
}


void HOptimizedGraphBuilder::VisitTryFinallyStatement(
    TryFinallyStatement* stmt) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  return Bailout("TryFinallyStatement");
}


void HOptimizedGraphBuilder::VisitDebuggerStatement(DebuggerStatement* stmt) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  return Bailout("DebuggerStatement");
}


static Handle<SharedFunctionInfo> SearchSharedFunctionInfo(
    Code* unoptimized_code, FunctionLiteral* expr) {
  int start_position = expr->start_position();
  RelocIterator it(unoptimized_code);
  for (;!it.done(); it.next()) {
    RelocInfo* rinfo = it.rinfo();
    if (rinfo->rmode() != RelocInfo::EMBEDDED_OBJECT) continue;
    Object* obj = rinfo->target_object();
    if (obj->IsSharedFunctionInfo()) {
      SharedFunctionInfo* shared = SharedFunctionInfo::cast(obj);
      if (shared->start_position() == start_position) {
        return Handle<SharedFunctionInfo>(shared);
      }
    }
  }

  return Handle<SharedFunctionInfo>();
}


void HOptimizedGraphBuilder::VisitFunctionLiteral(FunctionLiteral* expr) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  Handle<SharedFunctionInfo> shared_info =
      SearchSharedFunctionInfo(info()->shared_info()->code(),
                               expr);
  if (shared_info.is_null()) {
    shared_info = Compiler::BuildFunctionInfo(expr, info()->script());
  }
  // We also have a stack overflow if the recursive compilation did.
  if (HasStackOverflow()) return;
  HValue* context = environment()->LookupContext();
  HFunctionLiteral* instr =
      new(zone()) HFunctionLiteral(context, shared_info, expr->pretenure());
  return ast_context()->ReturnInstruction(instr, expr->id());
}


void HOptimizedGraphBuilder::VisitSharedFunctionInfoLiteral(
    SharedFunctionInfoLiteral* expr) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  return Bailout("SharedFunctionInfoLiteral");
}


void HOptimizedGraphBuilder::VisitConditional(Conditional* expr) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  HBasicBlock* cond_true = graph()->CreateBasicBlock();
  HBasicBlock* cond_false = graph()->CreateBasicBlock();
  CHECK_BAILOUT(VisitForControl(expr->condition(), cond_true, cond_false));

  // Visit the true and false subexpressions in the same AST context as the
  // whole expression.
  if (cond_true->HasPredecessor()) {
    cond_true->SetJoinId(expr->ThenId());
    set_current_block(cond_true);
    CHECK_BAILOUT(Visit(expr->then_expression()));
    cond_true = current_block();
  } else {
    cond_true = NULL;
  }

  if (cond_false->HasPredecessor()) {
    cond_false->SetJoinId(expr->ElseId());
    set_current_block(cond_false);
    CHECK_BAILOUT(Visit(expr->else_expression()));
    cond_false = current_block();
  } else {
    cond_false = NULL;
  }

  if (!ast_context()->IsTest()) {
    HBasicBlock* join = CreateJoin(cond_true, cond_false, expr->id());
    set_current_block(join);
    if (join != NULL && !ast_context()->IsEffect()) {
      return ast_context()->ReturnValue(Pop());
    }
  }
}


HOptimizedGraphBuilder::GlobalPropertyAccess
    HOptimizedGraphBuilder::LookupGlobalProperty(
        Variable* var, LookupResult* lookup, bool is_store) {
  if (var->is_this() || !info()->has_global_object()) {
    return kUseGeneric;
  }
  Handle<GlobalObject> global(info()->global_object());
  global->Lookup(*var->name(), lookup);
  if (!lookup->IsNormal() ||
      (is_store && lookup->IsReadOnly()) ||
      lookup->holder() != *global) {
    return kUseGeneric;
  }

  return kUseCell;
}


HValue* HOptimizedGraphBuilder::BuildContextChainWalk(Variable* var) {
  ASSERT(var->IsContextSlot());
  HValue* context = environment()->LookupContext();
  int length = info()->scope()->ContextChainLength(var->scope());
  while (length-- > 0) {
    HInstruction* context_instruction = new(zone()) HOuterContext(context);
    AddInstruction(context_instruction);
    context = context_instruction;
  }
  return context;
}


void HOptimizedGraphBuilder::VisitVariableProxy(VariableProxy* expr) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  Variable* variable = expr->var();
  switch (variable->location()) {
    case Variable::UNALLOCATED: {
      if (IsLexicalVariableMode(variable->mode())) {
        // TODO(rossberg): should this be an ASSERT?
        return Bailout("reference to global lexical variable");
      }
      // Handle known global constants like 'undefined' specially to avoid a
      // load from a global cell for them.
      Handle<Object> constant_value =
          isolate()->factory()->GlobalConstantFor(variable->name());
      if (!constant_value.is_null()) {
        HConstant* instr =
            new(zone()) HConstant(constant_value, Representation::Tagged());
        return ast_context()->ReturnInstruction(instr, expr->id());
      }

      LookupResult lookup(isolate());
      GlobalPropertyAccess type =
          LookupGlobalProperty(variable, &lookup, false);

      if (type == kUseCell &&
          info()->global_object()->IsAccessCheckNeeded()) {
        type = kUseGeneric;
      }

      if (type == kUseCell) {
        Handle<GlobalObject> global(info()->global_object());
        Handle<JSGlobalPropertyCell> cell(global->GetPropertyCell(&lookup));
        HLoadGlobalCell* instr =
            new(zone()) HLoadGlobalCell(cell, lookup.GetPropertyDetails());
        return ast_context()->ReturnInstruction(instr, expr->id());
      } else {
        HValue* context = environment()->LookupContext();
        HGlobalObject* global_object = new(zone()) HGlobalObject(context);
        AddInstruction(global_object);
        HLoadGlobalGeneric* instr =
            new(zone()) HLoadGlobalGeneric(context,
                                           global_object,
                                           variable->name(),
                                           ast_context()->is_for_typeof());
        instr->set_position(expr->position());
        return ast_context()->ReturnInstruction(instr, expr->id());
      }
    }

    case Variable::PARAMETER:
    case Variable::LOCAL: {
      HValue* value = environment()->Lookup(variable);
      if (value == graph()->GetConstantHole()) {
        ASSERT(IsDeclaredVariableMode(variable->mode()) &&
               variable->mode() != VAR);
        return Bailout("reference to uninitialized variable");
      }
      return ast_context()->ReturnValue(value);
    }

    case Variable::CONTEXT: {
      HValue* context = BuildContextChainWalk(variable);
      HLoadContextSlot* instr = new(zone()) HLoadContextSlot(context, variable);
      return ast_context()->ReturnInstruction(instr, expr->id());
    }

    case Variable::LOOKUP:
      return Bailout("reference to a variable which requires dynamic lookup");
  }
}


void HOptimizedGraphBuilder::VisitLiteral(Literal* expr) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  HConstant* instr =
      new(zone()) HConstant(expr->handle(), Representation::None());
  return ast_context()->ReturnInstruction(instr, expr->id());
}


void HOptimizedGraphBuilder::VisitRegExpLiteral(RegExpLiteral* expr) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  Handle<JSFunction> closure = function_state()->compilation_info()->closure();
  Handle<FixedArray> literals(closure->literals());
  HValue* context = environment()->LookupContext();

  HRegExpLiteral* instr = new(zone()) HRegExpLiteral(context,
                                                     literals,
                                                     expr->pattern(),
                                                     expr->flags(),
                                                     expr->literal_index());
  return ast_context()->ReturnInstruction(instr, expr->id());
}


static void LookupInPrototypes(Handle<Map> map,
                               Handle<String> name,
                               LookupResult* lookup) {
  while (map->prototype()->IsJSObject()) {
    Handle<JSObject> holder(JSObject::cast(map->prototype()));
    if (!holder->HasFastProperties()) break;
    map = Handle<Map>(holder->map());
    map->LookupDescriptor(*holder, *name, lookup);
    if (lookup->IsFound()) return;
  }
  lookup->NotFound();
}


// Tries to find a JavaScript accessor of the given name in the prototype chain
// starting at the given map. Return true iff there is one, including the
// corresponding AccessorPair plus its holder (which could be null when the
// accessor is found directly in the given map).
static bool LookupAccessorPair(Handle<Map> map,
                               Handle<String> name,
                               Handle<AccessorPair>* accessors,
                               Handle<JSObject>* holder) {
  Isolate* isolate = map->GetIsolate();
  LookupResult lookup(isolate);

  // Check for a JavaScript accessor directly in the map.
  map->LookupDescriptor(NULL, *name, &lookup);
  if (lookup.IsPropertyCallbacks()) {
    Handle<Object> callback(lookup.GetValueFromMap(*map), isolate);
    if (!callback->IsAccessorPair()) return false;
    *accessors = Handle<AccessorPair>::cast(callback);
    *holder = Handle<JSObject>();
    return true;
  }

  // Everything else, e.g. a field, can't be an accessor call.
  if (lookup.IsFound()) return false;

  // Check for a JavaScript accessor somewhere in the proto chain.
  LookupInPrototypes(map, name, &lookup);
  if (lookup.IsPropertyCallbacks()) {
    Handle<Object> callback(lookup.GetValue(), isolate);
    if (!callback->IsAccessorPair()) return false;
    *accessors = Handle<AccessorPair>::cast(callback);
    *holder = Handle<JSObject>(lookup.holder());
    return true;
  }

  // We haven't found a JavaScript accessor anywhere.
  return false;
}


static bool LookupGetter(Handle<Map> map,
                         Handle<String> name,
                         Handle<JSFunction>* getter,
                         Handle<JSObject>* holder) {
  Handle<AccessorPair> accessors;
  if (LookupAccessorPair(map, name, &accessors, holder) &&
      accessors->getter()->IsJSFunction()) {
    *getter = Handle<JSFunction>(JSFunction::cast(accessors->getter()));
    return true;
  }
  return false;
}


static bool LookupSetter(Handle<Map> map,
                         Handle<String> name,
                         Handle<JSFunction>* setter,
                         Handle<JSObject>* holder) {
  Handle<AccessorPair> accessors;
  if (LookupAccessorPair(map, name, &accessors, holder) &&
      accessors->setter()->IsJSFunction()) {
    *setter = Handle<JSFunction>(JSFunction::cast(accessors->setter()));
    return true;
  }
  return false;
}


// Determines whether the given array or object literal boilerplate satisfies
// all limits to be considered for fast deep-copying and computes the total
// size of all objects that are part of the graph.
static bool IsFastLiteral(Handle<JSObject> boilerplate,
                          int max_depth,
                          int* max_properties,
                          int* data_size,
                          int* pointer_size) {
  ASSERT(max_depth >= 0 && *max_properties >= 0);
  if (max_depth == 0) return false;

  Isolate* isolate = boilerplate->GetIsolate();
  Handle<FixedArrayBase> elements(boilerplate->elements());
  if (elements->length() > 0 &&
      elements->map() != isolate->heap()->fixed_cow_array_map()) {
    if (boilerplate->HasFastDoubleElements()) {
      *data_size += FixedDoubleArray::SizeFor(elements->length());
    } else if (boilerplate->HasFastObjectElements()) {
      Handle<FixedArray> fast_elements = Handle<FixedArray>::cast(elements);
      int length = elements->length();
      for (int i = 0; i < length; i++) {
        if ((*max_properties)-- == 0) return false;
        Handle<Object> value(fast_elements->get(i), isolate);
        if (value->IsJSObject()) {
          Handle<JSObject> value_object = Handle<JSObject>::cast(value);
          if (!IsFastLiteral(value_object,
                             max_depth - 1,
                             max_properties,
                             data_size,
                             pointer_size)) {
            return false;
          }
        }
      }
      *pointer_size += FixedArray::SizeFor(length);
    } else {
      return false;
    }
  }

  Handle<FixedArray> properties(boilerplate->properties());
  if (properties->length() > 0) {
    return false;
  } else {
    int nof = boilerplate->map()->inobject_properties();
    for (int i = 0; i < nof; i++) {
      if ((*max_properties)-- == 0) return false;
      Handle<Object> value(boilerplate->InObjectPropertyAt(i), isolate);
      if (value->IsJSObject()) {
        Handle<JSObject> value_object = Handle<JSObject>::cast(value);
        if (!IsFastLiteral(value_object,
                           max_depth - 1,
                           max_properties,
                           data_size,
                           pointer_size)) {
          return false;
        }
      }
    }
  }

  *pointer_size += boilerplate->map()->instance_size();
  return true;
}


void HOptimizedGraphBuilder::VisitObjectLiteral(ObjectLiteral* expr) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  Handle<JSFunction> closure = function_state()->compilation_info()->closure();
  HValue* context = environment()->LookupContext();
  HInstruction* literal;

  // Check whether to use fast or slow deep-copying for boilerplate.
  int data_size = 0;
  int pointer_size = 0;
  int max_properties = kMaxFastLiteralProperties;
  Handle<Object> original_boilerplate(closure->literals()->get(
      expr->literal_index()), isolate());
  if (original_boilerplate->IsJSObject() &&
      IsFastLiteral(Handle<JSObject>::cast(original_boilerplate),
                    kMaxFastLiteralDepth,
                    &max_properties,
                    &data_size,
                    &pointer_size)) {
    Handle<JSObject> original_boilerplate_object =
        Handle<JSObject>::cast(original_boilerplate);
    Handle<JSObject> boilerplate_object =
        DeepCopy(original_boilerplate_object);

    literal = BuildFastLiteral(context,
                               boilerplate_object,
                               original_boilerplate_object,
                               data_size,
                               pointer_size,
                               DONT_TRACK_ALLOCATION_SITE);
  } else {
    Handle<FixedArray> closure_literals(closure->literals(), isolate());
    literal = AddInstruction(
        new(zone()) HObjectLiteral(context,
                                   expr->constant_properties(),
                                   closure_literals,
                                   expr->fast_elements(),
                                   expr->literal_index(),
                                   expr->depth(),
                                   expr->has_function()));
  }

  // The object is expected in the bailout environment during computation
  // of the property values and is the value of the entire expression.
  Push(literal);

  expr->CalculateEmitStore(zone());

  for (int i = 0; i < expr->properties()->length(); i++) {
    ObjectLiteral::Property* property = expr->properties()->at(i);
    if (property->IsCompileTimeValue()) continue;

    Literal* key = property->key();
    Expression* value = property->value();

    switch (property->kind()) {
      case ObjectLiteral::Property::MATERIALIZED_LITERAL:
        ASSERT(!CompileTimeValue::IsCompileTimeValue(value));
        // Fall through.
      case ObjectLiteral::Property::COMPUTED:
        if (key->handle()->IsInternalizedString()) {
          if (property->emit_store()) {
            property->RecordTypeFeedback(oracle());
            CHECK_ALIVE(VisitForValue(value));
            HValue* value = Pop();
            Handle<Map> map = property->GetReceiverType();
            Handle<String> name = property->key()->AsPropertyName();
            HInstruction* store;
            if (map.is_null()) {
              // If we don't know the monomorphic type, do a generic store.
              CHECK_ALIVE(store = BuildStoreNamedGeneric(literal, name, value));
            } else {
#if DEBUG
              Handle<JSFunction> setter;
              Handle<JSObject> holder;
              ASSERT(!LookupSetter(map, name, &setter, &holder));
#endif
              CHECK_ALIVE(store = BuildStoreNamedMonomorphic(literal,
                                                             name,
                                                             value,
                                                             map));
            }
            AddInstruction(store);
            if (store->HasObservableSideEffects()) {
              AddSimulate(key->id(), REMOVABLE_SIMULATE);
            }
          } else {
            CHECK_ALIVE(VisitForEffect(value));
          }
          break;
        }
        // Fall through.
      case ObjectLiteral::Property::PROTOTYPE:
      case ObjectLiteral::Property::SETTER:
      case ObjectLiteral::Property::GETTER:
        return Bailout("Object literal with complex property");
      default: UNREACHABLE();
    }
  }

  if (expr->has_function()) {
    // Return the result of the transformation to fast properties
    // instead of the original since this operation changes the map
    // of the object. This makes sure that the original object won't
    // be used by other optimized code before it is transformed
    // (e.g. because of code motion).
    HToFastProperties* result = new(zone()) HToFastProperties(Pop());
    AddInstruction(result);
    return ast_context()->ReturnValue(result);
  } else {
    return ast_context()->ReturnValue(Pop());
  }
}


void HOptimizedGraphBuilder::VisitArrayLiteral(ArrayLiteral* expr) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  ZoneList<Expression*>* subexprs = expr->values();
  int length = subexprs->length();
  HValue* context = environment()->LookupContext();
  HInstruction* literal;

  Handle<FixedArray> literals(environment()->closure()->literals(), isolate());
  Handle<Object> raw_boilerplate(literals->get(expr->literal_index()),
                                 isolate());

  if (raw_boilerplate->IsUndefined()) {
    raw_boilerplate = Runtime::CreateArrayLiteralBoilerplate(
        isolate(), literals, expr->constant_elements());
    if (raw_boilerplate.is_null()) {
      return Bailout("array boilerplate creation failed");
    }
    literals->set(expr->literal_index(), *raw_boilerplate);
    if (JSObject::cast(*raw_boilerplate)->elements()->map() ==
        isolate()->heap()->fixed_cow_array_map()) {
      isolate()->counters()->cow_arrays_created_runtime()->Increment();
    }
  }

  Handle<JSObject> original_boilerplate_object =
      Handle<JSObject>::cast(raw_boilerplate);
  ElementsKind boilerplate_elements_kind =
      Handle<JSObject>::cast(original_boilerplate_object)->GetElementsKind();

  // TODO(mvstanton): This heuristic is only a temporary solution.  In the
  // end, we want to quit creating allocation site info after a certain number
  // of GCs for a call site.
  AllocationSiteMode mode = AllocationSiteInfo::GetMode(
      boilerplate_elements_kind);

  // Check whether to use fast or slow deep-copying for boilerplate.
  int data_size = 0;
  int pointer_size = 0;
  int max_properties = kMaxFastLiteralProperties;
  if (IsFastLiteral(original_boilerplate_object,
                    kMaxFastLiteralDepth,
                    &max_properties,
                    &data_size,
                    &pointer_size)) {
    if (mode == TRACK_ALLOCATION_SITE) {
      pointer_size += AllocationSiteInfo::kSize;
    }

    Handle<JSObject> boilerplate_object = DeepCopy(original_boilerplate_object);
    literal = BuildFastLiteral(context,
                               boilerplate_object,
                               original_boilerplate_object,
                               data_size,
                               pointer_size,
                               mode);
  } else {
    literal = AddInstruction(
                  new(zone()) HArrayLiteral(context,
                                            original_boilerplate_object,
                                            literals,
                                            length,
                                            expr->literal_index(),
                                            expr->depth(),
                                            mode));
  }

  // The array is expected in the bailout environment during computation
  // of the property values and is the value of the entire expression.
  Push(literal);

  HLoadElements* elements = NULL;

  for (int i = 0; i < length; i++) {
    Expression* subexpr = subexprs->at(i);
    // If the subexpression is a literal or a simple materialized literal it
    // is already set in the cloned array.
    if (CompileTimeValue::IsCompileTimeValue(subexpr)) continue;

    CHECK_ALIVE(VisitForValue(subexpr));
    HValue* value = Pop();
    if (!Smi::IsValid(i)) return Bailout("Non-smi key in array literal");

    // Pass in literal as dummy depedency, since  the receiver always has
    // elements.
    elements = new(zone()) HLoadElements(literal, literal);
    AddInstruction(elements);

    HValue* key = AddInstruction(
        new(zone()) HConstant(Handle<Object>(Smi::FromInt(i), isolate()),
                              Representation::Integer32()));

    switch (boilerplate_elements_kind) {
      case FAST_SMI_ELEMENTS:
      case FAST_HOLEY_SMI_ELEMENTS:
        // Smi-only arrays need a smi check.
        AddInstruction(new(zone()) HCheckSmi(value));
        // Fall through.
      case FAST_ELEMENTS:
      case FAST_HOLEY_ELEMENTS:
      case FAST_DOUBLE_ELEMENTS:
      case FAST_HOLEY_DOUBLE_ELEMENTS:
        AddInstruction(new(zone()) HStoreKeyed(
            elements,
            key,
            value,
            boilerplate_elements_kind));
        break;
      default:
        UNREACHABLE();
        break;
    }

    AddSimulate(expr->GetIdForElement(i));
  }
  return ast_context()->ReturnValue(Pop());
}


// Sets the lookup result and returns true if the load/store can be inlined.
static bool ComputeLoadStoreField(Handle<Map> type,
                                  Handle<String> name,
                                  LookupResult* lookup,
                                  bool is_store) {
  if (type->has_named_interceptor()) {
    lookup->InterceptorResult(NULL);
    return false;
  }
  // If we directly find a field, the access can be inlined.
  type->LookupDescriptor(NULL, *name, lookup);
  if (lookup->IsField()) return true;

  // For a load, we are out of luck if there is no such field.
  if (!is_store) return false;

  // 2nd chance: A store into a non-existent field can still be inlined if we
  // have a matching transition and some room left in the object.
  type->LookupTransition(NULL, *name, lookup);
  return lookup->IsTransitionToField(*type) &&
      (type->unused_property_fields() > 0);
}


static int ComputeLoadStoreFieldIndex(Handle<Map> type,
                                      LookupResult* lookup) {
  ASSERT(lookup->IsField() || lookup->IsTransitionToField(*type));
  if (lookup->IsField()) {
    return lookup->GetLocalFieldIndexFromMap(*type);
  } else {
    Map* transition = lookup->GetTransitionMapFromMap(*type);
    int descriptor = transition->LastAdded();
    int index = transition->instance_descriptors()->GetFieldIndex(descriptor);
    return index - type->inobject_properties();
  }
}


static Representation ComputeLoadStoreRepresentation(Handle<Map> type,
                                                     LookupResult* lookup) {
  if (lookup->IsField()) {
    return lookup->representation();
  } else {
    Map* transition = lookup->GetTransitionMapFromMap(*type);
    int descriptor = transition->LastAdded();
    PropertyDetails details =
        transition->instance_descriptors()->GetDetails(descriptor);
    return details.representation();
  }
}


void HOptimizedGraphBuilder::AddCheckMap(HValue* object, Handle<Map> map) {
  AddInstruction(new(zone()) HCheckNonSmi(object));
  AddInstruction(HCheckMaps::New(object, map, zone()));
}


void HOptimizedGraphBuilder::AddCheckMapsWithTransitions(HValue* object,
                                                         Handle<Map> map) {
  AddInstruction(new(zone()) HCheckNonSmi(object));
  AddInstruction(HCheckMaps::NewWithTransitions(object, map, zone()));
}


HInstruction* HOptimizedGraphBuilder::BuildStoreNamedField(
    HValue* object,
    Handle<String> name,
    HValue* value,
    Handle<Map> map,
    LookupResult* lookup) {
  ASSERT(lookup->IsFound());
  // If the property does not exist yet, we have to check that it wasn't made
  // readonly or turned into a setter by some meanwhile modifications on the
  // prototype chain.
  if (!lookup->IsProperty() && map->prototype()->IsJSReceiver()) {
    Object* proto = map->prototype();
    // First check that the prototype chain isn't affected already.
    LookupResult proto_result(isolate());
    proto->Lookup(*name, &proto_result);
    if (proto_result.IsProperty()) {
      // If the inherited property could induce readonly-ness, bail out.
      if (proto_result.IsReadOnly() || !proto_result.IsCacheable()) {
        Bailout("improper object on prototype chain for store");
        return NULL;
      }
      // We only need to check up to the preexisting property.
      proto = proto_result.holder();
    } else {
      // Otherwise, find the top prototype.
      while (proto->GetPrototype(isolate())->IsJSObject()) {
        proto = proto->GetPrototype(isolate());
      }
      ASSERT(proto->GetPrototype(isolate())->IsNull());
    }
    ASSERT(proto->IsJSObject());
    AddInstruction(new(zone()) HCheckPrototypeMaps(
        Handle<JSObject>(JSObject::cast(map->prototype())),
        Handle<JSObject>(JSObject::cast(proto)),
        zone()));
  }

  int index = ComputeLoadStoreFieldIndex(map, lookup);
  bool is_in_object = index < 0;
  Representation representation = ComputeLoadStoreRepresentation(map, lookup);
  int offset = index * kPointerSize;
  if (index < 0) {
    // Negative property indices are in-object properties, indexed
    // from the end of the fixed part of the object.
    offset += map->instance_size();
  } else {
    offset += FixedArray::kHeaderSize;
  }
  HStoreNamedField* instr = new(zone()) HStoreNamedField(
      object, name, value, is_in_object, representation, offset);
  if (lookup->IsTransitionToField(*map)) {
    Handle<Map> transition(lookup->GetTransitionMapFromMap(*map));
    instr->set_transition(transition);
    // TODO(fschneider): Record the new map type of the object in the IR to
    // enable elimination of redundant checks after the transition store.
    instr->SetGVNFlag(kChangesMaps);
  }
  return instr;
}


HInstruction* HOptimizedGraphBuilder::BuildStoreNamedGeneric(
    HValue* object,
    Handle<String> name,
    HValue* value) {
  HValue* context = environment()->LookupContext();
  return new(zone()) HStoreNamedGeneric(
                         context,
                         object,
                         name,
                         value,
                         function_strict_mode_flag());
}


HInstruction* HOptimizedGraphBuilder::BuildCallSetter(
    HValue* object,
    HValue* value,
    Handle<Map> map,
    Handle<JSFunction> setter,
    Handle<JSObject> holder) {
  AddCheckConstantFunction(holder, object, map);
  AddInstruction(new(zone()) HPushArgument(object));
  AddInstruction(new(zone()) HPushArgument(value));
  return new(zone()) HCallConstantFunction(setter, 2);
}


HInstruction* HOptimizedGraphBuilder::BuildStoreNamedMonomorphic(
    HValue* object,
    Handle<String> name,
    HValue* value,
    Handle<Map> map) {
  // Handle a store to a known field.
  LookupResult lookup(isolate());
  if (ComputeLoadStoreField(map, name, &lookup, true)) {
    AddCheckMapsWithTransitions(object, map);
    return BuildStoreNamedField(object, name, value, map, &lookup);
  }

  // No luck, do a generic store.
  return BuildStoreNamedGeneric(object, name, value);
}


bool HOptimizedGraphBuilder::HandlePolymorphicArrayLengthLoad(
    Property* expr,
    HValue* object,
    SmallMapList* types,
    Handle<String> name) {
  if (!name->Equals(isolate()->heap()->length_string())) return false;

  for (int i = 0; i < types->length(); i++) {
    if (types->at(i)->instance_type() != JS_ARRAY_TYPE) return false;
  }

  AddInstruction(new(zone()) HCheckNonSmi(object));

  HInstruction* typecheck =
    AddInstruction(HCheckMaps::New(object, types, zone()));
  HInstruction* instr =
    HLoadNamedField::NewArrayLength(zone(), object, typecheck);
  instr->set_position(expr->position());
  ast_context()->ReturnInstruction(instr, expr->id());
  return true;
}


void HOptimizedGraphBuilder::HandlePolymorphicLoadNamedField(Property* expr,
    HValue* object,
    SmallMapList* types,
    Handle<String> name) {
  int count = 0;
  int previous_field_offset = 0;
  bool previous_field_is_in_object = false;
  bool is_monomorphic_field = true;

  if (HandlePolymorphicArrayLengthLoad(expr, object, types, name))
    return;

  Handle<Map> map;
  LookupResult lookup(isolate());
  for (int i = 0; i < types->length() && count < kMaxLoadPolymorphism; ++i) {
    map = types->at(i);
    if (ComputeLoadStoreField(map, name, &lookup, false)) {
      int index = ComputeLoadStoreFieldIndex(map, &lookup);
      bool is_in_object = index < 0;
      int offset = index * kPointerSize;
      if (index < 0) {
        // Negative property indices are in-object properties, indexed
        // from the end of the fixed part of the object.
        offset += map->instance_size();
      } else {
        offset += FixedArray::kHeaderSize;
      }
      if (count == 0) {
        previous_field_offset = offset;
        previous_field_is_in_object = is_in_object;
      } else if (is_monomorphic_field) {
        is_monomorphic_field = (offset == previous_field_offset) &&
                               (is_in_object == previous_field_is_in_object);
      }
      ++count;
    }
  }

  // Use monomorphic load if property lookup results in the same field index
  // for all maps.  Requires special map check on the set of all handled maps.
  AddInstruction(new(zone()) HCheckNonSmi(object));
  HInstruction* instr;
  if (count == types->length() && is_monomorphic_field) {
    AddInstruction(HCheckMaps::New(object, types, zone()));
    instr = BuildLoadNamedField(object, map, &lookup);
  } else {
    HValue* context = environment()->LookupContext();
    instr = new(zone()) HLoadNamedFieldPolymorphic(context,
                                                   object,
                                                   types,
                                                   name,
                                                   zone());
  }

  instr->set_position(expr->position());
  return ast_context()->ReturnInstruction(instr, expr->id());
}


void HOptimizedGraphBuilder::HandlePolymorphicStoreNamedField(
    Assignment* expr,
    HValue* object,
    HValue* value,
    SmallMapList* types,
    Handle<String> name) {
  // TODO(ager): We should recognize when the prototype chains for different
  // maps are identical. In that case we can avoid repeatedly generating the
  // same prototype map checks.
  int count = 0;
  HBasicBlock* join = NULL;
  for (int i = 0; i < types->length() && count < kMaxStorePolymorphism; ++i) {
    Handle<Map> map = types->at(i);
    LookupResult lookup(isolate());
    if (ComputeLoadStoreField(map, name, &lookup, true)) {
      if (count == 0) {
        AddInstruction(new(zone()) HCheckNonSmi(object));  // Only needed once.
        join = graph()->CreateBasicBlock();
      }
      ++count;
      HBasicBlock* if_true = graph()->CreateBasicBlock();
      HBasicBlock* if_false = graph()->CreateBasicBlock();
      HCompareMap* compare =
          new(zone()) HCompareMap(object, map, if_true, if_false);
      current_block()->Finish(compare);

      set_current_block(if_true);
      HInstruction* instr;
      CHECK_ALIVE(instr =
          BuildStoreNamedField(object, name, value, map, &lookup));
      instr->set_position(expr->position());
      // Goto will add the HSimulate for the store.
      AddInstruction(instr);
      if (!ast_context()->IsEffect()) Push(value);
      current_block()->Goto(join);

      set_current_block(if_false);
    }
  }

  // Finish up.  Unconditionally deoptimize if we've handled all the maps we
  // know about and do not want to handle ones we've never seen.  Otherwise
  // use a generic IC.
  if (count == types->length() && FLAG_deoptimize_uncommon_cases) {
    current_block()->FinishExitWithDeoptimization(HDeoptimize::kNoUses);
  } else {
    HInstruction* instr = BuildStoreNamedGeneric(object, name, value);
    instr->set_position(expr->position());
    AddInstruction(instr);

    if (join != NULL) {
      if (!ast_context()->IsEffect()) Push(value);
      current_block()->Goto(join);
    } else {
      // The HSimulate for the store should not see the stored value in
      // effect contexts (it is not materialized at expr->id() in the
      // unoptimized code).
      if (instr->HasObservableSideEffects()) {
        if (ast_context()->IsEffect()) {
          AddSimulate(expr->id(), REMOVABLE_SIMULATE);
        } else {
          Push(value);
          AddSimulate(expr->id(), REMOVABLE_SIMULATE);
          Drop(1);
        }
      }
      return ast_context()->ReturnValue(value);
    }
  }

  ASSERT(join != NULL);
  join->SetJoinId(expr->id());
  set_current_block(join);
  if (!ast_context()->IsEffect()) return ast_context()->ReturnValue(Pop());
}


void HOptimizedGraphBuilder::HandlePropertyAssignment(Assignment* expr) {
  Property* prop = expr->target()->AsProperty();
  ASSERT(prop != NULL);
  expr->RecordTypeFeedback(oracle(), zone());
  CHECK_ALIVE(VisitForValue(prop->obj()));

  if (prop->key()->IsPropertyName()) {
    // Named store.
    CHECK_ALIVE(VisitForValue(expr->value()));
    HValue* value = environment()->ExpressionStackAt(0);
    HValue* object = environment()->ExpressionStackAt(1);

    Literal* key = prop->key()->AsLiteral();
    Handle<String> name = Handle<String>::cast(key->handle());
    ASSERT(!name.is_null());

    HInstruction* instr = NULL;
    SmallMapList* types = expr->GetReceiverTypes();
    bool monomorphic = expr->IsMonomorphic();
    Handle<Map> map;
    if (monomorphic) {
      map = types->first();
      if (map->is_dictionary_map()) monomorphic = false;
    }
    if (monomorphic) {
      Handle<JSFunction> setter;
      Handle<JSObject> holder;
      if (LookupSetter(map, name, &setter, &holder)) {
        AddCheckConstantFunction(holder, object, map);
        if (FLAG_inline_accessors && TryInlineSetter(setter, expr, value)) {
          return;
        }
        Drop(2);
        AddInstruction(new(zone()) HPushArgument(object));
        AddInstruction(new(zone()) HPushArgument(value));
        instr = new(zone()) HCallConstantFunction(setter, 2);
      } else {
        Drop(2);
        CHECK_ALIVE(instr = BuildStoreNamedMonomorphic(object,
                                                       name,
                                                       value,
                                                       map));
      }

    } else if (types != NULL && types->length() > 1) {
      Drop(2);
      return HandlePolymorphicStoreNamedField(expr, object, value, types, name);
    } else {
      Drop(2);
      instr = BuildStoreNamedGeneric(object, name, value);
    }

    Push(value);
    instr->set_position(expr->position());
    AddInstruction(instr);
    if (instr->HasObservableSideEffects()) {
      AddSimulate(expr->AssignmentId(), REMOVABLE_SIMULATE);
    }
    return ast_context()->ReturnValue(Pop());

  } else {
    // Keyed store.
    CHECK_ALIVE(VisitForValue(prop->key()));
    CHECK_ALIVE(VisitForValue(expr->value()));
    HValue* value = environment()->ExpressionStackAt(0);
    HValue* key = environment()->ExpressionStackAt(1);
    HValue* object = environment()->ExpressionStackAt(2);
    bool has_side_effects = false;
    HandleKeyedElementAccess(object, key, value, expr, expr->AssignmentId(),
                             expr->position(),
                             true,  // is_store
                             &has_side_effects);
    Drop(3);
    Push(value);
    AddSimulate(expr->AssignmentId(), REMOVABLE_SIMULATE);
    return ast_context()->ReturnValue(Pop());
  }
}


// Because not every expression has a position and there is not common
// superclass of Assignment and CountOperation, we cannot just pass the
// owning expression instead of position and ast_id separately.
void HOptimizedGraphBuilder::HandleGlobalVariableAssignment(
    Variable* var,
    HValue* value,
    int position,
    BailoutId ast_id) {
  LookupResult lookup(isolate());
  GlobalPropertyAccess type = LookupGlobalProperty(var, &lookup, true);
  if (type == kUseCell) {
    Handle<GlobalObject> global(info()->global_object());
    Handle<JSGlobalPropertyCell> cell(global->GetPropertyCell(&lookup));
    HInstruction* instr =
        new(zone()) HStoreGlobalCell(value, cell, lookup.GetPropertyDetails());
    instr->set_position(position);
    AddInstruction(instr);
    if (instr->HasObservableSideEffects()) {
      AddSimulate(ast_id, REMOVABLE_SIMULATE);
    }
  } else {
    HValue* context =  environment()->LookupContext();
    HGlobalObject* global_object = new(zone()) HGlobalObject(context);
    AddInstruction(global_object);
    HStoreGlobalGeneric* instr =
        new(zone()) HStoreGlobalGeneric(context,
                                        global_object,
                                        var->name(),
                                        value,
                                        function_strict_mode_flag());
    instr->set_position(position);
    AddInstruction(instr);
    ASSERT(instr->HasObservableSideEffects());
    AddSimulate(ast_id, REMOVABLE_SIMULATE);
  }
}


void HOptimizedGraphBuilder::HandleCompoundAssignment(Assignment* expr) {
  Expression* target = expr->target();
  VariableProxy* proxy = target->AsVariableProxy();
  Property* prop = target->AsProperty();
  ASSERT(proxy == NULL || prop == NULL);

  // We have a second position recorded in the FullCodeGenerator to have
  // type feedback for the binary operation.
  BinaryOperation* operation = expr->binary_operation();

  if (proxy != NULL) {
    Variable* var = proxy->var();
    if (var->mode() == LET)  {
      return Bailout("unsupported let compound assignment");
    }

    CHECK_ALIVE(VisitForValue(operation));

    switch (var->location()) {
      case Variable::UNALLOCATED:
        HandleGlobalVariableAssignment(var,
                                       Top(),
                                       expr->position(),
                                       expr->AssignmentId());
        break;

      case Variable::PARAMETER:
      case Variable::LOCAL:
        if (var->mode() == CONST)  {
          return Bailout("unsupported const compound assignment");
        }
        Bind(var, Top());
        break;

      case Variable::CONTEXT: {
        // Bail out if we try to mutate a parameter value in a function
        // using the arguments object.  We do not (yet) correctly handle the
        // arguments property of the function.
        if (info()->scope()->arguments() != NULL) {
          // Parameters will be allocated to context slots.  We have no
          // direct way to detect that the variable is a parameter so we do
          // a linear search of the parameter variables.
          int count = info()->scope()->num_parameters();
          for (int i = 0; i < count; ++i) {
            if (var == info()->scope()->parameter(i)) {
              Bailout(
                  "assignment to parameter, function uses arguments object");
            }
          }
        }

        HStoreContextSlot::Mode mode;

        switch (var->mode()) {
          case LET:
            mode = HStoreContextSlot::kCheckDeoptimize;
            break;
          case CONST:
            return ast_context()->ReturnValue(Pop());
          case CONST_HARMONY:
            // This case is checked statically so no need to
            // perform checks here
            UNREACHABLE();
          default:
            mode = HStoreContextSlot::kNoCheck;
        }

        HValue* context = BuildContextChainWalk(var);
        HStoreContextSlot* instr =
            new(zone()) HStoreContextSlot(context, var->index(), mode, Top());
        AddInstruction(instr);
        if (instr->HasObservableSideEffects()) {
          AddSimulate(expr->AssignmentId(), REMOVABLE_SIMULATE);
        }
        break;
      }

      case Variable::LOOKUP:
        return Bailout("compound assignment to lookup slot");
    }
    return ast_context()->ReturnValue(Pop());

  } else if (prop != NULL) {
    prop->RecordTypeFeedback(oracle(), zone());

    if (prop->key()->IsPropertyName()) {
      // Named property.
      CHECK_ALIVE(VisitForValue(prop->obj()));
      HValue* object = Top();

      Handle<String> name = prop->key()->AsLiteral()->AsPropertyName();
      Handle<Map> map;
      HInstruction* load;
      bool monomorphic = prop->IsMonomorphic();
      if (monomorphic) {
        map = prop->GetReceiverTypes()->first();
        // We can't generate code for a monomorphic dict mode load so
        // just pretend it is not monomorphic.
        if (map->is_dictionary_map()) monomorphic = false;
      }
      if (monomorphic) {
        Handle<JSFunction> getter;
        Handle<JSObject> holder;
        if (LookupGetter(map, name, &getter, &holder)) {
          load = BuildCallGetter(object, map, getter, holder);
        } else {
          load = BuildLoadNamedMonomorphic(object, name, prop, map);
        }
      } else {
        load = BuildLoadNamedGeneric(object, name, prop);
      }
      PushAndAdd(load);
      if (load->HasObservableSideEffects()) {
        AddSimulate(prop->LoadId(), REMOVABLE_SIMULATE);
      }

      CHECK_ALIVE(VisitForValue(expr->value()));
      HValue* right = Pop();
      HValue* left = Pop();

      HInstruction* instr = BuildBinaryOperation(operation, left, right);
      PushAndAdd(instr);
      if (instr->HasObservableSideEffects()) {
        AddSimulate(operation->id(), REMOVABLE_SIMULATE);
      }

      HInstruction* store;
      if (!monomorphic || map->is_observed()) {
        // If we don't know the monomorphic type, do a generic store.
        CHECK_ALIVE(store = BuildStoreNamedGeneric(object, name, instr));
      } else {
        Handle<JSFunction> setter;
        Handle<JSObject> holder;
        if (LookupSetter(map, name, &setter, &holder)) {
          store = BuildCallSetter(object, instr, map, setter, holder);
        } else {
          CHECK_ALIVE(store = BuildStoreNamedMonomorphic(object,
                                                         name,
                                                         instr,
                                                         map));
        }
      }
      AddInstruction(store);
      // Drop the simulated receiver and value.  Return the value.
      Drop(2);
      Push(instr);
      if (store->HasObservableSideEffects()) {
        AddSimulate(expr->AssignmentId(), REMOVABLE_SIMULATE);
      }
      return ast_context()->ReturnValue(Pop());

    } else {
      // Keyed property.
      CHECK_ALIVE(VisitForValue(prop->obj()));
      CHECK_ALIVE(VisitForValue(prop->key()));
      HValue* obj = environment()->ExpressionStackAt(1);
      HValue* key = environment()->ExpressionStackAt(0);

      bool has_side_effects = false;
      HValue* load = HandleKeyedElementAccess(
          obj, key, NULL, prop, prop->LoadId(), RelocInfo::kNoPosition,
          false,  // is_store
          &has_side_effects);
      Push(load);
      if (has_side_effects) AddSimulate(prop->LoadId(), REMOVABLE_SIMULATE);


      CHECK_ALIVE(VisitForValue(expr->value()));
      HValue* right = Pop();
      HValue* left = Pop();

      HInstruction* instr = BuildBinaryOperation(operation, left, right);
      PushAndAdd(instr);
      if (instr->HasObservableSideEffects()) {
        AddSimulate(operation->id(), REMOVABLE_SIMULATE);
      }

      expr->RecordTypeFeedback(oracle(), zone());
      HandleKeyedElementAccess(obj, key, instr, expr, expr->AssignmentId(),
                               RelocInfo::kNoPosition,
                               true,  // is_store
                               &has_side_effects);

      // Drop the simulated receiver, key, and value.  Return the value.
      Drop(3);
      Push(instr);
      ASSERT(has_side_effects);  // Stores always have side effects.
      AddSimulate(expr->AssignmentId(), REMOVABLE_SIMULATE);
      return ast_context()->ReturnValue(Pop());
    }

  } else {
    return Bailout("invalid lhs in compound assignment");
  }
}


void HOptimizedGraphBuilder::VisitAssignment(Assignment* expr) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  VariableProxy* proxy = expr->target()->AsVariableProxy();
  Property* prop = expr->target()->AsProperty();
  ASSERT(proxy == NULL || prop == NULL);

  if (expr->is_compound()) {
    HandleCompoundAssignment(expr);
    return;
  }

  if (prop != NULL) {
    HandlePropertyAssignment(expr);
  } else if (proxy != NULL) {
    Variable* var = proxy->var();

    if (var->mode() == CONST) {
      if (expr->op() != Token::INIT_CONST) {
        CHECK_ALIVE(VisitForValue(expr->value()));
        return ast_context()->ReturnValue(Pop());
      }

      if (var->IsStackAllocated()) {
        // We insert a use of the old value to detect unsupported uses of const
        // variables (e.g. initialization inside a loop).
        HValue* old_value = environment()->Lookup(var);
        AddInstruction(new(zone()) HUseConst(old_value));
      }
    } else if (var->mode() == CONST_HARMONY) {
      if (expr->op() != Token::INIT_CONST_HARMONY) {
        return Bailout("non-initializer assignment to const");
      }
    }

    if (proxy->IsArguments()) return Bailout("assignment to arguments");

    // Handle the assignment.
    switch (var->location()) {
      case Variable::UNALLOCATED:
        CHECK_ALIVE(VisitForValue(expr->value()));
        HandleGlobalVariableAssignment(var,
                                       Top(),
                                       expr->position(),
                                       expr->AssignmentId());
        return ast_context()->ReturnValue(Pop());

      case Variable::PARAMETER:
      case Variable::LOCAL: {
        // Perform an initialization check for let declared variables
        // or parameters.
        if (var->mode() == LET && expr->op() == Token::ASSIGN) {
          HValue* env_value = environment()->Lookup(var);
          if (env_value == graph()->GetConstantHole()) {
            return Bailout("assignment to let variable before initialization");
          }
        }
        // We do not allow the arguments object to occur in a context where it
        // may escape, but assignments to stack-allocated locals are
        // permitted.
        CHECK_ALIVE(VisitForValue(expr->value(), ARGUMENTS_ALLOWED));
        HValue* value = Pop();
        Bind(var, value);
        return ast_context()->ReturnValue(value);
      }

      case Variable::CONTEXT: {
        // Bail out if we try to mutate a parameter value in a function using
        // the arguments object.  We do not (yet) correctly handle the
        // arguments property of the function.
        if (info()->scope()->arguments() != NULL) {
          // Parameters will rewrite to context slots.  We have no direct way
          // to detect that the variable is a parameter.
          int count = info()->scope()->num_parameters();
          for (int i = 0; i < count; ++i) {
            if (var == info()->scope()->parameter(i)) {
              return Bailout("assignment to parameter in arguments object");
            }
          }
        }

        CHECK_ALIVE(VisitForValue(expr->value()));
        HStoreContextSlot::Mode mode;
        if (expr->op() == Token::ASSIGN) {
          switch (var->mode()) {
            case LET:
              mode = HStoreContextSlot::kCheckDeoptimize;
              break;
            case CONST:
              return ast_context()->ReturnValue(Pop());
            case CONST_HARMONY:
              // This case is checked statically so no need to
              // perform checks here
              UNREACHABLE();
            default:
              mode = HStoreContextSlot::kNoCheck;
          }
        } else if (expr->op() == Token::INIT_VAR ||
                   expr->op() == Token::INIT_LET ||
                   expr->op() == Token::INIT_CONST_HARMONY) {
          mode = HStoreContextSlot::kNoCheck;
        } else {
          ASSERT(expr->op() == Token::INIT_CONST);

          mode = HStoreContextSlot::kCheckIgnoreAssignment;
        }

        HValue* context = BuildContextChainWalk(var);
        HStoreContextSlot* instr = new(zone()) HStoreContextSlot(
            context, var->index(), mode, Top());
        AddInstruction(instr);
        if (instr->HasObservableSideEffects()) {
          AddSimulate(expr->AssignmentId(), REMOVABLE_SIMULATE);
        }
        return ast_context()->ReturnValue(Pop());
      }

      case Variable::LOOKUP:
        return Bailout("assignment to LOOKUP variable");
    }
  } else {
    return Bailout("invalid left-hand side in assignment");
  }
}


void HOptimizedGraphBuilder::VisitYield(Yield* expr) {
  // Generators are not optimized, so we should never get here.
  UNREACHABLE();
}


void HOptimizedGraphBuilder::VisitThrow(Throw* expr) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  // We don't optimize functions with invalid left-hand sides in
  // assignments, count operations, or for-in.  Consequently throw can
  // currently only occur in an effect context.
  ASSERT(ast_context()->IsEffect());
  CHECK_ALIVE(VisitForValue(expr->exception()));

  HValue* context = environment()->LookupContext();
  HValue* value = environment()->Pop();
  HThrow* instr = new(zone()) HThrow(context, value);
  instr->set_position(expr->position());
  AddInstruction(instr);
  AddSimulate(expr->id());
  current_block()->FinishExit(new(zone()) HAbnormalExit);
  set_current_block(NULL);
}


HLoadNamedField* HOptimizedGraphBuilder::BuildLoadNamedField(
    HValue* object,
    Handle<Map> map,
    LookupResult* lookup) {
  Representation representation = lookup->representation();
  int index = lookup->GetLocalFieldIndexFromMap(*map);
  if (index < 0) {
    // Negative property indices are in-object properties, indexed
    // from the end of the fixed part of the object.
    int offset = (index * kPointerSize) + map->instance_size();
    return new(zone()) HLoadNamedField(object, true, representation, offset);
  } else {
    // Non-negative property indices are in the properties array.
    int offset = (index * kPointerSize) + FixedArray::kHeaderSize;
    return new(zone()) HLoadNamedField(object, false, representation, offset);
  }
}


HInstruction* HOptimizedGraphBuilder::BuildLoadNamedGeneric(
    HValue* object,
    Handle<String> name,
    Property* expr) {
  if (expr->IsUninitialized()) {
    AddSoftDeoptimize();
  }
  HValue* context = environment()->LookupContext();
  return new(zone()) HLoadNamedGeneric(context, object, name);
}


HInstruction* HOptimizedGraphBuilder::BuildCallGetter(
    HValue* object,
    Handle<Map> map,
    Handle<JSFunction> getter,
    Handle<JSObject> holder) {
  AddCheckConstantFunction(holder, object, map);
  AddInstruction(new(zone()) HPushArgument(object));
  return new(zone()) HCallConstantFunction(getter, 1);
}


HInstruction* HOptimizedGraphBuilder::BuildLoadNamedMonomorphic(
    HValue* object,
    Handle<String> name,
    Property* expr,
    Handle<Map> map) {
  // Handle a load from a known field.
  ASSERT(!map->is_dictionary_map());

  // Handle access to various length properties
  if (name->Equals(isolate()->heap()->length_string())) {
    if (map->instance_type() == JS_ARRAY_TYPE) {
      AddCheckMapsWithTransitions(object, map);
      return HLoadNamedField::NewArrayLength(zone(), object, object);
    }
  }

  LookupResult lookup(isolate());
  map->LookupDescriptor(NULL, *name, &lookup);
  if (lookup.IsField()) {
    AddCheckMap(object, map);
    return BuildLoadNamedField(object, map, &lookup);
  }

  // Handle a load of a constant known function.
  if (lookup.IsConstantFunction()) {
    AddCheckMap(object, map);
    Handle<JSFunction> function(lookup.GetConstantFunctionFromMap(*map));
    return new(zone()) HConstant(function, Representation::Tagged());
  }

  // Handle a load from a known field somewhere in the prototype chain.
  LookupInPrototypes(map, name, &lookup);
  if (lookup.IsField()) {
    Handle<JSObject> prototype(JSObject::cast(map->prototype()));
    Handle<JSObject> holder(lookup.holder());
    Handle<Map> holder_map(holder->map());
    AddCheckMap(object, map);
    AddInstruction(
        new(zone()) HCheckPrototypeMaps(prototype, holder, zone()));
    HValue* holder_value = AddInstruction(
            new(zone()) HConstant(holder, Representation::Tagged()));
    return BuildLoadNamedField(holder_value, holder_map, &lookup);
  }

  // Handle a load of a constant function somewhere in the prototype chain.
  if (lookup.IsConstantFunction()) {
    Handle<JSObject> prototype(JSObject::cast(map->prototype()));
    Handle<JSObject> holder(lookup.holder());
    Handle<Map> holder_map(holder->map());
    AddCheckMap(object, map);
    AddInstruction(new(zone()) HCheckPrototypeMaps(prototype, holder, zone()));
    Handle<JSFunction> function(lookup.GetConstantFunctionFromMap(*holder_map));
    return new(zone()) HConstant(function, Representation::Tagged());
  }

  // No luck, do a generic load.
  return BuildLoadNamedGeneric(object, name, expr);
}


HInstruction* HOptimizedGraphBuilder::BuildLoadKeyedGeneric(HValue* object,
                                                            HValue* key) {
  HValue* context = environment()->LookupContext();
  return new(zone()) HLoadKeyedGeneric(context, object, key);
}


HInstruction* HOptimizedGraphBuilder::BuildMonomorphicElementAccess(
    HValue* object,
    HValue* key,
    HValue* val,
    HValue* dependency,
    Handle<Map> map,
    bool is_store,
    KeyedAccessStoreMode store_mode) {
  HCheckMaps* mapcheck = HCheckMaps::New(object, map, zone(), dependency);
  AddInstruction(mapcheck);
  if (dependency) {
    mapcheck->ClearGVNFlag(kDependsOnElementsKind);
  }
  return BuildUncheckedMonomorphicElementAccess(
      object, key, val,
      mapcheck, map->instance_type() == JS_ARRAY_TYPE,
      map->elements_kind(), is_store, store_mode);
}


HInstruction* HOptimizedGraphBuilder::TryBuildConsolidatedElementLoad(
    HValue* object,
    HValue* key,
    HValue* val,
    SmallMapList* maps) {
  // For polymorphic loads of similar elements kinds (i.e. all tagged or all
  // double), always use the "worst case" code without a transition.  This is
  // much faster than transitioning the elements to the worst case, trading a
  // HTransitionElements for a HCheckMaps, and avoiding mutation of the array.
  bool has_double_maps = false;
  bool has_smi_or_object_maps = false;
  bool has_js_array_access = false;
  bool has_non_js_array_access = false;
  Handle<Map> most_general_consolidated_map;
  for (int i = 0; i < maps->length(); ++i) {
    Handle<Map> map = maps->at(i);
    // Don't allow mixing of JSArrays with JSObjects.
    if (map->instance_type() == JS_ARRAY_TYPE) {
      if (has_non_js_array_access) return NULL;
      has_js_array_access = true;
    } else if (has_js_array_access) {
      return NULL;
    } else {
      has_non_js_array_access = true;
    }
    // Don't allow mixed, incompatible elements kinds.
    if (map->has_fast_double_elements()) {
      if (has_smi_or_object_maps) return NULL;
      has_double_maps = true;
    } else if (map->has_fast_smi_or_object_elements()) {
      if (has_double_maps) return NULL;
      has_smi_or_object_maps = true;
    } else {
      return NULL;
    }
    // Remember the most general elements kind, the code for its load will
    // properly handle all of the more specific cases.
    if ((i == 0) || IsMoreGeneralElementsKindTransition(
            most_general_consolidated_map->elements_kind(),
            map->elements_kind())) {
      most_general_consolidated_map = map;
    }
  }
  if (!has_double_maps && !has_smi_or_object_maps) return NULL;

  HCheckMaps* check_maps = HCheckMaps::New(object, maps, zone());
  AddInstruction(check_maps);
  HInstruction* instr = BuildUncheckedMonomorphicElementAccess(
      object, key, val, check_maps,
      most_general_consolidated_map->instance_type() == JS_ARRAY_TYPE,
      most_general_consolidated_map->elements_kind(),
      false, STANDARD_STORE);
  return instr;
}


HValue* HOptimizedGraphBuilder::HandlePolymorphicElementAccess(
    HValue* object,
    HValue* key,
    HValue* val,
    Expression* prop,
    BailoutId ast_id,
    int position,
    bool is_store,
    KeyedAccessStoreMode store_mode,
    bool* has_side_effects) {
  *has_side_effects = false;
  AddInstruction(new(zone()) HCheckNonSmi(object));
  SmallMapList* maps = prop->GetReceiverTypes();
  bool todo_external_array = false;

  if (!is_store) {
    HInstruction* consolidated_load =
        TryBuildConsolidatedElementLoad(object, key, val, maps);
    if (consolidated_load != NULL) {
      *has_side_effects |= consolidated_load->HasObservableSideEffects();
      if (position != RelocInfo::kNoPosition) {
        consolidated_load->set_position(position);
      }
      return consolidated_load;
    }
  }

  static const int kNumElementTypes = kElementsKindCount;
  bool type_todo[kNumElementTypes];
  for (int i = 0; i < kNumElementTypes; ++i) {
    type_todo[i] = false;
  }

  // Elements_kind transition support.
  MapHandleList transition_target(maps->length());
  // Collect possible transition targets.
  MapHandleList possible_transitioned_maps(maps->length());
  for (int i = 0; i < maps->length(); ++i) {
    Handle<Map> map = maps->at(i);
    ElementsKind elements_kind = map->elements_kind();
    if (IsFastElementsKind(elements_kind) &&
        elements_kind != GetInitialFastElementsKind()) {
      possible_transitioned_maps.Add(map);
    }
  }
  // Get transition target for each map (NULL == no transition).
  for (int i = 0; i < maps->length(); ++i) {
    Handle<Map> map = maps->at(i);
    Handle<Map> transitioned_map =
        map->FindTransitionedMap(&possible_transitioned_maps);
    transition_target.Add(transitioned_map);
  }

  int num_untransitionable_maps = 0;
  Handle<Map> untransitionable_map;
  HTransitionElementsKind* transition = NULL;
  for (int i = 0; i < maps->length(); ++i) {
    Handle<Map> map = maps->at(i);
    ASSERT(map->IsMap());
    if (!transition_target.at(i).is_null()) {
      ASSERT(Map::IsValidElementsTransition(
          map->elements_kind(),
          transition_target.at(i)->elements_kind()));
      HValue* context = environment()->LookupContext();
      transition = new(zone()) HTransitionElementsKind(
          context, object, map, transition_target.at(i));
      AddInstruction(transition);
    } else {
      type_todo[map->elements_kind()] = true;
      if (IsExternalArrayElementsKind(map->elements_kind())) {
        todo_external_array = true;
      }
      num_untransitionable_maps++;
      untransitionable_map = map;
    }
  }

  // If only one map is left after transitioning, handle this case
  // monomorphically.
  if (num_untransitionable_maps == 1) {
    HInstruction* instr = NULL;
    if (untransitionable_map->has_slow_elements_kind()) {
      instr = AddInstruction(is_store ? BuildStoreKeyedGeneric(object, key, val)
                                      : BuildLoadKeyedGeneric(object, key));
    } else {
      instr = BuildMonomorphicElementAccess(
          object, key, val, transition, untransitionable_map, is_store,
          store_mode);
    }
    *has_side_effects |= instr->HasObservableSideEffects();
    if (position != RelocInfo::kNoPosition) instr->set_position(position);
    return is_store ? NULL : instr;
  }

  HInstruction* checkspec =
      AddInstruction(HCheckInstanceType::NewIsSpecObject(object, zone()));
  HBasicBlock* join = graph()->CreateBasicBlock();

  HInstruction* elements_kind_instr =
      AddInstruction(new(zone()) HElementsKind(object));
  HInstruction* elements =
      AddInstruction(new(zone()) HLoadElements(object, checkspec));
  HLoadExternalArrayPointer* external_elements = NULL;
  HInstruction* checked_key = NULL;

  // Generated code assumes that FAST_* and DICTIONARY_ELEMENTS ElementsKinds
  // are handled before external arrays.
  STATIC_ASSERT(FAST_SMI_ELEMENTS < FIRST_EXTERNAL_ARRAY_ELEMENTS_KIND);
  STATIC_ASSERT(FAST_HOLEY_ELEMENTS < FIRST_EXTERNAL_ARRAY_ELEMENTS_KIND);
  STATIC_ASSERT(FAST_DOUBLE_ELEMENTS < FIRST_EXTERNAL_ARRAY_ELEMENTS_KIND);
  STATIC_ASSERT(DICTIONARY_ELEMENTS < FIRST_EXTERNAL_ARRAY_ELEMENTS_KIND);

  for (ElementsKind elements_kind = FIRST_ELEMENTS_KIND;
       elements_kind <= LAST_ELEMENTS_KIND;
       elements_kind = ElementsKind(elements_kind + 1)) {
    // After having handled FAST_* and DICTIONARY_ELEMENTS, we need to add some
    // code that's executed for all external array cases.
    STATIC_ASSERT(LAST_EXTERNAL_ARRAY_ELEMENTS_KIND ==
                  LAST_ELEMENTS_KIND);
    if (elements_kind == FIRST_EXTERNAL_ARRAY_ELEMENTS_KIND
        && todo_external_array) {
      HInstruction* length =
          AddInstruction(new(zone()) HFixedArrayBaseLength(elements));
      checked_key = AddBoundsCheck(key, length);
      external_elements = new(zone()) HLoadExternalArrayPointer(elements);
      AddInstruction(external_elements);
    }
    if (type_todo[elements_kind]) {
      HBasicBlock* if_true = graph()->CreateBasicBlock();
      HBasicBlock* if_false = graph()->CreateBasicBlock();
      HCompareConstantEqAndBranch* elements_kind_branch =
          new(zone()) HCompareConstantEqAndBranch(
              elements_kind_instr, elements_kind, Token::EQ_STRICT);
      elements_kind_branch->SetSuccessorAt(0, if_true);
      elements_kind_branch->SetSuccessorAt(1, if_false);
      current_block()->Finish(elements_kind_branch);

      set_current_block(if_true);
      HInstruction* access;
      if (IsFastElementsKind(elements_kind)) {
        if (is_store && !IsFastDoubleElementsKind(elements_kind)) {
          AddInstruction(HCheckMaps::New(
              elements, isolate()->factory()->fixed_array_map(),
              zone(), elements_kind_branch));
        }
        // TODO(jkummerow): The need for these two blocks could be avoided
        // in one of two ways:
        // (1) Introduce ElementsKinds for JSArrays that are distinct from
        //     those for fast objects.
        // (2) Put the common instructions into a third "join" block. This
        //     requires additional AST IDs that we can deopt to from inside
        //     that join block. They must be added to the Property class (when
        //     it's a keyed property) and registered in the full codegen.
        HBasicBlock* if_jsarray = graph()->CreateBasicBlock();
        HBasicBlock* if_fastobject = graph()->CreateBasicBlock();
        HHasInstanceTypeAndBranch* typecheck =
            new(zone()) HHasInstanceTypeAndBranch(object, JS_ARRAY_TYPE);
        typecheck->SetSuccessorAt(0, if_jsarray);
        typecheck->SetSuccessorAt(1, if_fastobject);
        current_block()->Finish(typecheck);

        set_current_block(if_jsarray);
        HInstruction* length;
        length = AddInstruction(
            HLoadNamedField::NewArrayLength(zone(), object, typecheck,
                                            HType::Smi()));
        checked_key = AddBoundsCheck(key, length, ALLOW_SMI_KEY);
        access = AddInstruction(BuildFastElementAccess(
            elements, checked_key, val, elements_kind_branch,
            elements_kind, is_store, STANDARD_STORE));
        if (!is_store) {
          Push(access);
        }

        *has_side_effects |= access->HasObservableSideEffects();
        // The caller will use has_side_effects and add correct Simulate.
        access->SetFlag(HValue::kHasNoObservableSideEffects);
        if (position != -1) {
          access->set_position(position);
        }
        if_jsarray->GotoNoSimulate(join);

        set_current_block(if_fastobject);
        length = AddInstruction(new(zone()) HFixedArrayBaseLength(elements));
        checked_key = AddBoundsCheck(key, length, ALLOW_SMI_KEY);
        access = AddInstruction(BuildFastElementAccess(
            elements, checked_key, val, elements_kind_branch,
            elements_kind, is_store, STANDARD_STORE));
      } else if (elements_kind == DICTIONARY_ELEMENTS) {
        if (is_store) {
          access = AddInstruction(BuildStoreKeyedGeneric(object, key, val));
        } else {
          access = AddInstruction(BuildLoadKeyedGeneric(object, key));
        }
      } else {  // External array elements.
        access = AddInstruction(BuildExternalArrayElementAccess(
            external_elements, checked_key, val,
            elements_kind_branch, elements_kind, is_store));
      }
      *has_side_effects |= access->HasObservableSideEffects();
      // The caller will use has_side_effects and add correct Simulate.
      access->SetFlag(HValue::kHasNoObservableSideEffects);
      if (position != RelocInfo::kNoPosition) access->set_position(position);
      if (!is_store) {
        Push(access);
      }
      current_block()->GotoNoSimulate(join);
      set_current_block(if_false);
    }
  }

  // Deopt if none of the cases matched.
  current_block()->FinishExitWithDeoptimization(HDeoptimize::kNoUses);
  set_current_block(join);
  return is_store ? NULL : Pop();
}


HValue* HOptimizedGraphBuilder::HandleKeyedElementAccess(
    HValue* obj,
    HValue* key,
    HValue* val,
    Expression* expr,
    BailoutId ast_id,
    int position,
    bool is_store,
    bool* has_side_effects) {
  ASSERT(!expr->IsPropertyName());
  HInstruction* instr = NULL;
  if (expr->IsMonomorphic()) {
    Handle<Map> map = expr->GetMonomorphicReceiverType();
    if (map->has_slow_elements_kind()) {
      instr = is_store ? BuildStoreKeyedGeneric(obj, key, val)
                       : BuildLoadKeyedGeneric(obj, key);
      AddInstruction(instr);
    } else {
      AddInstruction(new(zone()) HCheckNonSmi(obj));
      instr = BuildMonomorphicElementAccess(
          obj, key, val, NULL, map, is_store, expr->GetStoreMode());
    }
  } else if (expr->GetReceiverTypes() != NULL &&
             !expr->GetReceiverTypes()->is_empty()) {
    return HandlePolymorphicElementAccess(
        obj, key, val, expr, ast_id, position, is_store,
        expr->GetStoreMode(), has_side_effects);
  } else {
    if (is_store) {
      instr = BuildStoreKeyedGeneric(obj, key, val);
    } else {
      instr = BuildLoadKeyedGeneric(obj, key);
    }
    AddInstruction(instr);
  }
  if (position != RelocInfo::kNoPosition) instr->set_position(position);
  *has_side_effects = instr->HasObservableSideEffects();
  return instr;
}


HInstruction* HOptimizedGraphBuilder::BuildStoreKeyedGeneric(
    HValue* object,
    HValue* key,
    HValue* value) {
  HValue* context = environment()->LookupContext();
  return new(zone()) HStoreKeyedGeneric(
                         context,
                         object,
                         key,
                         value,
                         function_strict_mode_flag());
}


void HOptimizedGraphBuilder::EnsureArgumentsArePushedForAccess() {
  // Outermost function already has arguments on the stack.
  if (function_state()->outer() == NULL) return;

  if (function_state()->arguments_pushed()) return;

  // Push arguments when entering inlined function.
  HEnterInlined* entry = function_state()->entry();
  entry->set_arguments_pushed();

  ZoneList<HValue*>* arguments_values = entry->arguments_values();

  HInstruction* insert_after = entry;
  for (int i = 0; i < arguments_values->length(); i++) {
    HValue* argument = arguments_values->at(i);
    HInstruction* push_argument = new(zone()) HPushArgument(argument);
    push_argument->InsertAfter(insert_after);
    insert_after = push_argument;
  }

  HArgumentsElements* arguments_elements =
      new(zone()) HArgumentsElements(true);
  arguments_elements->ClearFlag(HValue::kUseGVN);
  arguments_elements->InsertAfter(insert_after);
  function_state()->set_arguments_elements(arguments_elements);
}


bool HOptimizedGraphBuilder::TryArgumentsAccess(Property* expr) {
  VariableProxy* proxy = expr->obj()->AsVariableProxy();
  if (proxy == NULL) return false;
  if (!proxy->var()->IsStackAllocated()) return false;
  if (!environment()->Lookup(proxy->var())->CheckFlag(HValue::kIsArguments)) {
    return false;
  }

  HInstruction* result = NULL;
  if (expr->key()->IsPropertyName()) {
    Handle<String> name = expr->key()->AsLiteral()->AsPropertyName();
    if (!name->IsOneByteEqualTo(STATIC_ASCII_VECTOR("length"))) return false;

    if (function_state()->outer() == NULL) {
      HInstruction* elements = AddInstruction(
          new(zone()) HArgumentsElements(false));
      result = new(zone()) HArgumentsLength(elements);
    } else {
      // Number of arguments without receiver.
      int argument_count = environment()->
          arguments_environment()->parameter_count() - 1;
      result = new(zone()) HConstant(
          Handle<Object>(Smi::FromInt(argument_count), isolate()),
          Representation::Integer32());
    }
  } else {
    Push(graph()->GetArgumentsObject());
    VisitForValue(expr->key());
    if (HasStackOverflow() || current_block() == NULL) return true;
    HValue* key = Pop();
    Drop(1);  // Arguments object.
    if (function_state()->outer() == NULL) {
      HInstruction* elements = AddInstruction(
          new(zone()) HArgumentsElements(false));
      HInstruction* length = AddInstruction(
          new(zone()) HArgumentsLength(elements));
      HInstruction* checked_key = AddBoundsCheck(key, length);
      result = new(zone()) HAccessArgumentsAt(elements, length, checked_key);
    } else {
      EnsureArgumentsArePushedForAccess();

      // Number of arguments without receiver.
      HInstruction* elements = function_state()->arguments_elements();
      int argument_count = environment()->
          arguments_environment()->parameter_count() - 1;
      HInstruction* length = AddInstruction(new(zone()) HConstant(
          Handle<Object>(Smi::FromInt(argument_count), isolate()),
          Representation::Integer32()));
      HInstruction* checked_key = AddBoundsCheck(key, length);
      result = new(zone()) HAccessArgumentsAt(elements, length, checked_key);
    }
  }
  ast_context()->ReturnInstruction(result, expr->id());
  return true;
}


void HOptimizedGraphBuilder::VisitProperty(Property* expr) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  expr->RecordTypeFeedback(oracle(), zone());

  if (TryArgumentsAccess(expr)) return;

  CHECK_ALIVE(VisitForValue(expr->obj()));

  HInstruction* instr = NULL;
  if (expr->IsStringLength()) {
    HValue* string = Pop();
    AddInstruction(new(zone()) HCheckNonSmi(string));
    AddInstruction(HCheckInstanceType::NewIsString(string, zone()));
    instr = HStringLength::New(zone(), string);
  } else if (expr->IsStringAccess()) {
    CHECK_ALIVE(VisitForValue(expr->key()));
    HValue* index = Pop();
    HValue* string = Pop();
    HValue* context = environment()->LookupContext();
    HInstruction* char_code =
      BuildStringCharCodeAt(context, string, index);
    AddInstruction(char_code);
    instr = HStringCharFromCode::New(zone(), context, char_code);

  } else if (expr->IsFunctionPrototype()) {
    HValue* function = Pop();
    AddInstruction(new(zone()) HCheckNonSmi(function));
    instr = new(zone()) HLoadFunctionPrototype(function);

  } else if (expr->key()->IsPropertyName()) {
    Handle<String> name = expr->key()->AsLiteral()->AsPropertyName();
    SmallMapList* types = expr->GetReceiverTypes();
    HValue* object = Top();

    Handle<Map> map;
    bool monomorphic = false;
    if (expr->IsMonomorphic()) {
      map = types->first();
      monomorphic = !map->is_dictionary_map();
    } else if (object->HasMonomorphicJSObjectType()) {
      map = object->GetMonomorphicJSObjectMap();
      monomorphic = !map->is_dictionary_map();
    }
    if (monomorphic) {
      Handle<JSFunction> getter;
      Handle<JSObject> holder;
      if (LookupGetter(map, name, &getter, &holder)) {
        AddCheckConstantFunction(holder, Top(), map);
        if (FLAG_inline_accessors && TryInlineGetter(getter, expr)) return;
        AddInstruction(new(zone()) HPushArgument(Pop()));
        instr = new(zone()) HCallConstantFunction(getter, 1);
      } else {
        instr = BuildLoadNamedMonomorphic(Pop(), name, expr, map);
      }
    } else if (types != NULL && types->length() > 1) {
      return HandlePolymorphicLoadNamedField(expr, Pop(), types, name);
    } else {
      instr = BuildLoadNamedGeneric(Pop(), name, expr);
    }

  } else {
    CHECK_ALIVE(VisitForValue(expr->key()));

    HValue* key = Pop();
    HValue* obj = Pop();

    bool has_side_effects = false;
    HValue* load = HandleKeyedElementAccess(
        obj, key, NULL, expr, expr->id(), expr->position(),
        false,  // is_store
        &has_side_effects);
    if (has_side_effects) {
      if (ast_context()->IsEffect()) {
        AddSimulate(expr->id(), REMOVABLE_SIMULATE);
      } else {
        Push(load);
        AddSimulate(expr->id(), REMOVABLE_SIMULATE);
        Drop(1);
      }
    }
    return ast_context()->ReturnValue(load);
  }
  instr->set_position(expr->position());
  return ast_context()->ReturnInstruction(instr, expr->id());
}


void HOptimizedGraphBuilder::AddCheckPrototypeMaps(Handle<JSObject> holder,
                                                   Handle<Map> receiver_map) {
  if (!holder.is_null()) {
    Handle<JSObject> prototype(JSObject::cast(receiver_map->prototype()));
    AddInstruction(
        new(zone()) HCheckPrototypeMaps(prototype, holder, zone()));
  }
}


void HOptimizedGraphBuilder::AddCheckConstantFunction(
    Handle<JSObject> holder,
    HValue* receiver,
    Handle<Map> receiver_map) {
  // Constant functions have the nice property that the map will change if they
  // are overwritten.  Therefore it is enough to check the map of the holder and
  // its prototypes.
  AddCheckMapsWithTransitions(receiver, receiver_map);
  AddCheckPrototypeMaps(holder, receiver_map);
}


class FunctionSorter {
 public:
  FunctionSorter() : index_(0), ticks_(0), ast_length_(0), src_length_(0) { }
  FunctionSorter(int index, int ticks, int ast_length, int src_length)
      : index_(index),
        ticks_(ticks),
        ast_length_(ast_length),
        src_length_(src_length) { }

  int index() const { return index_; }
  int ticks() const { return ticks_; }
  int ast_length() const { return ast_length_; }
  int src_length() const { return src_length_; }

 private:
  int index_;
  int ticks_;
  int ast_length_;
  int src_length_;
};


inline bool operator<(const FunctionSorter& lhs, const FunctionSorter& rhs) {
  int diff = lhs.ticks() - rhs.ticks();
  if (diff != 0) return diff > 0;
  diff = lhs.ast_length() - rhs.ast_length();
  if (diff != 0) return diff < 0;
  return lhs.src_length() < rhs.src_length();
}


void HOptimizedGraphBuilder::HandlePolymorphicCallNamed(
    Call* expr,
    HValue* receiver,
    SmallMapList* types,
    Handle<String> name) {
  // TODO(ager): We should recognize when the prototype chains for different
  // maps are identical. In that case we can avoid repeatedly generating the
  // same prototype map checks.
  int argument_count = expr->arguments()->length() + 1;  // Includes receiver.
  HBasicBlock* join = NULL;
  FunctionSorter order[kMaxCallPolymorphism];
  int ordered_functions = 0;

  Handle<Map> initial_string_map(
      isolate()->native_context()->string_function()->initial_map());
  Handle<Map> string_marker_map(
      JSObject::cast(initial_string_map->prototype())->map());
  Handle<Map> initial_number_map(
      isolate()->native_context()->number_function()->initial_map());
  Handle<Map> number_marker_map(
      JSObject::cast(initial_number_map->prototype())->map());
  Handle<Map> heap_number_map = isolate()->factory()->heap_number_map();

  bool handle_smi = false;

  for (int i = 0;
       i < types->length() && ordered_functions < kMaxCallPolymorphism;
       ++i) {
    Handle<Map> map = types->at(i);
    if (expr->ComputeTarget(map, name)) {
      if (map.is_identical_to(number_marker_map)) handle_smi = true;
      order[ordered_functions++] =
          FunctionSorter(i,
                         expr->target()->shared()->profiler_ticks(),
                         InliningAstSize(expr->target()),
                         expr->target()->shared()->SourceSize());
    }
  }

  std::sort(order, order + ordered_functions);

  HBasicBlock* number_block = NULL;

  for (int fn = 0; fn < ordered_functions; ++fn) {
    int i = order[fn].index();
    Handle<Map> map = types->at(i);
    if (fn == 0) {
      // Only needed once.
      join = graph()->CreateBasicBlock();
      if (handle_smi) {
        HBasicBlock* empty_smi_block = graph()->CreateBasicBlock();
        HBasicBlock* not_smi_block = graph()->CreateBasicBlock();
        number_block = graph()->CreateBasicBlock();
        HIsSmiAndBranch* smicheck = new(zone()) HIsSmiAndBranch(receiver);
        smicheck->SetSuccessorAt(0, empty_smi_block);
        smicheck->SetSuccessorAt(1, not_smi_block);
        current_block()->Finish(smicheck);
        empty_smi_block->Goto(number_block);
        set_current_block(not_smi_block);
      } else {
        AddInstruction(new(zone()) HCheckNonSmi(receiver));
      }
    }
    HBasicBlock* if_true = graph()->CreateBasicBlock();
    HBasicBlock* if_false = graph()->CreateBasicBlock();
    HUnaryControlInstruction* compare;

    if (handle_smi && map.is_identical_to(number_marker_map)) {
      compare = new(zone()) HCompareMap(
          receiver, heap_number_map, if_true, if_false);
      map = initial_number_map;
      expr->set_number_check(
          Handle<JSObject>(JSObject::cast(map->prototype())));
    } else if (map.is_identical_to(string_marker_map)) {
      compare = new(zone()) HIsStringAndBranch(receiver);
      compare->SetSuccessorAt(0, if_true);
      compare->SetSuccessorAt(1, if_false);
      map = initial_string_map;
      expr->set_string_check(
          Handle<JSObject>(JSObject::cast(map->prototype())));
    } else {
      compare = new(zone()) HCompareMap(receiver, map, if_true, if_false);
      expr->set_map_check();
    }

    current_block()->Finish(compare);

    if (expr->check_type() == NUMBER_CHECK) {
      if_true->Goto(number_block);
      if_true = number_block;
      number_block->SetJoinId(expr->id());
    }
    set_current_block(if_true);

    expr->ComputeTarget(map, name);
    AddCheckPrototypeMaps(expr->holder(), map);
    if (FLAG_trace_inlining && FLAG_polymorphic_inlining) {
      Handle<JSFunction> caller = info()->closure();
      SmartArrayPointer<char> caller_name =
          caller->shared()->DebugName()->ToCString();
      PrintF("Trying to inline the polymorphic call to %s from %s\n",
             *name->ToCString(),
             *caller_name);
    }
    if (FLAG_polymorphic_inlining && TryInlineCall(expr)) {
      // Trying to inline will signal that we should bailout from the
      // entire compilation by setting stack overflow on the visitor.
      if (HasStackOverflow()) return;
    } else {
      HCallConstantFunction* call =
          new(zone()) HCallConstantFunction(expr->target(), argument_count);
      call->set_position(expr->position());
      PreProcessCall(call);
      AddInstruction(call);
      if (!ast_context()->IsEffect()) Push(call);
    }

    if (current_block() != NULL) current_block()->Goto(join);
    set_current_block(if_false);
  }

  // Finish up.  Unconditionally deoptimize if we've handled all the maps we
  // know about and do not want to handle ones we've never seen.  Otherwise
  // use a generic IC.
  if (ordered_functions == types->length() && FLAG_deoptimize_uncommon_cases) {
    current_block()->FinishExitWithDeoptimization(HDeoptimize::kNoUses);
  } else {
    HValue* context = environment()->LookupContext();
    HCallNamed* call = new(zone()) HCallNamed(context, name, argument_count);
    call->set_position(expr->position());
    PreProcessCall(call);

    if (join != NULL) {
      AddInstruction(call);
      if (!ast_context()->IsEffect()) Push(call);
      current_block()->Goto(join);
    } else {
      return ast_context()->ReturnInstruction(call, expr->id());
    }
  }

  // We assume that control flow is always live after an expression.  So
  // even without predecessors to the join block, we set it as the exit
  // block and continue by adding instructions there.
  ASSERT(join != NULL);
  if (join->HasPredecessor()) {
    set_current_block(join);
    join->SetJoinId(expr->id());
    if (!ast_context()->IsEffect()) return ast_context()->ReturnValue(Pop());
  } else {
    set_current_block(NULL);
  }
}


void HOptimizedGraphBuilder::TraceInline(Handle<JSFunction> target,
                                         Handle<JSFunction> caller,
                                         const char* reason) {
  if (FLAG_trace_inlining) {
    SmartArrayPointer<char> target_name =
        target->shared()->DebugName()->ToCString();
    SmartArrayPointer<char> caller_name =
        caller->shared()->DebugName()->ToCString();
    if (reason == NULL) {
      PrintF("Inlined %s called from %s.\n", *target_name, *caller_name);
    } else {
      PrintF("Did not inline %s called from %s (%s).\n",
             *target_name, *caller_name, reason);
    }
  }
}


static const int kNotInlinable = 1000000000;


int HOptimizedGraphBuilder::InliningAstSize(Handle<JSFunction> target) {
  if (!FLAG_use_inlining) return kNotInlinable;

  // Precondition: call is monomorphic and we have found a target with the
  // appropriate arity.
  Handle<JSFunction> caller = info()->closure();
  Handle<SharedFunctionInfo> target_shared(target->shared());

  // Do a quick check on source code length to avoid parsing large
  // inlining candidates.
  if (target_shared->SourceSize() >
      Min(FLAG_max_inlined_source_size, kUnlimitedMaxInlinedSourceSize)) {
    TraceInline(target, caller, "target text too big");
    return kNotInlinable;
  }

  // Target must be inlineable.
  if (!target->IsInlineable()) {
    TraceInline(target, caller, "target not inlineable");
    return kNotInlinable;
  }
  if (target_shared->dont_inline() || target_shared->dont_optimize()) {
    TraceInline(target, caller, "target contains unsupported syntax [early]");
    return kNotInlinable;
  }

  int nodes_added = target_shared->ast_node_count();
  return nodes_added;
}


bool HOptimizedGraphBuilder::TryInline(CallKind call_kind,
                                       Handle<JSFunction> target,
                                       int arguments_count,
                                       HValue* implicit_return_value,
                                       BailoutId ast_id,
                                       BailoutId return_id,
                                       InliningKind inlining_kind) {
  int nodes_added = InliningAstSize(target);
  if (nodes_added == kNotInlinable) return false;

  Handle<JSFunction> caller = info()->closure();

  if (nodes_added > Min(FLAG_max_inlined_nodes, kUnlimitedMaxInlinedNodes)) {
    TraceInline(target, caller, "target AST is too large [early]");
    return false;
  }

#if !defined(V8_TARGET_ARCH_IA32)
  // Target must be able to use caller's context.
  CompilationInfo* outer_info = info();
  if (target->context() != outer_info->closure()->context() ||
      outer_info->scope()->contains_with() ||
      outer_info->scope()->num_heap_slots() > 0) {
    TraceInline(target, caller, "target requires context change");
    return false;
  }
#endif


  // Don't inline deeper than kMaxInliningLevels calls.
  HEnvironment* env = environment();
  int current_level = 1;
  while (env->outer() != NULL) {
    if (current_level == Compiler::kMaxInliningLevels) {
      TraceInline(target, caller, "inline depth limit reached");
      return false;
    }
    if (env->outer()->frame_type() == JS_FUNCTION) {
      current_level++;
    }
    env = env->outer();
  }

  // Don't inline recursive functions.
  for (FunctionState* state = function_state();
       state != NULL;
       state = state->outer()) {
    if (*state->compilation_info()->closure() == *target) {
      TraceInline(target, caller, "target is recursive");
      return false;
    }
  }

  // We don't want to add more than a certain number of nodes from inlining.
  if (inlined_count_ > Min(FLAG_max_inlined_nodes_cumulative,
                           kUnlimitedMaxInlinedNodesCumulative)) {
    TraceInline(target, caller, "cumulative AST node limit reached");
    return false;
  }

  // Parse and allocate variables.
  CompilationInfo target_info(target, zone());
  Handle<SharedFunctionInfo> target_shared(target->shared());
  if (!Parser::Parse(&target_info) || !Scope::Analyze(&target_info)) {
    if (target_info.isolate()->has_pending_exception()) {
      // Parse or scope error, never optimize this function.
      SetStackOverflow();
      target_shared->DisableOptimization("parse/scope error");
    }
    TraceInline(target, caller, "parse failure");
    return false;
  }

  if (target_info.scope()->num_heap_slots() > 0) {
    TraceInline(target, caller, "target has context-allocated variables");
    return false;
  }
  FunctionLiteral* function = target_info.function();

  // The following conditions must be checked again after re-parsing, because
  // earlier the information might not have been complete due to lazy parsing.
  nodes_added = function->ast_node_count();
  if (nodes_added > Min(FLAG_max_inlined_nodes, kUnlimitedMaxInlinedNodes)) {
    TraceInline(target, caller, "target AST is too large [late]");
    return false;
  }
  AstProperties::Flags* flags(function->flags());
  if (flags->Contains(kDontInline) || flags->Contains(kDontOptimize)) {
    TraceInline(target, caller, "target contains unsupported syntax [late]");
    return false;
  }

  // If the function uses the arguments object check that inlining of functions
  // with arguments object is enabled and the arguments-variable is
  // stack allocated.
  if (function->scope()->arguments() != NULL) {
    if (!FLAG_inline_arguments) {
      TraceInline(target, caller, "target uses arguments object");
      return false;
    }

    if (!function->scope()->arguments()->IsStackAllocated()) {
      TraceInline(target,
                  caller,
                  "target uses non-stackallocated arguments object");
      return false;
    }
  }

  // All declarations must be inlineable.
  ZoneList<Declaration*>* decls = target_info.scope()->declarations();
  int decl_count = decls->length();
  for (int i = 0; i < decl_count; ++i) {
    if (!decls->at(i)->IsInlineable()) {
      TraceInline(target, caller, "target has non-trivial declaration");
      return false;
    }
  }

  // Generate the deoptimization data for the unoptimized version of
  // the target function if we don't already have it.
  if (!target_shared->has_deoptimization_support()) {
    // Note that we compile here using the same AST that we will use for
    // generating the optimized inline code.
    target_info.EnableDeoptimizationSupport();
    if (!FullCodeGenerator::MakeCode(&target_info)) {
      TraceInline(target, caller, "could not generate deoptimization info");
      return false;
    }
    if (target_shared->scope_info() == ScopeInfo::Empty(isolate())) {
      // The scope info might not have been set if a lazily compiled
      // function is inlined before being called for the first time.
      Handle<ScopeInfo> target_scope_info =
          ScopeInfo::Create(target_info.scope(), zone());
      target_shared->set_scope_info(*target_scope_info);
    }
    target_shared->EnableDeoptimizationSupport(*target_info.code());
    Compiler::RecordFunctionCompilation(Logger::FUNCTION_TAG,
                                        &target_info,
                                        target_shared);
  }

  // ----------------------------------------------------------------
  // After this point, we've made a decision to inline this function (so
  // TryInline should always return true).

  // Save the pending call context and type feedback oracle. Set up new ones
  // for the inlined function.
  ASSERT(target_shared->has_deoptimization_support());
  Handle<Code> unoptimized_code(target_shared->code());
  TypeFeedbackOracle target_oracle(
      unoptimized_code,
      Handle<Context>(target->context()->native_context()),
      isolate(),
      zone());
  // The function state is new-allocated because we need to delete it
  // in two different places.
  FunctionState* target_state = new FunctionState(
      this, &target_info, &target_oracle, inlining_kind);

  HConstant* undefined = graph()->GetConstantUndefined();
  bool undefined_receiver = HEnvironment::UseUndefinedReceiver(
      target, function, call_kind, inlining_kind);
  HEnvironment* inner_env =
      environment()->CopyForInlining(target,
                                     arguments_count,
                                     function,
                                     undefined,
                                     function_state()->inlining_kind(),
                                     undefined_receiver);
#ifdef V8_TARGET_ARCH_IA32
  // IA32 only, overwrite the caller's context in the deoptimization
  // environment with the correct one.
  //
  // TODO(kmillikin): implement the same inlining on other platforms so we
  // can remove the unsightly ifdefs in this function.
  HConstant* context =
      new(zone()) HConstant(Handle<Context>(target->context()),
                            Representation::Tagged());
  AddInstruction(context);
  inner_env->BindContext(context);
#endif

  AddSimulate(return_id);
  current_block()->UpdateEnvironment(inner_env);
  ZoneList<HValue*>* arguments_values = NULL;

  // If the function uses arguments copy current arguments values
  // to use them for materialization.
  if (function->scope()->arguments() != NULL) {
    HEnvironment* arguments_env = inner_env->arguments_environment();
    int arguments_count = arguments_env->parameter_count();
    arguments_values = new(zone()) ZoneList<HValue*>(arguments_count, zone());
    for (int i = 0; i < arguments_count; i++) {
      arguments_values->Add(arguments_env->Lookup(i), zone());
    }
  }

  HEnterInlined* enter_inlined =
      new(zone()) HEnterInlined(target,
                                arguments_count,
                                function,
                                function_state()->inlining_kind(),
                                function->scope()->arguments(),
                                arguments_values,
                                undefined_receiver);
  function_state()->set_entry(enter_inlined);
  AddInstruction(enter_inlined);

  // If the function uses arguments object create and bind one.
  if (function->scope()->arguments() != NULL) {
    ASSERT(function->scope()->arguments()->IsStackAllocated());
    inner_env->Bind(function->scope()->arguments(),
                    graph()->GetArgumentsObject());
  }


  VisitDeclarations(target_info.scope()->declarations());
  VisitStatements(function->body());
  if (HasStackOverflow()) {
    // Bail out if the inline function did, as we cannot residualize a call
    // instead.
    TraceInline(target, caller, "inline graph construction failed");
    target_shared->DisableOptimization("inlining bailed out");
    inline_bailout_ = true;
    delete target_state;
    return true;
  }

  // Update inlined nodes count.
  inlined_count_ += nodes_added;

  ASSERT(unoptimized_code->kind() == Code::FUNCTION);
  Handle<TypeFeedbackInfo> type_info(
      TypeFeedbackInfo::cast(unoptimized_code->type_feedback_info()));
  graph()->update_type_change_checksum(type_info->own_type_change_checksum());

  TraceInline(target, caller, NULL);

  if (current_block() != NULL) {
    FunctionState* state = function_state();
    if (state->inlining_kind() == CONSTRUCT_CALL_RETURN) {
      // Falling off the end of an inlined construct call. In a test context the
      // return value will always evaluate to true, in a value context the
      // return value is the newly allocated receiver.
      if (call_context()->IsTest()) {
        current_block()->Goto(inlined_test_context()->if_true(), state);
      } else if (call_context()->IsEffect()) {
        current_block()->Goto(function_return(), state);
      } else {
        ASSERT(call_context()->IsValue());
        current_block()->AddLeaveInlined(implicit_return_value, state);
      }
    } else if (state->inlining_kind() == SETTER_CALL_RETURN) {
      // Falling off the end of an inlined setter call. The returned value is
      // never used, the value of an assignment is always the value of the RHS
      // of the assignment.
      if (call_context()->IsTest()) {
        inlined_test_context()->ReturnValue(implicit_return_value);
      } else if (call_context()->IsEffect()) {
        current_block()->Goto(function_return(), state);
      } else {
        ASSERT(call_context()->IsValue());
        current_block()->AddLeaveInlined(implicit_return_value, state);
      }
    } else {
      // Falling off the end of a normal inlined function. This basically means
      // returning undefined.
      if (call_context()->IsTest()) {
        current_block()->Goto(inlined_test_context()->if_false(), state);
      } else if (call_context()->IsEffect()) {
        current_block()->Goto(function_return(), state);
      } else {
        ASSERT(call_context()->IsValue());
        current_block()->AddLeaveInlined(undefined, state);
      }
    }
  }

  // Fix up the function exits.
  if (inlined_test_context() != NULL) {
    HBasicBlock* if_true = inlined_test_context()->if_true();
    HBasicBlock* if_false = inlined_test_context()->if_false();

    // Pop the return test context from the expression context stack.
    ASSERT(ast_context() == inlined_test_context());
    ClearInlinedTestContext();
    delete target_state;

    // Forward to the real test context.
    if (if_true->HasPredecessor()) {
      if_true->SetJoinId(ast_id);
      HBasicBlock* true_target = TestContext::cast(ast_context())->if_true();
      if_true->Goto(true_target, function_state());
    }
    if (if_false->HasPredecessor()) {
      if_false->SetJoinId(ast_id);
      HBasicBlock* false_target = TestContext::cast(ast_context())->if_false();
      if_false->Goto(false_target, function_state());
    }
    set_current_block(NULL);
    return true;

  } else if (function_return()->HasPredecessor()) {
    function_return()->SetJoinId(ast_id);
    set_current_block(function_return());
  } else {
    set_current_block(NULL);
  }
  delete target_state;
  return true;
}


bool HOptimizedGraphBuilder::TryInlineCall(Call* expr, bool drop_extra) {
  // The function call we are inlining is a method call if the call
  // is a property call.
  CallKind call_kind = (expr->expression()->AsProperty() == NULL)
      ? CALL_AS_FUNCTION
      : CALL_AS_METHOD;

  return TryInline(call_kind,
                   expr->target(),
                   expr->arguments()->length(),
                   NULL,
                   expr->id(),
                   expr->ReturnId(),
                   drop_extra ? DROP_EXTRA_ON_RETURN : NORMAL_RETURN);
}


bool HOptimizedGraphBuilder::TryInlineConstruct(CallNew* expr,
                                                HValue* implicit_return_value) {
  return TryInline(CALL_AS_FUNCTION,
                   expr->target(),
                   expr->arguments()->length(),
                   implicit_return_value,
                   expr->id(),
                   expr->ReturnId(),
                   CONSTRUCT_CALL_RETURN);
}


bool HOptimizedGraphBuilder::TryInlineGetter(Handle<JSFunction> getter,
                                             Property* prop) {
  return TryInline(CALL_AS_METHOD,
                   getter,
                   0,
                   NULL,
                   prop->id(),
                   prop->LoadId(),
                   GETTER_CALL_RETURN);
}


bool HOptimizedGraphBuilder::TryInlineSetter(Handle<JSFunction> setter,
                                             Assignment* assignment,
                                             HValue* implicit_return_value) {
  return TryInline(CALL_AS_METHOD,
                   setter,
                   1,
                   implicit_return_value,
                   assignment->id(),
                   assignment->AssignmentId(),
                   SETTER_CALL_RETURN);
}


bool HOptimizedGraphBuilder::TryInlineApply(Handle<JSFunction> function,
                                            Call* expr,
                                            int arguments_count) {
  return TryInline(CALL_AS_METHOD,
                   function,
                   arguments_count,
                   NULL,
                   expr->id(),
                   expr->ReturnId(),
                   NORMAL_RETURN);
}


bool HOptimizedGraphBuilder::TryInlineBuiltinFunctionCall(Call* expr,
                                                          bool drop_extra) {
  if (!expr->target()->shared()->HasBuiltinFunctionId()) return false;
  BuiltinFunctionId id = expr->target()->shared()->builtin_function_id();
  switch (id) {
    case kMathExp:
      if (!FLAG_fast_math) break;
      // Fall through if FLAG_fast_math.
    case kMathRound:
    case kMathFloor:
    case kMathAbs:
    case kMathSqrt:
    case kMathLog:
    case kMathSin:
    case kMathCos:
    case kMathTan:
      if (expr->arguments()->length() == 1) {
        HValue* argument = Pop();
        HValue* context = environment()->LookupContext();
        Drop(1);  // Receiver.
        HInstruction* op =
            HUnaryMathOperation::New(zone(), context, argument, id);
        op->set_position(expr->position());
        if (drop_extra) Drop(1);  // Optionally drop the function.
        ast_context()->ReturnInstruction(op, expr->id());
        return true;
      }
      break;
    case kMathImul:
      if (expr->arguments()->length() == 2) {
        HValue* right = Pop();
        HValue* left = Pop();
        Drop(1);  // Receiver.
        HValue* context = environment()->LookupContext();
        HInstruction* op = HMul::NewImul(zone(), context, left, right);
        if (drop_extra) Drop(1);  // Optionally drop the function.
        ast_context()->ReturnInstruction(op, expr->id());
        return true;
      }
      break;
    default:
      // Not supported for inlining yet.
      break;
  }
  return false;
}


bool HOptimizedGraphBuilder::TryInlineBuiltinMethodCall(
    Call* expr,
    HValue* receiver,
    Handle<Map> receiver_map,
    CheckType check_type) {
  ASSERT(check_type != RECEIVER_MAP_CHECK || !receiver_map.is_null());
  // Try to inline calls like Math.* as operations in the calling function.
  if (!expr->target()->shared()->HasBuiltinFunctionId()) return false;
  BuiltinFunctionId id = expr->target()->shared()->builtin_function_id();
  int argument_count = expr->arguments()->length() + 1;  // Plus receiver.
  switch (id) {
    case kStringCharCodeAt:
    case kStringCharAt:
      if (argument_count == 2 && check_type == STRING_CHECK) {
        HValue* index = Pop();
        HValue* string = Pop();
        HValue* context = environment()->LookupContext();
        ASSERT(!expr->holder().is_null());
        AddInstruction(new(zone()) HCheckPrototypeMaps(
            oracle()->GetPrototypeForPrimitiveCheck(STRING_CHECK),
            expr->holder(),
            zone()));
        HInstruction* char_code =
            BuildStringCharCodeAt(context, string, index);
        if (id == kStringCharCodeAt) {
          ast_context()->ReturnInstruction(char_code, expr->id());
          return true;
        }
        AddInstruction(char_code);
        HInstruction* result =
            HStringCharFromCode::New(zone(), context, char_code);
        ast_context()->ReturnInstruction(result, expr->id());
        return true;
      }
      break;
    case kStringFromCharCode:
      if (argument_count == 2 && check_type == RECEIVER_MAP_CHECK) {
        AddCheckConstantFunction(expr->holder(), receiver, receiver_map);
        HValue* argument = Pop();
        HValue* context = environment()->LookupContext();
        Drop(1);  // Receiver.
        HInstruction* result =
            HStringCharFromCode::New(zone(), context, argument);
        ast_context()->ReturnInstruction(result, expr->id());
        return true;
      }
      break;
    case kMathExp:
      if (!FLAG_fast_math) break;
      // Fall through if FLAG_fast_math.
    case kMathRound:
    case kMathFloor:
    case kMathAbs:
    case kMathSqrt:
    case kMathLog:
    case kMathSin:
    case kMathCos:
    case kMathTan:
      if (argument_count == 2 && check_type == RECEIVER_MAP_CHECK) {
        AddCheckConstantFunction(expr->holder(), receiver, receiver_map);
        HValue* argument = Pop();
        HValue* context = environment()->LookupContext();
        Drop(1);  // Receiver.
        HInstruction* op =
            HUnaryMathOperation::New(zone(), context, argument, id);
        op->set_position(expr->position());
        ast_context()->ReturnInstruction(op, expr->id());
        return true;
      }
      break;
    case kMathPow:
      if (argument_count == 3 && check_type == RECEIVER_MAP_CHECK) {
        AddCheckConstantFunction(expr->holder(), receiver, receiver_map);
        HValue* right = Pop();
        HValue* left = Pop();
        Pop();  // Pop receiver.
        HValue* context = environment()->LookupContext();
        HInstruction* result = NULL;
        // Use sqrt() if exponent is 0.5 or -0.5.
        if (right->IsConstant() && HConstant::cast(right)->HasDoubleValue()) {
          double exponent = HConstant::cast(right)->DoubleValue();
          if (exponent == 0.5) {
            result =
                HUnaryMathOperation::New(zone(), context, left, kMathPowHalf);
          } else if (exponent == -0.5) {
            HConstant* double_one =
                new(zone()) HConstant(Handle<Object>(Smi::FromInt(1),
                                                     isolate()),
                                      Representation::Double());
            AddInstruction(double_one);
            HInstruction* sqrt =
                HUnaryMathOperation::New(zone(), context, left, kMathPowHalf);
            AddInstruction(sqrt);
            // MathPowHalf doesn't have side effects so there's no need for
            // an environment simulation here.
            ASSERT(!sqrt->HasObservableSideEffects());
            result = HDiv::New(zone(), context, double_one, sqrt);
          } else if (exponent == 2.0) {
            result = HMul::New(zone(), context, left, left);
          }
        } else if (right->EqualsInteger32Constant(2)) {
          result = HMul::New(zone(), context, left, left);
        }

        if (result == NULL) {
          result = HPower::New(zone(), left, right);
        }
        ast_context()->ReturnInstruction(result, expr->id());
        return true;
      }
      break;
    case kMathRandom:
      if (argument_count == 1 && check_type == RECEIVER_MAP_CHECK) {
        AddCheckConstantFunction(expr->holder(), receiver, receiver_map);
        Drop(1);  // Receiver.
        HValue* context = environment()->LookupContext();
        HGlobalObject* global_object = new(zone()) HGlobalObject(context);
        AddInstruction(global_object);
        HRandom* result = new(zone()) HRandom(global_object);
        ast_context()->ReturnInstruction(result, expr->id());
        return true;
      }
      break;
    case kMathMax:
    case kMathMin:
      if (argument_count == 3 && check_type == RECEIVER_MAP_CHECK) {
        AddCheckConstantFunction(expr->holder(), receiver, receiver_map);
        HValue* right = Pop();
        HValue* left = Pop();
        Drop(1);  // Receiver.
        HValue* context = environment()->LookupContext();
        HMathMinMax::Operation op = (id == kMathMin) ? HMathMinMax::kMathMin
                                                     : HMathMinMax::kMathMax;
        HInstruction* result =
            HMathMinMax::New(zone(), context, left, right, op);
        ast_context()->ReturnInstruction(result, expr->id());
        return true;
      }
      break;
    case kMathImul:
      if (argument_count == 3 && check_type == RECEIVER_MAP_CHECK) {
        AddCheckConstantFunction(expr->holder(), receiver, receiver_map);
        HValue* right = Pop();
        HValue* left = Pop();
        Drop(1);  // Receiver.
        HValue* context = environment()->LookupContext();
        HInstruction* result = HMul::NewImul(zone(), context, left, right);
        ast_context()->ReturnInstruction(result, expr->id());
        return true;
      }
      break;
    default:
      // Not yet supported for inlining.
      break;
  }
  return false;
}


bool HOptimizedGraphBuilder::TryCallApply(Call* expr) {
  Expression* callee = expr->expression();
  Property* prop = callee->AsProperty();
  ASSERT(prop != NULL);

  if (!expr->IsMonomorphic() || expr->check_type() != RECEIVER_MAP_CHECK) {
    return false;
  }
  Handle<Map> function_map = expr->GetReceiverTypes()->first();
  if (function_map->instance_type() != JS_FUNCTION_TYPE ||
      !expr->target()->shared()->HasBuiltinFunctionId() ||
      expr->target()->shared()->builtin_function_id() != kFunctionApply) {
    return false;
  }

  if (info()->scope()->arguments() == NULL) return false;

  ZoneList<Expression*>* args = expr->arguments();
  if (args->length() != 2) return false;

  VariableProxy* arg_two = args->at(1)->AsVariableProxy();
  if (arg_two == NULL || !arg_two->var()->IsStackAllocated()) return false;
  HValue* arg_two_value = environment()->Lookup(arg_two->var());
  if (!arg_two_value->CheckFlag(HValue::kIsArguments)) return false;

  // Found pattern f.apply(receiver, arguments).
  VisitForValue(prop->obj());
  if (HasStackOverflow() || current_block() == NULL) return true;
  HValue* function = Top();
  AddCheckConstantFunction(expr->holder(), function, function_map);
  Drop(1);

  VisitForValue(args->at(0));
  if (HasStackOverflow() || current_block() == NULL) return true;
  HValue* receiver = Pop();

  if (function_state()->outer() == NULL) {
    HInstruction* elements = AddInstruction(
        new(zone()) HArgumentsElements(false));
    HInstruction* length =
        AddInstruction(new(zone()) HArgumentsLength(elements));
    HValue* wrapped_receiver =
        AddInstruction(new(zone()) HWrapReceiver(receiver, function));
    HInstruction* result =
        new(zone()) HApplyArguments(function,
                                    wrapped_receiver,
                                    length,
                                    elements);
    result->set_position(expr->position());
    ast_context()->ReturnInstruction(result, expr->id());
    return true;
  } else {
    // We are inside inlined function and we know exactly what is inside
    // arguments object. But we need to be able to materialize at deopt.
    // TODO(mstarzinger): For now we just ensure arguments are pushed
    // right after HEnterInlined, but we could be smarter about this.
    EnsureArgumentsArePushedForAccess();
    ASSERT_EQ(environment()->arguments_environment()->parameter_count(),
              function_state()->entry()->arguments_values()->length());
    HEnterInlined* entry = function_state()->entry();
    ZoneList<HValue*>* arguments_values = entry->arguments_values();
    int arguments_count = arguments_values->length();
    PushAndAdd(new(zone()) HWrapReceiver(receiver, function));
    for (int i = 1; i < arguments_count; i++) {
      Push(arguments_values->at(i));
    }

    Handle<JSFunction> known_function;
    if (function->IsConstant()) {
      HConstant* constant_function = HConstant::cast(function);
      known_function = Handle<JSFunction>::cast(constant_function->handle());
      int args_count = arguments_count - 1;  // Excluding receiver.
      if (TryInlineApply(known_function, expr, args_count)) return true;
    }

    Drop(arguments_count - 1);
    PushAndAdd(new(zone()) HPushArgument(Pop()));
    for (int i = 1; i < arguments_count; i++) {
      PushAndAdd(new(zone()) HPushArgument(arguments_values->at(i)));
    }

    HValue* context = environment()->LookupContext();
    HInvokeFunction* call = new(zone()) HInvokeFunction(
        context,
        function,
        known_function,
        arguments_count);
    Drop(arguments_count);
    call->set_position(expr->position());
    ast_context()->ReturnInstruction(call, expr->id());
    return true;
  }
}


// Checks if all maps in |types| are from the same family, i.e., are elements
// transitions of each other. Returns either NULL if they are not from the same
// family, or a Map* indicating the map with the first elements kind of the
// family that is in the list.
static Map* CheckSameElementsFamily(SmallMapList* types) {
  if (types->length() <= 1) return NULL;
  // Check if all maps belong to the same transition family.
  Map* kinds[kFastElementsKindCount];
  Map* first_map = *types->first();
  ElementsKind first_kind = first_map->elements_kind();
  if (!IsFastElementsKind(first_kind)) return NULL;
  int first_index = GetSequenceIndexFromFastElementsKind(first_kind);
  int last_index = first_index;

  for (int i = 0; i < kFastElementsKindCount; i++) kinds[i] = NULL;

  kinds[first_index] = first_map;

  for (int i = 1; i < types->length(); ++i) {
    Map* map = *types->at(i);
    ElementsKind elements_kind = map->elements_kind();
    if (!IsFastElementsKind(elements_kind)) return NULL;
    int index = GetSequenceIndexFromFastElementsKind(elements_kind);
    if (index < first_index) {
      first_index = index;
    } else if (index > last_index) {
      last_index = index;
    } else if (kinds[index] != map) {
      return NULL;
    }
    kinds[index] = map;
  }

  Map* current = kinds[first_index];
  for (int i = first_index + 1; i <= last_index; i++) {
    Map* next = kinds[i];
    if (next != NULL) {
      ElementsKind current_kind = next->elements_kind();
      if (next != current->LookupElementsTransitionMap(current_kind)) {
        return NULL;
      }
      current = next;
    }
  }

  return kinds[first_index];
}


void HOptimizedGraphBuilder::VisitCall(Call* expr) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  Expression* callee = expr->expression();
  int argument_count = expr->arguments()->length() + 1;  // Plus receiver.
  HInstruction* call = NULL;

  Property* prop = callee->AsProperty();
  if (prop != NULL) {
    if (!prop->key()->IsPropertyName()) {
      // Keyed function call.
      CHECK_ALIVE(VisitArgument(prop->obj()));

      CHECK_ALIVE(VisitForValue(prop->key()));
      // Push receiver and key like the non-optimized code generator expects it.
      HValue* key = Pop();
      HValue* receiver = Pop();
      Push(key);
      Push(receiver);

      CHECK_ALIVE(VisitArgumentList(expr->arguments()));

      HValue* context = environment()->LookupContext();
      call = new(zone()) HCallKeyed(context, key, argument_count);
      call->set_position(expr->position());
      Drop(argument_count + 1);  // 1 is the key.
      return ast_context()->ReturnInstruction(call, expr->id());
    }

    // Named function call.
    expr->RecordTypeFeedback(oracle(), CALL_AS_METHOD);

    if (TryCallApply(expr)) return;

    CHECK_ALIVE(VisitForValue(prop->obj()));
    CHECK_ALIVE(VisitExpressions(expr->arguments()));

    Handle<String> name = prop->key()->AsLiteral()->AsPropertyName();
    SmallMapList* types = expr->GetReceiverTypes();

    bool monomorphic = expr->IsMonomorphic();
    Handle<Map> receiver_map;
    if (monomorphic) {
      receiver_map = (types == NULL || types->is_empty())
          ? Handle<Map>::null()
          : types->first();
    } else {
      Map* family_map = CheckSameElementsFamily(types);
      if (family_map != NULL) {
        receiver_map = Handle<Map>(family_map);
        monomorphic = expr->ComputeTarget(receiver_map, name);
      }
    }

    HValue* receiver =
        environment()->ExpressionStackAt(expr->arguments()->length());
    if (monomorphic) {
      if (TryInlineBuiltinMethodCall(expr,
                                     receiver,
                                     receiver_map,
                                     expr->check_type())) {
        if (FLAG_trace_inlining) {
          PrintF("Inlining builtin ");
          expr->target()->ShortPrint();
          PrintF("\n");
        }
        return;
      }

      if (CallStubCompiler::HasCustomCallGenerator(expr->target()) ||
          expr->check_type() != RECEIVER_MAP_CHECK) {
        // When the target has a custom call IC generator, use the IC,
        // because it is likely to generate better code.  Also use the IC
        // when a primitive receiver check is required.
        HValue* context = environment()->LookupContext();
        call = PreProcessCall(
            new(zone()) HCallNamed(context, name, argument_count));
      } else {
        AddCheckConstantFunction(expr->holder(), receiver, receiver_map);

        if (TryInlineCall(expr)) return;
        call = PreProcessCall(
            new(zone()) HCallConstantFunction(expr->target(),
                                              argument_count));
      }
    } else if (types != NULL && types->length() > 1) {
      ASSERT(expr->check_type() == RECEIVER_MAP_CHECK);
      HandlePolymorphicCallNamed(expr, receiver, types, name);
      return;

    } else {
      HValue* context = environment()->LookupContext();
      call = PreProcessCall(
          new(zone()) HCallNamed(context, name, argument_count));
    }

  } else {
    expr->RecordTypeFeedback(oracle(), CALL_AS_FUNCTION);
    VariableProxy* proxy = expr->expression()->AsVariableProxy();
    bool global_call = proxy != NULL && proxy->var()->IsUnallocated();

    if (proxy != NULL && proxy->var()->is_possibly_eval(isolate())) {
      return Bailout("possible direct call to eval");
    }

    if (global_call) {
      Variable* var = proxy->var();
      bool known_global_function = false;
      // If there is a global property cell for the name at compile time and
      // access check is not enabled we assume that the function will not change
      // and generate optimized code for calling the function.
      LookupResult lookup(isolate());
      GlobalPropertyAccess type = LookupGlobalProperty(var, &lookup, false);
      if (type == kUseCell &&
          !info()->global_object()->IsAccessCheckNeeded()) {
        Handle<GlobalObject> global(info()->global_object());
        known_global_function = expr->ComputeGlobalTarget(global, &lookup);
      }
      if (known_global_function) {
        // Push the global object instead of the global receiver because
        // code generated by the full code generator expects it.
        HValue* context = environment()->LookupContext();
        HGlobalObject* global_object = new(zone()) HGlobalObject(context);
        PushAndAdd(global_object);
        CHECK_ALIVE(VisitExpressions(expr->arguments()));

        CHECK_ALIVE(VisitForValue(expr->expression()));
        HValue* function = Pop();
        AddInstruction(new(zone()) HCheckFunction(function, expr->target()));

        // Replace the global object with the global receiver.
        HGlobalReceiver* global_receiver =
            new(zone()) HGlobalReceiver(global_object);
        // Index of the receiver from the top of the expression stack.
        const int receiver_index = argument_count - 1;
        AddInstruction(global_receiver);
        ASSERT(environment()->ExpressionStackAt(receiver_index)->
               IsGlobalObject());
        environment()->SetExpressionStackAt(receiver_index, global_receiver);

        if (TryInlineBuiltinFunctionCall(expr, false)) {  // Nothing to drop.
          if (FLAG_trace_inlining) {
            PrintF("Inlining builtin ");
            expr->target()->ShortPrint();
            PrintF("\n");
          }
          return;
        }
        if (TryInlineCall(expr)) return;

        if (expr->target().is_identical_to(info()->closure())) {
          graph()->MarkRecursive();
        }

        call = PreProcessCall(new(zone()) HCallKnownGlobal(expr->target(),
                                                           argument_count));
      } else {
        HValue* context = environment()->LookupContext();
        HGlobalObject* receiver = new(zone()) HGlobalObject(context);
        AddInstruction(receiver);
        PushAndAdd(new(zone()) HPushArgument(receiver));
        CHECK_ALIVE(VisitArgumentList(expr->arguments()));

        call = new(zone()) HCallGlobal(context, var->name(), argument_count);
        Drop(argument_count);
      }

    } else if (expr->IsMonomorphic()) {
      // The function is on the stack in the unoptimized code during
      // evaluation of the arguments.
      CHECK_ALIVE(VisitForValue(expr->expression()));
      HValue* function = Top();
      HValue* context = environment()->LookupContext();
      HGlobalObject* global = new(zone()) HGlobalObject(context);
      AddInstruction(global);
      HGlobalReceiver* receiver = new(zone()) HGlobalReceiver(global);
      PushAndAdd(receiver);
      CHECK_ALIVE(VisitExpressions(expr->arguments()));
      AddInstruction(new(zone()) HCheckFunction(function, expr->target()));

      if (TryInlineBuiltinFunctionCall(expr, true)) {  // Drop the function.
        if (FLAG_trace_inlining) {
          PrintF("Inlining builtin ");
          expr->target()->ShortPrint();
          PrintF("\n");
        }
        return;
      }

      if (TryInlineCall(expr, true)) {   // Drop function from environment.
        return;
      } else {
        call = PreProcessCall(
            new(zone()) HInvokeFunction(context,
                                        function,
                                        expr->target(),
                                        argument_count));
        Drop(1);  // The function.
      }

    } else {
      CHECK_ALIVE(VisitForValue(expr->expression()));
      HValue* function = Top();
      HValue* context = environment()->LookupContext();
      HGlobalObject* global_object = new(zone()) HGlobalObject(context);
      AddInstruction(global_object);
      HGlobalReceiver* receiver = new(zone()) HGlobalReceiver(global_object);
      AddInstruction(receiver);
      PushAndAdd(new(zone()) HPushArgument(receiver));
      CHECK_ALIVE(VisitArgumentList(expr->arguments()));

      call = new(zone()) HCallFunction(context, function, argument_count);
      Drop(argument_count + 1);
    }
  }

  call->set_position(expr->position());
  return ast_context()->ReturnInstruction(call, expr->id());
}


// Checks whether allocation using the given constructor can be inlined.
static bool IsAllocationInlineable(Handle<JSFunction> constructor) {
  return constructor->has_initial_map() &&
      constructor->initial_map()->instance_type() == JS_OBJECT_TYPE &&
      constructor->initial_map()->instance_size() < HAllocateObject::kMaxSize;
}


void HOptimizedGraphBuilder::VisitCallNew(CallNew* expr) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  expr->RecordTypeFeedback(oracle());
  int argument_count = expr->arguments()->length() + 1;  // Plus constructor.
  HValue* context = environment()->LookupContext();

  if (FLAG_inline_construct &&
      expr->IsMonomorphic() &&
      IsAllocationInlineable(expr->target())) {
    // The constructor function is on the stack in the unoptimized code
    // during evaluation of the arguments.
    CHECK_ALIVE(VisitForValue(expr->expression()));
    HValue* function = Top();
    CHECK_ALIVE(VisitExpressions(expr->arguments()));
    Handle<JSFunction> constructor = expr->target();
    HValue* check = AddInstruction(
        new(zone()) HCheckFunction(function, constructor));

    // Force completion of inobject slack tracking before generating
    // allocation code to finalize instance size.
    if (constructor->shared()->IsInobjectSlackTrackingInProgress()) {
      constructor->shared()->CompleteInobjectSlackTracking();
    }

    // Replace the constructor function with a newly allocated receiver.
    HInstruction* receiver = new(zone()) HAllocateObject(context, constructor);
    // Index of the receiver from the top of the expression stack.
    const int receiver_index = argument_count - 1;
    AddInstruction(receiver);
    ASSERT(environment()->ExpressionStackAt(receiver_index) == function);
    environment()->SetExpressionStackAt(receiver_index, receiver);

    if (TryInlineConstruct(expr, receiver)) return;

    // TODO(mstarzinger): For now we remove the previous HAllocateObject and
    // add HPushArgument for the arguments in case inlining failed.  What we
    // actually should do is emit HInvokeFunction on the constructor instead
    // of using HCallNew as a fallback.
    receiver->DeleteAndReplaceWith(NULL);
    check->DeleteAndReplaceWith(NULL);
    environment()->SetExpressionStackAt(receiver_index, function);
    HInstruction* call = PreProcessCall(
        new(zone()) HCallNew(context, function, argument_count));
    call->set_position(expr->position());
    return ast_context()->ReturnInstruction(call, expr->id());
  } else {
    // The constructor function is both an operand to the instruction and an
    // argument to the construct call.
    bool use_call_new_array = FLAG_optimize_constructed_arrays &&
        !(expr->target().is_null()) &&
        *(expr->target()) == isolate()->global_context()->array_function();

    CHECK_ALIVE(VisitArgument(expr->expression()));
    HValue* constructor = HPushArgument::cast(Top())->argument();
    CHECK_ALIVE(VisitArgumentList(expr->arguments()));
    HCallNew* call;
    if (use_call_new_array) {
      AddInstruction(new(zone()) HCheckFunction(constructor,
          Handle<JSFunction>(isolate()->global_context()->array_function())));
      Handle<Object> feedback = oracle()->GetInfo(expr->CallNewFeedbackId());
      ASSERT(feedback->IsSmi());

      // TODO(mvstanton): It would be better to use the already created global
      // property cell that is shared by full code gen. That way, any transition
      // information that happened after crankshaft won't be lost.  The right
      // way to do that is to begin passing the cell to the type feedback oracle
      // instead of just the value in the cell. Do this in a follow-up checkin.
      Handle<JSGlobalPropertyCell> cell =
          isolate()->factory()->NewJSGlobalPropertyCell(feedback);

      // TODO(mvstanton): Here we should probably insert code to check if the
      // type cell elements kind is different from when we compiled, and deopt
      // in that case. Do this in a follow-up checin.
      call = new(zone()) HCallNewArray(context, constructor, argument_count,
                                       cell);
    } else {
      call = new(zone()) HCallNew(context, constructor, argument_count);
    }
    Drop(argument_count);
    call->set_position(expr->position());
    return ast_context()->ReturnInstruction(call, expr->id());
  }
}


// Support for generating inlined runtime functions.

// Lookup table for generators for runtime calls that are generated inline.
// Elements of the table are member pointers to functions of
// HOptimizedGraphBuilder.
#define INLINE_FUNCTION_GENERATOR_ADDRESS(Name, argc, ressize)  \
    &HOptimizedGraphBuilder::Generate##Name,

const HOptimizedGraphBuilder::InlineFunctionGenerator
    HOptimizedGraphBuilder::kInlineFunctionGenerators[] = {
        INLINE_FUNCTION_LIST(INLINE_FUNCTION_GENERATOR_ADDRESS)
        INLINE_RUNTIME_FUNCTION_LIST(INLINE_FUNCTION_GENERATOR_ADDRESS)
};
#undef INLINE_FUNCTION_GENERATOR_ADDRESS


void HOptimizedGraphBuilder::VisitCallRuntime(CallRuntime* expr) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  if (expr->is_jsruntime()) {
    return Bailout("call to a JavaScript runtime function");
  }

  const Runtime::Function* function = expr->function();
  ASSERT(function != NULL);
  if (function->intrinsic_type == Runtime::INLINE) {
    ASSERT(expr->name()->length() > 0);
    ASSERT(expr->name()->Get(0) == '_');
    // Call to an inline function.
    int lookup_index = static_cast<int>(function->function_id) -
        static_cast<int>(Runtime::kFirstInlineFunction);
    ASSERT(lookup_index >= 0);
    ASSERT(static_cast<size_t>(lookup_index) <
           ARRAY_SIZE(kInlineFunctionGenerators));
    InlineFunctionGenerator generator = kInlineFunctionGenerators[lookup_index];

    // Call the inline code generator using the pointer-to-member.
    (this->*generator)(expr);
  } else {
    ASSERT(function->intrinsic_type == Runtime::RUNTIME);
    CHECK_ALIVE(VisitArgumentList(expr->arguments()));

    HValue* context = environment()->LookupContext();
    Handle<String> name = expr->name();
    int argument_count = expr->arguments()->length();
    HCallRuntime* call =
        new(zone()) HCallRuntime(context, name, function, argument_count);
    Drop(argument_count);
    return ast_context()->ReturnInstruction(call, expr->id());
  }
}


void HOptimizedGraphBuilder::VisitUnaryOperation(UnaryOperation* expr) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  switch (expr->op()) {
    case Token::DELETE: return VisitDelete(expr);
    case Token::VOID: return VisitVoid(expr);
    case Token::TYPEOF: return VisitTypeof(expr);
    case Token::SUB: return VisitSub(expr);
    case Token::BIT_NOT: return VisitBitNot(expr);
    case Token::NOT: return VisitNot(expr);
    default: UNREACHABLE();
  }
}

void HOptimizedGraphBuilder::VisitDelete(UnaryOperation* expr) {
  Property* prop = expr->expression()->AsProperty();
  VariableProxy* proxy = expr->expression()->AsVariableProxy();
  if (prop != NULL) {
    CHECK_ALIVE(VisitForValue(prop->obj()));
    CHECK_ALIVE(VisitForValue(prop->key()));
    HValue* key = Pop();
    HValue* obj = Pop();
    HValue* context = environment()->LookupContext();
    HDeleteProperty* instr = new(zone()) HDeleteProperty(context, obj, key);
    return ast_context()->ReturnInstruction(instr, expr->id());
  } else if (proxy != NULL) {
    Variable* var = proxy->var();
    if (var->IsUnallocated()) {
      Bailout("delete with global variable");
    } else if (var->IsStackAllocated() || var->IsContextSlot()) {
      // Result of deleting non-global variables is false.  'this' is not
      // really a variable, though we implement it as one.  The
      // subexpression does not have side effects.
      HValue* value = var->is_this()
          ? graph()->GetConstantTrue()
          : graph()->GetConstantFalse();
      return ast_context()->ReturnValue(value);
    } else {
      Bailout("delete with non-global variable");
    }
  } else {
    // Result of deleting non-property, non-variable reference is true.
    // Evaluate the subexpression for side effects.
    CHECK_ALIVE(VisitForEffect(expr->expression()));
    return ast_context()->ReturnValue(graph()->GetConstantTrue());
  }
}


void HOptimizedGraphBuilder::VisitVoid(UnaryOperation* expr) {
  CHECK_ALIVE(VisitForEffect(expr->expression()));
  return ast_context()->ReturnValue(graph()->GetConstantUndefined());
}


void HOptimizedGraphBuilder::VisitTypeof(UnaryOperation* expr) {
  CHECK_ALIVE(VisitForTypeOf(expr->expression()));
  HValue* value = Pop();
  HValue* context = environment()->LookupContext();
  HInstruction* instr = new(zone()) HTypeof(context, value);
  return ast_context()->ReturnInstruction(instr, expr->id());
}


void HOptimizedGraphBuilder::VisitSub(UnaryOperation* expr) {
  CHECK_ALIVE(VisitForValue(expr->expression()));
  HValue* value = Pop();
  HValue* context = environment()->LookupContext();
  HInstruction* instr =
      HMul::New(zone(), context, value, graph()->GetConstantMinus1());
  TypeInfo info = oracle()->UnaryType(expr);
  Representation rep = ToRepresentation(info);
  if (info.IsUninitialized()) {
    AddSoftDeoptimize();
    info = TypeInfo::Unknown();
  }
  if (instr->IsBinaryOperation()) {
    HBinaryOperation::cast(instr)->set_observed_input_representation(1, rep);
    HBinaryOperation::cast(instr)->set_observed_input_representation(2, rep);
  }
  return ast_context()->ReturnInstruction(instr, expr->id());
}


void HOptimizedGraphBuilder::VisitBitNot(UnaryOperation* expr) {
  CHECK_ALIVE(VisitForValue(expr->expression()));
  HValue* value = Pop();
  TypeInfo info = oracle()->UnaryType(expr);
  if (info.IsUninitialized()) {
    AddSoftDeoptimize();
  }
  HInstruction* instr = new(zone()) HBitNot(value);
  return ast_context()->ReturnInstruction(instr, expr->id());
}


void HOptimizedGraphBuilder::VisitNot(UnaryOperation* expr) {
  if (ast_context()->IsTest()) {
    TestContext* context = TestContext::cast(ast_context());
    VisitForControl(expr->expression(),
                    context->if_false(),
                    context->if_true());
    return;
  }

  if (ast_context()->IsEffect()) {
    VisitForEffect(expr->expression());
    return;
  }

  ASSERT(ast_context()->IsValue());
  HBasicBlock* materialize_false = graph()->CreateBasicBlock();
  HBasicBlock* materialize_true = graph()->CreateBasicBlock();
  CHECK_BAILOUT(VisitForControl(expr->expression(),
                                materialize_false,
                                materialize_true));

  if (materialize_false->HasPredecessor()) {
    materialize_false->SetJoinId(expr->MaterializeFalseId());
    set_current_block(materialize_false);
    Push(graph()->GetConstantFalse());
  } else {
    materialize_false = NULL;
  }

  if (materialize_true->HasPredecessor()) {
    materialize_true->SetJoinId(expr->MaterializeTrueId());
    set_current_block(materialize_true);
    Push(graph()->GetConstantTrue());
  } else {
    materialize_true = NULL;
  }

  HBasicBlock* join =
    CreateJoin(materialize_false, materialize_true, expr->id());
  set_current_block(join);
  if (join != NULL) return ast_context()->ReturnValue(Pop());
}


HInstruction* HOptimizedGraphBuilder::BuildIncrement(
    bool returns_original_input,
    CountOperation* expr) {
  // The input to the count operation is on top of the expression stack.
  TypeInfo info = oracle()->IncrementType(expr);
  Representation rep = ToRepresentation(info);
  if (rep.IsTagged()) {
    rep = Representation::Integer32();
  }

  if (returns_original_input) {
    // We need an explicit HValue representing ToNumber(input).  The
    // actual HChange instruction we need is (sometimes) added in a later
    // phase, so it is not available now to be used as an input to HAdd and
    // as the return value.
    HInstruction* number_input = new(zone()) HForceRepresentation(Pop(), rep);
    AddInstruction(number_input);
    Push(number_input);
  }

  // The addition has no side effects, so we do not need
  // to simulate the expression stack after this instruction.
  // Any later failures deopt to the load of the input or earlier.
  HConstant* delta = (expr->op() == Token::INC)
      ? graph()->GetConstant1()
      : graph()->GetConstantMinus1();
  HValue* context = environment()->LookupContext();
  HInstruction* instr = HAdd::New(zone(), context, Top(), delta);
  // We can't insert a simulate here, because it would break deoptimization,
  // so the HAdd must not have side effects, so we must freeze its
  // representation.
  instr->AssumeRepresentation(rep);
  instr->ClearAllSideEffects();
  AddInstruction(instr);
  return instr;
}


void HOptimizedGraphBuilder::VisitCountOperation(CountOperation* expr) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  Expression* target = expr->expression();
  VariableProxy* proxy = target->AsVariableProxy();
  Property* prop = target->AsProperty();
  if (proxy == NULL && prop == NULL) {
    return Bailout("invalid lhs in count operation");
  }

  // Match the full code generator stack by simulating an extra stack
  // element for postfix operations in a non-effect context.  The return
  // value is ToNumber(input).
  bool returns_original_input =
      expr->is_postfix() && !ast_context()->IsEffect();
  HValue* input = NULL;  // ToNumber(original_input).
  HValue* after = NULL;  // The result after incrementing or decrementing.

  if (proxy != NULL) {
    Variable* var = proxy->var();
    if (var->mode() == CONST)  {
      return Bailout("unsupported count operation with const");
    }
    // Argument of the count operation is a variable, not a property.
    ASSERT(prop == NULL);
    CHECK_ALIVE(VisitForValue(target));

    after = BuildIncrement(returns_original_input, expr);
    input = returns_original_input ? Top() : Pop();
    Push(after);

    switch (var->location()) {
      case Variable::UNALLOCATED:
        HandleGlobalVariableAssignment(var,
                                       after,
                                       expr->position(),
                                       expr->AssignmentId());
        break;

      case Variable::PARAMETER:
      case Variable::LOCAL:
        Bind(var, after);
        break;

      case Variable::CONTEXT: {
        // Bail out if we try to mutate a parameter value in a function
        // using the arguments object.  We do not (yet) correctly handle the
        // arguments property of the function.
        if (info()->scope()->arguments() != NULL) {
          // Parameters will rewrite to context slots.  We have no direct
          // way to detect that the variable is a parameter so we use a
          // linear search of the parameter list.
          int count = info()->scope()->num_parameters();
          for (int i = 0; i < count; ++i) {
            if (var == info()->scope()->parameter(i)) {
              return Bailout("assignment to parameter in arguments object");
            }
          }
        }

        HValue* context = BuildContextChainWalk(var);
        HStoreContextSlot::Mode mode = IsLexicalVariableMode(var->mode())
            ? HStoreContextSlot::kCheckDeoptimize : HStoreContextSlot::kNoCheck;
        HStoreContextSlot* instr =
            new(zone()) HStoreContextSlot(context, var->index(), mode, after);
        AddInstruction(instr);
        if (instr->HasObservableSideEffects()) {
          AddSimulate(expr->AssignmentId(), REMOVABLE_SIMULATE);
        }
        break;
      }

      case Variable::LOOKUP:
        return Bailout("lookup variable in count operation");
    }

  } else {
    // Argument of the count operation is a property.
    ASSERT(prop != NULL);
    prop->RecordTypeFeedback(oracle(), zone());

    if (prop->key()->IsPropertyName()) {
      // Named property.
      if (returns_original_input) Push(graph()->GetConstantUndefined());

      CHECK_ALIVE(VisitForValue(prop->obj()));
      HValue* object = Top();

      Handle<String> name = prop->key()->AsLiteral()->AsPropertyName();
      Handle<Map> map;
      HInstruction* load;
      bool monomorphic = prop->IsMonomorphic();
      if (monomorphic) {
        map = prop->GetReceiverTypes()->first();
        if (map->is_dictionary_map()) monomorphic = false;
      }
      if (monomorphic) {
        Handle<JSFunction> getter;
        Handle<JSObject> holder;
        if (LookupGetter(map, name, &getter, &holder)) {
          load = BuildCallGetter(object, map, getter, holder);
        } else {
          load = BuildLoadNamedMonomorphic(object, name, prop, map);
        }
      } else {
        load = BuildLoadNamedGeneric(object, name, prop);
      }
      PushAndAdd(load);
      if (load->HasObservableSideEffects()) {
        AddSimulate(prop->LoadId(), REMOVABLE_SIMULATE);
      }

      after = BuildIncrement(returns_original_input, expr);
      input = Pop();

      HInstruction* store;
      if (!monomorphic || map->is_observed()) {
        // If we don't know the monomorphic type, do a generic store.
        CHECK_ALIVE(store = BuildStoreNamedGeneric(object, name, after));
      } else {
        Handle<JSFunction> setter;
        Handle<JSObject> holder;
        if (LookupSetter(map, name, &setter, &holder)) {
          store = BuildCallSetter(object, after, map, setter, holder);
        } else {
          CHECK_ALIVE(store = BuildStoreNamedMonomorphic(object,
                                                         name,
                                                         after,
                                                         map));
        }
      }
      AddInstruction(store);

      // Overwrite the receiver in the bailout environment with the result
      // of the operation, and the placeholder with the original value if
      // necessary.
      environment()->SetExpressionStackAt(0, after);
      if (returns_original_input) environment()->SetExpressionStackAt(1, input);
      if (store->HasObservableSideEffects()) {
        AddSimulate(expr->AssignmentId(), REMOVABLE_SIMULATE);
      }

    } else {
      // Keyed property.
      if (returns_original_input) Push(graph()->GetConstantUndefined());

      CHECK_ALIVE(VisitForValue(prop->obj()));
      CHECK_ALIVE(VisitForValue(prop->key()));
      HValue* obj = environment()->ExpressionStackAt(1);
      HValue* key = environment()->ExpressionStackAt(0);

      bool has_side_effects = false;
      HValue* load = HandleKeyedElementAccess(
          obj, key, NULL, prop, prop->LoadId(), RelocInfo::kNoPosition,
          false,  // is_store
          &has_side_effects);
      Push(load);
      if (has_side_effects) AddSimulate(prop->LoadId(), REMOVABLE_SIMULATE);

      after = BuildIncrement(returns_original_input, expr);
      input = environment()->ExpressionStackAt(0);

      expr->RecordTypeFeedback(oracle(), zone());
      HandleKeyedElementAccess(obj, key, after, expr, expr->AssignmentId(),
                               RelocInfo::kNoPosition,
                               true,  // is_store
                               &has_side_effects);

      // Drop the key and the original value from the bailout environment.
      // Overwrite the receiver with the result of the operation, and the
      // placeholder with the original value if necessary.
      Drop(2);
      environment()->SetExpressionStackAt(0, after);
      if (returns_original_input) environment()->SetExpressionStackAt(1, input);
      ASSERT(has_side_effects);  // Stores always have side effects.
      AddSimulate(expr->AssignmentId(), REMOVABLE_SIMULATE);
    }
  }

  Drop(returns_original_input ? 2 : 1);
  return ast_context()->ReturnValue(expr->is_postfix() ? input : after);
}


HInstruction* HOptimizedGraphBuilder::BuildStringCharCodeAt(
    HValue* context,
    HValue* string,
    HValue* index) {
  if (string->IsConstant() && index->IsConstant()) {
    HConstant* c_string = HConstant::cast(string);
    HConstant* c_index = HConstant::cast(index);
    if (c_string->HasStringValue() && c_index->HasNumberValue()) {
      int32_t i = c_index->NumberValueAsInteger32();
      Handle<String> s = c_string->StringValue();
      if (i < 0 || i >= s->length()) {
        return new(zone()) HConstant(OS::nan_value(), Representation::Double());
      }
      return new(zone()) HConstant(s->Get(i), Representation::Integer32());
    }
  }
  AddInstruction(new(zone()) HCheckNonSmi(string));
  AddInstruction(HCheckInstanceType::NewIsString(string, zone()));
  HInstruction* length = HStringLength::New(zone(), string);
  AddInstruction(length);
  HInstruction* checked_index = AddBoundsCheck(index, length);
  return new(zone()) HStringCharCodeAt(context, string, checked_index);
}

// Checks if the given shift amounts have form: (sa) and (32 - sa).
static bool ShiftAmountsAllowReplaceByRotate(HValue* sa,
                                             HValue* const32_minus_sa) {
  if (!const32_minus_sa->IsSub()) return false;
  HSub* sub = HSub::cast(const32_minus_sa);
  if (sa != sub->right()) return false;
  HValue* const32 = sub->left();
  if (!const32->IsConstant() ||
      HConstant::cast(const32)->Integer32Value() != 32) {
    return false;
  }
  return (sub->right() == sa);
}


// Checks if the left and the right are shift instructions with the oposite
// directions that can be replaced by one rotate right instruction or not.
// Returns the operand and the shift amount for the rotate instruction in the
// former case.
bool HOptimizedGraphBuilder::MatchRotateRight(HValue* left,
                                              HValue* right,
                                              HValue** operand,
                                              HValue** shift_amount) {
  HShl* shl;
  HShr* shr;
  if (left->IsShl() && right->IsShr()) {
    shl = HShl::cast(left);
    shr = HShr::cast(right);
  } else if (left->IsShr() && right->IsShl()) {
    shl = HShl::cast(right);
    shr = HShr::cast(left);
  } else {
    return false;
  }
  if (shl->left() != shr->left()) return false;

  if (!ShiftAmountsAllowReplaceByRotate(shl->right(), shr->right()) &&
      !ShiftAmountsAllowReplaceByRotate(shr->right(), shl->right())) {
    return false;
  }
  *operand= shr->left();
  *shift_amount = shr->right();
  return true;
}


bool CanBeZero(HValue *right) {
  if (right->IsConstant()) {
    HConstant* right_const = HConstant::cast(right);
    if (right_const->HasInteger32Value() &&
       (right_const->Integer32Value() & 0x1f) != 0) {
      return false;
    }
  }
  return true;
}


HInstruction* HOptimizedGraphBuilder::BuildBinaryOperation(
    BinaryOperation* expr,
    HValue* left,
    HValue* right) {
  HValue* context = environment()->LookupContext();
  TypeInfo left_info, right_info, result_info, combined_info;
  oracle()->BinaryType(expr, &left_info, &right_info, &result_info);
  Representation left_rep = ToRepresentation(left_info);
  Representation right_rep = ToRepresentation(right_info);
  Representation result_rep = ToRepresentation(result_info);
  if (left_info.IsUninitialized()) {
    // Can't have initialized one but not the other.
    ASSERT(right_info.IsUninitialized());
    AddSoftDeoptimize();
    left_info = right_info = TypeInfo::Unknown();
  }
  HInstruction* instr = NULL;
  switch (expr->op()) {
    case Token::ADD:
      if (left_info.IsString() && right_info.IsString()) {
        AddInstruction(new(zone()) HCheckNonSmi(left));
        AddInstruction(HCheckInstanceType::NewIsString(left, zone()));
        AddInstruction(new(zone()) HCheckNonSmi(right));
        AddInstruction(HCheckInstanceType::NewIsString(right, zone()));
        instr = HStringAdd::New(zone(), context, left, right);
      } else {
        instr = HAdd::New(zone(), context, left, right);
      }
      break;
    case Token::SUB:
      instr = HSub::New(zone(), context, left, right);
      break;
    case Token::MUL:
      instr = HMul::New(zone(), context, left, right);
      break;
    case Token::MOD:
      instr = HMod::New(zone(), context, left, right);
      break;
    case Token::DIV:
      instr = HDiv::New(zone(), context, left, right);
      break;
    case Token::BIT_XOR:
    case Token::BIT_AND:
      instr = HBitwise::New(zone(), expr->op(), context, left, right);
      break;
    case Token::BIT_OR: {
      HValue* operand, *shift_amount;
      if (left_info.IsInteger32() && right_info.IsInteger32() &&
          MatchRotateRight(left, right, &operand, &shift_amount)) {
        instr = new(zone()) HRor(context, operand, shift_amount);
      } else {
        instr = HBitwise::New(zone(), expr->op(), context, left, right);
      }
      break;
    }
    case Token::SAR:
      instr = HSar::New(zone(), context, left, right);
      break;
    case Token::SHR:
      instr = HShr::New(zone(), context, left, right);
      if (FLAG_opt_safe_uint32_operations && instr->IsShr() &&
          CanBeZero(right)) {
        graph()->RecordUint32Instruction(instr);
      }
      break;
    case Token::SHL:
      instr = HShl::New(zone(), context, left, right);
      break;
    default:
      UNREACHABLE();
  }

  if (instr->IsBinaryOperation()) {
    HBinaryOperation* binop = HBinaryOperation::cast(instr);
    binop->set_observed_input_representation(1, left_rep);
    binop->set_observed_input_representation(2, right_rep);
    binop->initialize_output_representation(result_rep);
  }
  return instr;
}


// Check for the form (%_ClassOf(foo) === 'BarClass').
static bool IsClassOfTest(CompareOperation* expr) {
  if (expr->op() != Token::EQ_STRICT) return false;
  CallRuntime* call = expr->left()->AsCallRuntime();
  if (call == NULL) return false;
  Literal* literal = expr->right()->AsLiteral();
  if (literal == NULL) return false;
  if (!literal->handle()->IsString()) return false;
  if (!call->name()->IsOneByteEqualTo(STATIC_ASCII_VECTOR("_ClassOf"))) {
    return false;
  }
  ASSERT(call->arguments()->length() == 1);
  return true;
}


void HOptimizedGraphBuilder::VisitBinaryOperation(BinaryOperation* expr) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  switch (expr->op()) {
    case Token::COMMA:
      return VisitComma(expr);
    case Token::OR:
    case Token::AND:
      return VisitLogicalExpression(expr);
    default:
      return VisitArithmeticExpression(expr);
  }
}


void HOptimizedGraphBuilder::VisitComma(BinaryOperation* expr) {
  CHECK_ALIVE(VisitForEffect(expr->left()));
  // Visit the right subexpression in the same AST context as the entire
  // expression.
  Visit(expr->right());
}


void HOptimizedGraphBuilder::VisitLogicalExpression(BinaryOperation* expr) {
  bool is_logical_and = expr->op() == Token::AND;
  if (ast_context()->IsTest()) {
    TestContext* context = TestContext::cast(ast_context());
    // Translate left subexpression.
    HBasicBlock* eval_right = graph()->CreateBasicBlock();
    if (is_logical_and) {
      CHECK_BAILOUT(VisitForControl(expr->left(),
                                    eval_right,
                                    context->if_false()));
    } else {
      CHECK_BAILOUT(VisitForControl(expr->left(),
                                    context->if_true(),
                                    eval_right));
    }

    // Translate right subexpression by visiting it in the same AST
    // context as the entire expression.
    if (eval_right->HasPredecessor()) {
      eval_right->SetJoinId(expr->RightId());
      set_current_block(eval_right);
      Visit(expr->right());
    }

  } else if (ast_context()->IsValue()) {
    CHECK_ALIVE(VisitForValue(expr->left()));
    ASSERT(current_block() != NULL);
    HValue* left_value = Top();

    if (left_value->IsConstant()) {
      HConstant* left_constant = HConstant::cast(left_value);
      if ((is_logical_and && left_constant->BooleanValue()) ||
          (!is_logical_and && !left_constant->BooleanValue())) {
        Drop(1);  // left_value.
        CHECK_BAILOUT(VisitForValue(expr->right()));
      }
      return ast_context()->ReturnValue(Pop());
    }

    // We need an extra block to maintain edge-split form.
    HBasicBlock* empty_block = graph()->CreateBasicBlock();
    HBasicBlock* eval_right = graph()->CreateBasicBlock();
    TypeFeedbackId test_id = expr->left()->test_id();
    ToBooleanStub::Types expected(oracle()->ToBooleanTypes(test_id));
    HBranch* test = is_logical_and
      ? new(zone()) HBranch(left_value, eval_right, empty_block, expected)
      : new(zone()) HBranch(left_value, empty_block, eval_right, expected);
    current_block()->Finish(test);

    set_current_block(eval_right);
    Drop(1);  // Value of the left subexpression.
    CHECK_BAILOUT(VisitForValue(expr->right()));

    HBasicBlock* join_block =
      CreateJoin(empty_block, current_block(), expr->id());
    set_current_block(join_block);
    return ast_context()->ReturnValue(Pop());

  } else {
    ASSERT(ast_context()->IsEffect());
    // In an effect context, we don't need the value of the left subexpression,
    // only its control flow and side effects.  We need an extra block to
    // maintain edge-split form.
    HBasicBlock* empty_block = graph()->CreateBasicBlock();
    HBasicBlock* right_block = graph()->CreateBasicBlock();
    if (is_logical_and) {
      CHECK_BAILOUT(VisitForControl(expr->left(), right_block, empty_block));
    } else {
      CHECK_BAILOUT(VisitForControl(expr->left(), empty_block, right_block));
    }

    // TODO(kmillikin): Find a way to fix this.  It's ugly that there are
    // actually two empty blocks (one here and one inserted by
    // TestContext::BuildBranch, and that they both have an HSimulate though the
    // second one is not a merge node, and that we really have no good AST ID to
    // put on that first HSimulate.

    if (empty_block->HasPredecessor()) {
      empty_block->SetJoinId(expr->id());
    } else {
      empty_block = NULL;
    }

    if (right_block->HasPredecessor()) {
      right_block->SetJoinId(expr->RightId());
      set_current_block(right_block);
      CHECK_BAILOUT(VisitForEffect(expr->right()));
      right_block = current_block();
    } else {
      right_block = NULL;
    }

    HBasicBlock* join_block =
      CreateJoin(empty_block, right_block, expr->id());
    set_current_block(join_block);
    // We did not materialize any value in the predecessor environments,
    // so there is no need to handle it here.
  }
}


void HOptimizedGraphBuilder::VisitArithmeticExpression(BinaryOperation* expr) {
  CHECK_ALIVE(VisitForValue(expr->left()));
  CHECK_ALIVE(VisitForValue(expr->right()));
  HValue* right = Pop();
  HValue* left = Pop();
  HInstruction* instr = BuildBinaryOperation(expr, left, right);
  instr->set_position(expr->position());
  return ast_context()->ReturnInstruction(instr, expr->id());
}


Representation HOptimizedGraphBuilder::ToRepresentation(TypeInfo info) {
  if (info.IsUninitialized()) return Representation::None();
  if (info.IsSmi()) return Representation::Integer32();
  if (info.IsInteger32()) return Representation::Integer32();
  if (info.IsDouble()) return Representation::Double();
  if (info.IsNumber()) return Representation::Double();
  return Representation::Tagged();
}


void HOptimizedGraphBuilder::HandleLiteralCompareTypeof(CompareOperation* expr,
                                                        Expression* sub_expr,
                                                        Handle<String> check) {
  CHECK_ALIVE(VisitForTypeOf(sub_expr));
  HValue* value = Pop();
  HTypeofIsAndBranch* instr = new(zone()) HTypeofIsAndBranch(value, check);
  instr->set_position(expr->position());
  return ast_context()->ReturnControl(instr, expr->id());
}


static bool IsLiteralCompareBool(HValue* left,
                                 Token::Value op,
                                 HValue* right) {
  return op == Token::EQ_STRICT &&
      ((left->IsConstant() && HConstant::cast(left)->handle()->IsBoolean()) ||
       (right->IsConstant() && HConstant::cast(right)->handle()->IsBoolean()));
}


void HOptimizedGraphBuilder::VisitCompareOperation(CompareOperation* expr) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());

  // Check for a few fast cases. The AST visiting behavior must be in sync
  // with the full codegen: We don't push both left and right values onto
  // the expression stack when one side is a special-case literal.
  Expression* sub_expr = NULL;
  Handle<String> check;
  if (expr->IsLiteralCompareTypeof(&sub_expr, &check)) {
    return HandleLiteralCompareTypeof(expr, sub_expr, check);
  }
  if (expr->IsLiteralCompareUndefined(&sub_expr, isolate())) {
    return HandleLiteralCompareNil(expr, sub_expr, kUndefinedValue);
  }
  if (expr->IsLiteralCompareNull(&sub_expr)) {
    return HandleLiteralCompareNil(expr, sub_expr, kNullValue);
  }

  if (IsClassOfTest(expr)) {
    CallRuntime* call = expr->left()->AsCallRuntime();
    ASSERT(call->arguments()->length() == 1);
    CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
    HValue* value = Pop();
    Literal* literal = expr->right()->AsLiteral();
    Handle<String> rhs = Handle<String>::cast(literal->handle());
    HClassOfTestAndBranch* instr =
        new(zone()) HClassOfTestAndBranch(value, rhs);
    instr->set_position(expr->position());
    return ast_context()->ReturnControl(instr, expr->id());
  }

  TypeInfo left_type, right_type, overall_type_info;
  oracle()->CompareType(expr, &left_type, &right_type, &overall_type_info);
  Representation combined_rep = ToRepresentation(overall_type_info);
  Representation left_rep = ToRepresentation(left_type);
  Representation right_rep = ToRepresentation(right_type);
  // Check if this expression was ever executed according to type feedback.
  // Note that for the special typeof/null/undefined cases we get unknown here.
  if (overall_type_info.IsUninitialized()) {
    AddSoftDeoptimize();
    overall_type_info = left_type = right_type = TypeInfo::Unknown();
  }

  CHECK_ALIVE(VisitForValue(expr->left()));
  CHECK_ALIVE(VisitForValue(expr->right()));

  HValue* context = environment()->LookupContext();
  HValue* right = Pop();
  HValue* left = Pop();
  Token::Value op = expr->op();

  if (IsLiteralCompareBool(left, op, right)) {
    HCompareObjectEqAndBranch* result =
        new(zone()) HCompareObjectEqAndBranch(left, right);
    result->set_position(expr->position());
    return ast_context()->ReturnControl(result, expr->id());
  }

  if (op == Token::INSTANCEOF) {
    // Check to see if the rhs of the instanceof is a global function not
    // residing in new space. If it is we assume that the function will stay the
    // same.
    Handle<JSFunction> target = Handle<JSFunction>::null();
    VariableProxy* proxy = expr->right()->AsVariableProxy();
    bool global_function = (proxy != NULL) && proxy->var()->IsUnallocated();
    if (global_function &&
        info()->has_global_object() &&
        !info()->global_object()->IsAccessCheckNeeded()) {
      Handle<String> name = proxy->name();
      Handle<GlobalObject> global(info()->global_object());
      LookupResult lookup(isolate());
      global->Lookup(*name, &lookup);
      if (lookup.IsNormal() && lookup.GetValue()->IsJSFunction()) {
        Handle<JSFunction> candidate(JSFunction::cast(lookup.GetValue()));
        // If the function is in new space we assume it's more likely to
        // change and thus prefer the general IC code.
        if (!isolate()->heap()->InNewSpace(*candidate)) {
          target = candidate;
        }
      }
    }

    // If the target is not null we have found a known global function that is
    // assumed to stay the same for this instanceof.
    if (target.is_null()) {
      HInstanceOf* result = new(zone()) HInstanceOf(context, left, right);
      result->set_position(expr->position());
      return ast_context()->ReturnInstruction(result, expr->id());
    } else {
      AddInstruction(new(zone()) HCheckFunction(right, target));
      HInstanceOfKnownGlobal* result =
          new(zone()) HInstanceOfKnownGlobal(context, left, target);
      result->set_position(expr->position());
      return ast_context()->ReturnInstruction(result, expr->id());
    }
  } else if (op == Token::IN) {
    HIn* result = new(zone()) HIn(context, left, right);
    result->set_position(expr->position());
    return ast_context()->ReturnInstruction(result, expr->id());
  } else if (overall_type_info.IsNonPrimitive()) {
    switch (op) {
      case Token::EQ:
      case Token::EQ_STRICT: {
        // Can we get away with map check and not instance type check?
        Handle<Map> map = oracle()->GetCompareMap(expr);
        if (!map.is_null()) {
          AddCheckMapsWithTransitions(left, map);
          AddCheckMapsWithTransitions(right, map);
          HCompareObjectEqAndBranch* result =
              new(zone()) HCompareObjectEqAndBranch(left, right);
          result->set_position(expr->position());
          return ast_context()->ReturnControl(result, expr->id());
        } else {
          AddInstruction(new(zone()) HCheckNonSmi(left));
          AddInstruction(HCheckInstanceType::NewIsSpecObject(left, zone()));
          AddInstruction(new(zone()) HCheckNonSmi(right));
          AddInstruction(HCheckInstanceType::NewIsSpecObject(right, zone()));
          HCompareObjectEqAndBranch* result =
              new(zone()) HCompareObjectEqAndBranch(left, right);
          result->set_position(expr->position());
          return ast_context()->ReturnControl(result, expr->id());
        }
      }
      default:
        return Bailout("Unsupported non-primitive compare");
    }
  } else if (overall_type_info.IsInternalizedString() &&
             Token::IsEqualityOp(op)) {
    AddInstruction(new(zone()) HCheckNonSmi(left));
    AddInstruction(HCheckInstanceType::NewIsInternalizedString(left, zone()));
    AddInstruction(new(zone()) HCheckNonSmi(right));
    AddInstruction(HCheckInstanceType::NewIsInternalizedString(right, zone()));
    HCompareObjectEqAndBranch* result =
        new(zone()) HCompareObjectEqAndBranch(left, right);
    result->set_position(expr->position());
    return ast_context()->ReturnControl(result, expr->id());
  } else {
    if (combined_rep.IsTagged() || combined_rep.IsNone()) {
      HCompareGeneric* result =
          new(zone()) HCompareGeneric(context, left, right, op);
      result->set_observed_input_representation(1, left_rep);
      result->set_observed_input_representation(2, right_rep);
      result->set_position(expr->position());
      return ast_context()->ReturnInstruction(result, expr->id());
    } else {
      HCompareIDAndBranch* result =
          new(zone()) HCompareIDAndBranch(left, right, op);
      result->set_observed_input_representation(left_rep, right_rep);
      result->set_position(expr->position());
      return ast_context()->ReturnControl(result, expr->id());
    }
  }
}


void HOptimizedGraphBuilder::HandleLiteralCompareNil(CompareOperation* expr,
                                                     Expression* sub_expr,
                                                     NilValue nil) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  CHECK_ALIVE(VisitForValue(sub_expr));
  HValue* value = Pop();
  EqualityKind kind =
      expr->op() == Token::EQ_STRICT ? kStrictEquality : kNonStrictEquality;
  HIfContinuation continuation;
  TypeFeedbackId id = expr->CompareOperationFeedbackId();
  CompareNilICStub::Types types;
  if (kind == kStrictEquality) {
    if (nil == kNullValue) {
      types = CompareNilICStub::kCompareAgainstNull;
    } else {
      types = CompareNilICStub::kCompareAgainstUndefined;
    }
  } else {
    types = static_cast<CompareNilICStub::Types>(
        oracle()->CompareNilTypes(id));
    if (types == 0) types = CompareNilICStub::kFullCompare;
  }
  Handle<Map> map_handle(oracle()->CompareNilMonomorphicReceiverType(id));
  BuildCompareNil(value, kind, types, map_handle,
                  expr->position(), &continuation);
  return ast_context()->ReturnContinuation(&continuation, expr->id());
}


HInstruction* HOptimizedGraphBuilder::BuildThisFunction() {
  // If we share optimized code between different closures, the
  // this-function is not a constant, except inside an inlined body.
  if (function_state()->outer() != NULL) {
      return new(zone()) HConstant(
          function_state()->compilation_info()->closure(),
          Representation::Tagged());
  } else {
      return new(zone()) HThisFunction;
  }
}


HInstruction* HOptimizedGraphBuilder::BuildFastLiteral(
    HValue* context,
    Handle<JSObject> boilerplate_object,
    Handle<JSObject> original_boilerplate_object,
    int data_size,
    int pointer_size,
    AllocationSiteMode mode) {
  Zone* zone = this->zone();
  int total_size = data_size + pointer_size;

  NoObservableSideEffectsScope no_effects(this);

  HAllocate::Flags flags = HAllocate::CAN_ALLOCATE_IN_NEW_SPACE;
  // TODO(hpayer): add support for old data space
  if (FLAG_pretenure_literals &&
      isolate()->heap()->ShouldGloballyPretenure() &&
      data_size == 0) {
    flags = static_cast<HAllocate::Flags>(
        flags | HAllocate::CAN_ALLOCATE_IN_OLD_POINTER_SPACE);
  }

  HValue* size_in_bytes =
      AddInstruction(new(zone) HConstant(total_size,
          Representation::Integer32()));
  HInstruction* result =
      AddInstruction(new(zone) HAllocate(context,
                                         size_in_bytes,
                                         HType::JSObject(),
                                         flags));
  int offset = 0;
  BuildEmitDeepCopy(boilerplate_object, original_boilerplate_object, result,
                    &offset, mode);
  return result;
}


void HOptimizedGraphBuilder::BuildEmitDeepCopy(
    Handle<JSObject> boilerplate_object,
    Handle<JSObject> original_boilerplate_object,
    HInstruction* target,
    int* offset,
    AllocationSiteMode mode) {
  Zone* zone = this->zone();
  Factory* factory = isolate()->factory();

  HInstruction* original_boilerplate = AddInstruction(new(zone) HConstant(
      original_boilerplate_object, Representation::Tagged()));

  bool create_allocation_site_info = mode == TRACK_ALLOCATION_SITE &&
      boilerplate_object->map()->CanTrackAllocationSite();

  // Only elements backing stores for non-COW arrays need to be copied.
  Handle<FixedArrayBase> elements(boilerplate_object->elements());
  Handle<FixedArrayBase> original_elements(
      original_boilerplate_object->elements());
  ElementsKind kind = boilerplate_object->map()->elements_kind();

  // Increase the offset so that subsequent objects end up right after
  // this object and its backing store.
  int object_offset = *offset;
  int object_size = boilerplate_object->map()->instance_size();
  int elements_size = (elements->length() > 0 &&
      elements->map() != isolate()->heap()->fixed_cow_array_map()) ?
          elements->Size() : 0;
  int elements_offset = *offset + object_size;
  int inobject_properties = boilerplate_object->map()->inobject_properties();
  if (create_allocation_site_info) {
    elements_offset += AllocationSiteInfo::kSize;
    *offset += AllocationSiteInfo::kSize;
  }

  *offset += object_size + elements_size;

  HValue* object_elements = BuildCopyObjectHeader(boilerplate_object, target,
      object_offset, elements_offset, elements_size);

  // Copy in-object properties.
  HValue* object_properties =
      AddInstruction(new(zone) HInnerAllocatedObject(target, object_offset));
  for (int i = 0; i < inobject_properties; i++) {
    Handle<Object> value =
        Handle<Object>(boilerplate_object->InObjectPropertyAt(i),
        isolate());
    if (value->IsJSObject()) {
      Handle<JSObject> value_object = Handle<JSObject>::cast(value);
      Handle<JSObject> original_value_object = Handle<JSObject>::cast(
          Handle<Object>(original_boilerplate_object->InObjectPropertyAt(i),
              isolate()));
      HInstruction* value_instruction =
          AddInstruction(new(zone) HInnerAllocatedObject(target, *offset));
      // TODO(verwaest): choose correct storage.
      AddInstruction(new(zone) HStoreNamedField(
          object_properties, factory->unknown_field_string(), value_instruction,
          true, Representation::Tagged(),
          boilerplate_object->GetInObjectPropertyOffset(i)));
      BuildEmitDeepCopy(value_object, original_value_object, target,
                        offset, DONT_TRACK_ALLOCATION_SITE);
    } else {
      // TODO(verwaest): choose correct storage.
      HInstruction* value_instruction = AddInstruction(new(zone) HConstant(
          value, Representation::Tagged()));
      AddInstruction(new(zone) HStoreNamedField(
          object_properties, factory->unknown_field_string(), value_instruction,
          true, Representation::Tagged(),
          boilerplate_object->GetInObjectPropertyOffset(i)));
    }
  }

  // Build Allocation Site Info if desired
  if (create_allocation_site_info) {
    BuildCreateAllocationSiteInfo(target, JSArray::kSize, original_boilerplate);
  }

  if (object_elements != NULL) {
    HInstruction* boilerplate_elements = AddInstruction(new(zone) HConstant(
        elements, Representation::Tagged()));

    int elements_length = elements->length();
    HValue* object_elements_length =
        AddInstruction(new(zone) HConstant(
            elements_length, Representation::Integer32()));

    BuildInitializeElements(object_elements, kind, object_elements_length);

    // Copy elements backing store content.
    if (elements->IsFixedDoubleArray()) {
      for (int i = 0; i < elements_length; i++) {
        HValue* key_constant =
            AddInstruction(new(zone) HConstant(i, Representation::Integer32()));
        HInstruction* value_instruction =
            AddInstruction(new(zone) HLoadKeyed(
                boilerplate_elements, key_constant, NULL, kind,
                ALLOW_RETURN_HOLE));
        HInstruction* store = AddInstruction(new(zone) HStoreKeyed(
                object_elements, key_constant, value_instruction, kind));
        store->ClearFlag(HValue::kDeoptimizeOnUndefined);
      }
    } else if (elements->IsFixedArray()) {
      Handle<FixedArray> fast_elements = Handle<FixedArray>::cast(elements);
      Handle<FixedArray> original_fast_elements =
          Handle<FixedArray>::cast(original_elements);
      for (int i = 0; i < elements_length; i++) {
        Handle<Object> value(fast_elements->get(i), isolate());
        HValue* key_constant =
            AddInstruction(new(zone) HConstant(i, Representation::Integer32()));
        if (value->IsJSObject()) {
          Handle<JSObject> value_object = Handle<JSObject>::cast(value);
          Handle<JSObject> original_value_object = Handle<JSObject>::cast(
              Handle<Object>(original_fast_elements->get(i), isolate()));
          HInstruction* value_instruction =
              AddInstruction(new(zone) HInnerAllocatedObject(target, *offset));
          AddInstruction(new(zone) HStoreKeyed(
              object_elements, key_constant, value_instruction, kind));
          BuildEmitDeepCopy(value_object, original_value_object, target,
              offset, DONT_TRACK_ALLOCATION_SITE);
        } else {
          HInstruction* value_instruction =
              AddInstruction(new(zone) HLoadKeyed(
                  boilerplate_elements, key_constant, NULL, kind,
                  ALLOW_RETURN_HOLE));
          AddInstruction(new(zone) HStoreKeyed(
              object_elements, key_constant, value_instruction, kind));
        }
      }
    } else {
      UNREACHABLE();
    }
  }
}


HValue* HOptimizedGraphBuilder::BuildCopyObjectHeader(
    Handle<JSObject> boilerplate_object,
    HInstruction* target,
    int object_offset,
    int elements_offset,
    int elements_size) {
  ASSERT(boilerplate_object->properties()->length() == 0);
  Zone* zone = this->zone();
  Factory* factory = isolate()->factory();
  HValue* result = NULL;

  HValue* object_header =
      AddInstruction(new(zone) HInnerAllocatedObject(target, object_offset));
  Handle<Map> boilerplate_object_map(boilerplate_object->map());
  BuildStoreMap(object_header, boilerplate_object_map);

  HInstruction* elements;
  if (elements_size == 0) {
    Handle<Object> elements_field =
        Handle<Object>(boilerplate_object->elements(), isolate());
    elements = AddInstruction(new(zone) HConstant(
        elements_field, Representation::Tagged()));
  } else {
    elements = AddInstruction(new(zone) HInnerAllocatedObject(
        target, elements_offset));
    result = elements;
  }
  HInstruction* elements_store = AddInstruction(new(zone) HStoreNamedField(
      object_header,
      factory->elements_field_string(),
      elements,
      true, Representation::Tagged(), JSObject::kElementsOffset));
  elements_store->SetGVNFlag(kChangesElementsPointer);

  Handle<Object> properties_field =
      Handle<Object>(boilerplate_object->properties(), isolate());
  ASSERT(*properties_field == isolate()->heap()->empty_fixed_array());
  HInstruction* properties = AddInstruction(new(zone) HConstant(
      properties_field, Representation::None()));
  AddInstruction(new(zone) HStoreNamedField(object_header,
                                            factory->empty_string(),
                                            properties, true,
                                            Representation::Tagged(),
                                            JSObject::kPropertiesOffset));

  if (boilerplate_object->IsJSArray()) {
    Handle<JSArray> boilerplate_array =
        Handle<JSArray>::cast(boilerplate_object);
    Handle<Object> length_field =
        Handle<Object>(boilerplate_array->length(), isolate());
    HInstruction* length = AddInstruction(new(zone) HConstant(
        length_field, Representation::None()));
    ASSERT(boilerplate_array->length()->IsSmi());
    Representation representation =
        IsFastElementsKind(boilerplate_array->GetElementsKind())
        ? Representation::Smi() : Representation::Tagged();
    HInstruction* length_store = AddInstruction(new(zone) HStoreNamedField(
        object_header,
        factory->length_field_string(),
        length,
        true, representation, JSArray::kLengthOffset));
    length_store->SetGVNFlag(kChangesArrayLengths);
  }

  return result;
}


void HOptimizedGraphBuilder::VisitThisFunction(ThisFunction* expr) {
  ASSERT(!HasStackOverflow());
  ASSERT(current_block() != NULL);
  ASSERT(current_block()->HasPredecessor());
  HInstruction* instr = BuildThisFunction();
  return ast_context()->ReturnInstruction(instr, expr->id());
}


void HOptimizedGraphBuilder::VisitDeclarations(
    ZoneList<Declaration*>* declarations) {
  ASSERT(globals_.is_empty());
  AstVisitor::VisitDeclarations(declarations);
  if (!globals_.is_empty()) {
    Handle<FixedArray> array =
       isolate()->factory()->NewFixedArray(globals_.length(), TENURED);
    for (int i = 0; i < globals_.length(); ++i) array->set(i, *globals_.at(i));
    int flags = DeclareGlobalsEvalFlag::encode(info()->is_eval()) |
                DeclareGlobalsNativeFlag::encode(info()->is_native()) |
                DeclareGlobalsLanguageMode::encode(info()->language_mode());
    HInstruction* result = new(zone()) HDeclareGlobals(
        environment()->LookupContext(), array, flags);
    AddInstruction(result);
    globals_.Clear();
  }
}


void HOptimizedGraphBuilder::VisitVariableDeclaration(
    VariableDeclaration* declaration) {
  VariableProxy* proxy = declaration->proxy();
  VariableMode mode = declaration->mode();
  Variable* variable = proxy->var();
  bool hole_init = mode == CONST || mode == CONST_HARMONY || mode == LET;
  switch (variable->location()) {
    case Variable::UNALLOCATED:
      globals_.Add(variable->name(), zone());
      globals_.Add(variable->binding_needs_init()
                       ? isolate()->factory()->the_hole_value()
                       : isolate()->factory()->undefined_value(), zone());
      return;
    case Variable::PARAMETER:
    case Variable::LOCAL:
      if (hole_init) {
        HValue* value = graph()->GetConstantHole();
        environment()->Bind(variable, value);
      }
      break;
    case Variable::CONTEXT:
      if (hole_init) {
        HValue* value = graph()->GetConstantHole();
        HValue* context = environment()->LookupContext();
        HStoreContextSlot* store = new(zone()) HStoreContextSlot(
            context, variable->index(), HStoreContextSlot::kNoCheck, value);
        AddInstruction(store);
        if (store->HasObservableSideEffects()) {
          AddSimulate(proxy->id(), REMOVABLE_SIMULATE);
        }
      }
      break;
    case Variable::LOOKUP:
      return Bailout("unsupported lookup slot in declaration");
  }
}


void HOptimizedGraphBuilder::VisitFunctionDeclaration(
    FunctionDeclaration* declaration) {
  VariableProxy* proxy = declaration->proxy();
  Variable* variable = proxy->var();
  switch (variable->location()) {
    case Variable::UNALLOCATED: {
      globals_.Add(variable->name(), zone());
      Handle<SharedFunctionInfo> function =
          Compiler::BuildFunctionInfo(declaration->fun(), info()->script());
      // Check for stack-overflow exception.
      if (function.is_null()) return SetStackOverflow();
      globals_.Add(function, zone());
      return;
    }
    case Variable::PARAMETER:
    case Variable::LOCAL: {
      CHECK_ALIVE(VisitForValue(declaration->fun()));
      HValue* value = Pop();
      environment()->Bind(variable, value);
      break;
    }
    case Variable::CONTEXT: {
      CHECK_ALIVE(VisitForValue(declaration->fun()));
      HValue* value = Pop();
      HValue* context = environment()->LookupContext();
      HStoreContextSlot* store = new(zone()) HStoreContextSlot(
          context, variable->index(), HStoreContextSlot::kNoCheck, value);
      AddInstruction(store);
      if (store->HasObservableSideEffects()) {
        AddSimulate(proxy->id(), REMOVABLE_SIMULATE);
      }
      break;
    }
    case Variable::LOOKUP:
      return Bailout("unsupported lookup slot in declaration");
  }
}


void HOptimizedGraphBuilder::VisitModuleDeclaration(
    ModuleDeclaration* declaration) {
  UNREACHABLE();
}


void HOptimizedGraphBuilder::VisitImportDeclaration(
    ImportDeclaration* declaration) {
  UNREACHABLE();
}


void HOptimizedGraphBuilder::VisitExportDeclaration(
    ExportDeclaration* declaration) {
  UNREACHABLE();
}


void HOptimizedGraphBuilder::VisitModuleLiteral(ModuleLiteral* module) {
  UNREACHABLE();
}


void HOptimizedGraphBuilder::VisitModuleVariable(ModuleVariable* module) {
  UNREACHABLE();
}


void HOptimizedGraphBuilder::VisitModulePath(ModulePath* module) {
  UNREACHABLE();
}


void HOptimizedGraphBuilder::VisitModuleUrl(ModuleUrl* module) {
  UNREACHABLE();
}


void HOptimizedGraphBuilder::VisitModuleStatement(ModuleStatement* stmt) {
  UNREACHABLE();
}


// Generators for inline runtime functions.
// Support for types.
void HOptimizedGraphBuilder::GenerateIsSmi(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  HValue* value = Pop();
  HIsSmiAndBranch* result = new(zone()) HIsSmiAndBranch(value);
  return ast_context()->ReturnControl(result, call->id());
}


void HOptimizedGraphBuilder::GenerateIsSpecObject(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  HValue* value = Pop();
  HHasInstanceTypeAndBranch* result =
      new(zone()) HHasInstanceTypeAndBranch(value,
                                            FIRST_SPEC_OBJECT_TYPE,
                                            LAST_SPEC_OBJECT_TYPE);
  return ast_context()->ReturnControl(result, call->id());
}


void HOptimizedGraphBuilder::GenerateIsFunction(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  HValue* value = Pop();
  HHasInstanceTypeAndBranch* result =
      new(zone()) HHasInstanceTypeAndBranch(value, JS_FUNCTION_TYPE);
  return ast_context()->ReturnControl(result, call->id());
}


void HOptimizedGraphBuilder::GenerateHasCachedArrayIndex(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  HValue* value = Pop();
  HHasCachedArrayIndexAndBranch* result =
      new(zone()) HHasCachedArrayIndexAndBranch(value);
  return ast_context()->ReturnControl(result, call->id());
}


void HOptimizedGraphBuilder::GenerateIsArray(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  HValue* value = Pop();
  HHasInstanceTypeAndBranch* result =
      new(zone()) HHasInstanceTypeAndBranch(value, JS_ARRAY_TYPE);
  return ast_context()->ReturnControl(result, call->id());
}


void HOptimizedGraphBuilder::GenerateIsRegExp(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  HValue* value = Pop();
  HHasInstanceTypeAndBranch* result =
      new(zone()) HHasInstanceTypeAndBranch(value, JS_REGEXP_TYPE);
  return ast_context()->ReturnControl(result, call->id());
}


void HOptimizedGraphBuilder::GenerateIsObject(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  HValue* value = Pop();
  HIsObjectAndBranch* result = new(zone()) HIsObjectAndBranch(value);
  return ast_context()->ReturnControl(result, call->id());
}


void HOptimizedGraphBuilder::GenerateIsNonNegativeSmi(CallRuntime* call) {
  return Bailout("inlined runtime function: IsNonNegativeSmi");
}


void HOptimizedGraphBuilder::GenerateIsUndetectableObject(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  HValue* value = Pop();
  HIsUndetectableAndBranch* result =
      new(zone()) HIsUndetectableAndBranch(value);
  return ast_context()->ReturnControl(result, call->id());
}


void HOptimizedGraphBuilder::GenerateIsStringWrapperSafeForDefaultValueOf(
    CallRuntime* call) {
  return Bailout(
      "inlined runtime function: IsStringWrapperSafeForDefaultValueOf");
}


// Support for construct call checks.
void HOptimizedGraphBuilder::GenerateIsConstructCall(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 0);
  if (function_state()->outer() != NULL) {
    // We are generating graph for inlined function.
    HValue* value = function_state()->inlining_kind() == CONSTRUCT_CALL_RETURN
        ? graph()->GetConstantTrue()
        : graph()->GetConstantFalse();
    return ast_context()->ReturnValue(value);
  } else {
    return ast_context()->ReturnControl(new(zone()) HIsConstructCallAndBranch,
                                        call->id());
  }
}


// Support for arguments.length and arguments[?].
void HOptimizedGraphBuilder::GenerateArgumentsLength(CallRuntime* call) {
  // Our implementation of arguments (based on this stack frame or an
  // adapter below it) does not work for inlined functions.  This runtime
  // function is blacklisted by AstNode::IsInlineable.
  ASSERT(function_state()->outer() == NULL);
  ASSERT(call->arguments()->length() == 0);
  HInstruction* elements = AddInstruction(
      new(zone()) HArgumentsElements(false));
  HArgumentsLength* result = new(zone()) HArgumentsLength(elements);
  return ast_context()->ReturnInstruction(result, call->id());
}


void HOptimizedGraphBuilder::GenerateArguments(CallRuntime* call) {
  // Our implementation of arguments (based on this stack frame or an
  // adapter below it) does not work for inlined functions.  This runtime
  // function is blacklisted by AstNode::IsInlineable.
  ASSERT(function_state()->outer() == NULL);
  ASSERT(call->arguments()->length() == 1);
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  HValue* index = Pop();
  HInstruction* elements = AddInstruction(
      new(zone()) HArgumentsElements(false));
  HInstruction* length = AddInstruction(new(zone()) HArgumentsLength(elements));
  HInstruction* checked_index = AddBoundsCheck(index, length);
  HAccessArgumentsAt* result =
      new(zone()) HAccessArgumentsAt(elements, length, checked_index);
  return ast_context()->ReturnInstruction(result, call->id());
}


// Support for accessing the class and value fields of an object.
void HOptimizedGraphBuilder::GenerateClassOf(CallRuntime* call) {
  // The special form detected by IsClassOfTest is detected before we get here
  // and does not cause a bailout.
  return Bailout("inlined runtime function: ClassOf");
}


void HOptimizedGraphBuilder::GenerateValueOf(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  HValue* value = Pop();
  HValueOf* result = new(zone()) HValueOf(value);
  return ast_context()->ReturnInstruction(result, call->id());
}


void HOptimizedGraphBuilder::GenerateDateField(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 2);
  ASSERT_NE(NULL, call->arguments()->at(1)->AsLiteral());
  Smi* index = Smi::cast(*(call->arguments()->at(1)->AsLiteral()->handle()));
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  HValue* date = Pop();
  HDateField* result = new(zone()) HDateField(date, index);
  return ast_context()->ReturnInstruction(result, call->id());
}


void HOptimizedGraphBuilder::GenerateOneByteSeqStringSetChar(
    CallRuntime* call) {
  ASSERT(call->arguments()->length() == 3);
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  CHECK_ALIVE(VisitForValue(call->arguments()->at(1)));
  CHECK_ALIVE(VisitForValue(call->arguments()->at(2)));
  HValue* value = Pop();
  HValue* index = Pop();
  HValue* string = Pop();
  HSeqStringSetChar* result = new(zone()) HSeqStringSetChar(
      String::ONE_BYTE_ENCODING, string, index, value);
  return ast_context()->ReturnInstruction(result, call->id());
}


void HOptimizedGraphBuilder::GenerateTwoByteSeqStringSetChar(
    CallRuntime* call) {
  ASSERT(call->arguments()->length() == 3);
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  CHECK_ALIVE(VisitForValue(call->arguments()->at(1)));
  CHECK_ALIVE(VisitForValue(call->arguments()->at(2)));
  HValue* value = Pop();
  HValue* index = Pop();
  HValue* string = Pop();
  HValue* context = environment()->LookupContext();
  HInstruction* char_code = BuildStringCharCodeAt(context, string, index);
  AddInstruction(char_code);
  HSeqStringSetChar* result = new(zone()) HSeqStringSetChar(
      String::TWO_BYTE_ENCODING, string, index, value);
  return ast_context()->ReturnInstruction(result, call->id());
}


void HOptimizedGraphBuilder::GenerateSetValueOf(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 2);
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  CHECK_ALIVE(VisitForValue(call->arguments()->at(1)));
  HValue* value = Pop();
  HValue* object = Pop();
  // Check if object is a not a smi.
  HIsSmiAndBranch* smicheck = new(zone()) HIsSmiAndBranch(object);
  HBasicBlock* if_smi = graph()->CreateBasicBlock();
  HBasicBlock* if_heap_object = graph()->CreateBasicBlock();
  HBasicBlock* join = graph()->CreateBasicBlock();
  smicheck->SetSuccessorAt(0, if_smi);
  smicheck->SetSuccessorAt(1, if_heap_object);
  current_block()->Finish(smicheck);
  if_smi->Goto(join);

  // Check if object is a JSValue.
  set_current_block(if_heap_object);
  HHasInstanceTypeAndBranch* typecheck =
      new(zone()) HHasInstanceTypeAndBranch(object, JS_VALUE_TYPE);
  HBasicBlock* if_js_value = graph()->CreateBasicBlock();
  HBasicBlock* not_js_value = graph()->CreateBasicBlock();
  typecheck->SetSuccessorAt(0, if_js_value);
  typecheck->SetSuccessorAt(1, not_js_value);
  current_block()->Finish(typecheck);
  not_js_value->Goto(join);

  // Create in-object property store to kValueOffset.
  set_current_block(if_js_value);
  Handle<String> name = isolate()->factory()->undefined_string();
  AddInstruction(new(zone()) HStoreNamedField(object,
                                              name,
                                              value,
                                              true,  // in-object store.
                                              Representation::Tagged(),
                                              JSValue::kValueOffset));
  if_js_value->Goto(join);
  join->SetJoinId(call->id());
  set_current_block(join);
  return ast_context()->ReturnValue(value);
}


// Fast support for charCodeAt(n).
void HOptimizedGraphBuilder::GenerateStringCharCodeAt(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 2);
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  CHECK_ALIVE(VisitForValue(call->arguments()->at(1)));
  HValue* index = Pop();
  HValue* string = Pop();
  HValue* context = environment()->LookupContext();
  HInstruction* result = BuildStringCharCodeAt(context, string, index);
  return ast_context()->ReturnInstruction(result, call->id());
}


// Fast support for string.charAt(n) and string[n].
void HOptimizedGraphBuilder::GenerateStringCharFromCode(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  HValue* char_code = Pop();
  HValue* context = environment()->LookupContext();
  HInstruction* result = HStringCharFromCode::New(zone(), context, char_code);
  return ast_context()->ReturnInstruction(result, call->id());
}


// Fast support for string.charAt(n) and string[n].
void HOptimizedGraphBuilder::GenerateStringCharAt(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 2);
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  CHECK_ALIVE(VisitForValue(call->arguments()->at(1)));
  HValue* index = Pop();
  HValue* string = Pop();
  HValue* context = environment()->LookupContext();
  HInstruction* char_code = BuildStringCharCodeAt(context, string, index);
  AddInstruction(char_code);
  HInstruction* result = HStringCharFromCode::New(zone(), context, char_code);
  return ast_context()->ReturnInstruction(result, call->id());
}


// Fast support for object equality testing.
void HOptimizedGraphBuilder::GenerateObjectEquals(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 2);
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  CHECK_ALIVE(VisitForValue(call->arguments()->at(1)));
  HValue* right = Pop();
  HValue* left = Pop();
  HCompareObjectEqAndBranch* result =
      new(zone()) HCompareObjectEqAndBranch(left, right);
  return ast_context()->ReturnControl(result, call->id());
}


void HOptimizedGraphBuilder::GenerateLog(CallRuntime* call) {
  // %_Log is ignored in optimized code.
  return ast_context()->ReturnValue(graph()->GetConstantUndefined());
}


// Fast support for Math.random().
void HOptimizedGraphBuilder::GenerateRandomHeapNumber(CallRuntime* call) {
  HValue* context = environment()->LookupContext();
  HGlobalObject* global_object = new(zone()) HGlobalObject(context);
  AddInstruction(global_object);
  HRandom* result = new(zone()) HRandom(global_object);
  return ast_context()->ReturnInstruction(result, call->id());
}


// Fast support for StringAdd.
void HOptimizedGraphBuilder::GenerateStringAdd(CallRuntime* call) {
  ASSERT_EQ(2, call->arguments()->length());
  CHECK_ALIVE(VisitArgumentList(call->arguments()));
  HValue* context = environment()->LookupContext();
  HCallStub* result = new(zone()) HCallStub(context, CodeStub::StringAdd, 2);
  Drop(2);
  return ast_context()->ReturnInstruction(result, call->id());
}


// Fast support for SubString.
void HOptimizedGraphBuilder::GenerateSubString(CallRuntime* call) {
  ASSERT_EQ(3, call->arguments()->length());
  CHECK_ALIVE(VisitArgumentList(call->arguments()));
  HValue* context = environment()->LookupContext();
  HCallStub* result = new(zone()) HCallStub(context, CodeStub::SubString, 3);
  Drop(3);
  return ast_context()->ReturnInstruction(result, call->id());
}


// Fast support for StringCompare.
void HOptimizedGraphBuilder::GenerateStringCompare(CallRuntime* call) {
  ASSERT_EQ(2, call->arguments()->length());
  CHECK_ALIVE(VisitArgumentList(call->arguments()));
  HValue* context = environment()->LookupContext();
  HCallStub* result =
      new(zone()) HCallStub(context, CodeStub::StringCompare, 2);
  Drop(2);
  return ast_context()->ReturnInstruction(result, call->id());
}


// Support for direct calls from JavaScript to native RegExp code.
void HOptimizedGraphBuilder::GenerateRegExpExec(CallRuntime* call) {
  ASSERT_EQ(4, call->arguments()->length());
  CHECK_ALIVE(VisitArgumentList(call->arguments()));
  HValue* context = environment()->LookupContext();
  HCallStub* result = new(zone()) HCallStub(context, CodeStub::RegExpExec, 4);
  Drop(4);
  return ast_context()->ReturnInstruction(result, call->id());
}


// Construct a RegExp exec result with two in-object properties.
void HOptimizedGraphBuilder::GenerateRegExpConstructResult(CallRuntime* call) {
  ASSERT_EQ(3, call->arguments()->length());
  CHECK_ALIVE(VisitArgumentList(call->arguments()));
  HValue* context = environment()->LookupContext();
  HCallStub* result =
      new(zone()) HCallStub(context, CodeStub::RegExpConstructResult, 3);
  Drop(3);
  return ast_context()->ReturnInstruction(result, call->id());
}


// Support for fast native caches.
void HOptimizedGraphBuilder::GenerateGetFromCache(CallRuntime* call) {
  return Bailout("inlined runtime function: GetFromCache");
}


// Fast support for number to string.
void HOptimizedGraphBuilder::GenerateNumberToString(CallRuntime* call) {
  ASSERT_EQ(1, call->arguments()->length());
  CHECK_ALIVE(VisitArgumentList(call->arguments()));
  HValue* context = environment()->LookupContext();
  HCallStub* result =
      new(zone()) HCallStub(context, CodeStub::NumberToString, 1);
  Drop(1);
  return ast_context()->ReturnInstruction(result, call->id());
}


// Fast call for custom callbacks.
void HOptimizedGraphBuilder::GenerateCallFunction(CallRuntime* call) {
  // 1 ~ The function to call is not itself an argument to the call.
  int arg_count = call->arguments()->length() - 1;
  ASSERT(arg_count >= 1);  // There's always at least a receiver.

  for (int i = 0; i < arg_count; ++i) {
    CHECK_ALIVE(VisitArgument(call->arguments()->at(i)));
  }
  CHECK_ALIVE(VisitForValue(call->arguments()->last()));

  HValue* function = Pop();
  HValue* context = environment()->LookupContext();

  // Branch for function proxies, or other non-functions.
  HHasInstanceTypeAndBranch* typecheck =
      new(zone()) HHasInstanceTypeAndBranch(function, JS_FUNCTION_TYPE);
  HBasicBlock* if_jsfunction = graph()->CreateBasicBlock();
  HBasicBlock* if_nonfunction = graph()->CreateBasicBlock();
  HBasicBlock* join = graph()->CreateBasicBlock();
  typecheck->SetSuccessorAt(0, if_jsfunction);
  typecheck->SetSuccessorAt(1, if_nonfunction);
  current_block()->Finish(typecheck);

  set_current_block(if_jsfunction);
  HInstruction* invoke_result = AddInstruction(
      new(zone()) HInvokeFunction(context, function, arg_count));
  Drop(arg_count);
  Push(invoke_result);
  if_jsfunction->Goto(join);

  set_current_block(if_nonfunction);
  HInstruction* call_result = AddInstruction(
      new(zone()) HCallFunction(context, function, arg_count));
  Drop(arg_count);
  Push(call_result);
  if_nonfunction->Goto(join);

  set_current_block(join);
  join->SetJoinId(call->id());
  return ast_context()->ReturnValue(Pop());
}


// Fast call to math functions.
void HOptimizedGraphBuilder::GenerateMathPow(CallRuntime* call) {
  ASSERT_EQ(2, call->arguments()->length());
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  CHECK_ALIVE(VisitForValue(call->arguments()->at(1)));
  HValue* right = Pop();
  HValue* left = Pop();
  HInstruction* result = HPower::New(zone(), left, right);
  return ast_context()->ReturnInstruction(result, call->id());
}


void HOptimizedGraphBuilder::GenerateMathSin(CallRuntime* call) {
  ASSERT_EQ(1, call->arguments()->length());
  CHECK_ALIVE(VisitArgumentList(call->arguments()));
  HValue* context = environment()->LookupContext();
  HCallStub* result =
      new(zone()) HCallStub(context, CodeStub::TranscendentalCache, 1);
  result->set_transcendental_type(TranscendentalCache::SIN);
  Drop(1);
  return ast_context()->ReturnInstruction(result, call->id());
}


void HOptimizedGraphBuilder::GenerateMathCos(CallRuntime* call) {
  ASSERT_EQ(1, call->arguments()->length());
  CHECK_ALIVE(VisitArgumentList(call->arguments()));
  HValue* context = environment()->LookupContext();
  HCallStub* result =
      new(zone()) HCallStub(context, CodeStub::TranscendentalCache, 1);
  result->set_transcendental_type(TranscendentalCache::COS);
  Drop(1);
  return ast_context()->ReturnInstruction(result, call->id());
}


void HOptimizedGraphBuilder::GenerateMathTan(CallRuntime* call) {
  ASSERT_EQ(1, call->arguments()->length());
  CHECK_ALIVE(VisitArgumentList(call->arguments()));
  HValue* context = environment()->LookupContext();
  HCallStub* result =
      new(zone()) HCallStub(context, CodeStub::TranscendentalCache, 1);
  result->set_transcendental_type(TranscendentalCache::TAN);
  Drop(1);
  return ast_context()->ReturnInstruction(result, call->id());
}


void HOptimizedGraphBuilder::GenerateMathLog(CallRuntime* call) {
  ASSERT_EQ(1, call->arguments()->length());
  CHECK_ALIVE(VisitArgumentList(call->arguments()));
  HValue* context = environment()->LookupContext();
  HCallStub* result =
      new(zone()) HCallStub(context, CodeStub::TranscendentalCache, 1);
  result->set_transcendental_type(TranscendentalCache::LOG);
  Drop(1);
  return ast_context()->ReturnInstruction(result, call->id());
}


void HOptimizedGraphBuilder::GenerateMathSqrt(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  HValue* value = Pop();
  HValue* context = environment()->LookupContext();
  HInstruction* result =
      HUnaryMathOperation::New(zone(), context, value, kMathSqrt);
  return ast_context()->ReturnInstruction(result, call->id());
}


// Check whether two RegExps are equivalent
void HOptimizedGraphBuilder::GenerateIsRegExpEquivalent(CallRuntime* call) {
  return Bailout("inlined runtime function: IsRegExpEquivalent");
}


void HOptimizedGraphBuilder::GenerateGetCachedArrayIndex(CallRuntime* call) {
  ASSERT(call->arguments()->length() == 1);
  CHECK_ALIVE(VisitForValue(call->arguments()->at(0)));
  HValue* value = Pop();
  HGetCachedArrayIndex* result = new(zone()) HGetCachedArrayIndex(value);
  return ast_context()->ReturnInstruction(result, call->id());
}


void HOptimizedGraphBuilder::GenerateFastAsciiArrayJoin(CallRuntime* call) {
  return Bailout("inlined runtime function: FastAsciiArrayJoin");
}


// Support for generators.
void HOptimizedGraphBuilder::GenerateGeneratorSend(CallRuntime* call) {
  return Bailout("inlined runtime function: GeneratorSend");
}


void HOptimizedGraphBuilder::GenerateGeneratorThrow(CallRuntime* call) {
  return Bailout("inlined runtime function: GeneratorThrow");
}


#undef CHECK_BAILOUT
#undef CHECK_ALIVE


HEnvironment::HEnvironment(HEnvironment* outer,
                           Scope* scope,
                           Handle<JSFunction> closure,
                           Zone* zone)
    : closure_(closure),
      values_(0, zone),
      frame_type_(JS_FUNCTION),
      parameter_count_(0),
      specials_count_(1),
      local_count_(0),
      outer_(outer),
      entry_(NULL),
      pop_count_(0),
      push_count_(0),
      ast_id_(BailoutId::None()),
      zone_(zone) {
  Initialize(scope->num_parameters() + 1, scope->num_stack_slots(), 0);
}


HEnvironment::HEnvironment(Zone* zone, int parameter_count)
    : values_(0, zone),
      frame_type_(STUB),
      parameter_count_(parameter_count),
      specials_count_(1),
      local_count_(0),
      outer_(NULL),
      entry_(NULL),
      pop_count_(0),
      push_count_(0),
      ast_id_(BailoutId::None()),
      zone_(zone) {
  Initialize(parameter_count, 0, 0);
}


HEnvironment::HEnvironment(const HEnvironment* other, Zone* zone)
    : values_(0, zone),
      frame_type_(JS_FUNCTION),
      parameter_count_(0),
      specials_count_(0),
      local_count_(0),
      outer_(NULL),
      entry_(NULL),
      pop_count_(0),
      push_count_(0),
      ast_id_(other->ast_id()),
      zone_(zone) {
  Initialize(other);
}


HEnvironment::HEnvironment(HEnvironment* outer,
                           Handle<JSFunction> closure,
                           FrameType frame_type,
                           int arguments,
                           Zone* zone)
    : closure_(closure),
      values_(arguments, zone),
      frame_type_(frame_type),
      parameter_count_(arguments),
      local_count_(0),
      outer_(outer),
      entry_(NULL),
      pop_count_(0),
      push_count_(0),
      ast_id_(BailoutId::None()),
      zone_(zone) {
}


void HEnvironment::Initialize(int parameter_count,
                              int local_count,
                              int stack_height) {
  parameter_count_ = parameter_count;
  local_count_ = local_count;

  // Avoid reallocating the temporaries' backing store on the first Push.
  int total = parameter_count + specials_count_ + local_count + stack_height;
  values_.Initialize(total + 4, zone());
  for (int i = 0; i < total; ++i) values_.Add(NULL, zone());
}


void HEnvironment::Initialize(const HEnvironment* other) {
  closure_ = other->closure();
  values_.AddAll(other->values_, zone());
  assigned_variables_.Union(other->assigned_variables_, zone());
  frame_type_ = other->frame_type_;
  parameter_count_ = other->parameter_count_;
  local_count_ = other->local_count_;
  if (other->outer_ != NULL) outer_ = other->outer_->Copy();  // Deep copy.
  entry_ = other->entry_;
  pop_count_ = other->pop_count_;
  push_count_ = other->push_count_;
  specials_count_ = other->specials_count_;
  ast_id_ = other->ast_id_;
}


void HEnvironment::AddIncomingEdge(HBasicBlock* block, HEnvironment* other) {
  ASSERT(!block->IsLoopHeader());
  ASSERT(values_.length() == other->values_.length());

  int length = values_.length();
  for (int i = 0; i < length; ++i) {
    HValue* value = values_[i];
    if (value != NULL && value->IsPhi() && value->block() == block) {
      // There is already a phi for the i'th value.
      HPhi* phi = HPhi::cast(value);
      // Assert index is correct and that we haven't missed an incoming edge.
      ASSERT(phi->merged_index() == i);
      ASSERT(phi->OperandCount() == block->predecessors()->length());
      phi->AddInput(other->values_[i]);
    } else if (values_[i] != other->values_[i]) {
      // There is a fresh value on the incoming edge, a phi is needed.
      ASSERT(values_[i] != NULL && other->values_[i] != NULL);
      HPhi* phi = new(zone()) HPhi(i, zone());
      HValue* old_value = values_[i];
      for (int j = 0; j < block->predecessors()->length(); j++) {
        phi->AddInput(old_value);
      }
      phi->AddInput(other->values_[i]);
      this->values_[i] = phi;
      block->AddPhi(phi);
    }
  }
}


void HEnvironment::Bind(int index, HValue* value) {
  ASSERT(value != NULL);
  assigned_variables_.Add(index, zone());
  values_[index] = value;
}


bool HEnvironment::HasExpressionAt(int index) const {
  return index >= parameter_count_ + specials_count_ + local_count_;
}


bool HEnvironment::ExpressionStackIsEmpty() const {
  ASSERT(length() >= first_expression_index());
  return length() == first_expression_index();
}


void HEnvironment::SetExpressionStackAt(int index_from_top, HValue* value) {
  int count = index_from_top + 1;
  int index = values_.length() - count;
  ASSERT(HasExpressionAt(index));
  // The push count must include at least the element in question or else
  // the new value will not be included in this environment's history.
  if (push_count_ < count) {
    // This is the same effect as popping then re-pushing 'count' elements.
    pop_count_ += (count - push_count_);
    push_count_ = count;
  }
  values_[index] = value;
}


void HEnvironment::Drop(int count) {
  for (int i = 0; i < count; ++i) {
    Pop();
  }
}


HEnvironment* HEnvironment::Copy() const {
  return new(zone()) HEnvironment(this, zone());
}


HEnvironment* HEnvironment::CopyWithoutHistory() const {
  HEnvironment* result = Copy();
  result->ClearHistory();
  return result;
}


HEnvironment* HEnvironment::CopyAsLoopHeader(HBasicBlock* loop_header) const {
  HEnvironment* new_env = Copy();
  for (int i = 0; i < values_.length(); ++i) {
    HPhi* phi = new(zone()) HPhi(i, zone());
    phi->AddInput(values_[i]);
    new_env->values_[i] = phi;
    loop_header->AddPhi(phi);
  }
  new_env->ClearHistory();
  return new_env;
}


HEnvironment* HEnvironment::CreateStubEnvironment(HEnvironment* outer,
                                                  Handle<JSFunction> target,
                                                  FrameType frame_type,
                                                  int arguments) const {
  HEnvironment* new_env =
      new(zone()) HEnvironment(outer, target, frame_type,
                               arguments + 1, zone());
  for (int i = 0; i <= arguments; ++i) {  // Include receiver.
    new_env->Push(ExpressionStackAt(arguments - i));
  }
  new_env->ClearHistory();
  return new_env;
}


HEnvironment* HEnvironment::CopyForInlining(
    Handle<JSFunction> target,
    int arguments,
    FunctionLiteral* function,
    HConstant* undefined,
    InliningKind inlining_kind,
    bool undefined_receiver) const {
  ASSERT(frame_type() == JS_FUNCTION);

  // Outer environment is a copy of this one without the arguments.
  int arity = function->scope()->num_parameters();

  HEnvironment* outer = Copy();
  outer->Drop(arguments + 1);  // Including receiver.
  outer->ClearHistory();

  if (inlining_kind == CONSTRUCT_CALL_RETURN) {
    // Create artificial constructor stub environment.  The receiver should
    // actually be the constructor function, but we pass the newly allocated
    // object instead, DoComputeConstructStubFrame() relies on that.
    outer = CreateStubEnvironment(outer, target, JS_CONSTRUCT, arguments);
  } else if (inlining_kind == GETTER_CALL_RETURN) {
    // We need an additional StackFrame::INTERNAL frame for restoring the
    // correct context.
    outer = CreateStubEnvironment(outer, target, JS_GETTER, arguments);
  } else if (inlining_kind == SETTER_CALL_RETURN) {
    // We need an additional StackFrame::INTERNAL frame for temporarily saving
    // the argument of the setter, see StoreStubCompiler::CompileStoreViaSetter.
    outer = CreateStubEnvironment(outer, target, JS_SETTER, arguments);
  }

  if (arity != arguments) {
    // Create artificial arguments adaptation environment.
    outer = CreateStubEnvironment(outer, target, ARGUMENTS_ADAPTOR, arguments);
  }

  HEnvironment* inner =
      new(zone()) HEnvironment(outer, function->scope(), target, zone());
  // Get the argument values from the original environment.
  for (int i = 0; i <= arity; ++i) {  // Include receiver.
    HValue* push = (i <= arguments) ?
        ExpressionStackAt(arguments - i) : undefined;
    inner->SetValueAt(i, push);
  }
  // If the function we are inlining is a strict mode function or a
  // builtin function, pass undefined as the receiver for function
  // calls (instead of the global receiver).
  if (undefined_receiver) {
    inner->SetValueAt(0, undefined);
  }
  inner->SetValueAt(arity + 1, LookupContext());
  for (int i = arity + 2; i < inner->length(); ++i) {
    inner->SetValueAt(i, undefined);
  }

  inner->set_ast_id(BailoutId::FunctionEntry());
  return inner;
}


void HEnvironment::PrintTo(StringStream* stream) {
  for (int i = 0; i < length(); i++) {
    if (i == 0) stream->Add("parameters\n");
    if (i == parameter_count()) stream->Add("specials\n");
    if (i == parameter_count() + specials_count()) stream->Add("locals\n");
    if (i == parameter_count() + specials_count() + local_count()) {
      stream->Add("expressions\n");
    }
    HValue* val = values_.at(i);
    stream->Add("%d: ", i);
    if (val != NULL) {
      val->PrintNameTo(stream);
    } else {
      stream->Add("NULL");
    }
    stream->Add("\n");
  }
  PrintF("\n");
}


void HEnvironment::PrintToStd() {
  HeapStringAllocator string_allocator;
  StringStream trace(&string_allocator);
  PrintTo(&trace);
  PrintF("%s", *trace.ToCString());
}


void HTracer::TraceCompilation(CompilationInfo* info) {
  Tag tag(this, "compilation");
  if (info->IsOptimizing()) {
    Handle<String> name = info->function()->debug_name();
    PrintStringProperty("name", *name->ToCString());
    PrintStringProperty("method", *name->ToCString());
  } else {
    CodeStub::Major major_key = info->code_stub()->MajorKey();
    PrintStringProperty("name", CodeStub::MajorName(major_key, false));
    PrintStringProperty("method", "stub");
  }
  PrintLongProperty("date", static_cast<int64_t>(OS::TimeCurrentMillis()));
}


void HTracer::TraceLithium(const char* name, LChunk* chunk) {
  ASSERT(!FLAG_parallel_recompilation);
  ALLOW_HANDLE_DEREF(chunk->isolate(), "debug output");
  Trace(name, chunk->graph(), chunk);
}


void HTracer::TraceHydrogen(const char* name, HGraph* graph) {
  ASSERT(!FLAG_parallel_recompilation);
  ALLOW_HANDLE_DEREF(graph->isolate(), "debug output");
  Trace(name, graph, NULL);
}


void HTracer::Trace(const char* name, HGraph* graph, LChunk* chunk) {
  Tag tag(this, "cfg");
  PrintStringProperty("name", name);
  const ZoneList<HBasicBlock*>* blocks = graph->blocks();
  for (int i = 0; i < blocks->length(); i++) {
    HBasicBlock* current = blocks->at(i);
    Tag block_tag(this, "block");
    PrintBlockProperty("name", current->block_id());
    PrintIntProperty("from_bci", -1);
    PrintIntProperty("to_bci", -1);

    if (!current->predecessors()->is_empty()) {
      PrintIndent();
      trace_.Add("predecessors");
      for (int j = 0; j < current->predecessors()->length(); ++j) {
        trace_.Add(" \"B%d\"", current->predecessors()->at(j)->block_id());
      }
      trace_.Add("\n");
    } else {
      PrintEmptyProperty("predecessors");
    }

    if (current->end()->SuccessorCount() == 0) {
      PrintEmptyProperty("successors");
    } else  {
      PrintIndent();
      trace_.Add("successors");
      for (HSuccessorIterator it(current->end()); !it.Done(); it.Advance()) {
        trace_.Add(" \"B%d\"", it.Current()->block_id());
      }
      trace_.Add("\n");
    }

    PrintEmptyProperty("xhandlers");
    const char* flags = current->IsLoopSuccessorDominator()
        ? "dom-loop-succ"
        : "";
    PrintStringProperty("flags", flags);

    if (current->dominator() != NULL) {
      PrintBlockProperty("dominator", current->dominator()->block_id());
    }

    PrintIntProperty("loop_depth", current->LoopNestingDepth());

    if (chunk != NULL) {
      int first_index = current->first_instruction_index();
      int last_index = current->last_instruction_index();
      PrintIntProperty(
          "first_lir_id",
          LifetimePosition::FromInstructionIndex(first_index).Value());
      PrintIntProperty(
          "last_lir_id",
          LifetimePosition::FromInstructionIndex(last_index).Value());
    }

    {
      Tag states_tag(this, "states");
      Tag locals_tag(this, "locals");
      int total = current->phis()->length();
      PrintIntProperty("size", current->phis()->length());
      PrintStringProperty("method", "None");
      for (int j = 0; j < total; ++j) {
        HPhi* phi = current->phis()->at(j);
        PrintIndent();
        trace_.Add("%d ", phi->merged_index());
        phi->PrintNameTo(&trace_);
        trace_.Add(" ");
        phi->PrintTo(&trace_);
        trace_.Add("\n");
      }
    }

    {
      Tag HIR_tag(this, "HIR");
      HInstruction* instruction = current->first();
      while (instruction != NULL) {
        int bci = 0;
        int uses = instruction->UseCount();
        PrintIndent();
        trace_.Add("%d %d ", bci, uses);
        instruction->PrintNameTo(&trace_);
        trace_.Add(" ");
        instruction->PrintTo(&trace_);
        trace_.Add(" <|@\n");
        instruction = instruction->next();
      }
    }


    if (chunk != NULL) {
      Tag LIR_tag(this, "LIR");
      int first_index = current->first_instruction_index();
      int last_index = current->last_instruction_index();
      if (first_index != -1 && last_index != -1) {
        const ZoneList<LInstruction*>* instructions = chunk->instructions();
        for (int i = first_index; i <= last_index; ++i) {
          LInstruction* linstr = instructions->at(i);
          if (linstr != NULL) {
            PrintIndent();
            trace_.Add("%d ",
                       LifetimePosition::FromInstructionIndex(i).Value());
            linstr->PrintTo(&trace_);
            trace_.Add(" <|@\n");
          }
        }
      }
    }
  }
}


void HTracer::TraceLiveRanges(const char* name, LAllocator* allocator) {
  Tag tag(this, "intervals");
  PrintStringProperty("name", name);

  const Vector<LiveRange*>* fixed_d = allocator->fixed_double_live_ranges();
  for (int i = 0; i < fixed_d->length(); ++i) {
    TraceLiveRange(fixed_d->at(i), "fixed", allocator->zone());
  }

  const Vector<LiveRange*>* fixed = allocator->fixed_live_ranges();
  for (int i = 0; i < fixed->length(); ++i) {
    TraceLiveRange(fixed->at(i), "fixed", allocator->zone());
  }

  const ZoneList<LiveRange*>* live_ranges = allocator->live_ranges();
  for (int i = 0; i < live_ranges->length(); ++i) {
    TraceLiveRange(live_ranges->at(i), "object", allocator->zone());
  }
}


void HTracer::TraceLiveRange(LiveRange* range, const char* type,
                             Zone* zone) {
  if (range != NULL && !range->IsEmpty()) {
    PrintIndent();
    trace_.Add("%d %s", range->id(), type);
    if (range->HasRegisterAssigned()) {
      LOperand* op = range->CreateAssignedOperand(zone);
      int assigned_reg = op->index();
      if (op->IsDoubleRegister()) {
        trace_.Add(" \"%s\"",
                   DoubleRegister::AllocationIndexToString(assigned_reg));
      } else {
        ASSERT(op->IsRegister());
        trace_.Add(" \"%s\"", Register::AllocationIndexToString(assigned_reg));
      }
    } else if (range->IsSpilled()) {
      LOperand* op = range->TopLevel()->GetSpillOperand();
      if (op->IsDoubleStackSlot()) {
        trace_.Add(" \"double_stack:%d\"", op->index());
      } else {
        ASSERT(op->IsStackSlot());
        trace_.Add(" \"stack:%d\"", op->index());
      }
    }
    int parent_index = -1;
    if (range->IsChild()) {
      parent_index = range->parent()->id();
    } else {
      parent_index = range->id();
    }
    LOperand* op = range->FirstHint();
    int hint_index = -1;
    if (op != NULL && op->IsUnallocated()) {
      hint_index = LUnallocated::cast(op)->virtual_register();
    }
    trace_.Add(" %d %d", parent_index, hint_index);
    UseInterval* cur_interval = range->first_interval();
    while (cur_interval != NULL && range->Covers(cur_interval->start())) {
      trace_.Add(" [%d, %d[",
                 cur_interval->start().Value(),
                 cur_interval->end().Value());
      cur_interval = cur_interval->next();
    }

    UsePosition* current_pos = range->first_pos();
    while (current_pos != NULL) {
      if (current_pos->RegisterIsBeneficial() || FLAG_trace_all_uses) {
        trace_.Add(" %d M", current_pos->pos().Value());
      }
      current_pos = current_pos->next();
    }

    trace_.Add(" \"\"\n");
  }
}


void HTracer::FlushToFile() {
  AppendChars(filename_.start(), *trace_.ToCString(), trace_.length(), false);
  trace_.Reset();
}


void HStatistics::Initialize(CompilationInfo* info) {
  if (info->shared_info().is_null()) return;
  source_size_ += info->shared_info()->SourceSize();
}


void HStatistics::Print() {
  PrintF("Timing results:\n");
  int64_t sum = 0;
  for (int i = 0; i < timing_.length(); ++i) {
    sum += timing_[i];
  }

  for (int i = 0; i < names_.length(); ++i) {
    PrintF("%30s", names_[i]);
    double ms = static_cast<double>(timing_[i]) / 1000;
    double percent = static_cast<double>(timing_[i]) * 100 / sum;
    PrintF(" - %8.3f ms / %4.1f %% ", ms, percent);

    unsigned size = sizes_[i];
    double size_percent = static_cast<double>(size) * 100 / total_size_;
    PrintF(" %9u bytes / %4.1f %%\n", size, size_percent);
  }

  PrintF("----------------------------------------"
         "---------------------------------------\n");
  int64_t total = create_graph_ + optimize_graph_ + generate_code_;
  PrintF("%30s - %8.3f ms / %4.1f %% \n",
         "Create graph",
         static_cast<double>(create_graph_) / 1000,
         static_cast<double>(create_graph_) * 100 / total);
  PrintF("%30s - %8.3f ms / %4.1f %% \n",
         "Optimize graph",
         static_cast<double>(optimize_graph_) / 1000,
         static_cast<double>(optimize_graph_) * 100 / total);
  PrintF("%30s - %8.3f ms / %4.1f %% \n",
         "Generate and install code",
         static_cast<double>(generate_code_) / 1000,
         static_cast<double>(generate_code_) * 100 / total);
  PrintF("----------------------------------------"
         "---------------------------------------\n");
  PrintF("%30s - %8.3f ms (%.1f times slower than full code gen)\n",
         "Total",
         static_cast<double>(total) / 1000,
         static_cast<double>(total) / full_code_gen_);

  double source_size_in_kb = static_cast<double>(source_size_) / 1024;
  double normalized_time =  source_size_in_kb > 0
      ? (static_cast<double>(total) / 1000) / source_size_in_kb
      : 0;
  double normalized_size_in_kb = source_size_in_kb > 0
      ? total_size_ / 1024 / source_size_in_kb
      : 0;
  PrintF("%30s - %8.3f ms           %7.3f kB allocated\n",
         "Average per kB source",
         normalized_time, normalized_size_in_kb);
}


void HStatistics::SaveTiming(const char* name, int64_t ticks, unsigned size) {
  if (name == HPhase::kFullCodeGen) {
    full_code_gen_ += ticks;
  } else {
    total_size_ += size;
    for (int i = 0; i < names_.length(); ++i) {
      if (strcmp(names_[i], name) == 0) {
        timing_[i] += ticks;
        sizes_[i] += size;
        return;
      }
    }
    names_.Add(name);
    timing_.Add(ticks);
    sizes_.Add(size);
  }
}


const char* const HPhase::kFullCodeGen = "Full code generator";


HPhase::HPhase(const char* name, Isolate* isolate) {
  Init(isolate, name, NULL, NULL, NULL);
}


HPhase::HPhase(const char* name, HGraph* graph) {
  Init(graph->isolate(), name, graph, NULL, NULL);
}


HPhase::HPhase(const char* name, LChunk* chunk) {
  Init(chunk->isolate(), name, NULL, chunk, NULL);
}


HPhase::HPhase(const char* name, LAllocator* allocator) {
  Init(allocator->isolate(), name, NULL, NULL, allocator);
}


void HPhase::Init(Isolate* isolate,
                  const char* name,
                  HGraph* graph,
                  LChunk* chunk,
                  LAllocator* allocator) {
  isolate_ = isolate;
  name_ = name;
  graph_ = graph;
  chunk_ = chunk;
  allocator_ = allocator;
  if (allocator != NULL && chunk_ == NULL) {
    chunk_ = allocator->chunk();
  }
  if (FLAG_hydrogen_stats) {
    start_ticks_ = OS::Ticks();
    start_allocation_size_ = Zone::allocation_size_;
  }
}


HPhase::~HPhase() {
  if (FLAG_hydrogen_stats) {
    int64_t ticks = OS::Ticks() - start_ticks_;
    unsigned size = Zone::allocation_size_ - start_allocation_size_;
    isolate_->GetHStatistics()->SaveTiming(name_, ticks, size);
  }

  // Produce trace output if flag is set so that the first letter of the
  // phase name matches the command line parameter FLAG_trace_phase.
  if (FLAG_trace_hydrogen &&
      OS::StrChr(const_cast<char*>(FLAG_trace_phase), name_[0]) != NULL) {
    if (graph_ != NULL) {
      isolate_->GetHTracer()->TraceHydrogen(name_, graph_);
    }
    if (chunk_ != NULL) {
      isolate_->GetHTracer()->TraceLithium(name_, chunk_);
    }
    if (allocator_ != NULL) {
      isolate_->GetHTracer()->TraceLiveRanges(name_, allocator_);
    }
  }

#ifdef DEBUG
  if (graph_ != NULL) graph_->Verify(false);  // No full verify.
  if (allocator_ != NULL) allocator_->Verify();
#endif
}

} }  // namespace v8::internal
