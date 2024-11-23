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

#include "tags.h"

#include <algorithm>
#include <mutex>  // NOLINT
#include <unordered_map>

namespace google {
namespace javaprofiler {
namespace {

// Protects the global variables keys and key_to_id.
std::mutex mutex;
const Tags *empty_tags = nullptr;
const AsyncRefCountedString *empty_async_string = nullptr;
// Stores all the keys.
std::vector<std::string> *keys = nullptr;
// Maps key to its id. The index of a key stored in keys (called id or key_id)
// is used to retrieve a key or value quickly as it is very friendly to vectors.
std::unordered_map<std::string, int32_t> *key_to_id = nullptr;

// Returns the id of the key if the key has been registered. Otherwise, it tries
// to add the key and returns its id upon success or returns -1 if there is no
// extra space.
int32_t RegisterKey(const std::string &key) {
  std::lock_guard<std::mutex> lock(mutex);
  const auto &target = key_to_id->find(key);
  if (target != key_to_id->end()) {
    // Key is found and directly return its id.
    return target->second;
  }
  if (keys->size() >= kMaxNumTags) {
    // No space to register a new key.
    return -1;
  }
  // Enough space to register a new key.
  keys->emplace_back(key);
  int32_t id = keys->size() - 1;
  (*key_to_id)[key] = id;
  return id;
}

}  // namespace


// Returns the id of the key if the key has been registered; otherwise, returns
// -1.
int32_t Tags::GetIdByKey(const std::string &key) {
  std::lock_guard<std::mutex> lock(mutex);
  auto target = key_to_id->find(key);
  if (target == key_to_id->end()) {
    return -1;
  }
  return target->second;
}

void Tags::AsyncSafeCopy(const Tags &from) {
  for (int i = 0; i < kMaxNumTags; i++) {
    values_[i].AsyncSafeCopy(from.values_[i]);
  }
}

bool Tags::Set(const std::string &key, const AsyncRefCountedString &value) {
  // All values are stored in the vector "values_" and "key_id" (resolved by
  // "key") is used to locate the right position.
  int32_t key_id = RegisterKey(key);
  if (key_id < 0) {
    return false;
  }
  values_[key_id] = value;
  return true;
}

void Tags::ClearAll() {
  for (auto &val : values_) {
    val.Reset();
  }
}

void Tags::AsyncSafeClearAll() {
  for (auto &val : values_) {
    val.AsyncSafeReset();
  }
}

bool Tags::operator==(const Tags &other) const {
  for (int i = 0; i < kMaxNumTags; i++) {
    if (values_[i] != other.values_[i]) {
      return false;
    }
  }
  return true;
}

uint64_t Tags::Hash() const {
    uint64_t h = 0;
  for (const auto &val : values_) {
    h += val.Hash();
    h += h << 10;
    h ^= h >> 6;
  }
  return h;
}

const AsyncRefCountedString &Tags::Get(const std::string &key) const {
  int32_t key_id = GetIdByKey(key);
  if (key_id >= 0) {
    return values_[key_id];
  }
  return *empty_async_string;
}

std::vector<std::pair<std::string_view, AsyncRefCountedString>> Tags::GetAll()
    const {
  std::lock_guard<std::mutex> lock(mutex);
  std::vector<std::pair<std::string_view, AsyncRefCountedString>> all_pairs(
      keys->size());
  for (int i = 0; i < keys->size(); i++) {
    all_pairs[i].first = (*keys)[i];
    all_pairs[i].second = values_[i];
  }
  return all_pairs;
}

const Tags &Tags::Empty() { return *empty_tags; }

bool Tags::Init() {
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (empty_tags != nullptr || empty_async_string != nullptr ||
        keys != nullptr || key_to_id != nullptr) {
      return false;
    }
    empty_async_string = new AsyncRefCountedString();
    keys = new std::vector<std::string>();
    key_to_id = new std::unordered_map<std::string, int32_t>();
    empty_tags = new Tags();
  }
  return true;
}

bool Tags::Destroy() {
  std::lock_guard<std::mutex> lock(mutex);
  if (empty_tags == nullptr || empty_async_string == nullptr ||
      keys == nullptr || key_to_id == nullptr) {
    return false;
  }
  delete empty_tags;
  empty_tags = nullptr;
  delete key_to_id;
  key_to_id = nullptr;
  delete keys;
  keys = nullptr;
  delete empty_async_string;
  empty_async_string = nullptr;
  return true;
}

}  // namespace javaprofiler
}  // namespace google
