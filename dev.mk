
.PHONY: dev/apt
dev/apt:
	apt install make cmake clang libtool autoconf git gdb libssl-dev zlib1g-dev jq

.PHONY: dev/dotnet
dev/dotnet:
	curl -sSL https://dot.net/v1/dotnet-install.sh -o /tmp/dotnet-install.sh
	chmod +x /tmp/dotnet-install.sh
	/tmp/dotnet-install.sh --channel 8.0
	/tmp/dotnet-install.sh --version 10.0.203
	dotnet --list-sdks
	rm /tmp/dotnet-install.sh

.PHONY: dev
dev:
	cmake -S . -B cmake-build-dev \
            -DCMAKE_C_COMPILER=clang \
            -DCMAKE_CXX_COMPILER=clang++ \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_FLAGS_DEBUG="-g -O0" \
            -DCMAKE_C_FLAGS_DEBUG="-g -O0"
	make -C cmake-build-dev -j profiler wrapper

