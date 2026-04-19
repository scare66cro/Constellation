/*-----------------------------------------------------------------------*/
/* MMC/SDC (in SPI mode) control module  (C)ChaN, 2007                   */
/*-----------------------------------------------------------------------*/
/* Only rcvr_spi(), xmit_spi(), disk_timerproc() and some macros         */
/* are platform dependent.                                               */
/*-----------------------------------------------------------------------*/

/*
 * This file was modified from a sample available from the FatFs
 * web site. It was modified to work with a Stellaris EK-LM3S9B92
 * evaluation board.
 */

#include <stdbool.h>
#include <stdint.h>

#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "inc/hw_ssi.h"
#include "driverlib/ssi.h"
//#include "driverlib/sysctl.h"
#include "system.h"
#include "kfs_port.h"
#include "pinout.h"

/* Definitions for MMC/SDC command */
#define CMD0    (0x40+0)    /* GO_IDLE_STATE */
#define CMD1    (0x40+1)    /* SEND_OP_COND */
#define CMD8    (0x40+8)    /* SEND_IF_COND */
#define CMD9    (0x40+9)    /* SEND_CSD */
#define CMD10    (0x40+10)    /* SEND_CID */
#define CMD12    (0x40+12)    /* STOP_TRANSMISSION */
#define CMD16    (0x40+16)    /* SET_BLOCKLEN */
#define CMD17    (0x40+17)    /* READ_SINGLE_BLOCK */
#define CMD18    (0x40+18)    /* READ_MULTIPLE_BLOCK */
#define CMD23    (0x40+23)    /* SET_BLOCK_COUNT */
#define CMD24    (0x40+24)    /* WRITE_BLOCK */
#define CMD25    (0x40+25)    /* WRITE_MULTIPLE_BLOCK */
#define CMD41    (0x40+41)    /* SEND_OP_COND (ACMD) */
#define CMD55    (0x40+55)    /* APP_CMD */
#define CMD58    (0x40+58)    /* READ_OCR */


// asserts the CS pin to the card
static void SELECT(void)
{
  //EnterCritical();
  set_output(SD_CS, 0);
}

// de-asserts the CS pin to the card
static void DESELECT(void)
{
  set_output(SD_CS, 1);
    //ExitCritical();
}


/*--------------------------------------------------------------------------
   Module Private Functions
---------------------------------------------------------------------------*/

//static volatile KFS_RET Stat = STA_NOINIT;    /* Disk status */

static volatile unsigned char Timer1;    /* 100Hz decrement timer */
static volatile unsigned char Timer2;

static unsigned char CardType;            /* b0:MMC, b1:SDC, b2:Block addressing */


/*-----------------------------------------------------------------------*/
/* Transmit a unsigned char to MMC via SPI  (Platform dependent)         */
/*-----------------------------------------------------------------------*/
static void xmit_spi(unsigned char dat)
{
  unsigned int rcvdat;
  SSIDataPut(SD_BASE, dat); /* Write the data to the tx fifo */
    SSIDataGet(SD_BASE, &rcvdat); /* flush data read during the write */
}


/*-----------------------------------------------------------------------*/
/* Receive a unsigned char from MMC via SPI  (Platform dependent)        */
/*-----------------------------------------------------------------------*/
static unsigned char rcvr_spi (void)
{
    unsigned int rcvdat;
    SSIDataPut(SD_BASE, 0xFF); /* write dummy data */
    SSIDataGet(SD_BASE, &rcvdat); /* read data frm rx fifo */
    return (unsigned char)rcvdat;
}

static void rcvr_spi_m (unsigned char *dst)
{
    *dst = rcvr_spi();
}

/*-----------------------------------------------------------------------*/
/* Wait for card ready                                                   */
/*-----------------------------------------------------------------------*/
static unsigned char wait_ready (void)
{
    unsigned char res;
    unsigned int tries=0;

    Timer2 = 50;    /* Wait for ready in timeout of 500ms */
    rcvr_spi();
    do
    {
        res = rcvr_spi();
      tries++;
    } while ((res != 0xFF) && Timer2);

    if ((res!=0xFF)&&(Timer2==0)) debug_printf("wait_ready failed with tries = %d\r\n", tries);

    return res;
}


/*-----------------------------------------------------------------------*/
/* Send 80 or so clock transitions with CS and DI held high. This is     */
/* required after card power up to get it into SPI mode                  */
/*-----------------------------------------------------------------------*/
 void send_initial_clock_train(void)
{
    unsigned int i;
    unsigned int dat;

    /* Ensure CS is held high. */
    DESELECT();

    // Switch the SSI TX line to a GPIO and drive it high too.
    configure_output_pin(SD_TX);
    set_output(SD_TX, 1);

    // Send 10 unsigned chars over the SSI. This causes the clock to wiggle the required number of times.
    for(i = 0 ; i < 10 ; i++)
    {
        /* Write DUMMY data. SSIDataPut() waits until there is room in the */
        /* FIFO. */
        SSIDataPut(SD_BASE, 0xFF);

        /* Flush data read during data write. */
        SSIDataGet(SD_BASE, &dat);
    }

    // Revert to hardware control of the SSI TX line.
    GPIOPinTypeSSI(SD_TX.port, SD_TX.pin);
}


/*-----------------------------------------------------------------------*/
/* Power Control  (Platform dependent)                                   */
/*-----------------------------------------------------------------------*/
/* When the target system does not support socket power control, there   */
/* is nothing to do in these functions and chk_power always returns 1.   */

static void power_on (void)
{
    // Deassert the SSI0 chip select
    DESELECT();

    // Set DI and CS high and apply more than 74 pulses to SCLK for the card to be able to accept a native command.
    send_initial_clock_train();
}

// set the SSI speed to the max setting

static void set_max_speed(void)
{
  unsigned int i;

    // Disable the SSI
    SSIDisable(SD_BASE);

    /* Set the maximum speed as half the system clock, with a max of 12.5 MHz. */
    i = system_clock_speed / 2;
    if(i > SD_MAX_SPEED)
    {
        i = SD_MAX_SPEED;
    }



    //debug_printf("Setting max speed to %d\r\n", i);

    // Configure the port
    SSIConfigSetExpClk(SD_BASE, system_clock_speed, SSI_FRF_MOTO_MODE_0, SSI_MODE_MASTER, 25000000, 8);

    // Enable the SSI
    SSIEnable(SD_BASE);
}


/*-----------------------------------------------------------------------*/
/* Receive a data packet from MMC                                        */
/*-----------------------------------------------------------------------*/

static unsigned char rcvr_datablock(unsigned char *buff, unsigned int btr)
{
    unsigned char token;


    Timer1 = 10;
    do {                            /* Wait for data packet in timeout of 100ms */
        token = rcvr_spi();
    } while ((token == 0xFF) && Timer1);
    if(token != 0xFE) return 0;    /* If not valid data token, return with error */

    do {                            /* Receive the data block into buffer */
        rcvr_spi_m(buff++);
        rcvr_spi_m(buff++);
    } while (btr -= 2);
    rcvr_spi();                        /* Discard CRC */
    rcvr_spi();

    return 1;                    /* Return with success */
}



/*-----------------------------------------------------------------------*/
/* Send a data packet to MMC                                             */
/*-----------------------------------------------------------------------*/

static unsigned char xmit_datablock (const unsigned char *buff, unsigned char token)
{
    unsigned char resp;
    unsigned char wc;


    if (wait_ready() != 0xFF) return 0;

    xmit_spi(token);                    /* Xmit data token */
    if (token != 0xFD) {    /* Is data token */
        wc = 0;
        do {                            /* Xmit the 512 unsigned char data block to MMC */
            xmit_spi(*buff++);
            xmit_spi(*buff++);
        } while (--wc);
        xmit_spi(0xFF);                    /* CRC (Dummy) */
        xmit_spi(0xFF);
        resp = rcvr_spi();                /* Receive data response */
        if ((resp & 0x1F) != 0x05)        /* If not accepted, return with error */
            return 0;
    }

    return 1;
}

/*-----------------------------------------------------------------------*/
/* Send a command packet to MMC                                          */
/*-----------------------------------------------------------------------*/

static unsigned char send_cmd(unsigned char cmd, unsigned int arg)
{
    unsigned char n;
    unsigned char res;


    if (wait_ready() != 0xFF) return 0xFF;

    /* Send command packet */
    xmit_spi(cmd);                        /* Command */
    xmit_spi((unsigned char)(arg >> 24));        /* Argument[31..24] */
    xmit_spi((unsigned char)(arg >> 16));        /* Argument[23..16] */
    xmit_spi((unsigned char)(arg >> 8));            /* Argument[15..8] */
    xmit_spi((unsigned char)arg);                /* Argument[7..0] */
    n = 0;
    if (cmd == CMD0) n = 0x95;            /* CRC for CMD0(0) */
    if (cmd == CMD8) n = 0x87;            /* CRC for CMD8(0x1AA) */
    xmit_spi(n);

    /* Receive command response */
    if (cmd == CMD12) rcvr_spi();        /* Skip a stuff unsigned char when stop reading */
    n = 10;                                /* Wait for a valid response in timeout of 10 attempts */
    do
        res = rcvr_spi();
    while ((res & 0x80) && --n);

    return res;            /* Return with the response value */
}


/*--------------------------------------------------------------------------

   Public Functions

---------------------------------------------------------------------------*/


/*-----------------------------------------------------------------------*/
/* Initialize Disk Drive                                                 */
/*-----------------------------------------------------------------------*/

KFS_RET kfs_disk_initialize(void)
{
    unsigned char n;
    unsigned char ty;
    unsigned char ocr[4];

    // Configure the port
    SSIConfigSetExpClk(SD_BASE, system_clock_speed, SSI_FRF_MOTO_MODE_0, SSI_MODE_MASTER, 400000, 8);
    // Enable the SSI
    SSIEnable(SD_BASE);

//    power_on();                            /* Force socket power on */
    send_initial_clock_train();

    SELECT();                /* CS = L */
    ty = 0;
    if (send_cmd(CMD0, 0) == 1) {            /* Enter Idle state */
        Timer1 = 100;                        /* Initialization timeout of 1000 msec */
        if (send_cmd(CMD8, 0x1AA) == 1) {    /* SDC Ver2+ */
            for (n = 0; n < 4; n++) ocr[n] = rcvr_spi();
            if (ocr[2] == 0x01 && ocr[3] == 0xAA) {    /* The card can work at vdd range of 2.7-3.6V */
                do {
                    if (send_cmd(CMD55, 0) <= 1 && send_cmd(CMD41, 1UL << 30) == 0)    break;    /* ACMD41 with HCS bit */
                } while (Timer1);
                if (Timer1 && send_cmd(CMD58, 0) == 0) {    /* Check CCS bit */
                    for (n = 0; n < 4; n++) ocr[n] = rcvr_spi();
                    ty = (ocr[0] & 0x40) ? 6 : 2;
                }
            }
        } else {                            /* SDC Ver1 or MMC */
            ty = (send_cmd(CMD55, 0) <= 1 && send_cmd(CMD41, 0) <= 1) ? 2 : 1;    /* SDC : MMC */
            do {
                if (ty == 2) {
                    if (send_cmd(CMD55, 0) <= 1 && send_cmd(CMD41, 0) == 0) break;    /* ACMD41 */
                } else {
                    if (send_cmd(CMD1, 0) == 0) break;                                /* CMD1 */
                }
            } while (Timer1);
            if (!Timer1 || send_cmd(CMD16, 512) != 0)    /* Select R/W block length */
                ty = 0;
        }
    }
    CardType = ty;
    DESELECT();            /* CS = H */
    rcvr_spi();            /* Idle (Release DO) */

    // Configure the port
    SSIConfigSetExpClk(SD_BASE, system_clock_speed, SSI_FRF_MOTO_MODE_0, SSI_MODE_MASTER, 25000000, 8);

    // Enable the SSI
    SSIEnable(SD_BASE);

    if (ty)
    {
        set_max_speed();
        return KFS_SUCCESS;
    }
    else
    {
        return KFS_BADDISK;
    }
}


/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

KFS_RET kfs_read_sector(unsigned char *buff, unsigned int sector, unsigned int count)
{
    if (!(CardType & 4)) sector *= 512;    /* Convert to unsigned char address if needed */

    SELECT();            /* CS = L */

    if (count == 1)
    {    /* Single block read */
        if ((send_cmd(CMD17, sector) == 0)&& rcvr_datablock(buff, 512))
        {
            count = 0;
        }
    }
    else
    {                /* Multiple block read */
        if (send_cmd(CMD18, sector) == 0)
        {    /* READ_MULTIPLE_BLOCK */
            do
            {
                if (!rcvr_datablock(buff, 512)) break;
                buff += 512;
            } while (--count);
            send_cmd(CMD12, 0);                /* STOP_TRANSMISSION */
        }
    }

    DESELECT();            /* CS = H */
    rcvr_spi();            /* Idle (Release DO) */

    return count ? KFS_READ_ERROR : KFS_SUCCESS;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/
KFS_RET kfs_write_sector(const unsigned char *buff, unsigned int sector, unsigned int count)
{
    if (!(CardType & 4)) sector *= 512;    /* Convert to unsigned char address if needed */

    SELECT();            /* CS = L */

    if (count == 1)
    {    /* Single block write */
        if ((send_cmd(CMD24, sector) == 0) && xmit_datablock(buff, 0xFE))
        {
            count = 0;
        }
    }
    else
    {                /* Multiple block write */
        if (CardType & 2)
        {
            send_cmd(CMD55, 0);
            send_cmd(CMD23, count);    /* ACMD23 */
        }
        if (send_cmd(CMD25, sector) == 0)
        {    /* WRITE_MULTIPLE_BLOCK */
            do
            {
                if (!xmit_datablock(buff, 0xFC)) break;
                buff += 512;
            } while (--count);
            if (!xmit_datablock(0, 0xFD))    /* STOP_TRAN token */
            {
              count = 1;
            }
        }
    }

    DESELECT();            /* CS = H */
    rcvr_spi();            /* Idle (Release DO) */

    return count ? KFS_WRITE_ERROR : KFS_SUCCESS;
}




/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/
unsigned int kfs_get_sector_count(void)
{
  unsigned char n;
  unsigned char csd[16];
  unsigned int csize;
  unsigned int sector_count=0;

  SELECT();        /* CS = L */

  if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16))
  {
    if ((csd[0] >> 6) == 1)
    {    /* SDC ver 2.00 */
            csize = csd[9] + ((unsigned int)csd[8] << 8) + 1;
            sector_count = (unsigned int)csize << 10;
        }
        else
        {                    /* MMC or SDC ver 1.XX */
            n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
            csize = (csd[8] >> 6) + ((unsigned int)csd[7] << 2) + ((unsigned int)(csd[6] & 3) << 10) + 1;
            sector_count = (unsigned int)csize << (n - 9);
        }
  }

  DESELECT();            /* CS = H */
    rcvr_spi();            /* Idle (Release DO) */

  return sector_count;
}

/*-----------------------------------------------------------------------*/
/* Device Timer Interrupt Procedure  (Platform dependent)                */
/*-----------------------------------------------------------------------*/
/* This function must be called in period of 10ms                        */

void kfs_timer(void)
{
#define SKIP_AMT 10
  static unsigned int skip=0;

    unsigned char n;

    if (skip++<SKIP_AMT) return;
    skip=0;

    n = Timer1;                        /* 100Hz decrement timer */
    if (n) Timer1 = --n;
    n = Timer2;
    if (n) Timer2 = --n;

}
