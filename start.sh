#!/bin/bash

export PROJECT_DIR=$(pwd)
export PROJECT_LOCAL_DIR=$PROJECT_DIR/local
export LD_LIBRARY_PATH=$PROJECT_LOCAL_DIR/lib:$PROJECT_LOCAL_DIR/lib64


mkdir -p $PROJECT_DIR/certs/
cd $PROJECT_DIR/certs/
$PROJECT_LOCAL_DIR/bin/openssl req -x509 \
        -newkey rsa:4096 \
        -keyout key.pem \
        -out cert.pem \
        -sha256 \
        -days 3650 \
        -nodes \
        -subj "/C=CY/ST=Cyprus/L=Home/O=none/OU=CompanySectionName/CN=localhost/CN=www.example.com"
cd ..
#$PROJECT_LOCAL_DIR/bin/tclsh9.0 $PROJECT_DIR/examples/simple/example-best-with-router.tcl
#$PROJECT_LOCAL_DIR/bin/tclsh9.0 $PROJECT_DIR/examples/sample-blog/app.tcl
$PROJECT_LOCAL_DIR/bin/tclsh9.0 $PROJECT_DIR/examples/sample-session/app.tcl