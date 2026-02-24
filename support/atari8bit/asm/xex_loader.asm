	* = $D100
magic
	.byte $61
init1
	ldx #0 : stx $09 : dex : txs
	dec magic
load_next_block
	dec read_status
	lda #1 : read_status = *-1 : beq *-2
	bmi init_go
	lda #>(load_next_block-1) : pha
	lda #<(load_next_block-1) : pha
	jmp ($2e2)
init_go
	dec magic
	jmp ($2e0)
