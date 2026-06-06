
#include	<xc.h>


#ifndef	NULL
#define	NULL	((void*)0)
#endif

typedef signed	char	 B;
typedef signed	short	 H;
#ifdef	__unix__
typedef signed	int	 W;
#else
typedef signed	long	 W;
#endif
typedef unsigned char	UB;
typedef unsigned short	UH;
#ifdef	__unix__
typedef unsigned int	UW;
#else
typedef unsigned long	UW;
#endif

typedef	volatile B	_B;
typedef	volatile H	_H;
typedef	volatile W	_W;
typedef	volatile UB	_UB;
typedef	volatile UH	_UH;
typedef	volatile UW	_UW;


static	void	wait1us(void)
{
	long	l;
	
	for (l=15; l>0; l--)
		asm("nop");
}


static	void	wait100us(void)
{
	long	l;
	
	for (l=1500; l>0; l--)
		asm("nop");
}


void	sendlogc(W c)
{
	static	W	first = 1;
	
	if ((first)) {
		first = 0;
		
		RPB10R = 2;		/* UTX2 */
		U2MODE = 0;
		U2BRG = 86;		/* 115.4kbps */
		U2MODE = 0x8008;	/* enable N81 4(U2BRG + 1) */
		U2STA = 0x1400;
	}
	for (;;) {
		if (U2STAbits.UTXBF == 0) {
			U2TXREG = c;
			return;
		}
	}
}


void	sendlogs(const char *s)
{
	W	c;
	
	while ((c = *(s++)))
		sendlogc(c);
}


int	main(int ac, char **av)
{
	W	i;
	
	CNPUA = 0xffff;
	CNPUB = 0xffff;
	
	CNPDB = 0;
	CNPDB = 0;
	
	TRISA = 0x0000;		/* -------- ---O--OO */
	TRISB = 0x0000;		/* OOO---OO O-OOOOOO */
	
	ANSELA = 0;
	ANSELB = 0;
	
	PORTA = 0xffff;
	PORTB = 0xffff;
	
	for (i=0; i<10000; i++)
		wait100us();
	
	sendlogs("boot.\n");
	for (;;)
		;
	return 0;
}


