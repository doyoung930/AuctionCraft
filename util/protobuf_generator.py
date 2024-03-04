import subprocess
import glob
import os

# .proto 파일들이 위치한 디렉터리
proto_files_directory = '../server/IO/IO/proto'
# 컴파일된 파일들을 저장할 디렉터리
output_directory = './proto_gen'

# 출력 디렉터리가 없다면 생성
if not os.path.exists(output_directory):
    os.makedirs(output_directory)

# 지정된 디렉터리에서 모든 .proto 파일을 찾음
proto_files = glob.glob(os.path.join(proto_files_directory, '*.proto'))

# 각 .proto 파일에 대해 protoc 명령어 실행
for proto_file in proto_files:
    command = [
        'protoc',
        f'--cpp_out={output_directory}',  # C++ 소스 코드를 저장할 디렉터리 지정
        f'-I{proto_files_directory}',     # .proto 파일들이 위치한 디렉터리 지정
        proto_file                        # 컴파일할 .proto 파일
    ]
    subprocess.run(command)

print("모든 .proto 파일이 컴파일되었습니다.")
