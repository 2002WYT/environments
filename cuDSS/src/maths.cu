#include "head.cuh"
#include <cuda_runtime.h>

__global__ void axpy_kernel(int n, double alpha, const double* x, double* y)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;

    if (i < n)
    {
        y[i] = alpha * x[i] + y[i];
    }
}
__global__ void dot_kernel(int n, const double *x, const double *y, double *partial_sums)
{
    extern __shared__ double sdata[];
    int tid = threadIdx.x;
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    double val = 0.0;
    if (i < n)
    {
        val = x[i] * y[i];
    }
    sdata[tid] = val;
    __syncthreads();
    for (int stride = blockDim.x/2; stride > 0; stride /= 2)
    {
        if (tid < stride)
        {
            sdata[tid] += sdata[tid + stride];
        }
        __syncthreads();
    }
    if (tid == 0)
    {
        partial_sums[blockIdx.x] = sdata[0];
    }
}
__global__ void spmv_csr_kernel(int n, const int *row_ptr, const int *col_idx, 
    const double *values, const double *x, double *y)
{
    int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row < n)
    {
        double sum = 0.0;
        for (int jj = row_ptr[row]; jj < row_ptr[row + 1]; jj++)
        {
            sum += values[jj] * x[col_idx[jj]];
        }
        y[row] = sum;
    }
}
int AXPY_gpu(int n, const double *x, double *y, double alpha, 
    int blockSize, int gridSize)
{
    double* d_x = nullptr;
    double* d_y = nullptr;
    CHECK_CUDA(cudaMalloc((void**)&d_x, n * sizeof(double)));
    CHECK_CUDA(cudaMalloc((void**)&d_y, n * sizeof(double)));

    CHECK_CUDA(cudaMemcpy(d_x, x, n * sizeof(double), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_y, y, n * sizeof(double), cudaMemcpyHostToDevice));

    axpy_kernel <<<gridSize, blockSize>>> (n, alpha, d_x, d_y);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    CHECK_CUDA(cudaMemcpy(y, d_y, n * sizeof(double), cudaMemcpyDeviceToHost));
    CHECK_CUDA(cudaFree(d_x));
    CHECK_CUDA(cudaFree(d_y));
    return 0;
}
int Dot_gpu(int n, const double *x, const double *y, double &result, 
    int blockSize, int gridSize)
{
    double* d_x = nullptr;
    double* d_y = nullptr;
    double *d_partial_sums = nullptr;
    CHECK_CUDA(cudaMalloc((void**)&d_x, n * sizeof(double)));
    CHECK_CUDA(cudaMalloc((void**)&d_y, n * sizeof(double)));
    CHECK_CUDA(cudaMalloc(&d_partial_sums, gridSize * sizeof(double)));

    std::vector<double> h_partial_sums(gridSize);

    CHECK_CUDA(cudaMemcpy(d_x, x, n * sizeof(double), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_y, y, n * sizeof(double), cudaMemcpyHostToDevice));
    dot_kernel <<<gridSize, blockSize, blockSize * sizeof(double)>>> 
        (n, d_x, d_y, d_partial_sums);
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    CHECK_CUDA(cudaMemcpy(h_partial_sums.data(), d_partial_sums, 
        gridSize * sizeof(double), cudaMemcpyDeviceToHost));
    result = 0.0;
    for (int i = 0; i < gridSize; i++)
    {
        result += h_partial_sums[i];
    }
    CHECK_CUDA(cudaFree(d_x));
    CHECK_CUDA(cudaFree(d_y));
    CHECK_CUDA(cudaFree(d_partial_sums));
    return 0;
}
int Spmv_cpu(int n, int nnz, const int *row_ptr, const int *col_idx, 
    const double *values, 
    const double *x, double *y, int blockSize, int gridSize)
{
    int *d_row_ptr = nullptr;
    int *d_col_idx = nullptr;
    double *d_values = nullptr;
    double* d_x = nullptr;
    double* d_y = nullptr;
    CHECK_CUDA(cudaMalloc((void**)&d_row_ptr, (n+1) * sizeof(int)));
    CHECK_CUDA(cudaMalloc((void**)&d_col_idx, nnz * sizeof(int)));
    CHECK_CUDA(cudaMalloc((void**)&d_values, nnz * sizeof(double)));
    CHECK_CUDA(cudaMalloc((void**)&d_x, n * sizeof(double)));
    CHECK_CUDA(cudaMalloc((void**)&d_y, n * sizeof(double)));

    CHECK_CUDA(cudaMemcpy(d_row_ptr, row_ptr, (n+1) * sizeof(int), 
        cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_col_idx, col_idx, nnz * sizeof(int), 
        cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_values, values, nnz * sizeof(double), 
        cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_x, x, n * sizeof(double), 
        cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_y, y, n * sizeof(double), 
        cudaMemcpyHostToDevice));
    spmv_csr_kernel <<<gridSize, blockSize>>> (n, d_row_ptr, d_col_idx,
        d_values, d_x, d_y);
    
    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());
    CHECK_CUDA(cudaMemcpy(y, d_y, 
        n * sizeof(double), cudaMemcpyDeviceToHost));

    CHECK_CUDA(cudaFree(d_row_ptr));
    CHECK_CUDA(cudaFree(d_col_idx));
    CHECK_CUDA(cudaFree(d_values));
    CHECK_CUDA(cudaFree(d_x));
    CHECK_CUDA(cudaFree(d_y));
    return 0;
}
