#pragma once

// Clang Thread Safety Analysis support for BOSS core.
//
// Provides:
//   * BOSS_GUARDED_BY / BOSS_REQUIRES / BOSS_EXCLUDES / ... annotation macros, and
//   * a capability-annotated SharedMutex + SharedLock/UniqueLock RAII guards,
// so the compiler can statically prove that fields guarded by a SharedMutex are only touched
// while the lock is held. std::shared_mutex itself is not annotated by libc++/libstdc++, hence
// this thin wrapper.
//
// This header has no consumers yet -- the engine-state locking that uses it lands in a
// follow-up change. The analysis flags are switched on now so that annotated code is checked
// from the moment it is written.
//
// All macros are no-ops outside Clang (GCC/MSVC ignore the attributes), so the analysis is a
// Clang-only, zero-runtime-cost static check. See
// https://clang.llvm.org/docs/ThreadSafetyAnalysis.html

#include <shared_mutex>

#if !defined(NDEBUG)
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>
#endif

// These have to be macros: they expand to __attribute__((...)) in declaration position, which no
// constexpr function can stand in for, and they must vanish entirely on non-Clang compilers.
// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#if defined(__clang__)
#define BOSS_TS_ATTR(x) __attribute__((x))
#else
#define BOSS_TS_ATTR(x) // no-op on non-Clang compilers
#endif

#define BOSS_CAPABILITY(x) BOSS_TS_ATTR(capability(x))
#define BOSS_SCOPED_CAPABILITY BOSS_TS_ATTR(scoped_lockable)
#define BOSS_GUARDED_BY(x) BOSS_TS_ATTR(guarded_by(x))
#define BOSS_PT_GUARDED_BY(x) BOSS_TS_ATTR(pt_guarded_by(x))
#define BOSS_REQUIRES(...) BOSS_TS_ATTR(requires_capability(__VA_ARGS__))
#define BOSS_REQUIRES_SHARED(...) BOSS_TS_ATTR(requires_shared_capability(__VA_ARGS__))
#define BOSS_ACQUIRE(...) BOSS_TS_ATTR(acquire_capability(__VA_ARGS__))
#define BOSS_ACQUIRE_SHARED(...) BOSS_TS_ATTR(acquire_shared_capability(__VA_ARGS__))
#define BOSS_RELEASE(...) BOSS_TS_ATTR(release_capability(__VA_ARGS__))
#define BOSS_RELEASE_SHARED(...) BOSS_TS_ATTR(release_shared_capability(__VA_ARGS__))
#define BOSS_RELEASE_GENERIC(...) BOSS_TS_ATTR(release_generic_capability(__VA_ARGS__))
#define BOSS_EXCLUDES(...) BOSS_TS_ATTR(locks_excluded(__VA_ARGS__))
#define BOSS_NO_THREAD_SAFETY_ANALYSIS BOSS_TS_ATTR(no_thread_safety_analysis)
// NOLINTEND(cppcoreguidelines-macro-usage)

namespace boss::concurrency {

// Capability-annotated wrapper over std::shared_mutex.
class BOSS_CAPABILITY("shared_mutex") SharedMutex {
public:
  SharedMutex() = default;
  SharedMutex(SharedMutex const&) = delete;
  SharedMutex(SharedMutex&&) = delete;
  SharedMutex& operator=(SharedMutex const&) = delete;
  SharedMutex& operator=(SharedMutex&&) = delete;
  ~SharedMutex() = default;

  // Standard-library spelling on purpose: with these names the wrapper satisfies the
  // SharedMutex requirements, so it also works with std::shared_lock / std::unique_lock and
  // any other generic code expecting them, not just the guards below.
  void lock() BOSS_ACQUIRE() { mutex_.lock(); }
  void unlock() BOSS_RELEASE() { mutex_.unlock(); }
  void lock_shared() BOSS_ACQUIRE_SHARED() { mutex_.lock_shared(); }
  void unlock_shared() BOSS_RELEASE_SHARED() { mutex_.unlock_shared(); }

private:
  std::shared_mutex mutex_;
};

// RAII exclusive (writer) lock.
class BOSS_SCOPED_CAPABILITY UniqueLock {
public:
  explicit UniqueLock(SharedMutex& mutex) BOSS_ACQUIRE(mutex) : mutex_(mutex) { mutex_.lock(); }
  ~UniqueLock() BOSS_RELEASE() { mutex_.unlock(); }
  UniqueLock(UniqueLock const&) = delete;
  UniqueLock(UniqueLock&&) = delete;
  UniqueLock& operator=(UniqueLock const&) = delete;
  UniqueLock& operator=(UniqueLock&&) = delete;

private:
  SharedMutex& mutex_;
};

// RAII shared (reader) lock.
class BOSS_SCOPED_CAPABILITY SharedLock {
public:
  explicit SharedLock(SharedMutex& mutex) BOSS_ACQUIRE_SHARED(mutex) : mutex_(mutex) {
    mutex_.lock_shared();
  }
  ~SharedLock() BOSS_RELEASE() { mutex_.unlock_shared(); }
  SharedLock(SharedLock const&) = delete;
  SharedLock(SharedLock&&) = delete;
  SharedLock& operator=(SharedLock const&) = delete;
  SharedLock& operator=(SharedLock&&) = delete;

private:
  SharedMutex& mutex_;
};

// ── ConcurrencyTripwire ─────────────────────────────────────────────────────────────────────
// Debug-build-only guard that detects violations of the "one context per concurrent caller"
// rule: a context that is not itself thread-safe (such as a chibi-scheme context) must never be
// entered by two threads at once. Construct it at the top of a per-context evaluation,
// scoped to the call, passing the context handle as `key`. If another thread is already inside
// an evaluation on the same key, it aborts loudly — converting silent memory corruption
// (concurrent use of a non-thread-safe chibi context) into an immediate, diagnosable crash.
//
// Re-entry by the SAME thread on the same key is permitted (depth-counted), so ordinary
// nested/recursive evaluation on one context does not trip it.
//
// In release builds (NDEBUG) this is an empty object with a trivial constructor — zero cost.
#if !defined(NDEBUG)
class ConcurrencyTripwire {
public:
  explicit ConcurrencyTripwire(void const* key, char const* site = "evaluate") : key_(key) {
    std::lock_guard<std::mutex> const guard(registryMutex());
    auto& owners = registry();
    auto const it = owners.find(key_);
    if(it != owners.end()) {
      if(it->second.thread != std::this_thread::get_id()) {
        std::cerr << "\n*** BOSS ConcurrencyTripwire: context " << key_
                  << " entered concurrently from a second thread in " << site
                  << ".\n*** This violates the one-context-per-caller contract and would "
                     "corrupt the context."
                     "\n*** Aborting.\n"
                  << std::flush;
        std::abort();
      }
      ++it->second.depth; // same-thread re-entry: allowed
    } else {
      owners.emplace(key_, Owner {std::this_thread::get_id(), 1});
    }
  }

  ~ConcurrencyTripwire() {
    std::lock_guard<std::mutex> const guard(registryMutex());
    auto& owners = registry();
    auto const it = owners.find(key_);
    // The constructor always inserts at depth 1 or increments, so by construction there is an
    // entry with a non-zero depth here. If there is not, the registry has been corrupted, and
    // decrementing an unsigned depth would silently wrap to a huge value and strand the entry
    // forever. This whole class exists to turn quiet corruption into a diagnosable crash, so
    // hold it to its own standard.
    if(it == owners.end() || it->second.depth == 0) {
      std::cerr << "\n*** BOSS ConcurrencyTripwire: on leaving the scope for context " << key_
                << ", its registry entry is missing or already at depth 0."
                   "\n*** The tripwire registry is corrupt; the depth count cannot be trusted."
                   "\n*** Aborting.\n"
                << std::flush;
      std::abort();
    }
    if(--it->second.depth == 0) {
      owners.erase(it);
    }
  }

  ConcurrencyTripwire(ConcurrencyTripwire const&) = delete;
  ConcurrencyTripwire(ConcurrencyTripwire&&) = delete;
  ConcurrencyTripwire& operator=(ConcurrencyTripwire const&) = delete;
  ConcurrencyTripwire& operator=(ConcurrencyTripwire&&) = delete;

private:
  struct Owner {
    std::thread::id thread;
    unsigned depth;
  };
  // Function-local statics so the header stays single-include-safe (no ODR-violating globals).
  static std::unordered_map<void const*, Owner>& registry() {
    static std::unordered_map<void const*, Owner> instance;
    return instance;
  }
  static std::mutex& registryMutex() {
    static std::mutex instance;
    return instance;
  }
  void const* key_;
};
#else
class ConcurrencyTripwire {
public:
  explicit ConcurrencyTripwire(void const* /*key*/, char const* /*site*/ = "evaluate") {}
  ~ConcurrencyTripwire() = default;
  // Kept non-copyable/non-movable to match the debug build, so misuse is caught in both
  // configurations rather than only where the tripwire is active.
  ConcurrencyTripwire(ConcurrencyTripwire const&) = delete;
  ConcurrencyTripwire(ConcurrencyTripwire&&) = delete;
  ConcurrencyTripwire& operator=(ConcurrencyTripwire const&) = delete;
  ConcurrencyTripwire& operator=(ConcurrencyTripwire&&) = delete;
};
#endif

} // namespace boss::concurrency
