.globl thread$tid
.globl _thread$tid
thread$tid:
_thread$tid:
	movq	%rsp, %rax
	shrq	$24, %rax
	ret

.globl thread$_tlsset
.globl _thread$_tlsset
thread$_tlsset:
_thread$_tlsset:
	movslq	%edi, %rdi
	movq	$0x10000000000000, %r10
	cmpq	%r10, %rsp
	jb	setnotmain

	movq	_thread$tlsmain(%rip), %r11
	movq	%rsi, (%r11, %rdi, 8)
	ret

setnotmain:
	movq	%rsp, %r11
	andq	$~0xffffff, %r11
	addq	$0x800000, %r11
	subq	_thread$tlscap(%rip), %r11
	movq	%rsi, (%r11, %rdi, 8)
	ret

.globl thread$_tlsget
.globl _thread$_tlsget
thread$_tlsget:
_thread$_tlsget:
	movslq	%edi, %rdi
	movq	$0x10000000000000, %r10
	cmpq	%r10, %rsp
	jb	getnotmain

	movq	_thread$tlsmain(%rip), %r11
	movq	(%r11, %rdi, 8), %rax
	ret

getnotmain:
	movq	%rsp, %r11
	andq	$~0xffffff, %r11
	addq	$0x800000, %r11
	subq	_thread$tlscap(%rip), %r11
	movq	(%r11, %rdi, 8), %rax
	ret
