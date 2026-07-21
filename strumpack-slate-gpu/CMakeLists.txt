cmake_minimum_required(VERSION 3.18)
if(DEFINED ENV{CUDA_HOME})
    set(CMAKE_CUDA_COMPILER "$ENV{CUDA_HOME}/bin/nvcc" CACHE FILEPATH "CUDA compiler")
    set(CUDAToolkit_ROOT "$ENV{CUDA_HOME}" CACHE PATH "CUDA Toolkit root")
endif()
set(CMAKE_CUDA_COMPILER /usr/local/cuda-12.6/bin/nvcc)
project(cuda_test LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_ARCHITECTURES 70)
set(CUDSS_ROOT /home/wangyitong/cudss_install/nvidia/cu12)
set(MPI_HOME /usr/lib/x86_64-linux-gnu/openmpi)
set(STRUMPACK_ROOT /home/wangyitong/soft/strumpack-slate-install)
set(SCALAPACK_ROOT /home/wangyitong/soft/scalapack-install)
set(OPENBLAS_ROOT /home/wangyitong/soft/openblas-install)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${catkin_INCLUDE_DIRS}
    ${CUDSS_ROOT}/include
    ${MPI_HOME}/include
    ${STRUMPACK_ROOT}/include
)

link_directories(
    ${catkin_LIB_DIRS}
    ${CUDSS_ROOT}/lib
    ${MPI_HOME}/lib
    ${STRUMPACK_ROOT}/lib
    ${SCALAPACK_ROOT}/lib
    ${OPENBLAS_ROOT}/lib
)

find_package(CUDAToolkit REQUIRED)
aux_source_directory(src SRC_SOURCE)
add_library(SRC ${SRC_SOURCE})

set(EXE3 test_strumpack)
add_executable(${EXE3} test_strumpack.cpp)
target_link_libraries(${EXE3} PRIVATE
    SRC
    ${CUDSS_ROOT}/lib/libcudss.so.0
    CUDA::cublas
    CUDA::cudart
    strumpack
    mpi
    mpi_cxx
    scalapack
    openblas
)
