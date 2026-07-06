// A program of realizing axpy, dot, SpMV functions on GPU
#include "head.cuh"
using namespace std;

int main()
{
    int device_count = 0;
    CHECK_CUDA(cudaGetDeviceCount(&device_count));
    if (device_count == 0) {
        std::cerr << "No CUDA device found." << std::endl;
        return 1;
    }
    CHECK_CUDA(cudaSetDevice(0));

    int n = 4;
    int blockSize = 256;
    int gridSize = (n + blockSize - 1) / blockSize;
    vector<double> a = {2.0, 3.0, 4.0, -1.0}, 
        b = {1.0, 1.0, 4.0, 5.0};
    double alpha = 2.0;


    cout << "a: " << endl;
    for (int i = 0; i < n; i++)
    {
        cout << a[i] << " ";
    }
    cout << endl << "b: " << endl;
    for (int i = 0; i < n; i++)
    {
        cout << b[i] << " ";
    }
    cout << endl << "alpha: " << alpha << endl;

    CHECK_FUNC(AXPY_gpu(n, a.data(), b.data(), alpha, blockSize, gridSize));

    cout << "Result b = alpha * a + b:" << endl;
    for (int i = 0; i < n; i++)
    {
        cout << b[i] << " ";
    }
    cout << endl;

    double dotsum;
    CHECK_FUNC(Dot_gpu(n, a.data(), b.data(), 
        dotsum, blockSize, gridSize));

    cout << "dot of a and b: " << dotsum << endl;

    int nA = 10000000, nnzA = 3*nA - 2;
    int blockSizeA = 256;
    int gridSizeA = (nA + blockSizeA - 1) / blockSizeA;
    vector<int> row_ptr(nA+1);
    vector<int> col_idx(nnzA);
    vector<double> values(nnzA);
    vector<double> a2(nA);
    vector<double> b2(nA);
    row_ptr[0] = 0;
    col_idx[0] = 0; col_idx[1] = 1;
    values[0] = 4.0; values[1] = -1.0;
    for (int i = 1; i < nA - 1; i++) 
    {
        row_ptr[i] = 3*i - 1;
        col_idx[3*i - 1] = i - 1;
        col_idx[3*i] = i;
        col_idx[3*i + 1] = i + 1;
        values[3*i - 1] = -1;
        values[3*i] = 4;
        values[3*i + 1] = -1;
    }
    row_ptr[nA-1] = 3*(nA-1) - 1;
    row_ptr[nA] = 3*nA - 2;
    col_idx[3*nA - 4] = nA - 2;
    col_idx[3*nA - 3] = nA - 1;
    values[3*nA - 4] = -1;
    values[3*nA - 3] = 4;
    for (int i = 0; i < nA; i++) 
    {
        a2[i] = 1.0;
        b2[i] = 0.0;
    }
    CHECK_FUNC(Spmv_cpu(nA, nnzA, row_ptr.data(), 
        col_idx.data(), values.data(), 
        a2.data(), b2.data(), blockSizeA, gridSizeA)); 
        // b = mat * a;

    double norm2b;
    CHECK_FUNC(Dot_gpu(nA, b2.data(), b2.data(), 
        norm2b, blockSizeA, gridSizeA));
    
    cout << "norm2 of b2: " << norm2b << endl;



    CHECK_CUDA(cudaDeviceReset());
    return 0;
}
