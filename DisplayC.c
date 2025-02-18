#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "pico/time.h"
#include "inc/ssd1306.h"

// Definição de pinos para comunicação com dispositivos externos
#define I2C_PORT        i2c1  // Interface I2C para comunicação com o display OLED
#define I2C_SDA         14    // Pino de dados I2C
#define I2C_SCL         15    // Pino de clock I2C
#define OLED_ADDR       0x3C  // Endereço do display OLED na I2C
#define WIDTH           128   // Largura do display em pixels
#define HEIGHT          64    // Altura do display em pixels

// Definição de pinos para LEDs e botões
#define LED_G           11  // LED Verde
#define LED_B           12  // LED Azul
#define LED_R           13  // LED Vermelho
#define BTN_A           5   // Botão A
#define JOYSTICK_BTN    22  // Botão do joystick
#define JOYSTICK_X_ADC  26  // Entrada ADC para eixo X do joystick
#define JOYSTICK_Y_ADC  27  // Entrada ADC para eixo Y do joystick
#define ADC_MAX         4095 // Valor máximo do ADC
#define ADC_CENTER      2048 // Valor central do ADC, joystick parado

// Variáveis globais para controle de estado
static volatile bool ledG_state = false;  // Estado do LED verde
static volatile bool g_pwm_active = true; // Estado do PWM para LEDs
static volatile bool border_active = false; // Alterna a borda no display OLED
static volatile uint32_t ultimo_tempo_a = 0; // Tempo do último pressionamento do Botão A
static volatile uint32_t ultimo_tempo_js = 0; // Tempo do último pressionamento do joystick
const uint32_t debounce_delay_us = 200000; // Tempo de debounce para evitar acionamentos repetidos
static ssd1306_t ssd; // Estrutura para controle do display OLED

// Função para inicializar PWM em um pino especificado
static void pwm_init_pin(uint gpio) {
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(gpio);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_wrap(&cfg, 65535);
    pwm_init(slice_num, &cfg, true);
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(gpio), 0);
}

//  Define o duty cycle do PWM, controlando o brilho do LED
static void pwm_set_duty(uint gpio, uint16_t duty) {
    if (!g_pwm_active) {
        duty = 0; // Se o PWM estiver desativado, define duty como 0
    }
    pwm_set_chan_level(pwm_gpio_to_slice_num(gpio), pwm_gpio_to_channel(gpio), duty);
}

// Atualiza a tela OLED com a posição do quadrado e borda opc
static void update_display(int sx, int sy) {
    ssd1306_fill(&ssd, false); // Limpa o display
    ssd1306_rect(&ssd, sx, sy, 8, 8, true, true); // Desenha o quadrado representando a posição do joystick

    if (border_active) {
        // Desenha apenas as bordas 
        ssd1306_line(&ssd, 0, 0, WIDTH - 1, 0, true); // Linha superior
        ssd1306_line(&ssd, 0, HEIGHT - 1, WIDTH - 1, HEIGHT - 1, true); // Linha inferior
        ssd1306_line(&ssd, 0, 0, 0, HEIGHT - 1, true); // Linha esquerda
        ssd1306_line(&ssd, WIDTH - 1, 0, WIDTH - 1, HEIGHT - 1, true); // Linha direita
    }

    ssd1306_send_data(&ssd); // Atualiza o display
}

// Função de callback para interrupções dos botões
static void gpio_callback(uint gpio, uint32_t events) {
    uint32_t agora = time_us_32();
    if (gpio == BTN_A && (agora - ultimo_tempo_a) > debounce_delay_us) {
        ultimo_tempo_a = agora;
        g_pwm_active = !g_pwm_active; // Alterna estado do PWM
    } else if (gpio == JOYSTICK_BTN && (agora - ultimo_tempo_js) > debounce_delay_us) {
        ultimo_tempo_js = agora;
        ledG_state = !ledG_state;
        gpio_put(LED_G, ledG_state); // Alterna estado do LED Verde
        border_active = !border_active; // Alterna estado da borda do display
    }
}

// Função principal
int main() {
    stdio_init_all(); // Inicializa comunicação serial
    i2c_init(I2C_PORT, 400 * 1000); // Inicializa I2C
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, OLED_ADDR, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);
    gpio_init(LED_G);
    gpio_set_dir(LED_G, GPIO_OUT);
    pwm_init_pin(LED_R);
    pwm_init_pin(LED_B);
    gpio_init(BTN_A);
    gpio_set_dir(BTN_A, GPIO_IN);
    gpio_pull_up(BTN_A);
    gpio_set_irq_enabled_with_callback(BTN_A, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_init(JOYSTICK_BTN);
    gpio_set_dir(JOYSTICK_BTN, GPIO_IN);
    gpio_pull_up(JOYSTICK_BTN);
    gpio_set_irq_enabled(JOYSTICK_BTN, GPIO_IRQ_EDGE_FALL, true);
    adc_init();
    adc_gpio_init(JOYSTICK_X_ADC);
    adc_gpio_init(JOYSTICK_Y_ADC);
    int square_x = (HEIGHT - 8) / 2;
    int square_y = (WIDTH - 8) / 2;
    
    while (true) {
        adc_select_input(0);
        uint16_t x_adc = adc_read();
        adc_select_input(1);
        uint16_t y_adc = adc_read();
        
        uint32_t duty_r = abs(x_adc - ADC_CENTER) * 32;
        uint32_t duty_b = abs(y_adc - ADC_CENTER) * 32;
        
        if (duty_r > 65535) duty_r = 65535;
        if (duty_b > 65535) duty_b = 65535;
        
        pwm_set_duty(LED_R, duty_r);
        pwm_set_duty(LED_B, duty_b);
        
        square_x = ((ADC_MAX - x_adc) * (HEIGHT - 8)) / ADC_MAX;
        square_y = (y_adc * (WIDTH - 8)) / ADC_MAX;
        
        update_display(square_x, square_y);
        sleep_ms(40);
    }
    return 0;
}
