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
	movq	$16, %r10
	movq	%rsi, %fs:(%r10, %rdi, 8)
	ret

.globl thread$tlsget
.globl _thread$tlsget
thread$tlsget:
_thread$tlsget:
	movslq	%edi, %rdi
	movq	$16, %r10
	movq	%fs:(%r10, %rdi, 8), %rax
	ret
