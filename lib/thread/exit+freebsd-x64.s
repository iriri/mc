/*
const thread.exit	: (stacksz : std.size -> void)
*/
.globl thread$exit
thread$exit:
	/* munmap(base, size) */
	movq	$73,%rax	/* munmap */
	movq	%fs:0x8,%rdi	/* base */
	movq	$0x800000,%rsi	/* stksz */
	syscall

	/* thr_exit(null) */
	movq	$431,%rax	/* exit */
	xorq	%rdi,%rdi	/* 0 */
	syscall
	
