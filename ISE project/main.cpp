#include<iostream>

using namespace System;
using namespace std;
class Barcode {
private:
    string code[11];
    int gap;
    int narrow;
    int wide;
    int quiet;
public:
    Barcode() {
        code[0] = "000110100";
        code[1] = "100100001";
        code[2] = "001100001";
        code[3] = "101100000";
        code[4] = "000110001";
        code[5] = "100110000";
        code[6] = "001110000";
        code[7] = "000100101";
        code[8] = "100100100";
        code[9] = "001100100";
        code[10] = "010010100"; // * (Start/Stop)

        narrow = 4;
        gap = narrow;
        wide = 12;
        quiet = 40;
    }
};


int main(array<System::String^>^ args)
{
    Console::WriteLine(L"Hello World");
    Console::ReadLine();
    return 0;
}