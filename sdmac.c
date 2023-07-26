/*
 * SDMAC  Version 0.4 2023-07-25
 * -----------------------------
 * Utility to inspect and test an Amiga 3000's Super DMAC (SDMAC),
 * WD SCSI controller for correct configuration and operation.
 *
 * Copyright 2023 Chris Hooper.  This program and source may be used
 * and distributed freely, for any purpose which benefits the Amiga
 * community. Commercial use of the binary, source, or algorithms requires
 * prior written or email approval from Chris Hooper <amiga@cdh.eebugs.com>.
 * All redistributions must retain this Copyright notice.
 *
 * DISCLAIMER: THE SOFTWARE IS PROVIDED "AS-IS", WITHOUT ANY WARRANTY.
 * THE AUTHOR ASSUMES NO LIABILITY FOR ANY DAMAGE ARISING OUT OF THE USE
 * OR MISUSE OF THIS UTILITY OR INFORMATION REPORTED BY THIS UTILITY.
 */
const char *version = "\0$VER: SDMAC 0.4 ("__DATE__") � Chris Hooper";

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <libraries/expansionbase.h>
#include <clib/expansion_protos.h>
#include <inline/exec.h>
#include <inline/expansion.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <exec/interrupts.h>
#include <exec/execbase.h>
#include <exec/lists.h>

#define RAMSEY_CTRL    0x00de0003 // Ramsey control register
#define RAMSEY_VER     0x00de0043 // Ramsey version register

#define SDMAC_BASE     0x00DD0000 //        Base address of SDMAC
#define SDMAC_DAWR     0x00DD0003 // Write  DACK width register (write)
#define SDMAC_WTC      0x00DD0004 // R/W    Word Transfer Count (SDMAC-02 only)
#define SDMAC_CONTR    0x00DD000b // R/W    Control Register (byte)
#define RAMSEY_ACR     0x00DD000C // R/W    DMA Control Register (in Ramsey)
#define SDMAC_ST_DMA   0x00DD0013 // Strobe Start DMA (write 0 to start)
#define SDMAC_FLUSH    0x00DD0017 // Strobe Flush DMA FIFO
#define SDMAC_CLR_INT  0x00DD001b // Strobe Clear Interrupts
#define SDMAC_ISTR     0x00DD001f // Read   Interrupt Status Register
#define SDMAC_SP_DMA   0x00DD003f // Strobe Stop DMA
#define SDMAC_SASR_L   0x00DD0040 // Write  WDC SASR long (obsolete)
#define SDMAC_SASR_B   0x00DD0041 // Read   WDC SASR byte (obsolete)
#define SDMAC_SCMD     0x00DD0043 // R/W    WDC mapped register (byte)
#define SDMAC_SCMD_B   0x00DD0047 // R/W    WDC SCMD (byte)
#define SDMAC_SASRW    0x00DD0048 // Write  WDC SCSI register select (long)
#define SDMAC_SASR_B2  0x00DD0049 // R/W    WDC SCSI auxiliary status (byte)

#define SDMAC_CI       0x00DD0050 // R/W    Coproc. Interface (long)
#define SDMAC_CIDDR    0x00DD0054 // R/W    Coproc. Interface Data Dir (long)
#define SDMAC_SSPBDAT  0x00DD0058 // R/W    Sync. Serial Periph. Bus Data (long)
#define SDMAC_SSPBCTL  0x00DD005C // R/W    Sync. Serial Periph. Bus Ctrl (long)

#define SDMAC_WTC_ALT  (SDMAC_WTC +  0x80) // Shadow of SDMAC WTC
#define RAMSEY_ACR_ALT (RAMSEY_ACR + 0x80) // Shadow of Ramsey ACR

#define WDC_ADDR       0x00 // Write  WDC Address Register
#define WDC_OWN_ID     0x00 // R/W    Own ID
#define WDC_CONTROL    0x01 // R/W    Control
#define WDC_TPERIOD    0x02 // R/W    Timeout Period
#define WDC_SECTORS    0x03 // R/W    Total Sectors
#define WDC_HEADS      0x04 // R/W    Total Heads
#define WDC_CYLS_H     0x05 // R/W    Total Cylinders (MSB)
#define WDC_CYLS_L     0x06 // R/W    Total Cylinders (LSB)
#define WDC_LADDR3     0x07 // R/W    Logical Address (MSB)
#define WDC_LADDR2     0x08 // R/W    Logical Address (2nd)
#define WDC_LADDR1     0x09 // R/W    Logical Address (3rd)
#define WDC_LADDR0     0x0a // R/W    Logical Address (LSB)
#define WDC_SECTOR     0x0b // R/W    Sector Number
#define WDC_HEAD       0x0c // R/W    Head Number
#define WDC_CYL_H      0x0d // R/W    Cylinder Number (MSB)
#define WDC_CYL_L      0x0e // R/W    Cylinder Number (LSB)
#define WDC_LUN        0x0f // R/W    Target LUN
#define WDC_CMDPHASE   0x10 // R/W    Command Phase
#define WDC_SYNC_TX    0x11 // R/W    Synchronous Transfer Reg
#define WDC_TCOUNT2    0x12 // R/W    Transfer Count (MSB)
#define WDC_TCOUNT1    0x13 // R/W    Transfer Count (2nd)
#define WDC_TCOUNT0    0x14 // R/W    Transfer Count (LSB)
#define WDC_DST_ID     0x15 // R/W    Destination ID
#define WDC_SRC_ID     0x16 // R/W    Source ID
#define WDC_SCSI_STAT  0x17 // Read   SCSI Status
#define WDC_CMD        0x18 // R/W    Command
#define WDC_DATA       0x19 // R/W    Data
#define WDC_QUETAG     0x1a // R/W    Queue Tag (WD33C93B only)
#define WDC_AUXST      0x1f // Read   Auxiliary Status

#define WDC_INVALID_REG 0x1e // Not a real WD register

/* Interrupt status register */
#define SDMAC_ISTR_FIFOE  0x01 // FIFO Empty
#define SDMAC_ISTR_FIFOF  0x02 // FIFO Full
// #define SDMAC_ISTR_OVER   0x04 // DMA overrun interrupt
// #define SDMAC_ISTR_UNDER  0x08 // DMA underrun interrupt
#define SDMAC_ISTR_INT_P  0x10 // Enabled interrupt pending
#define SDMAC_ISTR_INT_E  0x20 // DMA done interrupt (end of process)
#define SDMAC_ISTR_INT_S  0x40 // Interrupt SCSI (peripheral interrupt)
#define SDMAC_ISTR_INT_F  0x80 // Interrupt follow

/* Control Register */
#define SDMAC_CONTR_IODX   0x01 // Reserved (0)
#define SDMAC_CONTR_DMADIR 0x02 // DMA Data direction (0=Read, 1=Write)
#define SDMAC_CONTR_INTEN  0x04 // Interrupt enable
// #define SDMAC_CONTR_PMODE  0x08 // Peripheral mode (1=SCSI)
#define SDMAC_CONTR_RESET  0x10 // Peripheral reset (Strobe)
// #define SDMAC_CONTR_TCE    0x20 // Terminal count enable
#define SDMAC_CONTR_DMAENA 0x80 // DMA Enabled

/* Coprocessor Interface Register */

/* Coprocessor Interface Data Direction Register */

/* Synchronous Serial Peripheral Bus Data Register */

/* Synchronous Serial Peripheral Bus Control Register */

#define ADDR8(x)       (volatile uint8_t *)(x)
#define ADDR16(x)      (volatile uint16_t *)(x)
#define ADDR32(x)      (volatile uint32_t *)(x)

#define ARRAY_SIZE(x)  ((sizeof (x) / sizeof ((x)[0])))
#define BIT(x)         (1U << (x))

#define INTERRUPTS_DISABLE() Disable()  /* Disable Interrupts */
#define INTERRUPTS_ENABLE()  Enable()   /* Enable Interrupts */

/* Ramsey-07 requires the processor to be in Supervisor state */
#define USE_SUPERVISOR_STATE
#ifdef USE_SUPERVISOR_STATE
#define SUPERVISOR_STATE_ENTER()    { \
                                        APTR old_stack = SuperState()
#define SUPERVISOR_STATE_EXIT()         UserState(old_stack); \
                                    }
#else
#define SUPERVISOR_STATE_ENTER()
#define SUPERVISOR_STATE_EXIT()
#endif

extern struct ExecBase *SysBase;

typedef unsigned int uint;

#define DUMP_WORDS_AND_LONGS
#ifdef DUMP_WORDS_AND_LONGS
static uint32_t regs_l[0x20];
static uint16_t regs_w[0x40];
#endif
static uint8_t  regs_b[0x80];

static void
get_raw_regs(void)
{
    int pos;
    INTERRUPTS_DISABLE();
#ifdef DUMP_WORDS_AND_LONGS
    for (pos = 0; pos < ARRAY_SIZE(regs_l); pos++)
        regs_l[pos] = *ADDR32(SDMAC_BASE + pos * 4);

    for (pos = 0; pos < ARRAY_SIZE(regs_w); pos++)
        regs_w[pos] = *ADDR16(SDMAC_BASE + pos * 2);
#endif

    for (pos = 0; pos < ARRAY_SIZE(regs_b); pos++)
        regs_b[pos] = *ADDR8(SDMAC_BASE + pos * 1);
    INTERRUPTS_ENABLE();
}

static void
dump_values(void *addr, uint length)
{
    uint32_t *data = (uint32_t *) addr;
    uint pos;
    for (pos = 0; pos < length / 4; pos++) {
        if ((pos & 0x7) == 0) {
            if (pos > 0)
                printf("\n");
            printf("%02x:", pos * 4);
        }
        printf(" %08x", *(data++));
    }
    printf("\n");
}

static void
dump_raw_sdmac_regs(void)
{
#ifdef DUMP_WORDS_AND_LONGS
    printf("\nSDMAC registers as 32-bit reads\n");
    dump_values(regs_l, sizeof (regs_l));
    printf("\nSDMAC registers as 16-bit reads\n");
    dump_values(regs_w, sizeof (regs_w));
#endif
    printf("\nSDMAC registers as 8-bit reads\n");
    dump_values(regs_b, sizeof (regs_b));
}

#define RO 1  // Read-only
#define WO 2  // Write-only
#define RW 3  // Read-write

#define BYTE 1
#define WORD 2
#define LONG 4

typedef struct {
    uint32_t          addr;   // Base physical address
    uint8_t           width;  // 1=BYTE, 2=WORD, 4=LONG
    uint8_t           type;   // 1=RO, 2=WO, 3=RW
    const char *const name;
    const char *const desc;
} reglist_t;

static const reglist_t sdmac_reglist[] = {
    { RAMSEY_CTRL,   BYTE, RW, "Ramsey_CTRL",   "Ramsey Control" },
    { RAMSEY_VER,    BYTE, RW, "Ramsey_VER",    "Ramsey Version" },
    { SDMAC_DAWR,    BYTE, WO, "SDMAC_DAWR",    "DACK width register (WO)" },
    { SDMAC_WTC,     LONG, RW, "SDMAC_WTC",     "Word Transfer Count" },
    { SDMAC_CONTR,   BYTE, RW, "SDMAC_CONTR",   "Control Register" },
    { RAMSEY_ACR,    LONG, RW, "Ramsey_ACR",    "DMA Address Register" },
    { SDMAC_ST_DMA,  BYTE, WO, "SDMAC_ST_DMA",  "Start DMA" },
    { SDMAC_FLUSH,   BYTE, WO, "SDMAC_FLUSH",   "Flush DMA FIFO" },
    { SDMAC_CLR_INT, BYTE, WO, "SDMAC_CLR_INT", "Clear Interrupts" },
    { SDMAC_ISTR,    BYTE, RO, "SDMAC_ISTR",    "Interrupt Status Register" },
    { SDMAC_SP_DMA,  BYTE, WO, "SDMAC_SP_DMA",  "Stop DMA" },
    { SDMAC_SASR_L,  LONG, WO, "SDMAC_SASR_L",  "WDC register index" },
    { SDMAC_SASR_B,  BYTE, RO, "SDMAC_SASR_B",  "WDC register index" },
    { SDMAC_SCMD,    BYTE, RW, "SDMAC_SCMD",    "WDC register data" },
    { SDMAC_SASRW,   LONG, WO, "SDMAC_SASRW",   "WDC register index" },
    { SDMAC_SASR_B2, BYTE, RW, "SDMAC_SASR_B",  "WDC register index" },
    { SDMAC_CI,      LONG, RW, "SDMAC_CI",
      "Coprocessor Interface Register" },
    { SDMAC_CIDDR,   LONG, RW, "SDMAC_CIDDR",
      "Coprocessor Interface Data Direction Register" },
    { SDMAC_SSPBCTL, LONG, RW, "SDMAC_SSPBCTL",
      "Synchronous Serial Peripheral Bus Control Register"},
    { SDMAC_SSPBDAT, LONG, RW, "SDMAC_SSPBDAT",
      "Synchronous Serial Peripheral Bus Data Register "},
};

static const reglist_t wd_reglist[] = {
    { WDC_OWN_ID,    BYTE, RW, "WDC_OWN_ID",    "Own ID" },
    { WDC_CONTROL,   BYTE, RW, "WDC_CONTROL",   "Control" },
    { WDC_TPERIOD,   BYTE, RW, "WDC_TPERIOD",   "Timeout Period" },
    { WDC_SECTORS,   BYTE, RW, "WDC_SECTORS",   "Total Sectors" },
    { WDC_HEADS,     BYTE, RW, "WDC_HEADS",     "Total Heads" },
    { WDC_CYLS_H,    BYTE, RW, "WDC_CYLS_H",    "Total Cylinders MSB" },
    { WDC_CYLS_L,    BYTE, RW, "WDC_CYLS_L",    "Total Cylinders LSB" },
    { WDC_LADDR3,    BYTE, RW, "WDC_LADDR3",    "Logical Address MSB" },
    { WDC_LADDR2,    BYTE, RW, "WDC_LADDR2",    "Logical Address 2nd" },
    { WDC_LADDR1,    BYTE, RW, "WDC_LADDR1",    "Logical Address 3rd" },
    { WDC_LADDR0,    BYTE, RW, "WDC_LADDR0",    "Logical Address LSB" },
    { WDC_SECTOR,    BYTE, RW, "WDC_SECTOR",    "Sector Number" },
    { WDC_HEAD,      BYTE, RW, "WDC_HEAD",      "Head Number" },
    { WDC_CYL_H,     BYTE, RW, "WDC_CYL_H",     "Cylinder Number MSB" },
    { WDC_CYL_L,     BYTE, RW, "WDC_CYL_L",     "Cylinder Number LSB" },
    { WDC_LUN,       BYTE, RW, "WDC_LUN",       "Target LUN" },
    { WDC_CMDPHASE,  BYTE, RW, "WDC_CMDPHASE",  "Command Phase" },
    { WDC_SYNC_TX,   BYTE, RW, "WDC_SYNC_TX",   "Synchronous Transfer" },
    { WDC_TCOUNT2,   BYTE, RW, "WDC_TCOUNT2",   "Transfer Count MSB" },
    { WDC_TCOUNT1,   BYTE, RW, "WDC_TCOUNT1",   "Transfer Count 2nd" },
    { WDC_TCOUNT0,   BYTE, RW, "WDC_TCOUNT0",   "Transfer Count LSB" },
    { WDC_DST_ID,    BYTE, RW, "WDC_DST_ID",    "Destination ID" },
    { WDC_SRC_ID,    BYTE, RW, "WDC_SRC_ID",    "Source ID" },
    { WDC_SCSI_STAT, BYTE, RO, "WDC_SCSI_STAT", "Status" },
    { WDC_CMD,       BYTE, RW, "WDC_CMD",       "Command" },
    { WDC_DATA,      BYTE, RW, "WDC_DATA",      "Data" },
    { WDC_QUETAG,    BYTE, RW, "WDC_QUETAG",    "Queue Tag" },
    { WDC_AUXST,     BYTE, RO, "WDC_AUXST",     "Auxiliary Status" },
};

static void
set_wdc_index(uint8_t value)
{
#undef USE_LONGWORD_SASR
#ifdef USE_LONGWORD_SASR
    *ADDR32(SDMAC_SASRW) = value;
#else
    *ADDR8(SDMAC_SASR_B2) = value;
#endif
}

/*
 * get_wdc_reg
 * -----------
 * This function acquires the specified WDC33C93 register, which is available
 * via a window register in the A3000 SuperDMAC.
 *
 * Which window access register to use is messy due to the A3000
 * architecture which made properly-designed 68040 cards fail to access
 * SDMAC registers as bytes, and a work-around for a 68030 write-allocate
 * cache bug.
 */
static uint8_t
get_wdc_reg(uint8_t reg)
{
    uint8_t value;
    uint8_t oindex;
    INTERRUPTS_DISABLE();
    oindex = *ADDR8(SDMAC_SASR_B);
    set_wdc_index(reg);

    value = *ADDR8(SDMAC_SCMD);

    set_wdc_index(oindex);
    INTERRUPTS_ENABLE();
    return (value);
}

static void
set_wdc_reg(uint8_t reg, uint8_t value)
{
    uint8_t oindex;
    INTERRUPTS_DISABLE();
    oindex = *ADDR8(SDMAC_SASR_B);
    set_wdc_index(reg);

    *ADDR8(SDMAC_SCMD) = value;

    set_wdc_index(oindex);
    INTERRUPTS_ENABLE();
}

static const char * const scsi_mci_codes[] = {
    "Data Out",             // 000
    "Data In",              // 001
    "Command",              // 010
    "Status",               // 011
    "Unspecified Info Out", // 100
    "Unspecified Info In",  // 101
    "Message Out",          // 110
    "Message In",           // 111
};

static const char * const wdc_cmd_codes[] = {
    "Reset",                            // 0x00
    "Abort",                            // 0x01
    "Assert ATN",                       // 0x02
    "Negate ACK",                       // 0x03
    "Disconnect",                       // 0x04
    "Reselect",                         // 0x05
    "Select-with-ATN",                  // 0x06
    "Select-without-ATN",               // 0x07
    "Select-with-ATN-and-Transfer",     // 0x08
    "Select-without-ATN-and-Transfer",  // 0x09
    "Reselect-and-Receive-Data",        // 0x0a
    "Reselect-and-Send-Data",           // 0x0b
    "Wait-for-Select-and-Receive",      // 0x0c
    "Send-Status-and-Command-Complete", // 0x0d
    "Send-Disconnect-Message",          // 0x0e
    "Set IDI",                          // 0x0f
    "Receive Command",                  // 0x10
    "Receive Data",                     // 0x11
    "Receive Message Out",              // 0x12
    "Receive Unspecified Info Out",     // 0x13
    "Send Status",                      // 0x14
    "Send Data",                        // 0x15
    "Send Message In",                  // 0x16
    "Send Unspecified Info In",         // 0x17
    "Translate Address",                // 0x18
    NULL,                               // 0x19
    NULL,                               // 0x1a
    NULL,                               // 0x1b
    NULL,                               // 0x1c
    NULL,                               // 0x1d
    NULL,                               // 0x1e
    NULL,                               // 0x1f
    "Transfer Info",                    // 0x20
};

static void
decode_wdc_scsi_status(uint8_t statusreg)
{
    printf(": ");
    uint code = statusreg & 0xf;
    switch (statusreg >> 4) {
        case 0:
            printf("Reset state, ");
            switch (code) {
                case 0:  // 0000
                    printf("Reset");
                    break;
                case 1:  // 0001
                    printf("Reset with Advanced features");
                    break;
                default:
                    printf("Unknown code %x", code);
                    break;
            }
            break;
        case 1:
            printf("Command complete, ");
            switch (code) {
                case 0:  // 0000
                    printf("Reselect as target success");
                    break;
                case 1:  // 0001
                    printf("Reselect as initiator success");
                    break;
                case 3:  // 0011
                    printf("Success, no ATN");
                    break;
                case 4:  // 0100
                    printf("Success, ATN");
                    break;
                case 5:  // 0101
                    printf("Translate Address success");
                    break;
                case 6:  // 0110
                    printf("Select-and-Transfer success");
                    break;
                default:
                    if (code & 0x8) {  // 1MCI
                        printf("Transfer Info succes: %s phase",
                               scsi_mci_codes[code & 0xf]);
                    } else {
                        printf("Unknown code %x", code);
                    }
                    break;
            }
            break;
        case 2:
            printf("Command paused/aborted, ");
            switch (code) {
                case 0:  // 0000
                    printf("Transfer Info, ACK");
                    break;
                case 1:  // 0001
                    printf("Save-Data-Pointer during Select-and-Transfer");
                    break;
                case 2:  // 0010
                    printf("Select, Reselect, or Wait-for-Select aborted");
                    break;
                case 3:  // 0011
                    printf("Receive or Send aborted, or Wait-for-select error");
                    break;
                case 4:  // 0100
                    printf("Command aborted, ATN");
                    break;
                case 5:  // 0101
                    printf("Transfer Aborted, protocol violation");
                    break;
                case 6:  // 0110
                    printf("Queue Tag mismatch, ACK");
                    break;
                case 7:  // 0111
                    printf("Dest ID %x LUN %x != resel src %x, ACK",
                           get_wdc_reg(WDC_DST_ID) & 3,
                           get_wdc_reg(WDC_LUN) & 3,
                           get_wdc_reg(WDC_SRC_ID) & 3);
                    break;
                default:
                    printf("Unknown code %x", code);
                    break;
            }
            break;
        case 4:
            printf("Command error, ");
            switch (code) {
                case 0:  // 0000
                    printf("Invalid command");
                    break;
                case 1:  // 0001
                    printf("Unexpected disconnect");
                    break;
                case 2:  // 0010
                    printf("Timeout during Select or Reselect");
                    break;
                case 3:  // 0011
                    printf("Parity error, no ATN");
                    break;
                case 4:  // 0100
                    printf("Parity error, ATN");
                    break;
                case 5:  // 0101
                    printf("Translate Address > disk boundary");
                    break;
                case 6:  // 0110
                    printf("Select-and-Transfer reselect Target != Dest %x",
                           get_wdc_reg(WDC_DST_ID) & 3);
                    break;
                case 7:  // 0111
                    printf("Status parity error during Select-and-Transfer");
                    break;
                default:
                    if (code & 0x8) {  // 1MCI
                        printf("Unexpected change requested: %s phase",
                               scsi_mci_codes[code & 0xf]);
                    } else {
                        printf("Unknown code %x", code);
                    }
                    break;
            }
            break;
        case 8:
            printf("Bus Svc Required, ");
            switch (code) {
                case 0:  // 0000
                    printf("WDC reselected as initiator");
                    break;
                case 1:  // 0001
                    printf("WDC reselected in advanced mode, ACK");
                    break;
                case 2:  // 0010
                    printf("WDC selected as target, no ATN");
                    break;
                case 3:  // 0011
                    printf("WDC selected as target, ATN");
                    break;
                case 4:  // 0100
                    printf("ATN");
                    break;
                case 5:  // 0101
                    printf("Target disconnected");
                    break;
                case 7:  // 0111
                    printf("Wait-for-Select paused, unknown target command");
                    break;
                default:
                    if (code & 0x8) {  // 1MCI
                        printf("REQ during WDC idle initiator: %s phase",
                               scsi_mci_codes[code & 0xf]);
                    } else {
                        printf("Unknown code %x", code);
                    }
                    break;
            }
            break;
        default:
            printf("Unknown Status 0x%02x", statusreg);
            break;
    }
}

static void
decode_wdc_command(uint8_t lastcmd)
{
    printf(": ");
    if ((lastcmd >= ARRAY_SIZE(wdc_cmd_codes)) ||
        (wdc_cmd_codes[lastcmd] == NULL)) {
        printf("Unknown %02x", lastcmd);
    } else {
        printf("%s", wdc_cmd_codes[lastcmd]);
    }
}

static void
show_regs(void)
{
    int pos;
    uint32_t value;
    printf("\nREG VALUE    NAME          DESCRIPTION\n");
    for (pos = 0; pos < ARRAY_SIZE(sdmac_reglist); pos++) {
        if (sdmac_reglist[pos].type == WO)
            continue; // Skip this register
        SUPERVISOR_STATE_ENTER();  // Needed for RAMSEY_VER register
        switch (sdmac_reglist[pos].width) {
            case BYTE:
                value = *ADDR8(sdmac_reglist[pos].addr);
                break;
            case WORD:
                value = *ADDR16(sdmac_reglist[pos].addr);
                break;
            default:
            case LONG:
                value = *ADDR32(sdmac_reglist[pos].addr);
                break;
        }
        SUPERVISOR_STATE_EXIT();  // Needed for RAMSEY_VER register
        printf(" %02x %0*x%*s %-13s %s\n",
               sdmac_reglist[pos].addr & 0xff,
               sdmac_reglist[pos].width * 2, value,
               8 - sdmac_reglist[pos].width * 2, "",
               sdmac_reglist[pos].name, sdmac_reglist[pos].desc);
    }

    printf("REG VALUE    NAME          DESCRIPTION\n");
    for (pos = 0; pos < ARRAY_SIZE(wd_reglist); pos++) {
        if (wd_reglist[pos].type == WO)
            continue; // Skip this register
        value = get_wdc_reg(wd_reglist[pos].addr);
        printf(" %02x %0*x%*s %-13s %s",
               wd_reglist[pos].addr & 0xff,
               wd_reglist[pos].width * 2, value,
               8 - wd_reglist[pos].width * 2, "",
               wd_reglist[pos].name, wd_reglist[pos].desc);
        switch (wd_reglist[pos].addr) {
            case WDC_CMD:
                decode_wdc_command(value);
                break;
            case WDC_SCSI_STAT:
                decode_wdc_scsi_status(value);
                break;
        }
        printf("\n");
    }
}

static int
get_sdmac_version(void)
{
    int rev = 0;
    uint32_t ovalue;
    uint32_t rvalue;
    uint8_t  istr = *ADDR8(SDMAC_ISTR);

    if (istr != SDMAC_ISTR_FIFOE)
        return (0);  // Any other status should be handled by SCSI ISR

    INTERRUPTS_DISABLE();
    ovalue = *ADDR32(SDMAC_WTC);

    /* Probe for SDMAC version */
    *ADDR32(SDMAC_WTC) = ovalue | BIT(2);
    rvalue = *ADDR32(SDMAC_WTC);

    if (rvalue & BIT(2)) {
        rev = 2;  // SDMAC-02 WTC bit 2 is writable
    } else {
        rev = 4;  // SDMAC-04 WTC bit 2 is read-only
    }

    *ADDR32(SDMAC_WTC) = ovalue;
    INTERRUPTS_ENABLE();

    return (rev);
}

static uint8_t
get_ramsey_version(void)
{
    uint8_t version;
    SUPERVISOR_STATE_ENTER();
    INTERRUPTS_DISABLE();
    version = *ADDR8(RAMSEY_VER);
    INTERRUPTS_ENABLE();
    SUPERVISOR_STATE_EXIT();
    return (version);
}

static uint8_t
get_ramsey_control(void)
{
    uint8_t control;
    SUPERVISOR_STATE_ENTER();
    INTERRUPTS_DISABLE();
    control = *ADDR8(RAMSEY_CTRL);
    INTERRUPTS_ENABLE();
    SUPERVISOR_STATE_EXIT();
    return (control);
}

static int ramsey_rev = 0;

static int
show_ramsey_version(void)
{
    uint8_t ramsey_version = get_ramsey_version();

    switch (ramsey_version) {
        case 0x7f:
            ramsey_rev = 1;
            break;
        case 0x0d:
            ramsey_rev = 4;
            break;
        case 0x0f:
            ramsey_rev = 7;
            break;
        default:
            printf("Unrecognized Ramsey version $%x -- this program only works "
                   "on Amiga 3000\n", ramsey_version);
            return (1);
    }
    printf("Memory controller:   Ramsey-0%d $%x\n", ramsey_rev, ramsey_version);
    return (0);
}

static int sdmac_version = 0;

static int
show_dmac_version(void)
{
    sdmac_version = get_sdmac_version();

    switch (sdmac_version) {
        case 2:
            printf("SCSI DMA Controller: SDMAC-%02d\n", sdmac_version);
            return (0);
        case 4:
            printf("SCSI DMA Controller: SDMAC-%02d\n", sdmac_version);
            return (0);
        default:
            printf("Unrecognized SDMAC version -%02d\n", sdmac_version);
            return (1);
    }
}

static void
show_ramsey_config(void)
{
    int     printed = 0;
    uint8_t ramsey_control = get_ramsey_control();

    printf("Ramsey config:       ");
    if (ramsey_control & BIT(0)) {
        printf("Page Mode");
        printed++;
    }
    if (ramsey_control & BIT(1)) {
        if (printed++ > 0)
            printf(", ");
        printf("Burst Mode");
    }
    if (ramsey_control & BIT(2)) {
        if (printed++ > 0)
            printf(", ");
        printf("Wrap");
    }
    if (printed++ > 0)
        printf(", ");
    if (ramsey_control & BIT(3)) {
        printf("1Mx4");
    } else if (ramsey_rev < 7) {
        if (ramsey_control & BIT(4))
            printf("256Kx4");
        else
            printf("1Mx1");
    } else {
        printf("256Kx4");
        if (ramsey_control & BIT(4))
            printf(", Skip");
    }
    switch ((ramsey_control >> 5) & 3) {
        case 0:
            printf(", 154 clock refresh\n");
            break;
        case 1:
            printf(", 238 clock refresh\n");
            break;
        case 2:
            printf(", 380 clock refresh\n");
            break;
        case 3:
            printf(", refresh disabled\n");
            break;
    }
    if (ramsey_control & (BIT(0) | BIT(1))) {  // Page or Burst mode
        printf("                     Static Column RAM required\n");
    }
}



#define LEVEL_UNKNOWN  0
#define LEVEL_WD33C93  1
#define LEVEL_WD33C93A 2
#define LEVEL_WD33C93B 3
uint        wd_level = LEVEL_WD33C93;
static const uint8_t valid_cmd_phases[] = {
    0x00, 0x10, 0x20,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
    0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
    0x50,
    0x60, 0x61,
};

static void
show_wdc_version(void)
{
    uint8_t     cvalue;
    uint8_t     ovalue;
    uint8_t     rvalue;
    uint8_t     wvalue;
    uint        pass;
    uint        errs = 0;
    const char *wd_rev = "";

    printf("SCSI Controller:     ");

    /* Verify that WD33C93 or WD33C93A can be detected */
    rvalue = get_wdc_reg(WDC_INVALID_REG);
    if (rvalue != 0xff)
        errs |= 1;
    rvalue = get_wdc_reg(WDC_AUXST);
    if (rvalue & (BIT(2) | BIT(3)))
        errs |= 2;
    rvalue = get_wdc_reg(WDC_LUN);
    if (rvalue & (BIT(3) | BIT(4) | BIT(5)))
        errs |= 4;
    rvalue = get_wdc_reg(WDC_CMDPHASE);
    for (pass = 0; pass < ARRAY_SIZE(valid_cmd_phases); pass++)
        if (rvalue == valid_cmd_phases[pass])
            break;
    if (pass == ARRAY_SIZE(valid_cmd_phases))
        errs |= 8;
    rvalue = get_wdc_reg(WDC_SCSI_STAT);
    switch (rvalue >> 4) {
        case 0:
        case 1:
        case 2:
        case 4:
        case 8:
            break;
        default:
            errs |= 0x10;
            break;
    }

    if (errs) {
        wd_level = LEVEL_UNKNOWN;
        goto fail;
    }

    wd_level = LEVEL_WD33C93;
    INTERRUPTS_DISABLE();
    ovalue = get_wdc_reg(WDC_OWN_ID);
    if (ovalue & BIT(4)) {  // HHP (Halt on Host Parity error)
        /*
         * Official way to detect the WD33C93A vs the WD33C93:
         *
         * Enable:
         *      Bit 3 - EAF Advanced Features
         *      Bit 5 - EIH Enable Immediate Halt (WD33C93A)
         *              RAF Really Advanced Features (WD33C93B)
         * and from there hit the controller with a reset.
         * If the SCSI Status Register has a value of 0x01, then
         * the WD33C93B is present.
         */
        wd_level = LEVEL_WD33C93A;
    } else {
        wvalue = ovalue | BIT(4);
        set_wdc_reg(WDC_OWN_ID, wvalue);
        rvalue = get_wdc_reg(WDC_OWN_ID);
        if (rvalue & BIT(4)) {
            wd_level = LEVEL_WD33C93A;  // Could also be 'B' part
            set_wdc_reg(WDC_OWN_ID, ovalue);
        }
    }
    INTERRUPTS_ENABLE();

    if (wd_level == LEVEL_WD33C93A) {
        /*
         * Try to detect WD33C93B by changing the QUETAG register. If the
         * new value sticks, this part must be a WD33C93B.
         */
        INTERRUPTS_DISABLE();
        cvalue = get_wdc_reg(WDC_CONTROL);
        ovalue = get_wdc_reg(WDC_QUETAG);
        for (pass = 0; pass < 4; pass++) {
            switch (pass) {
                case 0: wvalue = 0x00; break;
                case 1: wvalue = 0xff; break;
                case 2: wvalue = 0xa5; break;
                case 3: wvalue = 0x5a; break;
            }
            set_wdc_reg(WDC_QUETAG, wvalue);
            rvalue = get_wdc_reg(WDC_QUETAG);
            if (get_wdc_reg(WDC_CONTROL) != cvalue) {
                break;  // Control register should remain the same
            }
            if (rvalue != wvalue) {
                break;
            }
        }
        set_wdc_reg(WDC_QUETAG, ovalue);
        INTERRUPTS_ENABLE();
        if (pass == 4) {
            wd_level = LEVEL_WD33C93B;
            wd_rev = "";
        }
    }
    if (wd_level == LEVEL_WD33C93A) {
        /*
         * This table is mostly an assumption by me:
         *            Marking   Revision
         *   WD33C93A 00-01     A           Not released?
         *   WD33C93A 00-02     B  Seen in A2091
         *   WD33C93A 00-03     C  Vesalia has stock, seen on ebay
         *   WD33C93A 00-04     D  Common in A3000, with "PROTO" label
         *   WD33C93A 00-05     E           Not released?
         *   WD33C93A 00-06     E  Seen on ebay
         *   WD33C93A 00-07     F           Not released?
         *   WD33C93A 00-08     F  Final production, same as AM33C93A
         */

        /*
         * The below tests don't work because the older parts
         * still allow all bits in the register to be set.
         */
        wd_rev = "or AM33C93A";
#if 0
        /* Test for Revision E or Revision F */
        INTERRUPTS_DISABLE();
        ovalue = get_wdc_reg(WDC_DST_ID);
        if (ovalue & BIT(5)) {  // DF - Data Phase Direction Check Disable
            wd_rev = "00-04 or higher or AM33C93A";
        } else {
            wvalue = ovalue | BIT(5);
            set_wdc_reg(WDC_OWN_ID, wvalue);
            rvalue = get_wdc_reg(WDC_OWN_ID);
            if (rvalue & BIT(5)) {
                wd_rev = "00-04 or higher or AM33C93A";
            } else {
                /*
                 * XXX: Verify that this works to detect 00-02 part
                 *
                 *      A3000 doesn't boot with 00-02 part installed.
                 *      It just hangs at first SCSI access.
                 *
                 * We should also (but don't) fall into this category for
                 * AM33C93A because the AMD datasheet says Own ID bit 5 is
                 * "0" not used. The datasheet is wrong.
                 */
                wd_rev = "00-02 C-D";
            }
            set_wdc_reg(WDC_OWN_ID, ovalue);
        }
        INTERRUPTS_ENABLE();
#endif
    }

fail:
    switch (wd_level) {
        case LEVEL_UNKNOWN:
            printf("Not detected:");
            if (errs & 1)
                printf(" INVALID");
            if (errs & 2)
                printf(" AUXST");
            if (errs & 4)
                printf(" LUN");
            if (errs & 8)
                printf(" CMDPHASE");
            if (errs & 0x10)
                printf(" STAT");
            printf("\n");
            break;
        case LEVEL_WD33C93:
            printf("WD33C93\n");
            break;
        case LEVEL_WD33C93A:
            printf("WD33C93A %s\n", wd_rev);
            break;
        case LEVEL_WD33C93B:
            printf("WD33C93B %s\n", wd_rev);
            break;
    }
}

static void
show_wdc_config(void)
{
    const uint inclk_pal  = 28375 / 2;   // PAL frequency  28.37516 MHz
    const uint inclk_ntsc = 28636 / 2;   // NTSC frequency 28.63636 MHz
    uint       inclk      = inclk_ntsc;  // WD33C93A has ~14MHz clock on A3000
    uint       control    = get_wdc_reg(WDC_CONTROL);
    uint       tperiod    = get_wdc_reg(WDC_TPERIOD);
    uint       syncreg    = get_wdc_reg(WDC_SYNC_TX);
    uint       tperiodms  = tperiod * 80 * 1000 / inclk;
    uint       fsel_div   = 3;  // Assumption good for A3000 only (12-15 MHz)
    uint       sync_tcycles;
    uint       syncoff;

    if (0)
        inclk = inclk_pal;
#if 0
    /*
     * Unfortunately the FSEL bits are only valid across a reset.
     * After that, the register may be used for SCSI CBD size.
     */
    uint own_id   = get_wdc_reg(WDC_OWN_ID);
    uint fsel     = own_id >> 6;
    uint fsel_div = 1;

    switch (fsel) {
        case 0:
            fsel_div = 2;
            break;
        case 1:
            fsel_div = 3;
            break;
        case 2:
            fsel_div = 4;
            break;
        default:
            printf("                     Invalid WDC FSEL: %x\n", fsel);
            fsel_div = 0;
            break;
    }
#endif

    printf("WDC Configuration:   ");
    switch (control >> 5) {
        case 0:  printf("Polled Mode"); break;
        case 1:  printf("Burst Mode");  break;
        case 2:  printf("WD Bus Mode"); break;
        case 4:  printf("DMA Mode");    break;
        default: printf("Unknown Bus Mode (%d)", control >> 5);
            break;
    }
    printf(", %d msec timeout", tperiodms);

    /* syncoff = 0 for Asynchronous mode */
    syncoff = syncreg & 0xf;
    if (syncoff == 0) {
        printf(", Async");
    } else {
        uint sync_khz;
        uint freq_mul;
        printf(", Offset %d", syncoff);
        if (syncoff > 12)
            printf(" (BAD)");

        switch ((syncreg >> 4) & 0x7) {
            default:
            case 0:
            case 1: sync_tcycles = 8; break;
            case 2: sync_tcycles = 2; break;
            case 3: sync_tcycles = 3; break;
            case 4: sync_tcycles = 4; break;
            case 5: sync_tcycles = 5; break;
            case 6: sync_tcycles = 6; break;
            case 7: sync_tcycles = 7; break;
        }

        if ((wd_level == LEVEL_WD33C93B) &&
            (fsel_div == 4)) {
            uint fss_mode = (syncreg & BIT(7)) ? 1 : 0;
            freq_mul = 1 + fss_mode;
            fsel_div = 2;
        } else {
            freq_mul = 2;
        }
        sync_khz = freq_mul * inclk / fsel_div / sync_tcycles;
        printf(", Sync %d.%03d MHz", sync_khz / 1000, sync_khz % 1000);
#ifdef DEBUG_SYNC_CALC
        printf("\ninclk=%uKHz mul=%u div=%u tcycles=%u\n",
               inclk, freq_mul, fsel_div, sync_tcycles);
#endif
    }
    printf("\n");
}

static uint32_t test_values[] = {
    0x00000000, 0xffffffff, 0xa5a5a5a5, 0x5a5a5a5a, 0xc3c3c3c3, 0x3c3c3c3c,
    0xd2d2d2d2, 0x2d2d2d2d, 0x4b4b4b4b, 0xb4b4b4b4, 0xe1e1e1e1, 0x1e1e1e1e,
    0x87878787, 0x78787878, 0xffff0000, 0x0000ffff, 0xff00ff00, 0x00ff00ff,
    0xf0f0f0f0, 0x0f0f0f0f,
};

static int
test_sdmac_wtc(void)
{
    int errs = 0;
    int pos;
    uint32_t ovalue;
    uint32_t rvalue;
    uint32_t wvalue;

    INTERRUPTS_DISABLE();
    ovalue = *ADDR32(SDMAC_WTC);
    for (pos = 0; pos < ARRAY_SIZE(test_values); pos++) {
        wvalue = test_values[pos] & 0x00ffffff;
        *ADDR32(SDMAC_WTC_ALT) = wvalue;
        (void) *ADDR8(RAMSEY_CTRL);  // flush bus access
        rvalue = *ADDR32(SDMAC_WTC) & 0x00ffffff;
        if (rvalue != wvalue) {
            *ADDR32(SDMAC_WTC_ALT) = ovalue;
            INTERRUPTS_ENABLE();
            if (errs++ == 0)
                printf("FAIL\n");
            printf("  SDMAC WTC %08x != expected %08x\n", rvalue, wvalue);
            INTERRUPTS_DISABLE();
            ovalue = *ADDR32(SDMAC_WTC);
        }
    }
    *ADDR32(SDMAC_WTC_ALT) = ovalue;
    INTERRUPTS_ENABLE();
    return (errs);
}

static int
test_sdmac_sspbdat(void)
{
    int errs = 0;
    int pos;
    uint32_t ovalue;
    uint32_t rvalue;
    uint32_t wvalue;

    INTERRUPTS_DISABLE();
    ovalue = *ADDR32(SDMAC_SSPBDAT);
    for (pos = 0; pos < ARRAY_SIZE(test_values); pos++) {
        wvalue = test_values[pos] & 0x000000ff;
        *ADDR32(SDMAC_SSPBDAT) = wvalue;
        (void) *ADDR8(RAMSEY_CTRL);  // flush bus access
        rvalue = *ADDR32(SDMAC_SSPBDAT) & 0x000000ff;
        if (rvalue != wvalue) {
            *ADDR32(SDMAC_SSPBDAT) = ovalue;
            INTERRUPTS_ENABLE();
            if (errs++ == 0)
                printf("FAIL\n");
            printf("  SDMAC SSPBDAT %02x != expected %02x\n", rvalue, wvalue);
            INTERRUPTS_DISABLE();
            ovalue = *ADDR32(SDMAC_SSPBDAT);
        }
    }
    *ADDR32(SDMAC_SSPBDAT) = ovalue;
    INTERRUPTS_ENABLE();
    return (errs);
}

static int
test_ramsey_access(void)
{
    int pos;
    uint32_t ovalue;
    uint32_t wvalue;
    uint32_t rvalue;
    int errs = 0;

    printf("Ramsey test:  ");
    fflush(stdout);
    SUPERVISOR_STATE_ENTER();
    INTERRUPTS_DISABLE();
    ovalue = *ADDR32(RAMSEY_ACR);
    for (pos = 0; pos < ARRAY_SIZE(test_values); pos++) {
        wvalue = test_values[pos] & 0xfffffffc;
        *ADDR32(RAMSEY_ACR_ALT) = wvalue;
        (void) *ADDR8(RAMSEY_CTRL);  // flush bus access
        rvalue = *ADDR32(RAMSEY_ACR);
        if (rvalue != wvalue) {
            *ADDR32(RAMSEY_ACR_ALT) = ovalue;
            INTERRUPTS_ENABLE();
            if (errs++ == 0)
                printf("FAIL\n");
            printf("  Ramsey ACR %08x != expected %08x\n", rvalue, wvalue);
            INTERRUPTS_DISABLE();
            ovalue = *ADDR32(RAMSEY_ACR);
        }
    }
    *ADDR32(RAMSEY_ACR_ALT) = ovalue;
    INTERRUPTS_ENABLE();
    SUPERVISOR_STATE_EXIT();
    if (errs == 0)
        printf("PASS\n");
    return (errs);
}

static int
test_sdmac_access(void)
{
    int errs = 0;

    printf("SDMAC test:   ");
    fflush(stdout);

    switch (sdmac_version) {
        case 2:
            errs = test_sdmac_wtc();
            break;
        case 4:
            errs = test_sdmac_sspbdat();
            break;
    }

    if (errs == 0)
        printf("PASS\n");

    return (errs);
}

static int
test_wdc_access(void)
{
    int pos;
    uint8_t ovalue;
    uint8_t wvalue;
    uint8_t rvalue = 0;
    uint8_t covalue;
    uint8_t crvalue;
    int errs = 0;

    printf("WDC test:     ");
    fflush(stdout);

    /* WDC_LADDR0 should be fully writable */
    INTERRUPTS_DISABLE();
    covalue = get_wdc_reg(WDC_CONTROL);
    ovalue = get_wdc_reg(WDC_LADDR0);
    for (pos = 0; pos < ARRAY_SIZE(test_values); pos++) {
        wvalue = test_values[pos] & 0x000000ff;
        set_wdc_reg(WDC_LADDR0, wvalue);
        (void) *ADDR8(RAMSEY_CTRL);  // flush bus access
        crvalue = get_wdc_reg(WDC_CONTROL);
        if (crvalue != covalue) {
            INTERRUPTS_ENABLE();
            if (errs++ == 0)
                printf("FAIL\n");
            printf("  WDC_CONTROL %02x != expected %02x\n", crvalue, covalue);
            INTERRUPTS_DISABLE();
        }
        rvalue = get_wdc_reg(WDC_LADDR0);
        if (rvalue != wvalue) {
            set_wdc_reg(WDC_LADDR0, ovalue);
            INTERRUPTS_ENABLE();
            if (errs++ == 0)
                printf("FAIL\n");
            printf("  WDC_LADDR0 %02x != expected %02x\n", rvalue, wvalue);
            INTERRUPTS_DISABLE();
            ovalue = get_wdc_reg(WDC_LADDR0);
        }
    }
    set_wdc_reg(WDC_LADDR0, ovalue);
    INTERRUPTS_ENABLE();

    /* WDC_AUXST should be read-only */
    INTERRUPTS_DISABLE();
    covalue = get_wdc_reg(WDC_CONTROL);
    ovalue = get_wdc_reg(WDC_AUXST);
    for (pos = 0; pos < ARRAY_SIZE(test_values); pos++) {
        wvalue = test_values[pos] & 0x000000ff;
        set_wdc_reg(WDC_AUXST, wvalue);
        (void) *ADDR8(RAMSEY_CTRL);  // flush bus access
        crvalue = get_wdc_reg(WDC_CONTROL);
        if (crvalue != covalue) {
            INTERRUPTS_ENABLE();
            if (errs++ == 0)
                printf("FAIL\n");
            printf("  WDC_CONTROL %02x != expected %02x\n", crvalue, covalue);
            INTERRUPTS_DISABLE();
        }
        rvalue = get_wdc_reg(WDC_AUXST);
        if (rvalue != ovalue) {
            set_wdc_reg(WDC_AUXST, ovalue);
            INTERRUPTS_ENABLE();
            if (errs++ == 0)
                printf("FAIL\n");
            printf("  WDC_AUXST  %02x != expected %02x when %02x written\n",
                   rvalue, ovalue, wvalue);
            INTERRUPTS_DISABLE();
            ovalue = get_wdc_reg(WDC_AUXST);
        }
    }
    set_wdc_reg(WDC_AUXST, ovalue);
    INTERRUPTS_ENABLE();

    /* Undefined WDC register should be read-only and always 0xff */
    INTERRUPTS_DISABLE();
    covalue = get_wdc_reg(WDC_CONTROL);
    ovalue = get_wdc_reg(WDC_INVALID_REG);
    for (pos = 0; pos < ARRAY_SIZE(test_values); pos++) {
        wvalue = test_values[pos] & 0x000000ff;
        set_wdc_reg(WDC_INVALID_REG, wvalue);
        (void) *ADDR8(RAMSEY_CTRL);  // flush bus access
        crvalue = get_wdc_reg(WDC_CONTROL);
        if (crvalue != covalue) {
            INTERRUPTS_ENABLE();
            if (errs++ == 0)
                printf("FAIL\n");
            printf("  WDC_CONTROL %02x != expected %02x\n", crvalue, covalue);
            INTERRUPTS_DISABLE();
        }
        rvalue = get_wdc_reg(WDC_INVALID_REG);
        if (rvalue != ovalue) {
            set_wdc_reg(WDC_INVALID_REG, ovalue);
            INTERRUPTS_ENABLE();
            if (errs++ == 0)
                printf("FAIL\n");
            printf("  WDC Reg 0x1e %02x != expected %02x when %02x written\n",
                   rvalue, ovalue, wvalue);
            INTERRUPTS_DISABLE();
            ovalue = get_wdc_reg(WDC_INVALID_REG);
        }
    }
    set_wdc_reg(WDC_INVALID_REG, ovalue);
    INTERRUPTS_ENABLE();
    if (rvalue != 0xff) {
        if (errs++ == 0)
            printf("FAIL\n");
        printf("  WDC Reg 0x%02x %02x != expected 0xff\n",
               WDC_INVALID_REG, rvalue);
    }

    if (errs == 0)
        printf("PASS\n");
    return (errs);
}

int
main(int argc, char **argv)
{
    int raw_sdmac_regs = 0;
    int all_regs = 0;
    int loop_until_failure = 0;
    int arg;

    for (arg = 1; arg < argc; arg++) {
        char *ptr = argv[arg];
        if (*ptr == '-') {
            while (*(++ptr) != '\0') {
                switch (*ptr) {
                    case 'L':
                        loop_until_failure++;
                        break;
                    case 'r':
                        all_regs++;
                        break;
                    case 's':
                        raw_sdmac_regs++;
                        break;
                    case 'v':
                        printf("%s\n", version + 7);
                        exit(0);
                    default:
                        goto usage;
                }
            }
        } else {
usage:
            printf("%s\nOptions:\n"
                   "    -L Loop tests until failure\n"
                   "    -r Display registers\n"
                   "    -s Display raw SDMAC registers\n"
                   "    -v Display program version\n", version + 7);
            exit(1);
        }
    }

    show_ramsey_version();
    show_ramsey_config();
    show_dmac_version();
    show_wdc_version();
    show_wdc_config();
    printf("\n");

    do {
        if (test_ramsey_access() +
            test_sdmac_access() +
            test_wdc_access() > 0) {
            break;
        }
    } while (loop_until_failure);

    if (all_regs) {
        show_regs();
    }
    if (raw_sdmac_regs) {
        get_raw_regs();
        dump_raw_sdmac_regs();
    }

    exit(0);
}
