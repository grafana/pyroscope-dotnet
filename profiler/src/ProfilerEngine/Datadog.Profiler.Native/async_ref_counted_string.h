/*
 * Copyright 2018 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef THIRD_PARTY_JAVAPROFILER_ASYNC_REF_COUNTED_STRING_H_
#define THIRD_PARTY_JAVAPROFILER_ASYNC_REF_COUNTED_STRING_H_

#include <atomic>
#include <string>

namespace google {
namespace javaprofiler {

// A string wrapper which uses string interning to store only one copy of a
// string.
// Init() must be called before any usage and the internal storage allocated by
// Init() will intentionally leak.
// The methods that are safe to call in a signal handler, are explicitly marked
// with "async-signal-safe".
// An underlying string table helps maintain only one copy per string to reduce
// memory overhead and speed up equality checking. Different
// AsyncRefCountedString instances owned by different threads do not need extra
// synchronization even if they refer to the same string. However, users have to
// provide their own synchronization to access the same AsyncRefCountedString
// instance from different threads. String interning cannot be achieved only by
// std::shared_ptr<string> since an additional associative array is needed to
// look up a string's intern. Combining a hashtable, std::shared_ptr<string> can
// only de-allocate itself if not needed and will not remove it from the
// hashtable. Furthermore, a sampling interrupt can happen when
// std::shared_ptr<string> is being copied and destructed, and the interrupt
// routine may see corrupted data. In other words, std::shared_ptr<string> is
// not async-signal-safe and thus not used.
class AsyncRefCountedString {
 public:
  AsyncRefCountedString() : ptr_(nullptr) {}
  explicit AsyncRefCountedString(const std::string &str);
  AsyncRefCountedString(const AsyncRefCountedString &other);

  ~AsyncRefCountedString();

  AsyncRefCountedString &operator=(const std::string &str);
  AsyncRefCountedString &operator=(const AsyncRefCountedString &other);
  AsyncRefCountedString &operator=(AsyncRefCountedString &&other);
  // Async-signal-safe version of operator=(const AsyncRefCountedString &other).
  // It requires that the instance refers to nullptr (does not contain any
  // string). Otherwise, it asserts an error.
  AsyncRefCountedString &AsyncSafeCopy(const AsyncRefCountedString &other);
  // Tests whether the referred string equals the one in another
  // AsyncRefCountedString. As a specific string only has one internal copy, the
  // stored string address is directly used for comparison without checking the
  // string content. Async-signal-safe.
  bool operator==(const AsyncRefCountedString &other) const {
    return ptr_.load() == other.ptr_.load();
  }
  // Async-signal-safe.
  bool operator!=(const AsyncRefCountedString &other) const {
    return !(*this == other);
  }

  // Renounces the ownership of currently referred string (if it currently owns
  // one) and refers to nullptr.
  void Reset();
  // Async-signal-safe version of Reset(). It will succeed under the condition
  // that there is at least one AsyncRefCountedString still refers to the string
  // that is released by this call. Otherwise, it asserts an error.
  void AsyncSafeReset();

  // Returns the string pointer it refers.
  // Returns nullptr when it does not refer to any string.
  // The returned string pointer is valid as long as the AsyncRefCountedString
  // instance stay unchanged and it is async-signal-safe.
  const std::string *Get() const;

  // Returns the hash value. As a specific string only has one internal copy,
  // the stored string address is directly used as the hash value.
  uint64_t Hash() const { return reinterpret_cast<uintptr_t>(Get()); }

  // Initializes the internal string storage. Must be called before using
  // AsyncRefCountedString to store any string. Should only be called once,
  // subsequent calls have no effect and return false. Destroy() should be
  // called to free the storage.
  static bool Init();

  // Frees the internal string storage. Must be called after all outstanding
  // AsyncRefCountedString objects are gone. No string can be stored after
  // Destroy() is called. Returns false if the storage is not currently
  // allocated or if there are known outstanding references to strings.
  static bool Destroy();

  static void CHECK(bool v)
  {
    if (!v) {
      abort();
    }
  }
 private:
  // The value of ptr_ requires to be changed atomically so that
  // its value will not get corrupted upon interrupts.
  std::atomic<std::pair<const std::string, std::atomic<int32_t>> *> ptr_;
};

}  // namespace javaprofiler
}  // namespace google

#endif  // THIRD_PARTY_JAVAPROFILER_ASYNC_REF_COUNTED_STRING_H_
