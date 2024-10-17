/*
 * SDMAC  Version 0.9 2024-10-17
 * -----------------------------
 * Utility to inspect and test an Amiga 3000's Super DMAC (SDMAC) and
 * WD SCSI controller for correct configuration and operation.
 *
 * Copyright Chris Hooper.  This program and source may be used and
 * distributed freely, for any purpose which benefits the Amiga community.
 * Commercial use of the binary, source, or algorithms requires prior
 * written or email approval from Chris Hooper <amiga@cdh.eebugs.com>.
 * All redistributions must retain this Copyright notice.
 *
 * DISCLAIMER: THE SOFTWARE IS PROVIDED "AS-IS", WITHOUT ANY WARRANTY.
 * THE AUTHOR ASSUMES NO LIABILITY FOR ANY DAMAGE ARISING OUT OF THE USE
 * OR MISUSE OF THIS UTILITY OR INFORMATION REPORTED BY THIS UTILITY.
 */
const char *version = "\0$VER: SDMAC " VER " ("__DATE__") © Chris Hooper";

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
#include <inline/timer.h>

#define ROM_BASE       0x00f80000 // Kickstart ROM base address

#define RAMSEY_CTRL    0x00de0003 // Ramsey control register
#define RAMSEY_VER     0x00de0043 // Ramsey version register

#define SDMAC_BASE     0x00DD0000 //        Base address of SDMAC
#define SDMAC_DAWR     0x00DD0003 // Write  DACK width register (write)
#define SDMAC_WTC      0x00DD0004 // R/W    Word Transfer Count (SDMAC-02 only)
#define SDMAC_CNTR     0x00DD0008 // R/W    Control Register (byte) from doc
#define SDMAC_CONTR    0x00DD000b // R/W    Control Register (byte)
#define RAMSEY_ACR     0x00DD000C // R/W    DMA Control Register (in Ramsey)
#define SDMAC_ST_DMA   0x00DD0013 // Strobe Start DMA (write 0 to start)
#define SDMAC_FLUSH    0x00DD0017 // Strobe Flush DMA FIFO
#define SDMAC_CLR_INT  0x00DD001b // Strobe Clear Interrupts
#define SDMAC_ISTR     0x00DD001f // Read   Interrupt Status Register
#define SDMAC_REVISION 0x00DD0020 // Read   Revision of ReSDMAC
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

#define SDMAC_WTC_ALT  (SDMAC_WTC +  0x100) // Shadow of SDMAC WTC
#define RAMSEY_ACR_ALT (RAMSEY_ACR + 0x100) // Shadow of Ramsey ACR
#define SDMAC_SSPBDAT_ALT (SDMAC_SSPBDAT +  0x100) // Shadow of SDMAC SSPBDAT

#define WDC_ADDR        0x00 // Write  WDC Address Register
#define WDC_OWN_ID      0x00 // R/W    Own ID
#define WDC_CONTROL     0x01 // R/W    Control
#define WDC_TPERIOD     0x02 // R/W    Timeout Period
#define WDC_CDB1        0x03 // R/W    CDB1 start (12 bytes)
#define WDC_SECTORS     0x03 // R/W    CDB1, Total Sectors
#define WDC_CDB2        0x04 // R/W    CDB2
#define WDC_HEADS       0x04 // R/W    CDB2, Total Heads
#define WDC_CYLS_H      0x05 // R/W    CDB3, Total Cylinders (MSB)
#define WDC_CYLS_L      0x06 // R/W    CDB4, Total Cylinders (LSB)
#define WDC_LADDR3      0x07 // R/W    CDB5, Logical Address (MSB)
#define WDC_LADDR2      0x08 // R/W    CDB6, Logical Address (2nd)
#define WDC_LADDR1      0x09 // R/W    CDB7, Logical Address (3rd)
#define WDC_LADDR0      0x0a // R/W    CDB8, Logical Address (LSB)
#define WDC_SECTOR      0x0b // R/W    CDB9, Sector Number
#define WDC_HEAD        0x0c // R/W    CDB10, Head Number
#define WDC_CYL_H       0x0d // R/W    CDB11, Cylinder Number (MSB)
#define WDC_CYL_L       0x0e // R/W    CDB12, Cylinder Number (LSB)
#define WDC_LUN         0x0f // R/W    Target LUN
#define WDC_CMDPHASE    0x10 // R/W    Command Phase
#define WDC_SYNC_TX     0x11 // R/W    Synchronous Transfer Reg
#define WDC_TCOUNT2     0x12 // R/W    Transfer Count (MSB)
#define WDC_TCOUNT1     0x13 // R/W    Transfer Count (2nd)
#define WDC_TCOUNT0     0x14 // R/W    Transfer Count (LSB)
#define WDC_DST_ID      0x15 // R/W    Destination ID
#define WDC_SRC_ID      0x16 // R/W    Source ID
#define WDC_SCSI_STAT   0x17 // Read   SCSI Status
#define WDC_CMD         0x18 // R/W    Command
#define WDC_DATA        0x19 // R/W    Data
#define WDC_QUETAG      0x1a // R/W    Queue Tag (WD33C93B only)
#define WDC_AUXST       0x1f // Read   Auxiliary Status

#define WDC_INVALID_REG 0x1e // Not a real WD register

#define WDC_CMD_RESET           0x00 // Soft reset the WDC controller
#define WDC_CMD_ABORT           0x01 // Abort
#define WDC_CMD_DISCONNECT      0x04 // Disconnect
#define WDC_CMD_SELECT_WITH_ATN 0x06 // Select with Attention
#define WDC_CMD_DISCONNECT_MSG  0x04 // Send Disconnect Message
#define WDC_CMD_TRANSFER_INFO   0x20 // Transfer Info
#define WDC_CMD_GET_REGISTER    0x44 // Read register (CDB1) into CDB2
#define WDC_CMD_SET_REGISTER    0x45 // Write register (CDB1) from CDB2

#define WDC_CONTROL_IDI         0x04 // Intermediate Disconnect Interrupt
#define WDC_CONTROL_EDI         0x08 // Ending Disconnect Interrupt

#define WDC_AUXST_DBR           0x01 // Data Buffer Ready
#define WDC_AUXST_PE            0x02 // Parity Error
#define WDC_AUXST_CIP           0x10 // Command in Progress (interpreting)
#define WDC_AUXST_BSY           0x20 // Busy (Level II command executing)
#define WDC_AUXST_LCI           0x40 // Last Command Ignored
#define WDC_AUXST_INT           0x80 // Interrupt Pending

#define WDC_SSTAT_SEL_COMPLETE  0x11  // Select complete (initiator)
#define WDC_SSTAT_SEL_TIMEOUT   0x42  // Select timeout

#define WDC_PHASE_DATA_OUT  0x00
#define WDC_PHASE_DATA_IN   0x01
#define WDC_PHASE_CMD       0x02
#define WDC_PHASE_STATUS    0x03
#define WDC_PHASE_BUS_FREE  0x04
#define WDC_PHASE_ARB_SEL   0x05
#define WDC_PHASE_MESG_OUT  0x06
#define WDC_PHASE_MESG_IN   0x07

#define WDC_DST_ID_DPD      0x40  // Data phase direction is IN from SCSI
#define WDC_DST_ID_SCC      0x80  // Select Command Chain (send disconnect)

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
//#define SDMAC_CONTR_PMODE  0x08 // Peripheral mode (1=SCSI)
#define SDMAC_CONTR_RESET  0x10 // WDC Peripheral reset (Strobe)
#define SDMAC_CONTR_TCE    0x20 // Terminal count enable
//#define SDMAC_CONTR_0x40   0x40 // Reserved (6)
//#define SDMAC_CONTR_0x80   0x80 // Reserved (7)
#define SDMAC_CONTR_DMAENA 0x100 // DMA Enabled

/* Coprocessor Interface Register */

/* Coprocessor Interface Data Direction Register */

/* Synchronous Serial Peripheral Bus Data Register */

/* Synchronous Serial Peripheral Bus Control Register */

#define SBIC_CLK           14200     // About 14.2 MHz in A3000
#define SBIC_TIMEOUT(val)  ((((val) * (SBIC_CLK)) / 80000) + 1)

#define ADDR8(x)       (volatile uint8_t *)(x)
#define ADDR16(x)      (volatile uint16_t *)(x)
#define ADDR32(x)      (volatile uint32_t *)(x)

#define ARRAY_SIZE(x)  ((sizeof (x) / sizeof ((x)[0])))
#define BIT(x)         (1U << (x))

/* These macros support nesting of interrupt disable state */
#define INTERRUPTS_DISABLE() if (irq_disabled++ == 0) \
                                 Disable()  /* Disable interrupts */
#define INTERRUPTS_ENABLE()  if (--irq_disabled == 0) \
                                 Enable()   /* Enable Interrupts */

#define AMIGA_BERR_DSACK 0x00de0000  // Bit7=1 for BERR on timeout, else DSACK
#define BERR_DSACK_SAVE() \
        uint8_t old_berr_dsack = *ADDR8(AMIGA_BERR_DSACK); \
        *ADDR8(AMIGA_BERR_DSACK) &= ~BIT(7);
#define BERR_DSACK_RESTORE() \
        *ADDR8(AMIGA_BERR_DSACK) = old_berr_dsack;

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

#define SCSI_TEST_UNIT_READY            0x00
typedef struct scsi_test_unit_ready {
        uint8_t opcode;
        uint8_t byte2;
        uint8_t reserved[3];
        uint8_t control;
} scsi_test_unit_ready_t;

extern struct ExecBase *SysBase;
struct Device          *TimerBase = NULL;

typedef unsigned int uint;

static uint8_t     irq_disabled      = 0;
static uint8_t     flag_debug        = 0;
static const char *sdmac_fail_reason = "";
static uint        wdc_khz;

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
    { RAMSEY_CTRL,    BYTE, RW, "Ramsey_CTRL",    "Ramsey Control" },
    { RAMSEY_VER,     BYTE, RW, "Ramsey_VER",     "Ramsey Version" },
    { SDMAC_DAWR,     BYTE, WO, "SDMAC_DAWR",     "DACK width (WO)" },
    { SDMAC_WTC,      LONG, RW, "SDMAC_WTC",      "Word Transfer Count" },
    { SDMAC_CONTR,    BYTE, RW, "SDMAC_CONTR",    "Control Register" },
    { RAMSEY_ACR,     LONG, RW, "Ramsey_ACR",     "DMA Address" },
    { SDMAC_ST_DMA,   BYTE, WO, "SDMAC_ST_DMA",   "Start DMA" },
    { SDMAC_FLUSH,    BYTE, WO, "SDMAC_FLUSH",    "Flush DMA FIFO" },
    { SDMAC_CLR_INT,  BYTE, WO, "SDMAC_CLR_INT",  "Clear Interrupts" },
    { SDMAC_ISTR,     BYTE, RO, "SDMAC_ISTR",     "Interrupt Status" },
    { SDMAC_REVISION, LONG, RO, "SDMAC_REVISION", "ReSDMAC revision" },
    { SDMAC_SP_DMA,   BYTE, WO, "SDMAC_SP_DMA",   "Stop DMA" },
    { SDMAC_SASR_L,   LONG, WO, "SDMAC_SASR_L",   "WDC register index" },
    { SDMAC_SASR_B,   BYTE, RO, "SDMAC_SASR_B",   "WDC register index" },
    { SDMAC_SCMD,     BYTE, RW, "SDMAC_SCMD",     "WDC register data" },
    { SDMAC_SASRW,    LONG, WO, "SDMAC_SASRW",    "WDC register index" },
    { SDMAC_SASR_B2,  BYTE, RW, "SDMAC_SASR_B",   "WDC register index" },
    { SDMAC_CI,       LONG, RW, "SDMAC_CI",
      "Coprocessor Interface Register" },
    { SDMAC_CIDDR,    LONG, RW, "SDMAC_CIDDR",
      "Coprocessor Interface Data Direction" },
    { SDMAC_SSPBCTL,  LONG, RW, "SDMAC_SSPBCTL",
      "Synchronous Serial Peripheral Bus Control"},
    { SDMAC_SSPBDAT,  LONG, RW, "SDMAC_SSPBDAT",
      "Synchronous Serial Peripheral Bus Data"},
};

static const reglist_t wd_reglist[] = {
    { WDC_OWN_ID,    BYTE, RW, "WDC_OWN_ID",    "Own ID" },
    { WDC_CONTROL,   BYTE, RW, "WDC_CONTROL",   "Control" },
    { WDC_TPERIOD,   BYTE, RW, "WDC_TPERIOD",   "Timeout Period" },
    { WDC_SECTORS,   BYTE, RW, "WDC_SECTORS",   "CDB1 Total Sectors" },
    { WDC_HEADS,     BYTE, RW, "WDC_HEADS",     "CDB2 Total Heads" },
    { WDC_CYLS_H,    BYTE, RW, "WDC_CYLS_H",    "CDB3 Total Cylinders MSB" },
    { WDC_CYLS_L,    BYTE, RW, "WDC_CYLS_L",    "CDB4 Total Cylinders LSB" },
    { WDC_LADDR3,    BYTE, RW, "WDC_LADDR3",    "CDB5 Logical Address MSB" },
    { WDC_LADDR2,    BYTE, RW, "WDC_LADDR2",    "CDB6 Logical Address 2nd" },
    { WDC_LADDR1,    BYTE, RW, "WDC_LADDR1",    "CDB7 Logical Address 3rd" },
    { WDC_LADDR0,    BYTE, RW, "WDC_LADDR0",    "CDB8 Logical Address LSB" },
    { WDC_SECTOR,    BYTE, RW, "WDC_SECTOR",    "CDB9 Sector Number" },
    { WDC_HEAD,      BYTE, RW, "WDC_HEAD",      "CDB10 Head Number" },
    { WDC_CYL_H,     BYTE, RW, "WDC_CYL_H",     "CDB11 Cylinder Number MSB" },
    { WDC_CYL_L,     BYTE, RW, "WDC_CYL_L",     "CDB12 Cylinder Number LSB" },
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

BOOL __check_abort_enabled = 0;       // Disable gcc clib2 ^C break handling
void __chkabort(void) { }             // Disable gcc libnix ^C break handling

/*
 * is_user_abort
 * -------------
 * Check for user break input (^C)
 */
static BOOL
is_user_abort(void)
{
    if (SetSignal(0, 0) & SIGBREAKF_CTRL_C)
        return (1);
    return (0);
}

/*
 * WD33C93B extended registers
 *
 * 0x50 live data pins D0-D7 (00 = none asserted)
 * 0x53 live control pins    (ff = none asserted)
 *      bit 0 - IO
 *      bit 1 - CD
 *      bit 2 - MSG
 * 0x55 state machine
 *      bit 3 - REQ
 *
 * Haven't located ATN, BSY, RST, ACK
 */

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

/*
 * set_wdc_reg
 * -----------
 * Writes an 8-bit WDC register value.
 */
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

/*
 * set_wdc_reg24
 * -------------
 * Writes a 24-bit WDC register value.
 */
static void
set_wdc_reg24(uint8_t reg, uint value)
{
    uint8_t oindex;
    INTERRUPTS_DISABLE();
    oindex = *ADDR8(SDMAC_SASR_B);
    set_wdc_index(reg);

    *ADDR8(SDMAC_SCMD) = (uint8_t) (value >> 24);
    *ADDR8(SDMAC_SCMD) = (uint8_t) (value >> 18);
    *ADDR8(SDMAC_SCMD) = (uint8_t) value;

    set_wdc_index(oindex);
    INTERRUPTS_ENABLE();
}

static const char * const sdmac_istr_bits[] = {
    "FIFO Empty",
    "FIFO Full",
    "RSVD:Overrun",
    "RSVD:Underrun",
    "Interrupt Pending",
    "DMA Done Interrupt",
    "SCSI Interrupt",
    "Interrupt Follow",
};

static void
decode_sdmac_istr(uint value)
{
    uint bit;
    uint printed = 0;

    for (bit = 0; bit < ARRAY_SIZE(sdmac_istr_bits); bit++) {
        if (value & BIT(bit)) {
            if (printed++)
                printf(",");
            else
                printf(":");
            printf(" %s", sdmac_istr_bits[bit]);
        }
    }
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
                            /* read */
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

typedef struct {
    uint8_t           phase;
    const char *const name;
} phaselist_t;

static const phaselist_t wdc_cmd_phases[] = {
    { 0x00, "No SCSI bus selected: D" },
    { 0x10, "Target selected: I" },
    { 0x20, "Identify message sent to target" },
    { 0x21, "Tag message code sent to target" },
    { 0x22, "Queue tag sent to target" },
    { 0x30, "Command phase stated, %u bytes transferred" },
    { 0x41, "Save-Data-Pointer message received" },
    { 0x42, "Disconnect received; but not free" },
    { 0x43, "Target disconnected after message: D" },
    { 0x44, "Reselected by target: I" },
    { 0x45, "Received matching Identify from target" },
    { 0x46, "Data transfer completed" },
    { 0x47, "Target in Receive Status phase" },
    { 0x50, "Received Status byte is in LUN register" },
    { 0x60, "Received Command-Complete message" },
    { 0x61, "Linked Command Complete" },
    { 0x70, "Received Identify message" },
    { 0x71, "Received Simple-Queue Tag message" },
};

static const char * const wdc_aux_status_bits[] = {
    "Data Buffer Ready",
    "Parity Error",
    "RSVD2",
    "RSVD3",
    "Command in Progress",
    "Busy",
    "Last Command Ignored",
    "Interrupt Pending"
};

static void
decode_wdc_aux_status(uint8_t stat)
{
    uint bit;
    uint printed = 0;

    for (bit = 0; bit < ARRAY_SIZE(wdc_aux_status_bits); bit++) {
        if (stat & BIT(bit)) {
            if (printed++)
                printf(",");
            else
                printf(":");
            printf(" %s", wdc_aux_status_bits[bit]);
        }
    }
}

static void
decode_wdc_scsi_status(uint8_t statusreg)
{
    printf(": ");
    uint code = statusreg & 0xf;
    switch (statusreg >> 4) {
        case 0:
            printf("Reset");
            switch (code) {
                case 0:  // 0000
                    break;
                case 1:  // 0001
                    printf(" with Advanced features");
                    break;
                default:
                    printf(", Unknown code %x", code);
                    break;
            }
            break;
        case 1:
            printf("Command success, ");
            switch (code) {
                case 0:  // 0000
                    printf("Reselect as target");
                    break;
                case 1:  // 0001
                    printf("Reselect as initiator");
                    break;
                case 3:  // 0011
                    printf("no ATN");
                    break;
                case 4:  // 0100
                    printf("ATN");
                    break;
                case 5:  // 0101
                    printf("Translate Address");
                    break;
                case 6:  // 0110
                    printf("Select-and-Transfer");
                    break;
                default:
                    if (code & 0x8) {  // 1MCI
                        printf("Transfer Info: %s phase",
                               scsi_mci_codes[code & 0x7]);
                    } else {
                        printf("Unknown code %x", code);
                    }
                    break;
            }
            break;
        case 2:
            printf("Command pause/abort, ");
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
                    printf("Parity error during Select-and-Transfer");
                    break;
                default:
                    if (code & 0x8) {  // 1MCI
                        printf("Unexpected change requested: %s phase",
                               scsi_mci_codes[code & 0x7]);
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
                               scsi_mci_codes[code & 0x7]);
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
    if ((lastcmd < ARRAY_SIZE(wdc_cmd_codes)) &&
        (wdc_cmd_codes[lastcmd] != NULL)) {
        printf("%s", wdc_cmd_codes[lastcmd]);
    } else if (lastcmd == WDC_CMD_GET_REGISTER) {
        printf("Get Register");
    } else if (lastcmd == WDC_CMD_SET_REGISTER) {
        printf("Set Register");
    } else {
        printf("Unknown %02x", lastcmd);
    }
}

static void
decode_wdc_cmd_phase(uint8_t phase)
{
    uint pos;
    printf(": ");
    for (pos = 0; pos < ARRAY_SIZE(wdc_cmd_phases); pos++) {
        if ((phase >= 0x30) && (phase < 0x3f) &&
            (wdc_cmd_phases[pos].phase == 0x30)) {
            printf(wdc_cmd_phases[pos].name, phase & 0xf);
        } else if (wdc_cmd_phases[pos].phase == phase) {
            printf("%s", wdc_cmd_phases[pos].name);
            break;
        }
    }
}

static void
show_wdc_pos(uint pos, uint value)
{
    printf(" %02x ", wd_reglist[pos].addr & 0xff);
    if (value > 0xff)
        printf("%.*s", wd_reglist[pos].width * 2, "--------");
    else
        printf("%0*x", wd_reglist[pos].width * 2, value);
    printf("%*s %-14s %s",
           8 - wd_reglist[pos].width * 2, "",
           wd_reglist[pos].name, wd_reglist[pos].desc);
    if (value <= 0xff) {
        switch (wd_reglist[pos].addr) {
            case WDC_CMD:
                decode_wdc_command(value);
                break;
            case WDC_SCSI_STAT:
                decode_wdc_scsi_status(value);
                break;
            case WDC_AUXST:
                decode_wdc_aux_status(value);
                break;
            case WDC_CMDPHASE:
                decode_wdc_cmd_phase(value);
                break;
        }
    }
    printf("\n");
}


static void
show_wdc_reg(uint addr, uint value)
{
    uint pos;
    for (pos = 0; pos < ARRAY_SIZE(wd_reglist); pos++) {
        if (wd_reglist[pos].addr == addr) {
            show_wdc_pos(pos, value);
            return;
        }
    }
    printf(" %02x %02x\n", addr, value);
}

static void
show_regs(uint extended)
{
    uint pos;
    uint32_t value;
    printf("\nREG VALUE    NAME           DESCRIPTION\n");
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
        printf(" %02x %0*x%*s %-14s %s",
               sdmac_reglist[pos].addr & 0xff,
               sdmac_reglist[pos].width * 2, value,
               8 - sdmac_reglist[pos].width * 2, "",
               sdmac_reglist[pos].name, sdmac_reglist[pos].desc);
        switch (sdmac_reglist[pos].addr) {
            case SDMAC_ISTR:
                decode_sdmac_istr(value);
                break;
        }
        printf("\n");
    }

    printf("REG VALUE    NAME           DESCRIPTION\n");
    for (pos = 0; pos < ARRAY_SIZE(wd_reglist); pos++) {
        INTERRUPTS_DISABLE();
        if ((wd_reglist[pos].type == WO) ||
            (wd_reglist[pos].addr == WDC_DATA)) {
            value = 0x100; // Skip this register
        } else if ((extended == 0) &&
                   (wd_reglist[pos].addr == WDC_SCSI_STAT) &&
                   (get_wdc_reg(WDC_AUXST) & WDC_AUXST_INT)) {
            /*
             * Skip reading this register when an interrupt is pending
             * because the read has a side-effect of clearing that
             * interrupt pending status.
             */
            value = 0x100; // Skip this register when interrupt pending
        } else {
            value = get_wdc_reg(wd_reglist[pos].addr);
        }
        INTERRUPTS_ENABLE();
        show_wdc_pos(pos, value);
    }
    if (extended) {
        uint addr;
        for (addr = 0x1b; addr <= 0x1e; addr++) {
            value = get_wdc_reg(addr);
            show_wdc_reg(addr, value);
        }
    }
}

/* CIA_USEC rounds up 1 value to at least one CIA tick */
#define CIA_USEC(x) (((x) * 715909 + 284091) / 1000000)

#define CIAA_TBLO        ADDR8(0x00bfe601)
#define CIAA_TBHI        ADDR8(0x00bfe701)

static uint
cia_ticks(void)
{
    uint8_t hi1;
    uint8_t hi2;
    uint8_t lo;

    hi1 = *CIAA_TBHI;
    lo  = *CIAA_TBLO;
    hi2 = *CIAA_TBHI;

    /*
     * The below operation will provide the same effect as:
     *     if (hi2 != hi1)
     *         lo = 0xff;  // rollover occurred
     */
    lo |= (hi2 - hi1);  // rollover of hi forces lo to 0xff value

    return (lo | (hi2 << 8));
}

void
cia_spin(unsigned int ticks)
{
    uint16_t start = cia_ticks();
    uint16_t now;
    uint16_t diff;

    while (ticks != 0) {
        now = cia_ticks();
        diff = start - now;
        if (diff >= ticks)
            break;
        ticks -= diff;
        start = now;
        __asm__ __volatile__("nop");
        __asm__ __volatile__("nop");
    }
}

static uint
scsi_wait(uint8_t cond, uint wait_for_set)
{
    uint    timeout;
    uint8_t auxst;

    for (timeout = 25000; timeout > 0; timeout--) {  // up to 500ms
        cia_spin(10);
        auxst = get_wdc_reg(WDC_AUXST);
        if (wait_for_set && ((auxst & cond) != 0))
            return (auxst);
        else if ((wait_for_set == 0) && ((auxst & cond) == 0))
            return (auxst);
    }
    return (0x100);  // timeout
}

static uint8_t
scsi_wait_cip(void)
{
    uint8_t auxst;
    auxst = scsi_wait(WDC_AUXST_CIP, 0);
    if (auxst == 0x100)
        printf("WDC timeout CIP\n");
    return (auxst);
}

/*
 * get_wdc_reg_extended
 * --------------------
 * Acquires the specified WDC33C93 register.
 * Access to to internal registers is supported by way of
 * the undocumented register get command.
 */
static uint8_t
get_wdc_reg_extended(uint reg)
{
    uint8_t reg3;
    uint8_t reg4;
    uint8_t value;
    uint8_t scsi_stat;

    if (reg < 0x40)
        return (get_wdc_reg(reg));

    /* This might only work with the WD33C93B parts */
    INTERRUPTS_DISABLE();
    scsi_wait_cip();
    reg3 = get_wdc_reg(WDC_CDB1);
    reg4 = get_wdc_reg(WDC_CDB2);
    set_wdc_reg(WDC_CDB1, reg);
    (void) get_wdc_reg(WDC_SCSI_STAT);
    set_wdc_reg(WDC_CMD, WDC_CMD_GET_REGISTER);
    scsi_wait_cip();
    scsi_stat = get_wdc_reg(WDC_SCSI_STAT);

    value = get_wdc_reg(WDC_CDB2);
    set_wdc_reg(WDC_CDB1, reg3);
    set_wdc_reg(WDC_CDB2, reg4);
    INTERRUPTS_ENABLE();
    if (scsi_stat != (WDC_CMD_GET_REGISTER | 0x10))
        printf("[fail: %02x]", scsi_stat);
    return (value);
}

/*
 * set_wdc_reg_extended
 * --------------------
 * Writes an 8-bit WDC register value.
 * Access to to internal registers is supported by way of
 * the undocumented register set command.
 */
static void
set_wdc_reg_extended(uint reg, uint8_t value)
{
    uint8_t reg3;
    uint8_t reg4;
    uint8_t scsi_stat;

    if (reg < 0x40) {
        set_wdc_reg(reg, value);
        return;
    }

    /* This might only work with the WD33C93B parts */
    INTERRUPTS_DISABLE();
    scsi_wait_cip();
    reg3 = get_wdc_reg(WDC_CDB1);
    reg4 = get_wdc_reg(WDC_CDB2);
    set_wdc_reg(WDC_CDB1, reg);
    set_wdc_reg(WDC_CDB2, value);
    (void) get_wdc_reg(WDC_SCSI_STAT);
    set_wdc_reg(WDC_CMD, WDC_CMD_SET_REGISTER);
    scsi_wait_cip();
    scsi_stat = get_wdc_reg(WDC_SCSI_STAT);

    set_wdc_reg(WDC_CDB1, reg3);
    set_wdc_reg(WDC_CDB2, reg4);
    INTERRUPTS_ENABLE();
    if (scsi_stat != (WDC_CMD_SET_REGISTER | 0x10))
        printf("[fail: %02x]", scsi_stat);
}


static void
scsi_hard_reset(void)
{
    uint8_t value;
    INTERRUPTS_DISABLE();
    value = *ADDR8(SDMAC_CONTR);
    *ADDR8(SDMAC_CONTR) = 0;  // Disable interrupts
    cia_spin(CIA_USEC(10));
    *ADDR8(SDMAC_CONTR) = SDMAC_CONTR_RESET;
    cia_spin(CIA_USEC(10));
    *ADDR8(SDMAC_CONTR) = 0;
    cia_spin(CIA_USEC(10));
    *ADDR8(SDMAC_CONTR) = value;
    INTERRUPTS_ENABLE();
}

static int
scsi_soft_reset(uint enhanced_features)
{
#undef RESET_WDC_OWN
#ifdef RESET_WDC_OWN
    uint8_t wdc_own;
#endif
    uint8_t wdc_new_own;
    uint auxst;

    INTERRUPTS_DISABLE();

    wdc_new_own =  0x40 |  // Input clock divisor 3 (FS0) for 14 MHz clock
                   0x00 |  // 0x08 to enable advanced features
                   0x07;   // SCSI bus id
    if (enhanced_features)
        wdc_new_own |= 0x08;  // EAF: Enable Advanced Features
    if (enhanced_features > 1)
        wdc_new_own |= 0x20;  // RAF: Really Advanced Features

    /* Clear previous command status */
    (void) get_wdc_reg(WDC_AUXST);
    (void) get_wdc_reg(WDC_SCSI_STAT);

#ifdef RESET_WDC_OWN
    wdc_own = get_wdc_reg(WDC_OWN_ID);
#endif
    set_wdc_reg(WDC_OWN_ID, wdc_new_own);
    set_wdc_reg(WDC_CMD, WDC_CMD_RESET);

    /* Wait for Command-In-Progress to clear */
    auxst = scsi_wait(WDC_AUXST_CIP, 0);

    /* WD33C93A takes about 8ms to fully complete soft reset */
    if (auxst != 0x100)
        auxst = scsi_wait(WDC_AUXST_INT, 1);

#ifdef RESET_WDC_OWN
    set_wdc_reg(WDC_OWN_ID, wdc_own);
#endif
    set_wdc_reg(WDC_SYNC_TX, 0);

    INTERRUPTS_ENABLE();

    if (auxst == 0x100) {
        if (irq_disabled)
            Enable();
        printf("reset timeout\n");
        if (irq_disabled)
            Disable();
        return (1);
    }
    return (0);
}

__attribute__((noinline))
uint8_t
get_sdmac_version(void)
{
    uint32_t ovalue;
    uint32_t rvalue;
    uint8_t  istr = *ADDR8(SDMAC_ISTR);
    uint     pass;
    uint     sdmac_version = 2;

    sdmac_fail_reason = "";
    if ((istr & SDMAC_ISTR_FIFOE) && (istr & SDMAC_ISTR_FIFOF)) {
        sdmac_fail_reason = "FIFO register test fail";
        if (flag_debug)
            printf(">> SDMAC_ISTR %02x has both FIFOE and FIFOF set\n", istr);
        return (0);  // Can not be both full and empty
    }


    /* Probe for SDMAC version */
    for (pass = 0; pass < 6; pass++) {
        uint32_t wvalue;
        switch (pass) {
            case 0: wvalue = 0x00000000; break;
            case 1: wvalue = 0xffffffff; break;
            case 2: wvalue = 0xa5a5a5a5; break;
            case 3: wvalue = 0x5a5a5a5a; break;
            case 4: wvalue = 0xc2c2c3c3; break;
            case 5: wvalue = 0x3c3c3c3c; break;
        }

        INTERRUPTS_DISABLE();
        ovalue = *ADDR32(SDMAC_WTC);
        *ADDR32(SDMAC_WTC) = wvalue;
#define FORCE_READ(x) asm volatile ("" : : "r" (x));
        /* Push out write and buffer something else on the bus */
        (void) *ADDR32(RAMSEY_VER);
        rvalue = *ADDR32(SDMAC_WTC);
        *ADDR32(SDMAC_WTC) = ovalue;
        INTERRUPTS_ENABLE();

        if (flag_debug)
            printf(">> SDMAC_WTC wvalue=%08x rvalue=%08x\n", wvalue, rvalue);

        if (rvalue == wvalue) {
            /* At least some bits of this register are read-only in SDMAC */
            if ((wvalue != 0x00000000) && (wvalue != 0xffffffff)) {
                sdmac_fail_reason = "read-only register bits not read-only";
                return (0);
            }
        } else if (((rvalue ^ wvalue) & 0x00ffffff) == 0) {
            /* SDMAC-02 */
        } else if ((rvalue & BIT(2)) == 0) {
            /* SDMAC-04 possibly */
            if (wvalue & BIT(2)) {
                /* SDMAC-04 bit 2 is always 0 */
                sdmac_version = 4;
            }
        } else {
            /* SDMAC-04 bit 2 should never be 1 */
            sdmac_fail_reason = "bit corruption in WTC register";
            return (0);
        }
    }
    return (sdmac_version);  // SDMAC-02 WTC bits 0-23 are writable
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

static uint
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

static uint8_t sdmac_version     = 0;
static uint32_t sdmac_version_rev = 0;

static uint
show_dmac_version(void)
{
    sdmac_version     = get_sdmac_version();
    sdmac_version_rev = *ADDR32(SDMAC_REVISION);  // ReSDMAC only

    switch (sdmac_version) {
        case 2:
        case 4:
            printf("SCSI DMA Controller: SDMAC-%02d", sdmac_version);
            if ((sdmac_version == 4) && ((sdmac_version_rev >> 24) == 'v') &&
                (((sdmac_version_rev >> 8) & 0xff) == '.')) {
                /*
                 * SDMAC_REVISION is in the format: 'v' <major> '.' <minor>
                 *
                 * XXX: Could also key off SSPBDAT as a differentiator for
                 *      ReSDMAC. According to the A3000+ docs, it may be only
                 *      the lower 11 bits of SSPBDAT which are r/w on SDMAC-04.
                 *      ReSDMAC implements all 32 bits as r/w.
                 */
                printf("  %c%c%c%c",
                        (char) (sdmac_version_rev >> 24),
                        (char) (sdmac_version_rev >> 16),
                        (char) (sdmac_version_rev >> 8),
                        (char) sdmac_version_rev);
            }
            printf("\n");
            return (0);
        default:
            printf("SDMAC was not detected: %s\n", sdmac_fail_reason);
            return (1);
    }
}

static uint
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
    return (0);
}



#define LEVEL_UNKNOWN  0
#define LEVEL_WD33C93  1
#define LEVEL_WD33C93A 2
#define LEVEL_WD33C93B 3
static uint    wd_level = LEVEL_WD33C93;

static uint8_t wdc_regs_saved = 0;
static uint8_t wdc_regs_store[32];
#define WDC_SAVE_MASK (BIT(WDC_OWN_ID) | BIT(WDC_CONTROL) | BIT(WDC_TPERIOD) | \
                       BIT(WDC_CMDPHASE) | BIT(WDC_SYNC_TX))

static void
scsi_save_regs(void)
{
    uint8_t pos;
    for (pos = 0; pos < ARRAY_SIZE(wdc_regs_store); pos++) {
        if (BIT(pos) & WDC_SAVE_MASK)
            wdc_regs_store[pos] = get_wdc_reg(pos);
    }
    wdc_regs_saved++;
}

static void
scsi_restore_regs(void)
{
    uint8_t pos;
    if (wdc_regs_saved--) {
        for (pos = 0; pos < ARRAY_SIZE(wdc_regs_store); pos++) {
            if (BIT(pos) & WDC_SAVE_MASK)
                set_wdc_reg(pos, wdc_regs_store[pos]);
        }
    }
}

/*
 * calc_wdc_clock
 * --------------
 * This function forces a SCSI timeout. When measured, the SCSI input
 * clock can be determined based on how long it takes for the SCSI timeout.
 * It returns the measured clock speed in KHz.
 */
static uint
calc_wdc_clock(void)
{
    uint16_t ticks;
    uint treg;
#undef DEBUG_CALC_WDC_LOCK
#ifdef DEBUG_CALC_WDC_LOCK
    uint auxst;
#endif
    uint sstat;
    uint efreq;
    uint count = 200000;
    struct EClockVal now;

    INTERRUPTS_DISABLE();
    scsi_wait_cip();
    scsi_soft_reset(0);  // set the clock divisor to 3

    sstat = get_wdc_reg(WDC_SCSI_STAT);  // clear reset status
    if (sstat == 0xff)
        return (0);  // timeout

    /* Perform a select to ID 7 LUN 7 (should cause timeout) */
    set_wdc_reg(WDC_DST_ID, 7);
    set_wdc_reg(WDC_LUN, 7);
    set_wdc_reg(WDC_SYNC_TX, 0);  // async
    set_wdc_reg(WDC_CONTROL, WDC_CONTROL_IDI | WDC_CONTROL_EDI);  // No DMA

    /*
     * Timeout Register = Tper * Ficlk / 80
     *                    Ficlk = input clock in MHz
     *                    Tper  = timeout period in milliseconds
     *
     *     Reg = Tper * Ficlk / 80
     *     Ficlk = Treg * 80 / Tper
     *
     * efreq = CIA ticks / second
     * ticks = CIA ticks per timeout
     * Tper = 1000 * ticks / efreq
     *
     * Ficlk = Treg * 80 / Tper
     * Ficlk = Treg * 80 / 1000 / ticks * efreq
     * Ficlk = efreq * Treg * 80 / 1000 / ticks
     * FiclkKHz = efreq * Treg * 80 / ticks
     *
     * timeoutspersec = efreq / ticks
     * SCSI clk = timeoutspersec * 80 * 100
     */
    treg = 10;  // any higher and there is risk of CIA timer rollover
    set_wdc_reg(WDC_TPERIOD, treg);
    (void) cia_ticks();

    /* Start select and wait for it to start */
    set_wdc_reg(WDC_CMD, WDC_CMD_SELECT_WITH_ATN);
    (void) scsi_wait_cip();

    /* Wait for select to timeout */
    ticks = cia_ticks();
    while ((*ADDR8(SDMAC_ISTR) & SDMAC_ISTR_INT_S) == 0)
        if (--count == 0)
            break;  // timeout

    ticks -= cia_ticks();  // ticks count down, not up

    /*
     * Hack: Compensate for microcode execution runtime
     *       There are minor variations in command processing time between
     *       WD33C93A 00-04, 00-06, and 00-08, but they are small enough
     *       to not affect the reported speed by even 1%.
     */
    switch (wd_level) {
        default:
        case LEVEL_WD33C93:
            ticks -= 292;  // ~400 usec
            break;
        case LEVEL_WD33C93A:
            ticks -= 380;  // ~530 usec
            break;
        case LEVEL_WD33C93B:
            ticks -= 260;  // ~360 usec
            break;
    }

#ifdef DEBUG_CALC_WDC_LOCK
    auxst = scsi_wait(WDC_AUXST_LCI | WDC_AUXST_INT, 1);
    sstat = get_wdc_reg(WDC_SCSI_STAT);
#endif

    scsi_soft_reset(0);  // set the clock divisor to 3
    INTERRUPTS_ENABLE();

    if (count == 0)
        return (0);

    if (TimerBase == NULL)
        TimerBase = (struct Device *) FindName(&SysBase->DeviceList, TIMERNAME);
    efreq = ReadEClock(&now);

    /*
     * efreq = CIA ticks / second
     * ticks = CIA ticks per timeout
     * Tper = 1000 * ticks / efreq
     * Ficlk = Treg * 80 / Tper
     * Ficlk = Treg * 80 / 1000 / ticks * efreq
     * Ficlk = efreq * Treg * 80 / 1000 / ticks
     * FiclkKHz = efreq * Treg * 80 / ticks
     *
     * timeoutspersec = efreq / ticks
     * SCSI clk = timeoutspersec * 80 * 100
     */
#ifdef DEBUG_CALC_WDC_LOCK
    printf("auxst=%x sstat=%x\n", auxst, sstat);
    printf("wdc clock ticks = %u\n", ticks);
    printf("Eclk=%u Hz\n", efreq);
    printf("timeouts/sec=%u\n", efreq / ticks);
    printf("SCSI clk=%u\n", efreq * treg * 80 / ticks);
#endif
    return (efreq * treg * 80 / ticks);
}

#define WD_DETECT_ERR_INVALID        0x0001
#define WD_DETECT_ERR_AUXST          0x0002
#define WD_DETECT_ERR_AUXST_WRITABLE 0x0004
#define WD_DETECT_ERR_SCSI_STAT      0x0008
#define WD_DETECT_ERR_CMDPHASE       0x0010
#define WD_DETECT_ERR_RESET_STATUS   0x0020

static uint
show_wdc_version(void)
{
    uint8_t     cvalue;
    uint8_t     ovalue;
    uint8_t     rvalue;
    uint8_t     wvalue;
    uint        pass;
    uint        errs = 0;
    uint        wd_rev_value = 0;
    const char *wd_rev = "";

    printf("SCSI Controller:     ");

    /* Verify that WD33C93 or WD33C93A can be detected */
    rvalue = get_wdc_reg(WDC_INVALID_REG);
    if (rvalue != 0xff)
        errs |= WD_DETECT_ERR_INVALID;
    rvalue = get_wdc_reg(WDC_AUXST);
    if (rvalue & (BIT(2) | BIT(3))) {
        if (flag_debug)
            printf("Got %02x for AUXST\n", rvalue);
        errs |= WD_DETECT_ERR_AUXST;
    }

    INTERRUPTS_DISABLE();
    rvalue = get_wdc_reg(WDC_AUXST);
    ovalue = get_wdc_reg(WDC_CMDPHASE);
    wvalue = 0xa5;
    for (pass = 0; pass < 2; pass++) {
        uint8_t rvalue2;
        set_wdc_reg(WDC_CMDPHASE, wvalue);
        rvalue2 = get_wdc_reg(WDC_AUXST);
        cvalue = get_wdc_reg(WDC_CMDPHASE);
        if (rvalue != rvalue2)
            errs |= WD_DETECT_ERR_INVALID;
        if (cvalue != wvalue)
            errs |= WD_DETECT_ERR_CMDPHASE;
        wvalue = 0x5a;
    }
    set_wdc_reg(WDC_CMDPHASE, ovalue);
    INTERRUPTS_ENABLE();

    rvalue = get_wdc_reg(WDC_SCSI_STAT);
    switch (rvalue >> 4) {
        case 0:
            if ((rvalue & 0xf) > 1)
                errs |= WD_DETECT_ERR_SCSI_STAT;
            break;
        case 1:
            if ((rvalue & 0xf) == 7)  // reserved
                errs |= WD_DETECT_ERR_SCSI_STAT;
            break;
        case 2:
            if ((rvalue & 0xf) == 6)  // reserved
                errs |= WD_DETECT_ERR_SCSI_STAT;
            break;
        case 4:
            break;
        case 8:
            if ((rvalue & 0xf) == 6)  // reserved
                errs |= WD_DETECT_ERR_SCSI_STAT;
            break;
        default:
            /* Only one status bit may be set */
            errs |= WD_DETECT_ERR_SCSI_STAT;
            break;
    }
    if ((errs & WD_DETECT_ERR_SCSI_STAT) && flag_debug)
        printf("Got %02x for SCSI_STAT\n", rvalue);

    if (errs == 0) {
        /* Attempt write of read-only AUXST */
        rvalue = get_wdc_reg(WDC_AUXST);
        set_wdc_reg(WDC_AUXST, ~rvalue);
        if (get_wdc_reg(WDC_AUXST) != rvalue) {
            set_wdc_reg(WDC_AUXST, rvalue);
            errs |= WD_DETECT_ERR_AUXST_WRITABLE;
        }
    }

    if (errs) {
        wd_level = LEVEL_UNKNOWN;
        goto fail;
    }

    INTERRUPTS_DISABLE();

    /*
     * Official way to detect the WD33C93A vs the WD33C93:
     *
     * Enable in WDC_OWN_ID:
     *      Bit 3 - EAF Advanced Features
     *      Bit 5 - EIH Enable Immediate Halt (WD33C93A)
     *              RAF Really Advanced Features (WD33C93B)
     * and from there hit the controller with a reset.
     * If the SCSI Status Register has a value of 0x01, then
     * the WD33C93A or WD33C93B is present.
     */
    scsi_soft_reset(1);
    rvalue = get_wdc_reg(WDC_SCSI_STAT);
    if (rvalue == 0x00) {
        /* Does not support advanced features */
        wd_level = LEVEL_WD33C93;
    } else if (rvalue == 0x01) {
        /* Supports advanced features: A or B part */
        wd_level = LEVEL_WD33C93A;
    } else {
        /* Bad part? */
        wd_level = LEVEL_UNKNOWN;
        errs |= WD_DETECT_ERR_RESET_STATUS;
        INTERRUPTS_ENABLE();
        goto fail;
    }

    if (wd_level == LEVEL_WD33C93A) {
        /*
         * Could be WD33C93A or WD33C93B or compatible chip at this point.
         *
         * Try to detect WD33C93B by changing the QUETAG register. If the
         * new value sticks, this part is a WD33C93B.
         */
        cvalue = get_wdc_reg(WDC_CONTROL);
        ovalue = get_wdc_reg(WDC_QUETAG);
        for (pass = 4; pass > 0; pass--) {
            switch (pass) {
                case 4: wvalue = 0x00; break;
                case 3: wvalue = 0xff; break;
                case 2: wvalue = 0xa5; break;
                case 1: wvalue = 0x5a; break;
            }
            set_wdc_reg(WDC_QUETAG, wvalue);
            if (get_wdc_reg(WDC_CONTROL) != cvalue) {
                break;  // Control register should remain the same
            }
            rvalue = get_wdc_reg(WDC_QUETAG);
            if (rvalue != wvalue) {
                break;
            }
        }
        set_wdc_reg(WDC_QUETAG, ovalue);
        if (pass == 0) {
            /* All QUETAG register tests passed */
            wd_level = LEVEL_WD33C93B;
        }
    }
    if ((wd_level == LEVEL_WD33C93A) ||
        (wd_level == LEVEL_WD33C93B)) {
        /*
         * This table is mostly assumptions by me:
         *          Marking mcode  HW Revision
         *   WD33C93A 00-01        A  / Not released? /
         *   WD33C93A 00-02        B  / Not released? /
         *   WD33C93A 00-03 06     C  Seen on ebay and on A2091
         *   WD33C93A 00-04 06     D  Common in A3000
         *   WD33C93A 00-05 07     E  / Not released? /
         *   WD33C93A 00-06 08     E  Seen on ebay
         *   WD33C93A 00-07 08     F  / Not released? /
         *   WD33C93A 00-08 09     F  Final production; same as AM33C93A?
         *
         * Actual samples from my part stock
         *                  mcode datecode
         *   WD33C93  00-02 00    8849 115315200102
         *   WD33C93A 00-03 00    8909
         *   WD33C93A 00-04 00    9040 040315200102  9109 041816200102
         *   WD33C93A 00-06 08    9018 058564200302
         *   AM33C93A       08    9022 9009 1048EXA A  8950 1608EXA A
         *   WD33C93A 00-08 09    9209 F 25933A5-3503  9205 F 25890A2-3503
         *   WD33C93B 00-02 0d    1025 E 2513427-3702
         */

        /*
         * The microcode version can be obtained by enabling the
         * RAF bit in OWN_ID and then forcing a reset. The value
         * is stored in the CDB1 register.
         */
        scsi_soft_reset(2);
        wd_rev_value = get_wdc_reg(WDC_CDB1);
    }
    scsi_soft_reset(0);

    if (wd_level == LEVEL_WD33C93A) {
        switch (wd_rev_value) {
            case 0x00:
                wd_rev = " 00-04 or 00-03";
                break;
            case 0x08:
                wd_rev = " 00-06 or AM33C93A";
                break;
            case 0x09:
                wd_rev = " 00-08";
                break;
        }
    }
    if (wd_level == LEVEL_WD33C93B) {
        wd_rev = "";
    }
    INTERRUPTS_ENABLE();

fail:
    switch (wd_level) {
        case LEVEL_UNKNOWN:
            printf("Not detected:");
            if (errs & WD_DETECT_ERR_INVALID)
                printf(" INVALID");
            if (errs & WD_DETECT_ERR_AUXST)
                printf(" AUXST");
            if (errs & WD_DETECT_ERR_CMDPHASE)
                printf(" CMDPHASE");
            if (errs & WD_DETECT_ERR_SCSI_STAT)
                printf(" SCSI_STAT");
            if (errs & WD_DETECT_ERR_RESET_STATUS)
                printf(" RESET_STATUS");
            break;
        case LEVEL_WD33C93:
            printf("WD33C93");
            break;
        case LEVEL_WD33C93A:
            printf("WD33C93A%s microcode %02x", wd_rev, wd_rev_value);
            break;
        case LEVEL_WD33C93B:
            printf("WD33C93B microcode %02x", wd_rev_value);
            break;
    }
    if (wd_level != LEVEL_UNKNOWN) {
        wdc_khz = calc_wdc_clock() + 50;
        if (wdc_khz >= 1000) {
            printf(", %u.%u MHz", wdc_khz / 1000, (wdc_khz % 1000) / 100);
        }
    }
    printf("\n");
    return (errs);
}

static uint
show_wdc_config(void)
{
    const uint inclk_pal  = 28375 / 2;   // PAL frequency  28.37516 MHz
    const uint inclk_ntsc = 28636 / 2;   // NTSC frequency 28.63636 MHz
    uint       inclk      = inclk_ntsc;  // WD33C93A has ~14MHz clock on A3000
    uint       control    = wdc_regs_store[WDC_CONTROL];
    uint       tperiod    = wdc_regs_store[WDC_TPERIOD];
    uint       syncreg    = wdc_regs_store[WDC_SYNC_TX];
    uint       tperiodms;
    uint       fsel_div   = 3;  // Assumption good for A3000 only (12-15 MHz)
    uint       sync_tcycles;
    uint       syncoff;

    if ((wdc_khz > 14000) && (wdc_khz < 14250))
        inclk = inclk_pal;
    else if ((wdc_khz >= 14250) && (wdc_khz < 14450))
        inclk = inclk_ntsc;
    else
        inclk = wdc_khz;
    tperiodms  = tperiod * 80 * 1000 / inclk;
#if 0
    /*
     * Unfortunately the FSEL bits are only valid across a reset.
     * After that, the register may be used for SCSI CBD size.
     *
     * We could probably determine what FSEL was set to by first
     * forcing a timeout of a specific period, and measuring how long
     * that took, then calculate the SCSI clock using our own
     * FSEL, and then from that would be able to report what FSEL
     * was originally programmed to.
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

        if (wd_level == LEVEL_WD33C93) {
            sync_tcycles = (syncreg >> 4) & 0x7;
        } else {
            /* WD33C93A and WD33C93B */
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
    printf("\n\n");
    return (0);
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
    ovalue = *ADDR32(SDMAC_WTC_ALT);
    for (pos = 0; pos < ARRAY_SIZE(test_values); pos++) {
        wvalue = test_values[pos] & 0x00ffffff;
        *ADDR32(SDMAC_WTC_ALT) = wvalue;
//      (void) *ADDR32(ROM_BASE);  // flush bus access
        (void) *ADDR32(RAMSEY_VER);
        rvalue = *ADDR32(SDMAC_WTC) & 0x00ffffff;
        if (rvalue != wvalue) {
            *ADDR32(SDMAC_WTC_ALT) = ovalue;
            INTERRUPTS_ENABLE();
            if (errs++ == 0)
                printf("FAIL\n");
            printf("  SDMAC WTC %08x != expected %08x\n", rvalue, wvalue);
            INTERRUPTS_DISABLE();
            ovalue = *ADDR32(SDMAC_WTC_ALT);
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
        *ADDR32(SDMAC_SSPBDAT_ALT) = wvalue;
        (void) *ADDR32(ROM_BASE);  // flush bus access
        rvalue = *ADDR32(SDMAC_SSPBDAT) & 0x000000ff;
        if (rvalue != wvalue) {
            *ADDR32(SDMAC_SSPBDAT_ALT) = ovalue;
            INTERRUPTS_ENABLE();
            if (errs++ == 0)
                printf("FAIL\n");
            printf("  SDMAC SSPBDAT %02x != expected %02x\n", rvalue, wvalue);
            INTERRUPTS_DISABLE();
            ovalue = *ADDR32(SDMAC_SSPBDAT);
        }
    }
    *ADDR32(SDMAC_SSPBDAT_ALT) = ovalue;
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
        (void) *ADDR32(ROM_BASE);  // flush bus access
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
    scsi_wait_cip();
    covalue = get_wdc_reg(WDC_CONTROL);
    ovalue = get_wdc_reg(WDC_LADDR0);
    for (pos = 0; pos < ARRAY_SIZE(test_values); pos++) {
        wvalue = test_values[pos] & 0x000000ff;
        set_wdc_reg(WDC_LADDR0, wvalue);
        (void) *ADDR32(ROM_BASE);  // flush bus access
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
            scsi_wait_cip();
            ovalue = get_wdc_reg(WDC_LADDR0);
        }
        if (errs > 6)
            break;
    }
    set_wdc_reg(WDC_LADDR0, ovalue);
    INTERRUPTS_ENABLE();

    /* WDC_AUXST should be read-only */
    INTERRUPTS_DISABLE();
    scsi_wait_cip();
    covalue = get_wdc_reg(WDC_CONTROL);
    ovalue = get_wdc_reg(WDC_AUXST);
    for (pos = 0; pos < ARRAY_SIZE(test_values); pos++) {
        wvalue = test_values[pos] & 0x000000ff;
        set_wdc_reg(WDC_AUXST, wvalue);
        (void) *ADDR32(ROM_BASE);  // flush bus access
        crvalue = get_wdc_reg(WDC_CONTROL);
        if (crvalue != covalue) {
            INTERRUPTS_ENABLE();
            if (errs++ == 0)
                printf("FAIL\n");
            printf("  WDC_CONTROL %02x != expected %02x\n", crvalue, covalue);
            INTERRUPTS_DISABLE();
            scsi_wait_cip();
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
            scsi_wait_cip();
            ovalue = get_wdc_reg(WDC_AUXST);
        }
        if (errs > 6)
            break;
    }
    set_wdc_reg(WDC_AUXST, ovalue);
    INTERRUPTS_ENABLE();

    /* Undefined WDC register should be read-only and always 0xff */
    INTERRUPTS_DISABLE();
    scsi_wait_cip();
    covalue = get_wdc_reg(WDC_CONTROL);
    ovalue = get_wdc_reg(WDC_INVALID_REG);
    for (pos = 0; pos < ARRAY_SIZE(test_values); pos++) {
        wvalue = test_values[pos] & 0x000000ff;
        set_wdc_reg(WDC_INVALID_REG, wvalue);
        (void) *ADDR32(ROM_BASE);  // flush bus access
        crvalue = get_wdc_reg(WDC_CONTROL);
        if (crvalue != covalue) {
            INTERRUPTS_ENABLE();
            if (errs++ == 0)
                printf("FAIL\n");
            printf("  WDC_CONTROL %02x != expected %02x\n", crvalue, covalue);
            INTERRUPTS_DISABLE();
            scsi_wait_cip();
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
            scsi_wait_cip();
            ovalue = get_wdc_reg(WDC_INVALID_REG);
        }
        if (errs > 6)
            break;
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

static void
scsi_set_transfer_len(uint len)
{
    set_wdc_reg24(WDC_TCOUNT2, len);
}

static uint8_t
scsi_select(uint8_t target)
{
    uint8_t sstat = 0;
    uint    auxst;
//  uint    xfer_dir = WDC_DST_ID_DPD;  // read from device
    uint    xfer_dir = 0;  // write to device

    set_wdc_reg(WDC_DST_ID, (target & 0xf) | xfer_dir);
    set_wdc_reg(WDC_SRC_ID, 0);
    set_wdc_reg(WDC_LUN, target >> 8);
    set_wdc_reg(WDC_SYNC_TX, 0);  // async

    set_wdc_reg(WDC_TPERIOD, SBIC_TIMEOUT(250));  // ~250ms
//  scsi_set_transfer_len(6);    // WD will get count after select
    scsi_set_transfer_len(0);    // WD will get count after select
    set_wdc_reg(WDC_CMD, WDC_CMD_SELECT_WITH_ATN);

    /* Wait for Command-In-Progress to clear */
    auxst = scsi_wait_cip();
    if (auxst == 0x100) {
        printf("SS1  astat=%02x sstat=%02x",
               get_wdc_reg(WDC_AUXST), get_wdc_reg(WDC_SCSI_STAT));
        return (0xff);
    }

    /* Wait for Select to complete */
    auxst = scsi_wait(WDC_AUXST_LCI | WDC_AUXST_INT, 1);
    sstat = get_wdc_reg(WDC_SCSI_STAT);
    INTERRUPTS_ENABLE();
    if (auxst & 0x100)
        printf("  timeout: no LCI or INT\n");
    if (auxst & WDC_AUXST_PE)
        printf("  ParityError:%02x\n", sstat);
    if (auxst & WDC_AUXST_CIP)
        printf("  CIP:%02x\n", sstat);
    if (auxst & WDC_AUXST_LCI)
        printf("  LCI:%02x\n", sstat);
    if (auxst & WDC_AUXST_BSY)
        printf("  BSY:%02x\n", sstat);
    INTERRUPTS_DISABLE();
    return (sstat);
}

#if 0
static void
scsi_disconnect(void)
{
    uint8_t auxst;

    scsi_wait_cip();
    set_wdc_reg(WDC_CMD, WDC_CMD_DISCONNECT);
    scsi_wait_cip();

    auxst = scsi_wait(WDC_AUXST_INT, 1);
    if (auxst == 0x100)
        printf("WDC timeout disconnect\n");
    printf("  disconnect sstat=%02x\n", get_wdc_reg(WDC_SCSI_STAT));
}

static void
scsi_disconnect_msg(void)
{
    set_wdc_reg(WDC_CMD, WDC_CMD_DISCONNECT_MSG);
    cia_spin(10000);
}
#endif

/*
 * scsi_abort()
 *
 * XXX: According to the WD33C93A E062-B @9, the Abort command is
 *      no longer supported in the initiator mode.
 */
void
scsi_abort(void)
{
    uint auxst;
    if (scsi_wait_cip() == 0x100) {
        printf("SS1  astat=%02x sstat=%02x",
               get_wdc_reg(WDC_AUXST), get_wdc_reg(WDC_SCSI_STAT));
        return;
    }
    set_wdc_reg(WDC_CMD, WDC_CMD_ABORT);
    auxst = scsi_wait_cip();
    if (auxst != 0x100)
        auxst = scsi_wait(WDC_AUXST_INT, 1);
}

#undef DEBUG_PROBE_SCSI

static void
scsi_set_cdb(void *ptr, uint len)
{
    uint pos;
    uint8_t *ptr_b = (uint8_t *) ptr;

    if (len >= 12) {
        printf("Invalid CDB len %u\n", len);
        return;
    }
    set_wdc_reg(WDC_OWN_ID, len);
    for (pos = 0; pos < len; pos++)
        set_wdc_reg(WDC_CDB1 + pos, *(ptr_b++));
}

static void
scsi_transfer_start(uint phase)
{
    switch (phase) {
        case WDC_PHASE_DATA_IN:
        case WDC_PHASE_MESG_IN:
#ifdef DEBUG_PROBE_SCSI
            printf("  phase in %x\n", phase);
#endif
            set_wdc_reg(WDC_DST_ID,
                        get_wdc_reg(WDC_DST_ID) | WDC_DST_ID_DPD);
            break;
        case WDC_PHASE_DATA_OUT:
        case WDC_PHASE_MESG_OUT:
        case WDC_PHASE_CMD:
#ifdef DEBUG_PROBE_SCSI
            printf("  phase out %x\n", phase);
#endif
            set_wdc_reg(WDC_DST_ID,
                        get_wdc_reg(WDC_DST_ID) & ~WDC_DST_ID_DPD);
            break;
        default:
            printf("Unknown phase %x\n", phase);
    }
}

static int
scsi_transfer_in(uint phase)
{
    uint timeout = 5000;
    uint8_t auxst;
    uint len = get_wdc_reg(WDC_OWN_ID);
    uint count = 0;

    printf("trans_in\n");
    show_regs(0);
    while (count < len) {
        timeout = 50000;
        auxst = get_wdc_reg(WDC_AUXST);
        while ((auxst & WDC_AUXST_DBR) == 0) {
            if ((auxst & WDC_AUXST_INT) || (timeout-- == 0)) {
                printf("  WDC timeout in transfer_in, %u left: %02x\n",
                       len - count, auxst);
                return (-1);
            }
            if (auxst & WDC_AUXST_LCI) {
                uint8_t sstat = get_wdc_reg(WDC_SCSI_STAT);
                printf("LCI sstat=%02x\n", sstat);
                return (-1);
            }
            cia_spin(10);
            auxst = get_wdc_reg(WDC_AUXST);
        }
        count++;
        printf(" %02x", get_wdc_reg(WDC_DATA));  // Pull data
    }

    printf(" [%02x %02x]\n", len, count);
    return (len);
}

static int
scsi_transfer_out(uint len, void *buf)
{
    uint8_t auxst;
    uint8_t *bufptr = (uint8_t *) buf;
    uint    timeout;

    if (scsi_wait_cip() == 0x100) {
        printf("STO1  astat=%02x sstat=%02x",
               get_wdc_reg(WDC_AUXST), get_wdc_reg(WDC_SCSI_STAT));
        return (-1);
    }

    /*
     * The below read of AUXST and subsequent read of appears to be
     * required. Otherwise, the WDC_CMD_TRANSFER_INFO will sometimes
     * be ignored.
     */
    auxst = get_wdc_reg(WDC_AUXST);
    if (auxst & WDC_AUXST_INT) {
        (void) get_wdc_reg(WDC_SCSI_STAT);
#ifdef DEBUG_PROBE_SCSI
        printf("t_out auxst=%02x sstat=%02x\n",
               auxst, get_wdc_reg(WDC_SCSI_STAT));
#endif
    }

    /*
     * The processor either should initialize the Transfer
     * Count Register prior to issuing WDC_CMD_TRANSFER_INFO or
     * issue the command with the SBT bit in the Command Register
     * set. SBT = Single-byte Transfer (one byte is transferred)
     */
    set_wdc_reg(WDC_CONTROL, WDC_CONTROL_IDI | WDC_CONTROL_EDI);  // polled xfer
//  scsi_set_transfer_len(len);    // WD will get count after select
    set_wdc_reg(WDC_CMD, WDC_CMD_TRANSFER_INFO);
    if (scsi_wait_cip() == 0x100) {
        printf("STO2  astat=%02x sstat=%02x",
               get_wdc_reg(WDC_AUXST), get_wdc_reg(WDC_SCSI_STAT));
        return (-1);
    }

    while (len > 0) {
        timeout = 5000;
        auxst = get_wdc_reg(WDC_AUXST);
        while ((auxst & WDC_AUXST_DBR) == 0) {
            if ((auxst & WDC_AUXST_INT) || (timeout-- == 0)) {
#ifdef DEBUG_PROBE_SCSI
                printf("  WDC timeout in transfer_out, %u left: %02x\n",
                       len, auxst);
#endif
                if (auxst & WDC_AUXST_LCI) {
                    uint8_t sstat = get_wdc_reg(WDC_SCSI_STAT);
                    printf("LCI sstat=%02x\n", sstat);
                    return (-1);
                }
                return (len);
            }
            cia_spin(10);
            auxst = get_wdc_reg(WDC_AUXST);
        }
        set_wdc_reg(WDC_DATA, *(bufptr++));  // Push data
        len--;
    }
    return (len);
}

static int
probe_scsi(void)
{
    int found = 0;
    uint target;
    uint8_t auxst;
    uint8_t sstat;
    uint8_t sstat2;
    uint8_t sdmac_contr;

    INTERRUPTS_DISABLE();
    sdmac_contr = *ADDR8(SDMAC_CONTR);
    *ADDR8(SDMAC_CONTR) = 0;  // Disable interrupts
    INTERRUPTS_ENABLE();

    scsi_soft_reset(0);
    (void) get_wdc_reg(WDC_SCSI_STAT);  // clear reset status

    scsi_test_unit_ready_t tur;
    memset(&tur, 0, sizeof (tur));
    tur.opcode = SCSI_TEST_UNIT_READY;

    INTERRUPTS_DISABLE();
    for (target = 0; target < 7; target++) {
        uint8_t phase;
        uint    cmdlen;
        uint    present = 0;

        if (scsi_wait_cip() == 0x100) {
            printf("CIP%d  astat=%02x sstat=%02x", 0,
                   get_wdc_reg(WDC_AUXST), get_wdc_reg(WDC_SCSI_STAT));
            found = -1;
            break;
        }
        sstat = get_wdc_reg(WDC_SCSI_STAT);
        if (sstat & 0xe0) {
            printf("odd sstat %02x\n", sstat);
            found = -1;
            break;
        }
        /* Disable WDC DMA mode */
        set_wdc_reg(WDC_CONTROL, WDC_CONTROL_IDI | WDC_CONTROL_EDI);

        cmdlen = sizeof (tur);
        scsi_set_cdb(&tur, cmdlen);
        scsi_set_transfer_len(0);
        sstat = scsi_select(target);
        sstat2 = get_wdc_reg(WDC_SCSI_STAT);

        set_wdc_reg(WDC_SRC_ID, 0);  // Disable reselection

        if (sstat == WDC_SSTAT_SEL_COMPLETE) {
            uint timeout = 10;
            while (sstat2 == sstat) {
                cia_spin(10);
                sstat2 = get_wdc_reg(WDC_SCSI_STAT);
                if (--timeout == 0)
                    break;
            }
            if (timeout == 0) {
                printf("timeout: sstat=%02x sstat2=%02x\n", sstat, sstat2);
                found = -1;
                break;
            }
            phase  = sstat2 & 0x07;  // phase bits of status
            present = 1;
            found++;
#ifdef DEBUG_PROBE_SCSI
            printf("  sstat2=%02x astat=%02x\n",
                   sstat2, get_wdc_reg(WDC_AUXST));
#endif
            if (scsi_wait_cip() == 0x100) {
                printf("CIP%d  astat=%02x sstat=%02x", 1,
                       get_wdc_reg(WDC_AUXST), get_wdc_reg(WDC_SCSI_STAT));
                found = -1;
                break;
            }

            /* Set up transfer */
            set_wdc_reg(WDC_DST_ID, target & 0x7);
            scsi_transfer_start(phase);

            /* Delay and then check for interrupt */
            if (scsi_wait_cip() == 0x100) {
                printf("CIP%d  astat=%02x sstat=%02x", 2,
                       get_wdc_reg(WDC_AUXST), get_wdc_reg(WDC_SCSI_STAT));
                found = -1;
                break;
            }
            (void) get_wdc_reg(WDC_AUXST);
            (void) get_wdc_reg(WDC_AUXST);
            (void) get_wdc_reg(WDC_AUXST);
            auxst = get_wdc_reg(WDC_AUXST);
            if (auxst & WDC_AUXST_INT) {
                /* Clear pending interrupt */
                (void) get_wdc_reg(WDC_SCSI_STAT);
            }
            if (auxst & WDC_AUXST_LCI) {
                printf("LCI during SCSI transfter start: %02x\n",
                       get_wdc_reg(WDC_SCSI_STAT));
                found = -1;
                break;
            }
            if ((phase == WDC_PHASE_DATA_IN) || (phase == WDC_PHASE_MESG_IN)) {
                if (scsi_transfer_in(phase) <= 0) {
                    printf("phase=%02x sstat=%02x sstat2=%02x\n",
                           phase, sstat, sstat2);
                    found = -1;
                    break;
                }
            } else if (scsi_transfer_out(cmdlen, &tur) <= 0) {
                found = -1;
                break;
            }
            set_wdc_reg(WDC_CONTROL, WDC_CONTROL_IDI | WDC_CONTROL_EDI);
            scsi_abort();
        } else if (sstat == WDC_SSTAT_SEL_TIMEOUT) {
            present = 0;
        } else {
            printf("  Unexpected SCSI STAT sstat=%02x %02x\n", sstat, sstat2);
            found = -1;
            break;
        }

        if (scsi_wait_cip() == 0x100) {
            printf("CIP%d  astat=%02x sstat=%02x", 3,
                   get_wdc_reg(WDC_AUXST), get_wdc_reg(WDC_SCSI_STAT));
            found = -1;
            break;
        }
        scsi_soft_reset(0);
        if (scsi_wait_cip() == 0x100) {
            printf("CIP%d  astat=%02x sstat=%02x", 4,
                   get_wdc_reg(WDC_AUXST), get_wdc_reg(WDC_SCSI_STAT));
            found = -1;
            break;
        }
        (void) get_wdc_reg(WDC_SCSI_STAT);  // clear reset status

        *ADDR8(SDMAC_CLR_INT) = 0;  // Clear pending interrupts

        if (is_user_abort()) {
            printf("^C Abort\n");
            found = -1;
            break;
        }
        INTERRUPTS_ENABLE();
#ifdef DEBUG_PROBE_SCSI
        printf("%u %s sstat=%02x phase=%x\n",
               target, present ? "present" : "no response", sstat, phase);
#else
        printf("  %u %s\n", target, present ? "present" : "no response");
#endif
        INTERRUPTS_DISABLE();
    }
#ifdef DEBUG_PROBE_SCSI
    printf("auxst=%02x\n", get_wdc_reg(WDC_AUXST));
    printf("sstat=%02x\n", get_wdc_reg(WDC_SCSI_STAT));
    printf("istr=%02x\n", *ADDR8(SDMAC_ISTR));
#endif
    *ADDR8(SDMAC_CLR_INT) = 0;          // Clear interrupt
    *ADDR8(SDMAC_CONTR) = sdmac_contr;  // Restore interrupts
    INTERRUPTS_ENABLE();
    if (found == 0) {
        printf("No device found\n");
        return (1);
    }
    if (found == -1)
        return (1);
    return (0);
}

int
main(int argc, char **argv)
{
    int do_wdc_reset = 0;
    int raw_sdmac_regs = 0;
    int all_regs = 0;
    int loop_until_failure = 0;
    int readwrite_wdc_reg = 0;
    int probe_scsi_bus = 0;
    int flag_show = 0;
    int flag_force_test = 0;
    int arg;
    uint pass = 0;

    for (arg = 1; arg < argc; arg++) {
        char *ptr = argv[arg];
        if (*ptr == '-') {
            while (*(++ptr) != '\0') {
                switch (*ptr) {
                    case 'd':
                        flag_debug++;
                        break;
                    case 'L':
                        loop_until_failure++;
                        break;
                    case 'p':
                        probe_scsi_bus++;
                        break;
                    case 'r': {
                        int pos = 0;
                        uint addr;
                        uint val;
                        char *arg1 = argv[arg + 1];
                        char *arg2 = argv[arg + 2];
                        if ((argc <= arg + 1) || (*arg1 == '-')) {
                            /* Display all registers */
                            all_regs++;
                            break;
                        }
                        if ((sscanf(arg1, "%x%n", &addr, &pos) != 1) ||
                            (arg1[pos] != '\0') || (addr > 0xff)) {
                            printf("Invalid address %s for -%s\n", arg1, ptr);
                            exit(1);
                        }
                        if ((argc <= arg + 2) || (*arg2 == '-')) {
                            /* read */
                            val = get_wdc_reg_extended(addr);
                            arg++;
                            readwrite_wdc_reg++;
                            show_wdc_reg(addr, val);
                            break;
                        }
                        if ((sscanf(arg2, "%x%n", &val, &pos) != 1) ||
                            (arg2[pos] != '\0') || (val > 0xff)) {
                            printf("Invalid data %s for -%s\n", arg2, ptr);
                            exit(1);
                        }

                        /* write */
                        set_wdc_reg_extended(addr, val);
                        arg += 2;
                        readwrite_wdc_reg++;
                        show_wdc_reg(addr, val);
                        break;
                    }
                    case 'R':
                        do_wdc_reset++;
                        break;
                    case 's':
                        raw_sdmac_regs++;
                        break;
                    case 't':
                        flag_force_test++;
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
                   "    -d Debug output\n"
                   "    -L Loop tests until failure\n"
                   "    -p probe SCSI bus (not well-tested)\n"
                   "    -R reset WD SCSI Controller\n"
                   "    -r [<reg> [<value>]] Display/change WDC registers\n"
                   "    -s Display raw SDMAC registers\n"
                   "    -t Force tests to run\n"
                   "    -v Display program version\n", version + 7);
            exit(1);
        }
    }
    BERR_DSACK_SAVE();
    scsi_save_regs();
    if (all_regs)
        goto finish;  // Do not probe or perform tests
    if (readwrite_wdc_reg)
        goto finish;

    if ((probe_scsi_bus == 0) &&
        (do_wdc_reset == 0) &&
        (raw_sdmac_regs == 0) &&
        (flag_force_test == 0)) {
        flag_force_test++;
        flag_show++;
    }

    if (flag_show &&
        (show_ramsey_version() ||
         show_ramsey_config() ||
         show_dmac_version() ||
         show_wdc_version() ||
         show_wdc_config())) {
        if (flag_force_test == 0)
            goto finish;
    }

    do {
        pass++;
        if (flag_force_test &&
            (test_ramsey_access() +
             test_sdmac_access() +
             test_wdc_access() > 0)) {
            break;
        }
        if (probe_scsi_bus &&
            probe_scsi()) {
            break;
        }
        if (do_wdc_reset) {
            const char *mode;
            if (do_wdc_reset > 3) {
                scsi_hard_reset();
                mode = "hard";
            } else {
                scsi_soft_reset(do_wdc_reset);
                mode = "soft";
            }
            printf("WDC %s reset complete\n", mode);
        }
        if (is_user_abort()) {
            printf("^C Abort\n");
            break;
        }
    } while (loop_until_failure);

finish:
    if (all_regs) {
        show_regs(all_regs > 1);
    }
    if (raw_sdmac_regs) {
        get_raw_regs();
        dump_raw_sdmac_regs();
    }
    INTERRUPTS_DISABLE();
    scsi_restore_regs();
    INTERRUPTS_ENABLE();
    BERR_DSACK_RESTORE();
    if (loop_until_failure)
        printf("%s at pass %u\n", is_user_abort() ? "Stopped" : "Failed", pass);

    exit(0);
}
