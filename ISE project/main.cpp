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
    char decodeCharacter(string pattern) {
        for (int i = 0; i < 11; i++) {
            if (code[i] == pattern) {
                if (i == 10) return '*';
                return '0' + i;
            }
        }
        return '?'; // Unknown pattern
    }
    void generate(string payload) {
        vector<int> row;

        // 1. Left Margin
        appendPixels(row, 0, quiet);
        
        // 2. Start Marker
        encodeChar(row, code[10]);

        // 3. Payload Loop
        for (int i = 0; i < payload.length(); i++) {
            // Must have a gap before the next character
            appendPixels(row, 0, gap); 
            
            // Convert character '5' to integer index 5
            int idx = payload[i] - '0'; 
            encodeChar(row, code[idx]);
        }

        // 4. Stop Marker
        appendPixels(row, 0, gap);
        encodeChar(row, code[10]);

        // 5. Right Margin
        appendPixels(row, 0, quiet);

      int height = 150;
        ofstream img("barcode.svg");
        
        // Pure SVG Header
        img << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << row.size() << " " << height << "\">\n";
        
        // White Background
        img << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
        
        // Draw black bars
        for (int x = 0; x < row.size(); x++) {
            if (row[x] == 1) {
                img << "<rect x=\"" << x << "\" y=\"0\" width=\"1\" height=\"" << height << "\" fill=\"black\"/>\n";
            }
        }
        
        // Close SVG
        img << "</svg>\n";
        img.close();
        
        cout << "Generated barcode.svg. Open it directly in Chrome or Edge." << endl;
    }


	void readSVG(string filename) {
        ifstream file(filename);
        if (!file.is_open()) {
            cout << "Failed to open SVG." << endl;
            return;
        }

        string line;
        vector<int> x_coords;

        // STEP 1: SCAN THE SVG (Simulating a perfect horizontal scanline)
        while (getline(file, line)) {
            // Find rectangles that are black
            if (line.find("fill=\"black\"") != string::npos) {
                int pos = line.find("x=\"");
                if (pos != string::npos) {
                    int end_pos = line.find("\"", pos + 3);
                    string x_str = line.substr(pos + 3, end_pos - pos - 3);
                    x_coords.push_back(stoi(x_str));
                }
            }
        }
        file.close();

        if (x_coords.empty()) {
            cout << "No barcode found." << endl;
            return;
        }

        // STEP 2: RUN-LENGTH ENCODING (Calculate widths of bars and spaces)
        vector<int> widths;
        int current_bar_width = 1;

        for (int i = 1; i < x_coords.size(); i++) {
            // If the next black pixel is exactly 1 unit away, the bar continues
            if (x_coords[i] == x_coords[i - 1] + 1) {
                current_bar_width++;
            } else {
                // The black bar ended. Record its width.
                widths.push_back(current_bar_width); 
                
                // The distance to the next black pixel is the white space width
                int space_width = x_coords[i] - x_coords[i - 1] - 1;
                widths.push_back(space_width); 
                
                // Reset bar width for the new bar
                current_bar_width = 1;
            }
        }
        // Push the very last black bar
        widths.push_back(current_bar_width);

        // STEP 3: CLASSIFICATION & DECODING
        // Find the minimum (narrow) and maximum (wide) widths to establish a threshold
        int min_w = widths[0], max_w = widths[0];
        for (int w : widths) {
            if (w < min_w) min_w = w;
            if (w > max_w) max_w = w;
        }
        int threshold = (min_w + max_w) / 2;

        string decoded_payload = "";
        
        // Code 39 logic: 9 elements per character, followed by 1 narrow space gap (skip it).
        // Therefore, we step by 10.
        for (int i = 0; i + 8 < widths.size(); i += 10) {
            string pattern = "";
            // Read 9 widths
            for (int j = 0; j < 9; j++) {
                if (widths[i + j] > threshold) {
                    pattern += "1"; // Wide
                } else {
                    pattern += "0"; // Narrow
                }
            }
            decoded_payload += decodeCharacter(pattern);
        }

        cout << "Scanned Code 39 Pattern: " << decoded_payload << endl;
    }
};


int main(array<System::String^>^ args)
{
    Console::WriteLine(L"Hello World");
    Console::ReadLine();
    return 0;
}