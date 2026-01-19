#!/bin/bash
# Quest VR APK完整构建脚本 - 包含VR功能

set -e

echo "=== Flycast Quest VR APK构建脚本 ==="
echo "开始时间: $(date)"

# 环境变量
export ANDROID_HOME=/tmp/android-sdk
export ANDROID_NDK=/tmp/android-ndk-r25c
export ANDROID_NDK_HOME=/tmp/android-ndk-r25c
export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64

echo "环境变量:"
echo "  ANDROID_HOME=$ANDROID_HOME"
echo "  ANDROID_NDK=$ANDROID_NDK"
echo "  JAVA_HOME=$JAVA_HOME"

# 检查local.properties
if [ ! -f /home/engine/project/shell/android-studio/local.properties ]; then
    echo "创建 local.properties..."
    cat > /home/engine/project/shell/android-studio/local.properties << EOF
sdk.dir=/tmp/android-sdk
ndk.dir=/tmp/android-ndk-r25c
EOF
fi

# 切换到构建目录
cd /home/engine/project/shell/android-studio

echo "开始清理构建..."
./gradlew clean --no-daemon

echo "开始构建 Flycast Quest VR APK (包含VR功能)..."
./gradlew assembleRelease -x lint --no-daemon --stacktrace

# 检查构建结果
if [ -f "flycast/build/outputs/apk/release/flycast-release.apk" ]; then
    echo "✅ 构建成功！"
    echo "APK位置: $(pwd)/flycast/build/outputs/apk/release/flycast-release.apk"
    ls -lh flycast/build/outputs/apk/release/flycast-release.apk
    
    # 复制到项目根目录
    cp flycast/build/outputs/apk/release/flycast-release.apk /home/engine/project/flycast-quest-vr.apk
    echo "APK已复制到: /home/engine/project/flycast-quest-vr.apk"
    
    # 显示APK信息
    echo ""
    echo "APK信息:"
    /tmp/android-sdk/build-tools/35.0.0/aapt dump badging /home/engine/project/flycast-quest-vr.apk | grep -E "package:|sdkVersion:|targetSdkVersion:"
else
    echo "❌ 构建失败！"
    echo "请检查构建日志"
    exit 1
fi

echo "完成时间: $(date)"
