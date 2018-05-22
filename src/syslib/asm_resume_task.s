	.text
	.globl	asm_resume_task
	.type	asm_resume_task, @function
asm_resume_task:
.LFB0:
	.cfi_startproc
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	movq	%rdi, %rbx

	movq	48(%rdi), %rcx
	andq	$4, %rcx
	testq	%rcx, %rcx
	jne		.ASM_RESUME_TASK_OVER

	addq	$72, %rdi

	movq	(%rdi), %rax
	movq	24(%rdi), %rdx
	movq	48(%rdi), %rdx		# temp save %rsp in rdx
	movq	56(%rdi), %rbp

	movq	32(%rbx), %r9

	orq		$2, 48(%rbx)
	movq	48(%rbx), %rcx

	popq	%rbx
	popq	%rbp

	movq	56(%rdi), %rbp
	movq	%rdx, %rsp
	jmp		*%r9		# jump to asm_yield_task: nop

.ASM_RESUME_TASK_OVER:
	popq	%rbx
	popq	%rbp

	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE0:
	.size	asm_resume_task, .-asm_resume_task
	.ident	"GCC: (Ubuntu 5.4.0-6ubuntu1~16.04.4) 5.4.0 20160609"
	.section	.note.GNU-stack,"",@progbits
