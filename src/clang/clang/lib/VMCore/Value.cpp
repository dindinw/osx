//===-- Value.cpp - Implement the Value class -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Value, ValueHandle, and User classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/Constant.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/InstrTypes.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/ValueSymbolTable.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/LeakDetector.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/ValueHandle.h"
#include "llvm/ADT/DenseMap.h"
#include <algorithm>
using namespace llvm;

//===----------------------------------------------------------------------===//
//                                Value Class
//===----------------------------------------------------------------------===//

static inline const Type *checkType(const Type *Ty) {
  assert(Ty && "Value defined with a null type: Error!");
  return Ty;
}

Value::Value(const Type *ty, unsigned scid)
  : SubclassID(scid), HasValueHandle(0), SubclassData(0), VTy(checkType(ty)),
    UseList(0), Name(0) {
  if (isa<CallInst>(this) || isa<InvokeInst>(this))
    assert((VTy->isFirstClassType() || VTy == Type::VoidTy ||
            isa<OpaqueType>(ty) || VTy->getTypeID() == Type::StructTyID) &&
           "invalid CallInst  type!");
  else if (!isa<Constant>(this) && !isa<BasicBlock>(this))
    assert((VTy->isFirstClassType() || VTy == Type::VoidTy ||
           isa<OpaqueType>(ty)) &&
           "Cannot create non-first-class values except for constants!");
}

Value::~Value() {
  // Notify all ValueHandles (if present) that this value is going away.
  if (HasValueHandle)
    ValueHandleBase::ValueIsDeleted(this);
  
#ifndef NDEBUG      // Only in -g mode...
  // Check to make sure that there are no uses of this value that are still
  // around when the value is destroyed.  If there are, then we have a dangling
  // reference and something is wrong.  This code is here to print out what is
  // still being referenced.  The value in question should be printed as
  // a <badref>
  //
  if (!use_empty()) {
    cerr << "While deleting: " << *VTy << " %" << getNameStr() << "\n";
    for (use_iterator I = use_begin(), E = use_end(); I != E; ++I)
      cerr << "Use still stuck around after Def is destroyed:"
           << **I << "\n";
  }
#endif
  assert(use_empty() && "Uses remain when a value is destroyed!");

  // If this value is named, destroy the name.  This should not be in a symtab
  // at this point.
  if (Name)
    Name->Destroy();
  
  // There should be no uses of this object anymore, remove it.
  LeakDetector::removeGarbageObject(this);
}

/// hasNUses - Return true if this Value has exactly N users.
///
bool Value::hasNUses(unsigned N) const {
  use_const_iterator UI = use_begin(), E = use_end();

  for (; N; --N, ++UI)
    if (UI == E) return false;  // Too few.
  return UI == E;
}

/// hasNUsesOrMore - Return true if this value has N users or more.  This is
/// logically equivalent to getNumUses() >= N.
///
bool Value::hasNUsesOrMore(unsigned N) const {
  use_const_iterator UI = use_begin(), E = use_end();

  for (; N; --N, ++UI)
    if (UI == E) return false;  // Too few.

  return true;
}

/// isUsedInBasicBlock - Return true if this value is used in the specified
/// basic block.
bool Value::isUsedInBasicBlock(const BasicBlock *BB) const {
  for (use_const_iterator I = use_begin(), E = use_end(); I != E; ++I) {
    const Instruction *User = dyn_cast<Instruction>(*I);
    if (User && User->getParent() == BB)
      return true;
  }
  return false;
}


/// getNumUses - This method computes the number of uses of this Value.  This
/// is a linear time operation.  Use hasOneUse or hasNUses to check for specific
/// values.
unsigned Value::getNumUses() const {
  return (unsigned)std::distance(use_begin(), use_end());
}

static bool getSymTab(Value *V, ValueSymbolTable *&ST) {
  ST = 0;
  if (Instruction *I = dyn_cast<Instruction>(V)) {
    if (BasicBlock *P = I->getParent())
      if (Function *PP = P->getParent())
        ST = &PP->getValueSymbolTable();
  } else if (BasicBlock *BB = dyn_cast<BasicBlock>(V)) {
    if (Function *P = BB->getParent()) 
      ST = &P->getValueSymbolTable();
  } else if (GlobalValue *GV = dyn_cast<GlobalValue>(V)) {
    if (Module *P = GV->getParent()) 
      ST = &P->getValueSymbolTable();
  } else if (Argument *A = dyn_cast<Argument>(V)) {
    if (Function *P = A->getParent()) 
      ST = &P->getValueSymbolTable();
  } else {
    assert(isa<Constant>(V) && "Unknown value type!");
    return true;  // no name is setable for this.
  }
  return false;
}

/// getNameStart - Return a pointer to a null terminated string for this name.
/// Note that names can have null characters within the string as well as at
/// their end.  This always returns a non-null pointer.
const char *Value::getNameStart() const {
  if (Name == 0) return "";
  return Name->getKeyData();
}

/// getNameLen - Return the length of the string, correctly handling nul
/// characters embedded into them.
unsigned Value::getNameLen() const {
  return Name ? Name->getKeyLength() : 0;
}

/// isName - Return true if this value has the name specified by the provided
/// nul terminated string.
bool Value::isName(const char *N) const {
  unsigned InLen = strlen(N);
  return InLen == getNameLen() && memcmp(getNameStart(), N, InLen) == 0;
}


std::string Value::getNameStr() const {
  if (Name == 0) return "";
  return std::string(Name->getKeyData(),
                     Name->getKeyData()+Name->getKeyLength());
}

void Value::setName(const std::string &name) {
  setName(&name[0], name.size());
}

void Value::setName(const char *Name) {
  setName(Name, Name ? strlen(Name) : 0);
}

void Value::setName(const char *NameStr, unsigned NameLen) {
  if (NameLen == 0 && !hasName()) return;
  assert(getType() != Type::VoidTy && "Cannot assign a name to void values!");
  
  // Get the symbol table to update for this object.
  ValueSymbolTable *ST;
  if (getSymTab(this, ST))
    return;  // Cannot set a name on this value (e.g. constant).

  if (!ST) { // No symbol table to update?  Just do the change.
    if (NameLen == 0) {
      // Free the name for this value.
      Name->Destroy();
      Name = 0;
      return;
    }
    
    if (Name) {
      // Name isn't changing?
      if (NameLen == Name->getKeyLength() &&
          !memcmp(Name->getKeyData(), NameStr, NameLen))
        return;
      Name->Destroy();
    }
    
    // NOTE: Could optimize for the case the name is shrinking to not deallocate
    // then reallocated.
      
    // Create the new name.
    Name = ValueName::Create(NameStr, NameStr+NameLen);
    Name->setValue(this);
    return;
  }
  
  // NOTE: Could optimize for the case the name is shrinking to not deallocate
  // then reallocated.
  if (hasName()) {
    // Name isn't changing?
    if (NameLen == Name->getKeyLength() &&
        !memcmp(Name->getKeyData(), NameStr, NameLen))
      return;

    // Remove old name.
    ST->removeValueName(Name);
    Name->Destroy();
    Name = 0;

    if (NameLen == 0)
      return;
  }

  // Name is changing to something new.
  Name = ST->createValueName(NameStr, NameLen, this);
}


/// takeName - transfer the name from V to this value, setting V's name to
/// empty.  It is an error to call V->takeName(V). 
void Value::takeName(Value *V) {
  ValueSymbolTable *ST = 0;
  // If this value has a name, drop it.
  if (hasName()) {
    // Get the symtab this is in.
    if (getSymTab(this, ST)) {
      // We can't set a name on this value, but we need to clear V's name if
      // it has one.
      if (V->hasName()) V->setName(0, 0);
      return;  // Cannot set a name on this value (e.g. constant).
    }
    
    // Remove old name.
    if (ST)
      ST->removeValueName(Name);
    Name->Destroy();
    Name = 0;
  } 
  
  // Now we know that this has no name.
  
  // If V has no name either, we're done.
  if (!V->hasName()) return;
   
  // Get this's symtab if we didn't before.
  if (!ST) {
    if (getSymTab(this, ST)) {
      // Clear V's name.
      V->setName(0, 0);
      return;  // Cannot set a name on this value (e.g. constant).
    }
  }
  
  // Get V's ST, this should always succed, because V has a name.
  ValueSymbolTable *VST;
  bool Failure = getSymTab(V, VST);
  assert(!Failure && "V has a name, so it should have a ST!"); Failure=Failure;
  
  // If these values are both in the same symtab, we can do this very fast.
  // This works even if both values have no symtab yet.
  if (ST == VST) {
    // Take the name!
    Name = V->Name;
    V->Name = 0;
    Name->setValue(this);
    return;
  }
  
  // Otherwise, things are slightly more complex.  Remove V's name from VST and
  // then reinsert it into ST.
  
  if (VST)
    VST->removeValueName(V->Name);
  Name = V->Name;
  V->Name = 0;
  Name->setValue(this);
  
  if (ST)
    ST->reinsertValue(this);
}


// uncheckedReplaceAllUsesWith - This is exactly the same as replaceAllUsesWith,
// except that it doesn't have all of the asserts.  The asserts fail because we
// are half-way done resolving types, which causes some types to exist as two
// different Type*'s at the same time.  This is a sledgehammer to work around
// this problem.
//
void Value::uncheckedReplaceAllUsesWith(Value *New) {
  // Notify all ValueHandles (if present) that this value is going away.
  if (HasValueHandle)
    ValueHandleBase::ValueIsRAUWd(this, New);
 
  while (!use_empty()) {
    Use &U = *UseList;
    // Must handle Constants specially, we cannot call replaceUsesOfWith on a
    // constant because they are uniqued.
    if (Constant *C = dyn_cast<Constant>(U.getUser())) {
      if (!isa<GlobalValue>(C)) {
        C->replaceUsesOfWithOnConstant(this, New, &U);
        continue;
      }
    }
    
    U.set(New);
  }
}

void Value::replaceAllUsesWith(Value *New) {
  assert(New && "Value::replaceAllUsesWith(<null>) is invalid!");
  assert(New != this && "this->replaceAllUsesWith(this) is NOT valid!");
  assert(New->getType() == getType() &&
         "replaceAllUses of value with new value of different type!");

  uncheckedReplaceAllUsesWith(New);
}

Value *Value::stripPointerCasts() {
  if (!isa<PointerType>(getType()))
    return this;
  Value *V = this;
  do {
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(V)) {
      if (CE->getOpcode() == Instruction::GetElementPtr) {
        for (unsigned i = 1, e = CE->getNumOperands(); i != e; ++i)
          if (!CE->getOperand(i)->isNullValue())
            return V;
        V = CE->getOperand(0);
      } else if (CE->getOpcode() == Instruction::BitCast) {
        V = CE->getOperand(0);
      } else {
        return V;
      }
    } else if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(V)) {
      if (!GEP->hasAllZeroIndices())
        return V;
      V = GEP->getOperand(0);
    } else if (BitCastInst *CI = dyn_cast<BitCastInst>(V)) {
      V = CI->getOperand(0);
    } else {
      return V;
    }
    assert(isa<PointerType>(V->getType()) && "Unexpected operand type!");
  } while (1);
}

Value *Value::getUnderlyingObject() {
  if (!isa<PointerType>(getType()))
    return this;
  Value *V = this;
  unsigned MaxLookup = 6;
  do {
    if (Instruction *I = dyn_cast<Instruction>(V)) {
      if (!isa<BitCastInst>(I) && !isa<GetElementPtrInst>(I))
        return V;
      V = I->getOperand(0);
    } else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(V)) {
      if (CE->getOpcode() != Instruction::BitCast &&
          CE->getOpcode() != Instruction::GetElementPtr)
        return V;
      V = CE->getOperand(0);
    } else {
      return V;
    }
    assert(isa<PointerType>(V->getType()) && "Unexpected operand type!");
  } while (--MaxLookup);
  return V;
}

/// DoPHITranslation - If this value is a PHI node with CurBB as its parent,
/// return the value in the PHI node corresponding to PredBB.  If not, return
/// ourself.  This is useful if you want to know the value something has in a
/// predecessor block.
Value *Value::DoPHITranslation(const BasicBlock *CurBB, 
                               const BasicBlock *PredBB) {
  PHINode *PN = dyn_cast<PHINode>(this);
  if (PN && PN->getParent() == CurBB)
    return PN->getIncomingValueForBlock(PredBB);
  return this;
}

//===----------------------------------------------------------------------===//
//                             ValueHandleBase Class
//===----------------------------------------------------------------------===//

/// ValueHandles - This map keeps track of all of the value handles that are
/// watching a Value*.  The Value::HasValueHandle bit is used to know whether or
/// not a value has an entry in this map.
typedef DenseMap<Value*, ValueHandleBase*> ValueHandlesTy;
static ManagedStatic<ValueHandlesTy> ValueHandles;

/// AddToUseList - Add this ValueHandle to the use list for VP, where List is
/// known to point into the existing use list.
void ValueHandleBase::AddToExistingUseList(ValueHandleBase **List) {
  assert(List && "Handle list is null?");
  
  // Splice ourselves into the list.
  Next = *List;
  *List = this;
  setPrevPtr(List);
  if (Next) {
    Next->setPrevPtr(&Next);
    assert(VP == Next->VP && "Added to wrong list?");
  }
}

/// AddToUseList - Add this ValueHandle to the use list for VP.
void ValueHandleBase::AddToUseList() {
  assert(VP && "Null pointer doesn't have a use list!");
  if (VP->HasValueHandle) {
    // If this value already has a ValueHandle, then it must be in the
    // ValueHandles map already.
    ValueHandleBase *&Entry = (*ValueHandles)[VP];
    assert(Entry != 0 && "Value doesn't have any handles?");
    return AddToExistingUseList(&Entry);
  }
  
  // Ok, it doesn't have any handles yet, so we must insert it into the
  // DenseMap.  However, doing this insertion could cause the DenseMap to
  // reallocate itself, which would invalidate all of the PrevP pointers that
  // point into the old table.  Handle this by checking for reallocation and
  // updating the stale pointers only if needed.
  ValueHandlesTy &Handles = *ValueHandles;
  const void *OldBucketPtr = Handles.getPointerIntoBucketsArray();
  
  ValueHandleBase *&Entry = Handles[VP];
  assert(Entry == 0 && "Value really did already have handles?");
  AddToExistingUseList(&Entry);
  VP->HasValueHandle = 1;
  
  // If reallocation didn't happen or if this was the first insertion, don't
  // walk the table.
  if (Handles.isPointerIntoBucketsArray(OldBucketPtr) || 
      Handles.size() == 1)
    return;
  
  // Okay, reallocation did happen.  Fix the Prev Pointers.
  for (ValueHandlesTy::iterator I = Handles.begin(), E = Handles.end();
       I != E; ++I) {
    assert(I->second && I->first == I->second->VP && "List invariant broken!");
    I->second->setPrevPtr(&I->second);
  }
}

/// RemoveFromUseList - Remove this ValueHandle from its current use list.
void ValueHandleBase::RemoveFromUseList() {
  assert(VP && VP->HasValueHandle && "Pointer doesn't have a use list!");

  // Unlink this from its use list.
  ValueHandleBase **PrevPtr = getPrevPtr();
  assert(*PrevPtr == this && "List invariant broken");
  
  *PrevPtr = Next;
  if (Next) {
    assert(Next->getPrevPtr() == &Next && "List invariant broken");
    Next->setPrevPtr(PrevPtr);
    return;
  }
  
  // If the Next pointer was null, then it is possible that this was the last
  // ValueHandle watching VP.  If so, delete its entry from the ValueHandles
  // map.
  ValueHandlesTy &Handles = *ValueHandles;
  if (Handles.isPointerIntoBucketsArray(PrevPtr)) {
    Handles.erase(VP);
    VP->HasValueHandle = false;
  }
}


void ValueHandleBase::ValueIsDeleted(Value *V) {
  assert(V->HasValueHandle && "Should only be called if ValueHandles present");

  // Get the linked list base, which is guaranteed to exist since the
  // HasValueHandle flag is set.
  ValueHandleBase *Entry = (*ValueHandles)[V];
  assert(Entry && "Value bit set but no entries exist");
  
  while (Entry) {
    // Advance pointer to avoid invalidation.
    ValueHandleBase *ThisNode = Entry;
    Entry = Entry->Next;
    
    switch (ThisNode->getKind()) {
    case Assert:
#ifndef NDEBUG      // Only in -g mode...
      cerr << "While deleting: " << *V->getType() << " %" << V->getNameStr()
           << "\n";
#endif
      cerr << "An asserting value handle still pointed to this value!\n";
      abort();
    case Weak:
      // Weak just goes to null, which will unlink it from the list.
      ThisNode->operator=(0);
      break;
    case Callback:
      assert(0 && "Callback not implemented yet!");
    }
  }
  
  // All callbacks and weak references should be dropped by now.
  assert(!V->HasValueHandle && "All references to V were not removed?");
}


void ValueHandleBase::ValueIsRAUWd(Value *Old, Value *New) {
  assert(Old->HasValueHandle &&"Should only be called if ValueHandles present");
  assert(Old != New && "Changing value into itself!");
  
  // Get the linked list base, which is guaranteed to exist since the
  // HasValueHandle flag is set.
  ValueHandleBase *Entry = (*ValueHandles)[Old];
  assert(Entry && "Value bit set but no entries exist");
  
  while (Entry) {
    // Advance pointer to avoid invalidation.
    ValueHandleBase *ThisNode = Entry;
    Entry = Entry->Next;
    
    switch (ThisNode->getKind()) {
    case Assert:
      // Asserting handle does not follow RAUW implicitly.
      break;
    case Weak:
      // Weak goes to the new value, which will unlink it from Old's list.
      ThisNode->operator=(New);
      break;
    case Callback:
      assert(0 && "Callback not implemented yet!");
    }
  }
}


//===----------------------------------------------------------------------===//
//                                 User Class
//===----------------------------------------------------------------------===//

// replaceUsesOfWith - Replaces all references to the "From" definition with
// references to the "To" definition.
//
void User::replaceUsesOfWith(Value *From, Value *To) {
  if (From == To) return;   // Duh what?

  assert((!isa<Constant>(this) || isa<GlobalValue>(this)) &&
         "Cannot call User::replaceUsesofWith on a constant!");

  for (unsigned i = 0, E = getNumOperands(); i != E; ++i)
    if (getOperand(i) == From) {  // Is This operand is pointing to oldval?
      // The side effects of this setOperand call include linking to
      // "To", adding "this" to the uses list of To, and
      // most importantly, removing "this" from the use list of "From".
      setOperand(i, To); // Fix it now...
    }
}

