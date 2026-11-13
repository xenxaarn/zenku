; ---------------------------------------------------------------------------
; Ring-3 entry/exit and scheduler context switches.
;
; user_enter(entry, stack) is the Phase 4 single-process entry. It is kept
; for reference; the scheduler uses sched_run_first().
;
; sched_run_first(frame, kstack_top, cr3) saves the kernel callee-saved
; context, installs a process address space, and iretq's into a user frame
; stored in the process struct.
;
; sched_return_to_kernel() is called when the last process exits. It restores
; the kernel context saved by sched_run_first and returns to its caller.
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

    mov ax, 0x1B                ; GDT_USER_DATA | RPL 3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push qword 0x1B             ; ss: GDT_USER_DATA | RPL 3
    push rsi                    ; rsp
    push qword 0x202            ; rflags: IF=1, reserved bit 1
    push qword 0x23             ; cs: GDT_USER_CODE | RPL 3
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

global sched_run_first
sched_run_first:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    pushfq
    mov [rel kernel_rsp], rsp

    mov cr3, rdx                ; switch to the process address space

    mov rsp, rsi                ; build the interrupt frame on its kstack
    sub rsp, 22 * 8
    mov rcx, 22
    mov r8, rdi
    mov r9, rsp
.copy_frame:
    mov rax, [r8]
    mov [r9], rax
    add r8, 8
    add r9, 8
    loop .copy_frame

    mov ax, 0x1B                ; user data segments
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Fall through to the common frame restore.

global sched_restore_frame
sched_restore_frame:
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16                 ; drop vector and error code
    iretq

global sched_return_to_kernel
sched_return_to_kernel:
    cli
    mov ax, 0x10
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
