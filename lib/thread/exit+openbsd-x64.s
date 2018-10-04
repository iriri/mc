/*
const thread.exit	: (stacksz : std.size -> void)
*/
.globl thread$exit
thread$exit:
	/* 
	  Because OpenBSD wants a valid stack whenever
	  we enter the kernel, we need to toss a preallocated
	  stack pointer into %rsp.
	 */
	movq	thread$exitstk,%rsp

	/* __get_tcb() */
	movq	$330,%rax
	syscall

	/* munmap(base, size) */
	movq	$73,%rax	/* munmap */
	movslq	%fs:0x8,%r10	/* stksz */
	movq	%rax,%rdi	/* fs */
	subq	%r10,%rdi	/* base = fs - stksz */
	movslq	%fs:0xc,%rsi	/* tlssz */
	addq	%r10,%rsi	/* size = stksz + tlssz */
	syscall

	/* __threxit(0) */
	movq	$302,%rax	/* exit */
	xorq	%rdi,%rdi	/* 0 */
	syscall
	
