/*
	TMC2209 stepper driver sample for PIC32MX270F256B (XC32)

	- STEP/DIR drive with step counting, UART register access to the TMC2209
	- current limit by register (internal reference, vsense=1)
	- driver supervision (reset / undervoltage / overtemperature / short)
	- sensorless homing with StallGuard4

	See README.md for wiring and commands.
*/

#include	<xc.h>

/* same as smallest-touchpanel-ui/pic32mx/lcdtp.c: SYSCLK 40MHz, PBCLK 40MHz */
#pragma config  PMDL1WAY=OFF, IOL1WAY=OFF, FUSBIDIO=OFF, FVBUSONIO=OFF
#pragma config  FPLLIDIV=DIV_1, FPLLMUL=MUL_20, UPLLIDIV=DIV_1, UPLLEN=OFF
#pragma	config	FPLLODIV=DIV_2, FNOSC=FRCPLL, FSOSCEN=OFF, IESO=OFF
#pragma	config	POSCMOD=XT, OSCIOFNC=OFF, FPBDIV=DIV_1, FCKSM=CSECMD
#pragma	config	WDTPS=PS16384, WINDIS=OFF, FWDTEN=OFF, FWDTWINSZ=WINSZ_50
#pragma	config	DEBUG=OFF, JTAGEN=OFF, ICESEL=ICS_PGx2, PWP=OFF
#pragma	config	BWP=OFF, CP=OFF


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


/* log output: UART2 TX on RPB10, 115.2kbps */

static	void	sendlogc(W c)
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
	while ((U2STAbits.UTXBF))
		;
	U2TXREG = c;
}


static	void	sendlogs(const char *s)
{
	W	c;
	
	while ((c = *(s++)))
		sendlogc(c);
}


static	void	sendlogdec(W v)
{
	if ((v < 0)) {
		v = -v;
		sendlogc('-');
	}
	if (v >= 10)
		sendlogdec(v / 10);
	sendlogc('0' + (v % 10));
}


static	void	sendloguw(UW v)
{
	W	i;
	
	for (i=28; i>=0; i-=4)
		sendlogc("0123456789abcdef"[(v >> i) & 0xf]);
}


#define	REGSIZE	256
static	W	reglist[REGSIZE];
#define	REG_PORT	0x00
#define	REG_MOTOR	0x20
#define	REG_IRUN	0x21		/* 1..TMC_IRUN_MAX, 0:default */
#define	REG_IHOLD	0x22		/* 1..IRUN, 0:default */

/* TMC2209, Rsense=0.11ohm, vsense=1: I_rms = (CS + 1) / 32 * 0.98A */
#define	TMC_IRUN_DEF	12		/* 0.40A */
#define	TMC_IHOLD_DEF	4		/* 0.12A */
#define	TMC_IRUN_MAX	19		/* 0.61A */

#define	TMC_GCONF	0x000001c0	/* pdn_disable, mstep_reg_select, multistep_filt, internal ref */
#define	TMC_CHOPCONF	0x15030053	/* intpol, 8ustep, vsense=1, TBL=2, HSTRT=5, TOFF=3 */
#define	TMC_TPOWERDOWN	20
#define	TMC_IHOLDDELAY	6

#define	DRV_OTPW	0x0001
#define	DRV_OT	0x0002
#define	DRV_SHORT	0x003c		/* s2ga s2gb s2vsa s2vsb */
#define	DRV_T120	0x0100

#define	TICK_PER_MS	20000		/* core timer = SYSCLK / 2 */
#define	TMC_CHECK_MS	100
#define	TMC_TIMEOUT_MS	10
#define	MOVE_TIMEOUT_MS	60000		/* m, h */
#define	HOME_BLANK_STEPS	64		/* ignore StallGuard while starting */
#define	HOME_AVG	8		/* SG_RESULT moving average */

static	W	tmc_busy = 0;
static	W	tmc_ready = 0;		/* config written and verified */
static	W	tmc_hot = 0;		/* otpw/ot/t120: disabled until cooled */
static	W	tmc_short = 0;		/* short detected: disabled until reset */
static	UW	tmc_ifcnt = 0;
static	UW	tmc_ihold_irun = 0;


/*jp.pa-i.cir/map32mx2-28
PGD	RPB10	PGED2	UTX2
PGC	RPB11	PGEC2	URX2
P1	RPB8
P2__DIR	RPA0
P3__ENN	RPA1
P4__STEP	RPB0	OC3	TCK5
P5	RPB1
P6__PWM	RPB2	OC4
P7__TX	RPB15	UTX1
P8__RX	RPB13	URX1
*/


#define	LAT_DIR	LATAbits.LATA0
#define	LAT_ENN	LATAbits.LATA1


static	UW	motorpos = 0;

#define	BUFSIZE	1024
static	UB	recvbuf[BUFSIZE];
static	W	recvwpos = 0;
static	W	recvrpos = 0;


static	void	tmc_check();
static	void	setenn();


static	void	idletask()
{
	for (;;) {
		W	c;
		
		if ((U1STAbits.OERR))
			U1STA = 0x1400;
		if ((U1STAbits.FERR)) {
			c = U1RXREG;
		} else if ((U1STAbits.PERR)) {
			c = U1RXREG;
		} else if ((U1STAbits.URXDA)) {
			c = U1RXREG;
			recvbuf[recvwpos++] = c;
			if (recvwpos >= BUFSIZE)
				recvwpos = 0;
		} else
			break;
	}
	{
		static	UW	oldtmr = 0;
		UW	tmr;
		
		tmr = TMR5;
		if (oldtmr == tmr)
			;
		else if ((LAT_DIR))
			motorpos -= (tmr - oldtmr) & 0xffff;
		else
			motorpos += (tmr - oldtmr) & 0xffff;
		oldtmr = tmr;
	}
	if (tmc_busy == 0) {
		static	UW	oldtick = 0;
		
		if ((_CP0_GET_COUNT() - oldtick) >= TMC_CHECK_MS * TICK_PER_MS) {
			oldtick = _CP0_GET_COUNT();
			tmc_check();
		}
	}
}


static	void	sendmotorwithcrc(UB *buf, W len)
{
	UB	crc, v;
	W	pos, bitpos;
	
	crc = 0;
	for (pos=0; pos<len; pos++) {
		v = buf[pos];
		while ((U1STAbits.UTXBF))
			idletask();
		U1TXREG = v;
		
		for (bitpos=0; bitpos<8; bitpos++) {
			if (((crc >> 7)^(v & 1)))
				crc = (crc << 1) ^ 7;
			else
				crc = crc << 1;
			v >>= 1;
		}
	}
	while ((U1STAbits.UTXBF))
		idletask();
	U1TXREG = crc;
}


static	W	recvmotor(UB *buf, W len)
{
	UW	t0;
	W	pos;
	
	t0 = _CP0_GET_COUNT();
	for (pos=0; pos<len; pos++) {
		while (recvrpos == recvwpos) {
			idletask();
			if ((_CP0_GET_COUNT() - t0) >= TMC_TIMEOUT_MS * TICK_PER_MS)
				return -1;
		}
		buf[pos] = recvbuf[recvrpos++];
		if (recvrpos >= BUFSIZE)
			recvrpos = 0;
	}
	return 0;
}


static	UB	crc8(const UB *buf, W len)
{
	UB	crc, v;
	W	pos, bitpos;
	
	crc = 0;
	for (pos=0; pos<len; pos++) {
		v = buf[pos];
		for (bitpos=0; bitpos<8; bitpos++) {
			if (((crc >> 7)^(v & 1)))
				crc = (crc << 1) ^ 7;
			else
				crc = crc << 1;
			v >>= 1;
		}
	}
	return crc;
}


/* returns 0 on success */
static	W	tmc_read(W reg, UW *val)
{
	UB	buf[12];
	W	ret;
	
	tmc_busy++;
	recvrpos = recvwpos;
	buf[0] = 5;
	buf[1] = 0;
	buf[2] = reg & 0x7f;
	sendmotorwithcrc(buf, 3);
	ret = recvmotor(buf, 12);		/* 4 bytes echo + 8 bytes reply */
	{
		UW	t0 = _CP0_GET_COUNT();
		
		while ((_CP0_GET_COUNT() - t0) < 1 * TICK_PER_MS)	/* bus turnaround */
			idletask();
	}
	tmc_busy--;
	if (ret < 0)
		return -1;
	if ((buf[4] != 5)||(buf[5] != 0xff)||(buf[6] != (reg & 0x7f)))
		return -2;
	if (crc8(buf + 4, 7) != buf[11])
		return -3;
	*val = (((UW)buf[7]) << 24) | (((UW)buf[8]) << 16) | (((UW)buf[9]) << 8) | buf[0xa];
	return 0;
}


static	W	tmc_write(W reg, UW val)
{
	UB	buf[8];
	W	ret;
	
	tmc_busy++;
	recvrpos = recvwpos;
	buf[0] = 5;
	buf[1] = 0;
	buf[2] = (reg & 0x7f) | 0x80;
	buf[3] = val >> 24;
	buf[4] = val >> 16;
	buf[5] = val >> 8;
	buf[6] = val;
	sendmotorwithcrc(buf, 7);
	ret = recvmotor(buf, 8);		/* echo */
	tmc_busy--;
	return ret;
}


static	UW	readmotorreg(W reg)
{
	UW	v;
	
	if (tmc_read(reg, &v) < 0)
		return 0x7fffffff;
	return v;
}


static	void	tmc_log(const char *s, UW v)
{
	sendlogs("tmc ");
	sendlogs(s);
	sendlogs(" ");
	sendloguw(v);
	sendlogs("\n");
}


static	UW	tmc_calc_ihold_irun()
{
	W	irun, ihold;
	
	irun = reglist[REG_IRUN];
	if ((irun <= 0)||(irun > TMC_IRUN_MAX))
		irun = TMC_IRUN_DEF;
	ihold = reglist[REG_IHOLD];
	if (ihold <= 0)
		ihold = TMC_IHOLD_DEF;
	if (ihold > irun)
		ihold = irun;
	return (TMC_IHOLDDELAY << 16) | ((irun - 1) << 8) | (ihold - 1);
}


/* write all config and verify. returns 0 on success */
static	W	tmc_init()
{
	UW	ifcnt0, ifcnt, v;
	
	tmc_ready = 0;
	setenn();
	if (tmc_read(0x02, &ifcnt0) < 0)
		return -1;
	tmc_ihold_irun = tmc_calc_ihold_irun();
	tmc_write(0x01, 0x00000007);		/* GSTAT: clear */
	tmc_write(0x00, TMC_GCONF);
	tmc_write(0x6c, TMC_CHOPCONF);
	tmc_write(0x10, tmc_ihold_irun);
	tmc_write(0x11, TMC_TPOWERDOWN);
	if (tmc_read(0x02, &ifcnt) < 0)
		return -2;
	if (((ifcnt - ifcnt0) & 0xff) != 5)
		return -3;
	if ((tmc_read(0x00, &v) < 0)||(v != TMC_GCONF))
		return -4;
	if ((tmc_read(0x6c, &v) < 0)||(v != TMC_CHOPCONF))
		return -5;
	tmc_ifcnt = ifcnt & 0xff;
	tmc_ready = 1;
	setenn();
	return 0;
}


static	void	tmc_check()
{
	UW	v;
	W	ret;
	
	if ((tmc_ready)) {
		if ((tmc_read(0x01, &v) < 0)||((v & 7))||(tmc_read(0x02, &v) < 0)||((v & 0xff) != tmc_ifcnt)) {
			tmc_log("lost", v);		/* reset, uv_cp, drv_err or comm error */
			tmc_ready = 0;
			setenn();
		}
	}
	if (tmc_ready == 0) {
		if ((ret = tmc_init()) < 0) {
			static	W	oldret = 0;
			
			if (ret != oldret)
				tmc_log("init fail", ret);
			oldret = ret;
			return;
		}
		tmc_log("init ok", tmc_ihold_irun);
	}
	if (tmc_read(0x6f, &v) < 0)
		return;
	if ((v & DRV_SHORT)&&(tmc_short == 0)) {
		tmc_short = 1;
		tmc_log("short", v);
		setenn();
	}
	if ((v & (DRV_OTPW | DRV_OT | DRV_T120))) {
		if (tmc_hot == 0) {
			tmc_hot = 1;
			tmc_log("hot", v);
			setenn();
		}
	} else if ((tmc_hot)) {
		tmc_hot = 0;
		tmc_log("cool", v);
		setenn();
	}
	if (tmc_ihold_irun != tmc_calc_ihold_irun()) {
		tmc_ihold_irun = tmc_calc_ihold_irun();
		tmc_write(0x10, tmc_ihold_irun);
		tmc_ifcnt = (tmc_ifcnt + 1) & 0xff;
		tmc_log("current", tmc_ihold_irun);
	}
}


static	W	tmc_fault()
{
	return (tmc_ready == 0)||(tmc_hot)||(tmc_short);
}


static	void	setenn()
{
	if ((tmc_fault()))
		LAT_ENN = 1;
	else
		LAT_ENN = (reglist[REG_PORT] & 2)? 1 : 0;
}


static	void	applyreg()
{
	W	v;
	
	LAT_DIR = (reglist[REG_PORT] & 1)? 1 : 0;
	setenn();
	
	T2CON = 0x0070;
	if ((v = reglist[REG_MOTOR])) {
		PR2 = 781250 / v;
		OC3R = 1;
		OC3RS = 1;
		T2CONSET = 0x8000;
	}
}


int	main(int ac, char **av)
{
	LAT_DIR = 0;
	LAT_ENN = 1;
	ANSELA = 0;		/* all digital (RB0: T5CK, RB13: U1RX) */
	ANSELB = 0;
	TRISAbits.TRISA0 = 0;
	TRISAbits.TRISA1 = 0;
	
	RPB0R = 5;		/* OC3 */
	
	T2CON = 0x0070;		/* 1/256 */
	TMR2 = 0;
	PR2 = 0xfff0;
	
	OC3CON = 0x8003;		/* toggle */
	OC3R = 1;
	OC3RS = 1;
	
	T5CKR = 2;		/* RPB0 */
	T5CON = 0x0002;		/* ext-clk */
	TMR5 = 0;
	PR5 = 0xffff;
	T5CON = 0x8002;		/* on */
	
	T3CON = 0x0070;		/* 40MHz/256 */
	TMR3 = 0;
	PR3 = 155;		/* 1kHz */
	T3CONSET = 0x8000;
	
	RPB15R = 1;		/* UTX1 */
	U1RXR = 3;		/* RPB13 */
	TRISBbits.TRISB13 = 1;
	CNPUBbits.CNPUB13 = 0;
	
	U1MODE = 0;
	U1BRG = (10000000 / 115200) - 1;
	U1MODE = 0x8008;	/* enable N81 4(U2BRG + 1) */
	U1STA = 0x1400;
	
	U2RXR = 3;		/* RPB11 */
	TRISBbits.TRISB11 = 1;
	
	sendlogs("\nboot\n");		/* also enables UART2 */
	tmc_check();
	
	for (;;) {
#define	CMDSIZE	2048
		static	UB	cmdbuf[CMDSIZE];
		W	cmdrpos;
		W	cmdwpos;
		W	c, arg, regaddr;
		
		applyreg();
		cmdwpos = -1;
		for (;;) {
			idletask();
			if ((U2STAbits.OERR))
				U2STA = 0x1400;
			if ((U2STAbits.FERR))
				c = U2RXREG;
			if ((U2STAbits.PERR))
				c = U2RXREG;
			if (U2STAbits.URXDA == 0)
				continue;
			switch ((c = U2RXREG)) {
				case	9:
					cmdwpos = 0;
					continue;
				case	0xa:
					break;
				default:
					if ((cmdwpos >= 0)&&(cmdwpos < CMDSIZE))
						cmdbuf[cmdwpos++] = c;
					continue;
			}
			break;
		}
		
		cmdrpos = 0;
		regaddr = 0;
		while (cmdrpos < cmdwpos) {
			idletask();
			if ((U2STAbits.URXDA))
				break;
			arg = 0;
			while (cmdrpos < cmdwpos) {
				c = cmdbuf[cmdrpos++];
				if ((c >= '0')&&(c <= '9'))
					arg = ((arg << 4) & 0x00fffff0) | (c - '0');
				else if ((c >= 'A')&&(c <= 'F'))
					arg = ((arg << 4) & 0x00fffff0) | (c - 'A' + 0xa);
				else if ((c >= 'a')&&(c <= 'f'))
					arg = ((arg << 4) & 0x00fffff0) | (c - 'a' + 0xa);
				else
					break;
			}
			switch (c) {
				case	'h':
					applyreg();
					{
						UW	t0 = _CP0_GET_COUNT();
						UW	pos0 = motorpos;
						W	v, d, i, n;
						W	hist[HOME_AVG];
						
						for (i=0; i<HOME_AVG; i++)
							hist[i] = 1023;
						for (n=0; ; n++) {
							if ((v = readmotorreg(0x41)) > 1023)
								v = 1023;		/* read error */
							hist[n % HOME_AVG] = v;
							for (i=v=0; i<HOME_AVG; i++)
								v += hist[i];
							v /= HOME_AVG;
							d = (W)(motorpos - pos0);
							if (d < 0)
								d = -d;
							if ((d >= HOME_BLANK_STEPS)&&(v <= arg))
								break;
							if ((U2STAbits.URXDA)||(tmc_fault()))
								break;
							if ((_CP0_GET_COUNT() - t0) >= MOVE_TIMEOUT_MS * TICK_PER_MS) {
								sendlogs("h timeout\n");
								break;
							}
						}
						reglist[REG_MOTOR] = 0;
						applyreg();
						sendlogs("h stop sg ");
						sendlogdec(v);
						sendlogs(" pos ");
						sendlogdec(motorpos);
						sendlogs("\n");
					}
					motorpos = 0;
					break;
				case	'i':
					motorpos = 0;
					break;
				case	'm':
					applyreg();
					{
						UW	t0 = _CP0_GET_COUNT();
						
						for (;;) {
							idletask();
							if ((U2STAbits.URXDA)||(tmc_fault()))
								break;
							if ((_CP0_GET_COUNT() - t0) >= MOVE_TIMEOUT_MS * TICK_PER_MS) {
								sendlogs("m timeout\n");
								break;
							}
							if ((LAT_DIR)) {
								if ((W)motorpos <= arg)
									break;
							} else {
								if ((W)motorpos >= arg)
									break;
							}
						}
					}
					reglist[REG_MOTOR] = 0;
					applyreg();
					break;
				case	't':
					applyreg();
					while (arg-- >= 0) {
						IFS0bits.T3IF = 0;
						while (IFS0bits.T3IF == 0)
							idletask();
					}
					break;
				case	'r':
					regaddr = arg;
					break;
				case	'w':
					if ((regaddr >= 0)&&(regaddr < REGSIZE))
						reglist[regaddr] = arg;
					break;
			}
		}
	}
	return 0;
}
