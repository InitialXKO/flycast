#!/bin/bash
# Quest VR APK构建脚本

set -e

echo "=== Quest VR APK Build Script ==="
echo "开始时间: $(date)"

# 环境变量
export ANDROID_HOME=/tmp/android-sdk
export ANDROID_NDK=/tmp/android-ndk-r25c
export ANDROID_NDK_HOME=/tmp/android-ndk-r25c

# 切换到构建目录
cd /home/engine/project/shell/android-studio

# 检查是否有后台任务
if jobs | grep -q "gradlew"; then
    echo "检测到正在运行的构建任务，等待完成..."
    wait
fi

# 开始构建
echo "开始构建 Flycast Quest VR APK..."
./gradlew assembleRelease -x lint --no-daemon --stacktrace

# 检查构建结果
if [ -f "flycast/build/outputs/apk/release/flycast-release.apk" ]; then
    echo "✅ 构建成功！"
    echo "APK位置: $(pwd)/flycast/build/outputs/apk/release/flycast-release.apk"
    ls -lh flycast/build/outputs/apk/release/flycast-release.apk
    
    # 复制到项目根目录
    cp flycast/build/outputs/apk/release/flycast-release.apk /home/engine/project/flycast-quest-vr.apk
    echo "APK已复制到: /home/engine/project/flycast-quest-vr.apk"
else
    echo "❌ 构建失败！"
    echo "请检查构建日志"
    exit 1
fi

echo "完成时间: $(date)"
