%macro INTERRUPT_NAME 1
dd __interrupt%1
%endmacro

%macro INTERRUPT_ERR 1
__interrupt%1:
    push %1
    jmp __interrupt_common
%endmacro

%macro INTERRUPT_NOERR 1
__interrupt%1:
    push 0
    push %1
    jmp __interrupt_common
%endmacro

%macro INTERRUPT_SYSCALL 1
__interrupt%1:
    push 0
    push %1
    jmp __interrupt_common
%endmacro

extern interrupts_handler

__interrupt_common:
    cld

    pushad

    push ds
    push es
    push fs
    push gs

    mov ax, 0x10

    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp

    call interrupts_handler

    mov esp, eax

    pop gs
    pop fs
    pop es
    pop ds

    popad

    add esp, 8 ; pop errcode and int number

    iret

INTERRUPT_NOERR 0
INTERRUPT_NOERR 1
INTERRUPT_NOERR 2
INTERRUPT_NOERR 3
INTERRUPT_NOERR 4
INTERRUPT_NOERR 5
INTERRUPT_NOERR 6
INTERRUPT_NOERR 7
INTERRUPT_ERR   8
INTERRUPT_NOERR 9
INTERRUPT_ERR   10
INTERRUPT_ERR   11
INTERRUPT_ERR   12
INTERRUPT_ERR   13
INTERRUPT_ERR   14
INTERRUPT_NOERR 15
INTERRUPT_NOERR 16
INTERRUPT_ERR 17
INTERRUPT_NOERR 18
INTERRUPT_NOERR 19
INTERRUPT_NOERR 20
INTERRUPT_NOERR 21
INTERRUPT_NOERR 22
INTERRUPT_NOERR 23
INTERRUPT_NOERR 24
INTERRUPT_NOERR 25
INTERRUPT_NOERR 26
INTERRUPT_NOERR 27
INTERRUPT_NOERR 28
INTERRUPT_NOERR 29
INTERRUPT_ERR   30
INTERRUPT_NOERR 31

INTERRUPT_NOERR 32
INTERRUPT_NOERR 33
INTERRUPT_NOERR 34
INTERRUPT_NOERR 35
INTERRUPT_NOERR 36
INTERRUPT_NOERR 37
INTERRUPT_NOERR 38
INTERRUPT_NOERR 39
INTERRUPT_NOERR 40
INTERRUPT_NOERR 41
INTERRUPT_NOERR 42
INTERRUPT_NOERR 43
INTERRUPT_NOERR 44
INTERRUPT_NOERR 45
INTERRUPT_NOERR 46
INTERRUPT_NOERR 47

INTERRUPT_NOERR 127
INTERRUPT_SYSCALL 128

global __interrupt_vector

__interrupt_vector:
    INTERRUPT_NAME 0
    INTERRUPT_NAME 1
    INTERRUPT_NAME 2
    INTERRUPT_NAME 3
    INTERRUPT_NAME 4
    INTERRUPT_NAME 5
    INTERRUPT_NAME 6
    INTERRUPT_NAME 7
    INTERRUPT_NAME 8
    INTERRUPT_NAME 9
    INTERRUPT_NAME 10
    INTERRUPT_NAME 11
    INTERRUPT_NAME 12
    INTERRUPT_NAME 13
    INTERRUPT_NAME 14
    INTERRUPT_NAME 15
    INTERRUPT_NAME 16
    INTERRUPT_NAME 17
    INTERRUPT_NAME 18
    INTERRUPT_NAME 19
    INTERRUPT_NAME 20
    INTERRUPT_NAME 21
    INTERRUPT_NAME 22
    INTERRUPT_NAME 23
    INTERRUPT_NAME 24
    INTERRUPT_NAME 25
    INTERRUPT_NAME 26
    INTERRUPT_NAME 27
    INTERRUPT_NAME 28
    INTERRUPT_NAME 29
    INTERRUPT_NAME 30
    INTERRUPT_NAME 31

    INTERRUPT_NAME 32
    INTERRUPT_NAME 33
    INTERRUPT_NAME 34
    INTERRUPT_NAME 35
    INTERRUPT_NAME 36
    INTERRUPT_NAME 37
    INTERRUPT_NAME 38
    INTERRUPT_NAME 39
    INTERRUPT_NAME 40
    INTERRUPT_NAME 41
    INTERRUPT_NAME 42
    INTERRUPT_NAME 43
    INTERRUPT_NAME 44
    INTERRUPT_NAME 45
    INTERRUPT_NAME 46
    INTERRUPT_NAME 47

    INTERRUPT_NAME 127
    INTERRUPT_NAME 128