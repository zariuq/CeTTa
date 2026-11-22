#define main kernel_matching_case_main
#include "test_match_worklist_frames.c"
#undef main
#define KERNEL_CASE_ARGV 1
#include "native_kernel_matching_runner.h"
