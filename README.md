Amiga 3000 SDMAC and WD33C93A register test utility

This is a small utility which can identify the Amiga 3000 SDMAC version
and talk through the SDMAC to read WD33C93A registers and identify that
chip as well.

It can be built using the [amigadev/crosstools](https://hub.docker.com/r/amigadev/crosstools) Docker container and VSCode or can be built using Bebbo's gcc Amiga cross-compiler from Linux.

-------------------------------------------------------

Example output

<PRE>
9.OS322:> sdmac
Memory controller:   Ramsey-07 $f
Ramsey config:       1Mx4, 238 clock refresh
SCSI DMA Controller: SDMAC-02
SCSI Controller:     WD33C93A 00-08 microcode 09
WDC Configuration:   14.3 MHz, Polled Mode, 245 msec timeout, Async

Ramsey test:  PASS
SDMAC test:   PASS
WDC test:     PASS
</PRE>

-------------------------------------------------------

For more information on WD33C93 and compatible chips, including high resolution photos of chip packages, see the following:<BR>
<A HREF="http://eebugs.com/scsi/wd33c93/">
    http://eebugs.com/scsi/wd33c93/
</A>

Actual samples from my part stock
<PRE>
                 mcode datecode
WD33C93    00-02 00    8849 115315200102
WD33C93A   00-03 00    8909
WD33C93A   00-04 00    9040 040315200102  9109 041816200102
WD33C93A   00-06 08    9018 058564200302
AM33C93A         08    9022 9009 1048EXA A   8950 1608EXA A
WD33C93A   00-08 09    9209 F 25933A5-3503  9205 F 25890A2-3503
WD33C93B   00-02 0d    1025 E 2513427-3702
AIC-33C93B       0d    EBACA724
</PRE>
