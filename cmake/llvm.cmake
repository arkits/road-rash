# Plan 22 Task 2: locates Homebrew LLVM for the fuzzing, TSan and clang-tidy
# trees. Apple clang ships neither libFuzzer nor clang-tidy, so these builds
# require the Homebrew compiler.
# Plan 33 Task A9: other hosts use the clang on PATH (distribution LLVM
# includes libFuzzer and the sanitizers); override with -DRR_LLVM_ROOT.
if(EXISTS "/opt/homebrew/opt/llvm/bin/clang++")
    set(RR_LLVM_DEFAULT "/opt/homebrew/opt/llvm")
else()
    find_program(RR_LLVM_CLANGXX NAMES clang++ clang++-22 clang++-21 clang++-20 clang++-19)
    if(RR_LLVM_CLANGXX)
        get_filename_component(RR_LLVM_BIN_DIR "${RR_LLVM_CLANGXX}" DIRECTORY)
        get_filename_component(RR_LLVM_DEFAULT "${RR_LLVM_BIN_DIR}" DIRECTORY)
    else()
        set(RR_LLVM_DEFAULT "/opt/homebrew/opt/llvm")
    endif()
endif()
set(RR_LLVM_ROOT "${RR_LLVM_DEFAULT}" CACHE PATH "LLVM prefix (Homebrew on macOS, PATH elsewhere)")
set(RR_LLVM_CC "${RR_LLVM_ROOT}/bin/clang")
set(RR_LLVM_CXX "${RR_LLVM_ROOT}/bin/clang++")
if(NOT EXISTS "${RR_LLVM_CXX}")
    message(FATAL_ERROR "Homebrew LLVM clang++ not found at ${RR_LLVM_CXX}; install it with `brew install llvm` (ask the owner before installing anything).")
endif()
