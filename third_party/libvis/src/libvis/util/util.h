// Copyright 2019 ETH Zürich, Thomas Schöps
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.


#pragma once

#include <array>
#include <limits>
#include <vector>

#include "libvis/libvis.h"

namespace vis {

/// Function that returns the size in bytes of a std::vector.
///
/// The issue with std::vector::size() is that it returns the size in elements.
/// If you have a std::vector<u8>, then this is also the size in bytes, so you might write "vec.size()" where
/// you need its size in bytes. But if you later change the vector to have a type with a different size,
/// then this member function does not give the size in bytes anymore,
/// which makes code such as the above break that assumes this.
///
/// To prevent this issue from happening, this size_in_bytes() function should be used, which expresses
/// the intent to get the size in bytes rather than elements and keeps returning the correct value regardless
/// of the type of the std::vector elements.
template <typename T>
inline usize size_in_bytes(const vector<T>& vec) {
  return vec.size() * sizeof(T);
}

/// See size_in_bytes() for vectors above. This is the analogous function for std::array.
template <typename T, usize N>
inline usize size_in_bytes(const array<T, N>& /*arr*/) {
  return N * sizeof(T);
}

/// Fast, after compiler optimizations likely condition-free "proper" (positive) modulo implementation (unlike C++'s % operator,
/// which may return negative values, which is often undesirable).
///
/// Notice that if m is a constant power-of-two value, then it is even faster to use: (value & (m - 1)).
///
/// See also: modulo_fast_unsafe().
///
/// Taken from: https://stackoverflow.com/a/58118871
inline unsigned modulo(int value, unsigned m) {
  int mod = value % static_cast<int>(m);
  if (mod < 0) {
    mod += m;
  }
  return mod;
}

/// Fast, condition-free "proper" (positive) modulo implementation (unlike C++'s % operator,
/// which may return negative values, which is often undesirable).
///
/// Attention: Works only if the compiler implements right-shift of signed values as an arithmetic
///            shift (replicating the sign bit) instead of as a logical shift (adding in zeros).
///            This is "usually" the case, but is implementation-dependent! See:
///            https://stackoverflow.com/questions/7622/are-the-shift-operators-arithmetic-or-logical-in-c
///
/// TODO: Use of this function could be made safe by enabling it with a #define (in place of the other modulo() function above) after checking the compiler's behavior.
///
/// Notice that if m is a constant power-of-two value, then it is even faster to use: (value & (m - 1)).
///
/// See also: modulo().
///
/// Taken from: https://stackoverflow.com/a/58118871
inline unsigned modulo_fast_unsafe(int value, unsigned m) {
  // Get the modulo result that may be negative
  int mod = value % static_cast<int>(m);
  
  // Shift the sign bit of the result over the whole number
  // to get a bitmask as a result that is all-one if the result was negative,
  // and all-zero if the result was positive.
  // NOTE: This only works in case right-shift of signed values is implemented
  //       as an arithmetic (instead of logical) shift. This is implementation-dependent!
  // Use the bitmask to set m to zero in case the result was positive (or leave it unchanged otherwise).
  m &= mod >> std::numeric_limits<int>::digits;
  
  // If the result from the % oeprator was negative, add m to get the positive result.
  return mod + m;
}

/// This is a replacement for the following construct using C++ standard library
/// functionality:
/// 
/// some_vector.erase(std::remove_if(some_vector.begin(), some_vector.end(), condition), some_vector.end());
/// 
/// Using erase_if, an equivalent expression is this:
/// 
/// erase_if(some_vector, condition);
/// 
/// Currently the implementation is specific to vectors, but it could be
/// expanded easily.
template <typename T, typename Cond>
void erase_if(vector<T>& container, Cond condition) {
  const T* end_ptr = container.data() + container.size();
  
  T* write_ptr = container.data();
  while (write_ptr < end_ptr &&
         !condition(*write_ptr)) {
    ++ write_ptr;
  }
  
  const T* read_ptr = write_ptr + 1;
  while (read_ptr < end_ptr) {
    if (!condition(*read_ptr)) {
      *write_ptr = *read_ptr;
      ++ write_ptr;
    }
    ++ read_ptr;
  }
  
  // Should not use resize() instead of erase() since the former requires a
  // default constructor for T to be present while the latter does not.
  container.erase(
      container.begin() + static_cast<int>(write_ptr - container.data()),
      container.end());
}

/// Removes a single element with the given value from the container vector.
/// The function thus only iterates over the vector (from the back) until it finds the first element having this value.
/// It returns true if such an element was found (and removed), and false otherwise.
template <typename T>
bool remove_one(vector<T>& container, const T& value) {
  // The reason for the backwards iteration is that if there are multiple matching elements,
  // it is faster to erase the last one than the first one, since there are less following
  // elements for the last one that need to be moved.
  for (int i = static_cast<int>(container.size()) - 1; i >= 0; -- i) {
    if (container[i] == value) {
      container.erase(container.begin() + i);
      return true;
    }
  }
  return false;
}

/// Get a key press from the terminal without requiring the user to press Return
/// to confirm. If no key has been pressed, waits until one is available.
char GetKeyInput();

/// Polls for a key press in the terminal. If no key was pressed, returns EOF.
int PollKeyInput();

}
