#!/usr/bin/env bash
# Build the exact experimental TLS dependency into an explicit disposable prefix.
set -euo pipefail
prefix=${1:?usage: build-acp-pilot-tls.sh ABSOLUTE_DISPOSABLE_PREFIX}
case "$prefix" in /*) ;; *) exit 2 ;; esac
mkdir -p "$prefix/source" "$prefix/install"
curl --fail --location --proto '=https' --tlsv1.2 \
  https://github.com/openssl/openssl/releases/download/openssl-3.6.5/openssl-3.6.5.tar.gz \
  --output "$prefix/source/openssl.tar.gz"
python3 - "$prefix/source/openssl.tar.gz" <<'PY'
import hashlib,sys
assert hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest() == 'a2157c2830efdec3788939b00c9b0638306d3f0bbb76dc4832ee503bb397df98'
PY
tar -xzf "$prefix/source/openssl.tar.gz" -C "$prefix/source"
cd "$prefix/source/openssl-3.6.5"
./Configure --prefix="$prefix/install" --libdir=lib shared no-tests no-docs
make -j2
make install_sw
