#!/bin/bash

set -e

mkdir -p amalgamation
OUTPUT_PREFIX=amalgamation/miniz

cat miniz.h > $OUTPUT_PREFIX.h
cat miniz.c > $OUTPUT_PREFIX.c
cat miniz_common.h >> $OUTPUT_PREFIX.h
cat miniz_tdef.c >> $OUTPUT_PREFIX.c
cat miniz_tdef.h >> $OUTPUT_PREFIX.h
cat miniz_tinfl.c >> $OUTPUT_PREFIX.c
cat miniz_tinfl.h >> $OUTPUT_PREFIX.h
cat miniz_zip.c >> $OUTPUT_PREFIX.c
cat miniz_zip.h >> $OUTPUT_PREFIX.h


sed -i '0,/#include "miniz.h"/{s/#include "miniz.h"/#include  "miniz.h"/}' $OUTPUT_PREFIX.c
for i in miniz miniz_common miniz_tdef miniz_tinfl miniz_zip
do
	sed -i "s/#include \"$i.h\"//g" $OUTPUT_PREFIX.h
	sed -i "s/#include \"$i.h\"//g" $OUTPUT_PREFIX.c
done

echo "int main() { return 0; }" > main.c
echo "Test compile with GCC..."
gcc -pedantic -Wall main.c $OUTPUT_PREFIX.c -o test.out
echo "Test compile with GCC ANSI..."
gcc -ansi -pedantic -Wall main.c $OUTPUT_PREFIX.c -o test.out
if command -v clang
then
		echo "Test compile with clang..."
        clang -Wall -Wpedantic -fsanitize=unsigned-integer-overflow main.c $OUTPUT_PREFIX.c -o test.out
fi
for def in MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_APIS MINIZ_NO_ARCHIVE_WRITING_APIS MINIZ_NO_ZLIB_APIS MINIZ_NO_ZLIB_COMPATIBLE_NAMES MINIZ_NO_MALLOC
do
	echo "Test compile with GCC and define $def..."
	gcc -ansi -pedantic -Wall main.c $OUTPUT_PREFIX.c -o test.out -D${def}
done
rm test.out
rm main.c

cp ChangeLog.md amalgamation/
cp LICENSE amalgamation/
cp readme.md amalgamation/
mkdir -p amalgamation/examples
cp examples/* amalgamation/examples/

cd amalgamation
! test -e miniz.zip || rm miniz.zip
cat << EOF | zip -@ miniz
miniz.c
miniz.h
ChangeLog.md
LICENSE
readme.md
examples/example1.c
examples/example2.c
examples/example3.c
examples/example4.c
examples/example5.c
examples/example6.c
EOF
cd ..

echo "Amalgamation created."


