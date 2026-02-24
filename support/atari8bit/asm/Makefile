# This produces the supporting binary data for the Atari800 core, 
# the internal XEX loader and the PBI rom file boot3.rom. The
# latter should be placed in games/ATARI800 folder of your MiSTer
# SD card.

all: ../xex_loader.h boot3.rom

../xex_loader.h: xex_loader.asm
	xa xex_loader.asm -o xex_loader.o65 -l xex_loader.lab
	python3 process_xex_loader.py
	rm xex_loader.o65 xex_loader.lab
	mv xex_loader.h ..

boot3.rom: pbi_bios.asm
	xa -M -o boot3.rom pbi_bios.asm