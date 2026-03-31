del *.profraw
clang -std=c23 -pedantic -W -Wall -O3 -flto -fuse-ld=lld -D_CRT_SECURE_NO_DEPRECATE -march=%1 -fprofile-generate mperft.c -o .\mperft.exe
.\mperft.exe -d 7 -h 64 -t 4 -n -q
.\mperft.exe -d 7 -h 64 -t 4 -b -q
llvm-profdata merge -sparse -output=mperft.profdata *.profraw
clang -std=c23 -pedantic -W -Wall -O3 -flto -fomit-frame-pointer -fuse-ld=lld -DNDEBUG -D_CRT_SECURE_NO_DEPRECATE -march=%1 -fprofile-use=mperft.profdata mperft.c -o .\mperft-%1.exe
del *.profraw *.profdata


