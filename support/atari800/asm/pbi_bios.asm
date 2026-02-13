; Links:
; http://atariki.krap.pl/index.php/ROM_PBI

#define VER_MAJOR 0
#define VER_MINOR 8

warmst	= $008
pdvmsk	= $247
pdvrs	= $248
ddevic	= $300
dunit	= $301
dstats	= $303
dtimlo	= $306
cdtma1	= $226
setvbv	= $e45c

; PBI BIOS RAM
pbi_magic = $d100 ; word
pbi_splash_flag = $d102
pbi_req_init_flag = $d103
pbi_req_proc_res = $d104
pbi_req_proc_flag = $d105
pbi_stack_save = $d106
pbi_drive_boot_act = $d10a
pbi_drive_boot = $d10b
pbi_drive_conf = $d10c
; HSIO RAM variables sit upwards of $d1f0 incl.

	* = $D800
bios1_start
	.byte 'M', 'S', 'T'
	; Magic 1
	.byte $80
	.byte $31	; ddevic we are servicing
pdior_vec
	jmp pdior
pdint_vec
	rts : nop : nop
	; Magic 2
	.byte $91
	.byte $00	; no CIO 
	.word pdint_vec
	.word pdint_vec
	.word pdint_vec
	.word pdint_vec
	.word pdint_vec
	.word pdint_vec

pdinit
	lda pdvmsk : ora pdvrs : sta pdvmsk
	lda #0 : ldx #$fe : sta $d100-1,x : dex : bne *-4
	; Marker for the core firmware
	lda #$a5 : sta pbi_magic : sta pbi_magic+1
	; ask for init
	inc pbi_req_init_flag : lda pbi_req_init_flag : bne *-3
	lda warmst : bmi pdinit_1
	lda pbi_drive_boot_act : beq pdinit_1
	sta dunit
pdinit_1
	; Do we want the splash?
	lda pbi_splash_flag : beq pdinit_ret
	jsr boot_screen_init
	lda #$0c : sta $2c5 ; color 1
	lda #$e0 : sta $d409 ; chbase
	lda #<display_list : sta $d402 : lda #>display_list : sta $d403 ; display list
	lda $14 : cmp $14 : beq *-2 : ldy #$22 : sty $d400 ; dmactl
	clc : adc #100 : cmp $14 : bne *-2
	lda #0 : sta $d400
pdinit_ret
	rts

display_list
	.byte $70, $70, $70
	.byte $42 : .word display_text1
	.byte $10
	.byte $42 : .word display_text2
	.byte $70
	.byte $42 : .word display_text3
	.byte $30
	.byte $02
	.byte $41 : .word display_list

display_text1
	.byte 0,0
	.byte 'A'-$20,'tari','8'-$20,'0'-$20,'0'-$20,0,'M'-$20,'i','S'-$20,'T'-$20,'er',0,'core',0 
	.byte 'P'-$20,'B'-$20,'I'-$20,0,'B'-$20,'I'-$20,'O'-$20,'S'-$20,0
	.byte 'v','0'-$20,'.'-$20,'8'-$20
display_text1_len = *-display_text1
	.dsb 40-display_text1_len,0
display_text2
	.byte 0,0
	.byte '('-$20,'C'-$20,')'-$20,0,'2'-$20,'0'-$20,'2'-$20,'5'-$20,0,'woj','@'-$20,'A'-$20,'tari','A'-$20,'ge'
display_text2_len = *-display_text2
	.dsb 40-display_text2_len,0
display_text3 = $d110 

drive_labels_1	.byte 'O'-$20, 'P'-$20, 'H'-$20
drive_labels_2	.byte 'f',     'B'-$20, 'S'-$20
drive_labels_3	.byte 'f',     'I'-$20, 'I'-$20
drive_labels_4	.byte 0,       0,       'O'-$20

boot_label_1	.byte 'B'-$20,'o','o','t',0,'D'-$20,'r','i','v','e',':'-$20
boot_label_1_len = *-boot_label_1
boot_label_2	.byte 'D'-$20,'e','f','a','u','l','t'
boot_label_2_len = *-boot_label_2
boot_label_3	.byte 'A'-$20,'P'-$20,'T'-$20
boot_label_3_len = *-boot_label_3

boot_screen_init
	ldy #1
boot_screen_init_loop
	lda #'D'-$20 : sta display_text3+2,x : inx
	tya : pha : ora #$10 : sta display_text3+2,x : inx
	lda #':'-$20 : sta display_text3+2,x : inx : inx
	lda pbi_drive_conf-1,y : tay
	lda drive_labels_1,y : sta display_text3+2,x : inx
	lda drive_labels_2,y : sta display_text3+2,x : inx
	lda drive_labels_3,y : sta display_text3+2,x : inx
	lda drive_labels_4,y : sta display_text3+2,x : inx : inx
	pla : tay : iny : cpy #5 : bne boot_screen_init_loop
	ldy #boot_label_1_len
boot_screen_init_loop_2
	lda boot_label_1-1,y : sta display_text3+41,y
	dey : bne boot_screen_init_loop_2
	ldx pbi_drive_boot : dex 
	bmi boot_drv_def
	beq boot_drv_apt
	lda #'D'-$20 : sta display_text3+42+boot_label_1_len+1
	txa : ora #$10 : sta display_text3+42+boot_label_1_len+2
	lda #':'-$20 : sta display_text3+42+boot_label_1_len+3
	bne boot_screen_init_ret
boot_drv_def
	ldy #boot_label_2_len
boot_drv_def_loop
	lda boot_label_2-1,y : sta display_text3+42+boot_label_1_len,y
	dey : bne boot_drv_def_loop
	beq boot_screen_init_ret
boot_drv_apt
	ldy #boot_label_3_len
boot_drv_apt_loop
	lda boot_label_3-1,y : sta display_text3+42+boot_label_1_len,y
	dey : bne boot_drv_apt_loop
boot_screen_init_ret
	rts

; The main block I/O routine
pdior
	lda ddevic : and #$7F : cmp #$31 : beq pdior_2
	cmp #$20 : bne pdior_bail
pdior_2
	lda dunit : beq pdior_bail
	cmp #$10 : bcs pdior_bail
	tsx : stx pbi_stack_save
	lda dtimlo : ror : ror : tay : and #$3f : tax : tya : ror : and #$c0 : tay : lda #1
	jsr setvbv
	lda #<pbi_time_out : sta cdtma1 : lda #>pbi_time_out : sta cdtma1+1
	inc pbi_req_proc_flag : lda pbi_req_proc_flag : bne *-3
	ldx #0 : ldy #0 : lda #1 : jsr setvbv 
	lda pbi_req_proc_res : bmi pdior_bail ; the FW says either no PBI service or ATX (plain SIO)
	beq pdior_pbi_ok ; the drive was in PBI mode and got serviced
	; otherwise call HSIO
	jsr $dc00 : sec : rts
pdior_pbi_ok
	ldy dstats : sec : rts
pdior_bail
	; We are not servicing this block I/O request
	clc : rts
pbi_time_out
	lda #0 : sta pbi_req_proc_flag : ldx pbi_stack_save : txs
	lda #$8a : sta dstats : bne pdior_pbi_ok
bios1_end

.dsb ($400-bios1_end+bios1_start),$ff

hsio_start	; This should be $dc00
.bin 6,0,"hsio_pbi.xex"
hsio_end

.dsb ($3AD-hsio_end+hsio_start),$ff

.byte VER_MAJOR*16+VER_MINOR ; Version byte

.byte (device_name_end-device_name-1)
device_name
.byte "SDHC MiSTer SDEMU v.",VER_MAJOR+$30,'.',VER_MINOR+$30,$9B
device_name_end

.dsb  (40-device_name_end+device_name),$ff

; .dsb ($3D7-hsio_end+hsio_start),$ff

.byte (bios_name_end-bios_name-1)
bios_name
.byte "MiSTer core PBI BIOS v.",VER_MAJOR+$30,'.',VER_MINOR+$30,$9B
bios_name_end

.dsb  (40-bios_name_end+bios_name),$ff

	* = $D800
.dsb $800,$FF

	* = $D800
.dsb $800,$FF

	* = $D800
.dsb $800,$FF