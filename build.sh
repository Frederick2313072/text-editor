#!/bin/bash

# 创建并进入构建目录
mkdir -p build
cd build

# 配置并构建
cmake ..
make

# 移动可执行文件到根目录
mv bin/kilo ../kilo

# 返回上级目录并清理build
cd ..
rm -rf build

echo "编译完成，可执行文件在当前目录"
echo "使用方法: ./kilo [文件名]" 