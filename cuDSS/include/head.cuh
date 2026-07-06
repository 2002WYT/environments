#pragma once
#include <cuda_runtime.h>
#include <library_types.h>
#include <cudss.h>
#include <iostream>
#include <vector>
#define CHECK_FUNC(call)                                      \
    do {                                                      \
        int status = (call);                                  \
        if (status != 0) {                                    \
            std::cerr << "Function error at " << __FILE__     \
                      << ":" << __LINE__                      \
                      << " : status = " << status             \
                      << std::endl;                           \
            return 1;                                         \
        }                                                     \
    } while (0)

#define CHECK_CUDA(call)                                      \
    do {                                                      \
        cudaError_t err = (call);                             \
        if (err != cudaSuccess) {                             \
            std::cerr << "CUDA error at " << __FILE__ << ":"  \
                      << __LINE__ << " : "                    \
                      << cudaGetErrorString(err) << std::endl; \
            return 1;                                         \
        }                                                     \
    } while (0)

#define CHECK_CUDSS(call)                                     \
    do {                                                      \
        cudssStatus_t status = (call);                        \
        if (status != CUDSS_STATUS_SUCCESS) {                 \
            std::cerr << "cuDSS error at " << __FILE__ << ":" \
                      << __LINE__ << " : status = "           \
                      << static_cast<int>(status) << std::endl;\
            return 1;                                         \
        }                                                     \
    } while (0)

int AXPY_gpu(int n, const double *x, double *y, double alpha, 
    int blockSize, int gridSize);
int Dot_gpu(int n, const double *x, const double *y, double &result, 
    int blockSize, int gridSize);
int Spmv_cpu(int n, int nnz, const int *row_ptr, const int *col_idx, 
    const double *values, 
    const double *x, double *y, int blockSize, int gridSize);
