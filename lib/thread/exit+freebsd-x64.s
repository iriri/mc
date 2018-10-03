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

	movslq	%fs:0x8,%r10	/* stksz */
	movslq	%fs:0xc,%r11	/* nslots */

	/* munmap(base, size) */
	movq	$73,%rax	/* munmap */
	movq	(%rsp),%rdi	/* base */
	subq	%r10,%rdi
	leaq	0x10(%r10, %r11, 0x8),%rsi	/* stksz + sizeof tls region */
	addq	$0xf,%rsi
	andq	$~0xf,%rsi
	syscall

	/* thr_exit(null) */
	movq	$431,%rax	/* exit */
	xorq	%rdi,%rdi	/* 0 */
	syscall
	
