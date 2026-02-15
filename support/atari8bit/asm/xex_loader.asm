	* = $700
magic
	.byte $61
init2
	dec magic : reloc01 = *-1
load_next_block
	dec read_status : reloc02 = *-1
	lda #1 : read_status = *-1 : beq *-2
	bmi init_go
	lda #>(load_next_block-1) : reloc03 = *-1 : pha
	lda #<(load_next_block-1) : pha
	jmp ($2e2)
init_go
	dec magic : reloc04 = *-1
	jmp ($2e0)

loader_len = *-magic

init1
	ldx #0 : stx $09
	dex : txs
	lda #0 : stack_flag = *-1 : beq init2
	sta reloc01 : sta reloc02 : sta reloc03 : sta reloc04 : sta reloc05 : sta reloc06

	ldy #loader_len-1
copy_loader_loop
	lda magic,y : sta $100,y : reloc05 = *-1
	dey : bpl copy_loader_loop
	jmp $101 : reloc06 = *-1