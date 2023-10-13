#!/bin/bash -eu

if [ ! -e "$(pwd)/depot_tools" ]
then
  git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git
fi

export COMMAND_DIR=$(cd $(dirname $0); pwd)
export PATH="$(pwd)/depot_tools:$PATH"
export WEBRTC_VERSION=5790
export OUTPUT_DIR="$(pwd)/out"
export ARTIFACTS_DIR="$(pwd)/artifacts"
export PYTHON3_BIN="$(pwd)/depot_tools/python-bin/python3"

if [ ! -e "$(pwd)/src" ]
then
  fetch --nohooks webrtc_ios
  cd src
  sudo sh -c 'echo 127.0.1.1 $(hostname) >> /etc/hosts'
  sudo git config --system core.longpaths true
  git checkout "refs/remotes/branch-heads/$WEBRTC_VERSION"
  cd ..
  gclient sync -D --force --reset
fi

# add jsoncpp
patch -N "src/BUILD.gn" < "$COMMAND_DIR/patches/add_jsoncpp.patch"

# disable GCD taskqueue, use stdlib taskqueue instead
# This is because GCD cannot measure with UnityProfiler
patch -N "src/api/task_queue/BUILD.gn" < "$COMMAND_DIR/patches/disable_task_queue_gcd.patch"

# add objc library to use videotoolbox
patch -N "src/sdk/BUILD.gn" < "$COMMAND_DIR/patches/add_objc_deps.patch"

# modified BUILD.gn of audio_processing to build a static library
cd src
if git apply --check "$COMMAND_DIR/patches/build_static_audio_processing.patch" >/dev/null 2>&1; then
  echo "build_static_audio_processing.patch not applied"
  cd ..
  patch -N "src/modules/audio_processing/BUILD.gn" < "$COMMAND_DIR/patches/build_static_audio_processing.patch"
else
  echo "build_static_audio_processing.patch applied"
  cd ..
fi

# use included python
export PATH="$(pwd)/depot_tools/bootstrap-3.8.0.chromium.8_bin/python/bin:$PATH"

mkdir -p "$ARTIFACTS_DIR/lib"

for is_debug in "true" "false"
do
  for target_cpu in "arm64" "x64"
  do
    # generate ninja files
    # 
    # note: `treat_warnings_as_errors=false` is for avoiding LLVM warning.
    #       https://reviews.llvm.org/D72212
    #       See below for details.
    #       https://bugs.chromium.org/p/webrtc/issues/detail?id=11729
    #
    gn gen "$OUTPUT_DIR" --root="src" \
      --args="is_debug=${is_debug} \
      target_os=\"ios\" \
      target_cpu=\"${target_cpu}\" \
      rtc_use_h264=false \
      use_custom_libcxx=false \
      treat_warnings_as_errors=false \
      ios_enable_code_signing=false \
      rtc_include_tests=false \
      rtc_build_examples=false \
      use_cxx17=true"

    # build static library
    ninja -C "$OUTPUT_DIR" audio_processing

    # copy static library
    mkdir -p "$ARTIFACTS_DIR/lib/${target_cpu}"
    cp "$OUTPUT_DIR/obj/modules/audio_processing/libaudio_processing.a" "$ARTIFACTS_DIR/lib/${target_cpu}/"
  done

  filename="libaudio_processing.a"
  if [ $is_debug = "true" ]; then
    filename="libaudio_processingd.a"
  fi

  # make universal binary
  lipo -create -output                   \
  "$ARTIFACTS_DIR/lib/${filename}"       \
  "$ARTIFACTS_DIR/lib/arm64/libaudio_processing.a" \
  "$ARTIFACTS_DIR/lib/x64/libaudio_processing.a"
  
  rm -r "$ARTIFACTS_DIR/lib/arm64"
  rm -r "$ARTIFACTS_DIR/lib/x64"
done
