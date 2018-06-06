set(AccMutSourceFiles
        WAInstrumenter.cpp
        DMAInstrumenter.cpp
        MSInstrumenter.cpp
        MutationGen.cpp
        MutUtil.cpp
        StatisticsUtils.cpp)

add_llvm_loadable_module(AccMut
        ${AccMutSourceFiles}

        ADDITIONAL_HEADER_DIRS
        ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/AccMut

        DEPENDS
        intrinsics_gen
        PLUGIN_TOOL
        opt
        )

add_llvm_library(LLVMAccMut
        ${AccMutSourceFiles}

        ADDITIONAL_HEADER_DIRS
        ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/AccMut

        DEPENDS
        intrinsics_gen
        )
