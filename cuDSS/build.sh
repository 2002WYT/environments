rm -rf build/
mkdir build
cd ./build
cmake ..
make
compute-sanitizer --tool memcheck --leak-check full ./main
