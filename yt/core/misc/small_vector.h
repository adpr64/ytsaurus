//===- llvm/ADT/SmallVector.h - 'Normally small' vectors --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the SmallVector class.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "mpl.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>

#define LLVM_HAS_RVALUE_REFERENCES 1

namespace NYT {

/// SmallVectorBase - This is all the non-templated stuff common to all
/// SmallVectors.
class SmallVectorBase {
protected:
  void *BeginX, *EndX, *CapacityX;

  // Allocate raw space for N elements of type T.  If T has a ctor or dtor, we
  // don't want it to be automatically run, so we need to represent the space as
  // something else.  An array of char would work great, but might not be
  // aligned sufficiently.  Instead we use some number of union instances for
  // the space, which guarantee maximal alignment.
  union U {
    double D;
    // XXX(babenko): the following is commented out since hopefully we never need 16 bytes alignment
    // long double LD;
    long long L;
    void *P;
  } FirstEl;
  // Space after 'FirstEl' is clobbered, do not add any instance vars after it.

protected:
  SmallVectorBase(size_t Size)
    : BeginX(&CapacityX + 1), EndX(BeginX), CapacityX((char*)BeginX+Size) {}

  /// isSmall - Return true if this is a smallvector which has not had dynamic
  /// memory allocated for it.
  bool isSmall() const {
    // here we have dirty hack to get rid of FirstEl (in llvm no such dirty hack)
    return BeginX == static_cast<const void*>(&CapacityX + 1);
    // originally was:
    //return BeginX == static_cast<const void*>(&FirstEl);
  }

  /// resetToSmall - Put this vector in a state of being small.
  void resetToSmall() {
    BeginX = EndX = CapacityX = &FirstEl;
  }

  /// grow_pod - This is an implementation of the grow() method which only works
  /// on POD-like data types and is out of line to reduce code duplication.
  void grow_pod(size_t MinSizeInBytes, size_t TSize);

public:
  /// size_in_bytes - This returns size()*sizeof(T).
  size_t size_in_bytes() const {
    return size_t((char*)EndX - (char*)BeginX);
  }

  /// capacity_in_bytes - This returns capacity()*sizeof(T).
  size_t capacity_in_bytes() const {
    return size_t((char*)CapacityX - (char*)BeginX);
  }

  bool empty() const { return BeginX == EndX; }
};

template <typename T>
class SmallVectorTemplateCommon : public SmallVectorBase {
protected:
  SmallVectorTemplateCommon(size_t Size) : SmallVectorBase(Size) {}

  void setEnd(T *P) { this->EndX = P; }
public:
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef T value_type;
  typedef T *iterator;
  typedef const T *const_iterator;

  typedef std::reverse_iterator<const_iterator> const_reverse_iterator;
  typedef std::reverse_iterator<iterator> reverse_iterator;

  typedef T &reference;
  typedef const T &const_reference;
  typedef T *pointer;
  typedef const T *const_pointer;

  // forward iterator creation methods.
  iterator begin() { return (iterator)this->BeginX; }
  const_iterator begin() const { return (const_iterator)this->BeginX; }
  iterator end() { return (iterator)this->EndX; }
  const_iterator end() const { return (const_iterator)this->EndX; }
protected:
  iterator capacity_ptr() { return (iterator)this->CapacityX; }
  const_iterator capacity_ptr() const { return (const_iterator)this->CapacityX;}
public:

  // reverse iterator creation methods.
  reverse_iterator rbegin()            { return reverse_iterator(end()); }
  const_reverse_iterator rbegin() const{ return const_reverse_iterator(end()); }
  reverse_iterator rend()              { return reverse_iterator(begin()); }
  const_reverse_iterator rend() const { return const_reverse_iterator(begin());}

  size_type size() const { return end()-begin(); }
  size_type max_size() const { return size_type(-1) / sizeof(T); }

  /// capacity - Return the total number of elements in the currently allocated
  /// buffer.
  size_t capacity() const { return capacity_ptr() - begin(); }

  /// data - Return a pointer to the vector's buffer, even if empty().
  pointer data() { return pointer(begin()); }
  /// data - Return a pointer to the vector's buffer, even if empty().
  const_pointer data() const { return const_pointer(begin()); }

  reference operator[](unsigned idx) {
    assert(begin() + idx < end());
    return begin()[idx];
  }
  const_reference operator[](unsigned idx) const {
    assert(begin() + idx < end());
    return begin()[idx];
  }

  reference front() {
    return begin()[0];
  }
  const_reference front() const {
    return begin()[0];
  }

  reference back() {
    return end()[-1];
  }
  const_reference back() const {
    return end()[-1];
  }
};

/// SmallVectorTemplateBase<NMpl::TIsPod = false> - This is where we put method
/// implementations that are designed to work with non-POD-like T's.
template <typename T, bool IsPod>
class SmallVectorTemplateBase : public SmallVectorTemplateCommon<T> {
protected:
  SmallVectorTemplateBase(size_t Size) : SmallVectorTemplateCommon<T>(Size) {}

  static void destroy_range(T *S, T *E) {
    while (S != E) {
      --E;
      E->~T();
    }
  }

  /// move - Use move-assignment to move the range [I, E) onto the
  /// objects starting with "Dest".  This is just <memory>'s
  /// std::move, but not all stdlibs actually provide that.
  template<typename It1, typename It2>
  static It2 move(It1 I, It1 E, It2 Dest) {
#if LLVM_USE_RVALUE_REFERENCES
    for (; I != E; ++I, ++Dest)
      *Dest = ::std::move(*I);
    return Dest;
#else
    return ::std::copy(I, E, Dest);
#endif
  }

  /// move_backward - Use move-assignment to move the range
  /// [I, E) onto the objects ending at "Dest", moving objects
  /// in reverse order.  This is just <algorithm>'s
  /// std::move_backward, but not all stdlibs actually provide that.
  template<typename It1, typename It2>
  static It2 move_backward(It1 I, It1 E, It2 Dest) {
#if LLVM_USE_RVALUE_REFERENCES
    while (I != E)
      *--Dest = ::std::move(*--E);
    return Dest;
#else
    return ::std::copy_backward(I, E, Dest);
#endif
  }

  /// uninitialized_move - Move the range [I, E) into the uninitialized
  /// memory starting with "Dest", constructing elements as needed.
  template<typename It1, typename It2>
  static void uninitialized_move(It1 I, It1 E, It2 Dest) {
#if LLVM_USE_RVALUE_REFERENCES
    for (; I != E; ++I, ++Dest)
      ::new ((void*) &*Dest) T(::std::move(*I));
#else
    ::std::uninitialized_copy(I, E, Dest);
#endif
  }

  /// uninitialized_copy - Copy the range [I, E) onto the uninitialized
  /// memory starting with "Dest", constructing elements as needed.
  template <typename It1, typename It2>
  static void uninitialized_copy(It1 I, It1 E, It2 Dest) {
    std::uninitialized_copy(I, E, Dest);
  }

  /// grow - Grow the allocated memory (without initializing new
  /// elements), doubling the size of the allocated memory.
  /// Guarantees space for at least one more element, or MinSize more
  /// elements if specified.
  void grow(size_t MinSize = 0);

public:
  void push_back(const T &Elt) {
    if (this->EndX < this->CapacityX) {
    Retry:
      ::new ((void*) this->end()) T(Elt);
      this->setEnd(this->end()+1);
      return;
    }
    this->grow();
    goto Retry;
  }

#if LLVM_USE_RVALUE_REFERENCES
  void push_back(T &&Elt) {
    if (this->EndX < this->CapacityX) {
    Retry:
      ::new ((void*) this->end()) T(::std::move(Elt));
       this->setEnd(this->end()+1);
       return;
     }
     this->grow();
     goto Retry;
   }
#endif

  void pop_back() {
    this->setEnd(this->end()-1);
    this->end()->~T();
  }
};

// Define this out-of-line to dissuade the C++ compiler from inlining it.
template <typename T, bool IsPod>
void SmallVectorTemplateBase<T, IsPod>::grow(size_t MinSize) {
  size_t CurCapacity = this->capacity();
  size_t CurSize = this->size();
  size_t NewCapacity = 2*CurCapacity + 1; // Always grow, even from zero.
  if (NewCapacity < MinSize)
    NewCapacity = MinSize;
  T *NewElts = static_cast<T*>(malloc(NewCapacity*sizeof(T)));

  // Move the elements over.
  this->uninitialized_move(this->begin(), this->end(), NewElts);

  // Destroy the original elements.
  destroy_range(this->begin(), this->end());

  // If this wasn't grown from the inline copy, deallocate the old space.
  if (!this->isSmall())
    free(this->begin());

  this->setEnd(NewElts+CurSize);
  this->BeginX = NewElts;
  this->CapacityX = this->begin()+NewCapacity;
}


/// SmallVectorTemplateBase<NMpl::TIsPod = true> - This is where we put method
/// implementations that are designed to work with POD-like T's.
template <typename T>
class SmallVectorTemplateBase<T, true> : public SmallVectorTemplateCommon<T> {
protected:
  SmallVectorTemplateBase(size_t Size) : SmallVectorTemplateCommon<T>(Size) {}

  // No need to do a destroy loop for POD's.
  static void destroy_range(T *, T *) {}

  /// move - Use move-assignment to move the range [I, E) onto the
  /// objects starting with "Dest".  For PODs, this is just memcpy.
  template<typename It1, typename It2>
  static It2 move(It1 I, It1 E, It2 Dest) {
    return ::std::copy(I, E, Dest);
  }

  /// move_backward - Use move-assignment to move the range
  /// [I, E) onto the objects ending at "Dest", moving objects
  /// in reverse order.
  template<typename It1, typename It2>
  static It2 move_backward(It1 I, It1 E, It2 Dest) {
    return ::std::copy_backward(I, E, Dest);
  }

  /// uninitialized_move - Move the range [I, E) onto the uninitialized memory
  /// starting with "Dest", constructing elements into it as needed.
  template<typename It1, typename It2>
  static void uninitialized_move(It1 I, It1 E, It2 Dest) {
    // Just do a copy.
    uninitialized_copy(I, E, Dest);
  }

  /// uninitialized_copy - Copy the range [I, E) onto the uninitialized memory
  /// starting with "Dest", constructing elements into it as needed.
  template <typename It1, typename It2>
  static void uninitialized_copy(It1 I, It1 E, It2 Dest) {
    // Arbitrary iterator types; just use the basic implementation.
    std::uninitialized_copy(I, E, Dest);
  }

  /// uninitialized_copy - Copy the range [I, E) onto the uninitialized memory
  /// starting with "Dest", constructing elements into it as needed.
  template <typename T1, typename T2>
  static void uninitialized_copy(T1 *I, T1 *E, T2 *Dest) {
    // Use memcpy for PODs iterated by pointers (which includes SmallVector
    // iterators): std::uninitialized_copy optimizes to memmove, but we can
    // use memcpy here.
    memcpy(Dest, I, (E-I)*sizeof(T));
  }

  /// grow - double the size of the allocated memory, guaranteeing space for at
  /// least one more element or MinSize if specified.
  void grow(size_t MinSize = 0) {
    this->grow_pod(MinSize*sizeof(T), sizeof(T));
  }
public:
  void push_back(const T &Elt) {
    if (this->EndX < this->CapacityX) {
    Retry:
      memcpy(this->end(), &Elt, sizeof(T));
      this->setEnd(this->end()+1);
      return;
    }
    this->grow();
    goto Retry;
  }

  void pop_back() {
    this->setEnd(this->end()-1);
  }
};


/// SmallVectorImpl - This class consists of common code factored out of the
/// SmallVector class to reduce code duplication based on the SmallVector 'N'
/// template parameter.
template <typename T>
class SmallVectorImpl : public SmallVectorTemplateBase<T, NMpl::TIsPod<T>::Value> {
  typedef SmallVectorTemplateBase<T, NMpl::TIsPod<T>::Value > SuperClass;

  SmallVectorImpl(const SmallVectorImpl&); // DISABLED.
public:
  typedef typename SuperClass::iterator iterator;
  typedef typename SuperClass::size_type size_type;

protected:
  // Default ctor - Initialize to empty.
  explicit SmallVectorImpl(unsigned N)
    : SmallVectorTemplateBase<T, NMpl::TIsPod<T>::Value>(N*sizeof(T)) {
  }

public:
  ~SmallVectorImpl() {
    // Destroy the constructed elements in the vector.
    this->destroy_range(this->begin(), this->end());

    // If this wasn't grown from the inline copy, deallocate the old space.
    if (!this->isSmall())
      free(this->begin());
  }


  void clear() {
    this->destroy_range(this->begin(), this->end());
    this->EndX = this->BeginX;
  }

  void resize(unsigned N) {
    if (N < this->size()) {
      this->destroy_range(this->begin()+N, this->end());
      this->setEnd(this->begin()+N);
    } else if (N > this->size()) {
      if (this->capacity() < N)
        this->grow(N);
      std::uninitialized_fill(this->end(), this->begin()+N, T());
      this->setEnd(this->begin()+N);
    }
  }

  void resize(unsigned N, const T &NV) {
    if (N < this->size()) {
      this->destroy_range(this->begin()+N, this->end());
      this->setEnd(this->begin()+N);
    } else if (N > this->size()) {
      if (this->capacity() < N)
        this->grow(N);
      std::uninitialized_fill(this->end(), this->begin()+N, NV);
      this->setEnd(this->begin()+N);
    }
  }

  void reserve(unsigned N) {
    if (this->capacity() < N)
      this->grow(N);
  }

  T pop_back_val() {
#if LLVM_USE_RVALUE_REFERENCES
    T Result = ::std::move(this->back());
#else
    T Result = this->back();
#endif
    this->pop_back();
    return Result;
  }

  void swap(SmallVectorImpl &RHS);

  /// append - Add the specified range to the end of the SmallVector.
  ///
  template <typename in_iter>
  void append(in_iter in_start, in_iter in_end) {
    size_type NumInputs = std::distance(in_start, in_end);
    // Grow allocated space if needed.
    if (NumInputs > size_type(this->capacity_ptr()-this->end()))
      this->grow(this->size()+NumInputs);

    // Copy the new elements over.
    // TODO: NEED To compile time dispatch on whether in_iter is a random access
    // iterator to use the fast uninitialized_copy.
    std::uninitialized_copy(in_start, in_end, this->end());
    this->setEnd(this->end() + NumInputs);
  }

  /// append - Add the specified range to the end of the SmallVector.
  ///
  void append(size_type NumInputs, const T &Elt) {
    // Grow allocated space if needed.
    if (NumInputs > size_type(this->capacity_ptr()-this->end()))
      this->grow(this->size()+NumInputs);

    // Copy the new elements over.
    std::uninitialized_fill_n(this->end(), NumInputs, Elt);
    this->setEnd(this->end() + NumInputs);
  }

  void assign(unsigned NumElts, const T &Elt) {
    clear();
    if (this->capacity() < NumElts)
      this->grow(NumElts);
    this->setEnd(this->begin()+NumElts);
    std::uninitialized_fill(this->begin(), this->end(), Elt);
  }

  template <class other_iterator>
  void assign(other_iterator begin, other_iterator end) {
    unsigned NumElts = std::distance(begin, end);
    clear();
    if (this->capacity() < NumElts)
      this->grow(NumElts);
    this->setEnd(this->begin()+NumElts);
    std::uninitialized_copy(begin, end, this->begin());
  }

  iterator erase(iterator I) {
    iterator N = I;
    // Shift all elts down one.
    std::copy(I+1, this->end(), I);
    // Drop the last elt.
    this->pop_back();
    return(N);
  }

  iterator erase(iterator S, iterator E) {
    iterator N = S;
    // Shift all elts down.
    iterator I = std::copy(E, this->end(), S);
    // Drop the last elts.
    this->destroy_range(I, this->end());
    this->setEnd(I);
    return(N);
  }

#if LLVM_USE_RVALUE_REFERENCES
  iterator insert(iterator I, T &&Elt) {
    if (I == this->end()) {  // Important special case for empty vector.
      this->push_back(::std::move(Elt));
      return this->end()-1;
    }

    if (this->EndX < this->CapacityX) {
    Retry:
      ::new ((void*) this->end()) T(::std::move(this->back()));
      this->setEnd(this->end()+1);
      // Push everything else over.
      this->move_backward(I, this->end()-1, this->end());

      // If we just moved the element we're inserting, be sure to update
      // the reference.
      T *EltPtr = &Elt;
      if (I <= EltPtr && EltPtr < this->EndX)
        ++EltPtr;

      *I = ::std::move(*EltPtr);
      return I;
    }
    size_t EltNo = I-this->begin();
    this->grow();
    I = this->begin()+EltNo;
    goto Retry;
  }
#endif

  iterator insert(iterator I, const T &Elt) {
    if (I == this->end()) {  // Important special case for empty vector.
      this->push_back(Elt);
      return this->end()-1;
    }

    if (this->EndX < this->CapacityX) {
    Retry:
      ::new ((void*) this->end()) T(this->back());
      this->setEnd(this->end()+1);
      // Push everything else over.
      this->move_backward(I, this->end()-1, this->end());

      // If we just moved the element we're inserting, be sure to update
      // the reference.
      const T *EltPtr = &Elt;
      if (I <= EltPtr && EltPtr < this->EndX)
        ++EltPtr;

      *I = *EltPtr;
      return I;
    }
    size_t EltNo = I-this->begin();
    this->grow();
    I = this->begin()+EltNo;
    goto Retry;
  }

  iterator insert(iterator I, size_type NumToInsert, const T &Elt) {
    // Convert iterator to elt# to avoid invalidating iterator when we reserve()
    size_t InsertElt = I - this->begin();

    if (I == this->end()) {  // Important special case for empty vector.
      append(NumToInsert, Elt);
      return this->begin()+InsertElt;
    }

    // Ensure there is enough space.
    reserve(static_cast<unsigned>(this->size() + NumToInsert));

    // Uninvalidate the iterator.
    I = this->begin()+InsertElt;

    // If there are more elements between the insertion point and the end of the
    // range than there are being inserted, we can use a simple approach to
    // insertion.  Since we already reserved space, we know that this won't
    // reallocate the vector.
    if (size_t(this->end()-I) >= NumToInsert) {
      T *OldEnd = this->end();
      append(this->end()-NumToInsert, this->end());

      // Copy the existing elements that get replaced.
      this->move_backward(I, OldEnd-NumToInsert, OldEnd);

      std::fill_n(I, NumToInsert, Elt);
      return I;
    }

    // Otherwise, we're inserting more elements than exist already, and we're
    // not inserting at the end.

    // Copy over the elements that we're about to overwrite.
    T *OldEnd = this->end();
    this->setEnd(this->end() + NumToInsert);
    size_t NumOverwritten = OldEnd-I;
    this->uninitialized_copy(I, OldEnd, this->end()-NumOverwritten);

    // Replace the overwritten part.
    std::fill_n(I, NumOverwritten, Elt);

    // Insert the non-overwritten middle part.
    std::uninitialized_fill_n(OldEnd, NumToInsert-NumOverwritten, Elt);
    return I;
  }

  template <typename ItTy>
  iterator insert(iterator I, ItTy From, ItTy To) {
    // Convert iterator to elt# to avoid invalidating iterator when we reserve()
    size_t InsertElt = I - this->begin();

    if (I == this->end()) {  // Important special case for empty vector.
      append(From, To);
      return this->begin()+InsertElt;
    }

    size_t NumToInsert = std::distance(From, To);

    // Ensure there is enough space.
    reserve(static_cast<unsigned>(this->size() + NumToInsert));

    // Uninvalidate the iterator.
    I = this->begin()+InsertElt;

    // If there are more elements between the insertion point and the end of the
    // range than there are being inserted, we can use a simple approach to
    // insertion.  Since we already reserved space, we know that this won't
    // reallocate the vector.
    if (size_t(this->end()-I) >= NumToInsert) {
      T *OldEnd = this->end();
      append(this->end()-NumToInsert, this->end());

      // Copy the existing elements that get replaced.
      this->move_backward(I, OldEnd-NumToInsert, OldEnd);

      std::copy(From, To, I);
      return I;
    }

    // Otherwise, we're inserting more elements than exist already, and we're
    // not inserting at the end.

    // Copy over the elements that we're about to overwrite.
    T *OldEnd = this->end();
    this->setEnd(this->end() + NumToInsert);
    size_t NumOverwritten = OldEnd-I;
    this->uninitialized_copy(I, OldEnd, this->end()-NumOverwritten);

    // Replace the overwritten part.
    for (T *J = I; NumOverwritten > 0; --NumOverwritten) {
      *J = *From;
      ++J; ++From;
    }

    // Insert the non-overwritten middle part.
    this->uninitialized_copy(From, To, OldEnd);
    return I;
  }

  SmallVectorImpl &operator=(const SmallVectorImpl &RHS);

#if LLVM_USE_RVALUE_REFERENCES
  SmallVectorImpl &operator=(SmallVectorImpl &&RHS);
#endif

  bool operator==(const SmallVectorImpl &RHS) const {
    if (this->size() != RHS.size()) return false;
    return std::equal(this->begin(), this->end(), RHS.begin());
  }
  bool operator!=(const SmallVectorImpl &RHS) const {
    return !(*this == RHS);
  }

  bool operator<(const SmallVectorImpl &RHS) const {
    return std::lexicographical_compare(this->begin(), this->end(),
                                        RHS.begin(), RHS.end());
  }

  /// set_size - Set the array size to \arg N, which the current array must have
  /// enough capacity for.
  ///
  /// This does not construct or destroy any elements in the vector.
  ///
  /// Clients can use this in conjunction with capacity() to write past the end
  /// of the buffer when they know that more elements are available, and only
  /// update the size later. This avoids the cost of value initializing elements
  /// which will only be overwritten.
  void set_size(unsigned N) {
    assert(N <= this->capacity());
    this->setEnd(this->begin() + N);
  }
};


template <typename T>
void SmallVectorImpl<T>::swap(SmallVectorImpl<T> &RHS) {
  if (this == &RHS) return;

  // We can only avoid copying elements if neither vector is small.
  if (!this->isSmall() && !RHS.isSmall()) {
    std::swap(this->BeginX, RHS.BeginX);
    std::swap(this->EndX, RHS.EndX);
    std::swap(this->CapacityX, RHS.CapacityX);
    return;
  }
  if (RHS.size() > this->capacity())
    this->grow(RHS.size());
  if (this->size() > RHS.capacity())
    RHS.grow(this->size());

  // Swap the shared elements.
  size_t NumShared = this->size();
  if (NumShared > RHS.size()) NumShared = RHS.size();
  for (unsigned i = 0; i != static_cast<unsigned>(NumShared); ++i)
    std::swap((*this)[i], RHS[i]);

  // Copy over the extra elts.
  if (this->size() > RHS.size()) {
    size_t EltDiff = this->size() - RHS.size();
    this->uninitialized_copy(this->begin()+NumShared, this->end(), RHS.end());
    RHS.setEnd(RHS.end()+EltDiff);
    this->destroy_range(this->begin()+NumShared, this->end());
    this->setEnd(this->begin()+NumShared);
  } else if (RHS.size() > this->size()) {
    size_t EltDiff = RHS.size() - this->size();
    this->uninitialized_copy(RHS.begin()+NumShared, RHS.end(), this->end());
    this->setEnd(this->end() + EltDiff);
    this->destroy_range(RHS.begin()+NumShared, RHS.end());
    RHS.setEnd(RHS.begin()+NumShared);
  }
}

template <typename T>
SmallVectorImpl<T> &SmallVectorImpl<T>::
  operator=(const SmallVectorImpl<T> &RHS) {
  // Avoid self-assignment.
  if (this == &RHS) return *this;

  // If we already have sufficient space, assign the common elements, then
  // destroy any excess.
  size_t RHSSize = RHS.size();
  size_t CurSize = this->size();
  if (CurSize >= RHSSize) {
    // Assign common elements.
    iterator NewEnd;
    if (RHSSize)
      NewEnd = std::copy(RHS.begin(), RHS.begin()+RHSSize, this->begin());
    else
      NewEnd = this->begin();

    // Destroy excess elements.
    this->destroy_range(NewEnd, this->end());

    // Trim.
    this->setEnd(NewEnd);
    return *this;
  }

  // If we have to grow to have enough elements, destroy the current elements.
  // This allows us to avoid copying them during the grow.
  // FIXME: don't do this if they're efficiently moveable.
  if (this->capacity() < RHSSize) {
    // Destroy current elements.
    this->destroy_range(this->begin(), this->end());
    this->setEnd(this->begin());
    CurSize = 0;
    this->grow(RHSSize);
  } else if (CurSize) {
    // Otherwise, use assignment for the already-constructed elements.
    std::copy(RHS.begin(), RHS.begin()+CurSize, this->begin());
  }

  // Copy construct the new elements in place.
  this->uninitialized_copy(RHS.begin()+CurSize, RHS.end(),
                           this->begin()+CurSize);

  // Set end.
  this->setEnd(this->begin()+RHSSize);
  return *this;
}

#if LLVM_USE_RVALUE_REFERENCES
template <typename T>
SmallVectorImpl<T> &SmallVectorImpl<T>::operator=(SmallVectorImpl<T> &&RHS) {
  // Avoid self-assignment.
  if (this == &RHS) return *this;

  // If the RHS isn't small, clear this vector and then steal its buffer.
  if (!RHS.isSmall()) {
    this->destroy_range(this->begin(), this->end());
    if (!this->isSmall()) free(this->begin());
    this->BeginX = RHS.BeginX;
    this->EndX = RHS.EndX;
    this->CapacityX = RHS.CapacityX;
    RHS.resetToSmall();
    return *this;
  }

  // If we already have sufficient space, assign the common elements, then
  // destroy any excess.
  size_t RHSSize = RHS.size();
  size_t CurSize = this->size();
  if (CurSize >= RHSSize) {
    // Assign common elements.
    iterator NewEnd = this->begin();
    if (RHSSize)
      NewEnd = this->move(RHS.begin(), RHS.end(), NewEnd);

    // Destroy excess elements and trim the bounds.
    this->destroy_range(NewEnd, this->end());
    this->setEnd(NewEnd);

    // Clear the RHS.
    RHS.clear();

    return *this;
  }

  // If we have to grow to have enough elements, destroy the current elements.
  // This allows us to avoid copying them during the grow.
  // FIXME: this may not actually make any sense if we can efficiently move
  // elements.
  if (this->capacity() < RHSSize) {
    // Destroy current elements.
    this->destroy_range(this->begin(), this->end());
    this->setEnd(this->begin());
    CurSize = 0;
    this->grow(RHSSize);
  } else if (CurSize) {
    // Otherwise, use assignment for the already-constructed elements.
    this->move(RHS.begin(), RHS.end(), this->begin());
  }

  // Move-construct the new elements in place.
  this->uninitialized_move(RHS.begin()+CurSize, RHS.end(),
                           this->begin()+CurSize);

  // Set end.
  this->setEnd(this->begin()+RHSSize);

  RHS.clear();
  return *this;
}
#endif

/// SmallVector - This is a 'vector' (really, a variable-sized array), optimized
/// for the case when the array is small.  It contains some number of elements
/// in-place, which allows it to avoid heap allocation when the actual number of
/// elements is below that threshold.  This allows normal "small" cases to be
/// fast without losing generality for large inputs.
///
/// Note that this does not attempt to be exception safe.
///
template <typename T, unsigned N>
class SmallVector : public SmallVectorImpl<T> {
  /// InlineElts - These are 'N-1' elements that are stored inline in the body
  /// of the vector.  The extra '1' element is stored in SmallVectorImpl.
  typedef typename SmallVectorImpl<T>::U U;
  enum {
    // MinUs - The number of U's require to cover N T's.
    MinUs = (static_cast<unsigned int>(sizeof(T))*N +
             static_cast<unsigned int>(sizeof(U)) - 1) /
            static_cast<unsigned int>(sizeof(U)),

    // NumInlineEltsElts - The number of elements actually in this array.  There
    // is already one in the parent class, and we have to round up to avoid
    // having a zero-element array.
    NumInlineEltsElts = MinUs > 1 ? (MinUs - 1) : 1,

    // NumTsAvailable - The number of T's we actually have space for, which may
    // be more than N due to rounding.
    NumTsAvailable = (NumInlineEltsElts+1)*static_cast<unsigned int>(sizeof(U))/
                     static_cast<unsigned int>(sizeof(T))
  };
  U InlineElts[NumInlineEltsElts];
public:
  SmallVector() : SmallVectorImpl<T>(NumTsAvailable) {
  }

  explicit SmallVector(unsigned Size, const T &Value = T())
    : SmallVectorImpl<T>(NumTsAvailable) {
    this->assign(Size, Value);
  }

  template <typename ItTy>
  SmallVector(ItTy S, ItTy E) : SmallVectorImpl<T>(NumTsAvailable) {
    this->append(S, E);
  }

  SmallVector(const SmallVector &RHS) : SmallVectorImpl<T>(NumTsAvailable) {
    if (!RHS.empty())
      SmallVectorImpl<T>::operator=(RHS);
  }

  const SmallVector &operator=(const SmallVector &RHS) {
    SmallVectorImpl<T>::operator=(RHS);
    return *this;
  }

#if LLVM_USE_RVALUE_REFERENCES
  SmallVector(SmallVector &&RHS) : SmallVectorImpl<T>(NumTsAvailable) {
    if (!RHS.empty())
      SmallVectorImpl<T>::operator=(::std::move(RHS));
  }

  const SmallVector &operator=(SmallVector &&RHS) {
    SmallVectorImpl<T>::operator=(::std::move(RHS));
    return *this;
  }
#endif

};

/// Specialize SmallVector at N=0.  This specialization guarantees
/// that it can be instantiated at an incomplete T if none of its
/// members are required.
template <typename T>
class SmallVector<T,0> : public SmallVectorImpl<T> {
public:
  SmallVector() : SmallVectorImpl<T>(0) {}

  explicit SmallVector(unsigned Size, const T &Value = T())
    : SmallVectorImpl<T>(0) {
    this->assign(Size, Value);
  }

  template <typename ItTy>
  SmallVector(ItTy S, ItTy E) : SmallVectorImpl<T>(0) {
    this->append(S, E);
  }

  SmallVector(const SmallVector &RHS) : SmallVectorImpl<T>(0) {
    SmallVectorImpl<T>::operator=(RHS);
  }

  SmallVector &operator=(const SmallVectorImpl<T> &RHS) {
    return SmallVectorImpl<T>::operator=(RHS);
  }

};

template <typename T, unsigned N>
static inline size_t capacity_in_bytes(const SmallVector<T, N> &X) {
  return X.capacity_in_bytes();
}

} // namespace NYT

namespace std {

/// Implement std::swap in terms of SmallVector swap.
template <typename T>
inline void swap(NYT::SmallVectorImpl<T> &LHS, NYT::SmallVectorImpl<T> &RHS) {
    LHS.swap(RHS);
}

/// Implement std::swap in terms of SmallVector swap.
template <typename T, unsigned N>
inline void swap(NYT::SmallVector<T, N> &LHS, NYT::SmallVector<T, N> &RHS) {
    LHS.swap(RHS);
}

} // namespace std

#undef LLVM_HAS_RVALUE_REFERENCES
