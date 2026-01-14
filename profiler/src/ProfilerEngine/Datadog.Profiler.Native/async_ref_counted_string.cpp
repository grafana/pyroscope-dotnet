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

#include "async_ref_counted_string.h"

#include <mutex> // NOLINT
#include <unordered_map>

namespace google {
namespace javaprofiler {
namespace {

using StringRefCountTable =
    std::unordered_map<std::string, std::atomic<int32_t>>;
using StringRefCount = StringRefCountTable::value_type;

// Maps string to its reference count.
std::unordered_map<std::string, std::atomic<int32_t>> *string_table = nullptr;
// All accesses to string_table should be protected by string_table_mutex.
std::mutex string_table_mutex;

// The following methods are used to transfer ownership of a string.
// To own a string, the first step is to resolve (or create if necessary) the
// corresponding StringRefCount and claim the ownership by incrementing the
// reference count with either of AcquireByXXX methods. Externally, the
// current AsyncRefCountedString still refers to the old StringRefCount. Next,
// switch ptr_ to the new StringRefCount atomically and release the resource of
// old StringRefCount by calling Release() (or calling AsyncSafeRelease(), but
// it may fail). If the reference count of old StringRefCount reaches 0, the
// string stored in the old StringRefCount is removed. The ownership
// transference is safe upon interrupts. In the signal handler, the current
// instance of AsyncRefCountedString refers to either the old StringRefCount or
// the new StringRefCount without ending up with corrupted data.

// Resolves the StringRefCount of a given string, increments the reference count
// and returns the pointer of StringRefCount. It returns nullptr if the internal
// string table is not initialized.
StringRefCount *AcquireByString(const std::string &str) {
  std::lock_guard<std::mutex> lock(string_table_mutex);
  if (string_table == nullptr) {
    return nullptr;
  }
  // The following statement is equivalent to
  //   "const auto &it = string_table->emplace(str, 0).first".
  // However, gcc-4.8 does not compile "emplace(str,0)" as it complains that the
  // atomic copy constructor is deleted. Since gcc-4.8 is used in the docker
  // container to generate the cloud Java agent, use the pair's piecewise
  // constructor to make it compilable under gcc-4.8
  const auto &it =
      string_table
          ->emplace(std::piecewise_construct, std::forward_as_tuple(str),
                    std::forward_as_tuple(0))
          .first;
  it->second++;
  return &*it;
}

// Increments the reference count of the given StringRefCount pointer if it is
// not null, and returns the StringRefCount pointer. It is used to copy the
// StringRefCount from another AsyncRefCountedString.
StringRefCount *AcquireByCopy(StringRefCount *str_ref_cnt) {
  if (str_ref_cnt != nullptr) {
    // As "other" already holds a valid StringRefCount, we can directly
    // increment the reference count by 1. If "other" is owned by another
    // thread, the user has to make sure that "other" is not under editing.
    str_ref_cnt->second++;
  }
  return str_ref_cnt;
}

// Renounces the ownership of a string represented by the given StringRefCount
// pointer. It is async-signal-safe. If the string is not referred by any other
// AsyncRefCountedString and needs removing from the string table, it fails by
// returning false.
bool AsyncSafeRelease(StringRefCount *str_ref_cnt) {
  if (str_ref_cnt == nullptr) {
    return true;
  }

  int32_t ref_count = str_ref_cnt->second.load();
  while (ref_count > 1) {
    if (str_ref_cnt->second.compare_exchange_weak(ref_count, ref_count - 1)) {
      return true;
    }
  }
  return false;
}

// Renounces the ownership of a string represented by the given StringRefCount
// pointer. It firstly calls AsyncSafeRelease() to release the string. If
// AsyncSafeRelease(), it acquires the lock of the string table to make it
// succeed eventually.
void Release(StringRefCount *str_ref_cnt) {
  if (!AsyncSafeRelease(str_ref_cnt)) {
    std::lock_guard<std::mutex> lock(string_table_mutex);
    if (str_ref_cnt->second.fetch_sub(1) == 1) {
      // The counter becomes zero and delete the entry.
      string_table->erase(str_ref_cnt->first);
    }
  }
}

}  // namespace

AsyncRefCountedString::AsyncRefCountedString(const std::string &str)
    : AsyncRefCountedString() {
  Release(ptr_.exchange(AcquireByString(str)));
}

AsyncRefCountedString::AsyncRefCountedString(const AsyncRefCountedString &other)
    : AsyncRefCountedString() {
  Release(ptr_.exchange(AcquireByCopy(other.ptr_.load())));
}

AsyncRefCountedString::~AsyncRefCountedString() {
  Release(ptr_.exchange(nullptr));
}

AsyncRefCountedString &AsyncRefCountedString::operator=(
    const std::string &str) {
  Release(ptr_.exchange(AcquireByString(str)));
  return *this;
}

AsyncRefCountedString &AsyncRefCountedString::operator=(
    const AsyncRefCountedString &other) {
  Release(ptr_.exchange(AcquireByCopy(other.ptr_.load())));
  return *this;
}

AsyncRefCountedString &AsyncRefCountedString::operator=(
    AsyncRefCountedString &&other) {
  Reset();
  ptr_.store(other.ptr_);
  other.ptr_ = nullptr;
  return *this;
}

AsyncRefCountedString &AsyncRefCountedString::AsyncSafeCopy(
    const AsyncRefCountedString &other) {
  CHECK(ptr_.load() == nullptr);
  ptr_.exchange(AcquireByCopy(other.ptr_.load()));
  return *this;
}

void AsyncRefCountedString::Reset() { Release(ptr_.exchange(nullptr)); }

void AsyncRefCountedString::AsyncSafeReset() {
  CHECK(AsyncSafeRelease(ptr_.exchange(nullptr)));
}

const std::string *AsyncRefCountedString::Get() const {
  StringRefCount *str_ref_cnt = ptr_.load();
  if (str_ref_cnt == nullptr) {
    return nullptr;
  } else {
    return &(str_ref_cnt->first);
  }
}

bool AsyncRefCountedString::Init() {
  std::lock_guard<std::mutex> lock(string_table_mutex);
  if (string_table == nullptr) {
    string_table = new std::unordered_map<std::string, std::atomic<int32_t>>();
    return true;
  }
  return false;
}

bool AsyncRefCountedString::Destroy() {
  std::lock_guard<std::mutex> lock(string_table_mutex);
  if (string_table == nullptr || !string_table->empty()) {
    return false;
  }
  delete string_table;
  string_table = nullptr;
  return true;
}

}  // namespace javaprofiler
}  // namespace google
