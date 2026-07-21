# STRUMPACK 离线安装与 GPU/SLATE 配置指南

本文记录在**服务器无法联网、没有 sudo 权限、但可以上传文件**的条件下，从源码安装并验证以下软件栈的完整流程：

- OpenBLAS：BLAS/LAPACK
- METIS：串行图划分与重排序
- ScaLAPACK：MPI 分布式稠密线性代数
- SLATE：支持 GPU 的 ScaLAPACK 替代库
- STRUMPACK：MPI + OpenMP + CUDA + SLATE 稀疏直接求解器

> 本文命令按一个实际成功环境整理。执行前请根据自己的服务器修改编译器、MPI、CUDA 和安装路径。

---

## 目录

- [1. 已验证环境](#1-已验证环境)
- [2. 软件栈与安装顺序](#2-软件栈与安装顺序)
- [3. 目录规划](#3-目录规划)
- [4. 在联网机器准备离线源码](#4-在联网机器准备离线源码)
- [5. 统一环境变量](#5-统一环境变量)
- [6. 检查编译器与 MPI](#6-检查编译器与-mpi)
- [7. 安装 OpenBLAS](#7-安装-openblas)
- [8. 检查 METIS 与 GKlib](#8-检查-metis-与-gklib)
- [9. 安装 ScaLAPACK](#9-安装-scalapack)
- [10. 可选：先安装不含 SLATE 的 STRUMPACK](#10-可选先安装不含-slate-的-strumpack)
- [11. 安装 SLATE](#11-安装-slate)
- [12. 安装启用 SLATE 的 STRUMPACK](#12-安装启用-slate-的-strumpack)
- [13. 测试项目 CMake 配置](#13-测试项目-cmake-配置)
- [14. 使用 MPI_THREAD_MULTIPLE 初始化 MPI](#14-使用-mpi_thread_multiple-初始化-mpi)
- [15. 运行与验收](#15-运行与验收)
- [16. 使用 Nsight Systems 验证 GPU](#16-使用-nsight-systems-验证-gpu)
- [17. 运行环境脚本](#17-运行环境脚本)
- [18. 参考资料](#18-参考资料)

---

## 1. 已验证环境

| 项目 | 配置 |
|---|---|
| 权限 | 普通用户，无 sudo |
| 网络 | 服务器无法联网，可上传压缩包 |
| GPU | NVIDIA Tesla V100-SXM2-16GB |
| GPU 架构 | `sm_70` |
| CUDA | 12.6 |
| CUDA 路径 | `/usr/local/cuda-12.6` |
| GCC/G++/GFortran | 11.2.0 |
| MPI | OpenMPI |
| OpenBLAS | 0.3.34，包含完整 LAPACK |
| ScaLAPACK | 2.2.3 |
| SLATE | 2025.05.28 |
| STRUMPACK | 8.0.0 |

本文使用 LP64 整数接口：

```text
STRUMPACK_USE_BLAS64=OFF
```

如果使用 ILP64，OpenBLAS、ScaLAPACK、SLATE 和 STRUMPACK 必须保持一致。

---

## 2. 软件栈与安装顺序

```text
GCC / G++ / GFortran
        │
        ├── OpenMPI
        │      └── ScaLAPACK
        ├── OpenBLAS（包含完整 LAPACK）
        ├── GKlib + METIS
        └── CUDA
               └── SLATE
                      └── STRUMPACK
```

推荐分两阶段验证：

1. 先编译 `MPI + OpenMP + CUDA + ScaLAPACK + METIS` 的 STRUMPACK；
2. 基础求解正确后，再安装 SLATE 并重新编译完整 GPU 版本。

---

## 3. 目录规划

```text
$HOME/soft/
├── src/
│   ├── OpenBLAS-0.3.34/
│   ├── scalapack-2.2.3/
│   ├── slate-2025.05.28/
│   └── STRUMPACK-8.0.0/
├── openblas-install/
├── gklib-install/
├── metis-install/
├── scalapack-install/
├── slate-install/
├── strumpack-install/
└── strumpack-slate-install/
```

创建目录：

```bash
mkdir -p "$HOME/packages"
mkdir -p "$HOME/soft/src"
```

---

## 4. 在联网机器准备离线源码

### 4.1 普通源码包

准备以下文件：

```text
OpenBLAS-0.3.34.tar.gz
scalapack-v2.2.3.tar.gz
STRUMPACK-v8.0.0.tar.gz
```

如果 METIS 和 GKlib 尚未安装，也需要准备相应源码。

### 4.2 SLATE 必须包含 Git 子模块

不要使用 GitHub 页面自动生成的 `Source code (zip)` 或 `Source code (tar.gz)`；这些归档通常不包含子模块。

在联网 Linux 或 WSL 中执行：

```bash
mkdir -p "$HOME/offline_packages"
cd "$HOME/offline_packages"

git clone \
    --branch v2025.05.28 \
    --recursive \
    https://github.com/icl-utk-edu/slate.git \
    slate-2025.05.28
```

检查：

```bash
cd "$HOME/offline_packages/slate-2025.05.28"

git submodule status --recursive

for file in \
    blaspp/CMakeLists.txt \
    lapackpp/CMakeLists.txt \
    testsweeper/CMakeLists.txt
do
    if [ -f "$file" ]; then
        echo "OK: $file"
    else
        echo "MISSING: $file"
    fi
done
```

必须全部显示 `OK`。

打包：

```bash
cd "$HOME/offline_packages"
tar -czf slate-2025.05.28-complete.tar.gz slate-2025.05.28
```

### 4.3 生成校验文件

```bash
sha256sum \
    OpenBLAS-0.3.34.tar.gz \
    scalapack-v2.2.3.tar.gz \
    STRUMPACK-v8.0.0.tar.gz \
    slate-2025.05.28-complete.tar.gz \
    > SHA256SUMS
```

将压缩包和 `SHA256SUMS` 上传到：

```text
$HOME/packages/
```

服务器校验：

```bash
cd "$HOME/packages"
sha256sum -c SHA256SUMS
```

解压：

```bash
cd "$HOME/soft/src"

tar -xzf "$HOME/packages/OpenBLAS-0.3.34.tar.gz"
tar -xzf "$HOME/packages/scalapack-v2.2.3.tar.gz"
tar -xzf "$HOME/packages/STRUMPACK-v8.0.0.tar.gz"
tar -xzf "$HOME/packages/slate-2025.05.28-complete.tar.gz"
```

---

## 5. 统一环境变量

创建：

```bash
cat > "$HOME/soft/strumpack_build_env.sh" <<'ENVEOF'
export GCC_ROOT=$HOME/opt/gcc-11.2.0-full

export CUDA_HOME=/usr/local/cuda-12.6
export CUDAToolkit_ROOT=$CUDA_HOME

export MPICC=/usr/bin/mpicc
export MPICXX=/usr/bin/mpicxx
export MPIFC=/usr/bin/mpifort
export MPIRUN=/usr/bin/mpirun

export OPENBLAS_ROOT=$HOME/soft/openblas-install
export GKLIB_ROOT=$HOME/soft/gklib-install
export METIS_ROOT=$HOME/soft/metis-install
export SCALAPACK_ROOT=$HOME/soft/scalapack-install
export SLATE_ROOT=$HOME/soft/slate-install
export SLATE_DIR=$SLATE_ROOT

export STRUMPACK_ROOT=$HOME/soft/strumpack-install
export STRUMPACK_SLATE_ROOT=$HOME/soft/strumpack-slate-install

export OPENBLAS_SRC=$HOME/soft/src/OpenBLAS-0.3.34
export SCALAPACK_SRC=$HOME/soft/src/scalapack-2.2.3
export SLATE_SRC=$HOME/soft/src/slate-2025.05.28
export STRUMPACK_SRC=$HOME/soft/src/STRUMPACK-8.0.0

export PATH=$GCC_ROOT/bin:$CUDA_HOME/bin:$PATH

export LD_LIBRARY_PATH=\
$STRUMPACK_SLATE_ROOT/lib:\
$STRUMPACK_SLATE_ROOT/lib64:\
$SLATE_ROOT/lib:\
$SLATE_ROOT/lib64:\
$STRUMPACK_ROOT/lib:\
$SCALAPACK_ROOT/lib:\
$OPENBLAS_ROOT/lib:\
$METIS_ROOT/lib:\
$GKLIB_ROOT/lib:\
$CUDA_HOME/lib64:\
${LD_LIBRARY_PATH}

export CMAKE_PREFIX_PATH=\
$STRUMPACK_SLATE_ROOT:\
$SLATE_ROOT:\
$STRUMPACK_ROOT:\
$SCALAPACK_ROOT:\
$OPENBLAS_ROOT:\
$METIS_ROOT:\
${CMAKE_PREFIX_PATH}
ENVEOF
```

加载：

```bash
source "$HOME/soft/strumpack_build_env.sh"
```

> 将 GCC 放到 `PATH` 最前面后，系统 `mpifort` 底层调用的 `gfortran` 可能改变，这会导致 `mpi.mod` 不兼容。

---

## 6. 检查编译器与 MPI

```bash
which gcc g++ gfortran
which mpicc mpicxx mpifort mpirun
which nvcc cmake

gcc --version
g++ --version
gfortran --version
mpirun --version
nvcc --version
cmake --version
```

确认 MPI 包装器来自同一套 MPI：

```bash
readlink -f "$(which mpicc)"
readlink -f "$(which mpicxx)"
readlink -f "$(which mpifort)"
readlink -f "$(which mpirun)"
```

OpenMPI 查看底层编译器：

```bash
mpicc --showme:command
mpicxx --showme:command
mpifort --showme:command
```

### 6.1 MPI C++ 测试

```bash
cat > /tmp/test_mpi.cpp <<'CPPEOF'
#include <mpi.h>
#include <iostream>

int main(int argc, char** argv)
{
    MPI_Init(&argc, &argv);
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    std::cout << "Hello from rank " << rank << std::endl;
    MPI_Finalize();
    return 0;
}
CPPEOF

"$MPICXX" /tmp/test_mpi.cpp -o /tmp/test_mpi_cpp
"$MPIRUN" -np 2 /tmp/test_mpi_cpp
```

### 6.2 MPI Fortran 测试

```bash
cat > /tmp/test_mpi.f90 <<'FEOF'
program test_mpi
  use mpi
  implicit none
  integer :: ierr, rank

  call MPI_Init(ierr)
  call MPI_Comm_rank(MPI_COMM_WORLD, rank, ierr)
  print *, "Hello from rank", rank
  call MPI_Finalize(ierr)
end program test_mpi
FEOF

"$MPIFC" /tmp/test_mpi.f90 -o /tmp/test_mpi_fortran
"$MPIRUN" -np 2 /tmp/test_mpi_fortran
```

只有两个测试都通过后，才继续安装 ScaLAPACK。

---

## 7. 安装 OpenBLAS

STRUMPACK、ScaLAPACK 和 SLATE 使用同一套 BLAS/LAPACK。这里编译包含完整 LAPACK 的 OpenBLAS。

### 7.1 确认源码

```bash
find "$OPENBLAS_SRC/lapack-netlib/SRC" \
    \( -name 'sorhr_col.f' \
    -o -name 'dorhr_col.f' \
    -o -name 'cunhr_col.f' \
    -o -name 'zunhr_col.f' \)
```

### 7.2 编译安装

```bash
rm -rf "$OPENBLAS_ROOT"
mkdir -p "$OPENBLAS_ROOT"

cd "$OPENBLAS_SRC"
make clean 2>/dev/null || true

make -j4 \
    CC="$GCC_ROOT/bin/gcc" \
    FC="$GCC_ROOT/bin/gfortran" \
    HOSTCC="$GCC_ROOT/bin/gcc" \
    BINARY=64 \
    DYNAMIC_ARCH=1 \
    USE_OPENMP=1 \
    NO_LAPACK=0 \
    NO_LAPACKE=0

make PREFIX="$OPENBLAS_ROOT" install
```

`NO_LAPACK` 和 `NO_LAPACKE` 保持为 `0`，确保安装完整 LAPACK。

### 7.3 验证接口

```bash
ls -l "$OPENBLAS_ROOT/lib/libopenblas.so"

nm -D --defined-only "$OPENBLAS_ROOT/lib/libopenblas.so" \
    | grep -E ' (dgemm_|dgesv_|dgetrf_|dpotrf_)$'

nm -D --defined-only "$OPENBLAS_ROOT/lib/libopenblas.so" \
    | grep -E ' (sorhr_col_|dorhr_col_|cunhr_col_|zunhr_col_)$'
```

第二条检查应包含常用 BLAS/LAPACK 接口，第三条检查应输出四个 `orhr_col` 接口。

---

## 8. 检查 METIS 与 GKlib

本文使用以下安装目录：

```text
$HOME/soft/metis-install
$HOME/soft/gklib-install
```

检查头文件、库文件和动态依赖：

```bash
ls -l "$METIS_ROOT/include/metis.h"
ls -l "$METIS_ROOT/lib/libmetis"*
ls -l "$GKLIB_ROOT/lib/libGKlib"*

export LD_LIBRARY_PATH=\
"$GKLIB_ROOT/lib:$METIS_ROOT/lib:$LD_LIBRARY_PATH"

ldd "$METIS_ROOT/lib/libmetis.so"
ldd "$METIS_ROOT/lib/libmetis.so" | grep 'not found'
```

最后一条命令应无输出。

---

## 9. 安装 ScaLAPACK

### 9.1 配置

```bash
rm -rf "$SCALAPACK_SRC/build"
rm -rf "$SCALAPACK_ROOT"
mkdir -p "$SCALAPACK_SRC/build" "$SCALAPACK_ROOT"

cmake \
    -S "$SCALAPACK_SRC" \
    -B "$SCALAPACK_SRC/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$SCALAPACK_ROOT" \
    -DCMAKE_C_COMPILER="$MPICC" \
    -DCMAKE_Fortran_COMPILER="$MPIFC" \
    -DBLAS_LIBRARIES="$OPENBLAS_ROOT/lib/libopenblas.so" \
    -DLAPACK_LIBRARIES="$OPENBLAS_ROOT/lib/libopenblas.so" \
    -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_BUILD_RPATH="$OPENBLAS_ROOT/lib" \
    -DCMAKE_INSTALL_RPATH="$OPENBLAS_ROOT/lib" \
    -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON \
    2>&1 | tee "$SCALAPACK_SRC/build/configure.log"
```

### 9.2 编译安装

```bash
cmake --build "$SCALAPACK_SRC/build" -j4
cmake --install "$SCALAPACK_SRC/build"
```

### 9.3 验证

```bash
find "$SCALAPACK_ROOT" -name 'libscalapack.so*'

nm -D --defined-only \
    "$SCALAPACK_ROOT/lib/libscalapack.so" \
    | grep ' pdgemm_$'

ldd "$SCALAPACK_ROOT/lib/libscalapack.so" | grep 'not found'
```

最后一条应无输出。

---

## 10. 可选：先安装不含 SLATE 的 STRUMPACK

该阶段用于验证 MPI、Fortran ABI、OpenBLAS、ScaLAPACK、METIS 和 CUDA。此时运行可能提示：

```text
WARNING: SLATE is required for full GPU support.
```

这是预期现象。

### 10.1 配置

```bash
rm -rf "$STRUMPACK_SRC/build-cuda"
rm -rf "$STRUMPACK_ROOT"
mkdir -p "$STRUMPACK_SRC/build-cuda" "$STRUMPACK_ROOT"

export CUDA_HOST_CXX="$GCC_ROOT/bin/g++"

cmake \
    -S "$STRUMPACK_SRC" \
    -B "$STRUMPACK_SRC/build-cuda" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$STRUMPACK_ROOT" \
    -DCMAKE_C_COMPILER="$MPICC" \
    -DCMAKE_CXX_COMPILER="$MPICXX" \
    -DCMAKE_Fortran_COMPILER="$MPIFC" \
    -DCMAKE_CUDA_COMPILER="$CUDA_HOME/bin/nvcc" \
    -DCMAKE_CUDA_HOST_COMPILER="$CUDA_HOST_CXX" \
    -DCMAKE_CUDA_ARCHITECTURES=70 \
    -DCUDAToolkit_ROOT="$CUDA_HOME" \
    -DSTRUMPACK_USE_MPI=ON \
    -DSTRUMPACK_USE_OPENMP=ON \
    -DSTRUMPACK_USE_CUDA=ON \
    -DSTRUMPACK_USE_BLAS64=OFF \
    -DTPL_ENABLE_SLATE=OFF \
    -DTPL_ENABLE_PARMETIS=OFF \
    -DTPL_ENABLE_SCOTCH=OFF \
    -DTPL_ENABLE_PTSCOTCH=OFF \
    -DTPL_ENABLE_BPACK=OFF \
    -DTPL_ENABLE_ZFP=OFF \
    -DTPL_BLAS_LIBRARIES="$OPENBLAS_ROOT/lib/libopenblas.so" \
    -DTPL_LAPACK_LIBRARIES="$OPENBLAS_ROOT/lib/libopenblas.so" \
    -DTPL_SCALAPACK_LIBRARIES="$SCALAPACK_ROOT/lib/libscalapack.so;$OPENBLAS_ROOT/lib/libopenblas.so" \
    -DTPL_METIS_INCLUDE_DIRS="$METIS_ROOT/include" \
    -DTPL_METIS_LIBRARIES="$METIS_ROOT/lib/libmetis.so;$GKLIB_ROOT/lib/libGKlib.so" \
    -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_BUILD_RPATH="$SCALAPACK_ROOT/lib;$OPENBLAS_ROOT/lib;$METIS_ROOT/lib;$GKLIB_ROOT/lib;$CUDA_HOME/lib64" \
    -DCMAKE_INSTALL_RPATH="$SCALAPACK_ROOT/lib;$OPENBLAS_ROOT/lib;$METIS_ROOT/lib;$GKLIB_ROOT/lib;$CUDA_HOME/lib64" \
    -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON \
    2>&1 | tee "$STRUMPACK_SRC/build-cuda/configure.log"
```

> CMake 列表中的分号必须放在引号内。

### 10.2 编译安装

```bash
cmake --build "$STRUMPACK_SRC/build-cuda" -j4
cmake --install "$STRUMPACK_SRC/build-cuda"
```

### 10.3 验证

```bash
grep -E \
'STRUMPACK_USE_CUDA|CMAKE_CUDA_ARCHITECTURES|TPL_ENABLE_SLATE' \
"$STRUMPACK_SRC/build-cuda/CMakeCache.txt"

find "$STRUMPACK_ROOT" -name 'libstrumpack.so*'
ldd "$STRUMPACK_ROOT/lib/libstrumpack.so" \
    | grep -E 'mpi|openblas|scalapack|metis|GKlib|cuda|cublas|cusolver'
```

---

## 11. 安装 SLATE

SLATE 依赖 MPI、OpenMP、C++17、Fortran、BLAS/LAPACK、CUDA，以及源码子模块 BLAS++、LAPACK++ 和 TestSweeper。

### 11.1 检查源码完整性

```bash
for file in \
    "$SLATE_SRC/blaspp/CMakeLists.txt" \
    "$SLATE_SRC/lapackpp/CMakeLists.txt" \
    "$SLATE_SRC/testsweeper/CMakeLists.txt"
do
    if [ -f "$file" ]; then
        echo "OK: $file"
    else
        echo "MISSING: $file"
    fi
done
```

三个文件都应存在后再继续配置。

### 11.2 检查 MPI 线程支持

```bash
ompi_info | grep 'Thread support'
```

应包含：

```text
MPI_THREAD_MULTIPLE: yes
```

最小测试：

```bash
cat > /tmp/test_mpi_thread.cpp <<'CPPEOF'
#include <mpi.h>
#include <iostream>

int main(int argc, char** argv)
{
    int provided = MPI_THREAD_SINGLE;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (rank == 0) {
        std::cout << "Requested = " << MPI_THREAD_MULTIPLE << '\n';
        std::cout << "Provided  = " << provided << '\n';
    }
    MPI_Finalize();
    return provided >= MPI_THREAD_MULTIPLE ? 0 : 1;
}
CPPEOF

"$MPICXX" -std=c++17 /tmp/test_mpi_thread.cpp -o /tmp/test_mpi_thread
"$MPIRUN" -np 1 /tmp/test_mpi_thread
```

正常结果为 `Provided = 3`。

### 11.3 配置 SLATE

```bash
rm -rf "$SLATE_SRC/build"
rm -rf "$SLATE_ROOT"
mkdir -p "$SLATE_SRC/build" "$SLATE_ROOT"

export CUDA_HOST_CXX="$GCC_ROOT/bin/g++"

cmake \
    -S "$SLATE_SRC" \
    -B "$SLATE_SRC/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$SLATE_ROOT" \
    -DCMAKE_CXX_COMPILER="$MPICXX" \
    -DCMAKE_Fortran_COMPILER="$GCC_ROOT/bin/gfortran" \
    -DCMAKE_CUDA_COMPILER="$CUDA_HOME/bin/nvcc" \
    -DCMAKE_CUDA_HOST_COMPILER="$CUDA_HOST_CXX" \
    -DCMAKE_CUDA_ARCHITECTURES=70 \
    -Dgpu_backend=cuda \
    -Dblas=openblas \
    -DBLAS_LIBRARIES="$OPENBLAS_ROOT/lib/libopenblas.so" \
    -DLAPACK_LIBRARIES="$OPENBLAS_ROOT/lib/libopenblas.so" \
    -DSCALAPACK_LIBRARIES=none \
    -Dbuild_tests=no \
    -Dc_api=no \
    -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_BUILD_RPATH="$OPENBLAS_ROOT/lib;$CUDA_HOME/lib64" \
    -DCMAKE_INSTALL_RPATH="$OPENBLAS_ROOT/lib;$CUDA_HOME/lib64" \
    -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON \
    2>&1 | tee "$SLATE_SRC/build/configure.log"
```

说明：

- `gpu_backend=cuda`：启用 CUDA；
- `CMAKE_CUDA_ARCHITECTURES=70`：V100；
- `build_tests=no`：关闭大型对照测试；
- `SCALAPACK_LIBRARIES=none`：关闭测试后无需 ScaLAPACK 对照库；
- `c_api=no`：不构建不需要的 C API。

检查：

```bash
tail -n 30 "$SLATE_SRC/build/configure.log"

grep -E \
'CMAKE_CXX_COMPILER:|CMAKE_Fortran_COMPILER:|CMAKE_CUDA_COMPILER:|CMAKE_CUDA_HOST_COMPILER:|CMAKE_CUDA_ARCHITECTURES:|gpu_backend:|build_tests:' \
"$SLATE_SRC/build/CMakeCache.txt"
```

### 11.4 编译安装

```bash
cmake --build "$SLATE_SRC/build" -j4 \
    2>&1 | tee "$SLATE_SRC/build/build.log"

cmake --install "$SLATE_SRC/build" \
    2>&1 | tee "$SLATE_SRC/build/install.log"
```

### 11.5 验证

```bash
find "$SLATE_ROOT" \
    \( -name 'libslate.so*' \
    -o -name 'libblaspp.so*' \
    -o -name 'liblapackpp.so*' \
    -o -iname '*slate*config.cmake' \) \
    -print

SLATE_LIB=$(find "$SLATE_ROOT" -name 'libslate.so' | head -n 1)
ldd "$SLATE_LIB"
ldd "$SLATE_LIB" | grep 'not found'
```

最后一条应无输出。

---

## 12. 安装启用 SLATE 的 STRUMPACK

不要覆盖已经工作的基础版，使用新目录：

```text
$HOME/soft/strumpack-slate-install
```

### 12.1 配置

```bash
rm -rf "$STRUMPACK_SRC/build-slate"
rm -rf "$STRUMPACK_SLATE_ROOT"
mkdir -p "$STRUMPACK_SRC/build-slate" "$STRUMPACK_SLATE_ROOT"

export SLATE_DIR="$SLATE_ROOT"
export CUDA_HOST_CXX="$GCC_ROOT/bin/g++"

cmake \
    -S "$STRUMPACK_SRC" \
    -B "$STRUMPACK_SRC/build-slate" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$STRUMPACK_SLATE_ROOT" \
    -DCMAKE_C_COMPILER="$MPICC" \
    -DCMAKE_CXX_COMPILER="$MPICXX" \
    -DCMAKE_Fortran_COMPILER="$MPIFC" \
    -DCMAKE_CUDA_COMPILER="$CUDA_HOME/bin/nvcc" \
    -DCMAKE_CUDA_HOST_COMPILER="$CUDA_HOST_CXX" \
    -DCMAKE_CUDA_ARCHITECTURES=70 \
    -DCUDAToolkit_ROOT="$CUDA_HOME" \
    -DSTRUMPACK_USE_MPI=ON \
    -DSTRUMPACK_USE_OPENMP=ON \
    -DSTRUMPACK_USE_CUDA=ON \
    -DSTRUMPACK_USE_BLAS64=OFF \
    -DTPL_ENABLE_SLATE=ON \
    -DSLATE_DIR="$SLATE_ROOT" \
    -DTPL_ENABLE_PARMETIS=OFF \
    -DTPL_ENABLE_SCOTCH=OFF \
    -DTPL_ENABLE_PTSCOTCH=OFF \
    -DTPL_ENABLE_BPACK=OFF \
    -DTPL_ENABLE_ZFP=OFF \
    -DTPL_BLAS_LIBRARIES="$OPENBLAS_ROOT/lib/libopenblas.so" \
    -DTPL_LAPACK_LIBRARIES="$OPENBLAS_ROOT/lib/libopenblas.so" \
    -DTPL_SCALAPACK_LIBRARIES="$SCALAPACK_ROOT/lib/libscalapack.so;$OPENBLAS_ROOT/lib/libopenblas.so" \
    -DTPL_METIS_INCLUDE_DIRS="$METIS_ROOT/include" \
    -DTPL_METIS_LIBRARIES="$METIS_ROOT/lib/libmetis.so;$GKLIB_ROOT/lib/libGKlib.so" \
    -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_PREFIX_PATH="$SLATE_ROOT;$SCALAPACK_ROOT;$METIS_ROOT;$OPENBLAS_ROOT" \
    -DCMAKE_BUILD_RPATH="$SLATE_ROOT/lib;$SLATE_ROOT/lib64;$SCALAPACK_ROOT/lib;$OPENBLAS_ROOT/lib;$METIS_ROOT/lib;$GKLIB_ROOT/lib;$CUDA_HOME/lib64" \
    -DCMAKE_INSTALL_RPATH="$SLATE_ROOT/lib;$SLATE_ROOT/lib64;$SCALAPACK_ROOT/lib;$OPENBLAS_ROOT/lib;$METIS_ROOT/lib;$GKLIB_ROOT/lib;$CUDA_HOME/lib64" \
    -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON \
    2>&1 | tee "$STRUMPACK_SRC/build-slate/configure.log"
```

### 12.2 确认找到 SLATE

```bash
grep -Ei \
'Found SLATE|SLATE was not found|CUDA|ScaLAPACK|MPI|OpenMP' \
"$STRUMPACK_SRC/build-slate/configure.log"

grep -E \
'TPL_ENABLE_SLATE|STRUMPACK_USE_SLATE|STRUMPACK_USE_CUDA|CMAKE_CUDA_ARCHITECTURES' \
"$STRUMPACK_SRC/build-slate/CMakeCache.txt"
```

必须看到：

```text
Found SLATE
```

不能出现：

```text
SLATE was not found
```

### 12.3 编译安装

```bash
cmake --build "$STRUMPACK_SRC/build-slate" -j4
cmake --install "$STRUMPACK_SRC/build-slate"
```

### 12.4 验证动态库

```bash
STRUMPACK_LIB=$(find "$STRUMPACK_SLATE_ROOT" \
    -name 'libstrumpack.so' | head -n 1)

echo "$STRUMPACK_LIB"

ldd "$STRUMPACK_LIB" \
    | grep -E 'slate|blaspp|lapackpp|cuda|cublas|cusolver|mpi|openblas'

ldd "$STRUMPACK_LIB" | grep 'not found'
```

应能看到 `libslate.so`、`liblapackpp.so`、`libblaspp.so`、`libopenblas.so` 和 CUDA 运行库。

---

## 13. 测试项目 CMake 配置

测试程序显式链接 OpenBLAS，使 STRUMPACK、SLATE 和 LAPACK++ 使用同一套数值库。

```cmake
cmake_minimum_required(VERSION 3.21)
project(test_strumpack LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(MPI REQUIRED)
find_package(STRUMPACK REQUIRED)

set(OPENBLAS_ROOT
    "$ENV{OPENBLAS_ROOT}"
    CACHE PATH
    "OpenBLAS installation prefix"
)

if(NOT EXISTS "${OPENBLAS_ROOT}/lib/libopenblas.so")
    message(FATAL_ERROR
        "OpenBLAS not found: "
        "${OPENBLAS_ROOT}/lib/libopenblas.so"
    )
endif()

add_executable(test_strumpack
    test_strumpack.cpp
)

target_link_libraries(test_strumpack
    PRIVATE
    STRUMPACK::strumpack
    MPI::MPI_CXX
    "${OPENBLAS_ROOT}/lib/libopenblas.so"
)
```

找到 STRUMPACK 配置目录：

```bash
find "$STRUMPACK_SLATE_ROOT" \
    -iname '*strumpack*config*.cmake'

export STRUMPACK_DIR=$(dirname "$(
    find "$STRUMPACK_SLATE_ROOT" \
        -iname '*strumpack*config*.cmake' \
        | head -n 1
)")

echo "$STRUMPACK_DIR"
```

重新生成测试项目：

```bash
cd "$HOME/code/demo02"
rm -rf build

cmake \
    -S . \
    -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$MPICC" \
    -DCMAKE_CXX_COMPILER="$MPICXX" \
    -DSTRUMPACK_DIR="$STRUMPACK_DIR" \
    -DOPENBLAS_ROOT="$OPENBLAS_ROOT" \
    -DCMAKE_PREFIX_PATH="$STRUMPACK_SLATE_ROOT;$SLATE_ROOT;$SCALAPACK_ROOT;$METIS_ROOT;$OPENBLAS_ROOT"

cmake --build build --verbose -j1
```

检查：

```bash
cd "$HOME/code/demo02/build"

ldd ./test_strumpack \
    | grep -E 'strumpack|slate|lapackpp|blaspp|openblas|mpi|cuda'

ldd -r ./test_strumpack 2>&1 \
    | grep -E 'undefined symbol|not found'
```

第二条命令应无输出，并确认 `libstrumpack.so` 来自 `strumpack-slate-install`。

---

## 14. 使用 `MPI_THREAD_MULTIPLE` 初始化 MPI

SLATE 需要 `MPI_THREAD_MULTIPLE`。在创建任何 STRUMPACK 对象之前使用 `MPI_Init_thread`：

```cpp
#include <mpi.h>
#include <cstdlib>
#include <exception>
#include <iostream>

int main(int argc, char* argv[])
{
    int provided = MPI_THREAD_SINGLE;

    const int mpi_status = MPI_Init_thread(
        &argc,
        &argv,
        MPI_THREAD_MULTIPLE,
        &provided
    );

    if (mpi_status != MPI_SUCCESS) {
        std::cerr << "MPI_Init_thread failed." << std::endl;
        return EXIT_FAILURE;
    }

    int rank = 0;
    int size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (rank == 0) {
        std::cout
            << "MPI thread support requested: "
            << MPI_THREAD_MULTIPLE << '\n'
            << "MPI thread support provided : "
            << provided << std::endl;
    }

    if (provided < MPI_THREAD_MULTIPLE) {
        if (rank == 0) {
            std::cerr
                << "SLATE requires MPI_THREAD_MULTIPLE."
                << std::endl;
        }
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    int return_code = EXIT_SUCCESS;

    try {
        /*
         * 在此处创建 STRUMPACK 对象，执行矩阵构造、
         * reorder、factor、solve 和误差检查。
         * 所有 STRUMPACK 对象应在 MPI_Finalize 前析构。
         */
    }
    catch (const std::exception& error) {
        std::cerr
            << "Rank " << rank
            << " exception: " << error.what()
            << std::endl;
        return_code = EXIT_FAILURE;
    }
    catch (...) {
        std::cerr
            << "Rank " << rank
            << " unknown exception."
            << std::endl;
        return_code = EXIT_FAILURE;
    }

    MPI_Finalize();
    return return_code;
}
```

线程级别通常为：

```text
0 = MPI_THREAD_SINGLE
1 = MPI_THREAD_FUNNELED
2 = MPI_THREAD_SERIALIZED
3 = MPI_THREAD_MULTIPLE
```

OpenMPI 可用下面的环境变量进行临时验证：

```bash
export OMPI_MPI_THREAD_LEVEL=3
```

正式代码仍应调用 `MPI_Init_thread` 并检查 `provided`。

---

## 15. 运行与验收

```bash
source "$HOME/soft/strumpack_build_env.sh"

export CUDA_VISIBLE_DEVICES=0
export OMP_NUM_THREADS=1
export OPENBLAS_NUM_THREADS=1

cd "$HOME/code/demo02/build"

"$MPIRUN" -np 1 \
    ./test_strumpack 500 \
    --sp_enable_gpu \
    --sp_gpu_streams 4
```

成功标准：

1. 不再出现 `SLATE is required for full GPU support`；
2. 不再出现 `MPI_THREAD_MULTIPLE is required for SLATE`；
3. 打印 `MPI thread support provided : 3`；
4. 残差和误差正常；
5. 最后出现 `STRUMPACK TEST PASSED`。

比较不同线程数：

```bash
for threads in 1 2 4 8; do
    echo "OMP_NUM_THREADS=$threads"
    OMP_NUM_THREADS=$threads \
    OPENBLAS_NUM_THREADS=1 \
    "$MPIRUN" -np 1 \
        ./test_strumpack 500 \
        --sp_enable_gpu \
        --sp_gpu_streams 4
done
```

不要让 OpenMP 和 OpenBLAS 同时使用大量线程，以免线程过度订阅。

---

## 16. 使用 Nsight Systems 验证 GPU

使用 `nsys` 包裹 `mpirun`：

```bash
NSYS=$(readlink -f "$(which nsys)")

"$NSYS" profile \
    --trace=cuda,nvtx,osrt \
    --sample=none \
    --force-overwrite=true \
    -o strumpack_slate_gpu \
    "$MPIRUN" -np 1 \
    ./test_strumpack 500 \
        --sp_enable_gpu \
        --sp_gpu_streams 4
```

输出汇总：

```bash
nsys stats \
    --force-export=true \
    --report cuda_api_sum,cuda_gpu_kern_sum \
    strumpack_slate_gpu.nsys-rep
```

重点观察：

```text
cudaMalloc
cudaMemcpyAsync
cudaLaunchKernel
cublas*
cusolver*
```

实时监控：

```bash
watch -n 0.5 nvidia-smi
```

二维 Poisson 小规模测试会产生大量较小 frontal matrix，因此即使 SLATE 和 CUDA 已正确启用，也不保证小规模一定比 CPU 更快。

---

## 17. 运行环境脚本

```bash
cat > "$HOME/soft/strumpack_runtime_env.sh" <<'ENVEOF'
export CUDA_HOME=/usr/local/cuda-12.6

export OPENBLAS_ROOT=$HOME/soft/openblas-install
export GKLIB_ROOT=$HOME/soft/gklib-install
export METIS_ROOT=$HOME/soft/metis-install
export SCALAPACK_ROOT=$HOME/soft/scalapack-install
export SLATE_ROOT=$HOME/soft/slate-install
export STRUMPACK_ROOT=$HOME/soft/strumpack-slate-install

export MPICC=/usr/bin/mpicc
export MPICXX=/usr/bin/mpicxx
export MPIFC=/usr/bin/mpifort
export MPIRUN=/usr/bin/mpirun

export PATH=$CUDA_HOME/bin:$PATH

export LD_LIBRARY_PATH=\
$STRUMPACK_ROOT/lib:\
$STRUMPACK_ROOT/lib64:\
$SLATE_ROOT/lib:\
$SLATE_ROOT/lib64:\
$SCALAPACK_ROOT/lib:\
$OPENBLAS_ROOT/lib:\
$METIS_ROOT/lib:\
$GKLIB_ROOT/lib:\
$CUDA_HOME/lib64:\
${LD_LIBRARY_PATH}

export CMAKE_PREFIX_PATH=\
$STRUMPACK_ROOT:\
$SLATE_ROOT:\
$SCALAPACK_ROOT:\
$OPENBLAS_ROOT:\
$METIS_ROOT:\
${CMAKE_PREFIX_PATH}

export CUDA_VISIBLE_DEVICES=0
export OMP_NUM_THREADS=1
export OPENBLAS_NUM_THREADS=1
ENVEOF
```

加载：

```bash
source "$HOME/soft/strumpack_runtime_env.sh"
```

---

## 18. 参考资料

- [STRUMPACK GitHub](https://github.com/pghysels/STRUMPACK)
- [STRUMPACK 8.0.0 CMake 配置](https://github.com/pghysels/STRUMPACK/blob/v8.0.0/CMakeLists.txt)
- [STRUMPACK GPU Support](https://portal.nersc.gov/project/sparse/strumpack/master/GPU_Support.html)
- [SLATE GitHub](https://github.com/icl-utk-edu/slate)
- [SLATE Installation](https://github.com/icl-utk-edu/slate/blob/master/INSTALL.md)
- [OpenMPI MPI_Init_thread](https://docs.open-mpi.org/en/main/man-openmpi/man3/MPI_Init_thread.3.html)
- [OpenBLAS GitHub](https://github.com/OpenMathLib/OpenBLAS)
- [ScaLAPACK GitHub](https://github.com/Reference-ScaLAPACK/scalapack)

---

## 最终检查清单

```text
[ ] MPI C++ 测试通过
[ ] MPI Fortran 测试通过
[ ] OpenBLAS 包含完整 LAPACK
[ ] OpenBLAS 包含 ?orhr_col_ 四个符号
[ ] ScaLAPACK 包含 pdgemm_
[ ] SLATE 源码包含全部 Git 子模块
[ ] SLATE 使用 gpu_backend=cuda
[ ] SLATE CUDA 架构为 70
[ ] STRUMPACK 配置日志显示 Found SLATE
[ ] libstrumpack.so 链接 libslate.so
[ ] 测试程序链接新版 STRUMPACK
[ ] 测试程序使用 MPI_Init_thread
[ ] MPI provided 等于 MPI_THREAD_MULTIPLE
[ ] STRUMPACK TEST PASSED
[ ] Nsight Systems 中存在 CUDA API 和 CUDA kernel
```

## 最终检查清单

```text
[ ] MPI C++ 测试通过
[ ] MPI Fortran 测试通过
[ ] OpenBLAS 包含完整 LAPACK
[ ] OpenBLAS 包含 ?orhr_col_ 四个符号
[ ] ScaLAPACK 包含 pdgemm_
[ ] SLATE 源码包含全部 Git 子模块
[ ] SLATE 使用 gpu_backend=cuda
[ ] SLATE CUDA 架构为 70
[ ] STRUMPACK 配置日志显示 Found SLATE
[ ] libstrumpack.so 链接 libslate.so
[ ] 测试程序链接新版 STRUMPACK
[ ] 测试程序使用 MPI_Init_thread
[ ] MPI provided 等于 MPI_THREAD_MULTIPLE
[ ] STRUMPACK TEST PASSED
[ ] Nsight Systems 中存在 CUDA API 和 CUDA kernel
```
