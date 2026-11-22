/* Private SysV x86-64 helpers. The caller lends count live size_t words.
 * advance validates every digit before writing; allowed only reads them.
 * Return 0/1 for end/next or denied/allowed, and 2 for invalid dimensions.
 * Relative local branches make the read-only registration copies identical.
 */
.macro nm_advance_body
    endbr64
    test %rsi,%rsi
    jz 4f
    test %rdx,%rdx
    jz 5f
    xor %r8d,%r8d
1:
    cmp %rdx,(%rdi,%r8,8)
    jae 5f
    inc %r8
    cmp %rsi,%r8
    jb 1b
    lea -1(%rdx),%r9
2:
    dec %r8
    mov (%rdi,%r8,8),%rax
    cmp %r9,%rax
    jb 3f
    movq $0,(%rdi,%r8,8)
    test %r8,%r8
    jnz 2b
4:
    xor %eax,%eax
    ret
3:
    inc %rax
    mov %rax,(%rdi,%r8,8)
    mov $1,%eax
    ret
5:
    mov $2,%eax
    ret
.endm
.macro nm_allowed_body
    endbr64
    cmp %rsi,%rdx
    ja 5f
    xor %r8d,%r8d
1:
    cmp %rsi,%r8
    jae 4f
    mov (%rdi,%r8,8),%r10
    xor %r9d,%r9d
2:
    cmp %r8,%r9
    jae 3f
    cmp %rdx,%r9
    jae 3f
    cmp (%rdi,%r9,8),%r10
    je 6f
    inc %r9
    jmp 2b
3:
    inc %r8
    jmp 1b
4:
    mov $1,%eax
    ret
5:
    mov $2,%eax
    ret
6:
    xor %eax,%eax
    ret
.endm

.text
.p2align 4
.globl nm_selection_advance
.hidden nm_selection_advance
.type nm_selection_advance,@function
nm_selection_advance:
    nm_advance_body
.globl nm_selection_advance_end
.hidden nm_selection_advance_end
nm_selection_advance_end:
.size nm_selection_advance,.-nm_selection_advance
.p2align 4
.globl nm_selection_allowed
.hidden nm_selection_allowed
.type nm_selection_allowed,@function
nm_selection_allowed:
    nm_allowed_body
.globl nm_selection_allowed_end
.hidden nm_selection_allowed_end
nm_selection_allowed_end:
.size nm_selection_allowed,.-nm_selection_allowed

.section .rodata
.p2align 3
.globl nm_selection_advance_expected
.hidden nm_selection_advance_expected
nm_selection_advance_expected:
    nm_advance_body
.globl nm_selection_advance_expected_end
.hidden nm_selection_advance_expected_end
nm_selection_advance_expected_end:
.p2align 3
.globl nm_selection_allowed_expected
.hidden nm_selection_allowed_expected
nm_selection_allowed_expected:
    nm_allowed_body
.globl nm_selection_allowed_expected_end
.hidden nm_selection_allowed_expected_end
nm_selection_allowed_expected_end:
.section .note.GNU-stack,"",@progbits
