/*
 * This file is part of Cleanflight and Betaflight.
 *
 * Cleanflight and Betaflight are free software. You can redistribute
 * this software and/or modify this software under the terms of the
 * GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Cleanflight and Betaflight are distributed in the hope that they
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#ifdef USE_SPI

// STM32F405 can't DMA to/from FASTRAM (CCM SRAM)
#define IS_CCM(p) (((uint32_t)p & 0xffff0000) == 0x10000000)

#include "common/maths.h"
#include "drivers/bus.h"
#include "drivers/bus_spi.h"
#include "drivers/bus_spi_impl.h"
#include "drivers/exti.h"
#include "drivers/io.h"
#include "platform/rcc.h"

// Use DMA if possible if this many bytes are to be transferred
#define SPI_DMA_THRESHOLD 8

static SPI_InitTypeDef defaultInit = {
    .SPI_Mode = SPI_Mode_Master,
    .SPI_Direction = SPI_Direction_2Lines_FullDuplex,
    .SPI_DataSize = SPI_DataSize_8b,
    .SPI_NSS = SPI_NSS_Soft,
    .SPI_FirstBit = SPI_FirstBit_MSB,
    .SPI_CRCPolynomial = 7,
    .SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8,
    .SPI_CPOL = SPI_CPOL_High,
    .SPI_CPHA = SPI_CPHA_2Edge
};

static uint16_t spiDivisorToBRbits(const SPI_TypeDef *instance, uint16_t divisor)
{
    // SPI2 and SPI3 are on APB1/AHB1 which PCLK is half that of APB2/AHB2.
#if defined(STM32F410xx) || defined(STM32F411xE)
    UNUSED(instance);
#else
    if (instance == SPI2 || instance == SPI3) {
        divisor /= 2; // Safe for divisor == 0 or 1
    }
#endif

    divisor = constrain(divisor, 2, 256);

    return (ffs(divisor) - 2) << 3; // SPI_CR1_BR_Pos
}

static void spiSetDivisorBRreg(SPI_TypeDef *instance, uint16_t divisor)
{
#define BR_BITS ((BIT(5) | BIT(4) | BIT(3)))
    const uint16_t tempRegister = (instance->CR1 & ~BR_BITS);
    instance->CR1 = tempRegister | spiDivisorToBRbits(instance, divisor);
#undef BR_BITS
}

void spiInitDevice(SPIDevice device)
{
    spiDevice_t *spi = &(spiDevice[device]);

    if (!spi->dev) {
        return;
    }

    // Enable SPI clock
    RCC_ClockCmd(spi->rcc, ENABLE);
    RCC_ResetCmd(spi->rcc, ENABLE);

    IOInit(IOGetByTag(spi->sck),  OWNER_SPI_SCK,  RESOURCE_INDEX(device));
    IOInit(IOGetByTag(spi->miso), OWNER_SPI_SDI, RESOURCE_INDEX(device));
    IOInit(IOGetByTag(spi->mosi), OWNER_SPI_SDO, RESOURCE_INDEX(device));

    IOConfigGPIOAF(IOGetByTag(spi->sck),  SPI_IO_AF_SCK_CFG, spi->af);
    IOConfigGPIOAF(IOGetByTag(spi->miso), SPI_IO_AF_SDI_CFG, spi->af);
    IOConfigGPIOAF(IOGetByTag(spi->mosi), SPI_IO_AF_CFG, spi->af);

    // Init SPI hardware
    SPI_I2S_DeInit(spi->dev);

    SPI_I2S_DMACmd(spi->dev, SPI_I2S_DMAReq_Tx | SPI_I2S_DMAReq_Rx, DISABLE);
    SPI_Init(spi->dev, &defaultInit);
    SPI_Cmd(spi->dev, ENABLE);
}

void spiInternalResetDescriptors(busDevice_t *bus)
{
    DMA_InitTypeDef *dmaInitTx = bus->dmaInitTx;

    DMA_StructInit(dmaInitTx);
    dmaInitTx->DMA_Channel = bus->dmaTx->channel;
    dmaInitTx->DMA_DIR = DMA_DIR_MemoryToPeripheral;
    dmaInitTx->DMA_Mode = DMA_Mode_Normal;
    dmaInitTx->DMA_PeripheralBaseAddr = (uint32_t)&bus->busType_u.spi.instance->DR;
    dmaInitTx->DMA_Priority = DMA_Priority_Low;
    dmaInitTx->DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dmaInitTx->DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dmaInitTx->DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;

    if (bus->dmaRx) {
        DMA_InitTypeDef *dmaInitRx = bus->dmaInitRx;

        DMA_StructInit(dmaInitRx);
        dmaInitRx->DMA_Channel = bus->dmaRx->channel;
        dmaInitRx->DMA_DIR = DMA_DIR_PeripheralToMemory;
        dmaInitRx->DMA_Mode = DMA_Mode_Normal;
        dmaInitRx->DMA_PeripheralBaseAddr = (uint32_t)&bus->busType_u.spi.instance->DR;
        dmaInitRx->DMA_Priority = DMA_Priority_Low;
        dmaInitRx->DMA_PeripheralInc = DMA_PeripheralInc_Disable;
        dmaInitRx->DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    }
}

void spiInternalResetStream(dmaChannelDescriptor_t *descriptor)
{
    DMA_Stream_TypeDef *streamRegs = (DMA_Stream_TypeDef *)descriptor->ref;

    // Disable the stream
    streamRegs->CR = 0U;

    // Clear any pending interrupt flags
    DMA_CLEAR_FLAG(descriptor, DMA_IT_HTIF | DMA_IT_TEIF | DMA_IT_TCIF);
}

bool spiInternalReadWriteBufPolled(SPI_TypeDef *instance, const uint8_t *txData, uint8_t *rxData, int len)
{
    uint8_t b;

    while (len--) {
        b = txData ? *(txData++) : 0xFF;
        while (SPI_I2S_GetFlagStatus(instance, SPI_I2S_FLAG_TXE) == RESET);
        SPI_I2S_SendData(instance, b);

        while (SPI_I2S_GetFlagStatus(instance, SPI_I2S_FLAG_RXNE) == RESET);
        b = SPI_I2S_ReceiveData(instance);
        if (rxData) {
            *(rxData++) = b;
        }
    }

    return true;
}

void spiInternalInitStream(const extDevice_t *dev, volatile busSegment_t *segment)
{
    STATIC_DMA_DATA_AUTO uint8_t dummyTxByte = 0xff;
    STATIC_DMA_DATA_AUTO uint8_t dummyRxByte;
    busDevice_t *bus = dev->bus;
    int len = segment->len;

    uint8_t *txData = segment->u.buffers.txData;
    DMA_InitTypeDef *dmaInitTx = bus->dmaInitTx;

    if (txData) {
        dmaInitTx->DMA_Memory0BaseAddr = (uint32_t)txData;
        dmaInitTx->DMA_MemoryInc = DMA_MemoryInc_Enable;
    } else {
        dummyTxByte = 0xff;
        dmaInitTx->DMA_Memory0BaseAddr = (uint32_t)&dummyTxByte;
        dmaInitTx->DMA_MemoryInc = DMA_MemoryInc_Disable;
    }
    dmaInitTx->DMA_BufferSize = len;

    if (dev->bus->dmaRx) {
        uint8_t *rxData = segment->u.buffers.rxData;
        DMA_InitTypeDef *dmaInitRx = bus->dmaInitRx;

        if (rxData) {
            dmaInitRx->DMA_Memory0BaseAddr = (uint32_t)rxData;
            dmaInitRx->DMA_MemoryInc = DMA_MemoryInc_Enable;
        } else {
            dmaInitRx->DMA_Memory0BaseAddr = (uint32_t)&dummyRxByte;
            dmaInitRx->DMA_MemoryInc = DMA_MemoryInc_Disable;
        }
        // If possible use 16 bit memory writes to prevent atomic access issues on gyro data
        if ((dmaInitRx->DMA_Memory0BaseAddr & 0x1) || (len & 0x1)) {
            dmaInitRx->DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
        } else {
            dmaInitRx->DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
        }
        dmaInitRx->DMA_BufferSize = len;
    }
}

void spiInternalStartDMA(const extDevice_t *dev)
{
    dmaChannelDescriptor_t *dmaTx = dev->bus->dmaTx;
    dmaChannelDescriptor_t *dmaRx = dev->bus->dmaRx;
    DMA_Stream_TypeDef *streamRegsTx = (DMA_Stream_TypeDef *)dmaTx->ref;
    if (dmaRx) {
        DMA_Stream_TypeDef *streamRegsRx = (DMA_Stream_TypeDef *)dmaRx->ref;

        // Use the correct callback argument
        dmaRx->userParam = (uint32_t)dev;

        // Clear transfer flags
        DMA_CLEAR_FLAG(dmaTx, DMA_IT_HTIF | DMA_IT_TEIF | DMA_IT_TCIF);
        DMA_CLEAR_FLAG(dmaRx, DMA_IT_HTIF | DMA_IT_TEIF | DMA_IT_TCIF);

        // Disable streams to enable update
        streamRegsTx->CR = 0U;
        streamRegsRx->CR = 0U;

        /* Use the Rx interrupt as this occurs once the SPI operation is complete whereas the Tx interrupt
         * occurs earlier when the Tx FIFO is empty, but the SPI operation is still in progress
         */
        DMA_ITConfig(streamRegsRx, DMA_IT_TC, ENABLE);

        // Update streams
        DMA_Init(streamRegsTx, dev->bus->dmaInitTx);
        DMA_Init(streamRegsRx, dev->bus->dmaInitRx);

        /* Note from AN4031
         *
         * If the user enables the used peripheral before the corresponding DMA stream, a “FEIF”
         * (FIFO Error Interrupt Flag) may be set due to the fact the DMA is not ready to provide
         * the first required data to the peripheral (in case of memory-to-peripheral transfer).
         */

        // Enable streams
        DMA_Cmd(streamRegsTx, ENABLE);
        DMA_Cmd(streamRegsRx, ENABLE);

        /* Enable the SPI DMA Tx & Rx requests */
        SPI_I2S_DMACmd(dev->bus->busType_u.spi.instance, SPI_I2S_DMAReq_Tx | SPI_I2S_DMAReq_Rx, ENABLE);
    } else {
        // Use the correct callback argument
        dmaTx->userParam = (uint32_t)dev;

        // Clear transfer flags
        DMA_CLEAR_FLAG(dmaTx, DMA_IT_HTIF | DMA_IT_TEIF | DMA_IT_TCIF);

        // Disable stream to enable update
        streamRegsTx->CR = 0U;

        DMA_ITConfig(streamRegsTx, DMA_IT_TC, ENABLE);

        // Update stream
        DMA_Init(streamRegsTx, dev->bus->dmaInitTx);

        /* Note from AN4031
         *
         * If the user enables the used peripheral before the corresponding DMA stream, a “FEIF”
         * (FIFO Error Interrupt Flag) may be set due to the fact the DMA is not ready to provide
         * the first required data to the peripheral (in case of memory-to-peripheral transfer).
         */

        // Enable stream
        DMA_Cmd(streamRegsTx, ENABLE);

        /* Enable the SPI DMA Tx request */
        SPI_I2S_DMACmd(dev->bus->busType_u.spi.instance, SPI_I2S_DMAReq_Tx, ENABLE);
    }
}

void spiInternalStopDMA (const extDevice_t *dev)
{
    dmaChannelDescriptor_t *dmaTx = dev->bus->dmaTx;
    dmaChannelDescriptor_t *dmaRx = dev->bus->dmaRx;
    SPI_TypeDef *instance = dev->bus->busType_u.spi.instance;
    DMA_Stream_TypeDef *streamRegsTx = (DMA_Stream_TypeDef *)dmaTx->ref;

    if (dmaRx) {
        DMA_Stream_TypeDef *streamRegsRx = (DMA_Stream_TypeDef *)dmaRx->ref;

        // Disable streams
        streamRegsTx->CR = 0U;
        streamRegsRx->CR = 0U;

        SPI_I2S_DMACmd(instance, SPI_I2S_DMAReq_Tx | SPI_I2S_DMAReq_Rx, DISABLE);
    } else {
        // Ensure the current transmission is complete
        while (SPI_I2S_GetFlagStatus(instance, SPI_I2S_FLAG_BSY));

        // Drain the RX buffer
        while (SPI_I2S_GetFlagStatus(instance, SPI_I2S_FLAG_RXNE)) {
            instance->DR;
        }

        // Disable stream
        streamRegsTx->CR = 0U;

        SPI_I2S_DMACmd(instance, SPI_I2S_DMAReq_Tx, DISABLE);
    }
}

// DMA transfer setup and start
void spiSequenceStart(const extDevice_t *dev)
{
    busDevice_t *bus = dev->bus;
    SPI_TypeDef *instance = bus->busType_u.spi.instance;
    bool dmaSafe = dev->useDMA;
    uint32_t xferLen = 0;
    uint32_t segmentCount = 0;

    dev->bus->initSegment = true;

    SPI_Cmd(instance, DISABLE);

    // Switch bus speed
    if (dev->busType_u.spi.speed != bus->busType_u.spi.speed) {
        spiSetDivisorBRreg(bus->busType_u.spi.instance, dev->busType_u.spi.speed);
        bus->busType_u.spi.speed = dev->busType_u.spi.speed;
    }

    if (dev->busType_u.spi.leadingEdge != bus->busType_u.spi.leadingEdge) {
        // Switch SPI clock polarity/phase
        instance->CR1 &= ~(SPI_CPOL_High | SPI_CPHA_2Edge);

        // Apply setting
        if (dev->busType_u.spi.leadingEdge) {
            instance->CR1 |= SPI_CPOL_Low | SPI_CPHA_1Edge;
        } else
        {
            instance->CR1 |= SPI_CPOL_High | SPI_CPHA_2Edge;
        }
        bus->busType_u.spi.leadingEdge = dev->busType_u.spi.leadingEdge;
    }

    SPI_Cmd(instance, ENABLE);

    // Check that any there are no attempts to DMA to/from CCD SRAM
    for (busSegment_t *checkSegment = (busSegment_t *)bus->curSegment; checkSegment->len; checkSegment++) {
        // Check there is no receive data as only transmit DMA is available
        if (((checkSegment->u.buffers.rxData) && (IS_CCM(checkSegment->u.buffers.rxData) || (bus->dmaRx == (dmaChannelDescriptor_t *)NULL))) ||
            ((checkSegment->u.buffers.txData) && IS_CCM(checkSegment->u.buffers.txData))) {
            dmaSafe = false;
            break;
        }
        // Note that these counts are only valid if dmaSafe is true
        segmentCount++;
        xferLen += checkSegment->len;
    }
    // Use DMA if possible
    // If there are more than one segments, or a single segment with negateCS negated in the list terminator then force DMA irrespective of length
    if (bus->useDMA && dmaSafe && ((segmentCount > 1) ||
                                   (xferLen >= SPI_DMA_THRESHOLD) ||
                                   !bus->curSegment[segmentCount].negateCS)) {
        spiProcessSegmentsDMA(dev);
    } else {
        spiProcessSegmentsPolled(dev);
    }
}
#endif
