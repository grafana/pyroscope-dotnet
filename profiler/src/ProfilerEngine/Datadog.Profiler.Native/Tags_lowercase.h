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

#ifndef THIRD_PARTY_JAVAPROFILER_TAGS_H_
#define THIRD_PARTY_JAVAPROFILER_TAGS_H_
#include <utility>
#include <vector>

#include "async_ref_counted_string.h"

namespace google {
namespace javaprofiler {

constexpr int kMaxNumTags = 16;

// Stores additional attributes as <key,value> pairs for profiles.
class Tags {
 public:
  // Returns the value (in AsyncRefCountedString reference) indexed by "key". Be
  // cautious that the referenced AsyncRefCountedString may change if modifiers
  // (e.g., Set(), Clear()) are invoked later.
  const AsyncRefCountedString &Get(const std::string &key) const;
  // Sets the value indexed by "key" to "value". It returns true on success;
  // otherwise returns false. The failure usually comes from that "key" does not
  // exist in the key list while there is no space to accommodate this new
  // "key".
  bool Set(const std::string &key, const AsyncRefCountedString &value);
  // Resets all values to nullptr.
  void ClearAll();
  // Returns all the key-value pairs stored in this Tags.
  std::vector<std::pair<std::string_view, AsyncRefCountedString>> GetAll() const;

  // Async-signal-safe version of operator=(const Tags &other). It requires that
  // the instance should be empty (does not refer to any string). Otherwise, it
  // asserts an error.
  void AsyncSafeCopy(const Tags &from);
  // Async-signal-safe version of ClearAll(). It will succeed if no enty is
  // removed from the AsyncRefCountedString internal string table. Otherwise, it
  // asserts an error.
  void AsyncSafeClearAll();
  // Async-signal-safe.
  bool operator==(const Tags &other) const;
  // Async-signal-safe.
  uint64_t Hash() const;

  // Async-signal-safe.
  static const Tags &Empty();
  // Initializes the internal for key table and empty Tags and
  // AsyncRefCountedString instances. Must be called after
  // AsyncRefCountedString::Init(), and before using Tags to store any key-value
  // pair or calling any other static method. Should only be called once,
  // subsequent calls have no effect and return false. Destroy() should be
  // called to free the storage.
  static bool Init();
  // Frees the internal storage allocated by Init(). Must be called before
  // AsyncRefCountedString::Destroy(), and after all outstanding Tags objects
  // are gone. No key-value pair can be stored after Destroy() is called.
  // Returns false if the storage is not currently allocated.
  static bool Destroy();

 private:
  // // Returns the id of "key" if "key" is present; otherwise, returns -1.
  static int32_t GetIdByKey(const std::string &key);

  AsyncRefCountedString values_[kMaxNumTags];

  friend class TagsTest;
};

}  // namespace javaprofiler
}  // namespace google
#endif  // THIRD_PARTY_JAVAPROFILER_TAGS_H_
