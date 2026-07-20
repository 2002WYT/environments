# SuperLU_DIST 9.2.1 GPU 版本安装教程

本文记录在 Linux 服务器上，从源码安装以下依赖并最终编译 **SuperLU_DIST 9.2.1 GPU 版本**的完整流程：

- OpenBLAS
- GKlib
- METIS
- ParMETIS
- SuperLU_DIST
- MPI
- CUDA
- OpenMP

本教程对应的测试环境包含：

- NVIDIA Tesla V100
- CUDA 12.6
- GPU 架构 `sm_70`
- GCC/G++ 11.2.0
- MPI 编译器包装器 `mpicc`、`mpicxx`
- 64 位索引
- Shared Library
- OpenMP
- CUDA 加速
- ParMETIS 重排序

> [!IMPORTANT]
> 本文中的 `/home/wangyitong/soft/...` 是作者服务器上的绝对路径。
>
> **其他用户必须根据自己的用户名、软件目录、CUDA、MPI 和编译器位置进行修改。**
>
> 最推荐的做法不是在每条命令中手动修改路径，而是在开始时统一设置环境变量。

---

## 1. 最终安装目录结构

本文假设所有软件源码和安装目录都位于：

```text
/home/wangyitong/soft
```

最终目录大致如下：

```text
/home/wangyitong/soft/
├── OpenBLAS/
├── GKlib/
├── metis/
├── parmetis/
├── superlu_dist-9.2.1/
│
├── openblas-install/
├── gklib-install/
├── metis-install/
├── parmetis-install/
└── superlu_dist-install/
```

其中：

- 不带 `-install` 的目录是源码目录。
- 带 `-install` 的目录是最终安装目录。
- 源码目录名称可能因下载方式不同而不同，请按实际目录修改。

---

## 2. 统一设置路径变量

先设置软件根目录：

```bash
export SOFTWARE_ROOT=/home/wangyitong/soft
```

设置各软件源码目录：

```bash
export OPENBLAS_SRC="$SOFTWARE_ROOT/OpenBLAS"
export GKLIB_SRC="$SOFTWARE_ROOT/GKlib"
export METIS_SRC="$SOFTWARE_ROOT/metis"
export PARMETIS_SRC="$SOFTWARE_ROOT/parmetis"
export SUPERLU_DIST_SRC="$SOFTWARE_ROOT/superlu_dist-9.2.1"
```

设置各软件安装目录：

```bash
export OPENBLAS_ROOT="$SOFTWARE_ROOT/openblas-install"
export GKLIB_ROOT="$SOFTWARE_ROOT/gklib-install"
export METIS_ROOT="$SOFTWARE_ROOT/metis-install"
export PARMETIS_ROOT="$SOFTWARE_ROOT/parmetis-install"
export SUPERLU_DIST_ROOT="$SOFTWARE_ROOT/superlu_dist-install"
```

检查：

```bash
echo "$OPENBLAS_ROOT"
echo "$GKLIB_ROOT"
echo "$METIS_ROOT"
echo "$PARMETIS_ROOT"
echo "$SUPERLU_DIST_ROOT"
```

> [!IMPORTANT]
> 如果你的源码目录名称不是 `OpenBLAS`、`GKlib`、`metis`、`parmetis`，必须修改对应的 `*_SRC`。

---

## 3. 设置编译器与 CUDA 环境

下面只是示例。请根据自己服务器上的实际路径设置。

### 3.1 C、C++ 和 Fortran 编译器

```bash
export CC=/home/wangyitong/opt/gcc-11.2.0/bin/gcc
export CXX=/home/wangyitong/opt/gcc-11.2.0/bin/g++
export FC=/home/wangyitong/opt/gcc-11.2.0/bin/gfortran
```

验证：

```bash
"$CC" --version
"$CXX" --version
"$FC" --version
```

如果直接使用系统编译器，也可以写成：

```bash
export CC=gcc
export CXX=g++
export FC=gfortran
```

---

### 3.2 MPI 编译器

```bash
export MPICC=mpicc
export MPICXX=mpicxx
```

验证：

```bash
"$MPICC" --version
"$MPICXX" --version
```

检查 MPI 包装器实际调用的编译器：

```bash
mpicc -show
mpicxx -show
```

对于 OpenMPI，也可以使用：

```bash
mpicc --showme:command
mpicc --showme:compile
mpicc --showme:link
```

建议确保 MPI 包装器所使用的 GCC/G++ 与前面设置的 `$CC`、`$CXX` ABI 兼容。

---

### 3.3 CUDA

```bash
export CUDA_HOME=/usr/local/cuda-12.6
```

添加环境变量：

```bash
export PATH="$CUDA_HOME/bin:$PATH"
export LD_LIBRARY_PATH="$CUDA_HOME/lib64:${LD_LIBRARY_PATH}"
```

验证：

```bash
nvcc --version
nvidia-smi
```

本文使用 NVIDIA V100，因此：

```text
CMAKE_CUDA_ARCHITECTURES=70
```

常见 GPU 架构参考：

| GPU | Compute Capability | CMake 数值 |
|---|---:|---:|
| V100 | 7.0 | 70 |
| T4 | 7.5 | 75 |
| A100 | 8.0 | 80 |
| RTX 30 系列 | 8.6 | 86 |
| H100 | 9.0 | 90 |

必须根据自己的 GPU 修改：

```bash
-DCMAKE_CUDA_ARCHITECTURES=70
```

---

### 3.4 MPI 头文件参数

SuperLU_DIST 的部分 `.cu` 文件会通过 `nvcc` 编译，但源码中包含：

```cpp
#include <mpi.h>
```

因此需要把 MPI 头文件路径传给 CUDA 编译器。

对于 OpenMPI，可设置：

```bash
export MPI_COMPILE_FLAGS="$(mpicc --showme:compile)"
```

检查：

```bash
echo "$MPI_COMPILE_FLAGS"
```

输出中通常应该包含类似：

```text
-I/path/to/openmpi/include
```

如果当前 MPI 不支持 `--showme:compile`，可以先执行：

```bash
mpicc -show
```

然后手动提取其中的 MPI include 参数，例如：

```bash
export MPI_COMPILE_FLAGS="-I/path/to/mpi/include"
```

> [!WARNING]
> 如果 `MPI_COMPILE_FLAGS` 为空，CUDA 编译阶段可能出现：
>
> ```text
> fatal error: mpi.h: No such file or directory
> ```

---

## 4. 安装 OpenBLAS

进入 OpenBLAS 源码目录：

```bash
cd "$OPENBLAS_SRC"
```

建议先清理旧构建：

```bash
make clean
```

编译并安装：

```bash
make \
    CC="$CC" \
    FC="$FC" \
    HOSTCC="$CC" \
    BINARY=64 \
    DYNAMIC_ARCH=1 \
    USE_OPENMP=1 \
    NO_AFFINITY=1 \
    NUM_THREADS="$(nproc)" \
    PREFIX="$OPENBLAS_ROOT" \
    install
```

参数说明：

| 参数 | 作用 |
|---|---|
| `CC` | C 编译器 |
| `FC` | Fortran 编译器 |
| `HOSTCC` | 构建过程中使用的主机 C 编译器 |
| `BINARY=64` | 编译 64 位库 |
| `DYNAMIC_ARCH=1` | 支持运行时选择 CPU kernel |
| `USE_OPENMP=1` | 启用 OpenMP |
| `NO_AFFINITY=1` | 禁用 OpenBLAS 内部 CPU 绑定 |
| `NUM_THREADS` | OpenBLAS 支持的最大线程数 |
| `PREFIX` | 安装目录 |
| `install` | 编译完成后直接安装 |

安装后检查：

```bash
ls -lh "$OPENBLAS_ROOT/lib"
ls -lh "$OPENBLAS_ROOT/include"
```

应该至少看到：

```text
libopenblas.so
libopenblas.a
cblas.h
```

检查 BLAS 符号：

```bash
nm -D "$OPENBLAS_ROOT/lib/libopenblas.so" \
    | grep -E ' (dgemm_|dscal_|dtrsv_|zscal_)$'
```

如果正确，应当能找到：

```text
dgemm_
dscal_
dtrsv_
zscal_
```

---

## 5. 安装 GKlib

进入 GKlib 源码目录：

```bash
cd "$GKLIB_SRC"
```

清理旧构建：

```bash
rm -rf build
```

配置：

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$GKLIB_ROOT" \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG -march=native" \
    -DSHARED=ON
```

编译：

```bash
cmake --build build -j"$(nproc)"
```

安装：

```bash
cmake --install build
```

检查：

```bash
ls -lh "$GKLIB_ROOT/lib"
ls -lh "$GKLIB_ROOT/include"
```

重点确认库文件的大小写。

常见名称是：

```text
libGKlib.so
```

而不是：

```text
libgklib.so
```

Linux 文件名大小写敏感。

添加环境变量：

```bash
export LD_LIBRARY_PATH="$GKLIB_ROOT/lib:${LD_LIBRARY_PATH}"
export LIBRARY_PATH="$GKLIB_ROOT/lib:${LIBRARY_PATH}"
export CPATH="$GKLIB_ROOT/include:${CPATH}"
export CMAKE_PREFIX_PATH="$GKLIB_ROOT:${CMAKE_PREFIX_PATH}"
```

---

## 6. 安装 METIS

进入 METIS 源码目录：

```bash
cd "$METIS_SRC"
```

建议先清理旧配置：

```bash
make distclean 2>/dev/null || true
```

配置：

```bash
make config \
    shared=1 \
    cc="$CC" \
    prefix="$METIS_ROOT" \
    gklib_path="$GKLIB_ROOT" \
    i64=1
```

编译：

```bash
make -j"$(nproc)"
```

安装：

```bash
make install
```

参数说明：

| 参数 | 作用 |
|---|---|
| `shared=1` | 构建动态库 |
| `cc` | 指定 C 编译器 |
| `prefix` | METIS 安装目录 |
| `gklib_path` | 指向已安装的 GKlib |
| `i64=1` | 使用 64 位索引 |

检查：

```bash
ls -lh "$METIS_ROOT/lib"
ls -lh "$METIS_ROOT/include"
```

应该看到类似：

```text
libmetis.so
metis.h
```

添加环境变量：

```bash
export LD_LIBRARY_PATH="$METIS_ROOT/lib:${LD_LIBRARY_PATH}"
export LIBRARY_PATH="$METIS_ROOT/lib:${LIBRARY_PATH}"
export CPATH="$METIS_ROOT/include:${CPATH}"
export CMAKE_PREFIX_PATH="$METIS_ROOT:${CMAKE_PREFIX_PATH}"
```

---

## 7. 安装 ParMETIS

进入 ParMETIS 源码目录：

```bash
cd "$PARMETIS_SRC"
```

建议清理旧配置：

```bash
make distclean 2>/dev/null || true
```

配置：

```bash
make config \
    shared=1 \
    cc="$MPICC" \
    prefix="$PARMETIS_ROOT" \
    gklib_path="$GKLIB_ROOT" \
    metis_path="$METIS_ROOT"
```

编译：

```bash
make -j"$(nproc)"
```

安装：

```bash
make install
```

参数说明：

| 参数 | 作用 |
|---|---|
| `shared=1` | 构建动态库 |
| `cc="$MPICC"` | 使用 MPI C 编译器 |
| `prefix` | ParMETIS 安装目录 |
| `gklib_path` | GKlib 安装路径 |
| `metis_path` | METIS 安装路径 |

检查：

```bash
ls -lh "$PARMETIS_ROOT/lib"
ls -lh "$PARMETIS_ROOT/include"
```

应该看到类似：

```text
libparmetis.so
parmetis.h
```

添加环境变量：

```bash
export LD_LIBRARY_PATH="$PARMETIS_ROOT/lib:${LD_LIBRARY_PATH}"
export LIBRARY_PATH="$PARMETIS_ROOT/lib:${LIBRARY_PATH}"
export CPATH="$PARMETIS_ROOT/include:${CPATH}"
export CMAKE_PREFIX_PATH="$PARMETIS_ROOT:${CMAKE_PREFIX_PATH}"
```

---

## 8. 配置 SuperLU_DIST 9.2.1

进入 SuperLU_DIST 源码目录：

```bash
cd "$SUPERLU_DIST_SRC"
```

清理之前的 CMake 构建目录：

```bash
rm -rf build
```

> [!IMPORTANT]
> 修改依赖库路径、精度选项、CUDA 参数或 ParMETIS 参数后，应当删除整个 `build` 目录。
>
> 仅重新执行 `cmake` 可能继续使用旧的 `CMakeCache.txt`。

执行配置：

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$SUPERLU_DIST_ROOT" \
    \
    -DCMAKE_C_COMPILER="$MPICC" \
    -DCMAKE_CXX_COMPILER="$MPICXX" \
    -DCMAKE_CUDA_COMPILER="$CUDA_HOME/bin/nvcc" \
    -DCMAKE_CUDA_HOST_COMPILER="$CXX" \
    -DCMAKE_CUDA_FLAGS="$MPI_COMPILE_FLAGS" \
    \
    -DCUDAToolkit_ROOT="$CUDA_HOME" \
    -DCMAKE_CUDA_ARCHITECTURES=70 \
    \
    -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG -march=native" \
    -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -march=native" \
    \
    -DTPL_ENABLE_CUDALIB=ON \
    -Denable_openmp=ON \
    \
    -DTPL_ENABLE_INTERNAL_BLASLIB=OFF \
    -DTPL_BLAS_LIBRARIES="$OPENBLAS_ROOT/lib/libopenblas.so" \
    \
    -DTPL_ENABLE_LAPACKLIB=ON \
    -DTPL_LAPACK_LIBRARIES="$OPENBLAS_ROOT/lib/libopenblas.so" \
    \
    -DTPL_ENABLE_PARMETISLIB=ON \
    -DTPL_PARMETIS_INCLUDE_DIRS="$PARMETIS_ROOT/include;$METIS_ROOT/include;$GKLIB_ROOT/include" \
    -DTPL_PARMETIS_LIBRARIES="$PARMETIS_ROOT/lib/libparmetis.so;$METIS_ROOT/lib/libmetis.so;$GKLIB_ROOT/lib/libGKlib.so" \
    \
    -DXSDK_INDEX_SIZE=64 \
    \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_STATIC_LIBS=OFF \
    \
    -DXSDK_ENABLE_Fortran=OFF \
    -Denable_python=OFF \
    \
    -Denable_single=ON \
    -Denable_double=ON \
    -Denable_complex16=ON \
    \
    -Denable_tests=ON \
    -Denable_examples=ON
```

---

## 9. SuperLU_DIST 配置参数说明

### 9.1 编译器参数

```bash
-DCMAKE_C_COMPILER="$MPICC"
-DCMAKE_CXX_COMPILER="$MPICXX"
```

SuperLU_DIST 是分布式稀疏直接求解器，因此 C/C++ 编译器应使用 MPI 包装器。

```bash
-DCMAKE_CUDA_COMPILER="$CUDA_HOME/bin/nvcc"
```

指定 CUDA 编译器。

```bash
-DCMAKE_CUDA_HOST_COMPILER="$CXX"
```

指定 `nvcc` 使用的主机 C++ 编译器。

```bash
-DCMAKE_CUDA_FLAGS="$MPI_COMPILE_FLAGS"
```

将 MPI include 参数传递给 CUDA 编译阶段，避免 `.cu` 文件找不到 `mpi.h`。

---

### 9.2 CUDA 参数

```bash
-DTPL_ENABLE_CUDALIB=ON
```

启用 SuperLU_DIST CUDA 支持。

```bash
-DCUDAToolkit_ROOT="$CUDA_HOME"
```

告诉 CMake CUDA Toolkit 的安装位置。

```bash
-DCMAKE_CUDA_ARCHITECTURES=70
```

为 NVIDIA V100 生成 `sm_70` 代码。

其他 GPU 必须修改此数值。

---

### 9.3 BLAS 和 LAPACK

```bash
-DTPL_ENABLE_INTERNAL_BLASLIB=OFF
```

关闭 SuperLU_DIST 内部 BLAS，使用外部 OpenBLAS。

```bash
-DTPL_BLAS_LIBRARIES="$OPENBLAS_ROOT/lib/libopenblas.so"
-DTPL_LAPACK_LIBRARIES="$OPENBLAS_ROOT/lib/libopenblas.so"
```

OpenBLAS 同时提供 BLAS 和 LAPACK 接口，因此两个变量都可以指向同一个动态库。

> [!CAUTION]
> `TPL_BLAS_LIBRARIES` 和 `TPL_LAPACK_LIBRARIES` 必须填写具体库文件：
>
> ```text
> /path/to/libopenblas.so
> ```
>
> 不能只填写目录：
>
> ```text
> /path/to/openblas/lib
> ```

---

### 9.4 ParMETIS

```bash
-DTPL_ENABLE_PARMETISLIB=ON
```

启用 ParMETIS 支持。

头文件列表：

```bash
-DTPL_PARMETIS_INCLUDE_DIRS="$PARMETIS_ROOT/include;$METIS_ROOT/include;$GKLIB_ROOT/include"
```

库文件列表：

```bash
-DTPL_PARMETIS_LIBRARIES="$PARMETIS_ROOT/lib/libparmetis.so;$METIS_ROOT/lib/libmetis.so;$GKLIB_ROOT/lib/libGKlib.so"
```

CMake 列表使用分号分隔，因此整个参数必须使用双引号包住。

> [!CAUTION]
> 必须传递具体库文件，不能传递 `lib` 目录。
>
> 同时注意 `libGKlib.so` 中的大小写。

---

### 9.5 64 位索引

```bash
-DXSDK_INDEX_SIZE=64
```

启用 SuperLU_DIST 64 位索引，适合大规模稀疏矩阵。

建议 METIS 同时使用：

```bash
i64=1
```

---

### 9.6 精度选项

```bash
-Denable_single=ON
-Denable_double=ON
-Denable_complex16=ON
```

分别启用：

- 单精度实数
- 双精度实数
- 双精度复数

虽然实际应用可能只使用双精度，但 SuperLU_DIST 9.2.1 的部分共享源码和测试目标会引用其他精度的辅助函数。

只启用 double 时，测试程序链接阶段可能出现：

```text
undefined reference to `pzconvertUROWDATA2skyline'
undefined reference to `psconvertUROWDATA2skyline'
```

因此，本教程建议启用以上三种精度。

---

### 9.7 测试和示例

```bash
-Denable_tests=ON
-Denable_examples=ON
```

启用官方测试程序和示例程序。

安装完成后，可以使用这些程序检查 MPI、BLAS、ParMETIS 和 CUDA 是否正常工作。

---

## 10. 编译 SuperLU_DIST

推荐先使用单线程编译，便于观察真正的错误信息：

```bash
cmake --build build -j1
```

确认能够成功后，可以改用并行编译：

```bash
cmake --build build -j"$(nproc)"
```

> [!IMPORTANT]
> 使用 CMake 的 out-of-source 构建后，必须执行：
>
> ```bash
> cmake --build build
> ```
>
> 不要在源码根目录直接执行：
>
> ```bash
> make
> ```
>
> 源码根目录中的传统 Makefile 是另一套构建系统，混用后可能出现：
>
> ```text
> No rule to make target 'slangs_dist.o'
> ```

---

## 11. 安装 SuperLU_DIST

编译成功后执行：

```bash
cmake --install build
```

检查：

```bash
find "$SUPERLU_DIST_ROOT" -maxdepth 2 -type f | sort
```

通常会包含：

```text
superlu_dist-install/
├── include/
│   └── ...
└── lib/
    ├── libsuperlu_dist.so
    ├── libsuperlu_dist.so.9
    └── libsuperlu_dist.so.9.2.1
```

---

## 12. 设置运行时环境

将所有动态库目录加入 `LD_LIBRARY_PATH`：

```bash
export LD_LIBRARY_PATH="$SUPERLU_DIST_ROOT/lib:$PARMETIS_ROOT/lib:$METIS_ROOT/lib:$GKLIB_ROOT/lib:$OPENBLAS_ROOT/lib:$CUDA_HOME/lib64:${LD_LIBRARY_PATH}"
```

添加头文件搜索路径：

```bash
export CPATH="$SUPERLU_DIST_ROOT/include:$PARMETIS_ROOT/include:$METIS_ROOT/include:$GKLIB_ROOT/include:$OPENBLAS_ROOT/include:${CPATH}"
```

添加链接搜索路径：

```bash
export LIBRARY_PATH="$SUPERLU_DIST_ROOT/lib:$PARMETIS_ROOT/lib:$METIS_ROOT/lib:$GKLIB_ROOT/lib:$OPENBLAS_ROOT/lib:${LIBRARY_PATH}"
```

添加 CMake 搜索路径：

```bash
export CMAKE_PREFIX_PATH="$SUPERLU_DIST_ROOT:$PARMETIS_ROOT:$METIS_ROOT:$GKLIB_ROOT:$OPENBLAS_ROOT:${CMAKE_PREFIX_PATH}"
```

如需永久生效，可以将以上内容加入：

```text
~/.bashrc
```

然后执行：

```bash
source ~/.bashrc
```

---

## 13. 检查动态库依赖

检查 SuperLU_DIST 是否正确链接 OpenBLAS、MPI、CUDA、ParMETIS、METIS 和 GKlib：

```bash
ldd "$SUPERLU_DIST_ROOT/lib/libsuperlu_dist.so"
```

重点检查是否存在：

```text
not found
```

也可以过滤主要依赖：

```bash
ldd "$SUPERLU_DIST_ROOT/lib/libsuperlu_dist.so" \
    | grep -E "openblas|mpi|cuda|cublas|parmetis|metis|GKlib|not found"
```

如果存在 `not found`，优先检查：

```bash
echo "$LD_LIBRARY_PATH"
```

---

## 14. 检查 CMake 最终配置

查看关键变量：

```bash
grep -E \
"TPL_ENABLE_CUDALIB|TPL_BLAS_LIBRARIES|TPL_LAPACK_LIBRARIES|TPL_ENABLE_PARMETISLIB|TPL_PARMETIS_LIBRARIES|CMAKE_CUDA_ARCHITECTURES|XSDK_INDEX_SIZE" \
"$SUPERLU_DIST_SRC/build/CMakeCache.txt"
```

预期应当能看到：

```text
TPL_ENABLE_CUDALIB:BOOL=ON
TPL_ENABLE_PARMETISLIB:BOOL=ON
CMAKE_CUDA_ARCHITECTURES:STRING=70
XSDK_INDEX_SIZE:STRING=64
```

以及各依赖库的完整绝对路径。

---

## 15. 查找官方测试和示例程序

执行：

```bash
find "$SUPERLU_DIST_SRC/build" -type f -executable | sort
```

也可以筛选：

```bash
find "$SUPERLU_DIST_SRC/build" -type f -executable \
    | grep -E "pdtest|pddrive|drive3d|test"
```

具体生成的文件名称与 SuperLU_DIST 版本和构建选项有关。

---

## 16. 运行测试程序

以实际生成的可执行文件为准。

例如：

```bash
cd "$SUPERLU_DIST_SRC/build"
```

使用单个 MPI 进程运行：

```bash
mpirun -np 1 ./TEST/pdtest
```

使用多个 MPI 进程运行：

```bash
mpirun -np 4 ./TEST/pdtest
```

如果 OpenMPI 不允许 root 用户运行，普通用户通常不受影响；容器环境中可能需要额外参数，但不建议在正常服务器上使用 root 运行科学计算程序。

---

## 17. 确认 GPU 是否被使用

打开一个终端监控 GPU：

```bash
watch -n 0.5 nvidia-smi
```

另一个终端运行 SuperLU_DIST 示例：

```bash
mpirun -np 1 /path/to/superlu/example
```

观察：

- GPU 显存是否增加
- GPU 利用率是否变化
- 是否出现 SuperLU_DIST CUDA 相关输出

还可以使用 Nsight Systems：

```bash
nsys profile \
    --trace=cuda,nvtx,osrt \
    --stats=true \
    -o superlu_dist_profile \
    mpirun -np 1 /path/to/superlu/example
```

生成：

```text
superlu_dist_profile.nsys-rep
```

查看统计：

```bash
nsys stats superlu_dist_profile.nsys-rep
```

---

## 18. MPI 与 OpenMP 线程设置

同时启用 MPI、OpenMP 和多线程 OpenBLAS 时，需要避免线程过度订阅。

例如，每个 MPI 进程使用 4 个 OpenMP 线程：

```bash
export OMP_NUM_THREADS=4
export OPENBLAS_NUM_THREADS=4
```

运行：

```bash
mpirun -np 2 ./your_superlu_program
```

此时理论上会使用：

```text
2 个 MPI 进程 × 每进程 4 个线程 = 8 个 CPU 线程
```

如果主要并行由 MPI 或 SuperLU_DIST 自己控制，可以尝试：

```bash
export OPENBLAS_NUM_THREADS=1
```

避免每个 MPI rank 再启动大量 BLAS 线程。

建议根据 CPU 核数和 MPI rank 数进行测试，而不是始终使用 `nproc`。

---

# 常见问题

## 19. `dgemm_`、`dscal_`、`dtrsv_` 未定义

错误示例：

```text
undefined reference to `dgemm_'
undefined reference to `dscal_'
undefined reference to `dtrsv_'
undefined reference to `zscal_'
```

原因通常是将 OpenBLAS 的目录传给了 CMake，而不是具体库文件。

错误：

```bash
-DTPL_BLAS_LIBRARIES=/path/to/openblas/lib
```

正确：

```bash
-DTPL_BLAS_LIBRARIES=/path/to/openblas/lib/libopenblas.so
```

LAPACK 同理：

```bash
-DTPL_LAPACK_LIBRARIES=/path/to/openblas/lib/libopenblas.so
```

修改后必须清理：

```bash
rm -rf build
```

然后重新配置。

---

## 20. 找不到 `libgklib.so`

错误示例：

```text
No rule to make target '/path/to/libgklib.so'
```

常见原因是库文件大小写错误。

实际文件通常是：

```text
libGKlib.so
```

检查：

```bash
ls -lh "$GKLIB_ROOT/lib"
```

然后在 CMake 中使用实际存在的文件名：

```bash
"$GKLIB_ROOT/lib/libGKlib.so"
```

---

## 21. CUDA 编译找不到 `mpi.h`

错误示例：

```text
fatal error: mpi.h: No such file or directory
```

解决方法：

```bash
export MPI_COMPILE_FLAGS="$(mpicc --showme:compile)"
```

然后配置：

```bash
-DCMAKE_CUDA_FLAGS="$MPI_COMPILE_FLAGS"
```

也可以手动指定：

```bash
-DCMAKE_CUDA_FLAGS="-I/path/to/mpi/include"
```

---

## 22. `slangs_dist.o` 没有构建规则

错误示例：

```text
No rule to make target 'slangs_dist.o', needed by 'single'
```

原因是混用了源码目录中的传统 Makefile 和 CMake build 目录。

错误：

```bash
cd superlu_dist-9.2.1
make
```

正确：

```bash
cmake --build build
```

或者：

```bash
cd build
make
```

---

## 23. `pzconvertUROWDATA2skyline` 未定义

错误示例：

```text
undefined reference to `pzconvertUROWDATA2skyline'
undefined reference to `psconvertUROWDATA2skyline'
```

建议启用：

```bash
-Denable_single=ON
-Denable_double=ON
-Denable_complex16=ON
```

然后删除旧构建目录：

```bash
rm -rf build
```

重新配置和编译。

---

## 24. CMake 分号参数被 Shell 拆开

CMake 的列表使用分号分隔，例如：

```bash
-DTPL_PARMETIS_INCLUDE_DIRS="path1;path2;path3"
```

必须使用双引号。

正确：

```bash
-DTPL_PARMETIS_INCLUDE_DIRS="$PARMETIS_ROOT/include;$METIS_ROOT/include;$GKLIB_ROOT/include"
```

如果不加引号，Shell 可能把分号当成命令结束符。

---

## 25. 修改配置后仍然使用旧路径

CMake 会将依赖路径保存在：

```text
build/CMakeCache.txt
```

修改 BLAS、GKlib、ParMETIS、CUDA 或编译器路径后，建议直接：

```bash
rm -rf build
```

再重新执行完整的 `cmake -S . -B build ...`。

---

## 26. 编译中出现大量 warning

SuperLU_DIST 9.2.1 的 CUDA 和 C++ 模板代码可能产生一些 warning，例如：

```text
variable was declared but never referenced
missing return statement
unrecognized GCC pragma
#warning single node only
```

warning 本身不一定会导致编译失败。

真正失败时，应当使用单线程构建：

```bash
cmake --build build -j1 2>&1 | tee cmake_build.log
```

搜索错误：

```bash
grep -n -i -E "error:|undefined reference|No rule to make target|fatal error" cmake_build.log
```

不要只搜索 `error:`，因为链接错误和 Makefile 错误未必包含这个固定字符串。

---

# 一键环境变量示例

下面是本文使用的路径汇总。其他用户必须修改 `/home/wangyitong`：

```bash
export SOFTWARE_ROOT=/home/wangyitong/soft

export OPENBLAS_ROOT="$SOFTWARE_ROOT/openblas-install"
export GKLIB_ROOT="$SOFTWARE_ROOT/gklib-install"
export METIS_ROOT="$SOFTWARE_ROOT/metis-install"
export PARMETIS_ROOT="$SOFTWARE_ROOT/parmetis-install"
export SUPERLU_DIST_ROOT="$SOFTWARE_ROOT/superlu_dist-install"

export CUDA_HOME=/usr/local/cuda-12.6

export PATH="$CUDA_HOME/bin:$PATH"

export LD_LIBRARY_PATH="$SUPERLU_DIST_ROOT/lib:$PARMETIS_ROOT/lib:$METIS_ROOT/lib:$GKLIB_ROOT/lib:$OPENBLAS_ROOT/lib:$CUDA_HOME/lib64:${LD_LIBRARY_PATH}"

export LIBRARY_PATH="$SUPERLU_DIST_ROOT/lib:$PARMETIS_ROOT/lib:$METIS_ROOT/lib:$GKLIB_ROOT/lib:$OPENBLAS_ROOT/lib:${LIBRARY_PATH}"

export CPATH="$SUPERLU_DIST_ROOT/include:$PARMETIS_ROOT/include:$METIS_ROOT/include:$GKLIB_ROOT/include:$OPENBLAS_ROOT/include:${CPATH}"

export CMAKE_PREFIX_PATH="$SUPERLU_DIST_ROOT:$PARMETIS_ROOT:$METIS_ROOT:$GKLIB_ROOT:$OPENBLAS_ROOT:${CMAKE_PREFIX_PATH}"
```

---

# 完整安装顺序

整个流程的正确顺序是：

```text
1. OpenBLAS
2. GKlib
3. METIS
4. ParMETIS
5. SuperLU_DIST
```

依赖关系：

```text
OpenBLAS ───────────────────────────────┐
                                       │
GKlib ──> METIS ──> ParMETIS ─────────┼──> SuperLU_DIST
                                       │
MPI ───────────────────────────────────┤
CUDA ──────────────────────────────────┘
```

---

# 安装完成检查清单

- [ ] `nvcc --version` 正常
- [ ] `mpicc -show` 正常
- [ ] `$OPENBLAS_ROOT/lib/libopenblas.so` 存在
- [ ] `$GKLIB_ROOT/lib/libGKlib.so` 存在
- [ ] `$METIS_ROOT/lib/libmetis.so` 存在
- [ ] `$PARMETIS_ROOT/lib/libparmetis.so` 存在
- [ ] SuperLU_DIST CMake 配置成功
- [ ] `cmake --build build` 成功
- [ ] `cmake --install build` 成功
- [ ] `libsuperlu_dist.so` 存在
- [ ] `ldd libsuperlu_dist.so` 没有 `not found`
- [ ] 官方测试程序能够通过 `mpirun` 启动
- [ ] `nvidia-smi` 能观察到 GPU 使用情况

---

# 备注

本文配置使用：

```bash
-DXSDK_INDEX_SIZE=64
```

因此面向大规模稀疏矩阵计算。

如果后续自己编写程序链接 SuperLU_DIST，应确保：

- 应用程序使用与 SuperLU_DIST ABI 兼容的 MPI。
- 索引类型与 `XSDK_INDEX_SIZE=64` 一致。
- 运行时能找到 OpenBLAS、GKlib、METIS、ParMETIS、CUDA 和 SuperLU_DIST 动态库。
- 编译器 ABI 与安装依赖时使用的 GCC/G++ 保持兼容。
