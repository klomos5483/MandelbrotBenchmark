; Mandelbrot x64: jedna skalarna iteracja dla jednego piksela.
; RCX: MBParams*, RDX: bufor uint32_t*, R8: pojemność, R9: flaga anulowania lub NULL.
; Bufor należy do wywołującego. Kod nie alokuje pamięci i nie wywołuje C++.
OPTION CASEMAP:NONE

.const
four REAL8 4.0
one REAL8 1.0
coordinateLimit REAL8 1000000.0
negativeLimit REAL8 -1000000.0

.code
PUBLIC MandelbrotAsm
MandelbrotAsm PROC FRAME
    push rbx
    .pushreg rbx
    push rbp
    .pushreg rbp
    push rsi
    .pushreg rsi
    push rdi
    .pushreg rdi
    push r12
    .pushreg r12
    push r13
    .pushreg r13
    push r14
    .pushreg r14
    push r15
    .pushreg r15
    sub rsp, 40
    .allocstack 40
    .endprolog

    ; Lokalne double: [rsp] c_re, [rsp+8] c_im, [rsp+16] krok X, [rsp+24] krok Y.
    ; Używamy tylko ulotnych XMM0-XMM5. XMM6-XMM15 pozostają nietknięte.
    test rcx, rcx
    jz invalid_argument
    test rdx, rdx
    jz invalid_argument
    mov rsi, rcx
    mov rdi, rdx
    mov rbp, r9
    mov r12d, DWORD PTR [rsi]
    mov r13d, DWORD PTR [rsi+4]
    mov r14d, DWORD PTR [rsi+8]
    cmp r12d, 1
    jb invalid_argument
    cmp r12d, 8192
    ja invalid_argument
    cmp r13d, 1
    jb invalid_argument
    cmp r13d, 8192
    ja invalid_argument
    cmp r14d, 1
    jb invalid_argument
    cmp r14d, 100000
    ja invalid_argument
    cmp DWORD PTR [rsi+12], 0
    jne invalid_argument

    ; Każda współrzędna musi być skończona i należeć do bezpiecznego zakresu.
    mov ecx, 16
validate_coordinate:
    movsd xmm0, QWORD PTR [rsi+rcx]
    ucomisd xmm0, QWORD PTR [coordinateLimit]
    jp invalid_argument
    ja invalid_argument
    ucomisd xmm0, QWORD PTR [negativeLimit]
    jb invalid_argument
    add ecx, 8
    cmp ecx, 48
    jb validate_coordinate
    movsd xmm0, QWORD PTR [rsi+16]
    ucomisd xmm0, QWORD PTR [rsi+24]
    jae invalid_argument
    movsd xmm0, QWORD PTR [rsi+32]
    ucomisd xmm0, QWORD PTR [rsi+40]
    jae invalid_argument

    mov eax, r12d
    imul rax, r13
    cmp rax, 16777216
    ja invalid_argument
    cmp r8, rax
    jb buffer_too_small

    ; Dla wymiaru 1 dzielnikiem jest 1; jedyny piksel otrzymuje minimum osi.
    mov eax, r12d
    dec eax
    jnz x_denominator
    mov eax, 1
x_denominator:
    cvtsi2sd xmm1, eax
    movsd xmm0, QWORD PTR [rsi+24]
    subsd xmm0, QWORD PTR [rsi+16]
    divsd xmm0, xmm1
    movsd QWORD PTR [rsp+16], xmm0
    mov eax, r13d
    dec eax
    jnz y_denominator
    mov eax, 1
y_denominator:
    cvtsi2sd xmm1, eax
    movsd xmm0, QWORD PTR [rsi+40]
    subsd xmm0, QWORD PTR [rsi+32]
    divsd xmm0, xmm1
    movsd QWORD PTR [rsp+24], xmm0

    xor r15d, r15d
row_loop:
    cvtsi2sd xmm0, r15d
    mulsd xmm0, QWORD PTR [rsp+24]
    addsd xmm0, QWORD PTR [rsi+32]
    movsd QWORD PTR [rsp+8], xmm0
    xor ebx, ebx
pixel_loop:
    ; Flaga Windows LONG jest zapisywana przez GUI operacją InterlockedExchange.
    test rbp, rbp
    jz calculate_pixel
    cmp DWORD PTR [rbp], 0
    jne cancelled
calculate_pixel:
    cvtsi2sd xmm0, ebx
    mulsd xmm0, QWORD PTR [rsp+16]
    addsd xmm0, QWORD PTR [rsi+16]
    movsd QWORD PTR [rsp], xmm0
    xorpd xmm0, xmm0
    xorpd xmm1, xmm1
    xor eax, eax
iteration_loop:
    movapd xmm2, xmm0
    mulsd xmm2, xmm0
    movapd xmm3, xmm1
    mulsd xmm3, xmm1
    movapd xmm4, xmm2
    addsd xmm4, xmm3
    comisd xmm4, QWORD PTR [four]
    ja pixel_done
    cmp eax, r14d
    jae pixel_done

    ; Nowa część urojona korzysta ze STARYCH obu części liczby z.
    movapd xmm4, xmm0
    mulsd xmm4, xmm1
    addsd xmm4, xmm4
    addsd xmm4, QWORD PTR [rsp+8]
    movapd xmm0, xmm2
    subsd xmm0, xmm3
    addsd xmm0, QWORD PTR [rsp]
    movapd xmm1, xmm4
    inc eax
    jmp iteration_loop
pixel_done:
    mov DWORD PTR [rdi], eax
    add rdi, 4
    inc ebx
    cmp ebx, r12d
    jb pixel_loop
    inc r15d
    cmp r15d, r13d
    jb row_loop
    xor eax, eax
    jmp finish
invalid_argument:
    mov eax, 1
    jmp finish
buffer_too_small:
    mov eax, 2
    jmp finish
cancelled:
    mov eax, 3
finish:
    add rsp, 40
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    ret
MandelbrotAsm ENDP
END
