FPSanitizer - A debugger to detect and diagnose numerical errors in floating point programs
======

Building LLVM
------

1. Get llvm and clang version 9

```
  wget http://releases.llvm.org/9.0.0/llvm-9.0.0.src.tar.xz
  wget http://releases.llvm.org/9.0.0/cfe-9.0.0.src.tar.xz
```

2. Build llvm and clang

```
  tar -xvf llvm-9.0.0.src.tar.xz
  mv llvm-9.0.0.src/ llvm
  tar -xvf cfe-9.0.0.src.tar.xz
  mv cfe-9.0.0.src clang
  mkdir build
  cd build
  cmake -DLLVM_ENABLE_PROJECTS=clang -G "Unix Makefiles" ../llvm
  make -j8

```

3. Set env variable LLVM_HOME to the LLVM build directory
```
  export LLVM_HOME=<path to LLVM build>
```

Building FPSanitizer
------

1. Clone fpsan git repo.
```
  git clone https://github.com/rutgers-apl/fpsanitizer.git

```

2. Set the environment variable FPSAN_HOME

```
  export FPSAN_HOME=<path to the FPSan github checkout>

```


3. If your compiler does not support C++11 by default, add the following line to $FPSAN_HOME/fpsan-pass/FPSan/CMakefile

```
  target_compile_feature(FPSanitizer PRIVATE cxx_range_for cxx_auto_type)

```

otherwise, use the followng line

```
  target_compile_features(FPSanitizer PRIVATE )

```

3. Build the FPSan pass

```
  cd $FPSAN_HOME/fpsan_pass
  mkdir build
  cd build
  cmake -DCMAKE_BUILD_TYPE="Debug" ../
  make

```


4. Build the FPSan runtime

```
  cd $FPSAN_HOME/fpsan_runtime
  export SET_TRACING=TRACING
  make

```


Testing microbenchmarks using fpsanitizer:
------

One can test the microbenchmarks using
fpsanitizer with the following commands: 

```
  export LD_LIBRARY_PATH=$FPSAN_HOME/fpsan_runtime/obj:$LD_LIBRARY_PATH
  cd fpsanitizer_test/
  python3 correctness_test.py
```


This process should take less than 4 minutes. During the execution,
the script runs each microbenchmark and reports whether the
microbenchmark has (a) NaN computation, (b)catastrophic cancellation,
(c) infinity computation, or (d) branch flip. The script also outputs
whether this error is correctly found (expected with green letters) or
incorrectly found (unexpected with red letters). The script should
only report "expected" and not "unexpected."

Finally, when the script terminates, it reports the total number of
microbenchmarks, the total number of microbenchmarks that reports each
type of error, and whether the numbers are correct or not.

------
Running case studies using fpsanitizer:
------
```
 $ cd case_studies/cordic/cordic_sin
 $ gdb cordic_sin.fp.o
```
Now our goal is to find a computation of the variable "y"
which has more than or equal to 45 bits of error.  Place a
conditional breakpoint which looks for any floating point operation
resulting in the error of 45 bits or more. Since
handleReal.cpp is a part of the shared library, type "y"
when prompted, and run the program:

```
    (gdb) break handleReal.cpp:180 if realRes->error >= 45
    (gdb) y
    (gdb) run
```
Skip the first eight break. This instance computes the value for
the variable "z." 
```
    (gdb) continue
```
Now, call the function "fpsan_trace" which prints the trace of
expressions leading up to the computed value:
```
    (gdb) call fpsan_trace(realRes)
```
You should be able to see a trace of 8 operations. For each
line, the first value tells you the line number of the computation. The
second value tells you which operations are executed, the
third value shows the line number for first operand,
and the fourth value shows the line number for the
second operand. The result of reals (mpfr) computation, the
result of floating point computation, and the number of bits in error
is shown in parenthesis.

You will notice that the first line of trace for an add operation
incurs 45 bits of error. You should also be able to track
the operands by looking at the line number of the operands and
finding the corresponding trace.

Now our goal is to detect the first instance of branch
flip. First, remove the exisiting breakpoint and add a
breakpoint that detects where branch flip occurs:

```
    (gdb) clear
    (gdb) break handleReal.cpp:143 if(realRes != computedRes)
    (gdb) continue
```
At this point, the floating point result of "op1" causes a branch
flip. You can look at the trace of "op1" by calling
"fpsan_trace":

```
    (gdb) call fpsan_trace(op1)
```
You should see a trace of 4 instructions. You will notice that
the first line of the trace for a subtraction operation
(FSUB) shows that fp computed a negative value whereas
mpfr computed a positive value. The prorgram at this point
compares op1 < 0. This means that fp computation will
incorrectly evaluate op1 < 0.


(4.2) Simpson's Rule (Section 5.2.2)

 The source code for simpson's rule can be found in the case
 studies directory. 
```
    $ cd case_studies/simpson
```
Compile the program,
```
    $ make
```
Run the program simpsons.fp.o. It will take roughly 1
minute. You should observe that the output is
1.8633..E+20. However, the correct result is 1.8840...E20. 
If you view error.log, there are a number of operations with  
more than 45 bits of error.

Run gdb, set break point to line 28 in SimpsonsRule.c, and start the program:
```
    $ gdb SimpsonsRule.fp.o
    (gdb) break 28
    (gdb) r
    (gdb) break handleReal.cpp:180
    (gdb) y
    (gdb) c
```
handleReal.cpp:180 is a location in the runtime that
explicitly checks whether an operation has more than or
equal to 45 bits of error. When gdb breaks, look at the
trace of the realRes:
```
    (gdb) call fpsan_trace(realRes)
```
You will notice that at this point, the first operation
FMUL at line 28 has 45 bits of error. It's operands at line 28 and 27 
have error of 26 and 45 bits respectively. FADD at line 27 has two operands- 
line 25 and 6. Line 6 is a function so we don't have a trace for computations 
in this function. To diagnose the errors in this function we have to set a breakpoint
in this function. 
   ``` 
    (gdb) delete
    (gdb) b SimpsonsRule.c:27
    (gdb) b f
    (gdb) b fpsan_check_error_f
    (gdb) call fpsan_trace(realRes)
```
At this point you can see that multiplication at line 6 causes 27 bits of error. 
We can investigate how we have got 45 bits of error in FADD at line 25. 
We want to skip few iterations of the for loop in floatSimpsonsRuleV1F1. 
To do that follow below steps:
```
    (gdb) b SimpsonsRule.c:23
    (gdb) r 
    (gdb) c 2000
    (gdb) b fpsan_check_error_f
    (gdb) c
    (gdb) call fpsan_trace(realRes)
```
This will show error trace for FMUL at line 22 with error of 26 bits.
If you continue skipping some iterations and looking at error trace, you will notice that 
error is being accumulated due to computations with very large values.
