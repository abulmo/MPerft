#libs
LIBS = -lm
MAKE = make
RM = rm -f
BIN = .
DEFINE = -D_GNU_SOURCE -DUSE_GCC_X64
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

ifeq ($(EXT),) 
	EXT=popcount
endif

ifeq ($(ARCH),)
	ARCH=native
endif

#clang
ifeq ($(CC),clang)
	CFLAGS = -std=c11 -Wall -W -pedantic $(DEFINE)
	ifeq ($(BUILD),fast)
		CFLAGS += -O3 -flto -DNDEBUG 
	else ifeq ($(BUILD),profile)
		CFLAGS += -O3 -fno-inline -DNDEBUG
		LIBS += -lprofiler
	else
		CFLAGS += -O0 -g -fno-inline -ftrapv
	endif

	ifeq ($(EXT),popcount) 
		DEFINE += -DPOPCOUNT
		CFLAGS += -mpopcnt
	else ifeq ($(EXT),pext) 
		DEFINE += -DUSE_PEXT -DPOPCOUNT
		CFLAGS += -mbmi2 -mpopcnt
	endif

	PGO_GEN = -fprofile-instr-generate
	PGO_USE = -fprofile-instr-use=mperft.profdata
	PGO_MERGE = llvm-profdata merge -output=mperft.profdata mperft-*.profraw

endif

#gcc
ifeq ($(CC),gcc)
	CFLAGS = -pipe -Wall -W -Wextra -pedantic -std=c11 $(DEFINE)
	ifeq ($(BUILD),fast)
		CFLAGS += -Ofast -flto -DNDEBUG
	else ifeq ($(BUILD),profile)
		CFLAGS += -O3 -pg -DNDEBUG
	else ifeq ($(BUILD),cov)
		CFLAGS += -O0 -fno-inline -fprofile-arcs -ftest-coverage -DNDEBUG
	else
		CFLAGS += -O0 -g -fno-inline -fstack-protector
	endif

	ifeq ($(EXT),popcount) 
		DEFINE += -DPOPCOUNT
		CFLAGS += -mpopcnt
	else ifeq ($(EXT),pext) 
		DEFINE += -DUSE_PEXT -DPOPCOUNT
		CFLAGS += -mbmi2 -mpopcnt
	endif

	PGO_GEN = -fprofile-generate -lgcov
	PGO_USE = -fprofile-use
	PGO_MERGE = 
endif
 
#mingw 64 bits
ifeq ($(CC),x86_64-w64-mingw32-gcc)
	CFLAGS = -pipe -Wall -W -Wextra -pedantic -std=c11 $(DEFINE) -D__USE_MINGW_ANSI_STDIO -DWINVER=0x501
	EXE = mperft.exe
	ifeq ($(BUILD),fast)
		CFLAGS += -O3 -flto -fwhole-program -DNDEBUG
	else
		CFLAGS += -O0 -g -fno-inline -fstack-protector
	endif

	ifeq ($(EXT),popcount) 
		DEFINE += -DPOPCOUNT
		CFLAGS += -mpopcnt
	else ifeq ($(EXT),pext) 
		DEFINE += -DUSE_PEXT -DPOPCOUNT
		CFLAGS += -mbmi2 -mpopcnt
	endif

	PGO_GEN = -fprofile-generate -lgcov
	PGO_USE = -fprofile-use
	PGO_MERGE = 
endif

#commands
all :
	$(CC) $(CFLAGS) -march=$(ARCH) mperft.c -o $(BIN)/$(EXE) $(LIBS)

pgo :
	@echo $(CFLAGS)
	$(MAKE) clean
	$(CC) $(CFLAGS) -march=$(ARCH) $(PGO_GEN) mperft.c -o $(BIN)/$(EXE) $(LIBS) 
	cd $(BIN); LLVM_PROFILE_FILE=mperft-%p.profraw ./$(EXE) -d 6 -b | grep perft; 
	cd $(BIN); LLVM_PROFILE_FILE=mperft-%p.profraw ./$(EXE) -d 7 -b -H 24 | grep perft; 
	$(PGO_MERGE)
	$(CC) $(CFLAGS) -march=$(ARCH) $(PGO_USE) mperft.c -o $(BIN)/$(EXE) $(LIBS)

pext:$(SRC)	
	$(MAKE) pgo EXT=pext

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

