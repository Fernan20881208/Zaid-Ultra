#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "$script_dir/.." && pwd)"
sdk_dir="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"

if [[ -z "$sdk_dir" || ! -d "$sdk_dir/platforms" || ! -d "$sdk_dir/build-tools" ]]; then
    echo "Android SDK platforms/build-tools not found" >&2
    exit 1
fi

android_jar="$(find "$sdk_dir/platforms" -type f -name android.jar -print | sort -V | tail -n 1)"
d8_bin="$(find "$sdk_dir/build-tools" -type f -name d8 -print | sort -V | tail -n 1)"
if [[ -z "$android_jar" || -z "$d8_bin" ]]; then
    echo "android.jar or d8 not found" >&2
    exit 1
fi

source_dir="$project_dir/tools/replay-audio-helper/src"
build_dir="$project_dir/build-replay-audio-helper"
classes_dir="$build_dir/classes"
dex_dir="$build_dir/dex"

rm -rf "$build_dir"
mkdir -p "$classes_dir" "$dex_dir" "$project_dir/resources"

mapfile -t java_sources < <(find "$source_dir" -type f -name '*.java' -print | sort)
if [[ ${#java_sources[@]} -eq 0 ]]; then
    echo "Replay audio helper Java source not found" >&2
    exit 1
fi

javac -encoding UTF-8 -source 8 -target 8 -classpath "$android_jar" \
    -d "$classes_dir" "${java_sources[@]}"
jar --create --file "$build_dir/replay-audio-classes.jar" -C "$classes_dir" .
"$d8_bin" --min-api 33 --lib "$android_jar" --output "$dex_dir" \
    "$build_dir/replay-audio-classes.jar"
jar --create --file "$project_dir/resources/replay-audio.jar" -C "$dex_dir" classes.dex

echo "Created resources/replay-audio.jar"
