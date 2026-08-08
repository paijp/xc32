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

# The installer finishes its work but never exits: after writing
# "Installation completed" to /tmp/bitrock_installer.log it deadlocks in a
# futex wait and hangs forever, which would stall the image build. So run it
# in the background, wait for the completion marker, then terminate it and
# verify the toolchain independently.
RUN set -x &&\
	cd /tmp &&\
	wget -q -O installer.run \
		"https://ww1.microchip.com/downloads/en/DeviceDoc/xc32-${XC32VER}-full-install-linux-installer.run" &&\
	chmod a+x installer.run &&\
	rm -f bitrock_installer.log &&\
	( ./installer.run --mode unattended --unattendedmodeui none \
		--netservername localhost --LicenseType FreeMode & echo $! > installer.pid ) &&\
	for i in $(seq 1 180); do \
		grep -q 'Installation completed' bitrock_installer.log 2>/dev/null && break; \
		kill -0 "$(cat installer.pid)" 2>/dev/null || break; \
		sleep 5; \
	done &&\
	grep -q 'Installation completed' bitrock_installer.log &&\
	{ kill "$(cat installer.pid)" 2>/dev/null; sleep 2; kill -9 "$(cat installer.pid)" 2>/dev/null; true; } &&\
	${XC32BIN}/xc32-gcc --version &&\
	rm -f installer.run installer.pid bitrock_installer.log


COPY makefile test.c /root/

CMD cd&&make test.hex
