#pragma once

// =============================================================================
// Pdf.h - saves text as a PDF file (bills and reports), no external library
//   IPdfExporter       what the screens need
//   SimplePdfExporter  writes A4 pages in a fixed-width font (Courier)
//   Depends on         Common.h
// =============================================================================

#include "Common.h"

public interface class IPdfExporter
{
    void Export(String^ path, String^ text);
};

public ref class SimplePdfExporter : public IPdfExporter
{
private:
    literal double PageWidth = 595;         // A4 in points
    literal double PageHeight = 842;
    literal double Margin = 40;
    literal double MaxFont = 10;
    literal double MinFont = 5;
    literal double CharWidth = 0.6;         // Courier: every character is 0.6 x the font size
    literal double LineSpacing = 1.25;

    // Only plain ASCII goes into the page; ( ) and \ must be escaped in a PDF string
    String^ Escape(String^ line)
    {
        StringBuilder^ sb = gcnew StringBuilder();
        for each(Char c in line)
        {
            if (c < 32 || c > 126)
                sb->Append(L'?');
            else if (c == L'\\' || c == L'(' || c == L')')
                sb->Append(L'\\')->Append(c);
            else
                sb->Append(c);
        }
        return sb->ToString();
    }

    // Appends "n 0 obj ... endobj" and remembers where the object starts
    void AddObject(StringBuilder^ pdf, List<int>^ offsets, String^ body)
    {
        offsets->Add(pdf->Length);
        pdf->Append(Convert::ToString(offsets->Count) + " 0 obj\n" + body + "\nendobj\n");
    }

    // The drawing commands of one page: the lines from 'first' to 'last'
    String^ PageContent(array<String^>^ lines, int first, int last, double fontSize)
    {
        double leading = fontSize * LineSpacing;

        StringBuilder^ content = gcnew StringBuilder();
        content->Append("BT\n/F1 " + Fmt::Number(fontSize) + " Tf\n" + Fmt::Number(leading) + " TL\n");
        content->Append(Fmt::Number(Margin) + " " + Fmt::Number(PageHeight - Margin - fontSize) + " Td\n");
        for (int i = first; i < last; i++)
            content->Append("(" + Escape(lines[i]) + ") Tj T*\n");
        content->Append("ET");
        return content->ToString();
    }

public:
    virtual void Export(String^ path, String^ text)
    {
        array<String^>^ lines = text->Replace("\r\n", "\n")->Split(gcnew array<Char>{ L'\n' });

        // the longest line decides the font size, so nothing is cut at the right edge
        int longest = 1;
        for each(String ^ line in lines)
        {
            if (line->Length > longest)
                longest = line->Length;
        }
        double fontSize = Math::Max(MinFont, Math::Min(MaxFont, (PageWidth - 2 * Margin) / (CharWidth * longest)));

        int linesPerPage = (int)((PageHeight - 2 * Margin) / (fontSize * LineSpacing));
        int pageCount = (lines->Length + linesPerPage - 1) / linesPerPage;
        if (pageCount < 1)
            pageCount = 1;

        StringBuilder^ pdf = gcnew StringBuilder();
        List<int>^ offsets = gcnew List<int>();
        pdf->Append("%PDF-1.4\n");

        // object 1 = catalog, 2 = page list, 3 = font, then page and content of every page
        AddObject(pdf, offsets, "<< /Type /Catalog /Pages 2 0 R >>");

        StringBuilder^ kids = gcnew StringBuilder();
        for (int p = 0; p < pageCount; p++)
            kids->Append(Convert::ToString(4 + 2 * p) + " 0 R ");
        AddObject(pdf, offsets, "<< /Type /Pages /Kids [ " + kids->ToString() + "] /Count " + Convert::ToString(pageCount) + " >>");

        AddObject(pdf, offsets, "<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>");

        for (int p = 0; p < pageCount; p++)
        {
            int first = p * linesPerPage;
            int last = Math::Min(lines->Length, first + linesPerPage);
            String^ content = PageContent(lines, first, last, fontSize);

            AddObject(pdf, offsets, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] " +
                "/Resources << /Font << /F1 3 0 R >> >> /Contents " + Convert::ToString(5 + 2 * p) + " 0 R >>");
            AddObject(pdf, offsets, "<< /Length " + Convert::ToString(content->Length) + " >>\nstream\n" +
                content + "\nendstream");
        }

        // cross reference table: every entry is exactly 20 bytes
        int xrefStart = pdf->Length;
        int size = offsets->Count + 1;
        pdf->Append("xref\n0 " + Convert::ToString(size) + "\n");
        pdf->Append("0000000000 65535 f \n");
        for each(int offset in offsets)
            pdf->Append(offset.ToString("D10") + " 00000 n \n");
        pdf->Append("trailer\n<< /Size " + Convert::ToString(size) + " /Root 1 0 R >>\nstartxref\n" +
            Convert::ToString(xrefStart) + "\n%%EOF\n");

        File::WriteAllBytes(path, Encoding::ASCII->GetBytes(pdf->ToString()));
    }
};
