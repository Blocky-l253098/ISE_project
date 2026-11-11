#using <System.dll>
#using <System.Drawing.dll>
#using <System.Windows.Forms.dll>

using namespace System;
using namespace System::Collections::Generic;
using namespace System::Drawing;
using namespace System::Drawing::Imaging;
using namespace System::IO;
using namespace System::Text;
using namespace System::Text::RegularExpressions;
using namespace System::Windows::Forms;

// ---------------------------------------------------------------------------
// Barcode logic (encode / decode / render / SVG IO)
// ---------------------------------------------------------------------------
public ref class Barcode
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

    Char DecodeCharacter(String^ pattern)
    {
        for (int i = 0; i < code->Length; i++)
        {
            if (code[i] == pattern)
                return (i == 10) ? L'*' : (Char)(L'0' + i);
        }
        return L'?';
    }

public:
    literal int BarHeight = 150;

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
    }

    // Payload -> pixel row (0/1 per pixel column)
    List<int>^ Encode(String^ payload)
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

    // Pixel row -> payload string (start/stop stripped)
    String^ Decode(List<int>^ row)
    {
        // Trim white margins
        int first = -1, last = -1;
        for (int i = 0; i < row->Count; i++)
        {
            if (row[i] == 1)
            {
                if (first < 0) first = i;
                last = i;
            }
        }
        if (first < 0)
            throw gcnew FormatException("No barcode found.");

        // Run-length encode bars and spaces
        List<int>^ runs = gcnew List<int>();
        int run = 1;
        for (int i = first + 1; i <= last; i++)
        {
            if (row[i] == row[i - 1]) run++;
            else { runs->Add(run); run = 1; }
        }
        runs->Add(run);

        // Each char = 9 elements + 1 gap, except the last char (no trailing gap)
        if ((runs->Count + 1) % 10 != 0)
            throw gcnew FormatException("Barcode structure is invalid (unexpected element count).");

        // Narrow/wide threshold
        int minW = runs[0], maxW = runs[0];
        for each(int w in runs)
        {
            if (w < minW) minW = w;
            if (w > maxW) maxW = w;
        }
        int threshold = (minW + maxW) / 2;

        StringBuilder^ decoded = gcnew StringBuilder();
        for (int i = 0; i + 8 < runs->Count; i += 10)
        {
            StringBuilder^ pattern = gcnew StringBuilder();
            for (int j = 0; j < 9; j++)
                pattern->Append(runs[i + j] > threshold ? L'1' : L'0');
            decoded->Append(DecodeCharacter(pattern->ToString()));
        }

        String^ s = decoded->ToString();
        if (s->Length < 2 || s[0] != L'*' || s[s->Length - 1] != L'*')
            throw gcnew FormatException("Missing start/stop markers.");

        return s->Substring(1, s->Length - 2);
    }
};

// ---------------------------------------------------------------------------
// Main window
// ---------------------------------------------------------------------------
public ref class MainForm : public Form
{
private:
    Barcode^ barcode;
    List<int>^ currentRow;

    Label^ lblPayload;
    TextBox^ txtPayload;
    Button^ btnPreview;
    Button^ btnSave;
    Button^ btnLoad;
    PictureBox^ pic;
    StatusStrip^ status;
    ToolStripStatusLabel^ lblStatus;

public:
    MainForm()
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
        btnPreview->Text = "Preview";
        btnPreview->Location = Point(280, 12);
        btnPreview->Size = Drawing::Size(90, 27);
        btnPreview->Click += gcnew EventHandler(this, &MainForm::OnPreview);

        btnSave = gcnew Button();
        btnSave->Text = "Save...";
        btnSave->Location = Point(378, 12);
        btnSave->Size = Drawing::Size(90, 27);
        btnSave->Click += gcnew EventHandler(this, &MainForm::OnSave);

        btnLoad = gcnew Button();
        btnLoad->Text = "Load...";
        btnLoad->Location = Point(476, 12);
        btnLoad->Size = Drawing::Size(90, 27);
        btnLoad->Click += gcnew EventHandler(this, &MainForm::OnLoad);

        pic = gcnew PictureBox();
        pic->Location = Point(12, 52);
        pic->Size = Drawing::Size(696, 330);
        pic->Anchor = AnchorStyles::Top | AnchorStyles::Bottom | AnchorStyles::Left | AnchorStyles::Right;
        pic->BorderStyle = BorderStyle::FixedSingle;
        pic->BackColor = Color::White;
        pic->SizeMode = PictureBoxSizeMode::Zoom;

        status = gcnew StatusStrip();
        lblStatus = gcnew ToolStripStatusLabel("Enter digits and click Preview, or load a saved barcode.");
        status->Items->Add(lblStatus);

        this->Controls->Add(lblPayload);
        this->Controls->Add(txtPayload);
        this->Controls->Add(btnPreview);
        this->Controls->Add(btnSave);
        this->Controls->Add(btnLoad);
        this->Controls->Add(pic);
        this->Controls->Add(status);

        this->AcceptButton = btnPreview;
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

    void OnPreview(Object^ sender, EventArgs^ e)
    {
        try
        {
            currentRow = barcode->Encode(txtPayload->Text->Trim());
            ShowRow(currentRow);
            SetStatus("Previewing: " + txtPayload->Text->Trim());
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
            currentRow = barcode->Encode(txtPayload->Text->Trim());
            ShowRow(currentRow);

            SaveFileDialog^ dlg = gcnew SaveFileDialog();
            dlg->Filter = "SVG barcode (*.svg)|*.svg|PNG image (*.png)|*.png";
            dlg->FileName = "barcode";
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
        dlg->Filter = "SVG barcode (*.svg)|*.svg";

        if (dlg->ShowDialog() != System::Windows::Forms::DialogResult::OK)
            return;

        try
        {
            List<int>^ row = barcode->LoadSvg(dlg->FileName);
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



[STAThread]
int main(array<String^>^ args)
{
    Application::EnableVisualStyles();
    Application::SetCompatibleTextRenderingDefault(false);
    Application::Run(gcnew MainForm());
    return 0;
}