#include "pal_transport.h"
#include "main.h"
#include <string.h>

extern UART_HandleTypeDef huart2;

#define UART_RX_BUF_SIZE 280

static volatile uint8_t rx_buf[UART_RX_BUF_SIZE];
static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;
static volatile uint8_t rx_byte;

static bool uart_send(const uint8_t *data, uint16_t len)
{
    return HAL_UART_Transmit(&huart2, data, len, 50) == HAL_OK;
}

static uint16_t uart_receive(uint8_t *buf, uint16_t max_len)
{
    uint16_t count = 0;
    while (count < max_len) {
        uint16_t h = rx_head;
        uint16_t t = rx_tail;
        if (h == t)
            break;
        buf[count++] = rx_buf[t];
        rx_tail = (t + 1) % UART_RX_BUF_SIZE;
    }
    return count;
}

static bool uart_tx_busy(void)
{
    return huart2.gState == HAL_UART_STATE_BUSY_TX;
}

void pal_transport_init(pal_transport_t *tp)
{
    rx_head = 0;
    rx_tail = 0;

    tp->send = uart_send;
    tp->receive = uart_receive;
    tp->tx_busy = uart_tx_busy;

    /* Start interrupt-driven receive of first byte */
    HAL_UART_Receive_IT(&huart2, (uint8_t *)&rx_byte, 1);
}

/* Called by HAL from USART2_IRQHandler when a byte is received */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        uint16_t next = (rx_head + 1) % UART_RX_BUF_SIZE;
        if (next != rx_tail) {
            rx_buf[rx_head] = rx_byte;
            rx_head = next;
        }
        /* Re-arm for next byte */
        HAL_UART_Receive_IT(&huart2, (uint8_t *)&rx_byte, 1);
    }
}
