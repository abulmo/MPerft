#libs
LIBS = -lm
MAKE = make
RM = rm -f
BIN = .
DEFINE = -D_GNU_SOURCE -DUSE_GCC_X64 -DPOPCOUNT $(PEXT)
EXE = mperft

ifeq ($(BUILD),)
	BUILD = fast
endif

ifeq ($(CC),)
	CC = gcc
endif

ifeq ($(CC),cc)
	CC = gcc
endif

#clang
ifeq ($(CC),clang)
	CFLAGS = -std=c11 -Wall -W -pedantic $(DEFINE)
	ifeq ($(BUILD),fast)
		CFLAGS += -march=native -O3 -flto -DNDEBUG 
	else ifeq ($(BUILD),profile)
		CFLAGS += -march=native -O3 -fno-inline -DNDEBUG
		LIBS += -lprofiler
	else
		CFLAGS += -O0 -g -fno-inline -ftrapv
	endif

	PGO_GEN = -fprofile-instr-generate
	PGO_USE = -fprofile-instr-use=mperft.profdata
	PGO_MERGE = llvm-profdata merge -output=mperft.profdata mperft-*.profraw

endif

#gcc
ifeq ($(CC),gcc)
	CFLAGS = -pipe -Wall -W -Wextra -pedantic -std=c11 $(DEFINE)
	ifeq ($(BUILD),fast)
		CFLAGS += -march=native -Ofast -flto -DNDEBUG
	else ifeq ($(BUILD),profile)
		CFLAGS += -march=native -O3 -pg -DNDEBUG
	else ifeq ($(BUILD),cov)
		CFLAGS += -march=native -O0 -fno-inline -fprofile-arcs -ftest-coverage -DNDEBUG
	else
		CFLAGS += -O0 -g -fno-inline -fstack-protector
	endif
	PGO_GEN = -fprofile-generate -lgcov
	PGO_USE = -fprofile-use
	PGO_MERGE = 
endif
 
#old gcc
ifeq ($(CC),gcc34)
	CFLAGS = -pipe -Wall -W -Wextra -pedantic -std=c99 $(DEFINE)
	ifeq ($(BUILD),fast)
		CFLAGS += -m64 -O3 -DNDEBUG
	else ifeq ($(BUILD),profile)
		CFLAGS += -m64 -O1 -pg -fno-inline -DNDEBUG
	else
		CFLAGS += -O0 -g -fno-inline -fstack-protector
	endif
	PGO_GEN = -fprofile-generate
	PGO_USE = -fprofile-use
	PGO_MERGE = 
endif

#icc
ifeq ($(CC),icc)
	CFLAGS = -std=c99 -Wall -Wcheck -wd2259 $(DEFINE)
	ifeq ($(BUILD),fast)
		CFLAGS += -xHost -Ofast -ansi-alias -DNDEBUG
	else
		CFLAGS += -xHost -Ofast -ansi-alias -g
	endif

	PGO_GEN = -prof_gen
	PGO_USE = -prof_use -wd11505
	PGO_MERGE = 
endif

#tcc
ifeq ($(CC),tcc)
	CFLAGS = -Wall -D_GNU_SOURCE
	ifeq ($(BUILD),fast)
		CFLAGS += -DNDEBUG
	else
		CFLAGS += -g -b
	endif

	PGO_GEN = 
	PGO_USE = 
	PGO_MERGE = 
endif

#mingw 32 bits
ifeq ($(CC),mingw32)
	CC = i686-w64-mingw32-gcc
	DEFINE = -D_GNU_SOURCE
	CFLAGS = -pipe -Wall -W -Wextra -pedantic -std=c11 $(DEFINE)
	CFLAGS += -D__USE_MINGW_ANSI_STDIO -DWINVER=0x501
	EXE = mperft.exe
	ifeq ($(BUILD),fast)
		CFLAGS += -march=i686 -O3 -flto -fwhole-program -DNDEBUG
	else
		CFLAGS += -O0 -g -fno-inline -fstack-protector
	endif
	PGO_GEN = -fprofile-generate -lgcov
	PGO_USE = -fprofile-use
	PGO_MERGE = 
endif

#mingw 64 bits
ifeq ($(CC),mingw64)
	CC = x86_64-w64-mingw32-gcc
	CFLAGS += -pipe -Wall -W -Wextra -pedantic -std=c11 $(DEFINE)
	CFLAGS += -D__USE_MINGW_ANSI_STDIO -DWINVER=0x501
	EXE = mperft.exe
	ifeq ($(BUILD),fast)
		CFLAGS += -march=native -O3 -flto -fwhole-program -DNDEBUG
	else
		CFLAGS += -O0 -g -fno-inline -fstack-protector
	endif
	PGO_GEN = -fprofile-generate -lgcov
	PGO_USE = -fprofile-use
	PGO_MERGE = 
endif

#commands
all :
	$(CC) $(CFLAGS) mperft.c -o $(BIN)/$(EXE) $(LIBS)

pgo :
	$(MAKE) clean
	$(CC) $(CFLAGS) $(PGO_GEN) mperft.c -o $(BIN)/$(EXE) $(LIBS)
	cd $(BIN); LLVM_PROFILE_FILE=mperft-%p.profraw ./$(EXE) -d 6 -b | grep perft; 
	cd $(BIN); LLVM_PROFILE_FILE=mperft-%p.profraw ./$(EXE) -d 7 -b -H 24 | grep perft; 
	$(PGO_MERGE)
	$(CC) $(CFLAGS) $(PGO_USE) mperft.c -o $(BIN)/$(EXE) $(LIBS)

pext:$(SRC)	
	PEXT=-DUSE_PEXT $(MAKE) pgo 

release:
	$(MAKE) clean
	$(CC) $(CFLAGS) -DRELEASE $(PGO_GEN) mperft.c -o $(BIN)/$(EXE) $(LIBS)
	LLVM_PROFILE_FILE=$(BIN)/mperft-%p.profraw $(BIN)/$(EXE) < bench.scr
	$(PGO_MERGE)
	$(CC) $(CFLAGS) -DRELEASE $(PGO_USE) mperft.c -o $(BIN)/$(EXE) $(LIBS)

prof:
	$(MAKE) BUILD=profile


debug :
	$(MAKE) BUILD=debug

cov :
	$(MAKE) BUILD=cov

clean:
	$(RM) *.o *.dyn *.gcda *.gcno pgopti* *.prof*
	cd $(BIN); $(RM) *.prof*
	
test:
	$(BIN)/mperft --test

.PHONY : all pgo prof release debug clean

SUFFIXES: .o .c

.c.o:
	$(CC) $(CFLAGS) -DNOINCLUDE -c $< -o $@ 

# Dependencies

