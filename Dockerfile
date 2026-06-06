FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND noninteractive

RUN set -x &&\
	dpkg --add-architecture i386 &&\
	apt-get update -yq &&\
	apt-get install -yq --no-install-recommends \
		wget make \
		libc6:i386 libstdc++6:i386 libexpat1:i386 &&\
	apt-get clean &&\
	rm -rf /var/lib/apt/lists/*

RUN set -x &&\
	cd /tmp &&\
	wget 'https://ww1.microchip.com/downloads/en/DeviceDoc/xc32-v1.42-full-install-linux-installer.run' &&\
	chmod a+x xc*.run &&\
	./xc32-v1.42-full-install-linux-installer.run --mode unattended --unattendedmodeui none --netservername localhost --LicenseType FreeMode &&\
	rm xc*.run


COPY makefile test.c /root/

CMD cd&&make test.hex
