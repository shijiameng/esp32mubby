# Mubby
MUBBY firmware for Espressif ESP32 platform

## 목차

> [개발환경 설정](#environment-setup)
> [How to Build](#how-to-build)

## 1. 개발환경 설정

무삐는 ESP32 Audio Development Framework (ESP-ADF)에 의해서 개발하였습니다. ESP-ADF의 개발환경을 우선 설치하여야 합니다.

#### 1.1 필수 패키지 설치

```bash
sudo apt-get install git wget make libncurses-dev flex bison gperf python python-pip python-setuptools python-serial
```

#### * Toolchain 설치

```bash
wget https://dl.espressif.com/dl/xtensa-esp32-elf-linux64-1.22.0-73-ge28a011-5.2.0.tar.gz
sudo tar xzvf xtensa-esp32-elf-linux64-1.22.0-73-ge28a011-5.2.0.tar.gz -C /opt
echo PATH=/opt/xtensa-esp32-elf/bin:$PATH >> ~/.bashrc
source ~/.bashrc
```
 
#### * ESP-ADF 설치

```bash
git clone --recursive https://github.com/espressif/esp-adf.git
```

#### * ESP-ADF의 경로를 PATH 환경 변수에 추가

```bash
echo ADF_PATH=/path/to/esp-adf >> ~/.bashrc
source ~/.bashrc
```

#### * MQTT 라이브러리 추가

```bash
cd $ADF_PATH/esp-idf
git submodule add https://github.com/tuanpmt/espmqtt.git components/espmqtt
```

## How to Build






