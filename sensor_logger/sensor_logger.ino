/*
 * 창가(window) 환경 로거 — Arduino Uno
 *
 * USB 시리얼로 측정값만 보낸다. CSV 파일은 노트북의 log_serial.py 가 만든다.
 * 시각은 PC 시계를 쓴다. Uno에는 RTC가 없다.
 *
 * 센서
 *   A0  MQ135 아날로그 출력 (AO)
 *   A1  DHT11 데이터 (아날로그 핀을 디지털 15로 사용)
 *   A2  조도(LDR) — 5V — LDR — A2 — 10k — GND
 *       밝을수록 LDR 저항이 줄고 ADC가 커진다.
 *
 * 기록 주기 10초. delay()는 쓰지 않는다.
 *   센서 읽기(DHT22는 수십~250ms) 뒤에 delay(10000)을 두면
 *   그 시간만큼 주기가 밀린다. millis()로 기록 시작 시각을 기준으로 맞춘다.
 *
 * 시리얼 115200, 한 줄에 측정값만 보낸다.
 *   mq135_adc,mq135_voltage,temperature,humidity,light_adc,light_voltage
 * DHT11 읽기 실패 시 온도·습도는 NaN.
 *
 * MQ135와 조도는 analogRead 만 쓴다.
 * 온습도는 스케치북 libraries\DHT11 의 DHT11.h 를 쓴다.
 *   (DHT.h 는 Adafruit 라이브러리 헤더라, 이 설치본에는 없다.)
 */

#include <DHT11.h>

const uint8_t PIN_MQ135 = A0;
const uint8_t PIN_DHT = A1;
const uint8_t PIN_LDR = A2;

const unsigned long LOG_INTERVAL_MS = 10000UL;
const float ADC_VREF = 5.0f;
// AVR ADC: 결과 = Vin * 1024 / Vref
const float ADC_STEPS = 1024.0f;

DHT11 dht(PIN_DHT);

unsigned long lastLogMs = 0;

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

  // 이 라이브러리는 실패 시 253(타임아웃), 254(체크섬)를 반환한다. CSV에는 NaN으로 적는다.
  int tempRaw = 0;
  int humRaw = 0;
  int dhtStatus = dht.readTemperatureHumidity(tempRaw, humRaw);
  float tempC = (dhtStatus == 0) ? tempRaw : NAN;
  float hum = (dhtStatus == 0) ? humRaw : NAN;

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
  // 읽기 간격은 loop의 millis()가 맡는다. 라이브러리 기본 500ms 대기는 끈다.
  dht.setDelay(0);
  // 첫 기록은 부팅 10초 뒤. DHT11 안정 시간을 그 안에 포함한다.
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
