#!/bin/sh
# usage: tls_test.sh <net_test binary> ; local openssl s_server with self-signed cert, verifies trust + rejection
set -e
D=$(mktemp -d); trap 'kill $P 2>/dev/null; rm -rf $D' EXIT
openssl req -x509 -newkey rsa:2048 -nodes -keyout $D/k.pem -out $D/c.pem -days 1 -subj /CN=localhost -addext subjectAltName=DNS:localhost >/dev/null 2>&1
openssl s_server -accept 0 -cert $D/c.pem -key $D/k.pem -www -naccept 8 >$D/log 2>&1 &
P=$!
PORT=""
for i in 1 2 3 4 5 6 7 8 9 10; do
  PORT=$(ss -ltnp 2>/dev/null | awk -v p="pid=$P," '$0 ~ p {n=split($4,a,":"); print a[n]; exit}')
  [ -n "$PORT" ] && break; sleep 0.2
done
[ -n "$PORT" ] || { echo "no port"; exit 1; }
"$1" --tls "https://localhost:$PORT/" $D/c.pem
