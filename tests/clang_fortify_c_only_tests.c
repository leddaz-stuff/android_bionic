#include <string.h>

// This is a test specifically of bionic's FORTIFY machinery. Other stdlibs need not apply.
#ifdef __BIONIC__

// Ensure that strlen can be evaluated at compile-time. Clang doesn't support
// this in C++, but does in C.
_Static_assert(strlen("foo") == 3, "");

#endif  // __BIONIC__
