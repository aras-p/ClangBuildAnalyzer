del *.json
pushd ..\..
rmdir /s /y build\clang
mkdir build\clang
"%PROGRAMFILES%\LLVM\bin\clang" -std=c++17 -O2 -ftime-trace -c src/BuildEvents.cpp -o build/clang/BuildEvents.o
"%PROGRAMFILES%\LLVM\bin\clang" -std=c++17 -O2 -ftime-trace -c src/main.cpp -o build/clang/main.o
popd
copy /Y ..\..\build\clang\*.json .
pause
