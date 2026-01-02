FROM python:3.10.5-windowsservercore-ltsc2022@sha256:cf357e769124547c6f1a44e291d98c5b55528bf9aaac62909135840e6e220869

WORKDIR /

# Pin to older test agent versions to try to avoid breakages in the future
RUN pip install --no-cache-dir "ddapm-test-agent==1.16.0" "ddsketch==3.0.1" "ddsketch[serialization]==3.0.1"

ENTRYPOINT [ "ddapm-test-agent", "--port=8126" ]