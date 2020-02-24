#CFLAGS=-O1 -fno-slp-vectorize -std=c11  -L$(FPROOT)/obj -lfpsanitizer -lmpfr -lm -pthread -ltbb -lstdc++
CFLAGS=-O1 -fno-slp-vectorize -g -std=c11 -L$(FPSAN_HOME)/fpsan_runtime/obj -lfpsanitizer -lmpfr -lm  -lstdc++ 
CAMLFLAGs=
CSRC = $(wildcard *.c)
COUT = $(patsubst %.c,%.o,$(CSRC))

#DEBUGTRACEFLAG = -D debugtrace
DEBUGTRACEFLAG = 


CC = clang $(DEBUGTRACEFLAG) -pg -Xclang -load -Xclang $(FPSAN_HOME)/fpsan_pass/build/FPSan/libFPSanitizer.so

all: $(COUT) $(CAMLOUT)

%.o: %.c
	$(CC) -o $@ $< $(CFLAGS)
	chmod u+x $@

%.sout: %.s
	$(CC) -o $@ $< $(CFLAGS)
	chmod u+x $@

%.s: %.c
	$(CC) $(CFLAGS) -S -o $@ $<


clean:
	rm -rf *.o *.sout *.dSYM *~ *.log functions.txt  *.out *.ll *.bc
