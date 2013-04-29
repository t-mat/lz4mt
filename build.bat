@echo off & setlocal

set SLN=lz4mt.sln
call "%VS110COMNTOOLS%\vsvars32.bat"

pushd platform_msvc2012
msbuild %SLN% /nologo /v:q /t:Rebuild /p:Platform=Win32 /p:Configuration=Release
msbuild %SLN% /nologo /v:q /t:Rebuild /p:Platform=Win32 /p:Configuration=Debug
msbuild %SLN% /nologo /v:q /t:Rebuild /p:Platform=x64 /p:Configuration=Release
msbuild %SLN% /nologo /v:q /t:Rebuild /p:Platform=x64 /p:Configuration=Debug
popd

.\platform_msvc2012\win32\release\lz4mt.exe -H
