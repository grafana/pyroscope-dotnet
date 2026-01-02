FROM mcr.microsoft.com/windows/servercore:ltsc2022-amd64@sha256:60edaa563e90829cc4e1eb21259c68e865c7a2bf8ab7668182726543a233e6a3
SHELL ["powershell", "-Command", "$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue';"]

WORKDIR /app

ADD wait-for-dependencies.ps1 .

ENTRYPOINT ["powershell.exe", ".\\wait-for-dependencies.ps1"]