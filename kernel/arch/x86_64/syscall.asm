; ---------------------------------------------------------------------------
; SYSCALL/SYSRET fast path (x86_64)
;
; User registers follow the Linux x86_64 convention:
;   rax = number, rdi/rsi/rdx/r10/r8/r9 = arguments, return in rax.
;
; The entry builds the same struct regs frame used by isr.asm, so scheduling,
; blocking, and process exit work identically on both gates. The return path
; uses sysretq because every runnable frame is a ring-3 frame.
; ---------------------------------------------------------------------------

BITS 64

section .text

extern syscall_dispatch

global syscall_entry
syscall_entry:
    swapgs
    mov [gs:8], rsp             ; save user stack pointer
    mov rsp, [gs:0]             ; load the current kernel stack top

    ; struct regs, from the highest address down.
    push qword 0x1B             ; ss: user data selector
    push qword [gs:8]           ; rsp
    push r11                    ; rflags
    push qword 0x23             ; cs: user code selector
    push rcx                    ; rip (SYSCALL saves it in rcx)
    push qword 0                ; error
    push qword 0                ; vector

    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    call syscall_dispatch

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

    ; Frame tail: rip, cs, rflags, rsp, ss.
    mov rcx, [rsp]              ; return rip
    mov r11, [rsp + 16]         ; return rflags
    mov rsp, [rsp + 24]         ; user stack pointer

    swapgs
    o64 sysret
