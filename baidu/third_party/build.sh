#!/bin/sh
# Build TFLite Micro and the microWakeWord spectrogram frontend as armhf static
# libraries for the micro_wake_word host port. Run by baidu/Dockerfile:
#   third_party/build.sh <source dir> <install prefix>
# Installs <prefix>/lib/libtflm.a, <prefix>/lib/libmicro_speech_features.a and
# the headers under <prefix>/include/tflm and <prefix>/include/msf. The tflm source list (tflm_sources.txt)
# is the C++ app's (../third_party/meson.build): esp-tflite-micro's
# CMakeLists.txt with the portable reference kernels instead of ESP-NN.
set -eu
SRC=$1
PREFIX=$2
OBJ=$(mktemp -d)
FL="-O3 -march=armv7-a -mfloat-abi=hard -mfpu=neon -w -fPIC"
CXX=arm-linux-gnueabihf-g++
CC=arm-linux-gnueabihf-gcc
AR=arm-linux-gnueabihf-ar

TF=$SRC/tflite-micro
TFINC="-I$TF -I$TF/third_party/gemmlowp -I$TF/third_party/flatbuffers/include -I$TF/third_party/ruy"
i=0
while read -r f; do
	[ -n "$f" ] || continue
	i=$((i + 1))
	$CXX $FL -std=c++17 -fno-exceptions -fno-threadsafe-statics \
		-DTF_LITE_STATIC_MEMORY -DTF_LITE_DISABLE_X86_NEON $TFINC \
		-c "$SRC/$f" -o "$OBJ/tflm_$i.o" &
	# a few compiles at a time
	[ $((i % 8)) -ne 0 ] || wait
done <"$SRC/tflm_sources.txt"
wait
mkdir -p "$PREFIX/lib" "$PREFIX/include"
$AR rcs "$PREFIX/lib/libtflm.a" "$OBJ"/tflm_*.o

MSF=$SRC/micro_speech_features
for f in "$MSF"/src/*.c; do
	$CC $FL -I"$MSF/include" -I"$MSF/src" -c "$f" -o "$OBJ/msf_$(basename "$f" .c).o"
done
$AR rcs "$PREFIX/lib/libmicro_speech_features.a" "$OBJ"/msf_*.o

# headers: tflite-micro's tree (plus its third_party headers) and the frontend's
mkdir -p "$PREFIX/include/tflm"
(cd "$TF" && find tensorflow third_party signal -name "*.h" | xargs cp --parents -t "$PREFIX/include/tflm")
# frontend.h includes ../src/*.h, so keep the include/ and src/ layout
mkdir -p "$PREFIX/include/msf/include" "$PREFIX/include/msf/src"
cp "$MSF"/include/*.h "$PREFIX/include/msf/include/"
cp "$MSF"/src/*.h "$PREFIX/include/msf/src/"
rm -rf "$OBJ"
ls -l "$PREFIX/lib"
