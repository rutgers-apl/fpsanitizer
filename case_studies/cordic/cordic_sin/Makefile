#CFLAGS=-O1 -fno-slp-vectorize -std=c11  -L$(FPROOT)/obj -lfpsanitizer -lmpfr -lm -pthread -ltbb -lstdc++
CFLAGS=-O1 -fno-slp-vectorize -g -std=c11 -L$(FPSAN_HOME)/fpsan_runtime/obj -lfpsanitizer -lmpfr -lm  -lstdc++  
CAMLFLAGs=
CSRC = $(wildcard *.c)
COUT = $(patsubst %.c,%.o,$(CSRC))
FPCOUT = $(patsubst %.c,%.fp.o,$(CSRC))
GCC = clang -O1
CC = clang -O1 -pg -Xclang -load -Xclang $(FPSAN_HOME)/fpsan_pass/build/FPSan/libFPSanitizer.so

all: $(COUT) $(FPCOUT)

%.fp.o: %.c
	$(CC) -o $@ $< $(CFLAGS)
	chmod u+x $@

%.o: %.c
	$(GCC) -o $@ $< $(CFLAGS)
	chmod u+x $@

clean:
	rm -rf *.o *.sout *.dSYM *~ *.log functions.txt  *.out
