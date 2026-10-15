"""노트북에 연결된 Uno의 시리얼을 읽어 data_window.csv에 기록한다.

아두이노는 측정값 6개만 보낸다. 시각은 이 PC 시계로 붙인다.
라벨은 이벤트가 진행 중일 때만 이름을 쓰고, 그 외에는 none이다.

실행 예:
  python log_serial.py --port COM3

실행 중 입력:
  label cooking
  label none
  quit
"""

from __future__ import annotations

import argparse
import os
import sys
import threading
from datetime import datetime
from pathlib import Path

import serial
from serial.tools import list_ports

LOCATION = "window"
BAUD = 115200
COLUMNS = (
    "timestamp,location,mq135_adc,mq135_voltage,"
    "temperature,humidity,light_adc,light_voltage,label"
)
LABEL_CHARS = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")


def default_csv_path() -> Path:
    return Path(__file__).resolve().parent / "data_window.csv"


def list_port_names() -> list[str]:
    return [port.device for port in list_ports.comports()]


def choose_port(requested: str | None) -> str:
    names = list_port_names()
    if requested:
        return requested
    if len(names) == 1:
        return names[0]
    print("시리얼 포트를 --port 로 지정하세요.")
    if names:
        print("연결된 포트: " + ", ".join(names))
    else:
        print("연결된 시리얼 포트가 없습니다.")
    sys.exit(1)


def valid_label(token: str) -> bool:
    return 1 <= len(token) <= 15 and all(ch in LABEL_CHARS for ch in token)


def parse_sample(line: str) -> tuple[int, str, str, str, int, str] | None:
    parts = [part.strip() for part in line.strip().split(",")]
    if len(parts) != 6:
        return None
    try:
        mq_adc = int(parts[0])
        mq_v = float(parts[1])
        light_adc = int(parts[4])
        light_v = float(parts[5])
        temperature = format_measurement(parts[2], 1)
        humidity = format_measurement(parts[3], 1)
    except ValueError:
        return None
    return mq_adc, f"{mq_v:.3f}", temperature, humidity, light_adc, f"{light_v:.3f}"


def format_measurement(text: str, digits: int) -> str:
    if text == "NaN":
        return "NaN"
    return f"{float(text):.{digits}f}"


def ensure_header(path: Path, location: str) -> None:
    if path.exists() and path.stat().st_size > 0:
        return
    with path.open("w", encoding="utf-8", newline="\n") as file:
        file.write(f"# location={location}\n")
        file.write(COLUMNS + "\n")
        file.flush()
        os.fsync(file.fileno())


def append_row(path: Path, row: str) -> None:
    with path.open("a", encoding="utf-8", newline="\n") as file:
        file.write(row + "\n")
        file.flush()
        os.fsync(file.fileno())


def make_row(sample: tuple[int, str, str, str, int, str], location: str, label: str) -> str:
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    mq_adc, mq_v, temperature, humidity, light_adc, light_v = sample
    return (
        f"{timestamp},{location},{mq_adc},{mq_v},"
        f"{temperature},{humidity},{light_adc},{light_v},{label}"
    )


def reader(
    link: serial.Serial,
    path: Path,
    location: str,
    label_box: dict[str, str],
    label_lock: threading.Lock,
    stop: threading.Event,
) -> None:
    print(f"{link.port} 연결. 약 10초 뒤부터 {path.name}에 기록합니다.")
    try:
        while not stop.is_set():
            raw = link.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            if not line or line.startswith("#"):
                continue
            sample = parse_sample(line)
            if sample is None:
                print(f"무시: {line}")
                continue
            with label_lock:
                label = label_box["label"]
            row = make_row(sample, location, label)
            append_row(path, row)
            print(row)
    finally:
        link.close()


def handle_command(line: str, label_box: dict[str, str], label_lock: threading.Lock) -> bool:
    """True면 기록을 끝낸다."""
    text = line.strip()
    if text == "":
        return False
    if text == "quit":
        return True
    if not text.startswith("label "):
        print("명령: label <이름> | label none | quit")
        return False

    token = text[6:].strip()
    if token == "" or token == "none":
        with label_lock:
            label_box["label"] = "none"
        print("label none")
        return False
    if not valid_label(token):
        print("라벨은 영문, 숫자, _ 만, 1~15자")
        return False
    with label_lock:
        label_box["label"] = token
    print(f"label {token}")
    return False


def main() -> None:
    parser = argparse.ArgumentParser(description="Uno 시리얼을 data_window.csv로 저장")
    parser.add_argument("--port", help="예: COM3. 포트가 하나면 생략 가능")
    parser.add_argument("--location", default=LOCATION)
    parser.add_argument("--out", type=Path, default=None, help="기본값: 이 스크립트 옆 data_window.csv")
    args = parser.parse_args()

    path = args.out if args.out is not None else default_csv_path()
    port = choose_port(args.port)
    ensure_header(path, args.location)

    try:
        link = serial.Serial(port, BAUD, timeout=1)
    except serial.SerialException as exc:
        print(f"포트를 열 수 없습니다: {exc}")
        sys.exit(1)
    # 포트가 열리면 Uno가 리셋된다. 부팅 중 깨진 바이트는 버린다.
    link.reset_input_buffer()

    label_box = {"label": "none"}
    label_lock = threading.Lock()
    stop = threading.Event()
    worker = threading.Thread(
        target=reader,
        args=(link, path, args.location, label_box, label_lock, stop),
        daemon=True,
    )
    worker.start()
    print("라벨 변경: label cooking    해제: label none    종료: quit")

    try:
        while not stop.is_set():
            if handle_command(input(), label_box, label_lock):
                break
    except (KeyboardInterrupt, EOFError):
        pass
    finally:
        stop.set()
        worker.join(timeout=2)
        print(f"저장 위치: {path}")


if __name__ == "__main__":
    main()
