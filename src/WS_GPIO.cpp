#include "WS_GPIO.h"
#include "WS_Information.h"

/*************************************************************  I/O  *************************************************************/
void digitalToggle(int pin)
{
  digitalWrite(pin, !digitalRead(pin));                                         // Toggle the state of the pin
}
void RGB_Light(uint8_t red_val, uint8_t green_val, uint8_t blue_val)
{
  neopixelWrite(GPIO_PIN_RGB,green_val,red_val,blue_val);                       // RGB color adjustment
}
void Buzzer_PWM(uint16_t Time)
{
  ledcWrite(PWM_Channel, Dutyfactor);
  delay(Time);
  ledcWrite(PWM_Channel, 0);
}
void Buzzer_Startup_Melody(uint16_t total_ms)
{
  if (!STARTUP_BUZZER_Enable || total_ms == 0) {
    return;
  }

  // freq(Hz), duration(ms) - short startup melody looped to total_ms
  static const uint16_t notes[][2] = {
    {523, 180}, {659, 180}, {784, 220}, {659, 180},
    {523, 180}, {392, 220}, {523, 260}, {0, 120}
  };
  const size_t note_count = sizeof(notes) / sizeof(notes[0]);
  size_t idx = 0;
  const uint32_t start_ms = millis();

  while ((millis() - start_ms) < total_ms) {
    uint16_t freq = notes[idx][0];
    uint16_t dur = notes[idx][1];
    const uint32_t elapsed = millis() - start_ms;
    if (elapsed + dur > total_ms) {
      dur = (uint16_t)(total_ms - elapsed);
    }

    ledcWriteTone(PWM_Channel, freq);
    delay(dur);
    idx = (idx + 1) % note_count;
  }

  ledcWriteTone(PWM_Channel, 0);
}
void GPIO_Init()
{
  /*************************************************************************
  Relay GPIO
  *************************************************************************/
  pinMode(GPIO_PIN_CH1, OUTPUT);                            // Initialize the control GPIO of relay CH1
  pinMode(GPIO_PIN_CH2, OUTPUT);                            // Initialize the control GPIO of relay CH2
  pinMode(GPIO_PIN_CH3, OUTPUT);                            // Initialize the control GPIO of relay CH3
  pinMode(GPIO_PIN_CH4, OUTPUT);                            // Initialize the control GPIO of relay CH4
  pinMode(GPIO_PIN_CH5, OUTPUT);                            // Initialize the control GPIO of relay CH5
  pinMode(GPIO_PIN_CH6, OUTPUT);                            // Initialize the control GPIO of relay CH6
  pinMode(GPIO_PIN_RGB, OUTPUT);                            // Initialize the control GPIO of RGB
  pinMode(GPIO_PIN_Buzzer, OUTPUT);                         // Initialize the control GPIO of Buzzer

  ledcSetup(PWM_Channel, Frequency, Resolution);            // Set a LEDC channel
  ledcAttachPin(GPIO_PIN_Buzzer, PWM_Channel);              // Connect the channel to the corresponding pin
}
