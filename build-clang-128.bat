clang -std=c23 -pedantic -W -Wall -O3 -flto -fuse-ld=lld -DNDEBUG -D_CRT_SECURE_NO_DEPRECATE -DUSE_INT128 -march=%1 -fprofile-generate %2\mperft.c -o .\mperft.exe -lclang_rt.builtins-x86_64
.\mperft.exe -d 7 -h 64 -t 4 -n -q
.\mperft.exe -d 7 -h 64 -t 4 -b -q
llvm-profdata merge -sparse -output=mperft.profdata *.profraw
clang -std=c23 -pedantic -W -Wall -O3 -flto -fomit-frame-pointer -fuse-ld=lld -DNDEBUG -D_CRT_SECURE_NO_DEPRECATE -DUSE_INT128 -march=%1 -fprofile-use=mperft.profdata %2\mperft.c -o .\mperft-%2-%1-128.exe -lclang_rt.builtins-x86_64
del *.profraw *.profdata
