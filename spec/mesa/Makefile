CC = clang -pg -O1
CCE = clang -pg -O1 -fno-inline -emit-llvm 
OPT = opt -load $(FPSAN_HOME)/fpsan_pass/build/FPSan/libFPSanitizer.so -fpsan
GCC := clang -pg -O1
SRC_DIR := .
OBJ_DIR := .
SRC_FILES := $(wildcard $(SRC_DIR)/*.c)
OBJ_FILES := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRC_FILES))
OBJ_FILES_FP := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.bc,$(SRC_FILES))
CPPFLAGS := -g -std=c11  -lmpfr -lm  -lpthread
CFLAGS= -fno-slp-vectorize -std=c11 -L$(FPSAN_HOME)/fpsan_runtime/obj -lfpsanitizer -lmpfr -lm  -lstdc++ -L$(FPSAN_HOME)/tbb/build -ltbb -I$(MPFR_HOME)/include
CXXFLAGS :=  -g -I . -I ./libutil -DSPEC_CPU -DNDEBUG -I. -DFN -DFAST -DCONGRAD_TMP_VECTORS -DDSLASH_TMP_LINKS   -fno-strict-aliasing -DSPEC_CPU_LP64  

all: mesa mesa_fp.bc mesa_opt_fp.bc mesa_opt_fp.o mesa_fp

mesa_fp:mesa_opt_fp.o 
	$(CC) $^ -o $@  $(CPPFLAGS) $(CFLAGS)

mesa_opt_fp.o: 
	llc mesa_opt_fp.bc -filetype=obj  -o $@ $^

mesa_opt_fp.bc: 
	$(OPT) <mesa_fp.bc> $@ $^

mesa_fp.bc: $(OBJ_FILES_FP)
	llvm-link $^ -o $@ 

$(OBJ_DIR)/%.bc: $(SRC_DIR)/%.c
	$(CCE) $(CXXFLAGS) -c -o $@ $< 

mesa: $(OBJ_FILES)
	$(GCC) $(CPPFLAGS) -o $@ $^ 

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(GCC) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -rf  mesa mesa_fp mesa.o  *.out *.o *.dSYM *.bc mesa_opt_fp
