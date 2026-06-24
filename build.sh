#!/bin/bash
set -e

# 获取项目根目录绝对路径
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST_DIR="${PROJECT_DIR}/dist"

echo "========================================="
echo "开始编译 sip-answer-engine..."
echo "========================================="

# 1. 创建或清理构建目录
mkdir -p "${PROJECT_DIR}/build"
cd "${PROJECT_DIR}/build"

# 2. 执行编译
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

echo "========================================="
echo "编译成功，开始打包部署目录..."
echo "========================================="

# 3. 创建/清理部署目录 dist
rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}/bin"

# 4. 拷贝二进制程序
cp "${PROJECT_DIR}/build/sip-answer-engine" "${DIST_DIR}/bin/"

# 5. 拷贝配置文件和运行脚本
cp "${PROJECT_DIR}/config.yaml" "${DIST_DIR}/"
cp "${PROJECT_DIR}/start.sh" "${DIST_DIR}/"
cp "${PROJECT_DIR}/stop.sh" "${DIST_DIR}/"
cp "${PROJECT_DIR}/test.sh" "${DIST_DIR}/"

# 6. 拷贝测试资源目录 tests
cp -r "${PROJECT_DIR}/tests" "${DIST_DIR}/"

# 7. 赋予可执行权限
chmod +x "${DIST_DIR}/bin/sip-answer-engine"
chmod +x "${DIST_DIR}/start.sh"
chmod +x "${DIST_DIR}/stop.sh"
chmod +x "${DIST_DIR}/test.sh"

echo "-> 部署包打包完成！"
echo "部署目录为: ${DIST_DIR}"
echo "您可以直接拷贝该目录下的内容去目标环境部署。"
echo "========================================="
