FROM ubuntu:14.04

ENV DEBIAN_FRONTEND noninteractive

RUN set -x &&\
	dpkg --add-architecture i386 &&\
	apt-get update -yq

RUN set -x &&\
	apt-get install -yq --no-install-recommends curl libc6:i386 libx11-6:i386 libxext6:i386 libstdc++6:i386 libexpat1:i386 libxext6 libxrender1 libxtst6 libgtk2.0-0 libxslt1.1
	
RUN set -x &&\
	apt-get install -yq wget make
	
RUN set -x &&\
	cd /tmp &&\
	wget 'https://ww1.microchip.com/downloads/en/DeviceDoc/xc32-v1.42-full-install-linux-installer.run'

RUN set -x &&\
	cd /tmp &&\
	chmod a+x xc*.run &&\
	./xc32-v1.42-full-install-linux-installer.run --mode unattended --unattendedmodeui none --netservername localhost --LicenseType FreeMode &&\
	rm xc*.run

	
COPY makefile test.c /root/

CMD cd&&make test.hex

