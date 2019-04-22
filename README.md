# Mubby
MUBBY firmware for Espressif ESP32 platform

## 목차

> [개발환경 설정](#environment-setup)
> [How to Build](#how-to-build)

## 1. 개발환경 설정

무삐는 ESP32 Audio Development Framework (ESP-ADF)에 의해서 개발하였습니다. ESP-ADF의 개발환경을 우선 설치하여야 합니다.

### 필수 패키지 설치

```bash
sudo apt-get install git wget make libncurses-dev flex bison gperf python python-pip python-setuptools python-serial
```

### Toolchain 설치

```bash
wget https://dl.espressif.com/dl/xtensa-esp32-elf-linux64-1.22.0-73-ge28a011-5.2.0.tar.gz
sudo tar xzvf xtensa-esp32-elf-linux64-1.22.0-73-ge28a011-5.2.0.tar.gz -C /opt
echo "export PATH=/opt/xtensa-esp32-elf/bin:$PATH" >> ~/.bashrc
source ~/.bashrc
```
 
### ESP-ADF 설치

```bash
git clone --recursive https://github.com/espressif/esp-adf.git
```

### ESP-ADF의 경로를 PATH 환경 변수에 추가

```bash
echo "export ADF_PATH=/path/to/esp-adf" >> ~/.bashrc
source ~/.bashrc
```

### MQTT 라이브러리 추가

```bash
cd $ADF_PATH/esp-idf
git submodule add https://github.com/tuanpmt/espmqtt.git components/espmqtt
```

## 2. How to Build

### 무삐 프로젝트 clone

```bash
git clone https://github.com/shijiameng/esp32mubby.git
```

### 클라이언트 인증서 및 키 발급

#### Private Key 및 인증서 청구 파일 생성
```bash
cd esp32mubby/main/certs
sh cert_req.sh
```

#### Certificate Authority (CA)에게서 사인 받기

위 절차에 생성된 인증서 청구 파일 client.req를 무삐 서버 관리자에게 보내며 클라이언트 인증서 파일cert.pem 및 CA인증서 파일 cacert.pem 청구하시오. cert.pem 및 cacert.pem을 디렉터리 mubby/main/certs에 넣으시오.

### 무삐 빌드

```bash
cd esp32mubby
make
```






