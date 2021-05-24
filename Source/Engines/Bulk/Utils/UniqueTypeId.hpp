#pragma once

namespace boss::engines::bulk {

/** Utility to return a compile-time type unique for any class T.
 * We use it visit or check a list of supported type for the Batch classes.
 * See BatchVisitDispatcher for usage.
 * We can obtain a similar behaviour using standard typeid()
 * but this implementation doesn't require RTTI. */
class UniqueId {
public:
  // this type is really just a function pointer which allows us to compare types
  // by comparing pointers to different implementations of typeId()
  using type = void (*)();

  template <typename T> static constexpr type forType() { return typeId<T>; }

private:
  // typeId() is unique for each implementation of the template.
  // It exists only for doing the function pointer comparison.
  template <typename T> static void typeId() {}
};

} // namespace boss::engines::bulk
