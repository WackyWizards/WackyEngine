@echo off
echo Compiling shaders...

slangc shader.slang -target spirv -entry VSMain -o vert.spv
if %errorlevel% neq 0 (
    echo FAILED: vertex shader
    pause
    exit /b 1
)

slangc shader.slang -target spirv -entry PSMain -o frag.spv
if %errorlevel% neq 0 (
    echo FAILED: fragment shader
    pause
    exit /b 1
)

echo Done. Copying to build output...

set OUT=x64\Debug
if not exist %OUT% set OUT=x64\Release

copy /y vert.spv %OUT%\vert.spv
copy /y frag.spv %OUT%\frag.spv

echo Shaders updated in %OUT%
pause