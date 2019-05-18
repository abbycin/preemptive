.text
.globl init_stack
.type init_stack,@function
.align 16
init_stack:
    movq %rdi, %rax
    leaq -0x40(%rax), %rax
    movq %rsi, 0x30(%rax)
    leaq quit(%rip), %rcx
    movq %rcx, 0x38(%rax) // return
    ret
quit:
    xorq %rdi, %rdi
    movq $60, %rax
    syscall

.size init_stack,.-init_stack
.section .note.GNU-stack,"",%progbits

.text
.globl switch_stack
.type switch_stack,@function
.align 16
switch_stack:
    pushq  %rbp
    pushq  %rbx
    pushq  %r15
    pushq  %r14
    pushq  %r13
    pushq  %r12

    movq  %rsp, (%rdi)
    movq  %rsi, %rsp

    popq  %r12
    popq  %r13
    popq  %r14
    popq  %r15
    popq  %rbx
    popq  %rbp

    popq  %r8 // return

    /* Context pointer of ctx_function first argument */
    movq  %rdx, %rdi

    jmp  *%r8
.size switch_stack,.-switch_stack
.section .note.GNU-stack,"",%progbits
