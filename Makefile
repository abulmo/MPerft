#libs
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
	CFLAGS = -std=c23 -Wall -W -pedantic
	ifeq ($(BUILD),fast)
		CFLAGS += -O3 -flto -DNDEBUG
	else ifeq ($(BUILD),profile)
		CFLAGS += -O3 -flto -fno-inline -DNDEBUG
	else ifeq ($(BUILD),cov)
		CFLAGS += -O3 -flto -fno-inline -NDEBUG --coverage
	else
		CFLAGS += -O0 -g -fno-inline -ftrapv
	endif

	LPF = LLVM_PROFILE_FILE=mperft-%p.profraw
	BOLT = -Xlinker --emit-relocs -Xlinker -znow
	PGO_GEN = -fprofile-generate
	PGO_USE = -fprofile-use=mperft.profdata
	PGO_MERGE = llvm-profdata merge -output=mperft.profdata *.profraw
	COV = llvm-cov gcov -b $(EXE)

endif

#icx (intel compiler)
ifeq ($(CC),icx)
	CFLAGS = -std=c23 -Wall -W -pedantic
	ifeq ($(BUILD),fast)
		CFLAGS += -O3 -flto -DNDEBUG
	else ifeq ($(BUILD),profile)
		CFLAGS += -O3 -flto -fno-inline -DNDEBUG
	else
		CFLAGS += -O0 -g -fno-inline -ftrapv
	endif

	LPF = LLVM_PROFILE_FILE=mperft-%p.profraw
	BOLT = -Xlinker --emit-relocs
#	PGO_GEN = -fprofile-generate
#	PGO_USE = -fprofile-use=mperft.profdata
	PGO_GEN = -fprofile-instr-generate
	PGO_USE = -fprofile-instr-use=mperft.profdata
	PGO_MERGE = llvm-profdata merge -output=mperft.profdata *.profraw
	COV =

endif

#gcc
ifeq ($(CC),gcc)
	CFLAGS = -pipe -Wall -W -Wextra -pedantic -std=c23
	ifeq ($(BUILD),fast)
		CFLAGS += -Ofast -flto -DNDEBUG
	else ifeq ($(BUILD),profile)
		CFLAGS += -O3 -pg -fno-inline -flto -DNDEBUG
	else ifeq ($(BUILD),cov)
		CFLAGS += -O3 -fno-inline -fprofile-arcs -ftest-coverage -DNDEBUG
	else
		CFLAGS += -O0 -g -fno-inline -fstack-protector
	endif

	LPF =
	BOLT = -Xl,--emit-relocs -Xl,-znow
	PGO_GEN = -fprofile-generate -lgcov
	PGO_USE = -fprofile-use -fprofile-correction
	PGO_MERGE =
	COV = gcov -b $(EXE)

endif

ifeq ($(COUNT),)
	COUNT = 64
endif

ifeq ($(COUNT),128)
	CFLAGS += -DUSE_INT128
endif

pgo:
	@$(MAKE) pgo-instr
	@echo Re-compiling with profile-guided optimization...
	@$(CC) $(CFLAGS) -march=$(ARCH) $(PGO_USE) mperft.c -o $(EXE)

pgo-instr:
	@$(MAKE) clean
	@echo Compiling with $(CC) for $(ARCH) architecture using $(COUNT) bits large integer.
	@echo Compiling with instrumentation for profile-guided optimization...
	@$(CC) $(CFLAGS) -march=$(ARCH) $(PGO_GEN) mperft.c -o $(EXE)
	@echo -n "Running the instrumented binary: "
	@ echo -n -e "1\b"
	@$(LPF) $(BIN)/$(EXE) --bench -b -h 256 > /dev/null
	@ echo -n -e "2\b"
	@$(LPF) $(BIN)/$(EXE) --bench -n -h 256 > /dev/null
	@ echo -n -e "3\b"
	@$(LPF) $(BIN)/$(EXE) --bench 7 -n -t 2 -h 256 > /dev/null;
	@ echo "done"
	@$(PGO_MERGE)

release:
	@$(MAKE) pgo-instr
	@echo Re-compiling with static libs and profile-guided optimization...
	@$(CC) $(CFLAGS) -march=$(ARCH) $(PGO_USE) -static mperft.c -o $(EXE)

pgo-128:
	@$(MAKE) pgo COUNT=128

no-pgo:
	@echo Compiling with $(CC) for $(ARCH) architecture using $(COUNT) bits large integer.
	@echo Compiling without profile-guided optimization...
	@$(CC) $(CFLAGS) -march=$(ARCH) mperft.c -o $(EXE)

bolt:
	@$(MAKE) pgo-instr
	@echo Re-compiling with profile-guided optimization...
	@$(CC) $(CFLAGS) $(BOLT) -march=$(ARCH) $(PGO_USE) mperft.c -o $(EXE)
	@llvm-bolt $(EXE) -instrument --instrumentation-file=$(EXE).fdata -o=$(EXE).i
	@echo -n "Running the instrumented binary for llvm-bolt: "
	@$(BIN)/$(EXE).i -d 7 -n -t 4 -h 256 -q  > /dev/null;
	@llvm-bolt ./$(EXE) -o ./$(EXE).bolt -data=$(EXE).fdata -reorder-blocks=ext-tsp -reorder-functions=cdsort -split-functions -split-all-cold -split-eh -dyno-stats

prof:
	@$(MAKE) no-pgo BUILD=profile

debug:
	@$(MAKE) no-pgo BUILD=debug

cov:
	@$(MAKE) clean
	@$(MAKE) no-pgo BUILD=cov
	@echo -n "Running the instrumented binary for test coverage: "
	@$(EXE) -d 7 -n -t 4 -h 256 -q
	@echo "running the coverage tool"
	$(COV)

clean:
	@$(RM) *.o *.dyn *.gcda *.gcno pgopti* *.prof* *.fdata

test:
	mperft --test -b -h 64 -t 4

help:
	@echo "Usage: make [pgo-128|pgo|no-pgo|prof|debug|cov|clean|test] [ARCH=<arch>] [CC=<compiler>]"
	@echo "  pgo       - build with PGO optimization (default)"
	@echo "  pgo-128   - build with PGO optimization for 128-bit counters & Zobrist's keys"
	@echo "  no-pgo    - build without PGO optimization"
	@echo "  prof      - build with profiling enabled"
	@echo "  debug     - build with debugging symbols"
	@echo "  cov       - build with coverage instrumentation"
	@echo "  clean     - remove object files and binaries"
	@echo "  test      - run the test suite"
	@echo "  help      - show this help message"
	@echo "  ARCH      - specify the target architecture (default: ARCH=native)"
	@echo "  CC        - specify the target compiler (default: CC=clang)"


.PHONY : all pgo-128 pgo no-pgo prof release debug clean test

# Dependencies
