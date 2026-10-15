# 변수표 (window)

설치 위치는 `window`이다. 노트북에 USB로 연결한 채 `log_serial.py`가 시리얼을 읽어 `data_window.csv`에 기록한다. 같은 값이 CSV 첫 줄과 `location` 컬럼에도 있다.

기록 주기는 10초이다. 지침 예시 `YYYY-MM-DD HH:MM`은 분 단위라 같은 분에 6행이 겹친다. 타임스탬프는 모든 행에서 `YYYY-MM-DD HH:MM:SS`로 통일했다.

라벨은 이벤트가 켜진 구간에만 이름을 쓰고, 그 밖에는 `none`만 쓴다. 빈 칸은 쓰지 않는다. 빈 칸은 pandas가 결측으로 읽어서 온도·습도 결측률과 섞인다.

| 변수명 | 단위 | 결측 | 설명 |
| --- | --- | --- | --- |
| timestamp | 없음. 형식 `YYYY-MM-DD HH:MM:SS` | 없음 | 노트북 로컬 시각. 시리얼로 한 줄을 받은 시점 |
| location | 없음 (범주) | 없음 | 설치 위치. 값 `window` |
| mq135_adc | 1 (무차원, 0–1023) | 없음 | A0, MQ135 모듈 AO. 10비트 ADC 원시값 |
| mq135_voltage | V | 없음 | `mq135_adc * 5.0 / 1024`. 기준전압 5.0 V 가정 |
| temperature | °C | `NaN` | A1, DHT22. 분해능 0.1°C. 읽기 실패면 `NaN` |
| humidity | %RH | `NaN` | A1, DHT22 상대습도. 분해능 0.1%RH. 실패 시 `NaN` |
| light_adc | 1 (무차원, 0–1023) | 없음 | A2. 5V — LDR — A2 — 10 kΩ — GND. 밝을수록 값이 커짐 |
| light_voltage | V | 없음 | `light_adc * 5.0 / 1024`. 기준전압 5.0 V 가정 |
| label | 없음 (범주) | 해당 없음 | 이벤트 구간의 이름. 평소는 `none`. 예: `cooking`, `ventilation` |

전압 변환은 ATmega328P ADC 식(결과 = Vin × 1024 / Vref)을 따른다. USB 전원의 실제 5V 레일이 5.0 V가 아니면 전압 열만 비례해서 어긋나고, ADC 원시값은 그대로다.

MQ135의 ppm은 청정 공기에서 잡은 R0 보정이 있어야 해서 기록하지 않는다. 공기질 지표는 `mq135_adc`와 `mq135_voltage`이다. 모듈은 예열(데이터시트 기준 24시간 이상이면 안정) 전에 값이 크게 움직인다.

## pandas

```python
import pandas as pd

df = pd.read_csv("data_window.csv", comment="#")
df["timestamp"] = pd.to_datetime(df["timestamp"])

# 온도·습도 결측률. label의 none은 결측이 아니다.
temp_missing_rate = df["temperature"].isna().mean()
hum_missing_rate = df["humidity"].isna().mean()
```

`NaN` 문자열은 `read_csv` 기본 결측값이다.
