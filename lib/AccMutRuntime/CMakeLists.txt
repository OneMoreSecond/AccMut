add_llvm_library(AccMutRuntime
        accmut_arith_common.cpp
        accmut_async_sig_safe_string.c
        accmut_config.c
        accmut_dma_fork.c
        accmut_GoodVar.cpp
        accmut_GoodVar_fork.c
        accmut_io.c

        ADDITIONAL_HEADER_DIRS
        ${LLVM_MAIN_INCLUDE_DIR}/llvm/AccMutRuntime
        )