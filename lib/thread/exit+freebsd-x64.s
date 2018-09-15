/*
const thread.exit	: (stacksz : std.size -> void)
*/
.globl thread$exit
thread$exit:
	/* munmap(base, size) */
	movq	$73,%rax	/* munmap */
	movq	%rbp,%rdi
	andq	$~0xffffff,%rdi	/* base*/
	movq	$0x800000,%rsi	/* size */
	syscall

	/* thr_exit(null) */
	movq	$431,%rax	/* exit */
	xorq	%rdi,%rdi	/* 0 */
	syscall
	
