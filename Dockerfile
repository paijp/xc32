# The XC32 v1.x installer completes its work correctly but never exits: it
# tears down its thread pool by calling pthread_cond_destroy on a condition
# variable that still has a waiter, and since glibc 2.25 that call blocks
# until every waiter has left. Nobody ever signals the waiter, so the
# installer deadlocks in a futex wait and the image build would hang forever.
#
# Rather than killing the installer, neutralise just that one call for the
# duration of the install. The process exits immediately afterwards, so
# skipping the destroy has no observable effect. The shim is built in a
# separate stage so gcc-multilib does not end up in the published image.
FROM debian:bookworm-slim AS shim

ENV DEBIAN_FRONTEND noninteractive

RUN set -x &&\
	apt-get update -yq &&\
	apt-get install -yq --no-install-recommends gcc-multilib libc6-dev-i386 &&\
	printf 'int pthread_cond_destroy(void *c){(void)c;return 0;}\n' > /tmp/shim.c &&\
	gcc -m32 -shared -fPIC -o /tmp/cond-destroy-shim.so /tmp/shim.c


FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND noninteractive

ARG XC32VER=v1.42
ENV XC32VER=${XC32VER}
ENV XC32BIN=/opt/microchip/xc32/${XC32VER}/bin
ENV PATH=${XC32BIN}:${PATH}

RUN set -x &&\
	dpkg --add-architecture i386 &&\
	apt-get update -yq &&\
	apt-get install -yq --no-install-recommends \
		wget ca-certificates make git \
		libc6:i386 libstdc++6:i386 libexpat1:i386 &&\
	apt-get clean &&\
	rm -rf /var/lib/apt/lists/*

COPY --from=shim /tmp/cond-destroy-shim.so /tmp/cond-destroy-shim.so

RUN set -x &&\
	cd /tmp &&\
	wget -q -O installer.run \
		"https://ww1.microchip.com/downloads/en/DeviceDoc/xc32-${XC32VER}-full-install-linux-installer.run" &&\
	chmod a+x installer.run &&\
	LD_PRELOAD=/tmp/cond-destroy-shim.so ./installer.run --mode unattended \
		--unattendedmodeui none --netservername localhost --LicenseType FreeMode &&\
	${XC32BIN}/xc32-gcc --version &&\
	rm -f installer.run cond-destroy-shim.so bitrock_installer.log


COPY makefile test.c /root/

CMD cd&&make test.hex
