.globl thread$tid
.globl _thread$tid
thread$tid:
_thread$tid:
	movq	%fs:0x00, %rax
	retq

.globl thread$settls
.globl _thread$settls
thread$settls:
_thread$settls:
	movq	%rdi, %fs:0x08
	retq

.globl thread$tls
.globl _thread$tls
thread$tls:
_thread$tls:
	movq	%fs:0x08, %rax
	retq
