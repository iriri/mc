/*
const thread.exit : (-> void)
*/
.globl thread$exit
thread$exit:
	/* munmap(base, size) */
	movq	$11,%rax	/* munmap */
	movq	%fs:0x8,%rdi	/* base */
	movq	$0x800000,%rsi	/* stksz */
	syscall

	/* thread_exit(0) */
	movq	$60,%rax	/* exit */
	xorq	%rdi,%rdi	/* 0 */
	syscall
	
