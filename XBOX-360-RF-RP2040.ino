/* Código Arduino para comunicar com o módulo RF do Xbox 360.
Modificado para adicionar checagem de CLK, retry e comandos iniciais.
Ajustado para RP2040 (Pi Pico e RP2040 Zero) com pull-ups extras para estabilidade.
Adicionado suporte para LED constante: azul no GPIO16 do RP2040-Zero (WS2812 RGB) e aceso no GPIO25 do Pi Pico (LED verde onboard).
Os #define sync_pin, data_pin e clock_pin estão definidos como GPIOs 2, 3 e 4, que são válidos para ambos os boards RP2040 (verifique o pinout físico: no Pico são pins 4,5,6; no Zero são pins semelhantes).
Instale a biblioteca Adafruit NeoPixel no Arduino IDE se usar RP2040-Zero (Ferramentas > Gerenciar Bibliotecas > Busque "Adafruit NeoPixel"). */

#include <pico/stdlib.h>  // Para funções gpio_ do Pico SDK

#if defined(ARDUINO_WAVESHARE_RP2040_ZERO)
#include <Adafruit_NeoPixel.h>
#define LED_PIN 16
Adafruit_NeoPixel pixels(1, LED_PIN, NEO_GRB + NEO_KHZ800);
#elif defined(ARDUINO_RASPBERRY_PI_PICO)
#define LED_PIN 25
#else
#warning "Board não suportado para LED. Defina manualmente."
#endif

#define sync_pin 2  // (RP2040-Pi Pico GPIO2=PIN4), (RP2040-Zero GPIO2=PIN3)
#define data_pin 3  // (RP2040-Pi Pico GPIO3=PIN5), (RP2040-Zero GPIO3=PIN4)
#define clock_pin 4  // (RP2040-Pi Pico GPIO4=PIN6), (RP2040-Zero Pico GPIO4=PIN5)

int start_cmd[10] = {0,0,0,0,0,1,0,0,1,0};
int power_cmd[10] = {0,0,1,0,0,0,0,1,0,1};
int sync_cmd[11] = {0,0,0,0,0,0,0,1,0,0,1};
int sync_cmd2[10] = {0,0,0,0,0,0,0,1,0,0};

volatile boolean sync_enable = 0;
unsigned long sync_press_time = 0;

int turn_off_cmd[10] = {0,0,0,0,0,0,1,0,0,1};
int led_red[10] = {0,0,1,0,1,1,1,1,1,1};
int led_red_off[10] = {0,0,1,0,1,1,0,0,0,0};
int led_init[10] = {0,0,1,0,0,0,0,1,0,0};

boolean sendData(int cmd_do[], int bits = 10) {  // Retorna true se enviado com sucesso
  pinMode(data_pin, OUTPUT);
  digitalWrite(data_pin, LOW);
  int prev = 1;
  unsigned long timeout = millis();
  for(int i = 0; i < bits; i++){
    while (prev == digitalRead(clock_pin) && millis() - timeout < 100) {}  // Timeout para evitar trava
    if (millis() - timeout >= 100) {
      Serial.println("Timeout no CLK! Verifique pull-ups.");
      digitalWrite(data_pin, HIGH);
      pinMode(data_pin, INPUT);
      gpio_set_function(data_pin, GPIO_FUNC_SIO);  // Reconfigura SIO
      gpio_pull_up(data_pin);  // Reativa pull-up
      return false;
    }
    prev = digitalRead(clock_pin);
    digitalWrite(data_pin, cmd_do[i]);

    timeout = millis();
    while (prev == digitalRead(clock_pin) && millis() - timeout < 100) {}
    if (millis() - timeout >= 100) return false;
    prev = digitalRead(clock_pin);
  }
  digitalWrite(data_pin, HIGH);
  pinMode(data_pin, INPUT);
  gpio_set_function(data_pin, GPIO_FUNC_SIO);  // Garante SIO após OUTPUT
  gpio_set_input_enabled(data_pin, true);  // Ativa input buffer
  gpio_pull_up(data_pin);  // Reativa pull-up após modo OUTPUT
  return true;
}

void sendData11(int cmd_do[]) {
  sendData(cmd_do, 11);
}

void initLEDs(){
  Serial.println("Inicializando...");
  // Tente enviar start e power primeiro
  sendData(start_cmd);
  delay(100);
  sendData(power_cmd);
  delay(100);
  
  // Retry até 3x para led_init
  for (int retry = 0; retry < 3; retry++) {
    if (sendData(led_init)) break;
    Serial.print("Retry init: "); Serial.println(retry + 1);
    delay(500);
  }
  delay(50);
  // Adicione animação se quiser: sendData(anim_cmd); delay(50);
}

void setup() {
  Serial.begin(9600);
  
  // Configuração com Pico SDK: Pull-ups internos para 3.3V em todos os pins
  // Passo 1: Inicializa e seta função SIO (essencial para controle software)
  gpio_init(sync_pin);
  gpio_set_function(sync_pin, GPIO_FUNC_SIO);
  gpio_set_dir(sync_pin, GPIO_IN);
  gpio_set_input_enabled(sync_pin, true);  // Ativa buffer de input (IE=1)
  // Workaround para resetar estado (evita latch ou pull-down default)
  gpio_set_dir(sync_pin, GPIO_OUT);
  gpio_put(sync_pin, 1);
  sleep_ms(1);
  gpio_set_dir(sync_pin, GPIO_IN);
  gpio_pull_up(sync_pin);  // Ativa pull-up (PUE=1, PDE=0)
  
  gpio_init(data_pin);
  gpio_set_function(data_pin, GPIO_FUNC_SIO);
  gpio_set_dir(data_pin, GPIO_IN);
  gpio_set_input_enabled(data_pin, true);
  gpio_set_dir(data_pin, GPIO_OUT);
  gpio_put(data_pin, 1);
  sleep_ms(1);
  gpio_set_dir(data_pin, GPIO_IN);
  gpio_pull_up(data_pin);
  
  gpio_init(clock_pin);
  gpio_set_function(clock_pin, GPIO_FUNC_SIO);
  gpio_set_dir(clock_pin, GPIO_IN);
  gpio_set_input_enabled(clock_pin, true);
  gpio_set_dir(clock_pin, GPIO_OUT);
  gpio_put(clock_pin, 1);
  sleep_ms(1);
  gpio_set_dir(clock_pin, GPIO_IN);
  gpio_pull_up(clock_pin);
  
  // Teste: Verifique estado inicial (deve ser 1 para high/3.3V)
  Serial.print("Estado inicial GPIO2 (sync_pin): ");
  Serial.println(gpio_get(sync_pin));
  Serial.print("Estado inicial GPIO3 (data_pin): ");
  Serial.println(gpio_get(data_pin));
  Serial.print("Estado inicial GPIO4 (clock_pin): ");
  Serial.println(gpio_get(clock_pin));
  
  delay(5000);  // Aumentado para estabilizar

#if defined(ARDUINO_WAVESHARE_RP2040_ZERO)
  pixels.begin();
  pixels.setPixelColor(0, pixels.Color(0, 0, 255));  // Azul constante (R=0, G=0, B=255)
  pixels.show();
#elif defined(ARDUINO_RASPBERRY_PI_PICO)
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // Acende o LED onboard (verde no Pico)
#endif

  initLEDs();
}

void loop(){
  if (digitalRead(sync_pin) == LOW) {
    if (sync_press_time == 0) {
      sync_press_time = millis();
      Serial.println("Botão sync pressionado...");
    }
    if (millis() - sync_press_time > 100) {
      if (!sync_enable) {
        Serial.println("Syncing.");
        sendData11(sync_cmd);
        sync_enable = true;
      }
    }
  } else {
    sync_press_time = 0;
    sync_enable = false;
  }
  
  delay(50);
}