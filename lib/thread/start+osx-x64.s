# The entry point for thread start, registered with bsdthread_register
#      %rdi: pthread (0, for us)
#      %rsi: mach thread port (ignored)
#      %rdx: func
#      %rcx: env
#      %r8: stack
#      %r9: flags (= 0)
#      %rsp: stack - C_64_REDZONE_LEN (= stack - 128)
.globl _thread$start
_thread$start:
	/* call the function */
	movq	%r8, %rsp	/* set up stack */
	movq	%rcx,%rdi
        callq    *%rdx		/* call function */
	
/*
const thread.exit	: (stacksz : std.size -> void)
*/
.globl _thread$exit
_thread$exit:
	/* munmap(base, size) */
	movq	$0x2000049,%rax	/* munmap */
	movq	%rbp,%rdi
	andq	$~0xffffff,%rdi	/* base*/
	movq	$0x800000,%rsi	/* size */
	syscall

	/* exit the thread */
	movq	$0x2000169, %rax	/* Sysbsdthread_terminate */
	movq	$0, %rdi	/* stack */
	movq	$0, %rsi	/* len */
	movq	$0, %rdx	/* sem */
	syscall
	

