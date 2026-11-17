#include "batch.h"
.text
.p2align 4
.macro TC_BATCH_READ
    mov %edi,%ecx
    and $255,%ecx
    shr $8,%edi
    cmp TBI_patterns(%r12),%rcx
    je .Lfield_constant\@
    mov 4*TC_BATCH_SELECTED(%r14,%rcx,4),%ecx
    mov TBI_row(%r12),%r8
    mov (%r8,%rcx,4),%ecx
    shl $5,%rcx
    add TBI_node(%r12),%rcx
    mov 8(%rcx),%edx
    add %rdi,%rdx
    mov TBI_item(%r12),%r8
    mov (%r8,%rdx,4),%eax
    jmp .Lfield_check\@
.Lfield_constant\@:
    mov TBI_plan(%r12),%r8
    mov TC_BP_CONSTANT(%r8,%rdi,4),%eax
.Lfield_check\@:
    test %eax,%eax
    jz .Lbatch_invalid
    cmp TBI_nodes(%r12),%rax
    jae .Lbatch_invalid
.endm
.globl tc_batch_x86
.hidden tc_batch_x86
.type tc_batch_x86,@function
tc_batch_x86:
    endbr64
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    sub $72,%rsp
    mov %rdi,%r12
    mov %rsi,%r13
    mov TBW_cursor(%r13),%r14
    mov TBW_output(%r13),%r15
    xor %ebp,%ebp
    movq $0,TBW_produced(%r13)
    movq $0,TBW_visited(%r13)
    mov TBI_plan(%r12),%rdx
    mov TC_BP_MAKES(%rdx),%eax
    add TBI_patterns(%r12),%rax
    mov $1,%edx
    test %rax,%rax
    cmovz %rdx,%rax
    mov %rax,0(%rsp)
    mov TBW_output_words(%r13),%rax
    mov %rax,8(%rsp)
    cmpl $0,0(%r14)
    jne .Lbatch_loop
    movl $1,0(%r14)
    movl $0,4(%r14)
    xor %eax,%eax
    cmpq $0,TBI_patterns(%r12)
    je .Lbatch_init_next
    mov TBI_stage(%r12),%rdx
    mov 4*TC_BATCH_LO0(%rdx),%eax
.Lbatch_init_next:
    mov %eax,4*TC_BATCH_NEXT(%r14)
    movl $0,4*TC_BATCH_TRIED(%r14)
.Lbatch_loop:
    cmpl $2,0(%r14)
    je .Lbatch_end
    mov 8(%rsp),%rax
    cmp 0(%rsp),%rax
    jb .Lbatch_yield
    mov TBW_visited(%r13),%rax
    cmp TBI_quantum(%r12),%rax
    jae .Lbatch_yield
    mov 4(%r14),%ebx
    cmp TBI_patterns(%r12),%rbx
    je .Lbatch_record
    mov %ebx,%eax
    shl $5,%rax
    add TBI_stage(%r12),%rax
    mov %rax,24(%rsp)
    mov %rax,%r10
    xor %edx,%edx
    testq $TC_BATCH_SYMMETRIC,TBI_flags(%r12)
    jz .Lbatch_position
    mov 4*TC_BATCH_TWIN(%r10),%eax
    cmp $-1,%eax
    je .Lbatch_position
    mov 4*TC_BATCH_SELECTED(%r14,%rax,4),%edx
    cmp TBI_takes(%r12),%rbx
    jae .Lbatch_position
    inc %edx
.Lbatch_position:
    mov 4*TC_BATCH_NEXT(%r14,%rbx,4),%eax
    cmp %edx,%eax
    cmovb %edx,%eax
    mov 4*TC_BATCH_LO0(%r10),%edx
    cmp %edx,%eax
    cmovb %edx,%eax
    cmp 4*TC_BATCH_HI0(%r10),%eax
    jb .Lbatch_propose
    mov 4*TC_BATCH_LO1(%r10),%edx
    cmp %edx,%eax
    cmovb %edx,%eax
    cmp 4*TC_BATCH_HI1(%r10),%eax
    jae .Lbatch_backtrack
.Lbatch_propose:
    mov TBW_used(%r13),%rdx
    cmp TBI_budget(%r12),%rdx
    je .Lbatch_exhausted
    incq TBW_used(%r13)
    incq TBW_visited(%r13)
    lea 1(%rax),%edx
    mov %edx,4*TC_BATCH_NEXT(%r14,%rbx,4)
    mov %rbx,%rdx
    cmp TBI_takes(%r12),%rbx
    jb .Lbatch_clashes
    mov TBI_takes(%r12),%rdx
.Lbatch_clashes:
    xor %ecx,%ecx
.Lbatch_clash_loop:
    cmp %edx,%ecx
    jae .Lbatch_row
    cmp 4*TC_BATCH_SELECTED(%r14,%rcx,4),%eax
    je .Lbatch_loop
    inc %ecx
    jmp .Lbatch_clash_loop
.Lbatch_row:
    mov TBI_row(%r12),%rdx
    mov (%rdx,%rax,4),%ecx
    test %ecx,%ecx
    jz .Lbatch_invalid
    cmp TBI_nodes(%r12),%rcx
    jae .Lbatch_invalid
    testq $TC_BATCH_ONE,TBI_flags(%r12)
    jz .Lbatch_shape
    cmp 4*TC_BATCH_TRIED(%r14,%rbx,4),%ecx
    je .Lbatch_loop
    mov %ecx,4*TC_BATCH_TRIED(%r14,%rbx,4)
.Lbatch_shape:
    shl $5,%rcx
    add TBI_node(%r12),%rcx
    cmpb $5,0(%rcx)
    jne .Lbatch_loop
    mov 4(%rcx),%edx
    cmp 4*TC_BATCH_LENGTH(%r10),%edx
    jne .Lbatch_loop
    cmpb $0,1(%rcx)
    je .Lbatch_invalid
    mov 8(%rcx),%edx
    mov TBI_items(%r12),%r9
    cmp %r9,%rdx
    ja .Lbatch_invalid
    sub %rdx,%r9
    mov 4(%rcx),%r8d
    cmp %r9,%r8
    ja .Lbatch_invalid
    mov TBI_item(%r12),%r11
    lea (%r11,%rdx,4),%r11
    mov %eax,4*TC_BATCH_SELECTED(%r14,%rbx,4)
    mov 4*TC_BATCH_GUARD_BEGIN(%r10),%eax
    mov %eax,32(%rsp)
.Lbatch_guard_loop:
    mov 24(%rsp),%r10
    mov 32(%rsp),%eax
    cmp 4*TC_BATCH_GUARD_END(%r10),%eax
    jae .Lbatch_descend
    mov TBI_plan(%r12),%rdx
    mov TC_BP_GUARD(%rdx,%rax,4),%edi
    mov %edi,%eax
    shr $16,%eax
    mov %eax,40(%rsp)
    shr $8,%edi
    and $255,%edi
    mov (%r11,%rdi,4),%eax
    test %eax,%eax
    jz .Lbatch_invalid
    cmp TBI_nodes(%r12),%rax
    jae .Lbatch_invalid
    mov %eax,44(%rsp)
    mov 40(%rsp),%edi
    TC_BATCH_READ
    cmp 44(%rsp),%eax
    jne .Lbatch_loop
    incl 32(%rsp)
    jmp .Lbatch_guard_loop
.Lbatch_descend:
    inc %ebx
    mov %ebx,4(%r14)
    cmp TBI_patterns(%r12),%rbx
    jae .Lbatch_loop
    mov %ebx,%eax
    shl $5,%rax
    add TBI_stage(%r12),%rax
    mov 4*TC_BATCH_LO0(%rax),%edx
    mov %edx,4*TC_BATCH_NEXT(%r14,%rbx,4)
    movl $0,4*TC_BATCH_TRIED(%r14,%rbx,4)
    jmp .Lbatch_loop
.Lbatch_backtrack:
    test %ebx,%ebx
    jz .Lbatch_finish
    dec %ebx
    mov %ebx,4(%r14)
    jmp .Lbatch_loop
.Lbatch_finish:
    movl $2,0(%r14)
    jmp .Lbatch_loop
.Lbatch_record:
    mov TBW_used(%r13),%rax
    cmp TBI_budget(%r12),%rax
    je .Lbatch_exhausted
    incq TBW_used(%r13)
    mov %r15,%rdi
    mov %rdi,48(%rsp)
    lea 4*TC_BATCH_SELECTED(%r14),%rsi
    mov %ebx,%ecx
    test %ecx,%ecx
    jz .Lbatch_copy_done
.Lbatch_copy:
    mov (%rsi),%eax
    mov %eax,(%rdi)
    add $4,%rsi
    add $4,%rdi
    dec %ecx
    jnz .Lbatch_copy
.Lbatch_copy_done:
    movl $0,56(%rsp)
.Lbatch_make_loop:
    mov 56(%rsp),%eax
    mov TBI_plan(%r12),%rdx
    cmp TC_BP_MAKES(%rdx),%eax
    jae .Lbatch_record_done
    mov TC_BP_MAKE(%rdx,%rax,4),%edi
    TC_BATCH_READ
    mov 56(%rsp),%ecx
    add %ebx,%ecx
    mov 48(%rsp),%rdx
    mov %eax,(%rdx,%rcx,4)
    incl 56(%rsp)
    jmp .Lbatch_make_loop
.Lbatch_record_done:
    mov 0(%rsp),%rax
    sub %rax,8(%rsp)
    lea (%r15,%rax,4),%r15
    inc %rbp
    mov %rbp,TBW_produced(%r13)
    jmp .Lbatch_backtrack
.Lbatch_end:
    xor %eax,%eax
    jmp .Lbatch_return
.Lbatch_yield:
    mov $TC_BATCH_YIELD,%eax
    jmp .Lbatch_return
.Lbatch_exhausted:
    mov $TC_BATCH_EXHAUSTED,%eax
    jmp .Lbatch_return
.Lbatch_invalid:
    mov $TC_BATCH_INVALID,%eax
.Lbatch_return:
    mov %rax,TBW_status(%r13)
    mov %rbp,TBW_produced(%r13)
    add $72,%rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret

.globl tc_batch_x86_end
.hidden tc_batch_x86_end
tc_batch_x86_end:
.size tc_batch_x86,.-tc_batch_x86

.section .rodata
.p2align 3
.globl tc_batch_x86_expected
.hidden tc_batch_x86_expected
tc_batch_x86_expected:
#include "native_batch_expected.inc"
.globl tc_batch_x86_expected_end
.hidden tc_batch_x86_expected_end
tc_batch_x86_expected_end:
.section .note.GNU-stack,"",@progbits
