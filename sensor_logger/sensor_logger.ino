/*
 * 창가(window) 환경 로거 — Arduino Uno
 *
 * USB 시리얼로 측정값만 보낸다. CSV 파일은 노트북의 log_serial.py 가 만든다.
 * 시각은 PC 시계를 쓴다. Uno에는 RTC가 없다.
 *
 * 센서
 *   A0  MQ135 아날로그 출력 (AO)
 *   A1  DHT22 데이터 (아날로그 핀을 디지털 15로 사용)
 *   A2  조도(LDR) — 5V — LDR — A2 — 10k — GND
 *       밝을수록 LDR 저항이 줄고 ADC가 커진다.
 *
 * 기록 주기 10초. delay()는 쓰지 않는다.
 *   센서 읽기(DHT22는 수십~250ms) 뒤에 delay(10000)을 두면
 *   그 시간만큼 주기가 밀린다. millis()로 기록 시작 시각을 기준으로 맞춘다.
 *
 * 시리얼 115200, 한 줄에 측정값만 보낸다.
 *   mq135_adc,mq135_voltage,temperature,humidity,light_adc,light_voltage
 * DHT22 읽기 실패 시 온도·습도는 NaN.
 *
 * MQ135와 조도는 analogRead 만 쓴다.
 * DHT11 라이브러리는 온도 상위 바이트만 돌려준다.
 * DHT22는 온도를 10배 정수로 두 바이트에 넣으므로, 25.5도 이하면 상위 바이트가 0이다.
 * 그래서 그 라이브러리를 쓰면 실온이 계속 0으로 찍힌다. 여기서 두 바이트를 직접 합친다.
 */

const uint8_t PIN_MQ135 = A0;
const uint8_t PIN_DHT = A1;
const uint8_t PIN_LDR = A2;

const unsigned long LOG_INTERVAL_MS = 10000UL;
const float ADC_VREF = 5.0f;
// AVR ADC: 결과 = Vin * 1024 / Vref
const float ADC_STEPS = 1024.0f;

unsigned long lastLogMs = 0;

uint16_t pulseWidth(volatile uint8_t *reg, uint8_t mask, bool level) {
  uint16_t count = 0;
  if (level) {
    while ((*reg & mask) != 0) {
      if (++count >= 30000) {
        return 0;
      }
    }
  } else {
    while ((*reg & mask) == 0) {
      if (++count >= 30000) {
        return 0;
      }
    }
  }
  return count;
}

// DHT22: 습도·온도 모두 (상위<<8 | 하위) / 10. 실패 시 false.
bool readDHT22(float &temperature, float &humidity) {
  uint8_t data[5] = {0, 0, 0, 0, 0};
  uint16_t lowCount[40];
  uint16_t highCount[40];
  uint8_t mask = digitalPinToBitMask(PIN_DHT);
  volatile uint8_t *reg = portInputRegister(digitalPinToPort(PIN_DHT));

  pinMode(PIN_DHT, OUTPUT);
  digitalWrite(PIN_DHT, LOW);
  delay(2);
  digitalWrite(PIN_DHT, HIGH);
  delayMicroseconds(40);
  pinMode(PIN_DHT, INPUT_PULLUP);
  delayMicroseconds(10);

  noInterrupts();
  bool ok = pulseWidth(reg, mask, false) != 0 && pulseWidth(reg, mask, true) != 0;
  for (uint8_t i = 0; ok && i < 40; i++) {
    lowCount[i] = pulseWidth(reg, mask, false);
    highCount[i] = pulseWidth(reg, mask, true);
    if (lowCount[i] == 0 || highCount[i] == 0) {
      ok = false;
    }
  }
  interrupts();
  if (!ok) {
    return false;
  }

  for (uint8_t i = 0; i < 40; i++) {
    data[i / 8] <<= 1;
    if (highCount[i] > lowCount[i]) {
      data[i / 8] |= 1;
    }
  }
  if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
    return false;
  }

  humidity = ((uint16_t)data[0] << 8 | data[1]) * 0.1f;
  float temp = ((uint16_t)(data[2] & 0x7F) << 8 | data[3]) * 0.1f;
  if (data[2] & 0x80) {
    temp = -temp;
  }
  temperature = temp;
  return true;
}

float adcToVolt(int adc) {
  return adc * (ADC_VREF / ADC_STEPS);
}

void printOrNaN(float value, uint8_t digits) {
  if (isnan(value)) {
    Serial.print(F("NaN"));
  } else {
    Serial.print(value, digits);
  }
}

void logSample() {
  int mqAdc = analogRead(PIN_MQ135);
  int lightAdc = analogRead(PIN_LDR);

  float tempC = NAN;
  float hum = NAN;
  readDHT22(tempC, hum);

  Serial.print(mqAdc);
  Serial.print(',');
  Serial.print(adcToVolt(mqAdc), 3);
  Serial.print(',');
  printOrNaN(tempC, 1);
  Serial.print(',');
  printOrNaN(hum, 1);
  Serial.print(',');
  Serial.print(lightAdc);
  Serial.print(',');
  Serial.println(adcToVolt(lightAdc), 3);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_DHT, INPUT_PULLUP);
  // 첫 기록은 부팅 10초 뒤. DHT22 안정 시간을 그 안에 포함한다.
  // 포트가 열리면 Uno가 리셋되므로, PC가 연결한 시점부터 다시 센다.
  lastLogMs = millis();
}

void loop() {
  unsigned long nowMs = millis();
  // 기록 시작 시각 기준. 읽기에 걸린 시간은 다음 주기에 더하지 않는다.
  if (nowMs - lastLogMs >= LOG_INTERVAL_MS) {
    lastLogMs = nowMs;
    logSample();
  }
}
