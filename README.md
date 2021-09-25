1. Paint.NET으로 샘플로 이용할 DDS 파일 생성 (크기는 4의 배수여야함. DDS 파일이 4x4 픽셀 단위로 압축하는 방식이기 때문. BC1)

1. DDS 파일 로드해서 ID3D12Resource 생성 - 책에 있는 코드 사용하고, 앞뒤로 Command Queue 초기화 및 플러시 해 주기

2. SRV Heap 생성

3. SRV Desc이용해 SRV Heap에 SRV 생성

4. Sampler 생성 & D3D12SerializeRootSignature()에 넘겨주기

5. 셰이더에서 SamplerState 및 Texture2D 만들기. 각각 register(s0), register(t0)

6. C++ Vertex 구조체 및 VB 데이터 변경, 셰이더의 VSInput, PSInput 변경 (tex 추가)
