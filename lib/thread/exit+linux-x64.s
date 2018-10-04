/*
const thread.exit : (-> void)
*/
.globl thread$exit
thread$exit:
	subq	$0x10,%rsp

	/* arch_prctl(op, addr) */
	movq	$158,%rax	/* arch_prctl */
	movq	$0x1003,%rdi	/* Archgetfs */
	movq	%rsp,%rsi	/* addr */
	syscall

	/* munmap(base, size) */
	movq	$11,%rax	/* munmap */
	movslq	%fs:0x8,%r10	/* stksz */
	movq	(%rsp),%rdi	/* fs */
	subq	%r10,%rdi	/* base = fs - stksz */
	movslq	%fs:0xc,%rsi	/* tlssz */
	addq	%r10,%rsi	/* size = stksz + tlssz */
	syscall

	/* thread_exit(0) */
	movq	$60,%rax	/* exit */
	xorq	%rdi,%rdi	/* 0 */
	syscall
	
