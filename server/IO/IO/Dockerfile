FROM ubuntu:20.04

# 필요한 패키지 설치
RUN apt-get update && apt-get install -y \
    g++ \
    cmake \
    make \
    git \
    libprotobuf-dev \
    protobuf-compiler \
    && rm -rf /var/lib/apt/lists/*

# 작업 디렉터리 설정
WORKDIR /app

# 프로젝트 파일 복사
COPY . /app

# 프로젝트 빌드
RUN mkdir build && cd build && cmake .. && make

# 실행할 커맨드
CMD ["./build/your_executable"]