/*
const thread.exit	: (stacksz : std.size -> void)
*/
.globl thread$exit
thread$exit:
	subq	$0x10,%rsp

	/* sysarch(op, parms) */
	movq	$165,%rax	/* sysarch */
	movq	$128,%rdi	/* Archamd64getfs */
	movq	%rsp,%rsi	/* parms */
	syscall

	/* munmap(base, size) */
	movq	$73,%rax	/* munmap */
	movslq	%fs:0x8,%r10	/* stksz */
	movq	(%rsp),%rdi	/* fs */
	subq	%r10,%rdi	/* base = fs - stksz */
	movslq	%fs:0xc,%rsi	/* tlssz */
	addq	%r10,%rsi	/* size = stksz + tlssz */
	syscall

	/* thr_exit(null) */
	movq	$431,%rax	/* exit */
	xorq	%rdi,%rdi	/* 0 */
	syscall
	
