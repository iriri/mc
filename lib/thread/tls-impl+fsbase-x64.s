.globl thread$tid
.globl _thread$tid
thread$tid:
_thread$tid:
	movq	%fs:0x0, %rax
	ret

.globl thread$_tlsset
.globl _thread$_tlsset
thread$_tlsset:
_thread$_tlsset:
	cmpl	%edi, %fs:0xc
	jnb	oob

	movslq	%edi, %rdi
	movq	$0x10, %r10
	movq	%rsi, %fs:(%r10, %rdi, 0x8)
	ret

.globl thread$_tlsget
.globl _thread$_tlsget
thread$_tlsget:
_thread$_tlsget:
	cmpl	%edi, %fs:0xc
	jnb	oob

	movslq	%edi, %rdi
	movq	$0x10, %r10
	movq	%fs:(%r10, %rdi, 0x8), %rax
	ret

oob:
	call	thread$tlsoob

.globl thread$tlslen
.globl _thread$tlslen
thread$tlslen:
_thread$tlslen:
	movl	%fs:0xc, %eax
	ret
