	.globl	quick_mmcpy_a	
	.type	quick_mmcpy_a, @function
quick_mmcpy_a:
	.cfi_startproc
	mov		%rsi, %rax
	orq		%rdi, %rax
	test	$0xf, %al
	jne		.Q_MMCPYA_FAIL
	test	$0xf, %dl
	jne		.Q_MMCPYA_FAIL
.Q_MMCPYA_START:
	testq	%rdx, %rdx
	jle		.Q_MMCPYA_FINISH
	movdqa	(%rsi), %xmm0
	movdqa	%xmm0, (%rdi)
	addq	$0x10, %rsi
	addq	$0x10, %rdi
	subq	$0x10, %rdx
	jmp		.Q_MMCPYA_START
.Q_MMCPYA_FAIL:
	movq	$-1, %rax
	ret
.Q_MMCPYA_FINISH:
	xorq	%rax, %rax
	ret
	.cfi_endproc


	.globl	quick_mmcpy_u	
	.type	quick_mmcpy_u, @function
quick_mmcpy_u:
	.cfi_startproc
	test	$0xf, %dl
	jne		.Q_MMCPYU_FAIL
.Q_MMCPYU_START:
	testq	%rdx, %rdx
	jle		.Q_MMCPYU_FINISH
	movdqu	(%rsi), %xmm0
	movdqu	%xmm0, (%rdi)
	addq	$0x10, %rsi
	addq	$0x10, %rdi
	subq	$0x10, %rdx
	jmp		.Q_MMCPYU_START
.Q_MMCPYU_FAIL:
	movq	$-1, %rax
	ret
.Q_MMCPYU_FINISH:
	xorq	%rax, %rax
	ret
	.cfi_endproc



