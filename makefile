
DEVICE	= 32MX270F256B


help:
	@awk '/#[#]/{sub(":[^#]*", "\t\t");print $0;}' makefile


%.hex:	%.c
	/opt/microchip/xc32/v1.42/bin/xc32-gcc -mprocessor=$(DEVICE) -ffreestanding -fno-hosted -nodefaultlibs -membedded-data -Wall -Wno-pointer-sign -O1 -I . -c $<'
	/opt/microchip/xc32/v1.42/bin/xc32-gcc -mprocessor=$(DEVICE) -mno-float -Wl,-Map=$*.map -o $*.out $*.o'
	/opt/microchip/xc32/v1.42/bin/xc32-bin2hex $*.out'


