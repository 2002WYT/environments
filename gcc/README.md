## 无 sudo 权限离线安装 GCC 11.2.0

本文记录如何在 Linux 服务器上 **不使用 sudo 权限**，将 GCC 11.2.0 离线安装到自己的用户目录中。

适用场景：

* 服务器不能使用 `sudo`
* 系统 GCC 版本过低
* 需要自己安装 GCC 11.2.0
* 已经将 `gcc-11.2.0.tar.gz` 上传到服务器并解压

下面假设用户名为：

```bash
wangyitong
```

GCC 源码目录为：

```bash
/home/wangyitong/gcc-11.2.0
```

GCC 安装目录为：

```bash
/home/wangyitong/opt/gcc-11.2.0
```

如果你的路径不同，请自行替换。

---

### 1. 检查源码目录

进入 GCC 源码目录：

```bash
cd /home/wangyitong/gcc-11.2.0
```

检查目录内容：

```bash
ls
```

正常情况下应该能看到：

```bash
configure
gcc
libstdc++-v3
contrib
```

---

### 2. 查看 GCC 11.2.0 推荐的依赖库版本

GCC 编译需要以下依赖库：

* GMP
* MPFR
* MPC
* ISL

不要自己随便选择最新版，最稳妥的方法是查看 GCC 源码自带的脚本：

```bash
cd /home/wangyitong/gcc-11.2.0
grep -nE "gmp-|mpfr-|mpc-|isl-" contrib/download_prerequisites
```

对于 GCC 11.2.0，通常会看到类似版本：

```bash
gmp-6.1.0.tar.bz2
mpfr-3.1.4.tar.bz2
mpc-1.0.3.tar.gz
isl-0.18.tar.bz2
```

这几个就是 GCC 11.2.0 推荐使用的依赖版本。

---

### 3. 准备离线依赖包

在本地有网络的电脑上下载以下文件：

```bash
gmp-6.1.0.tar.bz2
mpfr-3.1.4.tar.bz2
mpc-1.0.3.tar.gz
isl-0.18.tar.bz2
```

然后上传到服务器的 GCC 源码目录：

```bash
/home/wangyitong/gcc-11.2.0
```

上传完成后，在服务器上检查：

```bash
cd /home/wangyitong/gcc-11.2.0
ls -lh gmp-*.tar.* mpfr-*.tar.* mpc-*.tar.* isl-*.tar.*
```

应该能看到类似：

```bash
gmp-6.1.0.tar.bz2
mpfr-3.1.4.tar.bz2
mpc-1.0.3.tar.gz
isl-0.18.tar.bz2
```

---

### 4. 解压并准备依赖库

推荐直接使用 GCC 自带脚本：

```bash
cd /home/wangyitong/gcc-11.2.0
./contrib/download_prerequisites
```

虽然脚本名字叫 `download_prerequisites`，但是如果依赖压缩包已经在当前目录中，它通常会直接使用本地文件。

执行完成后检查：

```bash
ls -l gmp mpfr mpc isl
```

正常情况下应该看到类似：

```bash
gmp -> gmp-6.1.0
mpfr -> mpfr-3.1.4
mpc -> mpc-1.0.3
isl -> isl-0.18
```

这说明依赖已经准备好了。

---

### 5. 如果脚本不能正常使用，则手动解压依赖

如果 `./contrib/download_prerequisites` 因为网络问题或脚本问题失败，可以手动解压并建立软链接：

```bash
cd /home/wangyitong/gcc-11.2.0

tar -xf gmp-6.1.0.tar.bz2
tar -xf mpfr-3.1.4.tar.bz2
tar -xf mpc-1.0.3.tar.gz
tar -xf isl-0.18.tar.bz2

ln -s gmp-6.1.0 gmp
ln -s mpfr-3.1.4 mpfr
ln -s mpc-1.0.3 mpc
ln -s isl-0.18 isl
```

检查软链接：

```bash
ls -l gmp mpfr mpc isl
```

---

### 6. 创建编译目录和安装目录

不要在 GCC 源码目录中直接编译。

创建单独的 build 目录：

```bash
mkdir -p /home/wangyitong/build-gcc-11.2.0
mkdir -p /home/wangyitong/opt/gcc-11.2.0
```

目录含义如下：

```bash
/home/wangyitong/gcc-11.2.0          # GCC 源码目录
/home/wangyitong/build-gcc-11.2.0    # GCC 编译目录
/home/wangyitong/opt/gcc-11.2.0      # GCC 安装目录
```

进入编译目录：

```bash
cd /home/wangyitong/build-gcc-11.2.0
```

---

### 7. 配置 GCC

执行：

```bash
/home/wangyitong/gcc-11.2.0/configure \
  --prefix=/home/wangyitong/opt/gcc-11.2.0 \
  --enable-languages=c,c++ \
  --disable-multilib \
  --disable-bootstrap
```

参数说明：

```bash
--prefix=/home/wangyitong/opt/gcc-11.2.0
```

表示将 GCC 安装到自己的用户目录，不需要 sudo。

```bash
--enable-languages=c,c++
```

表示只编译 C 和 C++ 编译器。

```bash
--disable-multilib
```

表示不编译 32 位库，避免缺少 32 位系统库导致报错。

```bash
--disable-bootstrap
```

表示跳过 bootstrap 流程，减少编译时间。

配置成功后，当前 build 目录中会生成 `Makefile`。

---

### 8. 编译 GCC

先查看 CPU 核心数：

```bash
nproc
```

然后编译：

```bash
make -j4
```

如果服务器内存较小，建议使用：

```bash
make -j2
```

甚至：

```bash
make -j1
```

如果编译过程中出现 `Killed`，通常是内存不足，需要降低 `-j` 后面的并行数。

---

### 9. 安装 GCC

编译完成后执行：

```bash
make install
```

由于安装路径是自己的用户目录，因此不需要 sudo。

安装完成后检查：

```bash
ls /home/wangyitong/opt/gcc-11.2.0/bin
```

应该能看到：

```bash
gcc
g++
c++
cpp
```

---

### 10. 临时使用新 GCC

先临时设置环境变量：

```bash
export GCC_HOME=/home/wangyitong/opt/gcc-11.2.0
export PATH=$GCC_HOME/bin:$PATH
export LD_LIBRARY_PATH=$GCC_HOME/lib64:$GCC_HOME/lib:$LD_LIBRARY_PATH
```

检查当前使用的 GCC：

```bash
which gcc
which g++
```

应该输出类似：

```bash
/home/wangyitong/opt/gcc-11.2.0/bin/gcc
/home/wangyitong/opt/gcc-11.2.0/bin/g++
```

查看版本：

```bash
gcc --version
g++ --version
```

应该显示：

```bash
gcc (GCC) 11.2.0
g++ (GCC) 11.2.0
```

---

### 11. 永久配置环境变量

如果确认 GCC 可以正常使用，可以将环境变量写入 `~/.bashrc`：

```bash
nano ~/.bashrc
```

在文件最后加入：

```bash
# My GCC 11.2.0
export GCC_HOME=/home/wangyitong/opt/gcc-11.2.0
export PATH=$GCC_HOME/bin:$PATH
export LD_LIBRARY_PATH=$GCC_HOME/lib64:$GCC_HOME/lib:$LD_LIBRARY_PATH
```

保存后执行：

```bash
source ~/.bashrc
```

再次检查：

```bash
which gcc
gcc --version

which g++
g++ --version
```

---

### 12. 测试 C++ 编译是否正常

创建测试文件：

```bash
cat > test.cpp << 'EOF'
#include <iostream>
#include <vector>
#include <numeric>

int main() {
    std::vector<int> a = {1, 2, 3, 4};
    std::cout << "sum = " << std::accumulate(a.begin(), a.end(), 0) << std::endl;
    return 0;
}
EOF
```

编译：

```bash
g++ -std=c++17 test.cpp -o test
```

运行：

```bash
./test
```

正常输出：

```bash
sum = 10
```

说明 GCC 11.2.0 安装成功。

---

### 13. 在 CMake 项目中使用自己安装的 GCC

建议重新创建一个干净的 build 目录：

```bash
cd ~/code/demo01
rm -rf build
mkdir build
cd build
```

指定新的 GCC 和 G++：

```bash
cmake .. \
  -DCMAKE_C_COMPILER=/home/wangyitong/opt/gcc-11.2.0/bin/gcc \
  -DCMAKE_CXX_COMPILER=/home/wangyitong/opt/gcc-11.2.0/bin/g++
```

然后编译：

```bash
make -j4
```

如果是 CUDA 项目，还可以指定 CUDA host compiler：

```bash
cmake .. \
  -DCMAKE_C_COMPILER=/home/wangyitong/opt/gcc-11.2.0/bin/gcc \
  -DCMAKE_CXX_COMPILER=/home/wangyitong/opt/gcc-11.2.0/bin/g++ \
  -DCMAKE_CUDA_HOST_COMPILER=/home/wangyitong/opt/gcc-11.2.0/bin/g++
```

---

### 14. 常见问题

#### 问题 1：`configure: error: no acceptable C compiler found`

说明系统中没有可用的旧 GCC。

检查：

```bash
which gcc
which g++
```

如果服务器完全没有 GCC，并且没有 sudo 权限，就不能直接从源码编译 GCC，需要先获得一个可运行的预编译 GCC，或者联系管理员安装基础编译环境。

---

#### 问题 2：`gmp.h not found` / `mpfr.h not found` / `mpc.h not found`

说明依赖库没有准备好。

检查：

```bash
cd /home/wangyitong/gcc-11.2.0
ls -l gmp mpfr mpc isl
```

如果不存在，重新运行：

```bash
./contrib/download_prerequisites
```

或者手动解压依赖并建立软链接。

---

#### 问题 3：`cannot find crti.o` 或 32 位库相关错误

通常是因为没有加：

```bash
--disable-multilib
```

解决方法是清空 build 目录后重新配置：

```bash
rm -rf /home/wangyitong/build-gcc-11.2.0
mkdir -p /home/wangyitong/build-gcc-11.2.0
cd /home/wangyitong/build-gcc-11.2.0

/home/wangyitong/gcc-11.2.0/configure \
  --prefix=/home/wangyitong/opt/gcc-11.2.0 \
  --enable-languages=c,c++ \
  --disable-multilib \
  --disable-bootstrap
```

然后重新编译：

```bash
make -j4
make install
```

---

#### 问题 4：编译过程中出现 `Killed`

通常是服务器内存不足。

降低并行编译数量：

```bash
make -j2
```

或者：

```bash
make -j1
```

---

## 一键命令汇总

如果依赖包已经放在 `/home/wangyitong/gcc-11.2.0` 中，可以按下面顺序执行：

```bash
cd /home/wangyitong/gcc-11.2.0

./contrib/download_prerequisites

mkdir -p /home/wangyitong/build-gcc-11.2.0
mkdir -p /home/wangyitong/opt/gcc-11.2.0

cd /home/wangyitong/build-gcc-11.2.0

/home/wangyitong/gcc-11.2.0/configure \
  --prefix=/home/wangyitong/opt/gcc-11.2.0 \
  --enable-languages=c,c++ \
  --disable-multilib \
  --disable-bootstrap

make -j4
make install

export GCC_HOME=/home/wangyitong/opt/gcc-11.2.0
export PATH=$GCC_HOME/bin:$PATH
export LD_LIBRARY_PATH=$GCC_HOME/lib64:$GCC_HOME/lib:$LD_LIBRARY_PATH

gcc --version
g++ --version
```
