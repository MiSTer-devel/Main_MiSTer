#!/usr/bin/python3

f = open("xex_loader.o65", "rb")
loader = f.read()
f.close()

s = "const static uint8_t xex_loader[] =\n{\n\t"

i = 0
for b in loader:
	s += f"0x{b:02X},"
	i += 1
	if i == len(loader):
		s = s[:-1]
	if i % 16 == 0:
		s += "\n\t"

if i % 16 != 0:
	s += "\n"
else:
	s = s[:-1]

s += "};\n\n"

f = open("xex_loader.lab", "rt")
l = f.read().split("\n")
f.close()

for ll in l:
	if ll[:11] == "read_status":
		read_status = ll[17:19]
	elif ll[:5] == "init1":
		init1 = ll[11:13]

s += f"#define XEX_READ_STATUS 0x{read_status}\n"
s += f"#define XEX_INIT1 0x{init1}\n\n"

f = open("xex_loader.h", "wt")
f.write(s)
f.close()
