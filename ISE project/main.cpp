#using <System.dll>
#using <System.Drawing.dll>
#using <System.Windows.Forms.dll>
// Camera access: AForge.NET (AForge, AForge.Video, AForge.Video.DirectShow)
// is added through Project -> Add Reference, so no #using lines are needed here.

using namespace System;
using namespace System::Collections::Generic;
using namespace System::Drawing;
using namespace System::Drawing::Imaging;
using namespace System::Globalization;
using namespace System::IO;
using namespace System::Text;
using namespace System::Text::RegularExpressions;
using namespace System::Windows::Forms;
using namespace AForge::Video;
using namespace AForge::Video::DirectShow;

// Raised by the scanner control when a valid barcode is read from the camera
public delegate void BarcodeHandler(String^ payload);

// ---------------------------------------------------------------------------
// Small shared helpers
// ---------------------------------------------------------------------------

// Number / text formatting (always '.' as decimal point, whatever the PC's region is)
public ref class Fmt abstract sealed
{
public:
    // 12.5 -> "12.5", 12 -> "12" (used inside files)
    static String^ Number(double value)
    {
        return value.ToString("0.##", CultureInfo::InvariantCulture);
    }

    // 12.5 -> "12.50" (used on screen)
    static String^ Money(double value)
    {
        return value.ToString("0.00", CultureInfo::InvariantCulture);
    }

    static double Parse(String^ text)
    {
        return Double::Parse(text, CultureInfo::InvariantCulture);
    }

    // Fields are separated by spaces, so spaces inside a value become '_'. Empty -> "-"
    static String^ Pack(String^ text)
    {
        if (String::IsNullOrEmpty(text))
            return "-";
        return text->Replace(" ", "_");
    }

    static String^ Left(String^ text, int width)
    {
        return text->PadRight(width);
    }

    static String^ Right(String^ text, int width)
    {
        return text->PadLeft(width);
    }
};

// File names used by the POS
public ref class Files abstract sealed
{
private:
    static String^ folder = nullptr;

    // The folder that contains Product.txt: the working directory or the exe folder,
    // or one of their parent folders (Visual Studio runs the exe from a Debug folder,
    // while the data files usually sit in the project folder).
    static String^ Locate()
    {
        array<String^>^ roots = gcnew array<String^>
        {
            Directory::GetCurrentDirectory(), AppDomain::CurrentDomain->BaseDirectory
        };

        for each (String ^ root in roots)
        {
            DirectoryInfo^ dir = gcnew DirectoryInfo(root);
            for (int up = 0; dir != nullptr && up < 5; up++)
            {
                if (File::Exists(Path::Combine(dir->FullName, "Product.txt")))
                    return dir->FullName;
                dir = dir->Parent;
            }
        }
        return Directory::GetCurrentDirectory();
    }

public:
    static property String^ Folder
    {
        String^ get()
        {
            if (folder == nullptr)
                folder = Locate();
            return folder;
        }
    }

    static property String^ ProductFile
    {
        String^ get() { return Path::Combine(Folder, "Product.txt"); }
    }

    static property String^ OrderFile
    {
        String^ get() { return Path::Combine(Folder, "Order.txt"); }
    }

    static property String^ OrderDetailFile
    {
        String^ get() { return Path::Combine(Folder, "OrderDetail.txt"); }
    }

    static property String^ BillFile
    {
        String^ get() { return Path::Combine(Folder, "Bill.txt"); }
    }
};

// Allowed values of the order status field
public ref class OrderStatus abstract sealed
{
public:
    literal String^ InProgress = "in-progress";
    literal String^ OnHold = "on-hold";
    literal String^ Paid = "paid";
    literal String^ Archived = "archived";
};

// ---------------------------------------------------------------------------
// Record + file IO. Any new record type only has to implement ToLine()
// ---------------------------------------------------------------------------
public ref class Record abstract
{
public:
    // One record -> one line of text (without the '#' end marker)
    virtual String^ ToLine() abstract;
};

public ref class RecordFile abstract sealed
{
public:
    // Reads every record of a file ('#' is the end of line marker)
    static List<String^>^ ReadLines(String^ path)
    {
        List<String^>^ lines = gcnew List<String^>();
        if (!File::Exists(path))
            return lines;

        array<String^>^ parts = File::ReadAllText(path)->Split(gcnew array<Char>{ L'#' });
        for each (String ^ part in parts)
        {
            String^ line = part->Trim();
            if (line->Length > 0)
                lines->Add(line);
        }
        return lines;
    }

    // Replaces the whole file
    static void WriteLines(String^ path, List<String^>^ lines)
    {
        StringBuilder^ sb = gcnew StringBuilder();
        for each (String ^ line in lines)
            sb->AppendLine(line + "#");
        File::WriteAllText(path, sb->ToString());
    }

    // Adds one record at the end of the file
    static void Append(String^ path, Record^ record)
    {
        File::AppendAllText(path, record->ToLine() + "#" + Environment::NewLine);
    }
};

// ---------------------------------------------------------------------------
// Barcode logic (encode / decode / render / SVG IO / camera frame reading)
// ---------------------------------------------------------------------------
public ref class Barcode
{
private:
    array<String^>^ code;
    int gap;
    int narrow;
    int wide;
    int quiet;
    array<String^>^ symbols;     // each symbol as 15 modules ('1' = bar, '0' = space)

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

    Char DecodeCharacter(String^ pattern)
    {
        for (int i = 0; i < code->Length; i++)
        {
            if (code[i] == pattern)
                return (i == 10) ? L'*' : (Char)(L'0' + i);
        }
        return L'?';
    }

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

    // Brightness along a straight line (any angle). Averages 5 parallel lines so
    // sensor noise does not break the bars. Outside the picture counts as white.
    array<int>^ SampleLine(array<Byte>^ gray, int w, int h, double x0, double y0, double x1, double y1)
    {
        double dx = x1 - x0;
        double dy = y1 - y0;
        double len = Math::Sqrt(dx * dx + dy * dy);
        int n = (int)len;
        if (n < 16)
            return gcnew array<int>(0);

        double ux = dx / len, uy = dy / len;
        double nx = -uy, ny = ux;

        array<int>^ result = gcnew array<int>(n);
        for (int i = 0; i < n; i++)
        {
            int sum = 0;
            for (int k = -2; k <= 2; k++)
            {
                int px = (int)Math::Floor(x0 + ux * i + nx * k + 0.5);
                int py = (int)Math::Floor(y0 + uy * i + ny * k + 0.5);
                sum += (px < 0 || py < 0 || px >= w || py >= h) ? 255 : gray[py * w + px];
            }
            result[i] = sum / 5;
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
        if (w == 0 || brightest - darkest < 40)
        {
            for (int x = 0; x < w; x++) row->Add(0);
            return row;
        }

        int half = Math::Max(15, w / 12);
        for (int x = 0; x < w; x++)
        {
            int lo = Math::Max(0, x - half);
            int hi = Math::Min(w - 1, x + half);
            int mean = (prefix[hi + 1] - prefix[lo]) / (hi - lo + 1);
            row->Add((brightness[x] < mean - 10) ? 1 : 0);
        }

        Despeckle(row);
        return row;
    }

    // Reads a 6-digit barcode from the pixel row when the bars start at 'left' and
    // the whole barcode is 'width' pixels wide (127 modules). The row is SAMPLED at
    // the middle of every module and each character is matched to the closest symbol,
    // so a few blurred / merged bars do not break the read.
    // totalError = number of wrong modules. Returns nullptr when it is not a barcode.
    String^ DecodeModules(List<int>^ row, int left, int width, int% totalError)
    {
        totalError = 999;
        double m = width / 127.0;
        if (m < 0.8)
            return nullptr;

        array<int>^ bits = gcnew array<int>(127);
        for (int i = 0; i < 127; i++)
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

        StringBuilder^ chars = gcnew StringBuilder();
        bool ok = true;
        int errors = 0;

        for (int c = 0; c < 8; c++)
        {
            int at = c * 16;
            int best = 99, second = 99, bestSymbol = 0;

            for (int sym = 0; sym < 11; sym++)
            {
                String^ pattern = symbols[sym];
                int d = 0;
                for (int j = 0; j < 15; j++)
                {
                    int expected = (pattern[j] == L'1') ? 1 : 0;
                    if (expected != bits[at + j]) d++;
                }

                if (d < best) { second = best; best = d; bestSymbol = sym; }
                else if (d < second) { second = d; }
            }

            errors += best;
            if (c < 7 && bits[at + 15] != 0)        // gap between characters must be white
                errors++;

            if (best > 2 || best >= second)
                ok = false;

            chars->Append(bestSymbol == 10 ? L'*' : (Char)(L'0' + bestSymbol));
        }

        totalError = errors;
        if (!ok || errors > 8)
            return nullptr;

        String^ text = chars->ToString();
        if (text[0] != L'*' || text[7] != L'*')
            return nullptr;

        String^ digits = text->Substring(1, 6);
        for each (Char d in digits)
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
        int foundError = 999;

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
    // module size and so the expected total width (127 modules).
    String^ FindBarcode(List<int>^ row)
    {
        List<int>^ starts = gcnew List<int>();
        List<int>^ lens = gcnew List<int>();
        List<int>^ colors = gcnew List<int>();

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

        int runs = starts->Count;
        for (int s = 0; s < runs; s++)
        {
            if (colors[s] != 1)
                continue;

            int left = starts[s];
            for (int e = s + 60; e < runs; e++)
            {
                if (colors[e] != 1)
                    continue;

                int width = starts[e] + lens[e] - left;
                double m = width / 127.0;

                if (m * 0.4 > lens[s])         // too wide for this start bar, and it only grows
                    break;
                if (m < 0.8 || lens[s] > m * 2 + 1)
                    continue;
                if (lens[e] > m * 2 + 1 || lens[e] < m * 0.4)
                    continue;

                String^ payload = SampleDecode(row, left, width);
                if (payload != nullptr)
                    return payload;
            }
        }
        return nullptr;
    }

    // Tries the row as it is, then reversed (mirrored / upside-down image).
    // Returns the digits or nullptr. 'row' is left in the direction that worked.
    String^ DecodeEitherWay(List<int>^ row)
    {
        String^ payload = FindBarcode(row);
        if (payload == nullptr)
        {
            row->Reverse();
            payload = FindBarcode(row);
        }
        return payload;
    }

    // One scan line -> row when it holds a valid barcode, otherwise nullptr
    List<int>^ TryLine(array<Byte>^ gray, int w, int h, double x0, double y0, double x1, double y1)
    {
        List<int>^ row = Binarize(SampleLine(gray, w, h, x0, y0, x1, y1));
        String^ payload = DecodeEitherWay(row);
        if (payload != nullptr && Regex::IsMatch(payload, "^[1-9][0-9]{5}$"))
            return row;
        return nullptr;
    }

    // Scans a bitmap along many lines: 7 heights x 5 tilts (up to about 20 degrees)
    // for a horizontal barcode, then 5 positions x 3 tilts for a sideways one.
    // Returns the first row that holds a valid barcode (already in the readable
    // direction), or nullptr.
    List<int>^ ScanForRow(Bitmap^ bmp)
    {
        int w, h;
        array<Byte>^ gray = ToGray(bmp, w, h);
        BestError = 999;

        array<double>^ slopes = gcnew array<double>{ 0.0, 0.18, -0.18, 0.36, -0.36 };

        for (int t = 0; t < slopes->Length; t++)
        {
            for (int k = 1; k <= 7; k++)
            {
                double yc = h * k / 8.0;
                double shift = slopes[t] * w / 2.0;
                List<int>^ row = TryLine(gray, w, h, 0, yc - shift, w - 1, yc + shift);
                if (row != nullptr)
                    return row;
            }
        }

        for (int t = 0; t < 3; t++)
        {
            for (int k = 1; k <= 5; k++)
            {
                double xc = w * k / 6.0;
                double shift = slopes[t] * h / 2.0;
                List<int>^ row = TryLine(gray, w, h, xc - shift, 0, xc + shift, h - 1);
                if (row != nullptr)
                    return row;
            }
        }
        return nullptr;
    }

public:
    literal int BarHeight = 150;

    // Diagnostics: fewest wrong modules seen in the last scan (999 = no barcode-like bars at all)
    int BestError;

    Barcode()
    {
        code = gcnew array<String^>
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

        narrow = 4;
        gap = narrow;
        wide = 12;
        quiet = 40;
        BestError = 999;

        // Expand every symbol to modules: narrow = 1 module, wide = 3 modules
        symbols = gcnew array<String^>(11);
        for (int i = 0; i < 11; i++)
        {
            StringBuilder^ sb = gcnew StringBuilder();
            for (int j = 0; j < 9; j++)
            {
                int modules = (code[i][j] == L'1') ? 3 : 1;
                for (int k = 0; k < modules; k++)
                    sb->Append((j % 2 == 0) ? L'1' : L'0');
            }
            symbols[i] = sb->ToString();
        }
    }

    // Payload -> pixel row (0/1 per pixel column)
    List<int>^ Encode(String^ payload)
    {
        if (String::IsNullOrEmpty(payload))
            throw gcnew ArgumentException("Enter at least one digit.");

        for each (Char c in payload)
        {
            if (c < L'0' || c > L'9')
                throw gcnew ArgumentException("Only digits 0-9 are supported.");
        }

        if (payload->Length != 6)
            throw gcnew ArgumentException("Enter exactly 6 digits.");

        if (payload[0] == L'0')
            throw gcnew ArgumentException("The first digit cannot be 0.");

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

    // Row -> Bitmap (for preview and PNG export)
    Bitmap^ Render(List<int>^ row, int scale)
    {
        int height = BarHeight * scale / 2;
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

    // Row -> SVG file (adjacent black pixels merged into one rect)
    void SaveSvg(List<int>^ row, String^ path)
    {
        StringBuilder^ sb = gcnew StringBuilder();
        sb->AppendLine("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " +
            row->Count + " " + BarHeight + "\">");
        sb->AppendLine("<rect width=\"100%\" height=\"100%\" fill=\"white\"/>");

        int x = 0;
        while (x < row->Count)
        {
            if (row[x] == 1)
            {
                int start = x;
                while (x < row->Count && row[x] == 1) x++;
                sb->AppendLine("<rect x=\"" + start + "\" y=\"0\" width=\"" + (x - start) +
                    "\" height=\"" + BarHeight + "\" fill=\"black\"/>");
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
    List<int>^ LoadSvg(String^ path)
    {
        String^ svg = File::ReadAllText(path);

        List<int>^ starts = gcnew List<int>();
        List<int>^ widths = gcnew List<int>();
        int maxX = 0;

        MatchCollection^ rects = Regex::Matches(svg, "<rect[^>]*>");
        for each (Match ^ m in rects)
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

    // Image file -> pixel row of the scan line that holds the barcode
    List<int>^ LoadImage(String^ path)
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

    // Pixel row -> 6 digits (start/stop stripped). The barcode may sit anywhere in the row.
    String^ Decode(List<int>^ row)
    {
        bool anyBar = false;
        for each (int pixel in row)
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

    // Camera frame -> payload; nullptr when no valid 6-digit barcode is found
    // (normal for most frames).
    String^ TryDecode(Bitmap^ bmp)
    {
        List<int>^ row = ScanForRow(bmp);
        if (row == nullptr)
            return nullptr;
        return Decode(row);
    }
};

// ---------------------------------------------------------------------------
// Product + product file
// ---------------------------------------------------------------------------
public ref class Product : public Record
{
public:
    int Id;
    String^ Barcode;
    String^ Acronym;
    String^ Name;
    String^ Unit;           // count / weight
    double Price;           // per unit, without tax
    double Tax;             // per unit, PKR
    double Discount;        // per unit, PKR
    double Quantity;        // available in stock
    String^ BarcodeFile;

    // "id barcode acronym name... unit price tax discount quantity file"
    // The name may contain spaces, so it is whatever sits between the 3rd
    // field and the last 6 fields.
    static Product^ FromLine(String^ line)
    {
        array<String^>^ f = line->Split(gcnew array<Char>{ L' ' }, StringSplitOptions::RemoveEmptyEntries);
        if (f->Length < 10)
            throw gcnew FormatException("Bad product line: " + line);

        int n = f->Length;
        Product^ p = gcnew Product();
        p->Id = Int32::Parse(f[0]);
        p->Barcode = f[1];
        p->Acronym = f[2];
        p->Name = String::Join(" ", f, 3, n - 9);
        p->Unit = f[n - 6];
        p->Price = Fmt::Parse(f[n - 5]);
        p->Tax = Fmt::Parse(f[n - 4]);
        p->Discount = Fmt::Parse(f[n - 3]);
        p->Quantity = Fmt::Parse(f[n - 2]);
        p->BarcodeFile = f[n - 1];

        if (p->Acronym->Length > 8)
            throw gcnew FormatException("Acronym is longer than 8 characters: " + p->Acronym);

        return p;
    }

    virtual String^ ToLine() override
    {
        return String::Join(" ", gcnew array<String^>
        {
            Id.ToString(), Barcode, Acronym, Name, Unit, Fmt::Number(Price),
                Fmt::Number(Tax), Fmt::Number(Discount), Fmt::Number(Quantity), BarcodeFile
        });
    }

    // Text shown to the cashier / manager
    String^ Describe()
    {
        StringBuilder^ sb = gcnew StringBuilder();
        sb->AppendLine("Product ID   : " + Id.ToString());
        sb->AppendLine("Barcode      : " + Barcode);
        sb->AppendLine("Acronym      : " + Acronym);
        sb->AppendLine("Name         : " + Name);
        sb->AppendLine("Unit type    : " + Unit);
        sb->AppendLine("Unit price   : PKR " + Fmt::Money(Price));
        sb->AppendLine("Tax          : PKR " + Fmt::Money(Tax));
        sb->AppendLine("Discount     : PKR " + Fmt::Money(Discount));
        sb->AppendLine("Available    : " + Fmt::Number(Quantity));
        sb->AppendLine("Barcode file : " + BarcodeFile);
        return sb->ToString();
    }
};

public ref class ProductStore
{
private:
    List<Product^>^ products;

public:
    ProductStore()
    {
        Load();
    }

    void Load()
    {
        products = gcnew List<Product^>();
        for each (String ^ line in RecordFile::ReadLines(Files::ProductFile))
            products->Add(Product::FromLine(line));
    }

    // Writes the current stock back to Product.txt
    void Save()
    {
        List<String^>^ lines = gcnew List<String^>();
        for each (Product ^ p in products)
            lines->Add(p->ToLine());
        RecordFile::WriteLines(Files::ProductFile, lines);
    }

    // nullptr when the barcode is not in Product.txt
    Product^ FindByBarcode(String^ barcode)
    {
        for each (Product ^ p in products)
        {
            if (p->Barcode->Trim() == barcode->Trim())
                return p;
        }
        return nullptr;
    }

    // Where the products came from (shown when something does not match)
    String^ Summary()
    {
        if (!File::Exists(Files::ProductFile))
            return "Product.txt not found. Looked in: " + Files::Folder;

        String^ list = "";
        int shown = 0;
        for each (Product ^ p in products)
        {
            if (shown >= 5)
            {
                list += ", ...";
                break;
            }
            if (shown > 0)
                list += ", ";
            list += p->Barcode;
            shown++;
        }
        return Convert::ToString(products->Count) + " products in " + Files::ProductFile +
            " (barcodes: " + list + ")";
    }
};

// ---------------------------------------------------------------------------
// Order model: cart, order, order detail, bill
// ---------------------------------------------------------------------------

// One product and its quantity inside the order being recorded
public ref class OrderItem
{
public:
    Product^ Item;
    double Quantity;

    OrderItem(Product^ item, double quantity)
    {
        Item = item;
        Quantity = quantity;
    }

    double Subtotal() { return Item->Price * Quantity; }   // without tax
    double Tax() { return Item->Tax * Quantity; }
    double Discount() { return Item->Discount * Quantity; }
};

// The products of the order being recorded + its totals
public ref class Cart
{
private:
    List<OrderItem^>^ items;

    OrderItem^ Find(Product^ product)
    {
        for each (OrderItem ^ line in items)
        {
            if (line->Item == product)
                return line;
        }
        return nullptr;
    }

public:
    Cart()
    {
        items = gcnew List<OrderItem^>();
    }

    property List<OrderItem^>^ Items
    {
        List<OrderItem^>^ get() { return items; }
    }

    // false when the stock cannot cover the quantity (nothing is added then)
    bool Add(Product^ product, double quantity)
    {
        OrderItem^ line = Find(product);
        double wanted = quantity + ((line == nullptr) ? 0 : line->Quantity);

        if (wanted > product->Quantity)
            return false;

        if (line == nullptr)
            items->Add(gcnew OrderItem(product, quantity));
        else
            line->Quantity = wanted;
        return true;
    }

    // Takes one line out of the order
    void Remove(OrderItem^ line)
    {
        items->Remove(line);
    }

    // Takes every line out of the order
    void Clear()
    {
        items->Clear();
    }

    // Sets the quantity of a line. false when it is not positive or the stock
    // cannot cover it (nothing is changed then)
    bool SetQuantity(OrderItem^ line, double quantity)
    {
        if (quantity <= 0 || quantity > line->Item->Quantity)
            return false;
        line->Quantity = quantity;
        return true;
    }

    double TotalWithoutTax()
    {
        double total = 0;
        for each (OrderItem ^ line in items) total += line->Subtotal();
        return total;
    }

    double TotalTax()
    {
        double total = 0;
        for each (OrderItem ^ line in items) total += line->Tax();
        return total;
    }

    double TotalDiscount()
    {
        double total = 0;
        for each (OrderItem ^ line in items) total += line->Discount();
        return total;
    }

    double Payable()
    {
        return TotalWithoutTax() + TotalTax() - TotalDiscount();
    }
};

// One line of Order.txt
public ref class Order : public Record
{
public:
    String^ Id;             // 6 digit running number + 2 digit day of the order
    String^ Date;
    String^ Time;
    String^ CustomerName;
    String^ CustomerContact;
    String^ Status;
    double Payment;
    String^ PaymentMode;    // cash, card, wallet
    String^ Merchant;       // ABL, JazzCash ...

    virtual String^ ToLine() override
    {
        return String::Join(" ", gcnew array<String^>
        {
            Id, Date, Time, Fmt::Pack(CustomerName), Fmt::Pack(CustomerContact),
                Status, Fmt::Money(Payment), PaymentMode, Fmt::Pack(Merchant)
        });
    }
};

// One line of OrderDetail.txt
public ref class OrderDetail : public Record
{
private:
    String^ orderId;
    int productId;
    String^ barcode;
    double quantity;

public:
    OrderDetail(String^ orderId, int productId, String^ barcode, double quantity)
    {
        this->orderId = orderId;
        this->productId = productId;
        this->barcode = barcode;
        this->quantity = quantity;
    }

    virtual String^ ToLine() override
    {
        return String::Join(" ", gcnew array<String^>
        {
            orderId, productId.ToString(), barcode, Fmt::Number(quantity)
        });
    }
};

// One line of Bill.txt. Products are listed with ',' between them so that
// one bill stays on one line.
public ref class Bill : public Record
{
public:
    String^ Id;             // 9 digits
    Order^ Source;
    Cart^ Lines;

    virtual String^ ToLine() override
    {
        List<String^>^ acronyms = gcnew List<String^>();
        List<String^>^ prices = gcnew List<String^>();
        List<String^>^ discounts = gcnew List<String^>();
        List<String^>^ quantities = gcnew List<String^>();

        for each (OrderItem ^ line in Lines->Items)
        {
            acronyms->Add(line->Item->Acronym);
            prices->Add(Fmt::Number(line->Item->Price));
            discounts->Add(Fmt::Number(line->Item->Discount));
            quantities->Add(Fmt::Number(line->Quantity));
        }

        return String::Join(" ", gcnew array<String^>
        {
            Id, Fmt::Pack(Source->CustomerName), Source->Id,
                String::Join(",", acronyms->ToArray()),
                String::Join(",", prices->ToArray()),
                String::Join(",", discounts->ToArray()),
                String::Join(",", quantities->ToArray()),
                Fmt::Number(Lines->TotalWithoutTax()),
                Fmt::Number(Lines->TotalTax()),
                Fmt::Number(Lines->TotalDiscount()),
                Fmt::Number(Lines->Payable()),
                Source->Status
        });
    }

    // Text shown on the screen
    String^ ToText()
    {
        StringBuilder^ sb = gcnew StringBuilder();
        sb->AppendLine("BILL " + Id);
        sb->AppendLine("Order    : " + Source->Id + "  " + Source->Date + " " + Source->Time);
        sb->AppendLine("Customer : " + Source->CustomerName);
        sb->AppendLine("Payment  : " + Source->PaymentMode + " " + Fmt::Pack(Source->Merchant));
        sb->AppendLine("Status   : " + Source->Status);
        sb->AppendLine("----------------------------------------------");
        sb->AppendLine(Fmt::Left("Item", 10) + Fmt::Right("Qty", 6) + Fmt::Right("Price", 10) +
            Fmt::Right("Disc", 8) + Fmt::Right("Total", 10));

        for each (OrderItem ^ line in Lines->Items)
        {
            sb->AppendLine(Fmt::Left(line->Item->Acronym, 10) +
                Fmt::Right(Fmt::Number(line->Quantity), 6) +
                Fmt::Right(Fmt::Money(line->Item->Price), 10) +
                Fmt::Right(Fmt::Money(line->Discount()), 8) +
                Fmt::Right(Fmt::Money(line->Subtotal()), 10));
        }

        sb->AppendLine("----------------------------------------------");
        sb->AppendLine(Fmt::Left("Total without tax", 30) + Fmt::Right(Fmt::Money(Lines->TotalWithoutTax()), 16));
        sb->AppendLine(Fmt::Left("Total tax", 30) + Fmt::Right(Fmt::Money(Lines->TotalTax()), 16));
        sb->AppendLine(Fmt::Left("Total discount", 30) + Fmt::Right(Fmt::Money(Lines->TotalDiscount()), 16));
        sb->AppendLine(Fmt::Left("PAYABLE (PKR)", 30) + Fmt::Right(Fmt::Money(Lines->Payable()), 16));
        return sb->ToString();
    }
};

// ---------------------------------------------------------------------------
// Checkout: turns a cart into an order, order details and a bill (no GUI here)
// ---------------------------------------------------------------------------
public ref class OrderService
{
private:
    ProductStore^ products;

    // Next running number of a file, zero padded to 'digits'
    String^ NextNumber(String^ path, int digits)
    {
        int next = RecordFile::ReadLines(path)->Count + 1;
        return next.ToString()->PadLeft(digits, L'0');
    }

public:
    OrderService(ProductStore^ store)
    {
        products = store;
    }

    Bill^ Checkout(String^ customer, String^ contact, String^ mode, String^ merchant, Cart^ cart)
    {
        if (String::IsNullOrEmpty(customer))
            throw gcnew ArgumentException("Enter the customer name.");

        if (cart->Items->Count == 0)
            throw gcnew InvalidOperationException("Scan at least one product first.");

        DateTime now = DateTime::Now;

        Order^ order = gcnew Order();
        order->Id = NextNumber(Files::OrderFile, 6) + now.ToString("dd", CultureInfo::InvariantCulture);
        order->Date = now.ToString("yyyy-MM-dd", CultureInfo::InvariantCulture);
        order->Time = now.ToString("HH:mm:ss", CultureInfo::InvariantCulture);
        order->CustomerName = customer;
        order->CustomerContact = contact;
        order->Status = OrderStatus::Paid;
        order->Payment = cart->Payable();
        order->PaymentMode = mode;
        order->Merchant = merchant;

        Bill^ bill = gcnew Bill();
        bill->Id = NextNumber(Files::BillFile, 9);
        bill->Source = order;
        bill->Lines = cart;

        RecordFile::Append(Files::OrderFile, order);
        for each (OrderItem ^ line in cart->Items)
        {
            RecordFile::Append(Files::OrderDetailFile,
                gcnew OrderDetail(order->Id, line->Item->Id, line->Item->Barcode, line->Quantity));
            line->Item->Quantity -= line->Quantity;     // stock goes down
        }
        RecordFile::Append(Files::BillFile, bill);
        products->Save();

        return bill;
    }
};

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

// Talks to the first camera and keeps its latest frame
public ref class CameraScanner
{
private:
    VideoCaptureDevice^ source;
    Bitmap^ lastFrame;
    Object^ sync;

    // Called by AForge on its own thread for every frame
    void OnNewFrame(Object^ sender, NewFrameEventArgs^ e)
    {
        Bitmap^ frame = (Bitmap^)e->Frame->Clone();

        System::Threading::Monitor::Enter(sync);
        try
        {
            Bitmap^ old = lastFrame;
            lastFrame = frame;
            if (old != nullptr) delete old;
        }
        finally
        {
            System::Threading::Monitor::Exit(sync);
        }
    }

public:
    CameraScanner()
    {
        sync = gcnew Object();
        lastFrame = nullptr;

        FilterInfoCollection^ devices = gcnew FilterInfoCollection(FilterCategory::VideoInputDevice);
        if (devices->Count == 0)
            throw gcnew InvalidOperationException("No camera found.");

        source = gcnew VideoCaptureDevice(devices[0]->MonikerString);

        // Biggest mode up to 1280 px wide: thin bars stay readable at a normal distance
        VideoCapabilities^ best = nullptr;
        for each (VideoCapabilities ^ cap in source->VideoCapabilities)
        {
            if (cap->FrameSize.Width > 1280)
                continue;

            if (best == nullptr || cap->FrameSize.Width > best->FrameSize.Width)
                best = cap;
        }
        if (best != nullptr)
            source->VideoResolution = best;

        source->NewFrame += gcnew NewFrameEventHandler(this, &CameraScanner::OnNewFrame);
    }

    void Start()
    {
        source->Start();
    }

    void Stop()
    {
        source->SignalToStop();
        source->WaitForStop();
    }

    // A copy of the latest frame (caller owns it), nullptr before the first frame
    Bitmap^ GetFrame()
    {
        Bitmap^ copy = nullptr;

        System::Threading::Monitor::Enter(sync);
        try
        {
            if (lastFrame != nullptr)
                copy = (Bitmap^)lastFrame->Clone();
        }
        finally
        {
            System::Threading::Monitor::Exit(sync);
        }
        return copy;
    }
};

// Reusable control: live camera preview + barcode detection.
// Put it on any form and subscribe to BarcodeScanned.
public ref class ScannerControl : public UserControl
{
private:
    Barcode^ barcode;
    CameraScanner^ camera;
    PictureBox^ pic;
    Timer^ timer;
    Label^ info;
    DateTime pausedUntil;
    DateTime nextDecode;

public:
    event BarcodeHandler^ BarcodeScanned;

    ScannerControl()
    {
        barcode = gcnew Barcode();
        pausedUntil = DateTime::MinValue;
        nextDecode = DateTime::MinValue;

        pic = gcnew PictureBox();
        pic->Dock = DockStyle::Fill;
        pic->BorderStyle = System::Windows::Forms::BorderStyle::FixedSingle;
        pic->BackColor = Color::Black;
        pic->SizeMode = PictureBoxSizeMode::Zoom;
        this->Controls->Add(pic);

        // Small status line under the preview: shows what the scanner sees
        info = gcnew Label();
        info->Dock = DockStyle::Bottom;
        info->Height = 20;
        info->BackColor = Color::Black;
        info->ForeColor = Color::Lime;
        info->Text = "Waiting for camera...";
        this->Controls->Add(info);

        camera = gcnew CameraScanner();     // throws when there is no camera
        camera->Start();

        timer = gcnew Timer();
        timer->Interval = 40;       // about 25 preview frames per second
        timer->Tick += gcnew EventHandler(this, &ScannerControl::OnTick);
        timer->Start();
    }

    // Disposed together with the form: switches the camera off
    ~ScannerControl()
    {
        timer->Stop();
        camera->Stop();
    }

    // Stops / restarts reading (the camera stays on). Use around dialogs.
    void Pause()
    {
        timer->Stop();
    }

    void Resume()
    {
        timer->Start();
    }

private:
    void OnTick(Object^ sender, EventArgs^ e)
    {
        Bitmap^ frame = camera->GetFrame();
        if (frame == nullptr)
            return;

        Image^ old = pic->Image;
        pic->Image = frame;
        if (old != nullptr) delete old;

        // Showing the preview is cheap, decoding is not: decode 4 times a second at most.
        // The same barcode stays in front of the camera for a while, so also wait 2 s after a read.
        if (DateTime::Now < pausedUntil || DateTime::Now < nextDecode)
            return;
        nextDecode = DateTime::Now.AddMilliseconds(250);

        String^ payload = barcode->TryDecode(frame);
        if (payload == nullptr)
        {
            String^ size = "Camera " + frame->Width.ToString() + "x" + frame->Height.ToString();
            if (barcode->BestError < 999)
                info->Text = size + "  | closest match: " + barcode->BestError.ToString() + " wrong modules (need 8 or less)";
            else
                info->Text = size + "  | no barcode-like bars found";
            return;
        }

        info->Text = "Read: " + payload;
        pausedUntil = DateTime::Now.AddSeconds(2);
        BarcodeScanned(payload);
    }
};

// ---------------------------------------------------------------------------
// Barcode generator window (increment 1, unchanged)
// ---------------------------------------------------------------------------
public ref class BarcodeForm : public Form
{
private:
    Barcode^ barcode;
    List<int>^ currentRow;

    Label^ lblPayload;
    TextBox^ txtPayload;
    Button^ btnPreview;
    Button^ btnSave;
    Button^ btnLoad;
    Button^ btnBack;
    PictureBox^ pic;
    StatusStrip^ status;
    ToolStripStatusLabel^ lblStatus;

public:
    BarcodeForm()
    {
        barcode = gcnew Barcode();
        currentRow = nullptr;
        BuildUi();
    }

private:
    void BuildUi()
    {
        this->Text = "Code 39 Barcode Tool";
        this->ClientSize = Drawing::Size(720, 420);
        this->MinimumSize = Drawing::Size(560, 320);
        this->StartPosition = FormStartPosition::CenterScreen;

        lblPayload = gcnew Label();
        lblPayload->Text = "Digits:";
        lblPayload->Location = Point(12, 18);
        lblPayload->AutoSize = true;

        txtPayload = gcnew TextBox();
        txtPayload->Location = Point(65, 14);
        txtPayload->Size = Drawing::Size(200, 23);
        txtPayload->MaxLength = 30;

        btnPreview = gcnew Button();
        btnPreview->Text = "Generate";
        btnPreview->Location = Point(280, 12);
        btnPreview->Size = Drawing::Size(90, 27);
        btnPreview->Click += gcnew EventHandler(this, &BarcodeForm::OnPreview);

        btnSave = gcnew Button();
        btnSave->Text = "Save...";
        btnSave->Location = Point(378, 12);
        btnSave->Size = Drawing::Size(90, 27);
        btnSave->Click += gcnew EventHandler(this, &BarcodeForm::OnSave);

        btnLoad = gcnew Button();
        btnLoad->Text = "Load...";
        btnLoad->Location = Point(476, 12);
        btnLoad->Size = Drawing::Size(90, 27);
        btnLoad->Click += gcnew EventHandler(this, &BarcodeForm::OnLoad);

        btnBack = gcnew Button();
        btnBack->Text = "Back";
        btnBack->Location = Point(574, 12);
        btnBack->Size = Drawing::Size(90, 27);
        btnBack->Click += gcnew EventHandler(this, &BarcodeForm::OnBack);

        pic = gcnew PictureBox();
        pic->Location = Point(12, 52);
        pic->Size = Drawing::Size(696, 330);
        pic->Anchor = AnchorStyles::Top | AnchorStyles::Bottom | AnchorStyles::Left | AnchorStyles::Right;
        pic->BorderStyle = BorderStyle::FixedSingle;
        pic->BackColor = Color::White;
        pic->SizeMode = PictureBoxSizeMode::Zoom;

        status = gcnew StatusStrip();
        lblStatus = gcnew ToolStripStatusLabel("Enter digits and click Generate (saved as <code>.svg), or load a saved barcode.");
        status->Items->Add(lblStatus);

        this->Controls->Add(lblPayload);
        this->Controls->Add(txtPayload);
        this->Controls->Add(btnPreview);
        this->Controls->Add(btnSave);
        this->Controls->Add(btnLoad);
        this->Controls->Add(btnBack);
        this->Controls->Add(pic);
        this->Controls->Add(status);

        this->AcceptButton = btnPreview;
        this->CancelButton = btnBack;
    }

    void ShowRow(List<int>^ row)
    {
        Image^ old = pic->Image;
        pic->Image = barcode->Render(row, 3);
        if (old != nullptr) delete old;
    }

    void SetStatus(String^ msg)
    {
        lblStatus->Text = msg;
    }

    void OnBack(Object^ sender, EventArgs^ e)
    {
        this->Close();
    }

    // Generates the barcode and saves it right away as <code>.svg
    // (next to Product.txt, so it matches the barcode file name of the product)
    void OnPreview(Object^ sender, EventArgs^ e)
    {
        try
        {
            String^ code = txtPayload->Text->Trim();
            currentRow = barcode->Encode(code);
            ShowRow(currentRow);

            String^ path = Path::Combine(Files::Folder, code + ".svg");
            barcode->SaveSvg(currentRow, path);
            SetStatus("Generated and saved: " + path);
        }
        catch (Exception^ ex)
        {
            MessageBox::Show(ex->Message, "Cannot generate", MessageBoxButtons::OK, MessageBoxIcon::Warning);
        }
    }

    void OnSave(Object^ sender, EventArgs^ e)
    {
        try
        {
            // Always save what the textbox says right now
            String^ code = txtPayload->Text->Trim();
            currentRow = barcode->Encode(code);
            ShowRow(currentRow);

            SaveFileDialog^ dlg = gcnew SaveFileDialog();
            dlg->Filter = "SVG barcode (*.svg)|*.svg|PNG image (*.png)|*.png";
            dlg->FileName = code;                       // the barcode's own code
            dlg->InitialDirectory = Files::Folder;
            dlg->DefaultExt = "svg";
            dlg->AddExtension = true;

            if (dlg->ShowDialog() != System::Windows::Forms::DialogResult::OK)
                return;

            if (Path::GetExtension(dlg->FileName)->ToLower() == ".png")
            {
                Bitmap^ bmp = barcode->Render(currentRow, 3);
                bmp->Save(dlg->FileName, ImageFormat::Png);
                delete bmp;
            }
            else
            {
                barcode->SaveSvg(currentRow, dlg->FileName);
            }
            SetStatus("Saved: " + dlg->FileName);
        }
        catch (Exception^ ex)
        {
            MessageBox::Show(ex->Message, "Cannot save", MessageBoxButtons::OK, MessageBoxIcon::Warning);
        }
    }

    void OnLoad(Object^ sender, EventArgs^ e)
    {
        OpenFileDialog^ dlg = gcnew OpenFileDialog();
        dlg->Filter = "Barcode image (*.svg;*.png;*.jpg;*.jpeg;*.bmp)|*.svg;*.png;*.jpg;*.jpeg;*.bmp|SVG barcode (*.svg)|*.svg|Image (*.png;*.jpg;*.jpeg;*.bmp)|*.png;*.jpg;*.jpeg;*.bmp";

        if (dlg->ShowDialog() != System::Windows::Forms::DialogResult::OK)
            return;

        try
        {
            List<int>^ row;
            if (Path::GetExtension(dlg->FileName)->ToLower() == ".svg")
                row = barcode->LoadSvg(dlg->FileName);
            else
                row = barcode->LoadImage(dlg->FileName);
            String^ decoded = barcode->Decode(row);

            currentRow = row;
            txtPayload->Text = decoded;
            ShowRow(currentRow);
            SetStatus("Loaded " + Path::GetFileName(dlg->FileName) + " - decoded: " + decoded);
        }
        catch (Exception^ ex)
        {
            MessageBox::Show(ex->Message, "Cannot read barcode", MessageBoxButtons::OK, MessageBoxIcon::Warning);
        }
    }
};

// ---------------------------------------------------------------------------
// Product description window (cashier or manager scans a barcode)
// ---------------------------------------------------------------------------
public ref class ProductForm : public Form
{
private:
    ProductStore^ products;

    ScannerControl^ scanner;
    TextBox^ txtInfo;
    Button^ btnBack;
    StatusStrip^ status;
    ToolStripStatusLabel^ lblStatus;

public:
    ProductForm()
    {
        products = gcnew ProductStore();
        BuildUi();
        SetStatus(products->Summary());
    }

private:
    void BuildUi()
    {
        this->Text = "Product Description";
        this->ClientSize = Drawing::Size(820, 420);
        this->StartPosition = FormStartPosition::CenterScreen;

        scanner = gcnew ScannerControl();
        scanner->Location = Point(12, 12);
        scanner->Size = Drawing::Size(400, 330);
        scanner->BarcodeScanned += gcnew BarcodeHandler(this, &ProductForm::OnBarcodeScanned);

        txtInfo = gcnew TextBox();
        txtInfo->Location = Point(430, 12);
        txtInfo->Size = Drawing::Size(378, 330);
        txtInfo->Multiline = true;
        txtInfo->ReadOnly = true;
        txtInfo->Font = gcnew Drawing::Font("Consolas", 10.0f);

        btnBack = gcnew Button();
        btnBack->Text = "Back";
        btnBack->Location = Point(718, 350);
        btnBack->Size = Drawing::Size(90, 27);
        btnBack->Click += gcnew EventHandler(this, &ProductForm::OnBack);

        status = gcnew StatusStrip();
        lblStatus = gcnew ToolStripStatusLabel("Show a product barcode to the camera.");
        status->Items->Add(lblStatus);

        this->Controls->Add(scanner);
        this->Controls->Add(txtInfo);
        this->Controls->Add(btnBack);
        this->Controls->Add(status);

        this->CancelButton = btnBack;
    }

    void SetStatus(String^ msg)
    {
        lblStatus->Text = msg;
    }

    void OnBack(Object^ sender, EventArgs^ e)
    {
        this->Close();
    }

    void OnBarcodeScanned(String^ payload)
    {
        try
        {
            Product^ product = products->FindByBarcode(payload);
            if (product == nullptr)
            {
                txtInfo->Text = "Scanned barcode " + payload + " is not in Product.txt." +
                    Environment::NewLine + Environment::NewLine + products->Summary();
                SetStatus("Unknown barcode: " + payload);
                return;
            }

            txtInfo->Text = product->Describe();
            SetStatus("Scanned: " + payload);
        }
        catch (Exception^ ex)
        {
            txtInfo->Text = "Error: " + ex->Message;
        }
    }
};

// ---------------------------------------------------------------------------
// Bill window
// ---------------------------------------------------------------------------
public ref class BillForm : public Form
{
private:
    TextBox^ txtBill;
    Button^ btnClose;

public:
    BillForm(Bill^ bill)
    {
        this->Text = "Bill " + bill->Id;
        this->ClientSize = Drawing::Size(520, 480);
        this->StartPosition = FormStartPosition::CenterParent;

        txtBill = gcnew TextBox();
        txtBill->Location = Point(12, 12);
        txtBill->Size = Drawing::Size(496, 420);
        txtBill->Multiline = true;
        txtBill->ReadOnly = true;
        txtBill->ScrollBars = ScrollBars::Vertical;
        txtBill->Font = gcnew Drawing::Font("Consolas", 10.0f);
        txtBill->Text = bill->ToText();

        btnClose = gcnew Button();
        btnClose->Text = "Close";
        btnClose->Location = Point(418, 442);
        btnClose->Size = Drawing::Size(90, 27);
        btnClose->DialogResult = System::Windows::Forms::DialogResult::OK;

        this->Controls->Add(txtBill);
        this->Controls->Add(btnClose);

        this->AcceptButton = btnClose;
    }
};

// ---------------------------------------------------------------------------
// Small dialog: ask for a quantity
// ---------------------------------------------------------------------------
public ref class QuantityForm : public Form
{
private:
    Label^ lblPrompt;
    NumericUpDown^ numQuantity;
    Button^ btnOk;
    Button^ btnCancel;

public:
    QuantityForm(String^ title, String^ prompt, double current, double maximum)
    {
        maximum = Math::Min(Math::Max(maximum, 0.01), 1000000.0);
        current = Math::Min(Math::Max(current, 0.01), maximum);

        this->Text = title;
        this->ClientSize = Drawing::Size(360, 130);
        this->StartPosition = FormStartPosition::CenterParent;
        this->FormBorderStyle = System::Windows::Forms::FormBorderStyle::FixedDialog;
        this->MaximizeBox = false;
        this->MinimizeBox = false;

        lblPrompt = gcnew Label();
        lblPrompt->Text = prompt;
        lblPrompt->Location = Point(12, 12);
        lblPrompt->Size = Drawing::Size(336, 36);

        numQuantity = gcnew NumericUpDown();
        numQuantity->Location = Point(12, 56);
        numQuantity->Size = Drawing::Size(120, 23);
        numQuantity->DecimalPlaces = 2;
        numQuantity->Minimum = Decimal(0.01);
        numQuantity->Maximum = Decimal(maximum);
        numQuantity->Value = Decimal(current);

        btnOk = gcnew Button();
        btnOk->Text = "OK";
        btnOk->Location = Point(168, 92);
        btnOk->Size = Drawing::Size(85, 27);
        btnOk->DialogResult = System::Windows::Forms::DialogResult::OK;

        btnCancel = gcnew Button();
        btnCancel->Text = "Cancel";
        btnCancel->Location = Point(263, 92);
        btnCancel->Size = Drawing::Size(85, 27);
        btnCancel->DialogResult = System::Windows::Forms::DialogResult::Cancel;

        this->Controls->Add(lblPrompt);
        this->Controls->Add(numQuantity);
        this->Controls->Add(btnOk);
        this->Controls->Add(btnCancel);

        this->AcceptButton = btnOk;
        this->CancelButton = btnCancel;
    }

    property double Quantity
    {
        double get() { return Decimal::ToDouble(numQuantity->Value); }
    }
};

// ---------------------------------------------------------------------------
// Order window (cashier records an order and generates the bill)
// ---------------------------------------------------------------------------
public ref class OrderForm : public Form
{
private:
    ProductStore^ products;
    OrderService^ service;
    Cart^ cart;
    Product^ lastProduct;

    ScannerControl^ scanner;
    Label^ lblCustomer;
    TextBox^ txtCustomer;
    Label^ lblContact;
    TextBox^ txtContact;
    Label^ lblMode;
    ComboBox^ cmbMode;
    Label^ lblMerchant;
    TextBox^ txtMerchant;
    Label^ lblQty;
    NumericUpDown^ numQty;
    ListView^ list;
    Label^ lblTotal;
    Button^ btnQty;
    Button^ btnRemove;
    Button^ btnClear;
    Button^ btnDescription;
    Button^ btnBill;
    Button^ btnBack;
    StatusStrip^ status;
    ToolStripStatusLabel^ lblStatus;

public:
    OrderForm()
    {
        products = gcnew ProductStore();
        service = gcnew OrderService(products);
        cart = gcnew Cart();
        lastProduct = nullptr;
        BuildUi();
        SetStatus(products->Summary());
    }

private:
    void BuildUi()
    {
        this->Text = "Record Order";
        this->ClientSize = Drawing::Size(910, 520);
        this->StartPosition = FormStartPosition::CenterScreen;

        scanner = gcnew ScannerControl();
        scanner->Location = Point(12, 12);
        scanner->Size = Drawing::Size(400, 300);
        scanner->BarcodeScanned += gcnew BarcodeHandler(this, &OrderForm::OnBarcodeScanned);

        lblCustomer = gcnew Label();
        lblCustomer->Text = "Customer:";
        lblCustomer->Location = Point(12, 334);
        lblCustomer->AutoSize = true;

        txtCustomer = gcnew TextBox();
        txtCustomer->Location = Point(110, 330);
        txtCustomer->Size = Drawing::Size(300, 23);

        lblContact = gcnew Label();
        lblContact->Text = "Contact:";
        lblContact->Location = Point(12, 366);
        lblContact->AutoSize = true;

        txtContact = gcnew TextBox();
        txtContact->Location = Point(110, 362);
        txtContact->Size = Drawing::Size(300, 23);

        lblMode = gcnew Label();
        lblMode->Text = "Payment mode:";
        lblMode->Location = Point(12, 398);
        lblMode->AutoSize = true;

        cmbMode = gcnew ComboBox();
        cmbMode->Location = Point(110, 394);
        cmbMode->Size = Drawing::Size(300, 23);
        cmbMode->DropDownStyle = ComboBoxStyle::DropDownList;
        cmbMode->Items->AddRange(gcnew array<Object^>{ "cash", "card", "wallet" });
        cmbMode->SelectedIndex = 0;

        lblMerchant = gcnew Label();
        lblMerchant->Text = "Merchant:";
        lblMerchant->Location = Point(12, 430);
        lblMerchant->AutoSize = true;

        txtMerchant = gcnew TextBox();
        txtMerchant->Location = Point(110, 426);
        txtMerchant->Size = Drawing::Size(300, 23);

        lblQty = gcnew Label();
        lblQty->Text = "Quantity:";
        lblQty->Location = Point(12, 462);
        lblQty->AutoSize = true;

        numQty = gcnew NumericUpDown();
        numQty->Location = Point(110, 458);
        numQty->Size = Drawing::Size(100, 23);
        numQty->DecimalPlaces = 2;
        numQty->Minimum = Decimal(0.01);
        numQty->Maximum = Decimal(1000);
        numQty->Value = Decimal(1);

        list = gcnew ListView();
        list->Location = Point(430, 12);
        list->Size = Drawing::Size(468, 340);
        list->View = View::Details;
        list->FullRowSelect = true;
        list->Columns->Add("Item", 70);
        list->Columns->Add("Name", 170);
        list->Columns->Add("Qty", 60);
        list->Columns->Add("Price", 80);
        list->Columns->Add("Total", 80);
        list->DoubleClick += gcnew EventHandler(this, &OrderForm::OnChangeQty);
        list->KeyDown += gcnew KeyEventHandler(this, &OrderForm::OnListKey);

        btnQty = gcnew Button();
        btnQty->Text = "Change Qty";
        btnQty->Location = Point(430, 360);
        btnQty->Size = Drawing::Size(110, 28);
        btnQty->Click += gcnew EventHandler(this, &OrderForm::OnChangeQty);

        btnRemove = gcnew Button();
        btnRemove->Text = "Remove";
        btnRemove->Location = Point(548, 360);
        btnRemove->Size = Drawing::Size(100, 28);
        btnRemove->Click += gcnew EventHandler(this, &OrderForm::OnRemove);

        btnClear = gcnew Button();
        btnClear->Text = "Clear All";
        btnClear->Location = Point(656, 360);
        btnClear->Size = Drawing::Size(100, 28);
        btnClear->Click += gcnew EventHandler(this, &OrderForm::OnClear);

        lblTotal = gcnew Label();
        lblTotal->Location = Point(430, 402);
        lblTotal->AutoSize = true;
        lblTotal->Font = gcnew Drawing::Font("Segoe UI", 12.0f, FontStyle::Bold);
        lblTotal->Text = "Payable: PKR 0.00";

        btnDescription = gcnew Button();
        btnDescription->Text = "Description";
        btnDescription->Location = Point(430, 440);
        btnDescription->Size = Drawing::Size(120, 30);
        btnDescription->Click += gcnew EventHandler(this, &OrderForm::OnDescription);

        btnBill = gcnew Button();
        btnBill->Text = "Generate Bill";
        btnBill->Location = Point(560, 440);
        btnBill->Size = Drawing::Size(140, 30);
        btnBill->Click += gcnew EventHandler(this, &OrderForm::OnBill);

        btnBack = gcnew Button();
        btnBack->Text = "Back";
        btnBack->Location = Point(710, 440);
        btnBack->Size = Drawing::Size(90, 30);
        btnBack->Click += gcnew EventHandler(this, &OrderForm::OnBack);

        status = gcnew StatusStrip();
        lblStatus = gcnew ToolStripStatusLabel("Set the quantity, then show a product barcode to the camera.");
        status->Items->Add(lblStatus);

        this->Controls->Add(scanner);
        this->Controls->Add(lblCustomer);
        this->Controls->Add(txtCustomer);
        this->Controls->Add(lblContact);
        this->Controls->Add(txtContact);
        this->Controls->Add(lblMode);
        this->Controls->Add(cmbMode);
        this->Controls->Add(lblMerchant);
        this->Controls->Add(txtMerchant);
        this->Controls->Add(lblQty);
        this->Controls->Add(numQty);
        this->Controls->Add(list);
        this->Controls->Add(btnQty);
        this->Controls->Add(btnRemove);
        this->Controls->Add(btnClear);
        this->Controls->Add(lblTotal);
        this->Controls->Add(btnDescription);
        this->Controls->Add(btnBill);
        this->Controls->Add(btnBack);
        this->Controls->Add(status);

        this->CancelButton = btnBack;
    }

    void OnBack(Object^ sender, EventArgs^ e)
    {
        this->Close();
    }

    void SetStatus(String^ msg)
    {
        lblStatus->Text = msg;
    }

    // Cart -> list on the screen
    void RefreshList()
    {
        list->Items->Clear();
        for each (OrderItem ^ line in cart->Items)
        {
            ListViewItem^ row = gcnew ListViewItem(line->Item->Acronym);
            row->SubItems->Add(line->Item->Name);
            row->SubItems->Add(Fmt::Number(line->Quantity));
            row->SubItems->Add(Fmt::Money(line->Item->Price));
            row->SubItems->Add(Fmt::Money(line->Subtotal()));
            list->Items->Add(row);
        }
        lblTotal->Text = "Payable: PKR " + Fmt::Money(cart->Payable());
    }

    // Clears the screen for the next customer
    void NewOrder()
    {
        cart = gcnew Cart();
        lastProduct = nullptr;
        txtCustomer->Clear();
        txtContact->Clear();
        txtMerchant->Clear();
        cmbMode->SelectedIndex = 0;
        numQty->Value = Decimal(1);
        RefreshList();
        SetStatus("Ready for the next order.");
    }

    void OnBarcodeScanned(String^ payload)
    {
        try
        {
            Product^ product = products->FindByBarcode(payload);
            if (product == nullptr)
            {
                Alert("Barcode " + payload + " was read, but it is not in Product.txt." +
                    Environment::NewLine + Environment::NewLine + products->Summary());
                return;
            }

            lastProduct = product;
            double qty = Decimal::ToDouble(numQty->Value);
            if (!cart->Add(product, qty))
            {
                Alert("Not enough stock of " + product->Acronym + " (available: " +
                    Fmt::Number(product->Quantity) + ", wanted: " + Fmt::Number(qty) + ").");
                return;
            }

            RefreshList();
            SetStatus("Added " + product->Acronym + " x " + Fmt::Number(qty) +
                " at PKR " + Fmt::Money(product->Price));
        }
        catch (Exception^ ex)
        {
            Alert("Scan error: " + ex->Message);
        }
    }

    // Message box that does not let the camera read the same barcode again meanwhile
    void Alert(String^ message)
    {
        SetStatus(message);
        scanner->Pause();
        MessageBox::Show(message, "Scan", MessageBoxButtons::OK, MessageBoxIcon::Warning);
        scanner->Resume();
    }

    // The cart line selected in the list, nullptr when nothing is selected
    OrderItem^ SelectedLine()
    {
        if (list->SelectedIndices->Count == 0)
            return nullptr;
        return cart->Items[list->SelectedIndices[0]];
    }

    void SelectRow(int index)
    {
        if (index >= 0 && index < list->Items->Count)
            list->Items[index]->Selected = true;
    }

    // Change the quantity of the selected line (button or double click)
    void OnChangeQty(Object^ sender, EventArgs^ e)
    {
        OrderItem^ line = SelectedLine();
        if (line == nullptr)
        {
            SetStatus("Select a product in the list first.");
            return;
        }
        int index = list->SelectedIndices[0];

        scanner->Pause();
        QuantityForm^ dlg = gcnew QuantityForm("Change quantity",
            line->Item->Acronym + " - " + line->Item->Name +
            "   (in stock: " + Fmt::Number(line->Item->Quantity) + ")",
            line->Quantity, line->Item->Quantity);
        System::Windows::Forms::DialogResult result = dlg->ShowDialog();
        double quantity = dlg->Quantity;
        delete dlg;
        scanner->Resume();

        if (result != System::Windows::Forms::DialogResult::OK)
            return;

        if (!cart->SetQuantity(line, quantity))
        {
            SetStatus("Not enough stock of " + line->Item->Acronym +
                " (available: " + Fmt::Number(line->Item->Quantity) + ")");
            return;
        }

        RefreshList();
        SelectRow(index);
        SetStatus("Quantity of " + line->Item->Acronym + " is now " + Fmt::Number(quantity));
    }

    void RemoveSelected()
    {
        OrderItem^ line = SelectedLine();
        if (line == nullptr)
        {
            SetStatus("Select a product in the list first.");
            return;
        }
        int index = list->SelectedIndices[0];

        cart->Remove(line);
        RefreshList();
        SelectRow(Math::Min(index, list->Items->Count - 1));
        SetStatus("Removed " + line->Item->Acronym);
    }

    void OnRemove(Object^ sender, EventArgs^ e)
    {
        RemoveSelected();
    }

    // Delete key removes the selected line
    void OnListKey(Object^ sender, KeyEventArgs^ e)
    {
        if (e->KeyCode == Keys::Delete)
            RemoveSelected();
    }

    void OnClear(Object^ sender, EventArgs^ e)
    {
        if (cart->Items->Count == 0)
        {
            SetStatus("The order is already empty.");
            return;
        }

        scanner->Pause();
        System::Windows::Forms::DialogResult answer = MessageBox::Show(
            "Remove all products from this order?", "Clear order",
            MessageBoxButtons::YesNo, MessageBoxIcon::Question);
        scanner->Resume();

        if (answer != System::Windows::Forms::DialogResult::Yes)
            return;

        cart->Clear();
        lastProduct = nullptr;
        RefreshList();
        SetStatus("Order cleared.");
    }

    // Describes the selected line, otherwise the last scanned product
    void OnDescription(Object^ sender, EventArgs^ e)
    {
        Product^ product = lastProduct;
        if (list->SelectedIndices->Count > 0)
            product = cart->Items[list->SelectedIndices[0]]->Item;

        if (product == nullptr)
        {
            SetStatus("Scan or select a product first.");
            return;
        }

        scanner->Pause();
        MessageBox::Show(product->Describe(), "Product description", MessageBoxButtons::OK, MessageBoxIcon::Information);
        scanner->Resume();
    }

    void OnBill(Object^ sender, EventArgs^ e)
    {
        try
        {
            Bill^ bill = service->Checkout(txtCustomer->Text->Trim(), txtContact->Text->Trim(),
                cmbMode->Text, txtMerchant->Text->Trim(), cart);

            scanner->Pause();
            BillForm^ view = gcnew BillForm(bill);
            view->ShowDialog();
            delete view;
            scanner->Resume();

            NewOrder();
        }
        catch (Exception^ ex)
        {
            MessageBox::Show(ex->Message, "Cannot generate bill", MessageBoxButtons::OK, MessageBoxIcon::Warning);
        }
    }
};

// ---------------------------------------------------------------------------
// Main window
// ---------------------------------------------------------------------------
public ref class MenuForm : public Form
{
private:
    Button^ btnOrder;
    Button^ btnProduct;
    Button^ btnBarcode;

public:
    MenuForm()
    {
        BuildUi();
    }

private:
    void BuildUi()
    {
        this->Text = "POS";
        this->ClientSize = Drawing::Size(320, 220);
        this->StartPosition = FormStartPosition::CenterScreen;

        btnOrder = gcnew Button();
        btnOrder->Text = "Record Order";
        btnOrder->Location = Point(40, 20);
        btnOrder->Size = Drawing::Size(240, 45);
        btnOrder->Click += gcnew EventHandler(this, &MenuForm::OnOrder);

        btnProduct = gcnew Button();
        btnProduct->Text = "View Product";
        btnProduct->Location = Point(40, 80);
        btnProduct->Size = Drawing::Size(240, 45);
        btnProduct->Click += gcnew EventHandler(this, &MenuForm::OnProduct);

        btnBarcode = gcnew Button();
        btnBarcode->Text = "Barcode Generator";
        btnBarcode->Location = Point(40, 140);
        btnBarcode->Size = Drawing::Size(240, 45);
        btnBarcode->Click += gcnew EventHandler(this, &MenuForm::OnBarcode);

        this->Controls->Add(btnOrder);
        this->Controls->Add(btnProduct);
        this->Controls->Add(btnBarcode);
    }

    // Shows a window and disposes it afterwards (this also switches the camera off)
    void ShowForm(Form^ form)
    {
        form->ShowDialog();
        delete form;
    }

    void OnOrder(Object^ sender, EventArgs^ e)
    {
        try
        {
            ShowForm(gcnew OrderForm());
        }
        catch (Exception^ ex)
        {
            MessageBox::Show(ex->Message, "Cannot open order screen", MessageBoxButtons::OK, MessageBoxIcon::Warning);
        }
    }

    void OnProduct(Object^ sender, EventArgs^ e)
    {
        try
        {
            ShowForm(gcnew ProductForm());
        }
        catch (Exception^ ex)
        {
            MessageBox::Show(ex->Message, "Cannot open product screen", MessageBoxButtons::OK, MessageBoxIcon::Warning);
        }
    }

    void OnBarcode(Object^ sender, EventArgs^ e)
    {
        ShowForm(gcnew BarcodeForm());
    }
};

[STAThread]
int main(array<String^>^ args)
{
    Application::EnableVisualStyles();
    Application::SetCompatibleTextRenderingDefault(false);
    Application::Run(gcnew MenuForm());
    return 0;
}