clear && clear
echo "Compiling..."
g++ --std=c++11 -L/usr/local/lib -lPocoNet -lPocoFoundation -lPocoUtil -lpthread *.cpp -o thermal && echo "Compiling finished."

