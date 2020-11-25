#pragma once

#include <array>
#include <string>
#include <unordered_set>
#include <vector>

namespace UniqueString {

inline char const* MakeUnique(char const* str) {
  static std::unordered_set<std::string> uniqueStrings;
  auto const& it = uniqueStrings.insert(std::string(str)).first;
  return it->c_str();

  // TODO: check which version is better
  /*static size_t currentBucket = 0;
  static size_t bucketSize const = 1000;
  static size_t numBuckets const = 10000;
  static std::array<std::vector<std::string>, numBuckets> cachedStrings;

  if(cachedStrings[currentBucket].size() < cachedStrings[currentBucket].capacity()) {
      cachedStrings[currentBucket].push_back(value);
  }
  else if(cachedStrings[currentBucket].capacity() > 0) {
      // need to start a new bucket of strings
      if(currentBucket < numBuckets) {
          ++currentBucket;
          cachedStrings[currentBucket].reserve(bucketSize);
      }
      else {
          // no more space to allocate strings
          return false;
      }

      cachedStrings[currentBucket].push_back(value);
  }
  else {
      // initialisation of the first bucket
      cachedStrings[currentBucket].reserve(bucketSize);
      cachedStrings[currentBucket].push_back(value);
  }
  std::string const & uniqueStr = cachedStrings[currentBucket].back();
  char const * rawData = reinterpret_cast<char const *>(&uniqueStr);*/
}

} // namespace UniqueString
