make clean
make
if [ ! -d bin ];then
   mkdir -p bin
fi

if [ ! -d bin/log ];then
   mkdir -p bin/log
fi

mv chenWeb bin/

echo '=================================================';
echo 'Executable file is located ./bin/chenWeb'
echo '=================================================';