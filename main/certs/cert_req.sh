#!/bin/sh

PROJECT_NAME="MUBBY"
UUID=`cat /proc/sys/kernel/random/uuid`

# Generate the OpenSSL configure files

cat > client_cert.cnf << EOF
[ req ]
distinguished_name	= req_distinguished_name
prompt			= no

[ req_distinguished_name ]
O			= $PROJECT_NAME Client Certificate
CN			= MUBBY_$UUID

[ req_attributes ]

[ cert_ext ]
basicConstraints=CA:FALSE
KeyUsage=nonRepudiation,digitalSignature,keyEncipherment
EOF

# Private key generation
openssl genrsa -out privkey.pem

# Cert request
openssl req -out client.req -key privkey.pem -new \
	-config $PWD/client_cert.cnf

rm -f client_cert.cnf

