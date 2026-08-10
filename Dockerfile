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

# Download the installer in a layer of its own, and keep it. That layer
# holds the bytes that were about to be executed, and no later step can
# alter it — so if a tampered installer ever reached this build, the sample
# stays available for examination even if it erased itself while running.
# The file is deliberately not removed afterwards: once it is in a layer,
# deleting it saves nothing on the wire and only makes it harder to reach.
RUN set -x &&\
	cd /tmp &&\
	wget -q -O installer.run \
		"https://ww1.microchip.com/downloads/en/DeviceDoc/xc32-${XC32VER}-full-install-linux-installer.run" &&\
	sha256sum installer.run

# The installer does its job correctly and then never exits: it tears down
# its thread pool by calling pthread_cond_destroy on a condition variable
# that still has a waiter, and since glibc 2.25 that call blocks until every
# waiter has left. Nothing ever signals the waiter, so it deadlocks in a
# futex wait after the install has already finished.
#
# Nothing can be linked or preloaded to avoid this: the .run is statically
# linked with no PT_INTERP, and the process that deadlocks is a child it
# launches itself, so an explicit loader cannot be given to it either.
# Since the install itself is complete by then, wait for the installer's own
# completion marker, stop it, and verify the result independently.
#
# This is only needed for the v1.x installers. From v2.50 onwards the
# installer exits on its own (measured on v2.50, v3.01 and v5.00), so for
# those the loop below collapses back into a plain `./installer.run ...`.
RUN set -x &&\
	cd /tmp &&\
	chmod a+x installer.run &&\
	rm -f bitrock_installer.log &&\
	( ./installer.run --mode unattended --unattendedmodeui none \
		--netservername localhost --LicenseType FreeMode & echo $! > installer.pid ) &&\
	PID=$(cat installer.pid) &&\
	for i in $(seq 1 180); do \
		grep -q 'Installation completed' bitrock_installer.log 2>/dev/null && break; \
		kill -0 "$PID" 2>/dev/null || break; \
		sleep 5; \
	done &&\
	grep -q 'Installation completed' bitrock_installer.log &&\
	{ kill "$PID" 2>/dev/null; sleep 2; kill -9 "$PID" 2>/dev/null; true; } &&\
	${XC32BIN}/xc32-gcc --version &&\
	rm -f installer.pid bitrock_installer.log


COPY makefile test.c /root/

CMD cd&&make test.hex
