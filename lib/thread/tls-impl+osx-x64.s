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
	cmpq	%fs:0x8, %rdi
	jnb	oob

	movq	$0x28, %r10
	movq	%rsi, %fs:(%r10, %rdi, 0x8)
	ret

.globl thread$_tlsget
.globl _thread$_tlsget
thread$_tlsget:
_thread$_tlsget:
	cmpq	%fs:0x8, %rdi
	jnb	oob

	movq	$0x28, %r10
	movq	%fs:(%r10, %rdi, 0x8), %rax
	ret

oob:
	call	thread$tlsoob

.globl thread$tlslen
.globl _thread$tlslen
thread$tlslen:
_thread$tlslen:
	movq	%fs:0x8, %rax
	ret

/* undocumented syscall that sets %gs to %rdi */
.globl thread$setgsbase
.globl _thread$setgsbase
thread$setgsbase:
_thread$setgsbase:
	movq	$0x3000003, %rax
	syscall
	negq	%rax
	ret

.globl thread$getgsbase
.globl _thread$getgsbase
thread$getgsbase:
_thread$getgsbase:
	movq	%fs:0x20, %rax
	ret
