.globl thread$tid
.globl _thread$tid
thread$tid:
_thread$tid:
	movl	%fs:0x0, %eax
	ret

.globl thread$_tlsset
.globl _thread$_tlsset
thread$_tlsset:
_thread$_tlsset:
	movslq	%edi, %rdi
	movq	$16, %r10
	movq	%rsi, %fs:(%r10, %rdi, 8)
	ret

.globl thread$_tlsget
.globl _thread$_tlsget
thread$_tlsget:
_thread$_tlsget:
	movslq	%edi, %rdi
	movq	$16, %r10
	movq	%fs:(%r10, %rdi, 8), %rax
	ret

.globl thread$wrfsbase
.globl _thread$wrfsbase
thread$wrfsbase:
_thread$wrfsbase:
	wrfsbaseq	%rdi
	ret
