#pragma once

namespace boss::engines::bulk {

class UniqueId {
public:
  using type = void (*)();

  template <typename T> static constexpr type forType() { return typeId<T>; }

private:
  template <typename T> static void typeId() {}
};

} // namespace boss::engines::bulk