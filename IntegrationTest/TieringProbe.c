#include <stdint.h>

// Called directly from the same IL call site in the non-inlined HotLoop.
// SuppressGCTransition keeps this leaf call free of an interop stub frame.
__attribute__((noinline)) uintptr_t tiering_instruction_pointer(void)
{
    return (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0));
}
