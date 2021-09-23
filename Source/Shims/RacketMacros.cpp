#include <string>
/**
 * This is some racket code that establishes a convenient interface to BOSS --
 * right now I am just doing it using a raw string literal (maybe we'll use our expression builder
 * at some point)
 */
std::string getRacketMacroShims() {
  // NOLINTNEXTLINE
  return
      R"(
;; Begin Racket
;; End Racket
     )";
}

// Local Variables:
// mode: poly-c++
// fill-column: 0
// End:
