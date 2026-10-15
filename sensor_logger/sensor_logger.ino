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

bool waitPin(uint8_t level, unsigned long timeoutUs) {
  unsigned long start = micros();
  while (digitalRead(PIN_DHT) == level) {
    if (micros() - start > timeoutUs) {
      return false;
    }
  }
  return true;
}

int readDhtByte() {
  int value = 0;
  for (uint8_t i = 0; i < 8; i++) {
    if (!waitPin(LOW, 300)) {
      return -1;
    }
    delayMicroseconds(30);
    if (digitalRead(PIN_DHT) == HIGH) {
      value |= (1 << (7 - i));
    }
    if (!waitPin(HIGH, 300)) {
      return -1;
    }
  }
  return value;
}

// 상위 바이트만 보면 25.5도 이하가 0이 된다. 두 바이트를 합친 뒤 10으로 나눈다.
bool readDHT22(float &temperature, float &humidity) {
  uint8_t data[5];

  pinMode(PIN_DHT, OUTPUT);
  digitalWrite(PIN_DHT, LOW);
  delay(18);
  digitalWrite(PIN_DHT, HIGH);
  delayMicroseconds(40);
  pinMode(PIN_DHT, INPUT);

  if (!waitPin(HIGH, 1000)) {
    return false;
  }
  if (digitalRead(PIN_DHT) != LOW) {
    return false;
  }
  delayMicroseconds(80);
  if (digitalRead(PIN_DHT) != HIGH) {
    return false;
  }
  delayMicroseconds(80);

  for (uint8_t i = 0; i < 5; i++) {
    int byteValue = readDhtByte();
    if (byteValue < 0) {
      return false;
    }
    data[i] = (uint8_t)byteValue;
  }
  if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
    return false;
  }
  // 0 다섯 바이트는 체크섬도 0이라 정상 프레임처럼 보인다.
  if ((data[0] | data[1] | data[2] | data[3]) == 0) {
    return false;
  }

  float temp22 = ((uint16_t)(data[2] & 0x7F) << 8 | data[3]) * 0.1f;
  if (data[2] & 0x80) {
    temp22 = -temp22;
  }
  float hum22 = ((uint16_t)data[0] << 8 | data[1]) * 0.1f;
  if (hum22 >= 0 && hum22 <= 100 && temp22 >= -40 && temp22 <= 80) {
    temperature = temp22;
    humidity = hum22;
    return true;
  }

  // 정수만 보내는 DHT11이면 상위 바이트가 곧 측정값이다.
  if (data[1] <= 9 && data[3] <= 9 && data[0] <= 100 && data[2] <= 50) {
    temperature = data[2];
    humidity = data[0];
    return true;
  }
  return false;
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
