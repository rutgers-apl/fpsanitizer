# fpsanitizer - A debugger to detect and diagnose numerical errors in floating point programs


## How to build

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
      mv cfe-9.0.0.src/* clang
      mkdir build
      cd build
      cmake -DLLVM_ENABLE_PROJECTS=clang -G "Unix Makefiles" ../llvm
      make -j8

```

3. Set env variable LLVM_HOME to the LLVM build directory
```
  export LLVM_HOME=<path to LLVM build>
```

4. Clone fpsan git repo.
```
  git clone https://github.com/rutgers-apl/fpsanitizer.git

```

5. If your compiler does not support C++11 by default, add the following line to fpsan-pass/FPSan/CMakefile

```
  target_compile_feature(FPSanitizer PRIVATE cxx_range_for cxx_auto_type)

```

otherwise, use the followng line

```
        target_compile_features(FPSanitizer PRIVATE )

```

6. Build the FPSan pass

```
  mkdir build
  cd build
  cmake ../
  make

```

7. Try out the tests in regression_tests directory. Set the following environment variables

```
  export LLVM_HOME=<LLVM build directory>

  export FPSAN_HOME=<path to the FPSan github checkout>

  export LD_LIBRARY_PATH=$FPSAN_HOME/runtime/obj/

```

and then,
```
  make

  ./diff-root-simple.o

```

It should report the number of branch flips and number of instances with more than 50 ulp errors in error.log
      