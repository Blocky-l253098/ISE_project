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
    /**
     * encodeChar
     * ----------
     * Encodes a single 9-element barcode pattern into a horizontal row of pixels.
     *
     * The input `pattern` is a 9-character string where each character
     * describes the WIDTH of one element (bar or space):
     *   '1' -> wide element   (uses the `wide` module width)
     *   '0' -> narrow element (uses the `narrow` module width)
     *
     * Colors alternate automatically, starting with black:
     *   index 0, 2, 4, 6, 8 -> BLACK (1)  (bars)
     *   index 1, 3, 5, 7    -> WHITE (0)  (spaces)
     *
     * The resulting pixels are appended to `row` using appendPixels(),
     * which is defined elsewhere in the project.
     *
     * @param row     The pixel row (vector<int>) to append the encoded
     *                elements to. Modified in place.
     * @param pattern A 9-character string of '0'/'1' values describing
     *                the widths of the 9 alternating elements.
     *
     * Example:
     *   pattern = "101010101"
     *   -> black(wide), white(narrow), black(wide), ... (9 elements)
     */
    void encodeChar(vector<int>& row, string pattern) {
        // A standard barcode character/guard is made of 9 elements,
        // alternating between bar (black) and space (white).
        for (int i = 0; i < 9; i++) {

            // Determine the color of the current element.
            // Even indices are bars (black = 1), odd indices are
            // spaces (white = 0). This matches the standard
            // bar/space alternation used by EAN/UPC-style codes.
            int color = (i % 2 == 0) ? 1 : 0;

            // Determine the width of the current element.
            // '1' in the pattern means a WIDE element,
            // '0' means a NARROW element.
            int width = (pattern[i] == '1') ? wide : narrow;

            // Append `width` pixels of `color` to the row.
            // appendPixels() handles the actual vector push_back calls.
            appendPixels(row, color, width);
        }
    }
};


int main(array<System::String^>^ args)
{
    Console::WriteLine(L"Hello World");
    Console::ReadLine();
    return 0;
}