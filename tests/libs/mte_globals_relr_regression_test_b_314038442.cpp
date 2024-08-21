#include <stdint.h>
#include <stdio.h>
static volatile char array[0x10000];
volatile char* volatile oob_ptr = &array[0x111111111];

unsigned char get_tag(__attribute__((unused)) volatile void* ptr) {
#if defined(__aarch64__)
  return static_cast<unsigned char>(reinterpret_cast<uintptr_t>(ptr) >> 56) & 0xf;
#else   // !defined(__aarch64__)
  return 0;
#endif  // defined(__aarch64__)
}

int main() {
  printf("Program loaded successfully. %p %p. ", array, oob_ptr);
  if (get_tag(array) != get_tag(oob_ptr)) {
    printf("Tags are mismatched!\n");
    return 1;
  }
  if (get_tag(array) == 0) {
    printf("Tags are zero!\n");
  } else {
    printf("Tags are non-zero\n");
  }
  return 0;
}
