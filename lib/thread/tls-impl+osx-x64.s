.globl thread$tid
.globl _thread$tid
thread$tid:
_thread$tid:
	movq	%gs:0x0, %rax
	ret

.globl thread$_tlsset
.globl _thread$_tlsset
thread$_tlsset:
_thread$_tlsset:
	cmpq	%gs:0x8, %rdi
	jnb	oob

	movq	$0x28, %r10
	movq	%rsi, %gs:(%r10, %rdi, 0x8)
	ret

.globl thread$_tlsget
.globl _thread$_tlsget
thread$_tlsget:
_thread$_tlsget:
	cmpq	%gs:0x8, %rdi
	jnb	oob

	movq	$0x28, %r10
	movq	%gs:(%r10, %rdi, 0x8), %rax
	ret

oob:
	call	_thread$tlsoob

.globl thread$tlslen
.globl _thread$tlslen
thread$tlslen:
_thread$tlslen:
	movq	%gs:0x8, %rax
	ret

/* undocumented syscall that sets %gs to %rdi */
.globl thread$_setgsbase
.globl _thread$_setgsbase
thread$_setgsbase:
_thread$_setgsbase:
	movq	$0x3000003, %rax
	syscall
	ret

.globl thread$getgsbase
.globl _thread$getgsbase
thread$getgsbase:
_thread$getgsbase:
	movq	%gs:0x20, %rax
	ret
