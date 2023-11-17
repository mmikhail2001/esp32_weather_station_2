#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"

#include "dht11.h"

static gpio_num_t dht_gpio;
static int64_t last_read_time = -2000000;
static struct dht11_reading last_read;

static const char *TAG = "DHT11";

static int _waitOrTimeout(uint16_t microSeconds, int level) {
  int micros_ticks = 0;
  while (gpio_get_level(dht_gpio) == level) {
    if (micros_ticks++ > microSeconds)
      return DHT11_TIMEOUT_ERROR;
    ets_delay_us(1);
  }
  return micros_ticks;
}

static int _checkCRC(uint8_t data[]) {
  if (data[4] == (data[0] + data[1] + data[2] + data[3]))
    return DHT11_OK;
  else
    return DHT11_CRC_ERROR;
}

static void _sendStartSignal() {
  gpio_set_direction(dht_gpio, GPIO_MODE_OUTPUT);
  gpio_set_level(dht_gpio, 0);
  ets_delay_us(20 * 1000);
  gpio_set_level(dht_gpio, 1);
  ets_delay_us(40);
  gpio_set_direction(dht_gpio, GPIO_MODE_INPUT);
}

static int _checkResponse() {
  /* Wait for next step ~80us*/
  if (_waitOrTimeout(80, 0) == DHT11_TIMEOUT_ERROR)
    return DHT11_TIMEOUT_ERROR;

  /* Wait for next step ~80us*/
  if (_waitOrTimeout(80, 1) == DHT11_TIMEOUT_ERROR)
    return DHT11_TIMEOUT_ERROR;

  return DHT11_OK;
}

static struct dht11_reading _timeoutError() {
  struct dht11_reading timeoutError = {DHT11_TIMEOUT_ERROR, -1, -1};
  return timeoutError;
}

static struct dht11_reading _crcError() {
  struct dht11_reading crcError = {DHT11_CRC_ERROR, -1, -1};
  return crcError;
}

void DHT11_init(gpio_num_t gpio_num) {
  /* Wait 1 seconds to make the device pass its initial unstable status */
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  dht_gpio = gpio_num;
}

struct dht11_reading DHT11_read() {
  /* Tried to sense too son since last read (dht11 needs ~2 seconds to make a
   * new read) */
  if (esp_timer_get_time() - 2000000 < last_read_time) {
    return last_read;
  }

  last_read_time = esp_timer_get_time();

  uint8_t data[5] = {0, 0, 0, 0, 0};

  _sendStartSignal();

  if (_checkResponse() == DHT11_TIMEOUT_ERROR)
    return last_read = _timeoutError();

  /* Read response */
  for (int i = 0; i < 40; i++) {
    /* Initial data */
    if (_waitOrTimeout(50, 0) == DHT11_TIMEOUT_ERROR)
      return last_read = _timeoutError();

    if (_waitOrTimeout(70, 1) > 28) {
      /* Bit received was a 1 */
      data[i / 8] |= (1 << (7 - (i % 8)));
    }
  }

  if (_checkCRC(data) != DHT11_CRC_ERROR) {
    last_read.status = DHT11_OK;
    last_read.temperature = data[2];
    last_read.humidity = data[0];
    return last_read;
  } else {
    return last_read = _crcError();
  }
}

void dht11_read_task(void *arg) {
  lcd_data_t lcd_data;
  while (1) {
    struct dht11_reading sensor_data = {.temperature = DHT11_read().temperature,
                                        .humidity = DHT11_read().humidity};
    lcd_data.col = 0;
    // hum
    lcd_data.row = 1;
    sprintf(lcd_data.str, "hum %d", sensor_data.humidity);
    xQueueSendToBack(lcd_string_queue, &lcd_data, 0);
    vTaskDelay(300 / portTICK_PERIOD_MS);

    // delay
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}
