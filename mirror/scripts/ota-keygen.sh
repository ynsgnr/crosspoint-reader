#!/usr/bin/env bash
# Makes an OTA signing key pair (ECDSA P-256) and prints the public key as the C array for mirror/ota/OtaKey.h.
#   mirror/scripts/ota-keygen.sh [PRIVATE_KEY_PEM]     default: mirror/ota-signing.pem (git-ignored; refuses to overwrite)
#   mirror/scripts/ota-keygen.sh --array PRIVATE_KEY_PEM   only prints the array of an existing key
# The private key never goes into git. Give it to CI once:   gh secret set OTA_SIGNING_KEY --env ota-signing < mirror/ota-signing.pem
# Replacing the key means every device must first be updated with firmware that carries the new public key.
set -euo pipefail

array() {   # the public key of PEM $1 (SubjectPublicKeyInfo DER), 19 bytes to a line, as clang-format lays it out
  echo "inline constexpr uint8_t kOtaPublicKeyDer[] = {"
  openssl ec -in "$1" -pubout -outform DER 2>/dev/null | od -An -v -tx1 -w19 | sed 's/ \([0-9a-f][0-9a-f]\)/0x\1, /g; s/^/    /; s/ $//'
  echo "};"
}

if [ "${1:-}" = --array ]; then array "$2"; exit; fi
KEY="${1:-$(cd "$(dirname "$0")/.." && pwd)/ota-signing.pem}"
[ -e "$KEY" ] && { echo "$KEY exists: not overwriting" >&2; exit 1; }
umask 077
openssl ecparam -name prime256v1 -genkey -noout -out "$KEY"
echo "private key written to $KEY (keep it secret, never commit it)" >&2
array "$KEY"
