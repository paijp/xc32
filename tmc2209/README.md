# TMC2209 sample

Drives a Trinamic TMC2209 stepper driver from a PIC32MX270F256B: STEP/DIR
with step counting, register access over the TMC2209's single-wire UART,
current limit by register, driver supervision and sensorless homing.

```sh
cd tmc2209
docker run --rm -v "$PWD:/work" -w /work ghcr.io/paijp/xc32:v1.42 make -f ../makefile tmc2209.hex
```

## Wiring

| PIC32 pin | function | TMC2209 |
| --- | --- | --- |
| RA0 | DIR | DIR |
| RA1 | ENN | EN (low = enabled) |
| RB0 | STEP (OC3), also counted by T5CK | STEP |
| RB15 | UTX1, 115.2kbps (typically through 1kΩ) | PDN_UART |
| RB13 | URX1 | PDN_UART |
| RB10 | UTX2, log output 115.2kbps | |
| RB11 | URX2, command input 115.2kbps | |

MS1 = MS2 = low (UART address 0). Clock configuration is the same as
`smallest-touchpanel-ui/pic32mx/lcdtp.c` (PBCLK 40MHz).

## Driver settings

Written at start-up and verified (IFCNT and read-back) before the driver is
enabled; ENN stays high until then.

| register | value | |
| --- | --- | --- |
| GCONF | `0x000001c0` | pdn_disable, mstep_reg_select, multistep_filt, internal reference |
| CHOPCONF | `0x15030053` | 8 microsteps with interpolation, vsense=1 |
| IHOLD_IRUN | from `REG_IRUN` / `REG_IHOLD` | IHOLDDELAY=6 |
| TPOWERDOWN | 20 | |

With the internal reference the current no longer depends on the VREF
potentiometer. For Rsense = 0.11Ω and vsense=1:

    I_rms = (CS + 1) / 32 * 0.98A

| IRUN | I_rms |
| --- | --- |
| 4 (IHOLD default) | 0.12A |
| 12 (IRUN default) | 0.40A |
| 16 | 0.52A |
| 19 (`TMC_IRUN_MAX`) | 0.61A |

The defaults suit a 0.6A motor; adjust `TMC_IRUN_DEF`, `TMC_IHOLD_DEF` and
`TMC_IRUN_MAX` to the motor in use.

Every 100ms the driver is checked:

- GSTAT set, IFCNT changed or no reply: the driver was reset (or lost power),
  so ENN is raised and the settings are written again.
- `otpw`, `ot` or `t120` in DRV_STATUS: ENN is raised until the flags clear.
- a short-circuit flag: ENN is raised until the PIC32 is reset.

The TMC2209 does not report its temperature as a value; only these
thresholds (120/143/150/157°C) are available.

## Commands

A command line starts with TAB and ends with LF. Numbers are hexadecimal and
precede the command letter.

| command | |
| --- | --- |
| `<n>r` | select register `n` |
| `<v>w` | write `v` to the selected register |
| `<n>m` | run until the position reaches `n` (signed), or 60s |
| `<n>h` | run until the average of 8 SG_RESULT readings ≤ `n`, or 60s, then set the position to 0 (homing) |
| `i` | set the position to 0 |
| `<n>t` | wait `n`+1 ms |

| register | |
| --- | --- |
| `0` | bit0: DIR, bit1: ENN (1 = disabled) |
| `20` | step rate; about `v`/10 microsteps/s, 0 stops |
| `21` | IRUN, 1..19 (0: default) |
| `22` | IHOLD, 1..IRUN (0: default) |

Example: home at about 1 rev/s with the default current, then move two
turns.

```
	0r1w20r4000w5fh
	0r0w20r4000wc80m
```

## Sensorless homing

StallGuard4 works only in StealthChop and needs speed: at about 0.1 rev/s
SG_RESULT is noise. The first 64 microsteps after starting are ignored,
since SG_RESULT starts low from standstill, and an average of 8 readings is
compared, since single readings jump between neighbouring values.

The level depends on the motor, the current and the speed, so measure the
free-running value first and set the threshold to about 70–80% of it.
Measured at 12V:

| motor | IRUN | speed | free running | at the stop | threshold |
| --- | --- | --- | --- | --- | --- |
| 0.95A, stopper | 16 | 1 rev/s | 250–290 | below 30 | `80` (128) |
| 0.6A, 6mm lead screw | 12 | 1 rev/s | 113–121 (average) | 60–85 | `5f` (95) |
| 0.6A, 6mm lead screw | 12 | 2 rev/s | 207–216 (average) | | `96` (150) |

With the lead screw at 1 rev/s, three homing runs stopped 33, 32 and 32
microsteps past the end. With too low a threshold (48) the stop was found
only after 10–18 turns of skipping steps at the end.

## Notes

- RB0 and RB13 are analog after reset; without `ANSELB = 0` the step counter
  and the UART receiver do not work.
- After a read, the TMC2209 needs a short pause before it accepts the next
  frame; a write sent immediately is ignored.
- Positions are compared as signed values; the counter is read every few
  steps, so an unsigned `<= 0` can be stepped over.
