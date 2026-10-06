# escape=`
# Image de build Windows (conteneur Windows, Server Core) : MSVC + vcpkg + Qt 6.8.3.
# NON TESTÉE : écrite sans accès à un hôte Windows ni à un démon Docker. À valider sur une
# machine Windows avec « Windows containers » activés (Docker Desktop, mode Windows) ; la
# référence qui fait foi reste le job `windows-msvc` de .github/workflows/ci.yml.
# Ne tourne PAS sur un hôte Linux/macOS (un conteneur Windows exige un noyau Windows).
# L'image est lourde (≈ 15-25 Go) ; tests desktop en headless uniquement (pas d'écran).
FROM mcr.microsoft.com/windows/servercore:ltsc2022
SHELL ["powershell", "-NoLogo", "-NoProfile", "-Command", "$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue';"]

# Visual Studio 2022 Build Tools (MSVC x64, CMake, Windows SDK).
RUN Invoke-WebRequest https://aka.ms/vs/17/release/vs_buildtools.exe -OutFile C:\vs_buildtools.exe; `
    Start-Process C:\vs_buildtools.exe -Wait -ArgumentList '--quiet','--wait','--norestart','--nocache', `
      '--add','Microsoft.VisualStudio.Workload.VCTools','--includeRecommended', `
      '--add','Microsoft.VisualStudio.Component.VC.CMake.Project'; `
    Remove-Item C:\vs_buildtools.exe

# Git, Python (pour aqtinstall) et Ninja via Chocolatey.
RUN Set-ExecutionPolicy Bypass -Scope Process -Force; `
    iex ((New-Object Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1')); `
    choco install -y git python311 ninja; `
    $env:Path = [Environment]::GetEnvironmentVariable('Path','Machine')

# Qt 6.8.3 officiel (même version et même arch que la CI), via aqtinstall.
ARG QT_VERSION=6.8.3
RUN python -m pip install aqtinstall; `
    python -m aqt install-qt windows desktop $env:QT_VERSION win64_msvc2022_64 -O C:\Qt
ENV QT_ROOT=C:\Qt\6.8.3\msvc2022_64

# vcpkg (le manifeste vcpkg.json du dépôt installe fmt, spdlog, clipper2, opencv…).
RUN git clone https://github.com/microsoft/vcpkg.git C:\vcpkg; C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
ENV VCPKG_ROOT=C:\vcpkg `
    QT_QPA_PLATFORM=offscreen

WORKDIR C:\src
# Le dépôt est monté dans C:\src. Les presets `msvc*` attendent VCPKG_ROOT et QT_ROOT (ci-dessus) ;
# ils utilisent Visual Studio : lancer depuis l'environnement développeur MSVC :
#   docker run --rm -v ${PWD}:C:\src openstitch-windows
CMD ["cmd", "/c", "call \"C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat\" && cmake --preset msvc && cmake --build --preset msvc-debug && ctest --preset msvc-debug"]
