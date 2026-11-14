#!/usr/bin/env bash
# Makes an OTA signing key pair (ECDSA P-256) and prints the public key as the C array for mirror/ota/OtaKey.h.
#   mirror/scripts/ota-keygen.sh [PRIVATE_KEY_PEM]     default: ./ota-signing.pem (refuses to overwrite)
# The private key never goes into git. Give it to CI once:   gh secret set OTA_SIGNING_KEY < ota-signing.pem
# Replacing the key means every device must first be updated with firmware that carries the new public key.
set -euo pipefail
KEY="${1:-ota-signing.pem}"
[ -e "$KEY" ] && { echo "$KEY exists: not overwriting" >&2; exit 1; }
umask 077
openssl ecparam -name prime256v1 -genkey -noout -out "$KEY"
echo "private key written to $KEY (keep it secret, never commit it)" >&2
openssl ec -in "$KEY" -pubout -outform DER 2>/dev/null | python3 -c '
import sys
der = sys.stdin.buffer.read()
print("inline constexpr uint8_t kOtaPublicKeyDer[] = {")
for i in range(0, len(der), 12):
    print("    " + ", ".join("0x%02x" % b for b in der[i:i + 12]) + ",")
print("};")
'
