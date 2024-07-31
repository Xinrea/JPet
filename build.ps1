npm run build --prefix ./resources/panel
cmake . -Bbuild
cmake --build ./build --config Debug --target ALL_BUILD -j 26
./build/bin/JPet/Debug/JPet.exe
