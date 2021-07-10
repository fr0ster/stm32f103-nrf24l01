#include <stdio.h>
#include <string.h>
#include "nrf24l01.h"

u8 RX_BUF[NRF24L01_PLOAD_WIDTH];
u8 TX_BUF[NRF24L01_PLOAD_WIDTH];

static void NRF24L01_SPI_Init()
{
  GPIO_InitTypeDef GPIO_InitStruct;

  if(NRF24L01_SPIx == SPI1) {
    // A5, A6, A7
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO,ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1,ENABLE);

    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStruct);

  } else if(NRF24L01_SPIx == SPI2) {
    // B13, B14, B15
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO,ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2,ENABLE);

    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStruct);
  }

  SPI_InitTypeDef SPI_InitStruct;
  SPI_InitStruct.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;
  SPI_InitStruct.SPI_CPHA = SPI_CPHA_1Edge;
  SPI_InitStruct.SPI_CPOL = SPI_CPOL_Low;
  SPI_InitStruct.SPI_CRCPolynomial = 7;
  SPI_InitStruct.SPI_DataSize = SPI_DataSize_8b;
  SPI_InitStruct.SPI_Direction= SPI_Direction_2Lines_FullDuplex;
  SPI_InitStruct.SPI_FirstBit = SPI_FirstBit_MSB;
  SPI_InitStruct.SPI_Mode = SPI_Mode_Master;
  SPI_InitStruct.SPI_NSS = SPI_NSS_Soft;

  SPI_Init(NRF24L01_SPIx, &SPI_InitStruct);

  SPI_Cmd(NRF24L01_SPIx, ENABLE);
}

void NRF24L01_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct;
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
  // CE CSN Initialize
  GPIO_InitStruct.GPIO_Pin = NRF24L01_GPIO_CE | NRF24L01_GPIO_CSN;
  GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(NRF24L01_GPIOx, &GPIO_InitStruct);  
  // IRQ Initialize
  GPIO_InitStruct.GPIO_Pin = NRF24L01_GPIO_IRQ;
  GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPU;
  GPIO_Init(NRF24L01_GPIOx, &GPIO_InitStruct);

  NRF24L01_SPI_Init();
  CSN(1);
}

/**
* Basic SPI operation: Write to SPIx and read
*/
static u8 SPI_Write_Then_Read(u8 data)
{
  while(SPI_I2S_GetFlagStatus(NRF24L01_SPIx, SPI_I2S_FLAG_TXE) == RESET);
  SPI_I2S_SendData(NRF24L01_SPIx, data);

  while(SPI_I2S_GetFlagStatus(NRF24L01_SPIx, SPI_I2S_FLAG_RXNE)==RESET);
  return SPI_I2S_ReceiveData(NRF24L01_SPIx);
}

/**
* Read a 1-bit register
*/
u8 NRF24L01_Read_Reg(u8 reg)
{
  u8 value;
  CSN(0);
  SPI_Write_Then_Read(reg);
  value = SPI_Write_Then_Read(NRF24L01_CMD_NOP);
  CSN(1);
  return value;
}

/**
* Write a 1-byte register
*/
u8 NRF24L01_Write_Reg(u8 reg, u8 value)
{
  u8 status;
  CSN(0);
  if (reg < NRF24L01_CMD_REGISTER_W) {
    // This is a register access
		status = SPI_Write_Then_Read(NRF24L01_CMD_REGISTER_W | (reg & NRF24L01_MASK_REG_MAP));
		SPI_Write_Then_Read(value);

  } else {
    // This is a single byte command or future command/register
		status = SPI_Write_Then_Read(reg);
		if ((reg != NRF24L01_CMD_FLUSH_TX) 
        && (reg != NRF24L01_CMD_FLUSH_RX) 
        && (reg != NRF24L01_CMD_REUSE_TX_PL) 
        && (reg != NRF24L01_CMD_NOP)) {
			// Send register value
			SPI_Write_Then_Read(value);
		}
  }
  CSN(1); //停止SPI传输	
  return status; 
}

/**
* Read a multi-byte register
*  reg  - register to read
*  buf  - pointer to the buffer to write
*  len  - number of bytes to read
*/
u8 NRF24L01_Read_To_Buf(u8 reg, u8 *buf, u8 len)
{
  CSN(0);
  u8 status = SPI_Write_Then_Read(reg);
  while (len--) {
		*buf++ = SPI_Write_Then_Read(NRF24L01_CMD_NOP);
	}
  CSN(1);
  return status;
}

/**
* Write a multi-byte register
*  reg - register to write
*  buf - pointer to the buffer with data
*  len - number of bytes to write
*/
u8 NRF24L01_Write_From_Buf(u8 reg, u8 *buf, u8 len)
{
  CSN(0);
  u8 status = SPI_Write_Then_Read(reg);
  while (len--) {
    SPI_Write_Then_Read(*buf++);
  }
  CSN(1);
  return status;
}


u8 NRF24L01_Check(void)
{
  u8 rxbuf[5];
	u8 i;
	u8 *ptr = (u8 *)NRF24L01_TEST_ADDR;

	// Write test TX address and read TX_ADDR register
	NRF24L01_Write_From_Buf(NRF24L01_CMD_REGISTER_W | NRF24L01_REG_TX_ADDR, ptr, 5);
	NRF24L01_Read_To_Buf(NRF24L01_CMD_REGISTER_R | NRF24L01_REG_TX_ADDR, rxbuf, 5);

	// Compare buffers, return error on first mismatch
	for (i = 0; i < 5; i++) {
		if (rxbuf[i] != *ptr++) return 1;
	}
	return 0;
}

/**
* Flush the RX FIFO
*/
void NRF24L01_FlushRX(void)
{
	NRF24L01_Write_Reg(NRF24L01_CMD_FLUSH_RX, NRF24L01_CMD_NOP);
}

/**
* Flush the TX FIFO
*/
void NRF24L01_FlushTX(void)
{
	NRF24L01_Write_Reg(NRF24L01_CMD_FLUSH_TX, NRF24L01_CMD_NOP);
}

/**
* Clear IRQ bit of the STATUS register
*   reg - NRF24L01_FLAG_RX_DREADY
*         NRF24L01_FLAG_TX_DSENT
*         NRF24L01_FLAG_MAX_RT
*/
void NRF24L01_ClearIRQFlag(u8 reg) {
  NRF24L01_Write_Reg(NRF24L01_CMD_REGISTER_W + NRF24L01_REG_STATUS, reg);
}

/**
* Clear RX_DR, TX_DS and MAX_RT bits of the STATUS register
*/
void NRF24L01_ClearIRQFlags(void) {
	u8 reg;
	reg  = NRF24L01_Read_Reg(NRF24L01_REG_STATUS);
	reg |= NRF24L01_MASK_STATUS_IRQ;
	NRF24L01_Write_Reg(NRF24L01_REG_STATUS, reg);
}

/**
* Common configurations of RX and TX, internal function
*/
void _NRF24L01_Config(u8 *rx_addr, u8 *tx_addr)
{
  // TX Address
  NRF24L01_Write_From_Buf(NRF24L01_CMD_REGISTER_W + NRF24L01_REG_TX_ADDR, tx_addr, NRF24L01_ADDR_WIDTH);
  // RX Address of P0
  NRF24L01_Write_From_Buf(NRF24L01_CMD_REGISTER_W + NRF24L01_REG_RX_ADDR_P0, rx_addr, NRF24L01_ADDR_WIDTH);
  // RX P0 Payload Width
  NRF24L01_Write_Reg(NRF24L01_CMD_REGISTER_W + NRF24L01_REG_RX_PW_P0, NRF24L01_PLOAD_WIDTH);
  // 使能接收通道0自动应答
  NRF24L01_Write_Reg(NRF24L01_CMD_REGISTER_W + NRF24L01_REG_EN_AA, 0x3f);
  // 使能接收通道0
  NRF24L01_Write_Reg(NRF24L01_CMD_REGISTER_W + NRF24L01_REG_EN_RXADDR, 0x3f);
  // 选择射频通道40 - 2.440GHz
  NRF24L01_Write_Reg(NRF24L01_CMD_REGISTER_W + NRF24L01_REG_RF_CH, 0);
  // 07:1Mbps,0dBm, 0f:2Mbps,0dBm
  NRF24L01_Write_Reg(NRF24L01_CMD_REGISTER_W + NRF24L01_REG_RF_SETUP, 0x0f);
  // 0A:delay=250us,count=10, 1A:delay=500us,count=10
  NRF24L01_Write_Reg(NRF24L01_CMD_REGISTER_W + NRF24L01_REG_SETUP_RETR, 0x1a);
}

/**
* Switch NRF24L01 to RX mode
*/
void NRF24L01_RX_Mode(u8 *rx_addr, u8 *tx_addr)
{
  CE(0);
  _NRF24L01_Config(rx_addr, tx_addr);
  /**
  REG 0x00: 
  0)PRIM_RX     0:TX             1:RX
  1)PWR_UP      0:OFF            1:ON
  2)CRCO        0:8bit CRC       1:16bit CRC
  3)EN_CRC      Enabled if any of EN_AA is high
  4)MASK_MAX_RT 0:IRQ low        1:NO IRQ
  5)MASK_TX_DS  0:IRQ low        1:NO IRQ
  6)MASK_RX_DR  0:IRQ low        1:NO IRQ
  7)Reserved    0
  */
  NRF24L01_Write_Reg(NRF24L01_CMD_REGISTER_W + NRF24L01_REG_CONFIG, 0x0f); //RX,PWR_UP,CRC16,EN_CRC
  CE(1);
}

/**
* Switch NRF24L01 to TX mode
*/
void NRF24L01_TX_Mode(u8 *rx_addr, u8 *tx_addr)
{
  CE(0);
  _NRF24L01_Config(rx_addr, tx_addr);
  NRF24L01_Write_Reg(NRF24L01_CMD_REGISTER_W + NRF24L01_REG_CONFIG, 0x0e); //TX,PWR_UP,CRC16,EN_CRC
  CE(1);
}

/**
* Hold till data received and written to rx_buf
*/
u8 NRF24L01_RxPacket(u8 *rx_buf)
{
  u8 status, result = 0;
  while(IRQ);
  CE(0);
  status = NRF24L01_Read_Reg(NRF24L01_REG_STATUS);
  printf("Interrupted, status: %02X\r\n", status);

  if(status & NRF24L01_FLAG_RX_DREADY) {
    NRF24L01_Read_To_Buf(NRF24L01_CMD_RX_PLOAD_R, rx_buf, NRF24L01_PLOAD_WIDTH);
    for (int i = 0; i < 32; i++) {
      printf("%02X ", RX_BUF[i]);
    }
    result = 1;
    NRF24L01_ClearIRQFlag(NRF24L01_FLAG_RX_DREADY);
  }
  CE(1);
  return result;
}

/**
* Send data in tx_buf and wait till data is sent or max re-tr reached
*/
u8 NRF24L01_TxPacket(u8 *tx_buf, u8 len)
{
  u8 status = 0x00;
  CE(0);
  len = len > NRF24L01_PLOAD_WIDTH? NRF24L01_PLOAD_WIDTH : len;
  NRF24L01_Write_From_Buf(NRF24L01_CMD_TX_PLOAD_W, tx_buf, len);
  CE(1);
  while(IRQ != 0); // Waiting send finish

  status = NRF24L01_Read_Reg(NRF24L01_REG_STATUS);
  if(status & NRF24L01_FLAG_TX_DSENT) {
    printf("Send status: %2X\r\n", status);
    printf("Data sent: ");
    for (u8 i = 0; i < len; i++) {
      printf("%02X ", tx_buf[i]);
    }
    printf("\r\n");
    NRF24L01_ClearIRQFlag(NRF24L01_FLAG_TX_DSENT);
  } else if(status & NRF24L01_FLAG_MAX_RT) {
    printf("Sending exceeds max retries\r\n");
    NRF24L01_FlushTX();
    NRF24L01_ClearIRQFlag(NRF24L01_FLAG_MAX_RT);
  }
  return status;
}


/**
* Dump nRF24L01 configuration
*/
void NRF24L01_DumpConfig(void) {
	uint8_t i,j;
	uint8_t aw;
	uint8_t buf[5];

	// CONFIG
	i = NRF24L01_Read_Reg(NRF24L01_REG_CONFIG);
	printf("[0x%02X] 0x%02X MASK:%02x CRC:%02x PWR:%s MODE:P%s\r\n",
			NRF24L01_REG_CONFIG,
			i,
			i >> 4,
			(i & 0x0c) >> 2,
			(i & 0x02) ? "ON" : "OFF",
			(i & 0x01) ? "RX" : "TX"
		);
	// EN_AA
	i = NRF24L01_Read_Reg(NRF24L01_REG_EN_AA);
	printf("[0x%02X] 0x%02X ENAA: ",NRF24L01_REG_EN_AA,i);
	for (j = 0; j < 6; j++) {
		printf("[P%1u%s]%s",j,
				(i & (1 << j)) ? "+" : "-",
				(j == 5) ? "\r\n" : " "
			);
	}
	// EN_RXADDR
	i = NRF24L01_Read_Reg(NRF24L01_REG_EN_RXADDR);
	printf("[0x%02X] 0x%02X EN_RXADDR: ",NRF24L01_REG_EN_RXADDR,i);
	for (j = 0; j < 6; j++) {
		printf("[P%1u%s]%s",j,
				(i & (1 << j)) ? "+" : "-",
				(j == 5) ? "\r\n" : " "
			);
	}
	// SETUP_AW
	i = NRF24L01_Read_Reg(NRF24L01_REG_SETUP_AW);
	aw = (i & 0x03) + 2;
	printf("[0x%02X] 0x%02X EN_RXADDR=%03X (address width = %u)\r\n",NRF24L01_REG_SETUP_AW,i,i & 0x03,aw);
	// SETUP_RETR
	i = NRF24L01_Read_Reg(NRF24L01_REG_SETUP_RETR);
	printf("[0x%02X] 0x%02X ARD=%04X ARC=%04X (retr.delay=%uus, count=%u)\r\n",
			NRF24L01_REG_SETUP_RETR,
			i,
			i >> 4,
			i & 0x0F,
			((i >> 4) * 250) + 250,
			i & 0x0F
		);
	// RF_CH
	i = NRF24L01_Read_Reg(NRF24L01_REG_RF_CH);
	printf("[0x%02X] 0x%02X (%.3uGHz)\r\n",NRF24L01_REG_RF_CH,i,2400 + i);
	// RF_SETUP
	i = NRF24L01_Read_Reg(NRF24L01_REG_RF_SETUP);
	printf("[0x%02X] 0x%02X CONT_WAVE:%s PLL_LOCK:%s DataRate=",
			NRF24L01_REG_RF_SETUP,
			i,
			(i & 0x80) ? "ON" : "OFF",
			(i & 0x80) ? "ON" : "OFF"
		);
	switch ((i & 0x28) >> 3) {
		case 0x00:
			printf("1M");
			break;
		case 0x01:
			printf("2M");
			break;
		case 0x04:
			printf("250k");
			break;
		default:
			printf("???");
			break;
	}
	printf("pbs RF_PWR=");
	switch ((i & 0x06) >> 1) {
		case 0x00:
			printf("-18");
			break;
		case 0x01:
			printf("-12");
			break;
		case 0x02:
			printf("-6");
			break;
		case 0x03:
			printf("0");
			break;
		default:
			printf("???");
			break;
	}
	printf("dBm\r\n");
	// STATUS
	i = NRF24L01_Read_Reg(NRF24L01_REG_STATUS);
	printf("[0x%02X] 0x%02X IRQ:%03X RX_PIPE:%u TX_FULL:%s\r\n",
			NRF24L01_REG_STATUS,
			i,
			(i & 0x70) >> 4,
			(i & 0x0E) >> 1,
			(i & 0x01) ? "YES" : "NO"
		);

	// OBSERVE_TX
	i = NRF24L01_Read_Reg(NRF24L01_REG_OBSERVE_TX);
	printf("[0x%02X] 0x%02X PLOS_CNT=%u ARC_CNT=%u\r\n",NRF24L01_REG_OBSERVE_TX,i,i >> 4,i & 0x0F);

	// RPD
	i = NRF24L01_Read_Reg(NRF24L01_REG_RPD);
	printf("[0x%02X] 0x%02X RPD=%s\r\n",NRF24L01_REG_RPD,i,(i & 0x01) ? "YES" : "NO");

	// RX_ADDR_P0
	NRF24L01_Read_To_Buf(NRF24L01_REG_RX_ADDR_P0,buf,aw);
	printf("[0x%02X] RX_ADDR_P0 \"",NRF24L01_REG_RX_ADDR_P0);
	for (i = 0; i < aw; i++) printf("%X ",buf[i]);
	printf("\"\r\n");

  // RX_ADDR_P1
	NRF24L01_Read_To_Buf(NRF24L01_REG_RX_ADDR_P1,buf,aw);
	printf("[0x%02X] RX_ADDR_P1 \"",NRF24L01_REG_RX_ADDR_P1);
	for (i = 0; i < aw; i++) printf("%X ",buf[i]);
	printf("\"\r\n");

	// RX_ADDR_P2
	printf("[0x%02X] RX_ADDR_P2 \"",NRF24L01_REG_RX_ADDR_P2);
	for (i = 0; i < aw - 1; i++) printf("%X ",buf[i]);
	i = NRF24L01_Read_Reg(NRF24L01_REG_RX_ADDR_P2);
	printf("%X\"\r\n",i);

	// RX_ADDR_P3
	printf("[0x%02X] RX_ADDR_P3 \"",NRF24L01_REG_RX_ADDR_P3);
	for (i = 0; i < aw - 1; i++) printf("%X ",buf[i]);
	i = NRF24L01_Read_Reg(NRF24L01_REG_RX_ADDR_P3);
	printf("%X\"\r\n",i);

	// RX_ADDR_P4
	printf("[0x%02X] RX_ADDR_P4 \"",NRF24L01_REG_RX_ADDR_P4);
	for (i = 0; i < aw - 1; i++) printf("%X ",buf[i]);
	i = NRF24L01_Read_Reg(NRF24L01_REG_RX_ADDR_P4);
	printf("%X\"\r\n",i);

  // RX_ADDR_P5
	printf("[0x%02X] RX_ADDR_P5 \"",NRF24L01_REG_RX_ADDR_P5);
	for (i = 0; i < aw - 1; i++) printf("%X ",buf[i]);
	i = NRF24L01_Read_Reg(NRF24L01_REG_RX_ADDR_P5);
	printf("%X\"\r\n",i);

	// TX_ADDR
	NRF24L01_Read_To_Buf(NRF24L01_REG_TX_ADDR,buf,aw);
	printf("[0x%02X] TX_ADDR \"",NRF24L01_REG_TX_ADDR);
	for (i = 0; i < aw; i++) printf("%X ",buf[i]);
	printf("\"\r\n");

	// RX_PW_P0
	i = NRF24L01_Read_Reg(NRF24L01_REG_RX_PW_P0);
	printf("[0x%02X] RX_PW_P0=%u\r\n",NRF24L01_REG_RX_PW_P0,i);

	// RX_PW_P1
	i = NRF24L01_Read_Reg(NRF24L01_REG_RX_PW_P1);
	printf("[0x%02X] RX_PW_P1=%u\r\n",NRF24L01_REG_RX_PW_P1,i);

  // RX_PW_P2
	i = NRF24L01_Read_Reg(NRF24L01_REG_RX_PW_P2);
	printf("[0x%02X] RX_PW_P2=%u\r\n",NRF24L01_REG_RX_PW_P2,i);

	// RX_PW_P3
	i = NRF24L01_Read_Reg(NRF24L01_REG_RX_PW_P3);
	printf("[0x%02X] RX_PW_P3=%u\r\n",NRF24L01_REG_RX_PW_P3,i);

	// RX_PW_P4
	i = NRF24L01_Read_Reg(NRF24L01_REG_RX_PW_P4);
	printf("[0x%02X] RX_PW_P4=%u\r\n",NRF24L01_REG_RX_PW_P4,i);

	// RX_PW_P5
	i = NRF24L01_Read_Reg(NRF24L01_REG_RX_PW_P5);
	printf("[0x%02X] RX_PW_P5=%u\r\n",NRF24L01_REG_RX_PW_P5,i);
}
