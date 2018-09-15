.globl thread$tid
.globl _thread$tid
thread$tid:
_thread$tid:
	movl	%fs:0x0, %eax
	ret

.globl thread$tlsset
.globl _thread$tlsset
thread$tlsset:
_thread$tlsset:
	movslq	%edi, %rdi
	movq	%rsi, 16(%fs, %rdi, 8)
	retq

.globl thread$tlsget
.globl _thread$tlsget
thread$tlsget:
_thread$tlsget:
	movslq	%edi, %rdi
	movq	16(%fs, %rdi, 8), %rax
	retq
