	.text
	.globl	asm_co_resume
	.type	asm_co_resume, @function
asm_co_resume:
.LFB0:
	.cfi_startproc

	pushq	%rbp
	pushq	%rbx
	movq	%rdi, %rbx
	movq	%rsp, 0x20(%rbx)

	movq    0x10(%rbx),%rax
	movb	$0x1, 0x08(%rbx)
	movb	$0x0, 0x09(%rbx)	# _co_jump_flag
	                       
	movq    (%rax),%rdx
	movq    %rdx,%rax
	movq    0x8(%rax),%rdx
	movq    %rdx,%rbx
	movq    0x10(%rax),%rdx
	movq    %rdx,%rcx
	movq    0x18(%rax),%rdx
	movq    %rdx,%rdx
	movq    0x28(%rax),%rdx
	movq    %rdx,%rsi
	movq    0x20(%rax),%rdx
	movq    %rdx,%rdi
	movq    0x38(%rax),%rdx
	movq    %rdx,%rsp
	movq    0x30(%rax),%rdx
	movq    %rdx,%rbp
	movq    0x40(%rax),%rdx
	movq    %rdx,%r8
	movq    0x48(%rax),%rdx
	movq    %rdx,%r9
	movq    0x50(%rax),%rdx
	movq    %rdx,%r10
	movq    0x58(%rax),%rdx
	movq    %rdx,%r11
	movq    0x60(%rax),%rdx
	movq    %rdx,%r12
	movq    0x68(%rax),%rdx
	movq    %rdx,%r13
	movq    0x70(%rax),%rdx
	movq    %rdx,%r14
	movq    0x78(%rax),%rax
	movq    %rax,%r15

	leaq	.co_resume_lea_pt, %rax
	movq	%rax, 0x30(%rbx)

.co_resume_lea_pt:
	movb	0x09(%rbx), %dl
	testq	%rdx, %rdx
	jne		.co_resume_return

	movq	0x28(%rbx), %rcx
	movq	%rax, 0x28(%rbx)

	movb	$0x01, 0x09(%rbx)
	jmpq	*%rcx			# rcx: .co_yield_resume_pos

.co_resume_return:

	popq	%rbx
	popq	%rbp
	retq

	.cfi_endproc
.LFE0:
	.size	asm_co_resume, .-asm_co_resume
	.section	.note.GNU-stack,"",@progbits
