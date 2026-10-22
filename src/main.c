// Bare-metal "hello world" for the NUCLEO-L073RZ: blinks LD2 and prints a
// counter over the ST-LINK virtual COM port. No HAL; registers are accessed
// directly through the CMSIS device header.

#include "stm32l0xx.h"

#define BAUD 115200

// Milliseconds since SysTick was started. volatile because it is written by
// the interrupt handler and read by main-line code.
static volatile uint32_t ticks;

// Fires once per millisecond (configured in clock_init). The name must match
// the vector table entry in the startup file.
void SysTick_Handler(void) {
  ticks++;
}

// Busy-wait for the given number of milliseconds. The unsigned subtraction
// stays correct when ticks wraps around past 0xFFFFFFFF.
static void delay_ms(uint32_t ms) {
  uint32_t start = ticks;
  while (ticks - start < ms);
}

// Switch from the 2.1 MHz MSI reset clock to the 16 MHz HSI.
static void clock_init(void) {
  // Turn on the HSI oscillator and wait until it is stable.
  RCC->CR |= RCC_CR_HSION;
  while (!(RCC->CR & RCC_CR_HSIRDY));
  // Flash can't keep up with 16 MHz at the default core voltage with zero
  // wait states, so add one before raising the clock.
  FLASH->ACR |= FLASH_ACR_LATENCY;
  // Select HSI as the system clock and wait for the switch to take effect.
  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSI;
  while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI);
  // Recompute SystemCoreClock from the new RCC settings, then set SysTick
  // to interrupt every 1 ms.
  SystemCoreClockUpdate();
  SysTick_Config(SystemCoreClock / 1000);
}

// LD2 on PA5.
static void led_init(void) {
  // Enable the GPIOA peripheral clock; its registers are dead until this is set.
  RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
  // MODER has 2 bits per pin: 01 = general-purpose output.
  GPIOA->MODER = (GPIOA->MODER & ~GPIO_MODER_MODE5) | GPIO_MODER_MODE5_0;
}

// USART2 on PA2 (TX) / PA3 (RX), routed to the ST-LINK virtual COM port.
static void uart_init(void) {
  RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
  // Put PA2 and PA3 in alternate-function mode (MODER = 10).
  GPIOA->MODER = (GPIOA->MODER & ~(GPIO_MODER_MODE2 | GPIO_MODER_MODE3))
               | GPIO_MODER_MODE2_1 | GPIO_MODER_MODE3_1;
  // AFR[0] holds 4 bits per pin for pins 0-7; PA2 is bits 8-11, PA3 is
  // bits 12-15. AF4 connects them to USART2.
  GPIOA->AFR[0] = (GPIOA->AFR[0] & ~(0xFFu << 8)) | (4u << 8) | (4u << 12);
  // With 16x oversampling the divider is clock / baud, rounded to nearest.
  USART2->BRR = (SystemCoreClock + BAUD / 2) / BAUD;
  // Enable transmitter, receiver, and the USART itself (8N1 by default).
  USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

// Send one byte, waiting until the transmit data register is empty.
static void uart_putc(char c) {
  while (!(USART2->ISR & USART_ISR_TXE));
  USART2->TDR = c;
}

// Send a NUL-terminated string.
static void uart_puts(const char *s) {
  while (*s) uart_putc(*s++);
}

// Print an unsigned integer in decimal without pulling in printf. Digits are
// produced least-significant first, so they are buffered and sent in reverse.
// 10 chars is enough for the largest uint32_t (4294967295).
static void uart_putu(uint32_t v) {
  char buf[10];
  int i = 0;
  do buf[i++] = '0' + v % 10; while (v /= 10);
  while (i) uart_putc(buf[--i]);
}

int main(void) {
  // Clock first: the UART baud divider depends on SystemCoreClock.
  clock_init();
  led_init();
  uart_init();
  uart_puts("L073RZ bare-metal up\r\n");

  // Toggle the LED and print the loop count every 500 ms, forever.
  for (uint32_t n = 0;; n++) {
    GPIOA->ODR ^= GPIO_ODR_OD5;
    uart_puts("tick ");
    uart_putu(n);
    uart_puts("\r\n");
    delay_ms(500);
  }
}
