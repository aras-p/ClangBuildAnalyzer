del *.json
pushd ..\..
mkdir build\clang
"%PROGRAMFILES%\LLVM\bin\clang" -std=c++17 -O2 -ftime-trace -c src/Analysis.cpp -o build/clang/Analysis.o
"%PROGRAMFILES%\LLVM\bin\clang" -std=c++17 -O2 -ftime-trace -c src/Arena.cpp -o build/clang/Arena.o
"%PROGRAMFILES%\LLVM\bin\clang" -std=c++17 -O2 -ftime-trace -c src/BuildEvents.cpp -o build/clang/BuildEvents.o
"%PROGRAMFILES%\LLVM\bin\clang" -std=c++17 -O2 -ftime-trace -c src/main.cpp -o build/clang/main.o
popd
copy /Y ..\..\build\clang\*.json .
pause
