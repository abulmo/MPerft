del *.profraw
icx-cc -std=c23 -pedantic -W -Wall -O3 -DNDEBUG -D_CRT_SECURE_NO_DEPRECATE -march=%1 -fprofile-instr-generate  -mllvm -vp-counters-per-site=32 -fcoverage-mapping mperft.c -o .\mperft.exe
.\mperft.exe -d 7 -h 64 -t 4 -b -q
llvm-profdata merge -sparse -output=mperft.profdata *.profraw
icx-cc -std=c23 -pedantic -W -Wall -O3 -flto -fomit-frame-pointer -fuse-ld=lld -DNDEBUG -D_CRT_SECURE_NO_DEPRECATE -march=%1 -fprofile-instr-use=mperft.profdata mperft.c -o .\mperft-%1.exe


