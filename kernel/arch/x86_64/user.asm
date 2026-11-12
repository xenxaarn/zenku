; ---------------------------------------------------------------------------
; Ring-3 entry and exit for the first user process.
;
; user_enter(entry, stack) saves the kernel callee-saved context, installs the
; user data segments, and iretq's to ring 3 with interrupts enabled.
;
; user_exit_to_kernel() is called from the int 0x80 exit handler. It switches
; back to the saved kernel stack and returns from user_enter, leaving the
; stale interrupt frame behind on the syscall stack.
; ---------------------------------------------------------------------------

BITS 64

section .text

global user_enter
user_enter:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    pushfq
    mov [rel kernel_rsp], rsp

    mov ax, 0x23                ; GDT_USER_DATA | RPL 3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push qword 0x23             ; ss: GDT_USER_DATA | RPL 3
    push rsi                    ; rsp
    push qword 0x202            ; rflags: IF=1, reserved bit 1
    push qword 0x1B             ; cs: GDT_USER_CODE | RPL 3
    push rdi                    ; rip
    iretq

global user_exit_to_kernel
user_exit_to_kernel:
    cli
    mov ax, 0x10                ; GDT_KERNEL_DATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov rsp, [rel kernel_rsp]
    popfq
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

section .data
align 8
kernel_rsp:
    dq 0
