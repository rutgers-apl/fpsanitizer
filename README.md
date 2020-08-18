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
  make

```



Testing the FPSanitizer with applications
------

1. Try out the tests in regression_tests directory. Set the following environment variables

```
  export LLVM_HOME=<LLVM build directory>

  export FPSAN_HOME=<path to the FPSan github checkout>

  export LD_LIBRARY_PATH=$FPSAN_HOME/fpsan_runtime/obj/

```

and then,
```
  cd $FPSAN_HOME/regression_tests
  
  make

  ./diff-root-simple.o

```

It should report the number of branch flips and number of instances with more than 50 ulp errors in error.log


2. Try out the various modes: tracing that prints out DAGs, generating
DAGs in FPCore format, and different precisions for the metadata.
