#pragma once

// =============================================================================
// Barcode.h - everything about Code 39 barcodes, one small class per job
//   Code39            symbol table + constants
//   BarcodeEncoder    digits -> pixel row
//   BarcodeDecoder    pixel row -> digits
//   ImageScanner      picture (camera frame or file) -> pixel row with the barcode
//   BarcodeRenderer   pixel row -> picture
//   SvgBarcodeFile    pixel row <-> SVG file
//   Barcode           facade used by the screens (also the IBarcodeReader)
//   Depends on        Common.h
// =============================================================================

#include "Common.h"

// ---------------------------------------------------------------------------
// Code 39 symbol table
// ---------------------------------------------------------------------------
public ref class Code39 abstract sealed
{
public:
    literal int BarHeight = 150;
    literal int NoMatch = 999;      // "closest match" value when nothing looked like a barcode

    // A product barcode: 6 digits, the first one not 0
    static bool IsValidPayload(String^ payload)
    {
        return Regex::IsMatch(payload, "^[1-9][0-9]{5}$");
    }

    // Wide ('1') / narrow ('0') pattern of every symbol: digits 0-9, then '*' (start/stop)
    static array<String^>^ Patterns()
    {
        return gcnew array<String^>
        {
            "000110100", // 0
                "100100001", // 1
                "001100001", // 2
                "101100000", // 3
                "000110001", // 4
                "100110000", // 5
                "001110000", // 6
                "000100101", // 7
                "100100100", // 8
                "001100100", // 9
                "010010100"  // * (start/stop)
        };
    }

    // The same symbols as 15 modules each: narrow = 1 module, wide = 3 modules
    // ('1' = bar, '0' = space)
    static array<String^>^ Modules()
    {
        array<String^>^ patterns = Patterns();
        array<String^>^ modules = gcnew array<String^>(patterns->Length);

        for (int i = 0; i < patterns->Length; i++)
        {
            StringBuilder^ sb = gcnew StringBuilder();
            for (int j = 0; j < 9; j++)
            {
                int count = (patterns[i][j] == L'1') ? 3 : 1;
                for (int k = 0; k < count; k++)
                    sb->Append((j % 2 == 0) ? L'1' : L'0');
            }
            modules[i] = sb->ToString();
        }
        return modules;
    }
};

// ---------------------------------------------------------------------------
// Payload -> pixel row
// ---------------------------------------------------------------------------
public ref class BarcodeEncoder
{
private:
    array<String^>^ code;
    int gap;
    int narrow;
    int wide;
    int quiet;

    // Pushes 'count' pixels of 'color' (0 = white, 1 = black) into the row
    void AppendPixels(List<int>^ row, int color, int count)
    {
        for (int i = 0; i < count; i++)
            row->Add(color);
    }

    // Translates a 9-char pattern into pixels. '1' = wide, '0' = narrow.
    // Even index = bar (black), odd index = space (white).
    void EncodeChar(List<int>^ row, String^ pattern)
    {
        for (int i = 0; i < 9; i++)
        {
            int color = (i % 2 == 0) ? 1 : 0;
            int width = (pattern[i] == '1') ? wide : narrow;
            AppendPixels(row, color, width);
        }
    }

    void Validate(String^ payload)
    {
        if (String::IsNullOrEmpty(payload))
            throw gcnew ArgumentException("Enter at least one digit.");

        for each(Char c in payload)
        {
            if (c < L'0' || c > L'9')
                throw gcnew ArgumentException("Only digits 0-9 are supported.");
        }

        if (payload->Length != 6)
            throw gcnew ArgumentException("Enter exactly 6 digits.");

        if (payload[0] == L'0')
            throw gcnew ArgumentException("The first digit cannot be 0.");
    }

public:
    BarcodeEncoder()
    {
        code = Code39::Patterns();
        narrow = 4;
        gap = narrow;
        wide = 12;
        quiet = 40;
    }

    // Payload -> pixel row (0/1 per pixel column)
    List<int>^ Encode(String^ payload)
    {
        Validate(payload);

        List<int>^ row = gcnew List<int>();

        AppendPixels(row, 0, quiet);            // left quiet zone
        EncodeChar(row, code[10]);              // start '*'

        for (int i = 0; i < payload->Length; i++)
        {
            AppendPixels(row, 0, gap);          // inter-character gap
            EncodeChar(row, code[payload[i] - L'0']);
        }

        AppendPixels(row, 0, gap);
        EncodeChar(row, code[10]);              // stop '*'
        AppendPixels(row, 0, quiet);            // right quiet zone

        return row;
    }
};

// ---------------------------------------------------------------------------
// Pixel row -> payload
// ---------------------------------------------------------------------------
public ref class BarcodeDecoder
{
private:
    literal int TotalModules = 127;         // 8 characters x 15 modules + 7 gaps
    literal int SymbolModules = 15;         // modules of one character
    literal int CharsPerBarcode = 8;        // start, 6 digits, stop
    literal int MaxSymbolErrors = 2;        // wrong modules allowed inside one character
    literal int MaxTotalErrors = 8;         // wrong modules allowed in the whole barcode
    literal int MinRunsInBarcode = 60;      // bars + spaces between the two end bars
    literal double MinModulePixels = 0.8;   // a module narrower than this is noise
    literal double EndBarMin = 0.4;         // end bar width limits, in modules
    literal double EndBarMax = 2.0;

    array<String^>^ symbols;                // each symbol as 15 modules ('1' = bar, '0' = space)

    // Run-length encodes the row: start, length and colour of every run
    void SplitIntoRuns(List<int>^ row, List<int>^ starts, List<int>^ lens, List<int>^ colors)
    {
        int i = 0;
        while (i < row->Count)
        {
            int start = i;
            int color = row[i];
            while (i < row->Count && row[i] == color) i++;
            starts->Add(start);
            lens->Add(i - start);
            colors->Add(color);
        }
    }

    // Samples the row at the middle of every module -> 127 bits (1 = bar)
    array<int>^ SampleBits(List<int>^ row, int left, double m)
    {
        array<int>^ bits = gcnew array<int>(TotalModules);
        for (int i = 0; i < TotalModules; i++)
        {
            double centre = left + (i + 0.5) * m;
            int a = (int)Math::Floor(centre - m * 0.25);
            int b = (int)Math::Floor(centre + m * 0.25);
            if (b < a) b = a;

            int ones = 0, count = 0;
            for (int p = a; p <= b; p++)
            {
                if (p >= 0 && p < row->Count)
                    ones += row[p];
                count++;
            }
            bits[i] = (ones * 2 > count) ? 1 : 0;
        }
        return bits;
    }

    // Matches the 8 characters (start, 6 digits, stop) with the closest symbols.
    // Returns the characters as text, nullptr when one of them is unclear.
    // totalError = number of wrong modules.
    String^ MatchCharacters(array<int>^ bits, int% totalError)
    {
        StringBuilder^ chars = gcnew StringBuilder();
        bool ok = true;
        int errors = 0;

        for (int c = 0; c < CharsPerBarcode; c++)
        {
            int at = c * (SymbolModules + 1);
            int best = 99, second = 99, bestSymbol = 0;

            for (int sym = 0; sym < symbols->Length; sym++)
            {
                String^ pattern = symbols[sym];
                int d = 0;
                for (int j = 0; j < SymbolModules; j++)
                {
                    int expected = (pattern[j] == L'1') ? 1 : 0;
                    if (expected != bits[at + j]) d++;
                }

                if (d < best) { second = best; best = d; bestSymbol = sym; }
                else if (d < second) { second = d; }
            }

            errors += best;
            if (c < CharsPerBarcode - 1 && bits[at + SymbolModules] != 0)   // gap must be white
                errors++;

            if (best > MaxSymbolErrors || best >= second)
                ok = false;

            chars->Append(bestSymbol == 10 ? L'*' : (Char)(L'0' + bestSymbol));
        }

        totalError = errors;
        if (!ok)
            return nullptr;
        return chars->ToString();
    }

    // Reads a 6-digit barcode from the pixel row when the bars start at 'left' and
    // the whole barcode is 'width' pixels wide. The row is SAMPLED at the middle of
    // every module, so a few blurred / merged bars do not break the read.
    // totalError = number of wrong modules. Returns nullptr when it is not a barcode.
    String^ DecodeModules(List<int>^ row, int left, int width, int% totalError)
    {
        totalError = Code39::NoMatch;
        double m = width / (double)TotalModules;
        if (m < MinModulePixels)
            return nullptr;

        int errors;
        String^ text = MatchCharacters(SampleBits(row, left, m), errors);
        totalError = errors;
        if (text == nullptr || errors > MaxTotalErrors)
            return nullptr;

        if (text[0] != L'*' || text[CharsPerBarcode - 1] != L'*')
            return nullptr;

        String^ digits = text->Substring(1, 6);
        for each(Char d in digits)
        {
            if (d < L'0' || d > L'9')
                return nullptr;
        }
        return digits;
    }

    // Tries a few small position / width corrections around a candidate
    String^ SampleDecode(List<int>^ row, int left, int width)
    {
        String^ found = nullptr;
        int foundError = Code39::NoMatch;

        for (int dl = -1; dl <= 1; dl++)
        {
            for (int dw = -2; dw <= 2; dw++)
            {
                int err;
                String^ payload = DecodeModules(row, left + dl, width + dw, err);
                if (err < BestError)
                    BestError = err;
                if (payload != nullptr && err < foundError)
                {
                    found = payload;
                    foundError = err;
                }
            }
        }
        return found;
    }

    // Looks for a 6-digit barcode ANYWHERE in the row (the camera also sees the
    // table, the paper edge, other print...). A candidate starts at a bar and ends
    // at a bar; both end bars of the barcode are one module wide, which gives the
    // module size and so the expected total width.
    String^ FindBarcode(List<int>^ row)
    {
        List<int>^ starts = gcnew List<int>();
        List<int>^ lens = gcnew List<int>();
        List<int>^ colors = gcnew List<int>();
        SplitIntoRuns(row, starts, lens, colors);

        int runs = starts->Count;
        for (int s = 0; s < runs; s++)
        {
            if (colors[s] != 1)
                continue;

            int left = starts[s];
            for (int e = s + MinRunsInBarcode; e < runs; e++)
            {
                if (colors[e] != 1)
                    continue;

                int width = starts[e] + lens[e] - left;
                double m = width / (double)TotalModules;

                if (m * EndBarMin > lens[s])        // too wide for this start bar, and it only grows
                    break;
                if (m < MinModulePixels || lens[s] > m * EndBarMax + 1)
                    continue;
                if (lens[e] > m * EndBarMax + 1 || lens[e] < m * EndBarMin)
                    continue;

                String^ payload = SampleDecode(row, left, width);
                if (payload != nullptr)
                    return payload;
            }
        }
        return nullptr;
    }

public:
    // Diagnostics: fewest wrong modules seen in the last scan (Code39::NoMatch = no barcode-like bars)
    int BestError;

    BarcodeDecoder()
    {
        BestError = Code39::NoMatch;
        symbols = Code39::Modules();
    }

    void ResetDiagnostics()
    {
        BestError = Code39::NoMatch;
    }

    // Tries the row as it is, then reversed (mirrored / upside-down image).
    // Returns the digits or nullptr. 'row' is left in the direction that worked.
    String^ FindEitherWay(List<int>^ row)
    {
        String^ payload = FindBarcode(row);
        if (payload == nullptr)
        {
            row->Reverse();
            payload = FindBarcode(row);
        }
        return payload;
    }

    // Pixel row -> 6 digits (start/stop stripped). The barcode may sit anywhere in the row.
    String^ Decode(List<int>^ row)
    {
        bool anyBar = false;
        for each(int pixel in row)
        {
            if (pixel == 1) { anyBar = true; break; }
        }
        if (!anyBar)
            throw gcnew FormatException("No barcode found.");

        String^ payload = FindBarcode(row);
        if (payload == nullptr)
            throw gcnew FormatException("No valid 6-digit barcode found (bars unreadable).");
        return payload;
    }
};

// ---------------------------------------------------------------------------
// Picture (camera frame or file) -> pixel row that holds the barcode
// ---------------------------------------------------------------------------
public ref class ImageScanner
{
private:
    literal int LineSpread = 2;             // scan lines averaged on each side of the line
    literal int MinContrast = 40;           // darkest vs brightest pixel of a scan line
    literal int ThresholdOffset = 10;       // a bar must be this much darker than its surroundings
    literal int MinLineLength = 16;
    literal int MinWindow = 15;             // width of the local-average window
    literal int WindowDivisor = 12;
    literal int HorizontalLines = 7;        // scan lines for a horizontal barcode
    literal int VerticalLines = 5;          // scan lines for a sideways barcode
    literal int VerticalTilts = 3;

    BarcodeDecoder^ decoder;

    // Removes isolated 1-pixel flips caused by sensor noise / JPEG artefacts
    void Despeckle(List<int>^ row)
    {
        for (int i = 1; i < row->Count - 1; i++)
        {
            if (row[i] != row[i - 1] && row[i] != row[i + 1])
                row[i] = row[i - 1];
        }
    }

    // Bitmap -> grey values 0..255 (transparent = white)
    array<Byte>^ ToGray(Bitmap^ bmp, int% w, int% h)
    {
        w = bmp->Width;
        h = bmp->Height;
        array<Byte>^ gray = gcnew array<Byte>(w * h);
        array<Byte>^ buf = gcnew array<Byte>(w * 4);

        BitmapData^ data = bmp->LockBits(System::Drawing::Rectangle(0, 0, w, h),
            ImageLockMode::ReadOnly, PixelFormat::Format32bppArgb);
        try
        {
            for (int y = 0; y < h; y++)
            {
                IntPtr src = IntPtr(data->Scan0.ToInt64() + (Int64)y * data->Stride);
                System::Runtime::InteropServices::Marshal::Copy(src, buf, 0, w * 4);
                for (int x = 0; x < w; x++)
                {
                    int b = buf[x * 4];
                    int g = buf[x * 4 + 1];
                    int r = buf[x * 4 + 2];
                    int a = buf[x * 4 + 3];
                    int lum = (a >= 128) ? (r * 299 + g * 587 + b * 114) / 1000 : 255;
                    gray[y * w + x] = (Byte)lum;
                }
            }
        }
        finally
        {
            bmp->UnlockBits(data);
        }
        return gray;
    }

    // Brightness along a straight line (any angle). Averages parallel lines so
    // sensor noise does not break the bars. Outside the picture counts as white.
    array<int>^ SampleLine(array<Byte>^ gray, int w, int h, double x0, double y0, double x1, double y1)
    {
        double dx = x1 - x0;
        double dy = y1 - y0;
        double len = Math::Sqrt(dx * dx + dy * dy);
        int n = (int)len;
        if (n < MinLineLength)
            return gcnew array<int>(0);

        double ux = dx / len, uy = dy / len;
        double nx = -uy, ny = ux;

        array<int>^ result = gcnew array<int>(n);
        for (int i = 0; i < n; i++)
        {
            int sum = 0;
            for (int k = -LineSpread; k <= LineSpread; k++)
            {
                int px = (int)Math::Floor(x0 + ux * i + nx * k + 0.5);
                int py = (int)Math::Floor(y0 + uy * i + ny * k + 0.5);
                sum += (px < 0 || py < 0 || px >= w || py >= h) ? 255 : gray[py * w + px];
            }
            result[i] = sum / (2 * LineSpread + 1);
        }
        return result;
    }

    // Brightness -> pixel row (1 = bar, 0 = space). LOCAL threshold: a pixel is a
    // bar when it is clearly darker than the average around it, so uneven light and
    // dark/light backgrounds do not matter much.
    List<int>^ Binarize(array<int>^ brightness)
    {
        int w = brightness->Length;
        List<int>^ row = gcnew List<int>();

        array<int>^ prefix = gcnew array<int>(w + 1);
        int darkest = 255, brightest = 0;
        for (int x = 0; x < w; x++)
        {
            int b = brightness[x];
            prefix[x + 1] = prefix[x] + b;
            if (b < darkest) darkest = b;
            if (b > brightest) brightest = b;
        }

        // Too little contrast = no barcode on this line
        if (w == 0 || brightest - darkest < MinContrast)
        {
            for (int x = 0; x < w; x++) row->Add(0);
            return row;
        }

        int half = Math::Max(MinWindow, w / WindowDivisor);
        for (int x = 0; x < w; x++)
        {
            int lo = Math::Max(0, x - half);
            int hi = Math::Min(w - 1, x + half);
            int mean = (prefix[hi + 1] - prefix[lo]) / (hi - lo + 1);
            row->Add((brightness[x] < mean - ThresholdOffset) ? 1 : 0);
        }

        Despeckle(row);
        return row;
    }

    // One scan line -> row when it holds a valid barcode, otherwise nullptr
    List<int>^ TryLine(array<Byte>^ gray, int w, int h, double x0, double y0, double x1, double y1)
    {
        List<int>^ row = Binarize(SampleLine(gray, w, h, x0, y0, x1, y1));
        String^ payload = decoder->FindEitherWay(row);
        if (payload != nullptr && Code39::IsValidPayload(payload))
            return row;
        return nullptr;
    }

    // Lines across the picture, from left to right, with a few tilts
    List<int>^ ScanHorizontal(array<Byte>^ gray, int w, int h, array<double>^ slopes)
    {
        for (int t = 0; t < slopes->Length; t++)
        {
            for (int k = 1; k <= HorizontalLines; k++)
            {
                double yc = h * k / (HorizontalLines + 1.0);
                double shift = slopes[t] * w / 2.0;
                List<int>^ row = TryLine(gray, w, h, 0, yc - shift, w - 1, yc + shift);
                if (row != nullptr)
                    return row;
            }
        }
        return nullptr;
    }

    // Lines from top to bottom (barcode held sideways)
    List<int>^ ScanVertical(array<Byte>^ gray, int w, int h, array<double>^ slopes)
    {
        for (int t = 0; t < VerticalTilts; t++)
        {
            for (int k = 1; k <= VerticalLines; k++)
            {
                double xc = w * k / (VerticalLines + 1.0);
                double shift = slopes[t] * h / 2.0;
                List<int>^ row = TryLine(gray, w, h, xc - shift, 0, xc + shift, h - 1);
                if (row != nullptr)
                    return row;
            }
        }
        return nullptr;
    }

public:
    ImageScanner(BarcodeDecoder^ decoder)
    {
        this->decoder = decoder;
    }

    // Scans a bitmap along many lines (horizontal first, then sideways, up to about
    // 20 degrees of tilt). Returns the first row that holds a valid barcode (already
    // in the readable direction), or nullptr.
    List<int>^ ScanForRow(Bitmap^ bmp)
    {
        int w, h;
        array<Byte>^ gray = ToGray(bmp, w, h);
        decoder->ResetDiagnostics();

        array<double>^ slopes = gcnew array<double>{ 0.0, 0.18, -0.18, 0.36, -0.36 };

        List<int>^ row = ScanHorizontal(gray, w, h, slopes);
        if (row == nullptr)
            row = ScanVertical(gray, w, h, slopes);
        return row;
    }

    // Image file -> pixel row of the scan line that holds the barcode
    List<int>^ ScanFile(String^ path)
    {
        Bitmap^ bmp = gcnew Bitmap(path);
        List<int>^ result;
        try
        {
            result = ScanForRow(bmp);
        }
        finally
        {
            delete bmp;
        }

        if (result == nullptr)
            throw gcnew FormatException("No readable barcode found in this image.");
        return result;
    }

    // Camera frame -> payload; nullptr when no valid 6-digit barcode is found
    // (normal for most frames).
    String^ TryDecode(Bitmap^ bmp)
    {
        List<int>^ row = ScanForRow(bmp);
        if (row == nullptr)
            return nullptr;
        return decoder->Decode(row);
    }
};

// ---------------------------------------------------------------------------
// Pixel row -> picture
// ---------------------------------------------------------------------------
public ref class BarcodeRenderer
{
public:
    // Row -> Bitmap (for preview and PNG export)
    Bitmap^ Render(List<int>^ row, int scale)
    {
        int height = Code39::BarHeight * scale / 2;
        Bitmap^ bmp = gcnew Bitmap(row->Count * scale, height);
        Graphics^ g = Graphics::FromImage(bmp);
        g->Clear(Color::White);

        int x = 0;
        while (x < row->Count)
        {
            if (row[x] == 1)
            {
                int start = x;
                while (x < row->Count && row[x] == 1) x++;
                g->FillRectangle(Brushes::Black, start * scale, 0, (x - start) * scale, height);
            }
            else
            {
                x++;
            }
        }
        delete g;
        return bmp;
    }
};

// ---------------------------------------------------------------------------
// Pixel row <-> SVG file
// ---------------------------------------------------------------------------
public ref class SvgBarcodeFile
{
public:
    // Row -> SVG file (adjacent black pixels merged into one rect)
    void Save(List<int>^ row, String^ path)
    {
        StringBuilder^ sb = gcnew StringBuilder();
        sb->AppendLine("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " +
            row->Count + " " + Code39::BarHeight + "\">");
        sb->AppendLine("<rect width=\"100%\" height=\"100%\" fill=\"white\"/>");

        int x = 0;
        while (x < row->Count)
        {
            if (row[x] == 1)
            {
                int start = x;
                while (x < row->Count && row[x] == 1) x++;
                sb->AppendLine("<rect x=\"" + start + "\" y=\"0\" width=\"" + (x - start) +
                    "\" height=\"" + Code39::BarHeight + "\" fill=\"black\"/>");
            }
            else
            {
                x++;
            }
        }
        sb->AppendLine("</svg>");
        File::WriteAllText(path, sb->ToString(), gcnew UTF8Encoding(false));
    }

    // SVG file -> pixel row. Handles both 1px-wide rects (old format)
    // and merged rects with a width attribute (new format).
    List<int>^ Load(String^ path)
    {
        String^ svg = File::ReadAllText(path);

        List<int>^ starts = gcnew List<int>();
        List<int>^ widths = gcnew List<int>();
        int maxX = 0;

        MatchCollection^ rects = Regex::Matches(svg, "<rect[^>]*>");
        for each(Match ^ m in rects)
        {
            String^ tag = m->Value;
            if (!tag->Contains("fill=\"black\""))
                continue;

            Match^ mx = Regex::Match(tag, "\\sx=\"(\\d+)\"");
            if (!mx->Success)
                continue;

            int x = Int32::Parse(mx->Groups[1]->Value);
            int w = 1;
            Match^ mw = Regex::Match(tag, "\\swidth=\"(\\d+)\"");
            if (mw->Success)
                w = Int32::Parse(mw->Groups[1]->Value);

            starts->Add(x);
            widths->Add(w);
            if (x + w > maxX) maxX = x + w;
        }

        if (starts->Count == 0)
            throw gcnew FormatException("No barcode found in this SVG.");

        int total = maxX;
        Match^ mv = Regex::Match(svg, "viewBox=\"0 0 (\\d+) \\d+\"");
        if (mv->Success)
            total = Math::Max(total, Int32::Parse(mv->Groups[1]->Value));

        List<int>^ row = gcnew List<int>();
        for (int i = 0; i < total; i++) row->Add(0);

        for (int i = 0; i < starts->Count; i++)
            for (int k = 0; k < widths[i]; k++)
                row[starts[i] + k] = 1;

        return row;
    }
};

// ---------------------------------------------------------------------------
// What the scanner control needs from a barcode reader (so the reader can be swapped)
// ---------------------------------------------------------------------------
public interface class IBarcodeReader
{
    // Camera frame -> 6 digits, or nullptr when the frame holds no valid barcode
    String^ TryDecode(Bitmap^ frame);

    // Fewest wrong modules seen in the last frame (Code39::NoMatch = no barcode-like bars)
    int ClosestMatch();
};

// Facade: the one object the screens use for everything about barcodes.
// The real work sits in the small classes above, each with a single job.
public ref class Barcode : public IBarcodeReader
{
private:
    BarcodeEncoder^ encoder;
    BarcodeDecoder^ decoder;
    ImageScanner^ scanner;
    BarcodeRenderer^ renderer;
    SvgBarcodeFile^ svg;

public:
    Barcode()
    {
        encoder = gcnew BarcodeEncoder();
        decoder = gcnew BarcodeDecoder();
        scanner = gcnew ImageScanner(decoder);
        renderer = gcnew BarcodeRenderer();
        svg = gcnew SvgBarcodeFile();
    }

    List<int>^ Encode(String^ payload) { return encoder->Encode(payload); }
    Bitmap^ Render(List<int>^ row, int scale) { return renderer->Render(row, scale); }
    void SaveSvg(List<int>^ row, String^ path) { svg->Save(row, path); }
    List<int>^ LoadSvg(String^ path) { return svg->Load(path); }
    List<int>^ LoadImage(String^ path) { return scanner->ScanFile(path); }
    String^ Decode(List<int>^ row) { return decoder->Decode(row); }

    virtual String^ TryDecode(Bitmap^ frame) { return scanner->TryDecode(frame); }
    virtual int ClosestMatch() { return decoder->BestError; }
};
