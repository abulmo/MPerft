#libs
LIBS = -lm
MAKE = make
RM = rm -f
BIN = .
EXE = mperft

ifeq ($(BUILD),)
	BUILD=fast
endif

ifeq ($(CC),)
	CC=clang
endif

#hack: replace cc by clang
ifeq ($(CC),cc)
	CC=clang
endif

ifeq ($(ARCH),)
	ARCH=native
endif

#clang
ifeq ($(CC),clang)
	CFLAGS = -std=c23 -Wall -W -pedantic -D_GNU_SOURCE=1
	ifeq ($(BUILD),fast)
		CFLAGS += -O3 -flto -DNDEBUG
	else ifeq ($(BUILD),profile)
		CFLAGS += -O3 -fno-inline -DNDEBUG
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
	CFLAGS = -pipe -Wall -W -Wextra -pedantic -std=c23 -D_GNU_SOURCE=1
	ifeq ($(BUILD),fast)
		CFLAGS += -Ofast -flto -DNDEBUG
	else ifeq ($(BUILD),profile)
		CFLAGS += -O3 -pg -DNDEBUG
	else ifeq ($(BUILD),cov)
		CFLAGS += -O0 -fno-inline -fprofile-arcs -ftest-coverage -DNDEBUG
	else
		CFLAGS += -O0 -g -fno-inline -fstack-protector
	endif

	PGO_GEN = -fprofile-generate -lgcov
	PGO_USE = -fprofile-use
	PGO_MERGE =
endif

#mingw 64 bits
ifeq ($(CC),x86_64-w64-mingw32-gcc)
	CFLAGS = -pipe -Wall -W -Wextra -pedantic -std=c23 -D__USE_MINGW_ANSI_STDIO -DWINVER=0x501 -D_GNU_SOURCE=1
	EXE = mperft.exe
	ifeq ($(BUILD),fast)
		CFLAGS += -O3 -flto -fwhole-program -DNDEBUG
	else
		CFLAGS += -O0 -g -fno-inline -fstack-protector
	endif

	PGO_GEN = -fprofile-generate -lgcov
	PGO_USE = -fprofile-use
	PGO_MERGE =
endif

#commands
all :
	$(CC) $(CFLAGS) -march=$(ARCH) mperft.c -o $(BIN)/$(EXE) $(LIBS)

pgo :
	$(MAKE) clean
	$(CC) $(CFLAGS) -march=$(ARCH) $(PGO_GEN) mperft.c -o $(BIN)/$(EXE) $(LIBS)
	cd $(BIN); LLVM_PROFILE_FILE=mperft-%p.profraw ./$(EXE) -d 7 -b | grep perft;
	cd $(BIN); LLVM_PROFILE_FILE=mperft-%p.profraw ./$(EXE) -d 8 -b -h 256 | grep perft;
	$(PGO_MERGE)
	$(CC) $(CFLAGS) -march=$(ARCH) $(PGO_USE) mperft.c -o $(BIN)/$(EXE) $(LIBS)

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

.PHONY : all pgo prof release debug clean test

# Dependencies
