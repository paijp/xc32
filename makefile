
DEVICE	?= 32MX270F256B
XC32VER	?= v1.42
XC32BIN	?= /opt/microchip/xc32/$(XC32VER)/bin


help:
	@awk '/#[#]/{sub(":[^#]*", "\t\t");print $0;}' makefile


%.hex:	%.c
	$(XC32BIN)/xc32-gcc -mprocessor=$(DEVICE) -ffreestanding -fno-hosted -nodefaultlibs -membedded-data -Wall -Wno-pointer-sign -O1 -I . -c $<
	$(XC32BIN)/xc32-gcc -mprocessor=$(DEVICE) -mno-float -Wl,-Map=$*.map -o $*.out $*.o
	$(XC32BIN)/xc32-bin2hex $*.out

