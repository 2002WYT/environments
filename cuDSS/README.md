## 在无 sudo 权限的 Linux 服务器上安装并使用 clangd

本项目使用 VS Code Remote-SSH 在 Linux 服务器上开发 CUDA/C++ 程序。为了获得代码补全、跳转定义、悬停提示、红色波浪线诊断等功能，需要在服务器端安装 `clangd`，并让 VS Code 的 clangd 扩展找到它。

### 1. 检查服务器是否已经有 clangd

在服务器终端运行：

```bash
which clangd
clangd --version
```

如果能看到 `clangd` 路径和版本号，说明已经安装。

如果提示：

```bash
clangd: command not found
```

则需要手动安装到自己的用户目录。

---

### 2. 下载 clangd 到自己的用户目录

没有 `sudo` 权限时，可以把 clangd 安装到 `$HOME/tools`。

先确认服务器架构：

```bash
uname -m
```

如果输出是：

```bash
x86_64
```

可以使用 Linux x86_64 版本的 clangd。

创建安装目录：

```bash
mkdir -p $HOME/tools
cd $HOME/tools
```

以 clangd 22.1.0 为例下载：

```bash
wget https://github.com/clangd/clangd/releases/download/22.1.0/clangd-linux-22.1.0.zip
```

如果服务器不能直接访问 GitHub，可以在本地电脑浏览器打开 clangd releases 页面，下载 `clangd-linux-22.1.0.zip`，再上传到服务器的：

```bash
$HOME/tools
```

---

### 3. 解压 clangd

如果服务器有 `unzip`：

```bash
cd $HOME/tools
unzip clangd-linux-22.1.0.zip
```

如果没有 `unzip`，可以用 Python 解压：

```bash
python3 -m zipfile -e clangd-linux-22.1.0.zip .
```

然后查找 clangd 可执行文件：

```bash
find $HOME/tools -name clangd -type f
```

可能得到类似路径：

```bash
/home/wangyitong/tools/clangd_22.1.0/bin/clangd
```

测试：

```bash
/home/wangyitong/tools/clangd_22.1.0/bin/clangd --version
```

如果能输出版本号，说明安装成功。

---

### 4. 把 clangd 加入 PATH

假设 clangd 路径是：

```bash
/home/wangyitong/tools/clangd_22.1.0/bin/clangd
```

则执行：

```bash
echo 'export PATH=$HOME/tools/clangd_22.1.0/bin:$PATH' >> ~/.bashrc
source ~/.bashrc
```

检查：

```bash
which clangd
clangd --version
```

如果输出的是自己目录下的 clangd，例如：

```bash
/home/wangyitong/tools/clangd_22.1.0/bin/clangd
```

说明配置成功。

---

### 5. 在 VS Code 中指定 clangd 路径

使用 VS Code Remote-SSH 连接服务器后，打开远程设置：

```text
Ctrl + Shift + P
Preferences: Open Remote Settings (JSON)
```

加入：

```json
{
    "clangd.path": "/home/wangyitong/tools/clangd_22.1.0/bin/clangd",
    "C_Cpp.intelliSenseEngine": "disabled"
}
```

其中：

```json
"clangd.path"
```

用于指定服务器上的 clangd 可执行文件。

```json
"C_Cpp.intelliSenseEngine": "disabled"
```

用于关闭 Microsoft C/C++ 扩展的 IntelliSense，避免它和 clangd 同时分析代码导致冲突。

如果没有安装 Microsoft C/C++ 扩展，这一行可以不写。

---

### 6. 让 CMake 生成 compile_commands.json

clangd 需要 `compile_commands.json` 来知道项目的真实编译参数，例如头文件路径、CUDA 路径、cuDSS 路径等。

在 `CMakeLists.txt` 中加入：

```cmake
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

例如：

```cmake
cmake_minimum_required(VERSION 3.18)

project(cuda_test LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

然后重新配置项目：

```bash
cd ~/code/demo01
rm -rf build
cmake -S . -B build
cmake --build build -j
```

CMake 会在 `build/` 目录下生成：

```bash
build/compile_commands.json
```

为了让 clangd 更容易找到它，可以在项目根目录建立软链接：

```bash
ln -sf build/compile_commands.json compile_commands.json
```

检查：

```bash
ls -l compile_commands.json
```

应该看到类似：

```bash
compile_commands.json -> build/compile_commands.json
```

---

### 7. 配置项目 include 路径

如果项目结构类似：

```text
demo01/
├── CMakeLists.txt
├── demo03.cu
├── include/
│   └── head.cuh
└── src/
    └── maths.cu
```

则 `CMakeLists.txt` 里需要告诉编译器头文件目录：

```cmake
target_include_directories(main PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
```

否则 clangd 可能会报：

```text
'head.cuh' file not found
```

---

### 8. 处理 clangd 不认识 nvcc 参数的问题

CUDA 项目中，clangd 可能读取 `compile_commands.json` 后报错：

```text
Unknown argument: '-forward-unknown-to-host-compiler'
Unknown argument: '--generate-code=arch=compute_70,code=[compute_70,sm_70]'
```

这是因为这些参数是 `nvcc` 专用参数，clangd 不完全认识。可以在项目根目录创建 `.clangd` 文件：

```bash
cd ~/code/demo01
vim .clangd
```

写入：

```yaml
CompileFlags:
  Add:
    - -xcuda
    - -I/home/wangyitong/code/demo01/include
  Remove:
    - -forward-unknown-to-host-compiler
    - --generate-code=*
    - -gencode=*
```

如果 CUDA 头文件也找不到，可以先查看 CUDA 路径：

```bash
which nvcc
dirname $(dirname $(which nvcc))
```

假设 CUDA 根目录是：

```bash
/usr/local/cuda
```

则 `.clangd` 可以写成：

```yaml
CompileFlags:
  Add:
    - -xcuda
    - --cuda-path=/usr/local/cuda
    - -I/usr/local/cuda/include
    - -I/home/wangyitong/code/demo01/include
  Remove:
    - -forward-unknown-to-host-compiler
    - --generate-code=*
    - -gencode=*
```

如果使用 cuDSS，也可以加入 cuDSS 的 include 路径，例如：

```yaml
CompileFlags:
  Add:
    - -xcuda
    - --cuda-path=/usr/local/cuda
    - -I/usr/local/cuda/include
    - -I/home/wangyitong/code/demo01/include
    - -I/home/wangyitong/cudss_install/nvidia/cu12/include
  Remove:
    - -forward-unknown-to-host-compiler
    - --generate-code=*
    - -gencode=*
```

---

### 9. 重启 clangd

修改配置后，在 VS Code 中执行：

```text
Ctrl + Shift + P
clangd: Restart language server
```

或者重新加载窗口：

```text
Ctrl + Shift + P
Developer: Reload Window
```

---

### 10. 检查 clangd 是否生效

打开 `.cu`、`.cuh`、`.cpp` 文件，测试：

```text
Ctrl + 鼠标左键跳转定义
鼠标悬停查看类型
函数补全
include 路径识别
错误红色波浪线
```

如果仍然有大量红色波浪线，可以打开 clangd 日志查看原因：

```text
View -> Output -> clangd
```

常见问题包括：

```text
找不到 compile_commands.json
找不到 CUDA include
找不到项目 include 目录
clangd.path 路径写错
clangd 扩展没有安装在 Remote-SSH 服务器端
```

---

### 11. 推荐不要提交的文件

`build/` 目录不应该提交到 GitHub。

可以在 `.gitignore` 中加入：

```gitignore
build/
```

`compile_commands.json` 如果是指向 `build/compile_commands.json` 的软链接，也可以不提交，因为不同机器上的 build 目录可能不同。

如果希望仓库保留 clangd 的项目级配置，可以提交 `.clangd` 文件。

一般建议提交：

```text
.clangd
CMakeLists.txt
include/
src/
README.md
.gitignore
```

不建议提交：

```text
build/
.vscode/settings.json
```

因为 `.vscode/settings.json` 里面可能包含个人服务器路径，例如：

```json
"clangd.path": "/home/wangyitong/tools/clangd_22.1.0/bin/clangd"
```

这种路径只对个人服务器有效。
