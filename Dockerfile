FROM alpine:latest
ENV TZ=UTC
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone
RUN apk add --no-cache \
    linux-headers \
    bsd-compat-headers \
    zlib \
    bash \
    perl \
    gcc \
    g++ \
    binutils-gold \
    cmake \
    make \
    patch \
    cvs \
    git \
    curl \
    wget \
    zip \
    unzip

RUN mkdir -p /var/www/sample-project
COPY install.sh /var/www/sample-project/install.sh
COPY start.sh /var/www/sample-project/start.sh

ENV LD_LIBRARY_PATH="/var/www/sample-project/lib:/var/www/sample-project/lib64"

RUN /var/www/sample-project/install.sh

EXPOSE 8080
EXPOSE 4443
STOPSIGNAL SIGTERM

ENTRYPOINT ["/var/www/sample-project/start.sh"]
